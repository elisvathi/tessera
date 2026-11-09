#include "ClipGestures.hpp"

#include "../../model/LauncherCommands.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../interaction/Navigation.hpp"
#include "../interaction/Snap.hpp"
#include "../views/Arranger.hpp"
#include "../views/ArrangerLanes.hpp"
#include "../views/LauncherPanel.hpp"
#include "../views/TimelineDraw.hpp"
#include "ClipActions.hpp"
#include "../../model/AudioEdits.hpp"
#include "../views/AudioClipDraw.hpp"

#include <cmath>

namespace tessera::clipGestures {

using arrange::IdSet;
using arrange::Layout;
using rack::math::Vec;

namespace {

constexpr double kVelocityPerPx = 1.0 / 150;
constexpr double kFineFactor = 0.1;

std::string beatsText(double ticks, TimeSig ts) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f beats", ticks / double(ticksPerBeat(ts)));
    return buf;
}

/// A length as a note value where it is one ("1/32", "1/8"), else in beats.
std::string noteValueText(Tick length) {
    Tick whole = 4 * kPpq;
    if (length > 0 && length < whole && whole % length == 0)
        return "1/" + std::to_string(whole / length);
    char buf[32];
    snprintf(buf, sizeof buf, "%.3g beats", double(length) / double(kPpq));
    return buf;
}

/// IDs for instances a gesture creates. A gesture rebuilds its preview many times; the pool hands out the same
/// IDs each time, so previews never use up fresh IDs.
class IdPool {
  public:
    explicit IdPool(ArrangerLanes& lanes) : lanes(lanes) {}
    void restart() { next = 0; }
    ArrangementId take() {
        if (next == ids.size())
            ids.push_back(lanes.newInstanceId());
        return ids[next++];
    }
    arrange::IdSource source() {
        return [this] { return take(); };
    }

  private:
    ArrangerLanes& lanes;
    std::vector<ArrangementId> ids;
    size_t next = 0;
};

/// What every clip gesture shares: the starting layout and the press point in content terms (tick and
/// unscrolled y), so edge auto-scroll never shifts the gesture's origin.
class ClipGesture : public DragGesture {
  public:
    ClipGesture(ArrangerLanes& lanes, Vec press)
        : lanes(lanes), before(lanes.committed()), pressTick(lanes.time().xToTick(press.x)),
          pressContentY(press.y + lanes.verticalScroll()), pool(lanes) {}

    void cancel() override { lanes.clearPreview(); }
    bool autoScrollX() const override { return true; }
    bool autoScrollY() const override { return true; }

  protected:
    double tickAt(Vec p) { return lanes.time().xToTick(p.x); }
    /// The press point in the view's current coordinates (it moves when the view scrolls).
    Vec pressPos() {
        return Vec(float(lanes.time().tickToX(pressTick)), float(pressContentY - lanes.verticalScroll()));
    }
    const Song& song() { return lanes.context().module()->song(); }
    TimeSig timeSig() { return song().timeSig; }
    Tick minLength() { return std::max<Tick>(1, lanes.snapStep()); }
    void preview(Layout layout) {
        result = std::move(layout);
        lanes.setPreview(result);
    }

    ArrangerLanes& lanes;
    Layout before;
    Layout result;
    double pressTick;
    double pressContentY;
    IdPool pool;
};

/// A gesture on the selection that grabbed one instance: its starting state anchors snapping and readouts.
class SelectionGesture : public ClipGesture {
  public:
    SelectionGesture(ArrangerLanes& lanes, Vec press, ArrangementId grabbed)
        : ClipGesture(lanes, press), ids(lanes.selection()) {
        ids.insert(grabbed);
        if (const arrange::Placed* p = arrange::find(before, grabbed))
            anchor = p->clip;
    }

  protected:
    IdSet ids;
    ArrangerClip anchor;
};

// ---- Move ---------------------------------------------------------------------------------------------------

class MoveGesture : public SelectionGesture {
  public:
    MoveGesture(ArrangerLanes& lanes, Vec press, ArrangementId grabbed, bool linked)
        : SelectionGesture(lanes, press, grabbed), linked(linked), order(lanes.trackOrder()) {
        if (const arrange::Placed* p = arrange::find(before, grabbed))
            pressIndex = int(std::find(order.begin(), order.end(), p->track) - order.begin());
    }

