#include "Ruler.hpp"

#include "../../model/Playheads.hpp"
#include "../../model/TrackColors.hpp"
#include "../widgets/ColorMenuItem.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/MenuTextField.hpp"
#include "TimelineDraw.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

namespace {

constexpr double kClickSlopPx = 3;
constexpr double kRulerZoomPerPx = 0.01; // drag down 100 px = zoom in ×e
constexpr float kMarkerFlagWidth = 6;
constexpr float kMinOverviewWindow = 6;

double snapped(double tick, Tick step, bool free) { return free ? tick : snapTick(tick, step); }

std::string lengthText(Tick length, TimeSig ts) {
    double bars = double(length) / double(ticksPerBar(ts));
    char buf[32];
    if (std::fabs(bars - std::round(bars)) < 1e-9)
        snprintf(buf, sizeof(buf), "%d bar%s", int(std::round(bars)), std::round(bars) == 1 ? "" : "s");
    else
        snprintf(buf, sizeof(buf), "%.2f bars", bars);
    return buf;
}

} // namespace

void Ruler::autoScroll(double dx, double dy) {
    ctx.touchNavigation();
    viewport.time.scrollByPx(dx);
}

// ---- Gestures -----------------------------------------------------------------------------------------------

/// Draws a new loop region, moves it, or drags one of its edges.
/// Drags the end of the timeline clip the piano roll was opened from (Trello, 13 Oct): the clip plays longer or
/// shorter on the timeline. Snaps to the grid; Alt drags freely.
class InstanceEndGesture : public DragGesture {
  public:
    explicit InstanceEndGesture(Ruler& ruler) : ruler(ruler), end(ruler.source.instanceEnd()) {}

    void update(rack::math::Vec pos, Modifiers mods) override {
        double tick = ruler.viewport.time.xToTick(pos.x);
        end = std::max(1.0, snapped(tick, ruler.snapStep(), mods.alt));
        ruler.source.previewInstanceEnd(end);
    }
    void commit() override { ruler.source.setInstanceEnd(end); }
    void cancel() override { ruler.source.previewInstanceEnd(NAN); }
    std::string readout() const override {
        return "Ends " + timeline::positionText(end, ruler.source.timeSig()) + " on the timeline";
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }
    bool autoScrollX() const override { return true; }

  private:
    Ruler& ruler;
    double end;
};

/// Dragging the selected playhead's segment: an end to resize it, the bar to move it, the white tick to move where it
/// starts. Snapped to the grid (at most 1/16 steps); one undo step on release.
class PlayheadSegmentGesture : public DragGesture {
  public:
    PlayheadSegmentGesture(Ruler& ruler, TimelineSource::PlayheadSegment segment, Ruler::SegmentGrab grab, double press)
        : ruler(ruler), original(segment), now(segment), grab(grab), press(press) {}
    void update(rack::math::Vec pos, Modifiers mods) override {
        double step = double(std::min<Tick>(std::max<Tick>(1, ruler.snapStep()), kPpq / 4));
        double tick = ruler.viewport.time.xToTick(pos.x);
        double delta = std::round((tick - press) / step) * step;
        double most = playheads::kMaxRegionSteps * double(kPpq) / 4;
        now = original;
        switch (grab) {
        // The drag stops where the stored segment would: at the clip's start, and at the most its settings hold
        // (so nothing snaps back on release).
        case Ruler::SegmentGrab::Start:
            now.start = std::clamp(original.start + delta, std::max(original.clipStart, original.end - most),
                                   original.end - step);
            now.startTick = std::max(now.startTick, now.start);
            break;
        case Ruler::SegmentGrab::End:
            now.end = std::clamp(original.end + delta, original.start + step, original.start + most);
            break;
        case Ruler::SegmentGrab::Move:
            delta = std::clamp(delta, original.clipStart - original.start, original.clipStart + most - original.start);
            now.start += delta;
            now.end += delta;
            now.startTick += delta;
            break;
        case Ruler::SegmentGrab::StartTick:
            now.startTick = std::clamp(original.startTick + delta, original.start, original.end - step);
            break;
        default:
            break;
        }
        ruler.segmentPreview = now;
    }
    void commit() override {
        ruler.segmentPreview.reset();
        if (grab == Ruler::SegmentGrab::StartTick)
            ruler.source.setPlayheadStart(now.id, now.startTick);
        else if (now.start != original.start || now.end != original.end)
            ruler.source.setPlayheadSegment(now.id, now.start, now.end);
    }
    void cancel() override { ruler.segmentPreview.reset(); }
    std::string readout() const override {
        return grab == Ruler::SegmentGrab::StartTick ? "Where it starts" : "Where it plays";
    }
    CursorShape cursor() const override {
        return grab == Ruler::SegmentGrab::Move ? CursorShape::Move : CursorShape::ResizeH;
    }

