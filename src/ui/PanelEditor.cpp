#include "PanelEditor.hpp"

#include <set>

#include "../module/TesseraModule.hpp"
#include "Theme.hpp"
#include "interaction/Cursors.hpp"
#include "views/Arranger.hpp"
#include "views/DetailPane.hpp"
#include "views/PaneStack.hpp"
#include "views/TopPane.hpp"
#include "widgets/EditTools.hpp"
#include "widgets/ToolButton.hpp"

namespace tessera {

namespace {

constexpr float kMinPaneHeight = 70;
constexpr float kCornerButton = 20;    // the Full editor button in the top-right corner
constexpr float kTransportButton = 18; // play, stop, record, loop: top-left, beside the song pane's tools
constexpr float kCornerGroupGap = 8;   // between the transport and Full editor

/// Undo step for a panel width change. Neighbour moves are recorded next to it by Rack.
class PanelWidthChange : public rack::history::ModuleAction {
  public:
    PanelWidthChange(int64_t id, int oldHp, int newHp) : oldHp(oldHp), newHp(newHp) {
        moduleId = id;
        name = newHp > oldHp ? "widen Tessera" : "narrow Tessera";
    }
    void undo() override { set(oldHp); }
    void redo() override { set(newHp); }

  private:
    void set(int hp) {
        // The widget picks the new width up on its next step.
        if (auto* m = dynamic_cast<TesseraModule*>(APP->engine->getModule(moduleId)))
            m->editor().panelHp = hp;
    }
    int oldHp, newHp;
};

TesseraModule* tesseraOf(rack::app::ModuleWidget* mw) { return dynamic_cast<TesseraModule*>(mw->getModule()); }

/// Applies a width now and pushes neighbours out of the way, like Rack does for a dragged module.
void applyPanelWidth(rack::app::ModuleWidget* mw, int hp) {
    TesseraModule* m = tesseraOf(mw);
    if (!m)
        return;
    m->editor().panelHp = hp;
    mw->box.size.x = hp * rack::RACK_GRID_WIDTH;
    APP->scene->rack->setModulePosForce(mw, mw->box.pos);
}

} // namespace

int snapPanelHp(float widthHp) {
    int hp = int(std::round(widthHp));
    // Too narrow for a useful editor: fold back to the controls alone.
    if (hp < (EditorState::kPanelMinHp + EditorState::kPanelEditorMinHp) / 2)
        return EditorState::kPanelMinHp;
    return rack::math::clamp(hp, EditorState::kPanelEditorMinHp, EditorState::kPanelMaxHp);
}

void resizeTesseraPanel(rack::app::ModuleWidget* mw, int hp, int oldHp, bool beginNow) {
    TesseraModule* m = tesseraOf(mw);
    if (!m)
        return;
    rack::app::RackWidget* rack = APP->scene->rack;
    if (beginNow) {
        rack->updateModuleOldPositions();
        applyPanelWidth(mw, hp);
    }
    if (hp == oldHp)
        return;
    auto* action = new rack::history::ComplexAction;
    action->name = hp > oldHp ? "widen Tessera" : "narrow Tessera";
    action->push(new PanelWidthChange(m->id, oldHp, hp));
    if (rack::history::ComplexAction* moves = rack->getModuleDragAction()) {
        // Take over the neighbour moves so they undo together with the width.
        for (rack::history::Action* a : moves->actions)
            action->push(a);
        moves->actions.clear();
        delete moves;
    }
    APP->history->push(action);
}

// ---- PanelEditor --------------------------------------------------------------------------------------------

namespace {

std::set<PanelEditor*>& liveEditors() {
    static std::set<PanelEditor*> editors;
    return editors;
}

/// Rack scrolls the rack on Alt+drag, and its scroll view takes the press before any module sees it. The
/// editor uses Alt+drag (velocity, free placement, spreading values), so this layer on top of the scene hands
/// Alt+clicks that land on a panel editor to that editor. Rack then drags the editor's view as usual; Alt+drag
/// anywhere else still scrolls the rack.
struct AltClickRouter : rack::widget::Widget {
    void step() override {
        if (parent)
            box = rack::math::Rect(rack::math::Vec(), parent->box.size);
        Widget::step();
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT || (e.mods & RACK_MOD_MASK) != GLFW_MOD_ALT)
            return;
        if (APP->scene->browser && APP->scene->browser->isVisible())
            return;
        for (PanelEditor* editor : liveEditors()) {
            if (!editor->isVisible() || !editor->parent)
                continue;
            rack::math::Vec origin = editor->getRelativeOffset(rack::math::Vec(), APP->scene);
            float zoom = editor->getAbsoluteZoom();
            rack::math::Vec local = e.pos.minus(origin).div(zoom);
            if (!rack::math::Rect(rack::math::Vec(), editor->box.size).contains(local))
                continue;
            ButtonEvent inner = e;
            inner.pos = local;
            editor->onButton(inner);
            return;
        }
    }
};

void ensureAltClickRouter() {
    static AltClickRouter* router = nullptr;
    if (router || !APP || !APP->scene)
        return;
    router = new AltClickRouter;
    APP->scene->addChild(router);
}

} // namespace

