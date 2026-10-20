#include "NoteGestures.hpp"

#include "../../model/NoteOperators.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../interaction/Navigation.hpp"
#include "../interaction/Snap.hpp"
#include "../views/PianoRoll.hpp"
#include "../views/TimelineDraw.hpp"
#include "NoteHitTest.hpp"

#include <cctype>
#include <cmath>
#include <map>

namespace tessera::noteGestures {

using edits::IdSet;
using edits::NoteList;
using rack::math::Vec;

namespace {

constexpr double kClickSlopPx = 3;
constexpr double kVelocityPerPx = 1.0 / 150;
constexpr double kFineFactor = 0.1;

std::string beatsText(Tick ticks, TimeSig ts) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f beats", double(ticks) / double(ticksPerBeat(ts)));
    return buf;
}

std::string countText(const char* verb, size_t n) { return std::string(verb) + " " + countNotes(n); }

const Note* findNote(const NoteList& notes, NoteId id) {
    for (const Note& n : notes)
        if (n.id == id)
            return &n;
    return nullptr;
}

/// Where a tick falls across a note, 0..1 (clamped to the note).
float posAt(const Note& n, double tick) {
    return n.length > 0 ? std::clamp(float((tick - double(n.start)) / double(n.length)), 0.f, 1.f) : 0.f;
}

/// IDs for notes a gesture creates. A gesture rebuilds its preview many times; the pool hands out the same
/// IDs each time, so previews never use up fresh IDs.
class IdPool {
  public:
    explicit IdPool(PianoRollView& view) : view(view) {}
    void restart() { next = 0; }
    NoteId take() {
        if (next == ids.size())
            ids.push_back(view.newNoteId());
        return ids[next++];
    }
    edits::IdSource source() {
        return [this] { return take(); };
    }

  private:
    PianoRollView& view;
    std::vector<NoteId> ids;
    size_t next = 0;
};

/// What every note gesture shares: the starting notes and the press point in content terms (tick and row),
/// so edge auto-scroll never shifts the gesture's origin.
class NoteGesture : public DragGesture {
  public:
    NoteGesture(PianoRollView& view, Vec press)
        : view(view), before(view.committedNotes()), pressTick(view.viewport.time.xToTick(press.x)),
          pressRow(rowAt(view, press.y)) {}

    void cancel() override { view.clearPreview(); }
    bool autoScrollX() const override { return true; }
    bool autoScrollY() const override { return true; }

  protected:
    static double rowAt(PianoRollView& v, double y) { return v.pitch.top() + y / v.pitch.rowHeight(); }
    double tickAt(Vec p) { return view.viewport.time.xToTick(p.x); }
    double rowAt(double y) { return rowAt(view, y); }
    /// The pitch of the row under y (a shown row when folded).
    int pitchAt(double y) { return view.pitch.pitchAtRow(int(std::floor(rowAt(y)))); }
    /// The press point in the view's current coordinates (it moves when the view scrolls).
    Vec pressPos() {
        return Vec(float(view.viewport.time.tickToX(pressTick)),
                   float((pressRow - view.pitch.top()) * view.pitch.rowHeight()));
    }
    Tick step(bool free) { return free ? 0 : view.snapStep(); }
    EditorState& state() { return view.context().state(); }

    PianoRollView& view;
    NoteList before;
    double pressTick;
    double pressRow;
};

// ---- Move ---------------------------------------------------------------------------------------------------

class MoveGesture : public NoteGesture {
  public:
    MoveGesture(PianoRollView& view, Vec press, NoteId grabbed)
        : NoteGesture(view, press), ids(view.selectedIds()), pool(view) {
        if (const Note* n = findNote(before, grabbed))
            anchor = *n;
        anchorPitch = anchor.pitch;
    }

    void update(Vec pos, Modifiers mods) override {
        double dt = tickAt(pos) - pressTick;
        double rows = std::round(rowAt(pos.y) - pressRow);
        // Shift locks the drag to the axis it started along (EDITOR_UX §2).
        double dx = dt * view.viewport.time.pxPerTick(), dy = rows * view.pitch.rowHeight();
        lock.apply(dx, dy, mods.shift);
        if (dx == 0)
            dt = 0;
        if (dy == 0)
            rows = 0;

        delta = snapMoveDelta(anchor.start, dt, step(mods.alt));
        delta = std::max(delta, -anchor.start);
        // With scale snap a row is a key step; Alt allows any note (EDITOR_UX §4.11).
        chromatic = mods.alt;
        int newPitch = view.stepPitch(anchor.pitch, int(rows), chromatic);
        if (newPitch != anchorPitch)
            view.audition(newPitch, anchor.velocity);
        anchorPitch = newPitch;
        moved = rows != 0;
        // Once Ctrl is down the drag stays a copy: letting go of Ctrl just before the mouse must not move the original.
        copying = copying || mods.ctrl;

        NoteList after;
        IdSet changed;
        if (copying) {
            pool.restart();
            IdSet copies;
            after = edits::duplicate(before, ids, delta, pool.source(), &copies);
            changed = copies;
        } else {
            after = edits::move(before, ids, delta, 0);
            changed = ids;
        }
        after = view.stepPitches(after, changed, int(rows), chromatic);
        result = edits::resolveOverlaps(after, changed);
        resultIds = changed;
        view.setPreview(result);
        view.context().snapMark = double(anchor.start + delta);
        view.context().snapAxis = &view.viewport;
    }

