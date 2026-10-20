#include "EditorPane.hpp"

#include "../Theme.hpp"
#include "../widgets/Draw.hpp"

namespace tessera {

void EditorPane::onButton(const ButtonEvent& e) {
    Widget::onButton(e);
    if (e.action != GLFW_PRESS || !ctx.module())
        return;
    ctx.view().focus = which; // clicking anywhere in a pane focuses it
}

void EditorPane::draw(const DrawArgs& args) {
    Widget::draw(args);
    if (ctx.module() && focused()) {
        // Focus ring: the pane keyboard shortcuts go to.
        draw::roundedRectStroke(args.vg, rack::math::Rect(rack::math::Vec(), box.size), 0,
                                nvgTransRGBAf(theme::colors().accent, 0.5f), 1);
    }
}

} // namespace tessera