  private:
    Ruler& ruler;
    TimelineSource::PlayheadSegment original, now;
    Ruler::SegmentGrab grab;
    double press;
};

class LoopGesture : public DragGesture {
  public:
    enum class Mode { Create, Move, ResizeStart, ResizeEnd };

    LoopGesture(Ruler& ruler, Mode mode, double pressTick)
        : ruler(ruler), mode(mode), pressTick(pressTick), start(ruler.source.loopStart()), end(ruler.source.loopEnd()) {
    }

    void update(rack::math::Vec pos, Modifiers mods) override {
        Tick step = ruler.snapStep();
        double tick = ruler.viewport.time.xToTick(pos.x);
        Tick a = start, b = end;
        switch (mode) {
        case Mode::Create: {
            Tick t0 = Tick(snapped(pressTick, step, mods.alt));
            Tick t1 = Tick(snapped(tick, step, mods.alt));
            a = std::max<Tick>(0, std::min(t0, t1));
            b = std::max(t0, t1);
            if (b <= a)
                b = a + step;
            break;
        }
        case Mode::Move: {
            Tick delta = Tick(snapped(tick - pressTick, step, mods.alt));
            delta = std::max(delta, -start);
            a = start + delta;
            b = end + delta;
            break;
        }
        case Mode::ResizeStart:
            a = std::clamp<Tick>(Tick(snapped(tick, step, mods.alt)), 0, end - 1);
            break;
        case Mode::ResizeEnd:
            b = std::max<Tick>(Tick(snapped(tick, step, mods.alt)), start + 1);
            break;
        }
        ruler.loopPreview = std::make_pair(a, b);
    }
    void commit() override {
        auto loop = *ruler.loopPreview;
        ruler.loopPreview.reset();
        if (loop.first != start || loop.second != end)
            ruler.source.setLoop(loop.first, loop.second);
    }
    void cancel() override { ruler.loopPreview.reset(); }

    std::string readout() const override {
        if (!ruler.loopPreview)
            return {};
        TimeSig ts = ruler.source.timeSig();
        auto [a, b] = *ruler.loopPreview;
        return "Loop " + timeline::positionText(double(a), ts) + " – " + timeline::positionText(double(b), ts) + "  (" +
               lengthText(b - a, ts) + ")";
    }
    CursorShape cursor() const override { return mode == Mode::Move ? CursorShape::Move : CursorShape::ResizeH; }
    bool autoScrollX() const override { return true; }

  private:
    Ruler& ruler;
    Mode mode;
    double pressTick;
    Tick start, end;
};

/// Drags a marker; a click without movement jumps the playhead to it.
class MarkerGesture : public DragGesture {
  public:
    MarkerGesture(Ruler& ruler, std::vector<Marker> markers, int index, rack::math::Vec press)
        : ruler(ruler), original(std::move(markers)), index(index), press(press) {}

    void update(rack::math::Vec pos, Modifiers mods) override {
        if (pos.minus(press).norm() > kClickSlopPx)
            moved = true;
        if (!moved)
            return;
        std::vector<Marker> m = original;
        double tick = snapped(ruler.viewport.time.xToTick(pos.x), ruler.snapStep(), mods.alt);
        m[size_t(index)].position = std::max<Tick>(0, Tick(tick));
        ruler.markersPreview = m;
    }
    void commit() override {
        if (!moved) {
            ruler.source.seek(double(original[size_t(index)].position), false);
            return;
        }
        std::vector<Marker> m = *ruler.markersPreview;
        ruler.markersPreview.reset();
        std::stable_sort(m.begin(), m.end(), [](const Marker& a, const Marker& b) { return a.position < b.position; });
        ruler.source.setMarkers(std::move(m), "Move marker");
    }
    void cancel() override { ruler.markersPreview.reset(); }
    std::string readout() const override {
        if (!ruler.markersPreview)
            return {};
        return timeline::positionText(double((*ruler.markersPreview)[size_t(index)].position), ruler.source.timeSig());
    }
    CursorShape cursor() const override { return moved ? CursorShape::ResizeH : CursorShape::Hand; }
    bool autoScrollX() const override { return moved; }