    void commit() override {
        view.clearPreview();
        if (!copying && delta == 0 && !moved)
            return;
        view.commit(countText(copying ? "Copy" : "Move", ids.size()), before, result, resultIds);
    }

    std::string readout() const override {
        TimeSig ts = view.source.timeSig();
        std::string text = timeline::positionText(double(anchor.start + delta), ts) + "  " + pitchName(anchorPitch);
        return copying ? "Copy  " + text : text;
    }
    CursorShape cursor() const override { return CursorShape::Move; }

  private:
    IdSet ids;
    Note anchor;
    AxisLock lock;
    IdPool pool;
    Tick delta = 0;
    int anchorPitch = 0;
    bool moved = false;
    bool chromatic = false;
    bool copying = false;
    NoteList result;
    IdSet resultIds;
};

// ---- Resize and stretch -------------------------------------------------------------------------------------

class ResizeGesture : public NoteGesture {
  public:
    ResizeGesture(PianoRollView& view, Vec press, NoteId grabbed, bool startEdge, bool stretch)
        : NoteGesture(view, press), ids(view.selectedIds()), startEdge(startEdge), stretch(stretch) {
        if (const Note* n = findNote(before, grabbed))
            anchor = *n;
        span = edits::span(before, ids);
    }

    void update(Vec pos, Modifiers mods) override {
        Tick s = step(mods.alt);
        Tick edge = startEdge ? anchor.start : anchor.end();
        Tick target = snapPosition(double(edge) + tickAt(pos) - pressTick, s);
        view.context().snapMark = double(target);
        view.context().snapAxis = &view.viewport;
        NoteList after;
        if (stretch) {
            // Scale everything from the selection's opposite edge.
            Tick pivot = startEdge ? span.second : span.first;
            double from = double(edge - pivot);
            double to = double(target - pivot);
            factor = std::fabs(from) > 0 && to / from > 0.01 ? to / from : 0.01;
            after = edits::stretchTime(before, ids, pivot, factor);
        } else {
            Tick minLength = s > 0 ? s : 1;
            after = startEdge ? edits::resizeStart(before, ids, target - edge, minLength)
                              : edits::resizeEnd(before, ids, target - edge, minLength);
        }
        result = edits::resolveOverlaps(after, ids);
        view.setPreview(result);
        if (const Note* n = findNote(result, anchor.id))
            length = n->length;
    }

    void commit() override {
        view.clearPreview();
        if (length == anchor.length && (!stretch || factor == 1))
            return;
        view.commit(countText(stretch ? "Stretch" : "Resize", ids.size()), before, result);
        // The Draw tool reuses the length of the note you last shaped.
        if (ids.size() == 1)
            state().lastLength = length;
    }

    std::string readout() const override {
        if (stretch) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Stretch ×%.2f", factor);
            return buf;
        }
        return "Length " + beatsText(length, view.source.timeSig());
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }
    bool autoScrollY() const override { return false; }

  private:
    IdSet ids;
    Note anchor;
    std::pair<Tick, Tick> span;
    bool startEdge;
    bool stretch;
    double factor = 1;
    Tick length = 0;
    NoteList result;
};

// ---- Draw ---------------------------------------------------------------------------------------------------

class DrawGesture : public NoteGesture {
  public:
    DrawGesture(PianoRollView& view, Vec press) : NoteGesture(view, press), pool(view) {
        Tick s = view.snapStep();
        // A click places the note on the grid line at or before the pointer.
        start = s > 0 ? Tick(std::floor(pressTick / double(s))) * s : std::max<Tick>(0, Tick(pressTick));
        start = std::max<Tick>(0, start);
        length = state().lastLength;
        startLength = length;
        channel = view.drawChannel();
        root = view.placePitch(pitchAt(press.y), false);
        auditionChord();
    }

    void update(Vec pos, Modifiers mods) override {
        Tick s = step(mods.alt);
        if (std::fabs(pos.x - pressPos().x) > kClickSlopPx) {
            Tick end = snapPosition(tickAt(pos), s);
            length = std::max<Tick>(s > 0 ? s : 1, end - start);
        }
        int p = view.placePitch(pitchAt(pos.y), mods.alt);
        if (p != root) {
            root = p;
            auditionChord();
        }
        pool.restart();
        ids.clear();
        NoteList after = before;
        for (int pitch : view.stampPitches(root)) {
            Note n;
            n.id = pool.take();
            n.start = start;
            n.length = length;
            n.velocity = state().lastVelocity;
            n.channel = channel;
            n.pitch = int8_t(pitch);
            after.push_back(n);
            ids.insert(n.id);
        }
        std::sort(after.begin(), after.end(), noteOrder);
        result = edits::resolveOverlaps(after, ids);
        view.setPreview(result);
    }

    void commit() override {
        view.clearPreview();
        view.commit(ids.size() > 1 ? "Draw chord" : "Draw note", before, result, ids);
        if (length != startLength)
            state().lastLength = length;
        state().lastChannel = channel;
    }

    std::string readout() const override {
        std::string name = pitchName(root);
        const ChordStamp& chord = view.context().state().chord;
        if (ids.size() > 1)
            name += " " + std::string(chordShapeName(chord.shape));
        return name + "  " + timeline::positionText(double(start), view.source.timeSig()) + "  " +
               beatsText(length, view.source.timeSig());
    }
    CursorShape cursor() const override { return CursorShape::Crosshair; }

