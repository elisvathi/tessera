#include "DrumEditor.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/DrumKit.hpp"
#include "../../model/NoteOperators.hpp"
#include "../../module/DrumOutModule.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../editing/DrumActions.hpp"
#include "../interaction/ClickOrDrag.hpp"
#include "../interaction/InteractiveView.hpp"
#include "../interaction/Navigation.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/EditTools.hpp"
#include "../widgets/IconButton.hpp"
#include "../widgets/InlineTextField.hpp"
#include "../widgets/ToolButton.hpp"
#include "PianoRoll.hpp"
#include "TimelineDraw.hpp"

#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

struct GridMetrics {
    float header; // lane header column
    float row;    // lane row height
};
constexpr GridMetrics kOverlayMetrics{168, 26};
constexpr GridMetrics kPanelMetrics{124, 20};
constexpr float kRuler = 18;
constexpr float kMinStepPx = 14; // narrower steps scroll sideways instead
constexpr float kCellInset = 1.5f;
constexpr float kChip = 3;
constexpr float kToggle = 16;
constexpr float kGhostAlpha = 0.3f;
constexpr double kVelocityPerPx = 1.0 / 100;
constexpr float kWheelStep = 30;
constexpr float kVelocityLaneOverlay = 64;
constexpr float kVelocityLanePanel = 40;
constexpr float kVelocityPad = 4; // room above and below the bars
constexpr float kToolButton = 22;

std::string stepCount(size_t n) { return n == 1 ? "1 step" : std::to_string(n) + " steps"; }

/// Hands out the same note IDs on every preview rebuild of one gesture.
class NoteIdPool {
  public:
    explicit NoteIdPool(EditorContext& ctx) : ctx(ctx) {}
    void restart() { next = 0; }
    edits::IdSource source() {
        return [this] {
            if (next == ids.size())
                ids.push_back(ctx.module()->newId<NoteId>());
            return ids[next++];
        };
    }

  private:
    EditorContext& ctx;
    std::vector<NoteId> ids;
    size_t next = 0;
};

} // namespace

/// The lane headers and steps, with the bar ruler on top.
class DrumGrid : public InteractiveView {
  public:
    DrumGrid(EditorContext& ctx, DrumEditorView& owner) : InteractiveView(ctx), owner(owner) {}

    enum class Part { None, Name, Mute, Solo, Step, Ghost, AddLane };
    struct Hit {
        Part part = Part::None;
        int lane = -1;
        Tick stepStart = 0; // Part::Step
    };

    GridMetrics metrics() const { return ctx.surface == Surface::Panel ? kPanelMetrics : kOverlayMetrics; }

    /// Refreshes the time mapping from the clip each frame.
    void step() override {
        const Clip* clip = owner.clip();
        if (clip) {
            ClipSpan span = clipSpan(*clip);
            viewStart = span.start;
            viewLength = std::max<Tick>(1, span.length());
        }
        float width = std::max(1.f, box.size.x - metrics().header);
        pxPerTick = std::max(double(width) / double(viewLength) * zoom, double(kMinStepPx) / double(kPpq / 4));
        clampScroll();
        InteractiveView::step();
    }

    /// Zooms time by `factor` keeping the tick under `x` in place. 1 shows the whole clip; it never zooms out
    /// further, since the whole clip is already in view.
    void zoomAt(float x, double factor) {
        double anchor = xToTick(x);
        zoom = std::clamp(zoom * factor, 1.0, kMaxZoom);
        step();
        scrollX += tickToX(anchor) - x;
        clampScroll();
    }
    void zoomToFit() {
        zoom = 1;
        scrollX = 0;
        step();
    }
    float centerX() const { return metrics().header + (box.size.x - metrics().header) / 2; }

    float tickToX(double t) const { return metrics().header + float((t - double(viewStart)) * pxPerTick) - scrollX; }
    double xToTick(float x) const { return double(viewStart) + double(x - metrics().header + scrollX) / pxPerTick; }
    float rowTop(int lane) const { return kRuler + float(lane) * metrics().row - scrollY; }

    Hit hitTest(Vec pos) const {
        Hit hit;
        const Track* t = owner.track();
        if (!t || pos.y < kRuler)
            return hit;
        int lane = int(std::floor((pos.y - kRuler + scrollY) / metrics().row));
        int lanes = int(t->drumLanes.size());
        if (lane == lanes && lanes < kMaxDrumLanes && pos.x < metrics().header) {
            hit.part = Part::AddLane;
            return hit;
        }
        if (lane < 0 || lane >= lanes)
            return hit;
        hit.lane = lane;
        if (pos.x < metrics().header) {
            float top = rowTop(lane), h = metrics().row;
            Rect solo = toggleRect(top, h, 0), mute = toggleRect(top, h, 1);
            hit.part = solo.contains(pos) ? Part::Solo : mute.contains(pos) ? Part::Mute : Part::Name;
            return hit;
        }
        const Clip* clip = owner.clip();
        if (!clip)
            return hit;
        const DrumLane& l = t->drumLanes[size_t(lane)];
        LaneRegion region = laneRegion(l, *clip);
        double tick = xToTick(pos.x);
        if (tick < double(region.start) || tick >= double(viewStart + viewLength))
            return hit;
        Tick k = Tick(std::floor((tick - double(region.start)) / double(l.stepLength)));
        if (k < region.steps(l.stepLength)) {
            hit.part = Part::Step;
            hit.stepStart = region.start + k * l.stepLength;
        } else {
            hit.part = Part::Ghost;
        }
        return hit;
    }

    /// The note sitting on a step of a lane, or nullptr.
    const Note* noteAt(const edits::NoteList& notes, int pitch, Tick stepStart, Tick stepLength) const {
        for (const Note& n : notes)
            if (n.pitch == pitch && n.start >= stepStart && n.start < stepStart + stepLength)
                return &n;
        return nullptr;
    }

    /// The start of the step a note sits on in lane `l`, if it is one of the lane's own steps.
    std::optional<Tick> stepOf(const DrumLane& l, const Clip& clip, const Note& n) const {
        if (n.pitch != l.pitch)
            return std::nullopt;
        LaneRegion region = laneRegion(l, clip);
        if (n.start < region.start)
            return std::nullopt;
        Tick k = (n.start - region.start) / l.stepLength;
        if (k >= region.steps(l.stepLength))
            return std::nullopt;
        return region.start + k * l.stepLength;
    }

    /// The steps that are on and whose cells the box from `a` to `b` touches.
    edits::IdSet stepsIn(Vec a, Vec b) const {
        edits::IdSet out;
        const Track* t = owner.track();
        const Clip* clip = owner.clip();
        if (!t || !clip)
            return out;
        Rect r =
            Rect::fromMinMax(Vec(std::min(a.x, b.x), std::min(a.y, b.y)), Vec(std::max(a.x, b.x), std::max(a.y, b.y)));
        for (int lane = 0; lane < int(t->drumLanes.size()); lane++) {
            float top = rowTop(lane);
            if (top + metrics().row < r.pos.y || top > r.getBottom())
                continue;
            const DrumLane& l = t->drumLanes[size_t(lane)];
            for (const Note& n : owner.notes())
                if (std::optional<Tick> step = stepOf(l, *clip, n)) {
                    float x0 = tickToX(double(*step)), x1 = tickToX(double(*step + l.stepLength));
                    if (x1 >= r.pos.x && x0 <= r.getRight())
                        out.insert(n.id);
                }
        }
        return out;
    }