  private:
    Ruler& ruler;
    std::vector<Marker> original;
    int index;
    rack::math::Vec press;
    bool moved = false;
};

/// The minimap: drag the window to scroll, drag its edges to zoom, press elsewhere to jump there.
class OverviewGesture : public DragGesture {
  public:
    enum class Mode { Scroll, ResizeLeft, ResizeRight };

    OverviewGesture(Ruler& ruler, Mode mode, rack::math::Vec press)
        : ruler(ruler), mode(mode), press(press), start(ruler.viewport.time.state()) {}

    void update(rack::math::Vec pos, Modifiers mods) override {
        TimeAxis& t = ruler.viewport.time;
        t.restore(start);
        double tickAt = ruler.overviewTickAt(pos.x);
        switch (mode) {
        case Mode::Scroll:
            t.scrollTo(start.left + (tickAt - ruler.overviewTickAt(press.x)));
            break;
        case Mode::ResizeLeft:
            t.showRange(std::min(tickAt, t.right() - 1), t.right());
            break;
        case Mode::ResizeRight:
            t.showRange(t.left(), std::max(tickAt, t.left() + 1));
            break;
        }
        ruler.ctx.touchNavigation();
    }
    void cancel() override { ruler.viewport.time.restore(start); }
    CursorShape cursor() const override { return mode == Mode::Scroll ? CursorShape::Move : CursorShape::ResizeH; }

  private:
    Ruler& ruler;
    Mode mode;
    rack::math::Vec press;
    TimeAxis::State start;
};

/// The time ruler: a click moves the playhead; dragging zooms (down = in) around the click point while
/// horizontal movement pans.
class RulerTimeGesture : public DragGesture {
  public:
    RulerTimeGesture(Ruler& ruler, rack::math::Vec press)
        : ruler(ruler), press(press), start(ruler.viewport.time.state()) {}

    void update(rack::math::Vec pos, Modifiers mods) override {
        if (pos.minus(press).norm() > kClickSlopPx)
            zooming = true;
        free = mods.alt;
        if (!zooming)
            return;
        TimeAxis& t = ruler.viewport.time;
        t.restore(start);
        t.setZoom(start.pxPerTick * std::exp((pos.y - press.y) * kRulerZoomPerPx), press.x);
        t.scrollByPx(press.x - pos.x);
        ruler.ctx.touchNavigation();
    }
    void commit() override {
        if (zooming || !ruler.source.canSeek())
            return;
        double tick = ruler.viewport.time.xToTick(press.x);
        ruler.source.seek(std::max(0.0, snapped(tick, ruler.snapStep(), free)), false);
    }
    void cancel() override {
        if (zooming)
            ruler.viewport.time.restore(start);
    }
    CursorShape cursor() const override { return zooming ? CursorShape::ResizeV : CursorShape::Arrow; }

  private:
    Ruler& ruler;
    rack::math::Vec press;
    TimeAxis::State start;
    bool zooming = false;
    bool free = false;
};

// ---- Ruler --------------------------------------------------------------------------------------------------

Ruler::Strips Ruler::stripsFor(Surface surface) {
    using namespace theme::metrics;
    if (surface == Surface::Panel)
        return {kMarkerRowPanel, kLoopStripPanel, kTimeRulerPanel, kOverviewPanel};
    return {kMarkerRow, kLoopStrip, kTimeRuler, kOverview};
}

float Ruler::heightFor(TimelineSource& source, Surface surface) {
    Strips s = stripsFor(surface);
    float segments = source.playheadSegments().empty() ? 0 : theme::metrics::kSegmentRow;
    return segments + (source.hasMarkers() ? s.markers : 0) + s.loop + s.time + s.overview;
}

Tick Ruler::snapStep() { return gridSpacing(viewport.time.pxPerTick(), source.timeSig()).line; }