  private:
    void auditionChord() {
        for (int p : view.stampPitches(root))
            view.audition(p, state().lastVelocity);
    }

    IdPool pool;
    Tick start = 0;
    Tick length = 0;
    Tick startLength = 0;
    uint8_t channel = 1;
    int root = 60;
    IdSet ids;
    NoteList result;
};

// ---- Paint --------------------------------------------------------------------------------------------------

class PaintGesture : public NoteGesture {
  public:
    PaintGesture(PianoRollView& view, Vec press) : NoteGesture(view, press), pool(view), channel(view.drawChannel()) {
        firstPitch = view.placePitch(pitchAt(press.y), false);
    }

    void update(Vec pos, Modifiers mods) override {
        Tick s = view.snapStep();
        if (s <= 0)
            s = kPpq / 4;
        Tick cell = std::max<Tick>(0, Tick(std::floor(tickAt(pos) / double(s))) * s);
        int p = mods.shift ? firstPitch : view.placePitch(pitchAt(pos.y), mods.alt);
        // Fill every step between the last cell and this one, so fast strokes leave no gaps.
        if (lastCell < 0)
            lastCell = cell;
        Tick lo = std::min(lastCell, cell), hi = std::max(lastCell, cell);
        for (Tick c = lo; c <= hi; c += s) {
            if (!cells.count(c)) {
                cells[c] = p;
                view.audition(p, state().lastVelocity);
            }
        }
        cells[cell] = p; // the cell under the pointer follows the pointer's pitch
        lastCell = cell;

        pool.restart();
        NoteList after = before;
        IdSet ids;
        for (const auto& [start, cellPitch] : cells) {
            Note n;
            n.id = pool.take();
            n.start = start;
            n.length = s;
            n.pitch = int8_t(cellPitch);
            n.velocity = state().lastVelocity;
            n.channel = channel;
            after.push_back(n);
            ids.insert(n.id);
        }
        std::sort(after.begin(), after.end(), noteOrder);
        result = edits::resolveOverlaps(after, ids);
        resultIds = ids;
        view.setPreview(result);
    }

    void commit() override {
        view.clearPreview();
        if (!resultIds.empty())
            view.commit(countText("Paint", resultIds.size()), before, result, resultIds);
    }
    std::string readout() const override { return countNotes(cells.size()); }
    CursorShape cursor() const override { return CursorShape::Crosshair; }

  private:
    IdPool pool;
    uint8_t channel;
    int firstPitch;
    std::map<Tick, int> cells;
    Tick lastCell = -1;
    NoteList result;
    IdSet resultIds;
};

// ---- Erase and mute: everything the pointer passes over -----------------------------------------------------

class SweepGesture : public NoteGesture {
  public:
    SweepGesture(PianoRollView& view, Vec press, bool muting) : NoteGesture(view, press), muting(muting) {}

    void update(Vec pos, Modifiers mods) override {
        Vec from = hasLast ? last : pressPos();
        IdSet hit = notesAlongPath(before, view.viewport.time, view.pitch, from.x, from.y, pos.x, pos.y);
        if (muting && touched.empty() && !hit.empty()) {
            // The first note decides: mute everything if it was playing, unmute if it was muted.
            for (const Note& n : before)
                if (hit.count(n.id)) {
                    targetMuted = !n.muted;
                    break;
                }
        }
        touched.insert(hit.begin(), hit.end());
        last = pos;
        hasLast = true;
        if (muting) {
            view.setPreview(edits::setMuted(before, touched, targetMuted));
        } else {
            NoteList after;
            for (const Note& n : before)
                if (!touched.count(n.id))
                    after.push_back(n);
            view.setPreview(after);
        }
    }

    void commit() override {
        NoteList after = view.notes();
        view.clearPreview();
        if (touched.empty())
            return;
        std::string verb = muting ? (targetMuted ? "Mute" : "Unmute") : "Erase";
        view.commit(countText(verb.c_str(), touched.size()), before, after);
    }
    std::string readout() const override { return touched.empty() ? "" : countNotes(touched.size()); }
    CursorShape cursor() const override { return muting ? CursorShape::Hand : CursorShape::NotAllowed; }

  private:
    bool muting;
    bool targetMuted = true;
    IdSet touched;
    Vec last;
    bool hasLast = false;
};

// ---- Slice --------------------------------------------------------------------------------------------------

class SliceGesture : public NoteGesture {
  public:
    SliceGesture(PianoRollView& view, Vec press) : NoteGesture(view, press), pool(view) {}

    void update(Vec pos, Modifiers mods) override {
        current = pos;
        Tick s = step(mods.alt);
        Vec p0 = pressPos();
        std::vector<std::pair<NoteId, double>> cuts;
        if (pos.minus(p0).norm() <= kClickSlopPx) {
            // A click: cut the note under the pointer at the (snapped) pointer time.
            NoteHit hit = hitTestNotes(before, view.viewport.time, view.pitch, p0.x, p0.y, IdSet{}, 0);
            if (hit.any())
                cuts.push_back({hit.id, pressTick});
            line = false;
        } else {
            cuts = sliceLineCuts(before, view.viewport.time, view.pitch, p0.x, p0.y, pos.x, pos.y);
            line = true;
        }
        pool.restart();
        NoteList after = before;
        sliced = 0;
        for (const auto& [id, tick] : cuts) {
            size_t count = after.size();
            after = edits::slice(after, id, snapPosition(tick, s), pool.source());
            if (after.size() > count)
                sliced++;
        }
        guideTick = snapPosition(pressTick, s);
        result = after;
        view.setPreview(result);
    }