    void update(Vec pos, Modifiers mods) override {
        // Over an empty slot of the launcher panel, the clip goes into that slot instead.
        slotTrack = TrackId();
        if (LauncherPanel* panel = lanes.owner.panelView()) {
            Vec p = pos.plus(lanes.box.pos).minus(panel->box.pos);
            if (p.x >= 0 && p.x < panel->box.size.x) {
                LauncherPanel::Hit h = panel->hitTest(p);
                const Track* t = song().findTrack(h.track);
                bool empty = t && clipActions::fitsTrack(song(), anchor.clip, h.track) && h.scene >= 0 &&
                             h.scene < int(t->slots.size()) && !t->slots[size_t(h.scene)].clip;
                if (empty && (h.part == LauncherPanel::Hit::Part::Body || h.part == LauncherPanel::Hit::Part::Launch)) {
                    slotTrack = h.track;
                    slotScene = h.scene;
                }
                lanes.setPreview(before);
                result = before;
                return;
            }
        }
        double dt = tickAt(pos) - pressTick;
        int index = lanes.orderIndexAt(pos.y);
        int rows = index >= 0 && pressIndex < int(order.size()) ? index - pressIndex : 0;
        // Shift locks the drag to the axis it started along (EDITOR_UX §2).
        double dx = dt * lanes.time().pxPerTick(), dy = pos.y - pressPos().y;
        lock.apply(dx, dy, mods.shift);
        if (dx == 0)
            dt = 0;
        if (dy == 0)
            rows = 0;

        Tick start = lanes.snapMove(anchor.position, dt, anchor.length, mods.alt && !linked, ids);
        delta = std::max(start, Tick(0)) - anchor.position;
        trackDelta = rowsFit(rows) ? rows : 0;
        // Once Ctrl is down the drag stays a copy: letting go of Ctrl just before the mouse must not move the original.
        copying = copying || linked || mods.ctrl;

        pool.restart();
        Layout after;
        if (copying) {
            resultIds.clear();
            after = arrange::duplicate(before, ids, delta, order, trackDelta, pool.source(), &resultIds);
        } else {
            after = arrange::move(before, ids, delta, order, trackDelta);
            resultIds = ids;
        }
        preview(lanes.settle(after, resultIds, pool.source()));
    }

    void commit() override {
        if (slotTrack.valid()) {
            lanes.clearPreview();
            TesseraModule* m = lanes.context().module();
            if (m)
                m->perform(std::make_unique<SetSlot>(slotTrack, slotScene, std::nullopt, anchor.clip));
            return;
        }
        if (result == before) {
            lanes.clearPreview();
            return;
        }
        if (!copying && delta == 0 && trackDelta == 0) {
            lanes.clearPreview();
            return;
        }
        std::vector<Clip> newClips;
        if (copying && !linked) {
            NextId next = lanes.context().module()->ids();
            for (arrange::Placed& p : result) {
                const Clip* c = resultIds.count(p.clip.id) ? song().findClip(p.clip.clip) : nullptr;
                if (!c)
                    continue;
                newClips.push_back(copyClip(*c, next));
                p.clip.clip = newClips.back().id;
            }
        }
        std::string verb = !copying ? "Move " : linked ? "Duplicate " : "Copy ";
        lanes.commit(verb + arrange::countClips(ids.size()), before, result, resultIds, std::move(newClips));
    }

    bool autoScrollX() const override { return !slotTrack.valid(); }
    std::string readout() const override {
        if (slotTrack.valid())
            return "Put in slot " + std::to_string(slotScene + 1);
        const Song& s = lanes.context().module()->song();
        std::string text = timeline::positionText(double(anchor.position + delta), s.timeSig);
        if (trackDelta != 0 && pressIndex + trackDelta >= 0 && pressIndex + trackDelta < int(order.size()))
            if (const Track* t = s.findTrack(order[size_t(pressIndex + trackDelta)]))
                text += "  " + t->name;
        if (!copying)
            return text;
        return (linked ? "Linked copy  " : "Copy  ") + text;
    }
    CursorShape cursor() const override { return CursorShape::Move; }

  private:
    TrackId slotTrack; // over an empty launcher panel slot
    int slotScene = 0;
    /// Whether moving the selection `rows` tracks keeps every clip on a track of its own kind, audio or notes.
    bool rowsFit(int rows) {
        for (const arrange::Placed& p : before) {
            if (!ids.count(p.clip.id))
                continue;
            int at = int(std::find(order.begin(), order.end(), p.track) - order.begin()) + rows;
            if (at >= 0 && at < int(order.size()) && !clipActions::fitsTrack(song(), p.clip.clip, order[size_t(at)]))
                return false;
        }
        return true;
    }

