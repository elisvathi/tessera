#include "TimelineView.hpp"

#include "../Theme.hpp"

#include <cmath>

namespace tessera {

namespace {

constexpr double kKeyZoomFactor = 1.5;
constexpr double kZoomToolFactor = 2.0;
constexpr double kMinZoomRectPx = 6; // smaller than this, a zoom-tool drag counts as a click

/// Middle-drag or Space+drag: the content follows the hand.
class PanGesture : public DragGesture {
  public:
    PanGesture(TimelineView& view, rack::math::Vec start, EditorContext& ctx)
        : view(view), ctx(ctx), start(start), time(view.time().state()), vertical(view.verticalScroll()) {}

    void update(rack::math::Vec pos, Modifiers mods) override {
        rack::math::Vec d = pos.minus(start);
        view.time().restore(time);
        view.time().scrollByPx(-d.x);
        view.setVerticalScroll(vertical - d.y);
        ctx.touchNavigation();
    }
    void cancel() override {
        view.time().restore(time);
        view.setVerticalScroll(vertical);
    }
    CursorShape cursor() const override { return CursorShape::Hand; }

  private:
    TimelineView& view;
    EditorContext& ctx;
    rack::math::Vec start;
    TimeAxis::State time;
    double vertical;
};

/// Zoom tool: drag a rectangle to fill the view with it; a click zooms in (Alt: out) at the point.
class ZoomRectGesture : public DragGesture {
  public:
    ZoomRectGesture(TimelineView& view, TimeViewport& viewport, rack::math::Vec start, EditorContext& ctx)
        : view(view), viewport(viewport), ctx(ctx), start(start), end(start) {}

    void update(rack::math::Vec pos, Modifiers mods) override {
        end = pos;
        alt = mods.alt;
    }
    void commit() override {
        ctx.zoomToolUsed = true;
        ctx.touchNavigation();
        rack::math::Rect r = rect();
        if (r.size.x < kMinZoomRectPx || r.size.y < kMinZoomRectPx) {
            viewport.history.push(viewport.time.state());
            double factor = alt ? 1 / kZoomToolFactor : kZoomToolFactor;
            view.time().zoomAt(start.x, factor);
            view.zoomVertical(start.y, factor);
            return;
        }
        TimeAxis::State before = view.time().state();
        view.time().showRange(view.time().xToTick(r.pos.x), view.time().xToTick(r.pos.x + r.size.x));
        TimeAxis::State after = view.time().state();
        view.time().restore(before);
        viewport.jump(after);
        view.showVerticalRange(r.pos.y, r.pos.y + r.size.y);
    }
    void drawOverlay(NVGcontext* vg) override {
        rack::math::Rect r = rect();
        const theme::Palette& c = theme::colors();
        nvgBeginPath(vg);
        nvgRect(vg, r.pos.x + 0.5f, r.pos.y + 0.5f, r.size.x, r.size.y);
        nvgFillColor(vg, nvgTransRGBAf(c.select, 0.15f));
        nvgFill(vg);
        nvgStrokeColor(vg, c.select);
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
    CursorShape cursor() const override { return CursorShape::Crosshair; }

  private:
    rack::math::Rect rect() const {
        rack::math::Vec lo(std::min(start.x, end.x), std::min(start.y, end.y));
        rack::math::Vec hi(std::max(start.x, end.x), std::max(start.y, end.y));
        return rack::math::Rect(lo, hi.minus(lo));
    }

    TimelineView& view;
    TimeViewport& viewport;
    EditorContext& ctx;
    rack::math::Vec start, end;
    bool alt = false;
};

} // namespace

std::unique_ptr<DragGesture> TimelineView::navigationGesture(rack::math::Vec pos, int button, Modifiers mods) {
    if (button == GLFW_MOUSE_BUTTON_MIDDLE || (button == GLFW_MOUSE_BUTTON_LEFT && ctx.spaceHeld))
        return std::make_unique<PanGesture>(*this, pos, ctx);
    if (button == GLFW_MOUSE_BUTTON_LEFT && ctx.zoomToolHeld)
        return std::make_unique<ZoomRectGesture>(*this, viewport, pos, ctx);
    return nullptr;
}

CursorShape TimelineView::hoverCursor(rack::math::Vec pos, Modifiers mods) {
    if (ctx.spaceHeld)
        return CursorShape::Hand;
    if (ctx.zoomToolHeld)
        return CursorShape::Crosshair;
    return CursorShape::Arrow;
}

void TimelineView::wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) {
    ctx.touchNavigation();
    if (mods.ctrl) {
        time().animateZoomAt(pos.x, wheelZoomFactor(delta.y), kZoomAnimationSeconds);
    } else if (mods.alt) {
        zoomVertical(pos.y, wheelZoomFactor(delta.y));
    } else if (mods.shift) {
        // A mouse wheel only scrolls vertically, so Shift turns it sideways.
        time().scrollByPx(-(delta.y + delta.x));
    } else {
        // Trackpads send both axes; two-finger scrolling pans naturally.
        time().scrollByPx(-delta.x);
        setVerticalScroll(verticalScroll() - delta.y);
    }
}

void TimelineView::autoScroll(double dx, double dy) {
    time().scrollByPx(dx);
    setVerticalScroll(verticalScroll() + dy);
}

double TimelineView::zoomAnchorX() {
    rack::math::Vec m = localMouse();
    if (m.x >= 0 && m.x <= box.size.x && m.y >= 0 && m.y <= box.size.y)
        return m.x;
    double p = playhead();
    if (std::isfinite(p)) {
        double x = time().tickToX(p);
        if (x >= 0 && x <= box.size.x)
            return x;
    }
    return box.size.x / 2;
}

bool TimelineView::handleAction(Action action) {
    TimeAxis& t = time();
    switch (action) {
    case Action::ZoomIn:
        ctx.touchNavigation();
        t.animateZoomAt(zoomAnchorX(), kKeyZoomFactor, kZoomAnimationSeconds);
        return true;
    case Action::ZoomOut:
        ctx.touchNavigation();
        t.animateZoomAt(zoomAnchorX(), 1 / kKeyZoomFactor, kZoomAnimationSeconds);
        return true;
    case Action::ZoomToFit:
    case Action::ZoomToSelection:
        ctx.touchNavigation();
        zoomToFit();
        return true;
    case Action::ZoomBack:
        if (auto s = viewport.history.back(t.state()))
            t.restore(*s);
        ctx.touchNavigation();
        return true;
    case Action::ZoomForward:
        if (auto s = viewport.history.forward())
            t.restore(*s);
        ctx.touchNavigation();
        return true;
    case Action::PageLeft:
        ctx.touchNavigation();
        t.scrollTo(t.left() - t.visibleTicks());
        return true;
    case Action::PageRight:
        ctx.touchNavigation();
        t.scrollTo(t.left() + t.visibleTicks());
        return true;
    default:
        return false;
    }
}

} // namespace tessera
