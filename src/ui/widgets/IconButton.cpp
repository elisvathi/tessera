#include "IconButton.hpp"

#include "../Theme.hpp"
#include "Draw.hpp"

namespace tessera {

void IconButton::draw(const DrawArgs& args) {
    const theme::Palette& c = theme::colors();
    NVGcontext* vg = args.vg;

    NVGcolor fill = pressed ? c.line1 : hovered ? c.line0 : c.bg2;
    draw::roundedRect(vg, rack::math::Rect(rack::math::Vec(), box.size), theme::metrics::kRadiusControl, fill);

    // A chevron sized from the button, pointing left or right.
    float cx = box.size.x / 2, cy = box.size.y / 2;
    float h = box.size.y * 0.22f;
    float dir = icon == Icon::Next ? 1.f : -1.f;
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - dir * h * 0.5f, cy - h);
    nvgLineTo(vg, cx + dir * h * 0.5f, cy);
    nvgLineTo(vg, cx - dir * h * 0.5f, cy + h);
    nvgStrokeColor(vg, hovered ? c.text0 : c.text1);
    nvgStrokeWidth(vg, 1.5f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
}

void IconButton::onButton(const ButtonEvent& e) {
    if (e.button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    e.consume(this);
    if (e.action == GLFW_PRESS) {
        pressed = true;
    } else if (e.action == GLFW_RELEASE && pressed) {
        pressed = false;
        if (action)
            action();
    }
}

} // namespace tessera