    bool linked;
    std::vector<TrackId> order;
    int pressIndex = 0;
    AxisLock lock;
    Tick delta = 0;
    int trackDelta = 0;
    bool copying = false;
    IdSet resultIds;
};

// ---- Slip ---------------------------------------------------------------------------------------------------

class SlipGesture : public SelectionGesture {
  public:
    using SelectionGesture::SelectionGesture;

    void update(Vec pos, Modifiers mods) override {
        delta = snapPosition(tickAt(pos) - pressTick, lanes.snapStep());
        preview(arrange::slip(before, ids, delta, song().clips));
    }
    void commit() override {
        if (delta == 0) {
            lanes.clearPreview();
            return;
        }
        lanes.commit("Slip " + arrange::countClips(ids.size()), before, result, ids);
    }
    std::string readout() const override {
        return "Slip " + beatsText(double(delta), lanes.context().module()->song().timeSig);
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    Tick delta = 0;
};

// ---- Edges --------------------------------------------------------------------------------------------------

class EdgeGesture : public SelectionGesture {
  public:
    enum class Kind { End, Stretch, Start };
    EdgeGesture(ArrangerLanes& lanes, Vec press, ArrangementId grabbed, Kind kind)
        : SelectionGesture(lanes, press, grabbed), kind(kind) {}

    void update(Vec pos, Modifiers mods) override {
        double dt = tickAt(pos) - pressTick;
        Tick edge = kind == Kind::Start ? anchor.position : instanceEnd(anchor);
        // Alt started the stretch, so it can't also mean "free" there.
        Tick target = lanes.snapTick(double(edge) + dt, mods.alt && kind != Kind::Stretch, ids);
        delta = target - edge;
        Layout after;
        switch (kind) {
        case Kind::End:
            after = arrange::resizeEnd(before, ids, delta, minLength());
            // An audio clip that doesn't loop stops hard at the end of its file (§6.1).
            fileEnd = false;
            for (arrange::Placed& p : after) {
                const Clip* c = ids.count(p.clip.id) ? song().findClip(p.clip.clip) : nullptr;
                if (!c || !c->audio || c->loopMode == LoopMode::Loop)
                    continue;
                Tick most = std::max<Tick>(1, c->length - p.clip.offset);
                if (p.clip.length >= most) {
                    p.clip.length = most;
                    fileEnd = fileEnd || p.clip.id == anchor.id;
                }
            }
            break;
        case Kind::Stretch:
            after = arrange::stretchEnd(before, ids, delta, minLength());
            break;
        case Kind::Start:
            after = arrange::trimStart(before, ids, delta, minLength(), song().clips);
            break;
        }
        if (kind == Kind::Start) {
            const arrange::Placed* p = arrange::find(after, anchor.id);
            const Clip* c = p ? song().findClip(p->clip.clip) : nullptr;
            fileEnd = c && c->audio && c->loopMode != LoopMode::Loop && p->clip.offset == 0 && delta < 0 &&
                      p->clip.position > target;
        }
        pool.restart();
        preview(lanes.settle(after, ids, pool.source()));
        if (const arrange::Placed* p = arrange::find(result, anchor.id))
            now = p->clip;
    }
    void commit() override {
        if (delta == 0) {
            lanes.clearPreview();
            return;
        }
        const char* verb = kind == Kind::Stretch ? "Stretch " : kind == Kind::Start ? "Trim " : "Resize ";
        lanes.commit(verb + arrange::countClips(ids.size()), before, result, ids);
    }
    std::string readout() const override {
        TimeSig ts = lanes.context().module()->song().timeSig;
        if (kind == Kind::Stretch) {
            char text[32];
            snprintf(text, sizeof(text), "×%.3g", double(now.stretch));
            return std::string(text) + "  " + beatsText(double(now.length), ts);
        }
        if (kind == Kind::Start)
            return (fileEnd ? "Start of file  " : "") + timeline::positionText(double(now.position), ts);
        return (fileEnd ? "End of file  " : "") + beatsText(double(now.length), ts);
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    Kind kind;
    Tick delta = 0;
    ArrangerClip now;
    bool fileEnd = false; // an audio clip's edge stopped at its file's start or end
};

// ---- Audio: clip tempo, gain, fades -------------------------------------------------------------------------

/// Alt+drag on an audio clip's right edge: the clip tempo, the one place its speed is set. Making the clip twice as
/// long halves the speed: the audio was played at twice the tempo (docs/design/AUDIO_MIXER.md §6.1).
class ClipTempoGesture : public SelectionGesture {
  public:
    ClipTempoGesture(ArrangerLanes& lanes, Vec press, ArrangementId grabbed) : SelectionGesture(lanes, press, grabbed) {
        if (const Clip* c = song().findClip(anchor.clip))
            if (c->audio) {
                clip = c->id;
                startTempo = c->audio->sourceTempo;
                off = c->audio->follow == TempoFollow::Off;
            }
    }

