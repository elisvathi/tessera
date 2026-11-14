#pragma once

#include "../model/ParamRef.hpp"
#include "EditorState.hpp"
#include "interaction/Gesture.hpp"

#include <rack.hpp>

#include <cmath>
#include <functional>

#include <string>

namespace tessera {

class TesseraModule;
class InteractiveView;

/// What every view of one open editor shares: which Tessera it edits, and interaction state that crosses
/// views (the active drag, the cursor shape, the readout pill, when the user last navigated).
///
/// Views never keep a TesseraModule* or Song pointer across frames; they ask the context each time.
class EditorContext {
  public:
    EditorContext(int64_t moduleId, Surface surface) : moduleId(moduleId), surface(surface) {}

    const int64_t moduleId;
    const Surface surface;

    /// The Tessera being edited, or nullptr once it has been deleted.
    TesseraModule* module() const;
    /// Settings shared by every surface: what you work on and what is open. Defaults when there is no module.
    EditorState& state() const;
    /// Whether this editor is the one in use, so its views move and the other's copy them: the full editor while it
    /// is open, else the panel. Two editors moving one view would fight, since each fits and follows for its own size.
    bool drivesView() const;
    /// This surface's own pane layout.
    ViewState& view() const { return state().view(surface); }
    /// Selects the next (+1) or previous (-1) track, stopping at the ends.
    void selectTrackOffset(int delta) const;

    static double now() { return rack::system::getTime(); }

    // Interaction shared between views
    InteractiveView* dragging = nullptr; // the view whose gesture is running
    Modifiers mods;
    bool spaceHeld = false;    // Space + drag pans
    bool zoomToolHeld = false; // Z held: click zooms in, Alt+click out, drag zooms to a rectangle
    bool zoomToolUsed = false; // a zoom-tool click or drag happened during this hold
    CursorShape cursor = CursorShape::Arrow;

    /// The readout pill, positioned in editor-root coordinates. Empty text hides it.
    std::string readout;
    rack::math::Vec readoutPos;

    /// A short message at the bottom of the editor ("Exported 12 tracks to song.mid") for a few seconds.
    void toast(std::string text) {
        toastText = std::move(text);
        toastUntil = now() + kToastSeconds;
        toastButton.clear();
        toastAction = nullptr;
    }
    /// A toast with one button ("Add lane") that runs `action` and closes it. It stays up longer, to be read.
    void toast(std::string text, std::string button, std::function<void()> action) {
        toast(std::move(text));
        toastUntil = now() + kActionToastSeconds;
        toastButton = std::move(button);
        toastAction = std::move(action);
    }
    std::string toastText;
    std::string toastButton;
    std::function<void()> toastAction;
    rack::math::Rect toastButtonBox; // where the button was last drawn, in surface coordinates
    /// Where the running drag snapped, in the ticks of the viewport `snapAxis` (its ruler draws a mark there).
    double snapMark = NAN;
    const void* snapAxis = nullptr;
    bool showHelp = false; // the "?" overlay: every shortcut, generated from the shortcut table
    // The device strip (docs/DEVICES.md §1.1): IDs, so nothing points into the song across frames.
    uint64_t mappingModulator = 0; // ModulatorId being mapped onto knobs; 0 = not mapping
    /// The open device strip fills the notes pane (a short pane: devices in place of the notes). Set by the pane
    /// each frame; a curve opening then switches to the notes, where the curve is drawn.
    bool devicesFillPane = false;
    bool clipsOpenNarrow = false;  // a narrow song pane shows the clips although the timeline plays
    double toastUntil = 0;
    static constexpr double kToastSeconds = 4;
    static constexpr double kActionToastSeconds = 10;

    /// Call whenever the user scrolls or zooms by hand: follow-playhead waits a moment before taking over.
    void touchNavigation() { lastNavigation = now(); }
    bool followPaused() const { return dragging || now() - lastNavigation < kFollowPauseSeconds; }

    /// The editor root widget, for converting view positions to root coordinates.
    rack::widget::Widget* root = nullptr;

  private:
    double lastNavigation = -1e9;
};

} // namespace tessera