    /// Adds every step on the way from the last pointer position to `pos` to `visited` (lane, step start), so a
    /// fast drag skips none.
    void visitSteps(std::optional<Vec>& last, Vec pos, std::set<std::pair<int, Tick>>& visited) const {
        Vec from = last ? *last : pos;
        int samples = std::max(1, int(pos.minus(from).norm() / 3));
        for (int i = 0; i <= samples; i++) {
            Hit hit = hitTest(from.plus(pos.minus(from).mult(float(i) / float(samples))));
            if (hit.part == Part::Step)
                visited.insert({hit.lane, hit.stepStart});
        }
        last = pos;
    }

    /// Points in content coordinates stay put while the grid scrolls under a drag.
    Vec toContent(Vec p) const { return p.plus(Vec(scrollX, scrollY)); }
    Vec fromContent(Vec p) const { return p.minus(Vec(scrollX, scrollY)); }

    /// Pointer click on an empty step: turns it on and selects it (with the selection when `add`).
    void addStep(int lane, Tick start, bool add);

    void startRename(int lane) {
        const Track* t = owner.track();
        if (!t || lane < 0 || lane >= int(t->drumLanes.size()))
            return;
        float top = rowTop(lane);
        auto* field = new InlineTextField(
            t->drumLanes[size_t(lane)].name, [c = &ctx, track = t->id, lane](const std::string& name) {
                drumActions::editLane(*c, track, lane, [name](DrumLane& d) { d.name = name; }, "Rename lane");
            });
        field->box = Rect(kChip + 6, top + 2, metrics().header - 2 * kToggle - 16, metrics().row - 4);
        addChild(field);
        field->focus();
    }

    DrumEditorView& owner;
    Tick viewStart = 0;
    Tick viewLength = 4 * kPpq;
    double pxPerTick = 0.1;

  protected:
    std::unique_ptr<DragGesture> startGesture(Vec pos, int button, Modifiers mods) override;
    void click(Vec pos, int button, Modifiers mods) override;
    void doubleClick(Vec pos, Modifiers mods) override;
    void wheel(Vec pos, Vec delta, Modifiers mods) override {
        ctx.touchNavigation();
        if (mods.ctrl) {
            zoomAt(pos.x, wheelZoomFactor(delta.y)); // Ctrl+wheel zooms time, as in the piano roll
            return;
        }
        if (mods.shift || std::fabs(delta.x) > std::fabs(delta.y))
            scrollX -= (mods.shift ? delta.y : delta.x) / 50 * kWheelStep;
        else
            scrollY -= delta.y / 50 * kWheelStep;
        clampScroll();
    }
    void autoScroll(double dx, double dy) override {
        scrollX += float(dx);
        scrollY += float(dy);
        clampScroll();
    }
    CursorShape hoverCursor(Vec pos, Modifiers mods) override {
        hover = hitTest(pos);
        switch (hover.part) {
        case Part::Step: {
            Tool tool = DrumEditorView::acting(ctx.state().tool);
            if (mods.alt || tool == Tool::Velocity)
                return CursorShape::ResizeV;
            return tool == Tool::Pointer ? CursorShape::Arrow : CursorShape::Hand;
        }
        case Part::Mute:
        case Part::Solo:
        case Part::Name:
        case Part::AddLane:
            return CursorShape::Hand;
        default:
            return CursorShape::Arrow;
        }
    }
    void onLeave(const LeaveEvent& e) override {
        hover = Hit();
        InteractiveView::onLeave(e);
    }
    void drawView(const DrawArgs& args) override;

  private:
    Rect toggleRect(float top, float h, int fromRight) const {
        float x = metrics().header - 6 - kToggle * float(fromRight + 1) - 2 * float(fromRight);
        return Rect(x, top + (h - 14) / 2, kToggle, 14);
    }
    void clampScroll() {
        const Track* t = owner.track();
        float contentW = float(double(viewLength) * pxPerTick);
        float viewW = box.size.x - metrics().header;
        scrollX = std::clamp(scrollX, 0.f, std::max(0.f, contentW - viewW));
        int rows = t ? int(t->drumLanes.size()) + 1 : 1;
        scrollY = std::clamp(scrollY, 0.f, std::max(0.f, kRuler + float(rows) * metrics().row - box.size.y));
    }
    /// How far into its loop each lane is: clip time since the loop start, or NaN when the clip isn't playing.
    double elapsedTicks();
    void drawRuler(NVGcontext* vg);
    /// A press on the bar ruler seeks (and plays, `play`). False when it wasn't one.
    bool seekFromRuler(Vec pos, int button, bool play);
    void drawLane(NVGcontext* vg, const Track& track, int lane, const Clip* clip, double playhead, double elapsed);
    void drawOperators(NVGcontext* vg, const Note& n, Rect cell, float alpha);

    float scrollX = 0, scrollY = 0;
    double zoom = 1; // times the width that shows the whole clip
    static constexpr double kMaxZoom = 16;
    Hit hover;
};

namespace {

/// Click or drag across steps: every step passed turns on (or off, when the first one was on).
class PaintSteps : public DragGesture {
  public:
    PaintSteps(DrumGrid& grid, Vec press, bool on, float velocity)
        : grid(grid), before(grid.owner.notes()), on(on), velocity(velocity), pool(grid.owner.context()) {
        update(press, Modifiers());
    }

    void update(Vec pos, Modifiers mods) override {
        grid.visitSteps(last, pos, visited);
        const Track* t = grid.owner.track();
        if (!t)
            return;
        pool.restart();
        edits::NoteList after = before;
        for (const auto& [lane, start] : visited) {
            const DrumLane& l = t->drumLanes[size_t(lane)];
            bool isOn = grid.noteAt(before, l.pitch, start, l.stepLength) != nullptr;
            if (isOn == on)
                continue; // already as painted: keep its velocity and ID
            after = drums::setStep(after, l.pitch, start, l.stepLength, on ? &velocity : nullptr, pool.source());
        }
        result = std::move(after);
        grid.owner.setPreview(result);
    }
    void commit() override {
        grid.owner.clearPreview();
        if (!visited.empty() && result != before)
            grid.owner.commit(on ? "Add steps" : "Remove steps", before, result);
    }
    void cancel() override { grid.owner.clearPreview(); }
    bool autoScrollX() const override { return true; }
    CursorShape cursor() const override { return CursorShape::Hand; }