    void update(Vec pos, Modifiers mods) override {
        if (!clip.valid() || off)
            return;
        double end = double(instanceEnd(anchor)) + (tickAt(pos) - pressTick);
        double length = std::max(double(minLength()), end - double(anchor.position));
        tempo = std::clamp(startTempo * length / double(std::max<Tick>(1, anchor.length)), kMinClipTempo, kMaxClipTempo);
        if (!(mods.shift))
            tempo = std::round(tempo * 10) / 10;
        // The preview: every copy of the clip at its new length.
        double factor = tempo / startTempo;
        Layout after = before;
        for (arrange::Placed& p : after)
            if (p.clip.clip == clip)
                p.clip.length = std::max<Tick>(1, Tick(std::llround(double(p.clip.length) * factor)));
        preview(std::move(after));
    }
    void commit() override {
        lanes.clearPreview();
        TesseraModule* m = lanes.context().module();
        if (m && clip.valid() && !off && std::abs(tempo - startTempo) > 1e-6)
            m->perform(setClipTempo(m->song(), clip, tempo));
    }
    std::string readout() const override {
        if (off)
            return "This clip plays at its own speed: set Follow tempo to change it";
        char text[48];
        snprintf(text, sizeof text, "%.4g \u2192 %.4g BPM", startTempo, tempo > 0 ? tempo : startTempo);
        return text;
    }
    CursorShape cursor() const override { return off ? CursorShape::NotAllowed : CursorShape::ResizeH; }

  private:
    ClipId clip;
    double startTempo = 120, tempo = 0;
    bool off = false;
};

/// Up or down on an audio clip's title bar: its gain, for every selected audio clip, with a soft snap at 0 dB. The
/// waveform scales while dragging; the edit lands on mouse-up.
class GainGesture : public SelectionGesture {
  public:
    GainGesture(ArrangerLanes& lanes, Vec press, ArrangementId grabbed) : SelectionGesture(lanes, press, grabbed) {
        for (const arrange::Placed& p : before)
            if (ids.count(p.clip.id))
                if (const Clip* c = song().findClip(p.clip.clip); c && c->audio &&
                    std::find(clips.begin(), clips.end(), c->id) == clips.end()) {
                    clips.push_back(c->id);
                    start.push_back(c->audio->gainDb);
                }
        if (const Clip* c = song().findClip(anchor.clip); c && c->audio)
            anchorStart = c->audio->gainDb;
    }

    void update(Vec pos, Modifiers mods) override {
        double dy = pos.y - pressPos().y;
        float raw = float(-dy * kGainDbPerPx * (mods.shift || mods.ctrl ? kFineFactor : 1));
        // A soft snap where the grabbed clip passes 0 dB.
        float target = anchorStart + raw;
        if (!mods.alt && std::abs(target) < kGainSnapDb)
            raw = -anchorStart;
        delta = raw;
        std::vector<std::pair<ClipId, float>> shown;
        for (size_t i = 0; i < clips.size(); i++)
            shown.push_back({clips[i], gainAfter(start[i])});
        lanes.previewGain(std::move(shown));
    }
    void commit() override {
        lanes.previewGain({});
        TesseraModule* m = lanes.context().module();
        if (m && delta != 0)
            m->perform(nudgeClipGain(m->song(), clips, delta));
    }
    void cancel() override { lanes.previewGain({}); }
    std::string readout() const override {
        std::string text = "Gain " + audioDraw::dbText(gainAfter(anchorStart));
        if (clips.size() > 1)
            text = std::to_string(clips.size()) + " clips \u00b7 " + text;
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    static constexpr double kGainDbPerPx = 0.1;
    static constexpr float kGainSnapDb = 0.6f;
    float gainAfter(float from) const {
        return from <= kSilentDb ? kSilentDb : std::clamp(from + delta, kSilentDb, kMaxClipGainDb);
    }
    std::vector<ClipId> clips;
    std::vector<float> start;
    float anchorStart = 0;
    float delta = 0;
};

/// A fade handle: drags the fade's length. Below one grid step it moves freely; beyond, it snaps to grid lines
/// (Alt frees it).
class FadeGesture : public SelectionGesture {
  public:
    FadeGesture(ArrangerLanes& lanes, Vec press, ArrangementId grabbed, bool in)
        : SelectionGesture(lanes, press, grabbed), in(in) {}

