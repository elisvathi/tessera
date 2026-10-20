#pragma once

#include "../EditorContext.hpp"
#include "Gesture.hpp"

#include <rack.hpp>

#include <memory>

namespace tessera {

/// Base for every editor view that reacts to the mouse. It turns Rack's raw events into the editor's
/// vocabulary: gestures for drags, click and double-click, wheel navigation and hover cursors. It also runs
/// edge auto-scroll during drags and keeps the readout pill up to date.
class InteractiveView : public rack::widget::OpaqueWidget {
  public:
    explicit InteractiveView(EditorContext& ctx) : ctx(ctx) {}
    ~InteractiveView() override;

    bool hasGesture() const { return gesture != nullptr; }
    /// Esc during a drag: undo the preview and drop the gesture.
    void cancelGesture();
    /// Re-runs the gesture with the current pointer and modifiers (e.g. Shift pressed mid-drag).
    void refreshGesture();

    /// The pointer in this view's local coordinates, also valid during drags.
    rack::math::Vec localMouse();

    void step() override;
    void draw(const DrawArgs& args) override;
    void onButton(const ButtonEvent& e) override;
    void onDragStart(const DragStartEvent& e) override;
    void onDragMove(const DragMoveEvent& e) override;
    void onDragEnd(const DragEndEvent& e) override;
    void onDoubleClick(const DoubleClickEvent& e) override;
    void onHover(const HoverEvent& e) override;
    void onLeave(const LeaveEvent& e) override;
    void onHoverScroll(const HoverScrollEvent& e) override;

  protected:
    /// A press that may become a drag. Return nullptr when this press is a plain click.
    virtual std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) {
        return nullptr;
    }
    /// Press and release without a gesture and without moving.
    virtual void click(rack::math::Vec pos, int button, Modifiers mods) {}
    virtual void doubleClick(rack::math::Vec pos, Modifiers mods) {}
    virtual CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) { return CursorShape::Arrow; }
    /// Wheel or trackpad. `delta` is Rack's scroll delta (positive y = up / away).
    virtual void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) {}
    /// Scroll the content by pixels, for edge auto-scroll during drags.
    virtual void autoScroll(double dx, double dy) {}
    /// Drawing order: drawView, then child widgets (cached layers), then drawForeground, then the gesture overlay.
    virtual void drawView(const DrawArgs& args) {}
    virtual void drawForeground(const DrawArgs& args) {}

    EditorContext& ctx;

  private:
    void finishGesture(bool commit);
    void showReadout(rack::math::Vec pos);

    std::unique_ptr<DragGesture> gesture;
    int pressButton = -1;
    rack::math::Vec pressPos;
    bool moved = false;
    double lastStep = 0;
};

} // namespace tessera
