#pragma once

#include "EditorContext.hpp"
#include "editing/LivePlay.hpp"
#include "interaction/Shortcuts.hpp"
#include "views/Sources.hpp"

#include <rack.hpp>

namespace tessera {

class EditorPane;
class ArrangerView;

/// What every editor surface does the same way, whether it is the full-window overlay or the editor inside
/// the Tessera panel: keys go through the shortcut table, actions run on the focused pane, Space and Z work
/// as tap-or-hold keys, and the readout pill and mouse cursor follow the active gesture.
class EditorHost {
  public:
    EditorHost(int64_t moduleId, Surface surface) : ctx(moduleId, surface), song(ctx) {}
    virtual ~EditorHost() = default;

    EditorContext ctx;

    /// Returns true when the key was used (and must not reach anything else).
    bool handleKey(const rack::widget::Widget::KeyBaseEvent& e);
    void runAction(Action action);

  protected:
    virtual EditorPane* focusedPane() = 0;
    /// The pane the mouse is over, or nullptr (over the transport, the inspector, or outside the editor).
    virtual EditorPane* paneUnderMouse() = 0;
    /// The first of `panes` that is showing and has the mouse over it, or nullptr. Used by paneUnderMouse().
    static EditorPane* pickUnderMouse(std::initializer_list<EditorPane*> panes);
    /// The arranger, when this surface has one.
    virtual ArrangerView* arrangerPane() { return nullptr; }
    /// Esc when no drag is running.
    virtual void escape() {}

    /// Call every frame. Makes `root` the widget Rack sends keys to first: always (`always`), or when one of its
    /// views was clicked last. Rack's scene runs some shortcuts (arrows pan the rack) before any hover key
    /// handler sees them, so the editor must take keys through onSelectKey.
    void claimKeyboard(rack::widget::Widget* root, bool always);
    /// Call every frame. Keeps Space-to-pan from also toggling play.
    void trackHolds();
    /// Call every frame. Shows what Tessera reported since the last frame, such as CCs a take could not record.
    void showNotices();
    /// A press at `pos` (surface coordinates) on the toast's button: runs it. Returns true when it was there.
    bool clickToast(rack::math::Vec pos);
    /// Sets the OS cursor while `active` (hovered or dragging); restores it once when that ends.
    void updateCursor(bool active, std::optional<CursorShape> override = std::nullopt);
    /// The readout pill near the cursor, kept inside `bounds` (host-local coordinates).
    void drawReadout(NVGcontext* vg, rack::math::Vec bounds);
    void drawToast(NVGcontext* vg, rack::math::Vec bounds);
    void drawHelp(NVGcontext* vg, rack::math::Vec bounds);

    /// The song timeline, for loop and marker actions that work on every surface.
    ArrangerSource song;

    KeyboardPlay keyboard;

  private:
    bool spaceUsed = false;
    bool cursorApplied = false;
    uint64_t unlinkedSeen = 0; // the last UnlinkedControls serial shown
    bool noticesSeen = false;  // showNotices() has run once
};

} // namespace tessera