    void update(Vec pos, Modifiers mods) override {
        double dt = tickAt(pos) - pressTick;
        Tick from = in ? anchor.fadeIn.length : anchor.fadeOut.length;
        double raw = double(from) + (in ? dt : -dt);
        Tick step = lanes.snapStep();
        if (!mods.alt && step > 0 && raw >= double(step))
            raw = std::round(raw / double(step)) * double(step);
        length = std::clamp<Tick>(Tick(std::llround(raw)), 0, anchor.length - (in ? anchor.fadeOut.length : anchor.fadeIn.length));
        Layout after = before;
        for (arrange::Placed& p : after)
            if (p.clip.id == anchor.id)
                (in ? p.clip.fadeIn : p.clip.fadeOut).length = length;
        preview(std::move(after));
    }
    void commit() override {
        Tick from = in ? anchor.fadeIn.length : anchor.fadeOut.length;
        if (length == from) {
            lanes.clearPreview();
            return;
        }
        lanes.commit(in ? "Fade in" : "Fade out", before, result, {anchor.id});
    }
    std::string readout() const override {
        TesseraModule* m = lanes.context().module();
        double bpm = m && m->tempo() > 0 ? m->tempo() : 120;
        char text[64];
        snprintf(text, sizeof text, "%s %s \u00b7 %.0f ms", in ? "Fade in" : "Fade out", noteValueText(length).c_str(),
                 double(length) * secondsPerTick(bpm) * 1000);
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    bool in;
    Tick length = 0;
};

// ---- Curves and crossfades ----------------------------------------------------------------------------------

/// "Curve +40%", "Curve 0%".
std::string curveText(float curve) {
    char text[32];
    snprintf(text, sizeof text, curve == 0 ? "Curve 0%%" : "Curve %+.0f%%", double(curve) * 100);
    return text;
}

/// Bends a fade's curve, or both curves of a crossfade (`second` valid), with a vertical drag. Snaps to 0
/// (a straight fade, an equal-power crossfade) unless Alt.
class CurveGesture : public ClipGesture {
  public:
    CurveGesture(ArrangerLanes& lanes, Vec press, ArrangementId first, ArrangementId second, bool in)
        : ClipGesture(lanes, press), first(first), second(second), in(in) {
        if (const arrange::Placed* p = arrange::find(before, second.valid() ? second : first))
            start = second.valid() || in ? p->clip.fadeIn.curve : p->clip.fadeOut.curve;
        curve = start;
    }

    void update(Vec pos, Modifiers mods) override {
        float dy = pos.y - pressPos().y;
        curve = std::clamp(start - dy / kCurvePx * (mods.shift ? float(kFineFactor) : 1.f), -1.f, 1.f);
        if (!mods.alt && std::fabs(curve) < 0.05f)
            curve = 0;
        Layout after = before;
        for (arrange::Placed& p : after) {
            if (second.valid()) {
                if (p.clip.id == first)
                    p.clip.fadeOut.curve = curve;
                if (p.clip.id == second)
                    p.clip.fadeIn.curve = curve;
            } else if (p.clip.id == first) {
                (in ? p.clip.fadeIn : p.clip.fadeOut).curve = curve;
            }
        }
        preview(std::move(after));
    }
    void commit() override {
        if (curve == start) {
            lanes.clearPreview();
            return;
        }
        lanes.commit(second.valid() ? "Crossfade curve" : "Fade curve", before, result, {first});
    }
    std::string readout() const override { return curveText(curve); }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    static constexpr float kCurvePx = 80; // a drag this far bends the curve all the way
    ArrangementId first, second;
    bool in;
    float start = 0, curve = 0;
};

/// The crossfade handle: the overlap of two audio clips grows or shrinks evenly around its centre, as far as both
/// clips have audio.
class CrossfadeGesture : public ClipGesture {
  public:
    CrossfadeGesture(ArrangerLanes& lanes, Vec press, ArrangementId first, ArrangementId second)
        : ClipGesture(lanes, press), first(first), second(second) {
        const arrange::Placed* a = arrange::find(before, first);
        const arrange::Placed* b = arrange::find(before, second);
        if (a && b)
            start = std::max<Tick>(0, std::min(instanceEnd(a->clip), instanceEnd(b->clip)) - b->clip.position);
        length = start;
    }

