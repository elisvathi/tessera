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
    MoveGesture(ArrangerLanes& lanes, Vec press, ArrangementId grabbed, bool unique)
        : SelectionGesture(lanes, press, grabbed), unique(unique), order(lanes.trackOrder()) {
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
                bool empty = t && h.scene >= 0 && h.scene < int(t->slots.size()) && !t->slots[size_t(h.scene)].clip;
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

        Tick start = lanes.snapMove(anchor.position, dt, anchor.length, mods.alt && !unique, ids);
        delta = std::max(start, Tick(0)) - anchor.position;
        trackDelta = rows;
        // Once Ctrl is down the drag stays a copy: letting go of Ctrl just before the mouse must not move the original.
        copying = copying || unique || mods.ctrl;

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
        if (unique) {
            // Ctrl/Cmd+Alt: each copy plays its own new clip.
            NextId next = lanes.context().module()->ids();
            for (arrange::Placed& p : result) {
                const Clip* c = resultIds.count(p.clip.id) ? song().findClip(p.clip.clip) : nullptr;
                if (!c)
                    continue;
                newClips.push_back(copyClip(*c, next));
                p.clip.clip = newClips.back().id;
            }
        }
        std::string verb = !copying ? "Move " : unique ? "Copy " : "Duplicate ";
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
        return (unique ? "Copy  " : "Linked copy  ") + text;
    }
    CursorShape cursor() const override { return CursorShape::Move; }

  private:
    TrackId slotTrack; // over an empty launcher panel slot
    int slotScene = 0;
    bool unique;
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
            break;
        case Kind::Stretch:
            after = arrange::stretchEnd(before, ids, delta, minLength());
            break;
        case Kind::Start:
            after = arrange::trimStart(before, ids, delta, minLength(), song().clips);
            break;
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
            return timeline::positionText(double(now.position), ts);
        return beatsText(double(now.length), ts);
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    Kind kind;
    Tick delta = 0;
    ArrangerClip now;
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
            if (std::fabs(d.y) > std::fabs(d.x))
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
        if (!track.valid())
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

std::unique_ptr<DragGesture> move(ArrangerLanes& lanes, Vec press, ArrangementId grabbed, bool uniqueCopy) {
    return std::make_unique<MoveGesture>(lanes, press, grabbed, uniqueCopy);
}
std::unique_ptr<DragGesture> slip(ArrangerLanes& lanes, Vec press, ArrangementId grabbed) {
    return std::make_unique<SlipGesture>(lanes, press, grabbed);
}
std::unique_ptr<DragGesture> resizeEnd(ArrangerLanes& lanes, Vec press, ArrangementId grabbed, bool stretch) {
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
