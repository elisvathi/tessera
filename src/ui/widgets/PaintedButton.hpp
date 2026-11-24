#pragma once

#include "HoverTooltip.hpp"

#include <rack.hpp>

#include <functional>
#include <string>

namespace tessera {

/// A clickable area that its owner paints, with a tooltip: for buttons whose shape is their own, such as the clips'
/// edge handle and the folded timeline tab. A left press runs the action.
class PaintedButton : public rack::widget::OpaqueWidget {
  public:
    using Paint = std::function<void(NVGcontext*, rack::math::Vec size, bool hot)>;

    PaintedButton(std::string tooltip, std::function<void()> action, Paint paint)
        : tip(std::move(tooltip)), action(std::move(action)), paint(std::move(paint)) {}

    void draw(const DrawArgs& args) override { paint(args.vg, box.size, hovered); }
    void step() override {
        tip.step();
        OpaqueWidget::step();
    }
    void onEnter(const EnterEvent& e) override {
        hovered = true;
        tip.enter();
    }
    void onLeave(const LeaveEvent& e) override {
        hovered = false;
        tip.leave();
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        e.consume(this);
        if (e.action == GLFW_PRESS && action)
            action();
    }

  private:
    HoverTooltip tip;
    std::function<void()> action;
    Paint paint;
    bool hovered = false;
};

} // namespace tessera
