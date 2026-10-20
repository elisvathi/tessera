#pragma once

#include "HoverTooltip.hpp"

#include <rack.hpp>

#include <functional>
#include <string>

namespace tessera {

/// A small panel button that runs a UI-thread action. Unlike a Rack param button it has no value,
/// so it suits actions such as "next track" that change module state outside process().
class IconButton : public rack::widget::OpaqueWidget {
  public:
    enum class Icon { Previous, Next };

    IconButton(Icon icon, std::string tooltip, std::function<void()> action)
        : icon(icon), tip(std::move(tooltip)), action(std::move(action)) {}

    void draw(const DrawArgs& args) override;
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
        pressed = false;
        tip.leave();
    }
    void onButton(const ButtonEvent& e) override;

  private:
    Icon icon;
    HoverTooltip tip;
    std::function<void()> action;
    bool hovered = false;
    bool pressed = false;
};

} // namespace tessera