  private:
    DrumGrid& grid;
    edits::NoteList before, result;
    bool on;
    float velocity;
    NoteIdPool pool;
    std::set<std::pair<int, Tick>> visited;
    std::optional<Vec> last;
};

/// The Mute tool: every step passed is muted (or unmuted, when the first one was muted).
class MuteSteps : public DragGesture {
  public:
    MuteSteps(DrumGrid& grid, Vec press, bool mute) : grid(grid), before(grid.owner.notes()), mute(mute) {
        update(press, Modifiers());
    }
    void update(Vec pos, Modifiers mods) override {
        grid.visitSteps(last, pos, visited);
        const Track* t = grid.owner.track();
        if (!t)
            return;
        edits::IdSet ids;
        for (const auto& [lane, start] : visited) {
            const DrumLane& l = t->drumLanes[size_t(lane)];
            if (const Note* n = grid.noteAt(before, l.pitch, start, l.stepLength))
                ids.insert(n->id);
        }
        result = edits::setMuted(before, ids, mute);
        grid.owner.setPreview(result);
    }
    void commit() override {
        grid.owner.clearPreview();
        if (result != before)
            grid.owner.commit(mute ? "Mute steps" : "Unmute steps", before, result);
    }
    void cancel() override { grid.owner.clearPreview(); }
    bool autoScrollX() const override { return true; }
    CursorShape cursor() const override { return CursorShape::Hand; }

  private:
    DrumGrid& grid;
    edits::NoteList before, result;
    bool mute;
    std::set<std::pair<int, Tick>> visited;
    std::optional<Vec> last;
};

/// Alt+drag on a step, or the Velocity tool: velocity follows the mouse up and down, for the whole selection when
/// the step is in it.
class StepVelocity : public DragGesture {
  public:
    StepVelocity(DrumGrid& grid, Vec press, NoteId note) : grid(grid), before(grid.owner.notes()), pressY(press.y) {
        ids = grid.owner.isSelected(note) ? grid.owner.selectedIds() : edits::IdSet{note};
        for (const Note& n : before)
            if (n.id == note)
                start = value = n.velocity;
    }
    void update(Vec pos, Modifiers mods) override {
        double scale = mods.shift ? 0.1 : 1.0;
        float delta = float(-(pos.y - pressY) * kVelocityPerPx * scale);
        value = std::clamp(start + delta, 0.f, 1.f);
        result = edits::changeVelocity(before, ids, delta);
        grid.owner.setPreview(result);
    }
    void commit() override {
        grid.owner.clearPreview();
        if (result.empty() || result == before)
            return;
        grid.owner.context().state().lastVelocity = value;
        grid.owner.commit(ids.size() == 1 ? "Set step velocity"
                                          : "Set velocity of " + std::to_string(ids.size()) + " steps",
                          before, result);
    }
    void cancel() override { grid.owner.clearPreview(); }
    std::string readout() const override { return "Velocity " + std::to_string(int(std::lround(value * 100))) + "%"; }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    DrumGrid& grid;
    edits::NoteList before, result;
    edits::IdSet ids;
    float pressY;
    float start = 0.8f;
    float value = 0.8f;
};

/// A selection box: selects the steps it touches (Shift adds them to the selection).
class SelectSteps : public DragGesture {
  public:
    SelectSteps(DrumGrid& grid, Vec press, Modifiers mods)
        : grid(grid), press(grid.toContent(press)), current(press), original(grid.owner.selectedIds()) {
        if (mods.shift)
            base = original;
    }
    void update(Vec pos, Modifiers mods) override {
        current = pos;
        edits::IdSet next = base;
        edits::IdSet hits = grid.stepsIn(grid.fromContent(press), pos);
        next.insert(hits.begin(), hits.end());
        grid.owner.select(next);
    }
    void cancel() override { grid.owner.select(original); }
    void drawOverlay(NVGcontext* vg) override {
        const theme::Palette& c = theme::colors();
        Vec p0 = grid.fromContent(press);
        nvgBeginPath(vg);
        nvgRect(vg, std::min(p0.x, current.x) + 0.5f, std::min(p0.y, current.y) + 0.5f, std::fabs(current.x - p0.x),
                std::fabs(current.y - p0.y));
        nvgFillColor(vg, nvgTransRGBAf(c.select, 0.15f));
        nvgFill(vg);
        nvgStrokeColor(vg, c.select);
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
    std::string readout() const override { return stepCount(grid.owner.selectedIds().size()); }
    bool autoScrollX() const override { return true; }
    bool autoScrollY() const override { return true; }

  private:
    DrumGrid& grid;
    Vec press, current;
    edits::IdSet original, base;
};

/// Dragging selected steps: they move by whole steps of the pressed lane, and up and down lanes. Ctrl/Cmd copies
/// them. A step holds one note, so a step they land on gives way.
class MoveSteps : public DragGesture {
  public:
    MoveSteps(DrumGrid& grid, Vec press, int lane, bool copy)
        : grid(grid), before(grid.owner.committedNotes()), ids(grid.owner.selectedIds()), press(grid.toContent(press)),
          lane(lane), copy(copy), pool(grid.owner.context()) {}

    void update(Vec pos, Modifiers mods) override {
        const Track* t = grid.owner.track();
        const Clip* clip = grid.owner.clip();
        if (!t || !clip || ids.empty())
            return;
        const std::vector<DrumLane>& lanes = t->drumLanes;
        Vec d = grid.toContent(pos).minus(press);
        if (mods.shift) {
            if (std::fabs(d.x) > std::fabs(d.y))
                d.y = 0;
            else
                d.x = 0;
        }
        Tick stepLength = lanes[size_t(lane)].stepLength;
        Tick steps = Tick(std::lround(double(d.x) / (double(stepLength) * grid.pxPerTick)));
        int laneShift = int(std::lround(d.y / grid.metrics().row));

        // Every moved step stays on a lane and inside the clip.
        Tick first = std::numeric_limits<Tick>::max(), last = std::numeric_limits<Tick>::min();
        int lowest = int(lanes.size()), highest = -1;
        for (const Note& n : before)
            if (ids.count(n.id)) {
                first = std::min(first, n.start);
                last = std::max(last, n.start);
                int i = laneOfPitch(lanes, n.pitch);
                if (i >= 0) {
                    lowest = std::min(lowest, i);
                    highest = std::max(highest, i);
                }
            }
        if (highest >= 0)
            laneShift = std::clamp(laneShift, -lowest, int(lanes.size()) - 1 - highest);
        Tick end = grid.viewStart + grid.viewLength;
        Tick fewest = Tick(std::ceil(double(grid.viewStart - first) / double(stepLength)));
        Tick most = Tick(std::floor(double(end - 1 - last) / double(stepLength)));
        steps = fewest <= most ? std::clamp(steps, fewest, most) : 0;
        dt = steps * stepLength;

        pool.restart();
        edits::NoteList after = before;
        moved = ids;
        if (copy) {
            edits::IdSet added;
            after = edits::duplicate(after, ids, 0, pool.source(), &added);
            moved = added;
        }
        after = edits::move(after, moved, dt, 0);
        if (laneShift != 0)
            after = edits::mapPitches(after, moved, [&lanes, laneShift](int p) {
                int i = laneOfPitch(lanes, p);
                return i < 0 ? p : int(lanes[size_t(i + laneShift)].pitch);
            });
        std::set<std::pair<int, Tick>> landed;
        for (const Note& n : after)
            if (moved.count(n.id))
                if (std::optional<std::pair<int, Tick>> at = cellOf(lanes, *clip, n))
                    landed.insert(*at);
        result.clear();
        for (const Note& n : after) {
            std::optional<std::pair<int, Tick>> at = moved.count(n.id) ? std::nullopt : cellOf(lanes, *clip, n);
            if (!at || !landed.count(*at))
                result.push_back(n);
        }
        grid.owner.setPreview(result);
        if (laneShift != shownLane) {
            shownLane = laneShift;
            if (laneShift != 0 && grid.owner.context().state().audition)
                drumActions::audition(grid.owner.context(), t->id, lane + laneShift);
        }
        text = (steps > 0 ? "+" : "") + std::to_string(steps) + (std::abs(steps) == 1 ? " step" : " steps");
        if (laneShift != 0)
            text += "  ·  " + lanes[size_t(std::clamp(lane + laneShift, 0, int(lanes.size()) - 1))].name;
    }
    void commit() override {
        grid.owner.clearPreview();
        if (dt == 0 && shownLane == 0)
            return;
        grid.owner.commit((copy ? "Copy " : "Move ") + stepCount(ids.size()), before, result, moved);
    }
    void cancel() override { grid.owner.clearPreview(); }
    std::string readout() const override { return text; }
    CursorShape cursor() const override { return CursorShape::Move; }
    bool autoScrollX() const override { return true; }
    bool autoScrollY() const override { return true; }