Ruler::Zone Ruler::zoneAt(float y) {
    if (y < segmentsBottom())
        return Zone::Segments;
    if (y < markersBottom())
        return Zone::Markers;
    if (y < loopBottom())
        return Zone::Loop;
    if (y < timeBottom())
        return Zone::Time;
    return Zone::Overview;
}

int Ruler::markerAt(const std::vector<Marker>& markers, float x) {
    for (int i = int(markers.size()) - 1; i >= 0; i--) {
        float mx = float(viewport.time.tickToX(double(markers[size_t(i)].position)));
        if (x >= mx - 2 && x <= mx + kMarkerFlagWidth + 40)
            return i;
    }
    return -1;
}

double Ruler::overviewTickAt(float x) { return double(x) / box.size.x * source.contentEnd(); }

rack::math::Rect Ruler::overviewWindow() {
    double end = source.contentEnd();
    float x0 = float(viewport.time.left() / end) * box.size.x;
    float x1 = float(viewport.time.right() / end) * box.size.x;
    if (x1 - x0 < kMinOverviewWindow) {
        float mid = (x0 + x1) / 2;
        x0 = mid - kMinOverviewWindow / 2;
        x1 = mid + kMinOverviewWindow / 2;
    }
    return rack::math::Rect(x0, timeBottom(), x1 - x0, strips().overview);
}

std::unique_ptr<DragGesture> Ruler::startGesture(rack::math::Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return nullptr;
    const float edge = theme::metrics::kEdgeHit;
    TimeAxis& t = viewport.time;
    switch (zoneAt(pos.y)) {
    case Zone::Markers: {
        std::vector<Marker> m = source.markers();
        int i = markerAt(m, pos.x);
        if (i >= 0)
            return std::make_unique<MarkerGesture>(*this, std::move(m), i, pos);
        return nullptr;
    }
    case Zone::Segments: {
        TimelineSource::PlayheadSegment segment{};
        if (SegmentGrab grab = segmentGrabAt(pos.x, &segment); grab != SegmentGrab::None)
            return std::make_unique<PlayheadSegmentGesture>(*this, segment, grab, t.xToTick(pos.x));
        return nullptr;
    }
    case Zone::Loop: {
        if (std::isfinite(source.instanceEnd()) && instanceTag().grow(rack::math::Vec(edge / 2, 0)).contains(pos))
            return std::make_unique<InstanceEndGesture>(*this);
        if (source.loopLocked())
            return nullptr;
        float x0 = float(t.tickToX(double(source.loopStart())));
        float x1 = float(t.tickToX(double(source.loopEnd())));
        LoopGesture::Mode mode = LoopGesture::Mode::Create;
        if (std::fabs(pos.x - x0) <= edge)
            mode = LoopGesture::Mode::ResizeStart;
        else if (std::fabs(pos.x - x1) <= edge)
            mode = LoopGesture::Mode::ResizeEnd;
        else if (pos.x > x0 && pos.x < x1)
            mode = LoopGesture::Mode::Move;
        return std::make_unique<LoopGesture>(*this, mode, t.xToTick(pos.x));
    }
    case Zone::Time:
        return std::make_unique<RulerTimeGesture>(*this, pos);
    case Zone::Overview: {
        rack::math::Rect w = overviewWindow();
        if (std::fabs(pos.x - w.pos.x) <= edge / 2)
            return std::make_unique<OverviewGesture>(*this, OverviewGesture::Mode::ResizeLeft, pos);
        if (std::fabs(pos.x - (w.pos.x + w.size.x)) <= edge / 2)
            return std::make_unique<OverviewGesture>(*this, OverviewGesture::Mode::ResizeRight, pos);
        if (pos.x < w.pos.x || pos.x > w.pos.x + w.size.x) {
            // Jump: center the view on the pressed point, then keep dragging from there.
            t.scrollTo(overviewTickAt(pos.x) - t.visibleTicks() / 2);
            ctx.touchNavigation();
        }
        return std::make_unique<OverviewGesture>(*this, OverviewGesture::Mode::Scroll, pos);
    }
    }
    return nullptr;
}

void Ruler::click(rack::math::Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_RIGHT || zoneAt(pos.y) != Zone::Markers)
        return;
    std::vector<Marker> m = source.markers();
    if (int i = markerAt(m, pos.x); i >= 0)
        openMarkerMenu(m[size_t(i)].id);
}