    void commit() override {
        view.clearPreview();
        if (sliced > 0)
            view.commit(countText("Slice", sliced), before, result);
    }

    void drawOverlay(NVGcontext* vg) override {
        const theme::Palette& c = theme::colors();
        nvgBeginPath(vg);
        if (line) {
            Vec p0 = pressPos();
            nvgMoveTo(vg, p0.x, p0.y);
            nvgLineTo(vg, current.x, current.y);
        } else {
            float x = float(view.viewport.time.tickToX(double(guideTick)));
            nvgMoveTo(vg, x, 0);
            nvgLineTo(vg, x, 10000);
        }
        nvgStrokeColor(vg, c.accent);
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
    std::string readout() const override { return sliced ? countText("Slice", sliced) : ""; }
    CursorShape cursor() const override { return CursorShape::IBeam; }

  private:
    IdPool pool;
    Vec current;
    bool line = false;
    int sliced = 0;
    Tick guideTick = 0;
    NoteList result;
};

// ---- Velocity -----------------------------------------------------------------------------------------------

class VelocityGesture : public NoteGesture {
  public:
    VelocityGesture(PianoRollView& view, Vec press, NoteId grabbed) : NoteGesture(view, press), pressY(press.y) {
        ids = view.isSelected(grabbed) ? view.selectedIds() : IdSet{grabbed};
        if (const Note* n = findNote(before, grabbed))
            anchor = *n;
        // The changing values stay on the notes, not only in the pill by the cursor.
        view.velocityShown = ids;
    }
    ~VelocityGesture() override { view.velocityShown.clear(); }

    void update(Vec pos, Modifiers mods) override {
        double dv = (pressY - pos.y) * kVelocityPerPx * (mods.shift ? kFineFactor : 1.0);
        result = edits::changeVelocity(before, ids, float(dv));
        view.setPreview(result);
        if (const Note* n = findNote(result, anchor.id))
            value = n->velocity;
    }

    void commit() override {
        view.clearPreview();
        if (value == anchor.velocity)
            return;
        view.commit(ids.size() == 1 ? "Change velocity" : "Change velocity of " + countNotes(ids.size()), before,
                    result);
        state().lastVelocity = value;
    }
    std::string readout() const override { return "Velocity " + std::to_string(int(std::lround(value * 100))) + "%"; }
    CursorShape cursor() const override { return CursorShape::ResizeV; }
    bool autoScrollX() const override { return false; }
    bool autoScrollY() const override { return false; }

  private:
    float pressY;
    IdSet ids;
    Note anchor;
    float value = 0;
    NoteList result;
};

// ---- Bend ---------------------------------------------------------------------------------------------------

class BendGesture : public NoteGesture {
  public:
    /// `valueAt` turns a y into the curve's units: a pitch for the Pitch curve (so a drag moves by rows), else
    /// the value at that height of the strip the curve is drawn in.
    BendGesture(PianoRollView& view, Vec press, const BendHit& hit, expr::Curve curve,
                std::function<double(double)> valueAt)
        : NoteGesture(view, press), curve(curve), valueAt(std::move(valueAt)), pressValue(this->valueAt(press.y)),
          base(before), grabbedPoint(hit.point >= 0), originalPoints(view.curvePoints),
          originalCurve(view.pointsCurve) {
        if (grabbedPoint && pickGroup(hit))
            return;
        float rest = expr::info(curve).rest;
        IdSet ids = view.isSelected(hit.id) ? view.selectedIds() : IdSet{hit.id};
        double hitTick = 0;
        if (const Note* grabbed = findNote(before, hit.id))
            hitTick = double(grabbed->start) + double(hit.pos) * double(grabbed->length);
        for (Note& n : base) {
            if (!ids.count(n.id))
                continue;
            std::vector<ExprPoint>& points = expr::points(n, curve);
            // The other notes of a chord bend at the same beat, so they glide together whatever their lengths.
            float at = n.id == hit.id ? hit.pos : posAt(n, hitTick);
            int index = n.id == hit.id ? hit.point : -1;
            if (index < 0) {
                for (size_t i = 0; i < points.size(); i++)
                    if (std::fabs(points[i].pos - at) * float(n.length) <= kSameTicks &&
                        (index < 0 || std::fabs(points[i].pos - at) < std::fabs(points[size_t(index)].pos - at)))
                        index = int(i);
            }
            if (index < 0)
                index = int(expr::addPoint(points, at, expressionAt(points, at, rest), rest));
            const ExprPoint& p = points[size_t(index)];
            bent.push_back({n.id, size_t(index), p.pos, p.value});
            if (n.id == hit.id) {
                anchor = n;
                anchorIndex = bent.size() - 1;
            }
        }
        result = base;
        shown = bent.empty() ? 0 : bent[anchorIndex].value;
    }