PanelEditor::PanelEditor(int64_t moduleId, std::function<void()> openFullEditor)
    : EditorHost(moduleId, Surface::Panel) {
    ctx.root = this;
    liveEditors().insert(this);
    ensureAltClickRouter();
    top = new TopPane(ctx);
    detail = new DetailPane(ctx);
    panes = new PaneStack(ctx, top, detail, kMinPaneHeight, [this] { runAction(Action::MaximizePane); });
    addChild(panes);
    // "Full editor" belongs to the whole editor, not a pane: it stays in the top-right corner whichever pane is
    // maximized, next to that pane's own maximize / restore button.
    ToolButton::Options o;
    o.icon = Icon::FullEditor;
    o.tooltip = "Open the full editor";
    o.action = std::move(openFullEditor);
    fullEditor = new ToolButton(o);
    addChild(fullEditor);
    // The transport sits beside it (Trello, 13 Oct), apart from the clips toolbar's own stop-all and record.
    transport = makeTransportButtons(ctx);
    for (ToolButton* b : transport)
        addChild(b);
}

PanelEditor::~PanelEditor() {
    liveEditors().erase(this);
    // The views use `ctx`, which lives in the EditorHost base and is destroyed before Widget's destructor
    // would delete them.
    clearChildren();
    updateCursor(false);
    keyboard.releaseAll(ctx);
}

EditorPane* PanelEditor::focusedPane() {
    if (ctx.view().focus == Pane::Top && top->visible)
        return top;
    return detail->visible ? static_cast<EditorPane*>(detail) : top;
}

EditorPane* PanelEditor::paneUnderMouse() { return pickUnderMouse({top, detail}); }

ArrangerView* PanelEditor::arrangerPane() { return top->arranger(); }

bool PanelEditor::mouseInside() {
    rack::math::Vec origin = getAbsoluteOffset(rack::math::Vec());
    rack::math::Vec p = APP->scene->getMousePos().minus(origin).div(getAbsoluteZoom());
    return p.x >= 0 && p.y >= 0 && p.x < box.size.x && p.y < box.size.y;
}

void PanelEditor::step() {
    // Hidden (panel folded to the controls): don't step the views, or they would fit themselves to a size
    // they never get to show.
    if (!visible || !ctx.module())
        return;
    panes->box = rack::math::Rect(rack::math::Vec(), box.size);
    // Full editor sits left of the top pane's maximize button. The transport sits top-left, above the track names,
    // where the eye starts. With the notes maximized both move into the notes header (which keeps the room free).
    bool notesOnTop = ctx.view().maximized == Pane::Detail;
    float transportWidth = float(transport.size()) * (kTransportButton + 2) - 2;
    float groupWidth = kCornerButton + kCornerGroupGap + transportWidth;
    detail->reserveHeader(notesOnTop ? groupWidth + 6 : 0);
    ArrangerView* song = top->arranger();
    song->cornerInset = notesOnTop ? 0 : transportWidth + 4;
    panes->layout();
    detail->layout();
    float rulerHeight = Ruler::heightFor(song->source, ctx.surface);
    float buttonY = notesOnTop ? (theme::metrics::kPaneHeader - kCornerButton) / 2 : (rulerHeight - kCornerButton) / 2;
    float maximizeRoom = notesOnTop ? 6 + kMaximizeButton + 6 : 3 + kMaximizeButton + 4;
    float x = box.size.x - maximizeRoom - kCornerButton;
    fullEditor->box = rack::math::Rect(x, buttonY, kCornerButton, kCornerButton);
    x = notesOnTop ? x - kCornerGroupGap - transportWidth : 4;
    float transportY = notesOnTop ? buttonY + 1 : (rulerHeight - kTransportButton) / 2;
    for (ToolButton* b : transport) {
        b->box = rack::math::Rect(x, transportY, kTransportButton, kTransportButton);
        x += kTransportButton + 2;
    }

    bool inside = mouseInside();
    if (!inside && !ctx.dragging) {
        // Key releases go to whatever is under the mouse, so forget held keys once it leaves.
        ctx.spaceHeld = false;
        ctx.zoomToolHeld = false;
    }
    claimKeyboard(this, false);
    trackHolds();
    showNotices();
    OpaqueWidget::step();
    std::optional<CursorShape> splitterCursor;
    if (!ctx.dragging && panes->splitterActive())
        splitterCursor = CursorShape::ResizeV;
    updateCursor(inside, splitterCursor);
}