void Ruler::changeMarker(MarkerId id, const std::string& undoName,
                         const std::function<void(std::vector<Marker>&, size_t)>& change) {
    std::vector<Marker> m = source.markers();
    auto it = std::find_if(m.begin(), m.end(), [id](const Marker& x) { return x.id == id; });
    if (it == m.end())
        return;
    change(m, size_t(it - m.begin()));
    source.setMarkers(std::move(m), undoName);
}

void Ruler::startMarkerRename(MarkerId id) {
    std::vector<Marker> m = source.markers();
    auto it = std::find_if(m.begin(), m.end(), [id](const Marker& x) { return x.id == id; });
    if (it == m.end())
        return;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Marker name"));
    auto* field = new MenuTextField(it->name, [this, id](const std::string& name) {
        // The marker may be gone by the time the field closes (an undo meanwhile); changeMarker checks.
        changeMarker(id, "Rename marker to " + name, [&](std::vector<Marker>& m, size_t i) { m[i].name = name; });
    });
    menu->addChild(field);
    APP->event->setSelectedWidget(field);
}

void Ruler::openMarkerMenu(MarkerId id) {
    std::vector<Marker> m = source.markers();
    auto it = std::find_if(m.begin(), m.end(), [id](const Marker& x) { return x.id == id; });
    if (it == m.end())
        return;
    uint32_t current = it->color;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(it->name.empty() ? "Marker" : it->name));
    menu->addChild(rack::createMenuItem("Rename", "double-click", [this, id] { startMarkerRename(id); }));
    menu->addChild(rack::createSubmenuItem("Colour", "", [this, id, current](rack::ui::Menu* sub) {
        auto setColor = [this, id](uint32_t color) {
            changeMarker(id, "Change marker colour", [color](std::vector<Marker>& m, size_t i) { m[i].color = color; });
        };
        sub->addChild(
            rack::createCheckMenuItem("None", "", [current] { return current == 0; }, [setColor] { setColor(0); }));
        for (int i = 0; i < 16; i++) {
            uint32_t color = kTrackColors[i];
            sub->addChild(new ColorMenuItem("Colour " + std::to_string(i + 1), color, current == color,
                                            [setColor, color] { setColor(color); }));
        }
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Delete", "", [this, id] {
        changeMarker(id, "Delete marker", [](std::vector<Marker>& m, size_t i) { m.erase(m.begin() + long(i)); });
    }));
}

void Ruler::doubleClick(rack::math::Vec pos, Modifiers mods) {
    TimeAxis& t = viewport.time;
    switch (zoneAt(pos.y)) {
    case Zone::Segments:
        return;
    case Zone::Markers: {
        std::vector<Marker> m = source.markers();
        if (int i = markerAt(m, pos.x); i >= 0) {
            startMarkerRename(m[size_t(i)].id);
            return;
        }
        Marker marker;
        marker.id = source.newMarkerId();
        marker.position = std::max<Tick>(0, Tick(snapped(t.xToTick(pos.x), snapStep(), mods.alt)));
        marker.name = "Marker " + std::to_string(m.size() + 1);
        m.push_back(marker);
        std::stable_sort(m.begin(), m.end(), [](const Marker& a, const Marker& b) { return a.position < b.position; });
        source.setMarkers(std::move(m), "Add marker");
        break;
    }
    case Zone::Loop:
        source.toggleLoop();
        break;
    case Zone::Time:
        if (source.canSeek())
            source.seek(std::max(0.0, snapped(t.xToTick(pos.x), snapStep(), mods.alt)), true);
        break;
    case Zone::Overview: {
        TimeAxis copy = t;
        copy.showRange(0, source.contentEnd());
        viewport.jump(copy.state());
        ctx.touchNavigation();
        break;
    }
    }
}