  private:
    /// The lane and step a note sits on.
    std::optional<std::pair<int, Tick>> cellOf(const std::vector<DrumLane>& lanes, const Clip& clip, const Note& n) {
        int i = laneOfPitch(lanes, n.pitch);
        if (i < 0)
            return std::nullopt;
        std::optional<Tick> step = grid.stepOf(lanes[size_t(i)], clip, n);
        if (!step)
            return std::nullopt;
        return std::make_pair(i, *step);
    }

    DrumGrid& grid;
    edits::NoteList before, result;
    edits::IdSet ids, moved;
    Vec press;
    int lane;
    bool copy;
    NoteIdPool pool;
    Tick dt = 0;
    int shownLane = 0;
    std::string text;
};

} // namespace

/// Velocity bars for one lane's steps, lined up under the step grid. Dragging sets every step the pointer
/// crosses to the pointer's height, one undo step per drag.
class DrumVelocityLane : public InteractiveView {
  public:
    DrumVelocityLane(EditorContext& ctx, DrumEditorView& owner, DrumGrid& grid)
        : InteractiveView(ctx), owner(owner), grid(grid) {}

    /// The shown lane, clamped to the track's lanes; -1 when there is none.
    int lane() {
        const Track* t = owner.track();
        if (!t || t->drumLanes.empty())
            return -1;
        owner.velocityLane = std::clamp(owner.velocityLane, 0, int(t->drumLanes.size()) - 1);
        return owner.velocityLane;
    }
    float valueAt(float y) const {
        return std::clamp(1.f - (y - kVelocityPad) / std::max(1.f, box.size.y - 2 * kVelocityPad), 0.f, 1.f);
    }
    /// The start of the lane's step under x, if it is one of the lane's own steps.
    std::optional<Tick> stepAt(float x) {
        const Track* t = owner.track();
        const Clip* clip = owner.clip();
        int l = lane();
        if (!t || !clip || l < 0 || x < grid.metrics().header)
            return std::nullopt;
        const DrumLane& d = t->drumLanes[size_t(l)];
        LaneRegion region = laneRegion(d, *clip);
        double tick = grid.xToTick(x);
        if (tick < double(region.start))
            return std::nullopt;
        Tick k = Tick(std::floor((tick - double(region.start)) / double(d.stepLength)));
        if (k >= region.steps(d.stepLength))
            return std::nullopt;
        return region.start + k * d.stepLength;
    }

    DrumEditorView& owner;
    DrumGrid& grid;

  protected:
    std::unique_ptr<DragGesture> startGesture(Vec pos, int button, Modifiers mods) override;
    CursorShape hoverCursor(Vec pos, Modifiers mods) override {
        return stepAt(pos.x) ? CursorShape::ResizeV : CursorShape::Arrow;
    }
    void drawView(const DrawArgs& args) override;
};

namespace {

class PaintVelocity : public DragGesture {
  public:
    PaintVelocity(DrumVelocityLane& lane, Vec press) : lane(lane), before(lane.owner.notes()) {
        update(press, Modifiers());
    }
    void update(Vec pos, Modifiers mods) override {
        const Track* t = lane.owner.track();
        int index = lane.lane();
        if (!t || index < 0)
            return;
        const DrumLane& d = t->drumLanes[size_t(index)];
        // Visit every step between the last pointer position and this one, so a fast drag skips none, each
        // getting the height along the way.
        Vec from = last ? *last : pos;
        int samples = std::max(1, int(pos.minus(from).norm() / 3));
        for (int i = 0; i <= samples; i++) {
            Vec p = from.plus(pos.minus(from).mult(float(i) / float(samples)));
            if (std::optional<Tick> step = lane.stepAt(p.x))
                if (const Note* n = lane.grid.noteAt(before, d.pitch, *step, d.stepLength))
                    values[n->id] = lane.valueAt(p.y);
        }
        last = pos;
        value = lane.valueAt(pos.y);
        edits::NoteList after = before;
        for (Note& n : after)
            if (auto it = values.find(n.id); it != values.end())
                n.velocity = it->second;
        result = after;
        lane.owner.setPreview(std::move(after));
    }
    void commit() override {
        lane.owner.clearPreview();
        if (values.empty())
            return;
        lane.owner.context().state().lastVelocity = value;
        lane.owner.commit(values.size() == 1 ? "Set step velocity" : "Set step velocities", before, result);
    }
    void cancel() override { lane.owner.clearPreview(); }
    std::string readout() const override { return "Velocity " + std::to_string(int(std::lround(value * 100))) + "%"; }
    CursorShape cursor() const override { return CursorShape::ResizeV; }
    bool autoScrollX() const override { return true; }

  private:
    DrumVelocityLane& lane;
    edits::NoteList before, result;
    std::map<NoteId, float> values;
    std::optional<Vec> last;
    float value = 0;
};

} // namespace

std::unique_ptr<DragGesture> DrumVelocityLane::startGesture(Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || !ctx.module() || !owner.clip() || lane() < 0)
        return nullptr;
    return std::make_unique<PaintVelocity>(*this, pos);
}

