#pragma once

#include "EditorHost.hpp"

#include <rack.hpp>

namespace tessera {

class TesseraModule;
class TransportBar;
class ArrangerView;
class TopPane;
class DetailPane;
class Inspector;
class PaneStack;
class EditorPane;

/// The full editor: an overlay that fills the Rack window (DESIGN.md §6, EDITOR_UX §1).
///
/// While open it takes every mouse and key event inside it, so the rack behind never pans, zooms or reacts
/// to shortcuts. Only Rack's own save, open, quit and fullscreen keys pass through. One editor is open at
/// a time; it closes itself if its Tessera is deleted.
class EditorWindow : public rack::widget::OpaqueWidget, public EditorHost {
  public:
    static void open(TesseraModule* module);
    static bool isOpenFor(int64_t moduleId);
    /// The open editor's context when it edits this Tessera, else nullptr.
    static EditorContext* contextFor(int64_t moduleId);

    ~EditorWindow() override;

    void close();
    static constexpr float kLearnBannerWidth = 440;
    static constexpr float kLearnBannerHeight = 34;

    void step() override;
    void draw(const DrawArgs& args) override;
    void onHoverKey(const HoverKeyEvent& e) override;
    void onSelectKey(const SelectKeyEvent& e) override;
    void onHoverText(const HoverTextEvent& e) override;
    void onButton(const ButtonEvent& e) override;
    void onHover(const HoverEvent& e) override;
    void onHoverScroll(const HoverScrollEvent& e) override;

  protected:
    EditorPane* focusedPane() override;
    EditorPane* paneUnderMouse() override;
    ArrangerView* arrangerPane() override;
    void escape() override { close(); }

  private:
    bool learnBanner = false;
    std::vector<rack::widget::Widget*> hiddenForLearn;
    explicit EditorWindow(int64_t moduleId);

    void layout();

    rack::widget::ZoomWidget* zoom;
    rack::widget::Widget* content;
    TransportBar* transport;
    PaneStack* panes;
    TopPane* top;
    rack::widget::Widget* detailArea; // the piano roll and the inspector beside it
    DetailPane* detail;
    Inspector* inspector;
    bool closing = false;
    /// The Tessera was removed (an undo, say) or the editor is closing. The views read the module without checks,
    /// and the editor still gets events and a draw in the frame it closes, so all of them stop here.
    bool moduleGone() const { return closing || !ctx.module(); }

    static EditorWindow* instance;
};

} // namespace tessera