    void update(Vec pos, Modifiers mods) override {
        const arrange::Placed* a = arrange::find(before, first);
        const arrange::Placed* b = arrange::find(before, second);
        if (!a || !b)
            return;
        double raw = double(start) + 2 * (tickAt(pos) - pressTick);
        Tick step = lanes.snapStep();
        if (!mods.alt && step > 0 && raw >= double(step))
            raw = std::round(raw / double(step)) * double(step);
        // As far as both clips have audio, and never past the other clip's far end.
        Tick grow = std::max<Tick>(0, Tick(std::llround(raw)) - start);
        Tick shrink = std::max<Tick>(0, start - Tick(std::llround(raw)));
        Tick laterRoom = b->clip.position - a->clip.position;
        Tick earlierRoom = instanceEnd(b->clip) - instanceEnd(a->clip);
        if (const Clip* c = song().findClip(a->clip.clip); c && c->loopMode != LoopMode::Loop)
            earlierRoom = std::min(earlierRoom, c->length - (a->clip.offset + a->clip.length));
        if (const Clip* c = song().findClip(b->clip.clip); c && c->loopMode != LoopMode::Loop)
            laterRoom = std::min(laterRoom, b->clip.offset);
        Tick outA = std::clamp<Tick>((grow + 1) / 2, 0, std::max<Tick>(0, earlierRoom));
        Tick outB = std::clamp<Tick>(grow / 2, 0, std::max<Tick>(0, laterRoom));
        Tick inA = std::min<Tick>(shrink / 2, a->clip.length - 1);
        Tick inB = std::min<Tick>((shrink + 1) / 2, b->clip.length - 1);
        Layout after = arrange::resizeEnd(before, {first}, outA - inA, 1);
        after = arrange::trimStart(after, {second}, inB - outB, 1, song().clips);
        pool.restart();
        after = lanes.settle(after, {first, second}, pool.source());
        const arrange::Placed* na = arrange::find(after, first);
        const arrange::Placed* nb = arrange::find(after, second);
        length = na && nb
                     ? std::max<Tick>(0, std::min(instanceEnd(na->clip), instanceEnd(nb->clip)) - nb->clip.position)
                     : 0;
        preview(std::move(after));
    }
    void commit() override {
        if (length == start) {
            lanes.clearPreview();
            return;
        }
        lanes.commit(length > 0 ? "Crossfade" : "Remove crossfade", before, result, {first, second});
    }
    std::string readout() const override {
        TesseraModule* m = lanes.context().module();
        double bpm = m && m->tempo() > 0 ? m->tempo() : 120;
        char text[64];
        snprintf(text, sizeof text, "Crossfade %s \u00b7 %.0f ms", noteValueText(length).c_str(),
                 double(length) * secondsPerTick(bpm) * 1000);
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    ArrangementId first, second;
    Tick start = 0, length = 0;
};

// ---- Velocity scale -----------------------------------------------------------------------------------------

class VelocityGesture : public SelectionGesture {
  public:
    using SelectionGesture::SelectionGesture;

    void update(Vec pos, Modifiers mods) override {
        double dy = pos.y - pressPos().y;
        delta = float(-dy * kVelocityPerPx * (mods.shift ? kFineFactor : 1));
        Layout after = before;
        for (arrange::Placed& p : after)
            if (ids.count(p.clip.id))
                p.clip.velocityScale = std::clamp(p.clip.velocityScale + delta, 0.f, 2.f);
        preview(std::move(after));
    }
    void commit() override {
        if (delta == 0) {
            lanes.clearPreview();
            return;
        }
        lanes.commit("Scale velocity", before, result, ids);
    }
    std::string readout() const override {
        float v = std::clamp(anchor.velocityScale + delta, 0.f, 2.f);
        return "Velocity " + std::to_string(int(std::lround(v * 100))) + "%";
    }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    float delta = 0;
};

/// The header strip: the first few pixels of movement decide between velocity (up/down) and moving.
class HeaderGesture : public DragGesture {
  public:
    HeaderGesture(ArrangerLanes& lanes, Vec press, ArrangementId grabbed)
        : lanes(lanes), press(press), grabbed(grabbed) {}

