#pragma once

#include "../interaction/InteractiveView.hpp"
#include "../interaction/Navigation.hpp"
#include "../interaction/Shortcuts.hpp"

namespace tessera {

/// The view state of one pane: its time axis, zoom history and follow behavior. Shared by the pane's
/// ruler and body so they always scroll together.
struct TimeViewport {
    TimeAxis time;
    ZoomHistory history;

    /// Moves the view and records where it was, for zoom back / forward.
    void jump(TimeAxis::State next) {
        history.push(time.state());
        time.restore(next);
    }
};

/// A view with time across and something stacked down (pitches, tracks), with the navigation every such view
/// shares (EDITOR_UX §3): wheel scroll and zoom, pan by middle-drag or Space+drag, the zoom tool (hold Z),
/// zoom to fit, zoom history, page and home/end. Subclasses supply the vertical axis.
class TimelineView : public InteractiveView {
  public:
    TimelineView(EditorContext& ctx, TimeViewport& viewport) : InteractiveView(ctx), viewport(viewport) {}

    TimeAxis& time() { return viewport.time; }

    virtual double verticalScroll() = 0;
    virtual void setVerticalScroll(double px) = 0;
    virtual void zoomVertical(double y, double factor) = 0;
    /// Shows the rows between y0 and y1 (view px), for the zoom tool's rectangle.
    virtual void showVerticalRange(double y0, double y1) = 0;
    /// Fits everything there is to see, or the selection when there is one.
    virtual void zoomToFit() = 0;
    /// The playhead in this view's time, or NaN when there is none.
    virtual double playhead() { return NAN; }

    /// Handles a view action (zoom, page, home/end). Returns false when the action isn't a view action.
    bool handleAction(Action action);

  protected:
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    void autoScroll(double dx, double dy) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;

    /// Pan and zoom-tool gestures, or nullptr. Subclasses call this before starting their own gestures.
    std::unique_ptr<DragGesture> navigationGesture(rack::math::Vec pos, int button, Modifiers mods);
    /// Zooms around the mouse when it is over the view, else around the playhead, else the view center.
    double zoomAnchorX();

    TimeViewport& viewport;
};

/// Smooth zoom duration for wheel and keyboard (EDITOR_UX §3).
constexpr double kZoomAnimationSeconds = 0.08;

} // namespace tessera