    void update(Vec pos, Modifiers mods) override {
        lastMods = mods;
        if (bent.empty())
            return;
        const Bent& a = bent[anchorIndex];
        double moved = valueAt(pos.y) - pressValue;
        // Time: a small sideways wobble keeps the point where it is; Shift keeps it there whatever happens.
        double dx = pos.x - pressPos().x;
        double newTick = anchorTick(a.pos);
        if (!mods.shift && std::fabs(dx) > kTimeDeadPx) {
            newTick = anchorTick(a.pos) + (tickAt(pos) - pressTick);
            if (!mods.alt)
                newTick = double(snapPosition(newTick, view.snapStep()));
        }
        double dt = newTick - anchorTick(a.pos);

        // Pitch: free with Alt, else whole semitones, or steps of the key with scale snap.
        bool pitch = curve == expr::Curve::Pitch;
        double anchorTarget = anchor.pitch + double(a.value);
        Scale key = view.scale();
        bool inKey = pitch && view.snapToKey() && !mods.alt;
        int steps = 0;
        double shift = moved;
        if (inKey) {
            int from = key.snap(int(std::lround(anchorTarget)));
            int to = key.snap(int(std::lround(anchorTarget + moved)));
            for (int p = from; p != to && std::abs(steps) < kMaxSteps;) {
                int dir = to > p ? 1 : -1;
                int next = key.stepBy(p, dir);
                if (next == p)
                    break;
                steps += dir;
                p = next;
            }
        } else if (pitch && !mods.alt) {
            shift = std::round(anchorTarget + moved) - anchorTarget;
        }

        const expr::Info& range = expr::info(curve);
        result = base;
        // Several picked points on one note: move the one in front first, so none is held back by a neighbour
        // that is about to move out of its way.
        std::vector<const Bent*> order;
        for (const Bent& b : bent)
            order.push_back(&b);
        std::sort(order.begin(), order.end(), [dt](const Bent* x, const Bent* y) {
            if (x->id != y->id)
                return x->id.value < y->id.value;
            return dt > 0 ? x->index > y->index : x->index < y->index;
        });
        for (const Bent* bp : order)
            for (Note& n : result) {
                const Bent& b = *bp;
                if (n.id != b.id)
                    continue;
                float value = b.value + float(shift);
                if (inKey)
                    value = steps == 0
                                ? b.value
                                : float(key.stepBy(key.snap(int(std::lround(n.pitch + b.value))), steps) - n.pitch);
                value = std::clamp(value, range.min, range.max);
                float at = std::clamp(b.pos + float(dt / double(std::max<Tick>(1, n.length))), 0.f, 1.f);
                expr::movePoint(expr::points(n, curve), b.index, at, value);
            }
        view.setPreview(result);
        if (const Note* n = findNote(result, anchor.id))
            shown = expr::points(*n, curve)[a.index].value;
        if (group)
            view.curvePoints = currentRefs();
    }

    void commit() override {
        view.clearPreview();
        if (bent.empty())
            return;
        if (result == before) {
            // A click on a point picks it; with Shift it is added to (or taken from) the picked points.
            if (!grabbedPoint)
                return;
            expr::PointRef ref = currentRefs()[anchorIndex];
            std::vector<expr::PointRef> next;
            if (lastMods.shift && originalCurve == curve) {
                next = originalPoints;
                auto it = std::find(next.begin(), next.end(), ref);
                if (it != next.end())
                    next.erase(it);
                else
                    next.push_back(ref);
            } else {
                next = {ref};
            }
            view.curvePoints = next;
            view.pointsCurve = curve;
            return;
        }
        view.curvePoints = currentRefs();
        view.pointsCurve = curve;
        std::string what = curve == expr::Curve::Pitch ? "bend" : lowerName();
        if (result == base)
            view.commit(bent.size() == 1 ? "Add " + what + " point" : "Add " + what + " points", before, result);
        else if (curve == expr::Curve::Pitch)
            view.commit(bent.size() == 1 ? "Bend note" : "Bend " + countNotes(bent.size()), before, result);
        else
            view.commit("Shape " + what + (bent.size() == 1 ? "" : " of " + countNotes(bent.size())), before, result);
    }
    std::string readout() const override {
        if (bent.empty())
            return {};
        char buf[64];
        if (curve != expr::Curve::Pitch) {
            snprintf(buf, sizeof(buf), "%s %d%%", expr::info(curve).name, int(std::lround(shown * 100)));
            return buf;
        }
        double target = anchor.pitch + double(shown);
        if (std::fabs(target - std::round(target)) < 0.005)
            snprintf(buf, sizeof(buf), "%s  %+d semitones", pitchName(int(std::lround(target))).c_str(),
                     int(std::lround(shown)));
        else
            snprintf(buf, sizeof(buf), "%+.2f semitones", double(shown));
        return buf;
    }
    void cancel() override {
        view.clearPreview();
        view.curvePoints = originalPoints;
        view.pointsCurve = originalCurve;
    }
    CursorShape cursor() const override { return CursorShape::Move; }
    // A strip sits on its notes, so scrolling would only carry the points away from the pointer.
    bool autoScrollX() const override { return curve == expr::Curve::Pitch; }
    bool autoScrollY() const override { return curve == expr::Curve::Pitch; }

  private:
    struct Bent {
        NoteId id;
        size_t index;
        float pos, value;
    };
    static constexpr float kSameTicks = kPpq / 16; // another note's point this close to the beat is the one to move
    static constexpr double kTimeDeadPx = 4;
    static constexpr int kMaxSteps = 48;

    double anchorTick(float pos) const { return double(anchor.start) + double(pos) * double(anchor.length); }