CursorShape Ruler::hoverCursor(rack::math::Vec pos, Modifiers mods) {
    const float edge = theme::metrics::kEdgeHit;
    TimeAxis& t = viewport.time;
    bool onLockedLoop = zoneAt(pos.y) == Zone::Loop && source.loopLocked() &&
                        pos.x >= float(t.tickToX(double(source.loopStart()))) &&
                        pos.x <= float(t.tickToX(double(source.loopEnd())));
    if (onLockedLoop != tipOn) {
        tipOn = onLockedLoop;
        tip.leave();
        if (tipOn) {
            tip.setText("Loop " + lengthText(source.loopEnd() - source.loopStart(), source.timeSig()) +
                        ": set by the lane lengths. It runs until every lane lines up again");
            tip.enter();
        }
    }
    switch (zoneAt(pos.y)) {
    case Zone::Markers:
        return markerAt(source.markers(), pos.x) >= 0 ? CursorShape::Hand : CursorShape::Arrow;
    case Zone::Segments: {
        SegmentGrab grab = segmentGrabAt(pos.x, nullptr);
        return grab == SegmentGrab::None   ? CursorShape::Arrow
               : grab == SegmentGrab::Move ? CursorShape::Move
                                           : CursorShape::ResizeH;
    }
    case Zone::Loop: {
        if (std::isfinite(source.instanceEnd()) && instanceTag().grow(rack::math::Vec(edge / 2, 0)).contains(pos))
            return CursorShape::ResizeH;
        if (source.loopLocked())
            return CursorShape::Arrow;
        float x0 = float(t.tickToX(double(source.loopStart())));
        float x1 = float(t.tickToX(double(source.loopEnd())));
        if (std::fabs(pos.x - x0) <= edge || std::fabs(pos.x - x1) <= edge)
            return CursorShape::ResizeH;
        return pos.x > x0 && pos.x < x1 ? CursorShape::Move : CursorShape::Arrow;
    }
    case Zone::Time:
        return CursorShape::ResizeV;
    case Zone::Overview: {
        rack::math::Rect w = overviewWindow();
        if (std::fabs(pos.x - w.pos.x) <= edge / 2 || std::fabs(pos.x - (w.pos.x + w.size.x)) <= edge / 2)
            return CursorShape::ResizeH;
        return CursorShape::Move;
    }
    }
    return CursorShape::Arrow;
}

void Ruler::step() {
    InteractiveView::step();
    tip.step();
}

void Ruler::onLeave(const LeaveEvent& e) {
    tipOn = false;
    tip.leave();
    InteractiveView::onLeave(e);
}

void Ruler::wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) {
    ctx.touchNavigation();
    viewport.time.scrollByPx(-delta.x);
    if (delta.y != 0)
        viewport.time.animateZoomAt(pos.x, wheelZoomFactor(delta.y), kZoomAnimationSeconds);
}

// ---- Drawing ------------------------------------------------------------------------------------------------

void Ruler::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);

    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    if (source.hasMarkers()) {
        nvgSave(vg);
        nvgTranslate(vg, 0, segmentsBottom());
        drawMarkers(vg);
        nvgRestore(vg);
    }
    drawLoop(vg);
    drawPlayheadSegments(vg);
    drawTimeLabels(vg);
    drawOverview(vg);

    double p = source.playhead();
    if (std::isfinite(p)) {
        float x = float(viewport.time.tickToX(p));
        nvgSave(vg);
        nvgTranslate(vg, 0, markersBottom());
        timeline::drawPlayhead(vg, x, timeBottom() - markersBottom(), true);
        nvgRestore(vg);
    }
    if (ctx.snapAxis == &viewport && std::isfinite(ctx.snapMark)) {
        // Where the dragged note snaps, while dragging (EDITOR_UX §8).
        float x = float(viewport.time.tickToX(ctx.snapMark));
        nvgBeginPath(vg);
        nvgRect(vg, x - 1, timeBottom() - 7, 2, 7);
        nvgFillColor(vg, c.accent);
        nvgFill(vg);
    }
    nvgRestore(vg);

    nvgBeginPath(vg);
    nvgMoveTo(vg, 0, box.size.y - 0.5f);
    nvgLineTo(vg, box.size.x, box.size.y - 0.5f);
    nvgStrokeColor(vg, c.line1);
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
}