void DrumVelocityLane::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    float header = grid.metrics().header;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, header, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, 1);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);

    const Track* t = owner.track();
    int index = lane();
    if (!t || index < 0)
        return;
    const DrumLane& d = t->drumLanes[size_t(index)];
    draw::TextStyle label{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text2,
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::text(vg, label, 10, box.size.y / 2, draw::ellipsize(vg, label, "Velocity  ·  " + d.name, header - 16));
    const Clip* clip = owner.clip();
    if (!clip)
        return;

    nvgSave(vg);
    nvgScissor(vg, header, 0, box.size.x - header, box.size.y);
    NVGcolor color = theme::track(d.color ? d.color : t->color);
    LaneRegion region = laneRegion(d, *clip);
    float barH = box.size.y - 2 * kVelocityPad;
    float cellW = float(double(d.stepLength) * grid.pxPerTick);
    const edits::NoteList& notes = owner.notes();
    for (Tick k = 0; k < region.steps(d.stepLength); k++) {
        Tick start = region.start + k * d.stepLength;
        const Note* n = grid.noteAt(notes, d.pitch, start, d.stepLength);
        if (!n)
            continue;
        float x = grid.tickToX(double(start));
        if (x + cellW < header || x > box.size.x)
            continue;
        float h = std::max(2.f, barH * n->velocity);
        float w = std::clamp(cellW * 0.35f, 3.f, 8.f);
        float cx = x + cellW / 2;
        nvgBeginPath(vg);
        nvgRect(vg, cx - 0.5f, kVelocityPad + barH - h, 1, h);
        nvgFillColor(vg, color);
        nvgFill(vg);
        draw::roundedRect(vg, Rect(cx - w / 2, kVelocityPad + barH - h - 2, w, 4), 2, color);
    }
    nvgRestore(vg);
}

std::unique_ptr<DragGesture> DrumGrid::startGesture(Vec pos, int button, Modifiers mods) {
    const Track* t = owner.track();
    if (!ctx.module() || !t || !owner.clip() || pos.x < metrics().header || pos.y < kRuler)
        return nullptr;
    Hit hit = hitTest(pos);
    const Note* n = nullptr;
    if (hit.part == Part::Step) {
        const DrumLane& l = t->drumLanes[size_t(hit.lane)];
        n = noteAt(owner.notes(), l.pitch, hit.stepStart, l.stepLength);
    }
    Tool tool = DrumEditorView::acting(ctx.state().tool);
    // In the Draw tool, right-click and right-drag remove steps, as in the piano roll.
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
        return tool == Tool::Draw && hit.part == Part::Step ? std::make_unique<PaintSteps>(*this, pos, false, 0.f)
                                                            : nullptr;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return nullptr;
    // Ctrl/Cmd+drag from an empty step selects with any tool; Ctrl/Cmd+Alt+drag selects wherever it starts.
    if (mods.ctrl && (mods.alt || !n))
        return std::make_unique<SelectSteps>(*this, pos, mods);
    if (hit.part != Part::Step) {
        if (tool != Tool::Pointer)
            return nullptr;
        return std::make_unique<ClickOrDrag>(std::make_unique<SelectSteps>(*this, pos, mods), pos, [this, mods] {
            if (!mods.shift)
                owner.select({});
        });
    }
    owner.velocityLane = hit.lane;
    if (mods.alt && n)
        return std::make_unique<StepVelocity>(*this, pos, n->id);

    switch (tool) {
    case Tool::Pointer: {
        if (!n) {
            int lane = hit.lane;
            Tick start = hit.stepStart;
            return std::make_unique<ClickOrDrag>(std::make_unique<SelectSteps>(*this, pos, mods), pos,
                                                 [this, lane, start, mods] { addStep(lane, start, mods.shift); });
        }
        NoteId id = n->id;
        bool wasSelected = owner.isSelected(id);
        if (!wasSelected) {
            edits::IdSet next = mods.shift || mods.ctrl ? owner.selectedIds() : edits::IdSet{};
            next.insert(id);
            owner.select(next);
        }
        // A click without a drag: Ctrl/Cmd toggles, a plain click on a group picks just this step.
        return std::make_unique<ClickOrDrag>(std::make_unique<MoveSteps>(*this, pos, hit.lane, mods.ctrl), pos,
                                             [this, id, wasSelected, mods] {
                                                 if (mods.ctrl && wasSelected) {
                                                     edits::IdSet next = owner.selectedIds();
                                                     next.erase(id);
                                                     owner.select(next);
                                                 } else if (!mods.shift && !mods.ctrl) {
                                                     owner.select({id});
                                                 }
                                             });
    }
    case Tool::Erase:
        return std::make_unique<PaintSteps>(*this, pos, false, 0.f);
    case Tool::Mute:
        return std::make_unique<MuteSteps>(*this, pos, !(n && n->muted));
    case Tool::Velocity:
        return n ? std::make_unique<StepVelocity>(*this, pos, n->id) : nullptr;
    default:
        if (!n && ctx.state().audition)
            drumActions::audition(ctx, t->id, hit.lane);
        return std::make_unique<PaintSteps>(*this, pos, n == nullptr, ctx.state().lastVelocity);
    }
}

void DrumGrid::addStep(int lane, Tick start, bool add) {
    const Track* t = owner.track();
    if (!t || !ctx.module() || lane < 0 || lane >= int(t->drumLanes.size()))
        return;
    const DrumLane& l = t->drumLanes[size_t(lane)];
    NoteId id = ctx.module()->newId<NoteId>();
    float velocity = ctx.state().lastVelocity;
    edits::NoteList before = owner.committedNotes();
    edits::NoteList after = drums::setStep(before, l.pitch, start, l.stepLength, &velocity, [id] { return id; });
    edits::IdSet selection = add ? owner.selectedIds() : edits::IdSet{};
    selection.insert(id);
    owner.commit("Add step", before, after, selection);
    if (ctx.state().audition)
        drumActions::audition(ctx, t->id, lane);
}

void DrumGrid::click(Vec pos, int button, Modifiers mods) {
    const Track* t = owner.track();
    if (!t || !ctx.module())
        return;
    if (seekFromRuler(pos, button, false))
        return;
    Hit hit = hitTest(pos);
    TrackId track = t->id;
    int lane = hit.lane;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (hit.part == Part::Step) {
            const DrumLane& l = t->drumLanes[size_t(lane)];
            if (const Note* n = noteAt(owner.notes(), l.pitch, hit.stepStart, l.stepLength)) {
                // The menu acts on the selection; a step outside it becomes the selection.
                if (!owner.isSelected(n->id))
                    owner.select({n->id});
                drumActions::appendStepMenu(rack::createMenu(), owner);
                return;
            }
        }
        if (lane >= 0)
            drumActions::appendLaneMenu(rack::createMenu(), ctx, track, lane, [this, lane] { startRename(lane); });
        else
            drumActions::appendKitMenu(rack::createMenu(), ctx, track);
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    switch (hit.part) {
    case Part::Name:
        owner.velocityLane = lane;
        drumActions::audition(ctx, track, lane);
        break;
    case Part::Mute: {
        bool mute = !t->drumLanes[size_t(lane)].mute;
        drumActions::editLane(
            ctx, track, lane, [mute](DrumLane& d) { d.mute = mute; }, mute ? "Mute lane" : "Unmute lane");
        break;
    }
    case Part::Solo: {
        bool solo = !t->drumLanes[size_t(lane)].solo;
        drumActions::editLane(
            ctx, track, lane, [solo](DrumLane& d) { d.solo = solo; }, solo ? "Solo lane" : "Unsolo lane");
        break;
    }
    case Part::AddLane:
        drumActions::addLane(ctx, track);
        break;
    default:
        break;
    }
}