    /// A grabbed point that is one of the picked points takes all of them along. False when it isn't picked.
    bool pickGroup(const BendHit& hit) {
        const Note* grabbed = findNote(base, hit.id);
        const std::vector<ExprPoint>* grabbedCurve = grabbed ? &expr::points(*grabbed, curve) : nullptr;
        if (!grabbedCurve || size_t(hit.point) >= grabbedCurve->size() ||
            !view.pointPicked(hit.id, curve, (*grabbedCurve)[size_t(hit.point)]))
            return false;
        for (const expr::PointRef& r : view.curvePoints) {
            const Note* n = findNote(base, r.note);
            int i = n ? expr::findPoint(expr::points(*n, curve), r.pos, r.value) : -1;
            if (i < 0)
                continue;
            bent.push_back({r.note, size_t(i), r.pos, r.value});
            if (r.note == hit.id && i == hit.point) {
                anchor = *n;
                anchorIndex = bent.size() - 1;
            }
        }
        group = true;
        result = base;
        shown = bent.empty() ? 0 : bent[anchorIndex].value;
        return true;
    }
    /// The moved points where they are now, to keep them picked.
    std::vector<expr::PointRef> currentRefs() const {
        std::vector<expr::PointRef> refs;
        for (const Bent& b : bent)
            if (const Note* n = findNote(result, b.id)) {
                const std::vector<ExprPoint>& points = expr::points(*n, curve);
                if (b.index < points.size())
                    refs.push_back({b.id, points[b.index].pos, points[b.index].value});
            }
        return refs;
    }
    std::string lowerName() const {
        std::string s = expr::info(curve).name;
        s[0] = char(std::tolower(static_cast<unsigned char>(s[0])));
        return s;
    }

    expr::Curve curve;
    std::function<double(double)> valueAt;
    double pressValue;
    NoteList base; // the notes with any new points added
    std::vector<Bent> bent;
    size_t anchorIndex = 0;
    Note anchor;
    float shown = 0; // the grabbed point's value
    NoteList result;
    bool grabbedPoint;  // the press was on an existing point (a click picks it)
    bool group = false; // moving all the picked points
    std::vector<expr::PointRef> originalPoints;
    expr::Curve originalCurve;
    Modifiers lastMods;
};

// ---- Arch a curve segment -----------------------------------------------------------------------------------

class ArchGesture : public NoteGesture {
  public:
    ArchGesture(PianoRollView& view, Vec press, const BendHit& hit, expr::Curve curve)
        : NoteGesture(view, press), curve(curve), pressY(press.y) {
        IdSet ids = view.isSelected(hit.id) ? view.selectedIds() : IdSet{hit.id};
        double hitTick = 0;
        if (const Note* grabbed = findNote(before, hit.id))
            hitTick = double(grabbed->start) + double(hit.pos) * double(grabbed->length);
        for (const Note& n : before) {
            if (!ids.count(n.id))
                continue;
            const std::vector<ExprPoint>& points = expr::points(n, curve);
            // The other notes of a chord arch the segment under the same beat, so they glide alike.
            int i = n.id == hit.id ? hit.point : segmentAt(points, posAt(n, hitTick));
            if (i < 0 || size_t(i) + 1 >= points.size() || points[size_t(i)].value == points[size_t(i) + 1].value)
                continue;
            // Positive tension lifts a rising segment, so dragging up bends toward the pointer either way.
            float direction = points[size_t(i) + 1].value > points[size_t(i)].value ? 1.f : -1.f;
            arched.push_back({n.id, size_t(i), points[size_t(i)].tension, direction});
            if (n.id == hit.id)
                anchor = arched.size() - 1;
        }
        result = before;
    }

    void update(Vec pos, Modifiers mods) override {
        float delta = float(pressY - pos.y) * kTensionPerPx * (mods.shift ? float(kFineFactor) : 1.f);
        result = before;
        for (const Arched& a : arched)
            for (Note& n : result)
                if (n.id == a.id) {
                    float t = std::clamp(a.tension + delta * a.direction, -1.f, 1.f);
                    expr::points(n, curve)[a.index].tension = t;
                    if (&a == &arched[anchor])
                        shown = t;
                }
        view.setPreview(result);
    }

    void commit() override {
        view.clearPreview();
        if (arched.empty() || result == before)
            return;
        std::string what = curve == expr::Curve::Pitch ? "bend" : expr::info(curve).name;
        if (curve != expr::Curve::Pitch)
            what[0] = char(std::tolower(static_cast<unsigned char>(what[0])));
        view.commit("Arch " + what + (arched.size() == 1 ? "" : " of " + countNotes(arched.size())), before, result);
    }
    std::string readout() const override {
        if (arched.empty())
            return {};
        char text[32];
        snprintf(text, sizeof(text), "Curve %+.0f%%", double(shown) * 100);
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeV; }
    bool autoScrollX() const override { return false; }
    bool autoScrollY() const override { return false; }

  private:
    struct Arched {
        NoteId id;
        size_t index;
        float tension, direction;
    };
    static constexpr float kTensionPerPx = 1.f / 60;

    expr::Curve curve;
    double pressY;
    std::vector<Arched> arched;
    size_t anchor = 0;
    float shown = 0;
    NoteList result;
};

// ---- Picking curve points ---------------------------------------------------------------------------------

class PickPointsGesture : public NoteGesture {
  public:
    PickPointsGesture(PianoRollView& view, Vec press, expr::Curve curve, CurveY yOf,
                      std::function<bool(const Note&)> inScope, bool selectNotes, Modifiers mods)
        : NoteGesture(view, press), curve(curve), yOf(std::move(yOf)), inScope(std::move(inScope)),
          selectNotes(selectNotes), originalPoints(view.curvePoints), originalCurve(view.pointsCurve),
          originalNotes(view.selectedIds()) {
        if (mods.shift && originalCurve == curve)
            basePoints = originalPoints;
        if (mods.shift)
            baseNotes = originalNotes;
    }