void PanelEditor::drawLayer(const DrawArgs& args, int layer) {
    // Everything on the self-lit layer: readable when Rack's room brightness is turned down.
    if (layer != 1 || !ctx.module())
        return;
    nvgSave(args.vg);
    nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
    OpaqueWidget::draw(args);
    nvgRestore(args.vg);
    drawReadout(args.vg, box.size);
}

void PanelEditor::onHoverKey(const HoverKeyEvent& e) {
    OpaqueWidget::onHoverKey(e);
    if (e.isConsumed() || !ctx.module())
        return;
    // Our shortcuts win over Rack's module shortcuts while the mouse is on the editor. Keys we don't use
    // (e.g. Ctrl+Shift+D) still reach the module.
    if (handleKey(e))
        e.consume(this);
}

void PanelEditor::onSelectKey(const SelectKeyEvent& e) {
    // Clicked last but the mouse is elsewhere now: the keys belong to whatever is under the mouse.
    if (!ctx.module() || !mouseInside())
        return;
    if (handleKey(e))
        e.consume(this);
}

void PanelEditor::onHoverScroll(const HoverScrollEvent& e) {
    OpaqueWidget::onHoverScroll(e);
    if (!e.isConsumed())
        e.consume(this);
}

// ---- PanelResizeHandle --------------------------------------------------------------------------------------

void PanelResizeHandle::draw(const DrawArgs& args) {
    const theme::Palette& c = theme::colors();
    NVGcolor color = dragging || hovered ? c.accent : c.line1;
    // Three short grip lines, centered on the edge.
    float cx = box.size.x / 2, cy = box.size.y / 2;
    nvgBeginPath(args.vg);
    for (int i = -1; i <= 1; i++) {
        nvgMoveTo(args.vg, cx + i * 2.f, cy - 10);
        nvgLineTo(args.vg, cx + i * 2.f, cy + 10);
    }
    nvgStrokeColor(args.vg, color);
    nvgStrokeWidth(args.vg, 1);
    nvgStroke(args.vg);
}

void PanelResizeHandle::onEnter(const EnterEvent& e) {
    hovered = true;
    applyCursor(CursorShape::ResizeH);
}

void PanelResizeHandle::onLeave(const LeaveEvent& e) {
    hovered = false;
    if (!dragging)
        resetCursor();
}

void PanelResizeHandle::onDragStart(const DragStartEvent& e) {
    TesseraModule* m = dynamic_cast<TesseraModule*>(mw->getModule());
    if (!m || e.button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    dragging = true;
    startHp = m->editor().panelHp;
    APP->scene->rack->updateModuleOldPositions();
}

void PanelResizeHandle::onDragMove(const DragMoveEvent& e) {
    TesseraModule* m = dynamic_cast<TesseraModule*>(mw->getModule());
    if (!dragging || !m)
        return;
    rack::math::Vec origin = mw->getAbsoluteOffset(rack::math::Vec());
    float x = (APP->scene->getMousePos().x - origin.x) / mw->getAbsoluteZoom();
    int hp = snapPanelHp(x / rack::RACK_GRID_WIDTH);
    if (hp != m->editor().panelHp)
        applyPanelWidth(mw, hp);
}

void PanelResizeHandle::onDragEnd(const DragEndEvent& e) {
    TesseraModule* m = dynamic_cast<TesseraModule*>(mw->getModule());
    if (!dragging || !m)
        return;
    dragging = false;
    if (!hovered)
        resetCursor();
    resizeTesseraPanel(mw, m->editor().panelHp, startHp, false);
}

} // namespace tessera