    void update(Vec pos, Modifiers mods) override {
        if (!inner) {
            Vec d = pos.minus(press);
            if (std::hypot(d.x, d.y) < AxisLock::kDecidePx)
                return;
            bool audio = false;
            if (const arrange::Placed* p = arrange::find(lanes.committed(), grabbed))
                if (const Clip* c = lanes.context().module()->song().findClip(p->clip.clip))
                    audio = c->isAudio();
            if (std::fabs(d.y) > std::fabs(d.x) && audio)
                inner = std::make_unique<GainGesture>(lanes, press, grabbed);
            else if (std::fabs(d.y) > std::fabs(d.x))
                inner = std::make_unique<VelocityGesture>(lanes, press, grabbed);
            else
                inner = std::make_unique<MoveGesture>(lanes, press, grabbed, mods.ctrl && mods.alt);
        }
        inner->update(pos, mods);
    }
    void commit() override {
        if (inner)
            inner->commit();
    }
    void cancel() override {
        if (inner)
            inner->cancel();
    }
    std::string readout() const override { return inner ? inner->readout() : ""; }
    CursorShape cursor() const override { return inner ? inner->cursor() : CursorShape::ResizeV; }
    bool autoScrollX() const override { return inner && inner->autoScrollX(); }
    bool autoScrollY() const override { return inner && inner->autoScrollY(); }

  private:
    ArrangerLanes& lanes;
    Vec press;
    ArrangementId grabbed;
    std::unique_ptr<DragGesture> inner;
};

// ---- Tools --------------------------------------------------------------------------------------------------

class DrawGesture : public ClipGesture {
  public:
    DrawGesture(ArrangerLanes& lanes, Vec press)
        : ClipGesture(lanes, press), track(lanes.trackOfRow(lanes.rowAt(press.y))), clip(lanes.newClipId()),
          instance(lanes.newInstanceId()) {
        update(press, Modifiers());
    }

    void update(Vec pos, Modifiers mods) override {
        Tick a = std::max<Tick>(0, lanes.snapTick(pressTick, mods.alt, {}));
        Tick b = std::max<Tick>(0, lanes.snapTick(tickAt(pos), mods.alt, {}));
        if (std::fabs(pos.x - pressPos().x) < AxisLock::kDecidePx) {
            // A click draws one bar.
            b = a + ticksPerBar(timeSig());
        }
        start = std::min(a, b);
        length = std::max(std::max(a, b) - start, minLength());
        arrange::Placed p{track, ArrangerClip{}};
        p.clip.id = instance;
        p.clip.clip = clip;
        p.clip.position = start;
        p.clip.length = length;
        Layout after = before;
        after.push_back(p);
        pool.restart();
        preview(lanes.settle(after, {instance}, pool.source()));
    }
    void commit() override {
        lanes.clearPreview();
        TesseraModule* m = lanes.context().module();
        const Track* onto = m->song().findTrack(track);
        if (!onto || onto->type == TrackType::Audio)
            return;
        m->perform(arrange::drawClip(m->song(), track, start, length, clip, instance,
                                     [this] { return lanes.newInstanceId(); }));
        lanes.selection() = {instance};
        lanes.owner.target = ArrangerView::Target::Clips;
        clipActions::select(lanes.context(), track, clip);
    }
    std::string readout() const override {
        TimeSig ts = lanes.context().module()->song().timeSig;
        return timeline::positionText(double(start), ts) + "  " + beatsText(double(length), ts);
    }
    CursorShape cursor() const override { return CursorShape::Crosshair; }

  private:
    TrackId track;
    ClipId clip;
    ArrangementId instance;
    Tick start = 0;
    Tick length = 0;
};

/// Erase and Mute tools: every clip the pointer passes over is collected; the preview shows the result.
class SweepGesture : public ClipGesture {
  public:
    SweepGesture(ArrangerLanes& lanes, Vec press, bool muting) : ClipGesture(lanes, press), muting(muting) {
        update(press, Modifiers());
    }

    void update(Vec pos, Modifiers mods) override {
        ArrangerLanes::Hit hit = lanes.hitTest(pos);
        if (hit.id.valid())
            touched.insert(hit.id);
        Layout after;
        for (const arrange::Placed& p : before) {
            if (!touched.count(p.clip.id)) {
                after.push_back(p);
            } else if (muting) {
                arrange::Placed q = p;
                q.clip.muted = !q.clip.muted;
                after.push_back(q);
            }
        }
        preview(std::move(after));
    }
    void commit() override {
        if (touched.empty()) {
            lanes.clearPreview();
            return;
        }
        std::string name = (muting ? "Mute " : "Delete ") + arrange::countClips(touched.size());
        lanes.commit(name, before, result, {});
    }
    CursorShape cursor() const override { return muting ? CursorShape::Arrow : CursorShape::NotAllowed; }

  private:
    bool muting;
    IdSet touched;
};

class RubberBandGesture : public ClipGesture {
  public:
    RubberBandGesture(ArrangerLanes& lanes, Vec press, Modifiers mods)
        : ClipGesture(lanes, press), start(lanes.selection()), mods(mods) {}

