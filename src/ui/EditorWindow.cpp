#include "EditorWindow.hpp"

#include "../module/TesseraModule.hpp"
#include "Theme.hpp"
#include "interaction/Cursors.hpp"
#include "views/Arranger.hpp"
#include "views/DetailPane.hpp"
#include "views/Inspector.hpp"
#include "views/PaneStack.hpp"
#include "views/TopPane.hpp"
#include "views/TransportBar.hpp"
#include "widgets/Draw.hpp"

namespace tessera {

namespace {

constexpr float kMinPaneHeight = 90;

/// Rack's own file and window shortcuts keep working while the editor is open. Everything else is ours, so
/// no key ever reaches a module hidden behind the overlay.
bool passToRack(const rack::widget::Widget::KeyBaseEvent& e) {
    int mods = e.mods & RACK_MOD_MASK;
    if (e.key == GLFW_KEY_F11)
        return true;
    if (mods == RACK_MOD_CTRL)
        return e.keyName == "s" || e.keyName == "o" || e.keyName == "q" || e.keyName == "n";
    if (mods == (RACK_MOD_CTRL | GLFW_MOD_SHIFT))
        return e.keyName == "s";
    return false;
}

} // namespace

EditorWindow* EditorWindow::instance = nullptr;

void EditorWindow::open(TesseraModule* module) {
    if (!module)
        return;
    if (instance) {
        if (instance->ctx.moduleId == module->id)
            return;
        instance->close();
    }
    instance = new EditorWindow(module->id);
    instance->box = APP->scene->box;
    APP->scene->addChild(instance);
}

bool EditorWindow::isOpenFor(int64_t moduleId) { return instance && instance->ctx.moduleId == moduleId; }

EditorContext* EditorWindow::contextFor(int64_t moduleId) { return isOpenFor(moduleId) ? &instance->ctx : nullptr; }

EditorWindow::EditorWindow(int64_t moduleId) : EditorHost(moduleId, Surface::Overlay) {
    ctx.root = this;
    zoom = new rack::widget::ZoomWidget;
    addChild(zoom);
    content = new rack::widget::Widget;
    zoom->addChild(content);

    transport = new TransportBar(ctx, [this] { close(); });
    top = new TopPane(ctx);
    detail = new DetailPane(ctx);
    inspector = new Inspector(ctx);
    inspector->target = [this] { return detail->noteTarget(); };
    detailArea = new rack::widget::Widget;
    detailArea->addChild(detail);
    detailArea->addChild(inspector);
    panes = new PaneStack(ctx, top, detailArea, kMinPaneHeight, [this] { runAction(Action::MaximizePane); });

    content->addChild(panes);
    content->addChild(transport);
}

EditorWindow::~EditorWindow() {
    // The views use `ctx`, which lives in the EditorHost base and is destroyed before Widget's destructor
    // would delete them.
    clearChildren();
    resetCursor();
    if (instance == this)
        instance = nullptr;
}

void EditorWindow::close() {
    if (closing)
        return;
    closing = true;
    resetCursor();
    keyboard.releaseAll(ctx);
    if (ctx.module()) {
        ctx.state().keyboardPlay = false;
        ctx.state().stepInput = false;
    }
    if (instance == this)
        instance = nullptr;
    requestDelete();
}

EditorPane* EditorWindow::focusedPane() {
    if (ctx.view().focus == Pane::Top && top->visible)
        return top;
    return detailArea->visible ? static_cast<EditorPane*>(detail) : top;
}

EditorPane* EditorWindow::paneUnderMouse() { return pickUnderMouse({top, detail}); }

ArrangerView* EditorWindow::arrangerPane() { return top->arranger(); }

void EditorWindow::layout() {
    using namespace theme::metrics;
    ViewState& view = ctx.view();
    float scale = ctx.state().uiScale > 0 ? ctx.state().uiScale : 1.f;
    zoom->setZoom(scale);
    zoom->box.size = box.size;
    content->box.size = box.size.div(scale);

    rack::math::Vec size = content->box.size;
    transport->box = rack::math::Rect(0, 0, size.x, kTransportHeight);
    panes->box = rack::math::Rect(0, kTransportHeight, size.x, size.y - kTransportHeight);
    panes->layout();

    rack::math::Vec area = detailArea->box.size;
    float inspectorWidth = view.inspectorOpen ? kInspectorWidth : 0;
    detail->box = rack::math::Rect(0, 0, area.x - inspectorWidth, area.y);
    inspector->box = rack::math::Rect(area.x - inspectorWidth, 0, inspectorWidth, area.y);
    inspector->visible = view.inspectorOpen;
    detail->layout();
}

void EditorWindow::step() {
    if (closing)
        return;
    if (!ctx.module()) {
        // The Tessera was deleted: there is nothing left to edit.
        close();
        return;
    }
    // Mapping a knob: the editor steps aside to a banner, so the rack can be reached (DESIGN.md §6 "Learn").
    bool learning = ctx.module()->learning();
    if (learning != learnBanner) {
        learnBanner = learning;
        if (learning) {
            hiddenForLearn.clear();
            for (Widget* child : children)
                if (child->visible) {
                    hiddenForLearn.push_back(child);
                    child->visible = false;
                }
        } else {
            for (Widget* child : hiddenForLearn)
                child->visible = true;
            hiddenForLearn.clear();
        }
    }
    if (learning) {
        rack::math::Vec size(kLearnBannerWidth, kLearnBannerHeight);
        box = rack::math::Rect(rack::math::Vec((parent->box.size.x - size.x) / 2, 8), size);
        OpaqueWidget::step();
        return;
    }
    box = parent->box.zeroPos();
    layout();
    claimKeyboard(this, true);
    trackHolds();
    OpaqueWidget::step();

    std::optional<CursorShape> splitterCursor;
    if (!ctx.dragging && panes->splitterActive())
        splitterCursor = CursorShape::ResizeV;
    updateCursor(true, splitterCursor);
}

void EditorWindow::draw(const DrawArgs& args) {
    if (moduleGone())
        return;
    if (learnBanner) {
        const theme::Palette& c = theme::colors();
        tessera::draw::roundedRect(args.vg, rack::math::Rect(rack::math::Vec(), box.size), 6, c.bg2);
        tessera::draw::roundedRectStroke(args.vg, rack::math::Rect(rack::math::Vec(), box.size), 6, c.accent, 1.5f);
        tessera::draw::TextStyle text{theme::fonts::uiStrong(), theme::metrics::kTextUi, c.text0,
                                      NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        tessera::draw::text(args.vg, text, box.size.x / 2, box.size.y / 2,
                            "Touch a knob in the rack to map it  ·  click here to cancel");
        return;
    }
    nvgBeginPath(args.vg);
    nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(args.vg, theme::colors().bg0);
    nvgFill(args.vg);
    OpaqueWidget::draw(args);
    drawReadout(args.vg, box.size);
}

void EditorWindow::onHoverKey(const HoverKeyEvent& e) {
    if (moduleGone())
        return;
    OpaqueWidget::onHoverKey(e);
    if (e.isConsumed())
        return;
    if (handleKey(e) || !passToRack(e))
        e.consume(this);
}

void EditorWindow::onSelectKey(const SelectKeyEvent& e) {
    if (moduleGone())
        return;
    if (handleKey(e) || !passToRack(e))
        e.consume(this);
}

void EditorWindow::onHoverText(const HoverTextEvent& e) {
    OpaqueWidget::onHoverText(e);
    e.consume(this);
}

void EditorWindow::onButton(const ButtonEvent& e) {
    if (moduleGone()) {
        e.consume(this);
        return;
    }
    if (ctx.showHelp) {
        if (e.action == GLFW_PRESS)
            ctx.showHelp = false;
        e.consume(this);
        return;
    }
    if (learnBanner) {
        if (e.action == GLFW_PRESS)
            ctx.module()->cancelLearn();
        e.consume(this);
        return;
    }
    OpaqueWidget::onButton(e);
    if (!e.isConsumed())
        e.consume(this); // right and middle clicks too: nothing behind the overlay may react
}

void EditorWindow::onHover(const HoverEvent& e) {
    if (moduleGone()) {
        e.consume(this);
        return;
    }
    OpaqueWidget::onHover(e);
}

void EditorWindow::onHoverScroll(const HoverScrollEvent& e) {
    if (moduleGone()) {
        e.consume(this);
        return;
    }
    OpaqueWidget::onHoverScroll(e);
    if (!e.isConsumed())
        e.consume(this);
}

} // namespace tessera
