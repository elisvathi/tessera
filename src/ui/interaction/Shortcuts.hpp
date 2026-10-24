#pragma once

#include <optional>
#include <string>
#include <vector>

/// The editor's keymap (EDITOR_UX §7) as one table. Key handling, tooltips and the help overlay all read
/// it, so a binding is written exactly once. Designed so user remapping can replace the table later.
namespace tessera {

enum class Action {
    // Transport
    PlayStop,
    PlayFromMouse,
    Record,
    CaptureMidi,
    DeleteTime,
    InsertSilence,
    ToggleLoop,
    LoopToSelection,
    ToggleFollow,
    Fill,
    // Tools
    ToolPointer,
    ToolDraw,
    ToolPaint,
    ToolErase,
    ToolSlice,
    ToolMute,
    ToolVelocity,
    ToolZoom,
    ToolRange,
    StepInput,
    KeyboardPlay,
    ToggleAudition,
    // Edit
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    PasteAtMouse,
    Duplicate,
    Delete,
    SelectAll,
    SelectNone,
    SelectInvert,
    Split,
    Glue,
    MakeUnique,
    Quantize,
    QuantizeDialog,
    TransposeUp,
    TransposeDown,
    OctaveUp,
    OctaveDown,
    NudgeLeft,
    NudgeRight,
    NudgeFineLeft,
    NudgeFineRight,
    NudgeBarLeft,
    NudgeBarRight,
    LengthShorter,
    LengthLonger,
    VelocityUp,
    VelocityDown,
    VelocityUpFine,
    VelocityDownFine,
    CycleLeft,
    CycleRight,
    CycleUp,
    CycleDown,
    TransformBox,
    // View
    ZoomToFit,
    ZoomToSelection,
    ZoomIn,
    ZoomOut,
    ZoomBack,
    ZoomForward,
    GoToStart,
    GoToEnd,
    PageLeft,
    PageRight,
    FitAllTracks,
    ShowUsedLanes,
    MaximizeLane,
    GridNarrower,
    GridWider,
    GridTriplet,
    SnapToggle,
    ScaleFold,
    ToggleInspector,
    SwitchPane,
    MaximizePane,
    WidenClips,
    ToggleDevices,
    // Tracks
    AddInstrumentTrack,
    AddDrumTrack,
    GroupTracks,
    NextTrack,
    PreviousTrack,
    MuteTracks,
    SoloTracks,
    AddMarker,
    // General
    Help,
    ExportMidi,
    Close,
    Count
};

/// Modifier bits. kCtrl means Cmd on macOS.
enum : int { kModShift = 1, kModCtrl = 2, kModAlt = 4 };

/// Where a binding applies. Pane-scoped bindings let the two panes reuse a chord.
enum class Scope { Everywhere, TopPane, DetailPane };

struct KeyChord {
    int key = 0;    // GLFW key code for keys without a printable name (Space, arrows, F-keys, ...)
    std::string ch; // lowercase key name for printable keys ("d", "[", "="); empty when `key` is used
    int mods = 0;
    bool hold = false; // acts while held (momentary tool), as opposed to on press

    bool operator==(const KeyChord& o) const { return key == o.key && ch == o.ch && mods == o.mods; }
};

struct ActionInfo {
    Action action;
    const char* group;
    const char* label;
    std::vector<KeyChord> chords;
    Scope scope = Scope::Everywhere;
};

const std::vector<ActionInfo>& actionTable();
const ActionInfo& actionInfo(Action action);

/// A key press as the editor sees it. `name` is Rack's keyName (layout-correct for printable keys).
struct KeyInput {
    int key;
    std::string name;
    int mods;
};

/// The action bound to a key press in the given pane (pressed, not held, bindings).
std::optional<Action> findAction(const KeyInput& input, Scope pane);

/// "Ctrl+Shift+Z", or "⌘⇧Z" on macOS. Empty when the action has no binding.
std::string shortcutLabel(Action action);
/// "Loop  (L)": a tooltip with the action's shortcut, or just `text` when it has none.
std::string withShortcut(const std::string& text, Action action);
std::string chordLabel(const KeyChord& chord);

} // namespace tessera
