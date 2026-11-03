#pragma once

#include "../EditorContext.hpp"

#include <rack.hpp>

#include <functional>

namespace tessera {

class EditorPane;
class Splitter;

/// The top pane (launcher or arranger) above, the detail area below, a draggable splitter between them, and either one
/// maximized. The overlay and the in-panel editor both use it, so panes behave the same everywhere. Layout comes from
/// the surface's ViewState (topFraction, maximized).
class PaneStack : public rack::widget::Widget {
  public:
    /// The stack takes ownership of both panes. `minPaneHeight` keeps either pane from collapsing.
    PaneStack(EditorContext& ctx, EditorPane* top, rack::widget::Widget* bottom, float minPaneHeight,
              std::function<void()> onSplitterDoubleClick);

    void layout();
    /// Adds the mixer under the panes (folded to a tab unless opened). The stack takes ownership.
    void setMixer(class MixerPane* mixer);
    /// True while the pointer is on the splitter or dragging it (for the cursor).
    bool splitterActive() const;

  private:
    EditorContext& ctx;
    EditorPane* top;
    rack::widget::Widget* bottom;
    Splitter* splitter;
    MixerPane* mixer = nullptr;
    float minPaneHeight;
};

} // namespace tessera