    void update(Vec pos, Modifiers) override {
        current = pos;
        Vec p0 = pressPos();
        double x0 = std::min(p0.x, pos.x), x1 = std::max(p0.x, pos.x);
        double y0 = std::min(p0.y, pos.y), y1 = std::max(p0.y, pos.y);
        std::vector<expr::PointRef> picked = basePoints;
        IdSet notes = baseNotes;
        for (const Note& n : before) {
            if (inScope && !inScope(n))
                continue;
            double nx0 = view.viewport.time.tickToX(double(n.start)), nx1 = view.viewport.time.tickToX(double(n.end()));
            for (const ExprPoint& p : expr::points(n, curve)) {
                double x = nx0 + double(p.pos) * (nx1 - nx0), y = yOf(n, p.value);
                if (x < x0 || x > x1 || y < y0 || y > y1)
                    continue;
                expr::PointRef ref{n.id, p.pos, p.value};
                if (std::find(picked.begin(), picked.end(), ref) == picked.end())
                    picked.push_back(ref);
                notes.insert(n.id);
            }
        }
        count = picked.size();
        view.curvePoints = picked;
        view.pointsCurve = curve;
        if (selectNotes) {
            IdSet touched = notesInRect(before, view.viewport.time, view.pitch, x0, y0, x1, y1);
            notes.insert(touched.begin(), touched.end());
            view.select(notes);
        }
    }
    void cancel() override {
        view.curvePoints = originalPoints;
        view.pointsCurve = originalCurve;
        if (selectNotes)
            view.select(originalNotes);
    }
    void drawOverlay(NVGcontext* vg) override {
        const theme::Palette& c = theme::colors();
        Vec p0 = pressPos();
        nvgBeginPath(vg);
        nvgRect(vg, std::min(p0.x, current.x) + 0.5f, std::min(p0.y, current.y) + 0.5f, std::fabs(current.x - p0.x),
                std::fabs(current.y - p0.y));
        nvgFillColor(vg, nvgTransRGBAf(c.select, 0.15f));
        nvgFill(vg);
        nvgStrokeColor(vg, c.select);
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
    std::string readout() const override { return count == 1 ? "1 point" : std::to_string(count) + " points"; }
    // A strip sits on its notes: scrolling would move it under the rectangle.
    bool autoScrollX() const override { return selectNotes; }
    bool autoScrollY() const override { return selectNotes; }

  private:
    expr::Curve curve;
    CurveY yOf;
    std::function<bool(const Note&)> inScope;
    bool selectNotes;
    std::vector<expr::PointRef> originalPoints, basePoints;
    expr::Curve originalCurve;
    IdSet originalNotes, baseNotes;
    Vec current;
    size_t count = 0;
};

// ---- Rubber band --------------------------------------------------------------------------------------------

class RubberBandGesture : public NoteGesture {
  public:
    RubberBandGesture(PianoRollView& view, Vec press, Modifiers mods)
        : NoteGesture(view, press), original(view.selectedIds()) {
        base = mods.shift ? original : IdSet{};
    }

    void update(Vec pos, Modifiers mods) override {
        current = pos;
        Vec p0 = pressPos();
        IdSet hits = notesInRect(before, view.viewport.time, view.pitch, p0.x, p0.y, pos.x, pos.y);
        IdSet next = base;
        next.insert(hits.begin(), hits.end());
        view.select(next);
    }
    void cancel() override { view.select(original); }
    void drawOverlay(NVGcontext* vg) override {
        const theme::Palette& c = theme::colors();
        Vec p0 = pressPos();
        nvgBeginPath(vg);
        nvgRect(vg, std::min(p0.x, current.x) + 0.5f, std::min(p0.y, current.y) + 0.5f, std::fabs(current.x - p0.x),
                std::fabs(current.y - p0.y));
        nvgFillColor(vg, nvgTransRGBAf(c.select, 0.15f));
        nvgFill(vg);
        nvgStrokeColor(vg, c.select);
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
    std::string readout() const override { return countNotes(view.selectedIds().size()); }

  private:
    IdSet original;
    IdSet base;
    Vec current;
};

// ---- Transform box ------------------------------------------------------------------------------------------

class TransformBoxGesture : public NoteGesture {
  public:
    TransformBoxGesture(PianoRollView& view, Vec press, BoxHandle handle)
        : NoteGesture(view, press), ids(view.selectedIds()), handle(handle) {
        span = edits::span(before, ids);
        low = 127;
        high = 0;
        for (const Note& n : before)
            if (ids.count(n.id)) {
                low = std::min(low, int(n.pitch));
                high = std::max(high, int(n.pitch));
            }
    }