void Ruler::drawMarkers(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    std::vector<Marker> markers = markersPreview ? *markersPreview : source.markers();
    draw::TextStyle text{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.bg0,
                         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    float h = strips().markers;
    for (const Marker& m : markers) {
        float x = std::round(float(viewport.time.tickToX(double(m.position))));
        if (x < -200 || x > box.size.x)
            continue;
        NVGcolor color = m.color ? theme::track(m.color) : c.queue;
        text.color = m.color ? c.onTile : c.ink;
        float width = kMarkerFlagWidth + draw::textWidth(vg, text, m.name) + 6;
        draw::roundedRect(vg, rack::math::Rect(x, 1, width, h - 2), 2, color);
        draw::text(vg, text, x + kMarkerFlagWidth - 2, h / 2, m.name);
    }
}

Ruler::SegmentGrab Ruler::segmentGrabAt(float x, TimelineSource::PlayheadSegment* segment) {
    for (const TimelineSource::PlayheadSegment& sg : source.playheadSegments()) {
        if (!sg.selected)
            continue;
        float x0 = float(viewport.time.tickToX(sg.start)), x1 = float(viewport.time.tickToX(sg.end));
        float xs = float(viewport.time.tickToX(sg.startTick));
        if (segment)
            *segment = sg;
        // Each grip reaches a little way into the bar, but never past its middle, so a short segment keeps both.
        float inside = std::min(theme::metrics::kSegmentGrip + 6, (x1 - x0) / 2);
        if (x >= x0 - 7 && x <= x0 + inside)
            return SegmentGrab::Start;
        if (x >= x1 - inside && x <= x1 + 7)
            return SegmentGrab::End;
        if (sg.startTick > sg.start && std::fabs(x - xs) <= 4)
            return SegmentGrab::StartTick;
        if (x > x0 && x < x1)
            return SegmentGrab::Move;
    }
    return SegmentGrab::None;
}

void Ruler::drawPlayheadSegments(NVGcontext* vg) {
    float top = 0;
    float h = segmentsBottom();
    if (h <= 0)
        return;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, h);
    nvgFillColor(vg, theme::colors().bg0);
    nvgFill(vg);
    for (TimelineSource::PlayheadSegment sg : source.playheadSegments()) {
        if (segmentPreview && segmentPreview->id == sg.id)
            sg = *segmentPreview;
        NVGcolor color = theme::playheadColor(sg.color);
        float x0 = float(viewport.time.tickToX(sg.start)), x1 = float(viewport.time.tickToX(sg.end));
        if (!sg.selected) {
            nvgBeginPath(vg);
            nvgRect(vg, x0, top + h - 3, std::max(1.f, x1 - x0), 2);
            nvgFillColor(vg, nvgTransRGBAf(color, 0.7f));
            nvgFill(vg);
            continue;
        }
        // Drawn like the clip loop bar, in the playhead's colour, with solid grips standing out at its ends.
        float y = top + 3, bh = h - 6;
        rack::math::Rect bar(x0, y, std::max(2.f, x1 - x0), bh);
        draw::roundedRect(vg, bar, 2, nvgTransRGBAf(color, 0.35f));
        draw::roundedRectStroke(vg, bar, 2, color, 1);
        float grip = theme::metrics::kSegmentGrip;
        draw::roundedRect(vg, rack::math::Rect(x0 - 2, top + 1, grip, h - 2), 1.5f, color);
        draw::roundedRect(vg, rack::math::Rect(x1 - grip + 2, top + 1, grip, h - 2), 1.5f, color);
        draw::TextStyle label{theme::fonts::uiStrong(), theme::metrics::kTextRuler, theme::colors().text0,
                              NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        float nameX = x0 + grip + 3;
        if (!sg.name.empty() && nameX + draw::textWidth(vg, label, sg.name) < x1 - grip - 3)
            draw::text(vg, label, nameX, y + bh / 2, sg.name);
        if (sg.startTick > sg.start) {
            // The start: a tick across the bar with a small ▼ over it.
            float xs = float(viewport.time.tickToX(sg.startTick));
            nvgBeginPath(vg);
            nvgRect(vg, xs - 1, y, 3, bh);
            nvgMoveTo(vg, xs - 3, top);
            nvgLineTo(vg, xs + 4, top);
            nvgLineTo(vg, xs + 0.5f, y);
            nvgClosePath(vg);
            nvgFillColor(vg, theme::colors().text0);
            nvgFill(vg);
        }
    }
}

void Ruler::drawLoop(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    float top = markersBottom();
    float h = strips().loop;
    nvgBeginPath(vg);
    nvgRect(vg, 0, top, box.size.x, h);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);

    auto [a, b] = loopPreview ? *loopPreview : std::make_pair(source.loopStart(), source.loopEnd());
    float x0 = float(viewport.time.tickToX(double(a)));
    float x1 = float(viewport.time.tickToX(double(b)));
    NVGcolor color = source.loopOn() ? nvgTransRGBAf(c.accent, 0.6f) : nvgTransRGBAf(c.text2, 0.5f);
    // Opened from a timeline clip, the strip's lower edge holds a second bracket: the span that clip plays, with a
    // handle at its end to drag (Trello, 13 Oct). The loop bar gives it room.
    bool instance = std::isfinite(source.instanceEnd());
    float loopHeight = instance ? h - 7 : h - 4;
    rack::math::Rect bar(x0, top + 2, std::max(1.f, x1 - x0), loopHeight);
    // A locked loop is an outline: it shows where the loop is, but has nothing to grab.
    if (source.loopLocked() && source.loopOn()) {
        draw::roundedRect(vg, bar, 2, nvgTransRGBAf(c.accent, 0.2f));
        draw::roundedRectStroke(vg, bar, 2, color, 1);
    } else {
        draw::roundedRect(vg, bar, 2, color);
    }
    if (instance) {
        float s0 = float(viewport.time.tickToX(source.instanceStart()));
        float s1 = float(viewport.time.tickToX(source.instanceEnd()));
        draw::roundedRect(vg, rack::math::Rect(s0, top + h - 4, std::max(1.f, s1 - s0), 3), 1.5f, c.queue);
        draw::roundedRect(vg, instanceTag(), 1.5f, c.queue);
    }
}

