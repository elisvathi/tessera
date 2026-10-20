#pragma once

#include "../Theme.hpp"

#include <rack.hpp>

#include <functional>
#include <string>

namespace tessera {

/// A menu entry with a colour dot before its text, checked when `checked` is set.
class ColorMenuItem : public rack::ui::MenuItem {
  public:
    ColorMenuItem(const std::string& label, uint32_t color, bool checked, std::function<void()> action)
        : color(color), onPick(std::move(action)) {
        // Leading spaces leave room for the dot.
        text = "     " + label;
        rightText = CHECKMARK(checked);
    }

    void draw(const DrawArgs& args) override {
        MenuItem::draw(args);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, 13, box.size.y / 2, 4);
        nvgFillColor(args.vg, theme::rgb(color));
        nvgFill(args.vg);
    }

    void onAction(const ActionEvent& e) override {
        if (onPick)
            onPick();
    }

  private:
    uint32_t color;
    std::function<void()> onPick;
};

} // namespace tessera
