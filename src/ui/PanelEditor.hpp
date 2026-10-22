#pragma once

#include "EditorHost.hpp"

#include <rack.hpp>

#include <vector>

#include <functional>

namespace tessera {

class DetailPane;
class ArrangerView;
class TopPane;
class PaneStack;
class ToolButton;

/// The compact editor inside a widened Tessera panel: the launcher or arranger above the selected track's
/// piano roll, stacked like in the overlay. A strip on top switches between launcher, arranger, both, and
/// piano roll.
///
/// It shares all editing code with the overlay through EditorHost, but keeps its own zoom and scroll. Rack
/// sends keys to whatever the mouse is over, so shortcuts work while the pointer is on the editor; there they
/// win over Rack's module shortcuts (Delete never deletes the module). It draws on Rack's self-lit layer so
/// it stays readable when the room lights are dimmed.
class PanelEditor : public rack::widget::OpaqueWidget, public EditorHost {
  public:
    PanelEditor(int64_t moduleId, std::function<void()> openFullEditor);
    ~PanelEditor() override;

    void step() override;
    void draw(const DrawArgs& args) override {}
    void drawLayer(const DrawArgs& args, int layer) override;
    void onHoverKey(const HoverKeyEvent& e) override;
    void onSelectKey(const SelectKeyEvent& e) override;
    void onHoverScroll(const HoverScrollEvent& e) override;
    void onButton(const ButtonEvent& e) override {
        if (ctx.showHelp) {
            if (e.action == GLFW_PRESS)
                ctx.showHelp = false;
            e.consume(this);
            return;
        }
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && clickToast(e.pos)) {
            e.consume(this);
            return;
        }
        OpaqueWidget::onButton(e);
    }

  protected:
    EditorPane* focusedPane() override;
    EditorPane* paneUnderMouse() override;
    ArrangerView* arrangerPane() override;

  private:
    bool mouseInside();

    PaneStack* panes;
    TopPane* top;
    DetailPane* detail;
    ToolButton* fullEditor;
    std::vector<ToolButton*> transport;
};

/// The handle on a Tessera panel's right edge. Dragging it widens the panel to show the editor (or narrows
/// it back), pushing neighbouring modules aside the way Rack does when you drag a module. One undo step.
class PanelResizeHandle : public rack::widget::OpaqueWidget {
  public:
    explicit PanelResizeHandle(rack::app::ModuleWidget* moduleWidget) : mw(moduleWidget) {}

    void draw(const DrawArgs& args) override;
    void onEnter(const EnterEvent& e) override;
    void onLeave(const LeaveEvent& e) override;
    void onDragStart(const DragStartEvent& e) override;
    void onDragMove(const DragMoveEvent& e) override;
    void onDragEnd(const DragEndEvent& e) override;

  private:
    rack::app::ModuleWidget* mw;
    bool hovered = false;
    bool dragging = false;
    int startHp = 0;
};

/// Snaps a dragged width to what the panel supports: the controls alone, or controls plus an editor.
int snapPanelHp(float widthHp);

/// Resizes a Tessera panel to `hp`, pushing neighbours aside, and records one undo step.
/// Call with `rack->updateModuleOldPositions()` already done for drags; menu resizes pass `beginNow`.
void resizeTesseraPanel(rack::app::ModuleWidget* mw, int hp, int oldHp, bool beginNow);

} // namespace tessera
