#include "TopPane.hpp"

#include "Arranger.hpp"

namespace tessera {

TopPane::TopPane(EditorContext& ctx) : EditorPane(ctx, Pane::Top) {
    arrangerView = new ArrangerView(ctx);
    addChild(arrangerView);
}

bool TopPane::handleAction(Action action) { return arrangerView->handleAction(action); }

void TopPane::layout() {
    arrangerView->box = rack::math::Rect(rack::math::Vec(), box.size);
    arrangerView->layout();
}

void TopPane::step() {
    layout();
    EditorPane::step();
}

} // namespace tessera