    void update(Vec pos, Modifiers) override {
        current = pos;
        Vec p0 = pressPos();
        double t0 = lanes.time().xToTick(std::min(p0.x, pos.x)), t1 = lanes.time().xToTick(std::max(p0.x, pos.x));
        float y0 = std::min(p0.y, pos.y), y1 = std::max(p0.y, pos.y);
        IdSet inside;
        for (const arrange::Placed& p : before) {
            int row = lanes.rowOf(p.track);
            if (row < 0)
                continue;
            auto [top, height] = lanes.instanceSpan(row, p);
            float bottom = top + height;
            // Touching counts (EDITOR_UX §2).
            if (double(instanceEnd(p.clip)) > t0 && double(p.clip.position) < t1 && bottom > y0 && top < y1)
                inside.insert(p.clip.id);
        }
        IdSet sel = mods.shift ? start : IdSet{};
        sel.insert(inside.begin(), inside.end());
        lanes.selection() = sel;
    }
    void commit() override { lanes.owner.target = ArrangerView::Target::Clips; }
    void cancel() override { lanes.selection() = start; }
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

  private:
    IdSet start;
    Modifiers mods;
    Vec current;
};

} // namespace

std::unique_ptr<DragGesture> move(ArrangerLanes& lanes, Vec press, ArrangementId grabbed, bool linkedCopy) {
    return std::make_unique<MoveGesture>(lanes, press, grabbed, linkedCopy);
}
std::unique_ptr<DragGesture> slip(ArrangerLanes& lanes, Vec press, ArrangementId grabbed) {
    return std::make_unique<SlipGesture>(lanes, press, grabbed);
}
std::unique_ptr<DragGesture> fade(ArrangerLanes& lanes, Vec press, ArrangementId grabbed, bool in) {
    return std::make_unique<FadeGesture>(lanes, press, grabbed, in);
}
std::unique_ptr<DragGesture> fadeCurve(ArrangerLanes& lanes, Vec press, ArrangementId grabbed, bool in) {
    return std::make_unique<CurveGesture>(lanes, press, grabbed, ArrangementId(), in);
}
std::unique_ptr<DragGesture> crossfade(ArrangerLanes& lanes, Vec press, ArrangementId first, ArrangementId second) {
    return std::make_unique<CrossfadeGesture>(lanes, press, first, second);
}
std::unique_ptr<DragGesture> crossfadeCurve(ArrangerLanes& lanes, Vec press, ArrangementId first,
                                            ArrangementId second) {
    return std::make_unique<CurveGesture>(lanes, press, first, second, true);
}
std::unique_ptr<DragGesture> resizeEnd(ArrangerLanes& lanes, Vec press, ArrangementId grabbed, bool stretch) {
    // On an audio clip, Alt+drag on the right edge sets the clip tempo instead of a stretch.
    if (stretch)
        if (const arrange::Placed* p = arrange::find(lanes.committed(), grabbed))
            if (const Clip* c = lanes.context().module()->song().findClip(p->clip.clip); c && c->audio)
                return std::make_unique<ClipTempoGesture>(lanes, press, grabbed);
    return std::make_unique<EdgeGesture>(lanes, press, grabbed,
                                         stretch ? EdgeGesture::Kind::Stretch : EdgeGesture::Kind::End);
}
std::unique_ptr<DragGesture> trimStart(ArrangerLanes& lanes, Vec press, ArrangementId grabbed) {
    return std::make_unique<EdgeGesture>(lanes, press, grabbed, EdgeGesture::Kind::Start);
}
std::unique_ptr<DragGesture> header(ArrangerLanes& lanes, Vec press, ArrangementId grabbed) {
    return std::make_unique<HeaderGesture>(lanes, press, grabbed);
}
std::unique_ptr<DragGesture> draw(ArrangerLanes& lanes, Vec press) {
    return std::make_unique<DrawGesture>(lanes, press);
}
std::unique_ptr<DragGesture> erase(ArrangerLanes& lanes, Vec press) {
    return std::make_unique<SweepGesture>(lanes, press, false);
}
std::unique_ptr<DragGesture> mute(ArrangerLanes& lanes, Vec press) {
    return std::make_unique<SweepGesture>(lanes, press, true);
}
std::unique_ptr<DragGesture> rubberBand(ArrangerLanes& lanes, Vec press, Modifiers mods) {
    return std::make_unique<RubberBandGesture>(lanes, press, mods);
}

} // namespace tessera::clipGestures
