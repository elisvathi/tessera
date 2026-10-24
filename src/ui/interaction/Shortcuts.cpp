#include "Shortcuts.hpp"

#include <GLFW/glfw3.h>

#include <cassert>

namespace tessera {

namespace {

KeyChord K(int key, int mods = 0) { return {key, "", mods, false}; }
KeyChord C(const char* ch, int mods = 0) { return {0, ch, mods, false}; }
KeyChord Hold(KeyChord c) {
    c.hold = true;
    return c;
}

constexpr int S = kModShift;
constexpr int Ctl = kModCtrl;
constexpr int A = kModAlt;

std::vector<ActionInfo> buildTable() {
    using Ac = Action;
    using Sc = Scope;
    // Conflicts in EDITOR_UX §7 are resolved here; see docs/DECISIONS.md §12.
    return {
        {Ac::PlayStop, "Transport", "Play / stop", {K(GLFW_KEY_SPACE)}},
        {Ac::PlayFromMouse, "Transport", "Play from mouse / selection start", {K(GLFW_KEY_SPACE, S)}},
        {Ac::Record, "Transport", "Record (right-click the button for settings)", {C("r")}},
        {Ac::CaptureMidi, "Transport", "Capture what was just played as a clip", {C("r", Ctl | S)}},
        {Ac::ToggleLoop, "Transport", "Loop on / off", {C("l")}},
        {Ac::LoopToSelection, "Transport", "Set loop to selection", {C("l", Ctl)}},
        {Ac::DeleteTime, "Edit", "Delete time (the range closes up)", {K(GLFW_KEY_DELETE, Ctl | S)}, Sc::TopPane},
        {Ac::InsertSilence, "Edit", "Insert silence (as long as the range)", {C("i", Ctl | S)}, Sc::TopPane},
        {Ac::ToggleFollow, "Transport", "Follow playhead", {C("f", Ctl)}},
        {Ac::Fill, "Transport", "Fill (hold)", {Hold(K(GLFW_KEY_F1))}},

        {Ac::ToolPointer, "Tools", "Pointer", {C("s"), C("v")}},
        {Ac::ToolDraw, "Tools", "Draw", {C("d"), C("b")}},
        {Ac::ToolPaint, "Tools", "Paint", {C("p")}},
        {Ac::ToolErase, "Tools", "Erase", {C("e")}},
        {Ac::ToolSlice, "Tools", "Slice", {C("c")}},
        {Ac::ToolMute, "Tools", "Mute", {C("t")}},
        {Ac::ToolVelocity, "Tools", "Velocity", {C("y")}},
        {Ac::ToolZoom, "Tools", "Zoom (hold)", {Hold(C("z"))}},
        {Ac::ToolRange, "Tools", "Range", {C("r", A)}, Sc::TopPane},
        {Ac::StepInput, "Tools", "Step input", {C("r", Ctl)}},
        {Ac::KeyboardPlay, "Tools", "Keyboard play", {C("k", Ctl)}},
        {Ac::ToggleAudition, "Tools", "Hear notes while editing", {C("a", A)}},

        {Ac::Undo, "Edit", "Undo", {C("z", Ctl)}},
        {Ac::Redo, "Edit", "Redo", {C("z", Ctl | S), C("y", Ctl)}},
        {Ac::Cut, "Edit", "Cut", {C("x", Ctl)}},
        {Ac::Copy, "Edit", "Copy", {C("c", Ctl)}},
        {Ac::Paste, "Edit", "Paste", {C("v", Ctl)}},
        {Ac::PasteAtMouse, "Edit", "Paste at mouse", {C("v", Ctl | S)}},
        {Ac::Duplicate, "Edit", "Duplicate", {C("d", Ctl)}},
        {Ac::Delete, "Edit", "Delete", {K(GLFW_KEY_DELETE), K(GLFW_KEY_BACKSPACE)}},
        {Ac::SelectAll, "Edit", "Select all", {C("a", Ctl)}},
        {Ac::SelectNone, "Edit", "Select none", {C("a", Ctl | S)}},
        {Ac::SelectInvert, "Edit", "Invert selection", {C("i", Ctl)}},
        {Ac::Split, "Edit", "Split", {C("e", Ctl)}},
        {Ac::Glue, "Edit", "Glue", {C("j", Ctl)}},
        {Ac::MakeUnique, "Edit", "Make unique", {C("u", Ctl)}},
        {Ac::Quantize, "Edit", "Quantize", {C("q")}},
        {Ac::QuantizeDialog, "Edit", "Quantize…", {C("q", S)}},
        {Ac::TransposeUp, "Edit", "Transpose up (a key step with scale snap)", {K(GLFW_KEY_UP)}},
        {Ac::TransposeDown, "Edit", "Transpose down (a key step with scale snap)", {K(GLFW_KEY_DOWN)}},
        {Ac::OctaveUp, "Edit", "Octave up", {K(GLFW_KEY_UP, S)}},
        {Ac::OctaveDown, "Edit", "Octave down", {K(GLFW_KEY_DOWN, S)}},
        {Ac::NudgeLeft, "Edit", "Nudge left", {K(GLFW_KEY_LEFT)}},
        {Ac::NudgeRight, "Edit", "Nudge right", {K(GLFW_KEY_RIGHT)}},
        {Ac::NudgeFineLeft, "Edit", "Nudge left (fine)", {K(GLFW_KEY_LEFT, Ctl | A)}},
        {Ac::NudgeFineRight, "Edit", "Nudge right (fine)", {K(GLFW_KEY_RIGHT, Ctl | A)}},
        {Ac::NudgeBarLeft, "Edit", "Nudge left one bar", {K(GLFW_KEY_LEFT, S)}},
        {Ac::NudgeBarRight, "Edit", "Nudge right one bar", {K(GLFW_KEY_RIGHT, S)}},
        {Ac::LengthShorter, "Edit", "Shorter by one grid step", {K(GLFW_KEY_LEFT, S | A)}},
        {Ac::LengthLonger, "Edit", "Longer by one grid step", {K(GLFW_KEY_RIGHT, S | A)}},
        {Ac::VelocityUp, "Edit", "Velocity +5%", {K(GLFW_KEY_UP, Ctl | A)}},
        {Ac::VelocityDown, "Edit", "Velocity −5%", {K(GLFW_KEY_DOWN, Ctl | A)}},
        {Ac::VelocityUpFine, "Edit", "Velocity +1%", {K(GLFW_KEY_UP, Ctl | A | S)}},
        {Ac::VelocityDownFine, "Edit", "Velocity −1%", {K(GLFW_KEY_DOWN, Ctl | A | S)}},
        {Ac::CycleLeft, "Edit", "Select previous note", {K(GLFW_KEY_LEFT, A)}},
        {Ac::CycleRight, "Edit", "Select next note", {K(GLFW_KEY_RIGHT, A)}},
        {Ac::CycleUp, "Edit", "Select note above", {K(GLFW_KEY_UP, A)}},
        {Ac::CycleDown, "Edit", "Select note below", {K(GLFW_KEY_DOWN, A)}},
        {Ac::TransformBox, "Edit", "Transform box", {C("t", Ctl | S)}, Sc::DetailPane},

        {Ac::ZoomToFit, "View", "Zoom to fit", {C("f")}},
        {Ac::ZoomToSelection, "View", "Zoom to selection", {C("z")}},
        {Ac::ZoomIn, "View", "Zoom in", {C("="), C("=", S), K(GLFW_KEY_KP_ADD)}},
        {Ac::ZoomOut, "View", "Zoom out", {C("-"), K(GLFW_KEY_KP_SUBTRACT)}},
        {Ac::ZoomBack, "View", "Previous zoom", {C("[", Ctl)}},
        {Ac::ZoomForward, "View", "Next zoom", {C("]", Ctl)}},
        {Ac::GoToStart, "View", "Go to start", {K(GLFW_KEY_HOME)}},
        {Ac::GoToEnd, "View", "Go to end", {K(GLFW_KEY_END)}},
        {Ac::PageLeft, "View", "Scroll one screen left", {K(GLFW_KEY_PAGE_UP)}},
        {Ac::PageRight, "View", "Scroll one screen right", {K(GLFW_KEY_PAGE_DOWN)}},
        {Ac::FitAllTracks, "View", "Fit all tracks", {C("f", S)}, Sc::TopPane},
        {Ac::ShowUsedLanes, "View", "Show used automation lanes", {C("a", S)}},
        {Ac::MaximizeLane, "View", "Maximize automation lane", {C("a")}},
        {Ac::GridNarrower, "View", "Narrower grid", {C("1", Ctl)}},
        {Ac::GridWider, "View", "Wider grid", {C("2", Ctl)}},
        {Ac::GridTriplet, "View", "Triplet grid", {C("3", Ctl)}},
        {Ac::SnapToggle, "View", "Snap on / off", {C("4", Ctl)}},
        {Ac::ScaleFold, "View", "Scale fold (only rows of the key)", {C("f", Ctl | S)}, Sc::DetailPane},
        {Ac::ToggleInspector, "View", "Inspector", {C("i")}},
        {Ac::SwitchPane, "View", "Switch pane", {K(GLFW_KEY_TAB)}},
        {Ac::MaximizePane, "View", "Maximize pane", {K(GLFW_KEY_TAB, S)}},
        {Ac::WidenClips, "View", "Widen / narrow the clips beside the timeline", {K(GLFW_KEY_F2)}},
        {Ac::ToggleDevices, "View", "Device strip above the notes", {K(GLFW_KEY_F3)}},
        {Ac::ToggleDrumView, "View", "Drum lanes / keys", {C("g")}, Sc::DetailPane},

        {Ac::AddInstrumentTrack, "Tracks", "Add instrument track", {C("t", Ctl)}},
        {Ac::AddDrumTrack, "Tracks", "Add drum track", {C("t", Ctl | S)}, Sc::TopPane},
        {Ac::GroupTracks, "Tracks", "Group selected tracks", {C("g", Ctl)}, Sc::TopPane},
        {Ac::NextTrack, "Tracks", "Select next track", {K(GLFW_KEY_DOWN, Ctl)}},
        {Ac::PreviousTrack, "Tracks", "Select previous track", {K(GLFW_KEY_UP, Ctl)}},
        {Ac::MuteTracks, "Tracks", "Mute selected tracks", {C("m")}},
        {Ac::SoloTracks, "Tracks", "Solo selected tracks", {C("s", A)}},
        {Ac::AddMarker, "Tracks", "Add marker", {C("m", Ctl)}},

        {Ac::Help, "General", "Shortcut help", {C("/", S)}},
        {Ac::ExportMidi, "General", "Export MIDI…", {C("e", Ctl | S)}},
        {Ac::Close, "General", "Close editor", {K(GLFW_KEY_ESCAPE)}},
    };
}

bool matches(const KeyChord& chord, const KeyInput& in) {
    if (chord.hold || chord.mods != in.mods)
        return false;
    return chord.ch.empty() ? (chord.key != 0 && chord.key == in.key) : chord.ch == in.name;
}

std::string keyName(const KeyChord& c) {
    if (!c.ch.empty()) {
        std::string s = c.ch;
        for (char& ch : s)
            ch = char(toupper(static_cast<unsigned char>(ch)));
        return s;
    }
    switch (c.key) {
    case GLFW_KEY_SPACE:
        return "Space";
    case GLFW_KEY_DELETE:
        return "Delete";
    case GLFW_KEY_BACKSPACE:
        return "Backspace";
    case GLFW_KEY_UP:
        return "↑";
    case GLFW_KEY_DOWN:
        return "↓";
    case GLFW_KEY_LEFT:
        return "←";
    case GLFW_KEY_RIGHT:
        return "→";
    case GLFW_KEY_HOME:
        return "Home";
    case GLFW_KEY_END:
        return "End";
    case GLFW_KEY_PAGE_UP:
        return "Page Up";
    case GLFW_KEY_PAGE_DOWN:
        return "Page Down";
    case GLFW_KEY_TAB:
        return "Tab";
    case GLFW_KEY_ESCAPE:
        return "Esc";
    case GLFW_KEY_KP_ADD:
        return "Num +";
    case GLFW_KEY_KP_SUBTRACT:
        return "Num −";
    case GLFW_KEY_F1:
        return "F1";
    case GLFW_KEY_F2:
        return "F2";
    case GLFW_KEY_F3:
        return "F3";
    default:
        return "?";
    }
}

} // namespace

const std::vector<ActionInfo>& actionTable() {
    static const std::vector<ActionInfo> table = buildTable();
    return table;
}

const ActionInfo& actionInfo(Action action) {
    for (const ActionInfo& info : actionTable())
        if (info.action == action)
            return info;
    assert(false && "action missing from the table");
    return actionTable().front();
}

std::optional<Action> findAction(const KeyInput& input, Scope pane) {
    for (const ActionInfo& info : actionTable()) {
        if (info.scope != Scope::Everywhere && info.scope != pane)
            continue;
        for (const KeyChord& chord : info.chords)
            if (matches(chord, input))
                return info.action;
    }
    return std::nullopt;
}

std::string chordLabel(const KeyChord& c) {
#ifdef ARCH_MAC
    std::string s;
    if (c.mods & kModCtrl)
        s += "⌘";
    if (c.mods & kModAlt)
        s += "⌥";
    if (c.mods & kModShift)
        s += "⇧";
    return s + keyName(c);
#else
    std::string s;
    if (c.mods & kModCtrl)
        s += "Ctrl+";
    if (c.mods & kModAlt)
        s += "Alt+";
    if (c.mods & kModShift)
        s += "Shift+";
    return s + keyName(c);
#endif
}

std::string shortcutLabel(Action action) {
    const ActionInfo& info = actionInfo(action);
    return info.chords.empty() ? "" : chordLabel(info.chords.front());
}

std::string withShortcut(const std::string& text, Action action) {
    std::string key = shortcutLabel(action);
    return key.empty() ? text : text + "  (" + key + ")";
}

} // namespace tessera