bool DrumGrid::seekFromRuler(Vec pos, int button, bool play) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || pos.y >= kRuler || pos.x < metrics().header || !owner.clip() ||
        !owner.source.canSeek())
        return false;
    // As in the piano roll's ruler: a click moves the playhead here, a double-click plays from here. On the 1/16s.
    double step = double(kPpq) / 4;
    owner.source.seek(std::max(0.0, std::round(xToTick(pos.x) / step) * step), play);
    return true;
}

void DrumGrid::doubleClick(Vec pos, Modifiers mods) {
    const Track* t = owner.track();
    if (!t)
        return;
    if (seekFromRuler(pos, GLFW_MOUSE_BUTTON_LEFT, true))
        return;
    if (!owner.clip()) {
        clipActions::createInFirstEmpty(ctx, t->id);
        return;
    }
    Hit hit = hitTest(pos);
    if (hit.part == Part::Name)
        startRename(hit.lane);
}

double DrumGrid::elapsedTicks() {
    TesseraModule* m = ctx.module();
    const Track* t = owner.track();
    const Clip* clip = owner.clip();
    if (!m || !t || !clip)
        return NAN;
    LauncherStatus status = m->launcherStatus(t->id);
    if (status.playing >= 0 && status.playing < int(t->slots.size()) &&
        t->slots[size_t(status.playing)].clip == clip->id)
        return status.elapsed;
    if (m->playMode() == PlayMode::Arranger && !status.overridden) {
        double song = m->playheadTicks();
        if (const ArrangerClip* a = instanceAt(*t, Tick(song)); a && a->clip == clip->id) {
            ClipSpan span = clipSpan(*clip);
            double offset = double(normalizeOffset(*clip, a->offset) - span.start);
            return std::max(0.0, offset) + (song - double(a->position)) / double(a->stretch);
        }
    }
    return NAN;
}

void DrumGrid::drawRuler(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, kRuler);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    TimeSig ts = owner.source.timeSig();
    Tick beat = ticksPerBeat(ts);
    draw::TextStyle label{theme::fonts::mono(), theme::metrics::kTextRuler, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    nvgSave(vg);
    nvgScissor(vg, metrics().header, 0, box.size.x - metrics().header, kRuler);
    for (Tick t = viewStart - viewStart % beat; t < viewStart + viewLength; t += beat) {
        float x = tickToX(double(t));
        bool bar = t % ticksPerBar(ts) == 0;
        nvgBeginPath(vg);
        nvgRect(vg, x, bar ? 4 : 10, 1, bar ? kRuler - 4 : kRuler - 10);
        nvgFillColor(vg, bar ? c.line2 : c.line1);
        nvgFill(vg);
        if (bar || beat * pxPerTick > 40)
            draw::text(vg, label, x + 3, kRuler / 2, timeline::positionText(double(t), ts));
    }
    nvgRestore(vg);
}