    void update(Vec pos, Modifiers mods) override {
        Tick s = step(mods.alt);
        bool left = handle == BoxHandle::Left || handle == BoxHandle::TopLeft || handle == BoxHandle::BottomLeft;
        bool right = handle == BoxHandle::Right || handle == BoxHandle::TopRight || handle == BoxHandle::BottomRight;
        bool top = handle == BoxHandle::Top || handle == BoxHandle::TopLeft || handle == BoxHandle::TopRight;
        bool bottom =
            handle == BoxHandle::Bottom || handle == BoxHandle::BottomLeft || handle == BoxHandle::BottomRight;

        NoteList after = before;
        factor = 1;
        double width = double(span.second - span.first);
        if ((left || right) && width > 0) {
            double edge = left ? double(span.first) : double(span.second);
            Tick target = snapPosition(edge + tickAt(pos) - pressTick, s);
            Tick pivot = left ? span.second : span.first;
            double to = left ? double(pivot - target) : double(target - pivot);
            factor = std::max(0.02, to / width);
            after = edits::stretchTime(after, ids, pivot, factor);
        }
        newLow = low;
        newHigh = high;
        int rows = int(std::round(rowAt(pos.y) - pressRow));
        if (top)
            newHigh = std::max(low, view.stepPitch(high, rows, mods.alt));
        if (bottom)
            newLow = std::min(high, view.stepPitch(low, rows, mods.alt));
        if (newLow != low || newHigh != high) {
            after = edits::scalePitch(after, ids, low, high, newLow, newHigh);
            // Scaling a range lands notes between key notes; scale snap puts them back on the key.
            if (view.snapToKey() && !mods.alt)
                after = edits::fitToScale(after, ids, view.scale());
        }
        result = edits::resolveOverlaps(after, ids);
        view.setPreview(result);
    }

    void commit() override {
        view.clearPreview();
        if (factor != 1 || newLow != low || newHigh != high)
            view.commit(countText("Transform", ids.size()), before, result);
    }
    std::string readout() const override {
        char buf[64];
        snprintf(buf, sizeof(buf), "×%.2f  %s–%s", factor, pitchName(std::clamp(newLow, 0, 127)).c_str(),
                 pitchName(std::clamp(newHigh, 0, 127)).c_str());
        return buf;
    }
    CursorShape cursor() const override {
        switch (handle) {
        case BoxHandle::Left:
        case BoxHandle::Right:
            return CursorShape::ResizeH;
        case BoxHandle::Top:
        case BoxHandle::Bottom:
            return CursorShape::ResizeV;
        default:
            return CursorShape::Move;
        }
    }

  private:
    IdSet ids;
    BoxHandle handle;
    std::pair<Tick, Tick> span;
    int low, high;
    int newLow = 0, newHigh = 0;
    double factor = 1;
    NoteList result;
};

} // namespace

std::unique_ptr<DragGesture> move(PianoRollView& view, Vec press, NoteId grabbed) {
    return std::make_unique<MoveGesture>(view, press, grabbed);
}
std::unique_ptr<DragGesture> resize(PianoRollView& view, Vec press, NoteId grabbed, bool startEdge, bool stretch) {
    return std::make_unique<ResizeGesture>(view, press, grabbed, startEdge, stretch);
}
std::unique_ptr<DragGesture> draw(PianoRollView& view, Vec press) { return std::make_unique<DrawGesture>(view, press); }
std::unique_ptr<DragGesture> paint(PianoRollView& view, Vec press) {
    return std::make_unique<PaintGesture>(view, press);
}
std::unique_ptr<DragGesture> erase(PianoRollView& view, Vec press) {
    return std::make_unique<SweepGesture>(view, press, false);
}
std::unique_ptr<DragGesture> mute(PianoRollView& view, Vec press) {
    return std::make_unique<SweepGesture>(view, press, true);
}
std::unique_ptr<DragGesture> slice(PianoRollView& view, Vec press) {
    return std::make_unique<SliceGesture>(view, press);
}
std::unique_ptr<DragGesture> velocity(PianoRollView& view, Vec press, NoteId grabbed) {
    return std::make_unique<VelocityGesture>(view, press, grabbed);
}
std::unique_ptr<DragGesture> bend(PianoRollView& view, Vec press, const BendHit& hit) {
    PianoRollView* v = &view;
    return std::make_unique<BendGesture>(view, press, hit, expr::Curve::Pitch,
                                         [v](double y) { return v->pitch.yToBentPitch(y); });
}
std::unique_ptr<DragGesture> shapeCurve(PianoRollView& view, Vec press, const BendHit& hit, expr::Curve curve,
                                        std::function<double(double)> valueAt) {
    return std::make_unique<BendGesture>(view, press, hit, curve, std::move(valueAt));
}
std::unique_ptr<DragGesture> arch(PianoRollView& view, Vec press, const BendHit& hit, expr::Curve curve) {
    return std::make_unique<ArchGesture>(view, press, hit, curve);
}
std::unique_ptr<DragGesture> pickPoints(PianoRollView& view, Vec press, expr::Curve curve, CurveY yOf,
                                        std::function<bool(const Note&)> inScope, bool selectNotes, Modifiers mods) {
    return std::make_unique<PickPointsGesture>(view, press, curve, std::move(yOf), std::move(inScope), selectNotes,
                                               mods);
}
std::unique_ptr<DragGesture> rubberBand(PianoRollView& view, Vec press, Modifiers mods) {
    return std::make_unique<RubberBandGesture>(view, press, mods);
}
std::unique_ptr<DragGesture> transformBox(PianoRollView& view, Vec press, BoxHandle handle) {
    return std::make_unique<TransformBoxGesture>(view, press, handle);
}

} // namespace tessera::noteGestures
