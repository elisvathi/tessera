#pragma once

#include "../EditorContext.hpp"
#include "../interaction/Shortcuts.hpp"

#include <rack.hpp>

namespace tessera {

/// One of the editor's two panes (top: arranger, bottom: detail editor). Panes keep their view state in
/// EditorState so it survives closing the editor and reloading the patch.
class EditorPane : public rack::widget::Widget {
  public:
    EditorPane(EditorContext& ctx, Pane which) : ctx(ctx), which(which) {}

    /// Runs a pane action (zoom, page, ...). Returns false when the pane has no use for it.
    virtual bool handleAction(Action action) = 0;
    /// Places children for the current box size.
    virtual void layout() = 0;

    Pane pane() const { return which; }
    bool focused() { return ctx.view().focus == which; }

    void onButton(const ButtonEvent& e) override;
    void draw(const DrawArgs& args) override;

    /// Room below the pane's header that something above the content takes (the device strip).
    float topInset = 0;

  protected:
    EditorContext& ctx;
    Pane which;
};

} // namespace tessera