void DrumGrid::drawLane(NVGcontext* vg, const Track& track, int lane, const Clip* clip, double playhead,
                        double elapsed) {
    const theme::Palette& c = theme::colors();
    const DrumLane& l = track.drumLanes[size_t(lane)];
    float top = rowTop(lane), h = metrics().row, header = metrics().header;
    if (top > box.size.y || top + h < kRuler)
        return;
    NVGcolor color = theme::track(l.color ? l.color : track.color);
    bool anySolo =
        std::any_of(track.drumLanes.begin(), track.drumLanes.end(), [](const DrumLane& d) { return d.solo; });
    bool audible = anySolo ? l.solo : !l.mute;

    // Header: colour chip, name, mute and solo.
    nvgBeginPath(vg);
    nvgRect(vg, 0, top, header, h);
    NVGcolor headerFill = lane % 2 ? c.bg1 : nvgLerpRGBA(c.bg1, c.bg2, 0.4f);
    if (lane == owner.velocityLane)
        headerFill = nvgLerpRGBA(headerFill, c.select, 0.12f);
    nvgFillColor(vg, headerFill);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 4, top + 4, kChip, h - 8);
    nvgFillColor(vg, color);
    nvgFill(vg);
    bool nameHover = hover.part == Part::Name && hover.lane == lane;
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextUi,
                         !audible    ? c.text2
                         : nameHover ? c.text0
                                     : c.text1,
                         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::string label = l.name;
    if (clip && hasOwnLoop(l, *clip))
        label += "  " + std::to_string(laneRegion(l, *clip).steps(l.stepLength));
    Rect solo = toggleRect(top, h, 0), mute = toggleRect(top, h, 1);
    // The Drum Out jack the lane plays on; outputs past the 8 jacks are only on the poly outputs.
    std::string output = l.outputIndex < DrumOutModule::kTrigJacks ? "→" + std::to_string(l.outputIndex + 1)
                                                                   : "→" + std::to_string(l.outputIndex + 1) + " poly";
    draw::TextStyle out{theme::fonts::mono(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    float outRight = mute.pos.x - 4;
    draw::text(vg, out, outRight, top + h / 2, output);
    float nameRight = outRight - draw::textWidth(vg, out, output) - 6;
    draw::text(vg, name, kChip + 10, top + h / 2, draw::ellipsize(vg, name, label, nameRight - kChip - 10));
    auto toggle = [&](Rect r, const char* text, bool active, NVGcolor on) {
        draw::roundedRect(vg, r, theme::metrics::kRadiusControl, active ? on : c.bg2);
        draw::TextStyle s{theme::fonts::uiStrong(), theme::metrics::kTextRuler, active ? c.ink : c.text1,
                          NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, s, r.getCenter().x, r.getCenter().y, text);
    };
    toggle(mute, "M", l.mute, c.queue);
    toggle(solo, "S", l.solo, c.play);

    if (!clip)
        return;
    nvgSave(vg);
    nvgScissor(vg, header, kRuler, box.size.x - header, box.size.y - kRuler);
    LaneRegion region = laneRegion(l, *clip);
    Tick steps = region.steps(l.stepLength);
    const edits::NoteList& notes = owner.notes();
    float cellW = float(double(l.stepLength) * pxPerTick) - 2 * kCellInset;
    float cellH = h - 2 * kCellInset;
    float dim = audible ? 1.f : 0.45f;

    // The step that is playing now: lanes with their own loop count from the clip's start.
    Tick playingStep = -1;
    if (std::isfinite(elapsed) && hasOwnLoop(l, *clip))
        playingStep = Tick(std::fmod(elapsed, double(region.length)) / double(l.stepLength));
    else if (std::isfinite(playhead) && playhead >= double(region.start) &&
             playhead < double(region.start + region.length))
        playingStep = Tick((playhead - double(region.start)) / double(l.stepLength));

    // The lane's own steps, then faded repeats until the clip's loop ends.
    for (Tick repeat = 0; region.start + repeat * region.length < viewStart + viewLength; repeat++) {
        bool ghost = repeat > 0;
        for (Tick k = 0; k < steps; k++) {
            Tick start = region.start + k * l.stepLength;
            Tick drawnAt = start + repeat * region.length;
            if (drawnAt >= viewStart + viewLength)
                break;
            float x = tickToX(double(drawnAt)) + kCellInset;
            if (x + cellW < header || x > box.size.x)
                continue;
            Rect cell(x, top + kCellInset, std::max(2.f, cellW), cellH);
            float alpha = dim * (ghost ? kGhostAlpha : 1.f);
            bool beatGroup = (k / 4) % 2 == 0;
            NVGcolor empty = nvgLerpRGBA(c.bg1, c.bg2, beatGroup ? 1.f : 0.45f);
            empty.a *= ghost ? 0.5f : 1.f;
            draw::roundedRect(vg, cell, theme::metrics::kRadiusItem, empty);
            if (const Note* n = noteAt(notes, l.pitch, start, l.stepLength)) {
                // A muted step keeps its place but only its outline and a faint fill.
                float noteAlpha = n->muted ? alpha * 0.35f : alpha;
                NVGcolor back = color;
                back.a = 0.3f * noteAlpha;
                draw::roundedRect(vg, cell, theme::metrics::kRadiusItem, back);
                float fill = std::max(3.f, cellH * n->velocity);
                NVGcolor front = color;
                front.a = noteAlpha;
                draw::roundedRect(vg, Rect(cell.pos.x, cell.getBottom() - fill, cell.size.x, fill),
                                  theme::metrics::kRadiusItem, front);
                if (n->muted)
                    draw::roundedRectStroke(vg, cell, theme::metrics::kRadiusItem, nvgTransRGBAf(color, alpha), 1);
                drawOperators(vg, *n, cell, alpha);
                if (!ghost && owner.isSelected(n->id))
                    draw::roundedRectStroke(vg, cell, theme::metrics::kRadiusItem, c.select, 1.5f);
            }
            if (!ghost && k == playingStep)
                draw::roundedRectStroke(vg, cell, theme::metrics::kRadiusItem, nvgTransRGBAf(c.text0, 0.7f), 1.5f);
            if (!ghost && hover.part == Part::Step && hover.lane == lane && hover.stepStart == start)
                draw::roundedRectStroke(vg, cell, theme::metrics::kRadiusItem, nvgTransRGBAf(c.text0, 0.35f), 1);
        }
        if (!hasOwnLoop(l, *clip))
            break;
    }
    nvgRestore(vg);
}

void DrumGrid::drawOperators(NVGcontext* vg, const Note& n, Rect cell, float alpha) {
    const theme::Palette& c = theme::colors();
    NVGcolor mark = nvgTransRGBAf(c.onTile, 0.7f * alpha);
    // Repeats: the cell split where each hit starts (EDITOR_UX §5).
    int count = operators::hitCount(n, kPpq);
    if (count > 1 && cell.size.x / float(count) >= 3) {
        nvgBeginPath(vg);
        for (int i = 1; i < count; i++) {
            operators::Hit h = operators::hit(n, i, count);
            float x = cell.pos.x + float(h.start / double(n.length)) * cell.size.x;
            nvgRect(vg, std::round(x), cell.pos.y + 2, 1, cell.size.y - 4);
        }
        nvgFillColor(vg, mark);
        nvgFill(vg);
    }
    // Chance, occurrence and recurrence: a dot in the corner says "this step doesn't always play".
    const NoteOperators& o = n.ops;
    if (o.chance < 1 || o.occurrence != Occurrence::Always || o.recurrenceLength > 1) {
        nvgBeginPath(vg);
        nvgCircle(vg, cell.getRight() - 3.5f, cell.pos.y + 3.5f, 2);
        nvgFillColor(vg, nvgTransRGBAf(c.text0, 0.85f * alpha));
        nvgFill(vg);
    }
}

void DrumGrid::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);
    const Track* t = owner.track();
    if (!t)
        return;
    const Clip* clip = owner.clip();
    drawRuler(vg);
    double playhead = clip ? owner.source.playhead() : NAN;
    double elapsed = clip ? elapsedTicks() : NAN;
    for (int lane = 0; lane < int(t->drumLanes.size()); lane++)
        drawLane(vg, *t, lane, clip, playhead, elapsed);

    float addTop = rowTop(int(t->drumLanes.size()));
    if (int(t->drumLanes.size()) < kMaxDrumLanes && addTop < box.size.y) {
        draw::TextStyle add{theme::fonts::ui(), theme::metrics::kTextUi,
                            hover.part == Part::AddLane ? c.text0 : c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, add, 10, addTop + metrics().row / 2, "+ Add lane");
    }
    if (std::isfinite(playhead)) {
        float x = tickToX(playhead);
        if (x >= metrics().header && x <= box.size.x)
            timeline::drawPlayhead(vg, x, box.size.y);
    }
    if (!clip) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        float cx = metrics().header + (box.size.x - metrics().header) / 2;
        draw::text(vg, hint, cx, box.size.y / 2, "Double-click to create a clip");
    }
    nvgBeginPath(vg);
    nvgRect(vg, metrics().header - 1, 0, 1, box.size.y);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);
}

// ---- DrumEditorView -----------------------------------------------------------------------------------------

DrumEditorView::DrumEditorView(EditorContext& ctx) : EditorPane(ctx, Pane::Detail), source(ctx) {
    EditorContext* c = &ctx;
    clipChip = makeClipChip(ctx, [this] { return source.track; }, [this] { return source.clip; });
    addChild(clipChip);
    devicesToggle = makeDevicesToggle(ctx);
    addChild(devicesToggle);

    ToolButton::Options kit;
    kit.text = "Kit";
    kit.tooltip = "Drum kit presets and lanes";
    kit.action = [this] {
        if (const Track* t = track())
            drumActions::appendKitMenu(rack::createMenu(), this->ctx, t->id);
    };
    kitButton = new ToolButton(kit);
    addChild(kitButton);

    drumSwitch = makeDrumViewSwitch(ctx);
    addChild(drumSwitch.first);
    addChild(drumSwitch.second);

    tools = makeToolSwitch(ctx, {Tool::Erase, Tool::Mute, Tool::Velocity}, nullptr, nullptr, acting, [this] {
        if (selectedIds().empty())
            return false;
        handleAction(Action::Delete);
        return true;
    });
    for (ToolButton* b : {tools.select, tools.draw, tools.more})
        addChild(b);
    ToolButton::Options hear;
    hear.icon = Icon::Speaker;
    hear.tooltip = withShortcut("Hear steps while editing", Action::ToggleAudition);
    hear.action = [c] { c->state().audition = !c->state().audition; };
    hear.active = [c] { return c->module() && c->state().audition; };
    hearButton = new ToolButton(hear);
    addChild(hearButton);

    maximizeButton = makeMaximizeButton(ctx, Pane::Detail);
    addChild(maximizeButton);

    grid = new DrumGrid(ctx, *this);
    addChild(grid);
    velocity = new DrumVelocityLane(ctx, *this, *grid);
    addChild(velocity);
}

const Track* DrumEditorView::track() {
    TesseraModule* m = ctx.module();
    return m ? m->song().findTrack(ctx.state().selectedTrack) : nullptr;
}