rack::math::Rect Ruler::instanceTag() {
    float x = float(viewport.time.tickToX(source.instanceEnd()));
    return rack::math::Rect(x - 3, markersBottom() + 2, 4, strips().loop - 2);
}

void Ruler::drawTimeLabels(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    TimeAxis& t = viewport.time;
    TimeSig ts = source.timeSig();
    GridSpacing g = gridSpacing(t.pxPerTick(), ts);
    float top = loopBottom();
    float h = strips().time;

    nvgBeginPath(vg);
    Tick first = Tick(std::floor(t.left() / double(g.line))) * g.line;
    for (Tick tick = first; tick <= t.right(); tick += g.line) {
        float x = std::round(float(t.tickToX(double(tick)))) + 0.5f;
        float len = lineStrength(tick, ts) == LineStrength::Bar    ? h
                    : lineStrength(tick, ts) == LineStrength::Beat ? h * 0.45f
                                                                   : h * 0.25f;
        nvgMoveTo(vg, x, top + h - len);
        nvgLineTo(vg, x, top + h);
    }
    nvgStrokeColor(vg, c.line2);
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);

    draw::TextStyle label{theme::fonts::ui(), theme::metrics::kTextRuler, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
    Tick firstLabel = Tick(std::floor(t.left() / double(g.label))) * g.label;
    for (Tick tick = firstLabel; tick <= t.right(); tick += g.label) {
        float x = float(t.tickToX(double(tick)));
        draw::text(vg, label, x + 3, top + 2, timeline::positionLabel(tick, g.label, ts));
    }
}

void Ruler::drawOverview(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    float top = timeBottom();
    float h = strips().overview;
    double end = source.contentEnd();
    nvgBeginPath(vg);
    nvgRect(vg, 0, top, box.size.x, h);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);

    float lx0 = float(double(source.loopStart()) / end) * box.size.x;
    float lx1 = float(double(source.loopEnd()) / end) * box.size.x;
    nvgBeginPath(vg);
    nvgRect(vg, lx0, top + h - 2, std::max(1.f, lx1 - lx0), 2);
    nvgFillColor(vg, source.loopOn() ? nvgTransRGBAf(c.accent, 0.6f) : c.line1);
    nvgFill(vg);

    // Neutral, so it never reads as a second loop bar (the loop is the only accent-coloured bar here).
    rack::math::Rect w = overviewWindow();
    draw::roundedRect(vg, w.shrink(rack::math::Vec(0, 1)), 2, nvgTransRGBAf(c.text1, 0.14f));
    draw::roundedRectStroke(vg, w.shrink(rack::math::Vec(0, 1)), 2, c.line2, 1);

    double p = source.playhead();
    if (std::isfinite(p)) {
        float x = float(p / end) * box.size.x;
        nvgBeginPath(vg);
        nvgRect(vg, x, top, 1, h);
        nvgFillColor(vg, c.accent);
        nvgFill(vg);
    }
}

} // namespace tessera