const Clip* DrumEditorView::clip() {
    TesseraModule* m = ctx.module();
    return m ? m->song().findClip(source.clip) : nullptr;
}

const edits::NoteList& DrumEditorView::committedNotes() {
    static const edits::NoteList none;
    const Clip* c = clip();
    return c ? c->notes : none;
}

const edits::NoteList& DrumEditorView::notes() { return preview ? *preview : committedNotes(); }

edits::IdSet DrumEditorView::selectedIds() {
    const NoteSelection& s = ctx.state().selection;
    return s.clip == source.clip ? s.notes : edits::IdSet{};
}

bool DrumEditorView::isSelected(NoteId id) {
    const NoteSelection& s = ctx.state().selection;
    return s.clip == source.clip && s.contains(id);
}

void DrumEditorView::select(edits::IdSet ids) { ctx.state().selection = NoteSelection{source.clip, std::move(ids)}; }

void DrumEditorView::commit(const std::string& name, const edits::NoteList& before, const edits::NoteList& after,
                            std::optional<edits::IdSet> selectAfter) {
    TesseraModule* m = ctx.module();
    if (!m || !source.clip.valid())
        return;
    NoteSelection selection{source.clip, {}};
    if (selectAfter) {
        selection.notes = *selectAfter;
    } else {
        edits::IdSet present = edits::idsOf(after);
        for (NoteId id : selectedIds())
            if (present.count(id))
                selection.notes.insert(id);
    }
    CommandPtr command = editNotes(m->song(), name, source.clip, before, after);
    if (command)
        m->perform(std::move(command), selection);
    else
        ctx.state().selection = selection;
}

Tool DrumEditorView::acting(Tool chosen) {
    switch (chosen) {
    case Tool::Pointer:
    case Tool::Erase:
    case Tool::Mute:
    case Tool::Velocity:
        return chosen;
    default:
        return Tool::Draw;
    }
}

bool DrumEditorView::handleAction(Action action) {
    if (!ctx.module() || !clip())
        return false;
    edits::IdSet ids = selectedIds();
    switch (action) {
    case Action::LoopToSelection: {
        // The pattern's loop covers the selected steps, out to the beat, and the clip loops.
        if (ids.empty())
            return false;
        Tick from = std::numeric_limits<Tick>::max(), to = 0;
        for (const Note& n : clip()->notes)
            if (ids.count(n.id)) {
                from = std::min(from, n.start);
                to = std::max(to, n.end());
            }
        Tick beat = std::max<Tick>(ticksPerBeat(ctx.module()->song().timeSig), 1);
        from = std::max<Tick>(0, from / beat * beat);
        to = (to + beat - 1) / beat * beat;
        if (to <= from)
            return false;
        source.setLoop(from, to);
        if (!source.loopOn())
            source.toggleLoop();
        return true;
    }
    case Action::ZoomIn:
        grid->zoomAt(grid->centerX(), 1.5);
        return true;
    case Action::ZoomOut:
        grid->zoomAt(grid->centerX(), 1 / 1.5);
        return true;
    case Action::ZoomToFit:
        grid->zoomToFit();
        return true;
    case Action::Delete: {
        if (ids.empty())
            return true;
        edits::NoteList before = committedNotes(), after;
        for (const Note& n : before)
            if (!ids.count(n.id))
                after.push_back(n);
        commit("Delete " + stepCount(ids.size()), before, after, edits::IdSet{});
        return true;
    }
    case Action::SelectAll:
        select(edits::idsOf(committedNotes()));
        return true;
    case Action::SelectNone:
        select({});
        return true;
    case Action::SelectInvert: {
        edits::IdSet next;
        for (const Note& n : committedNotes())
            if (!ids.count(n.id))
                next.insert(n.id);
        select(next);
        return true;
    }
    default:
        return false;
    }
}

void DrumEditorView::layout() {
    float header = theme::metrics::kPaneHeader;
    float right = box.size.x - 6;
    maximizeButton->box.pos = Vec(right - kMaximizeButton, (header - kMaximizeButton) / 2);
    right -= kMaximizeButton + 6;
    right -= headerReserve;
    updateDevicesToggle(ctx, devicesToggle);
    // Right to left, as in the piano roll: the tool switch, the view switch, then the kit, which goes first when
    // the pane is narrow. Select and Draw stay longest.
    std::vector<ToolbarItem> items;
    items.push_back({hearButton, kToolButton, 1, 9});
    items.push_back({tools.more, toolSwitchCaretWidth(tools, kToolButton), 6, 9});
    items.push_back({tools.draw, kToolButton, 1, 10});
    items.push_back({tools.select, kToolButton, 6, 10});
    items.push_back({drumSwitch.first, kDrumSwitchWidth, 6, 4});
    items.push_back({kitButton, 44, 6, 3});
    // The clip chip first, then Devices, as in the piano roll. When the toolbar can't fit beside them, the chip
    // shortens to the track's name before buttons go.
    auto place = [&](bool shortText) {
        placeClipChip(ctx, clipChip, source.clip, shortText, box.size.x);
        devicesToggle->box = Rect(clipChip->box.getRight() + 4, 3, kDevicesToggleWidth, header - 6);
        drumSwitch.second->visible = false;
        fitToolbar(items, right, devicesToggle->box.getRight() + 6, (header - kToolButton) / 2, kToolButton,
                   [this](rack::widget::Widget* w, Rect r) {
                       if (w == drumSwitch.first) {
                           drumSwitch.second->visible = true;
                           placeDrumViewSwitch(drumSwitch, r);
                       } else {
                           w->box = r;
                       }
                   });
    };
    place(false);
    if (!kitButton->visible)
        place(true);

    float lane = ctx.surface == Surface::Panel ? kVelocityLanePanel : kVelocityLaneOverlay;
    // The device strip sits between the header and the grid. A short pane gives the grid the room: the velocity
    // lane shrinks, then hides.
    float top = header + topInset;
    constexpr float kMinGrid = 48, kMinLane = 24;
    float rest = std::max(0.f, box.size.y - top);
    if (rest - lane < kMinGrid)
        lane = std::max(0.f, rest - kMinGrid);
    if (lane < kMinLane)
        lane = 0;
    velocity->visible = lane > 0;
    float gridH = std::max(0.f, rest - lane);
    grid->box = Rect(0, top, box.size.x, gridH);
    velocity->box = Rect(0, top + gridH, box.size.x, box.size.y - top - gridH);
}

void DrumEditorView::step() {
    if (ctx.module()) {
        if (const Track* t = track()) {
            source.track = t->id;
            source.clip = clipActions::shownClip(ctx, *t);
        }
        layout();
    }
    EditorPane::step();
}

void DrumEditorView::draw(const DrawArgs& args) {
    const theme::Palette& c = theme::colors();
    float header = theme::metrics::kPaneHeader;
    nvgBeginPath(args.vg);
    nvgRect(args.vg, 0, 0, box.size.x, header);
    nvgFillColor(args.vg, c.bg1);
    nvgFill(args.vg);
    EditorPane::draw(args);
}

} // namespace tessera
