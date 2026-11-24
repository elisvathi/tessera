#pragma once

#include "Types.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include <jansson.h>

/// Launching clips from a MIDI controller (docs/DECISIONS.md §179): a grid controller's pads show an 8x8 window of
/// the launcher and light up to match it; any control can also be learned for a slot, a scene or a stop; program
/// changes can launch scenes. Pure: what a message does and which messages light the pads. The module owns the
/// MIDI ports and carries the actions out.
namespace tessera::clipControl {

/// How a controller's pads and buttons are read and lit. Off: learned controls and program changes only.
enum class Layout : uint8_t { Off, LaunchpadX, LaunchpadMini, ApcMini, NoteGrid };
constexpr int kLayouts = 5;
const char* layoutName(Layout l);
/// Whether the layout is an 8x8 grid that follows the launcher's window.
inline bool isGrid(Layout l) { return l != Layout::Off; }
/// Whether the controller has lights Tessera drives.
inline bool hasLights(Layout l) { return l == Layout::LaunchpadX || l == Layout::LaunchpadMini || l == Layout::ApcMini; }

constexpr int kSize = 8; // the grid is kSize tracks by kSize scenes

enum class Kind : uint8_t { None, Slot, Scene, StopTrack, StopAll, Up, Down, Left, Right };

/// What a pressed control does. A grid's slot and track stop count `column` from the window's first track; a
/// learned one names its `track`. Scenes are absolute.
struct Action {
    Kind kind = Kind::None;
    int column = -1;
    TrackId track;
    int scene = -1;
};

/// A control learned for an action: a note, a controller or a program change on a channel.
struct Binding {
    enum class Type : uint8_t { Note, Cc, Program } type = Type::Note;
    uint8_t channel = 0; // 0..15
    uint8_t number = 0;
    Kind kind = Kind::Slot;
    TrackId track; // Slot, StopTrack
    int scene = -1; // Slot, Scene

    bool sameControl(const Binding& o) const { return type == o.type && channel == o.channel && number == o.number; }
    bool sameAction(const Binding& o) const { return kind == o.kind && track == o.track && scene == o.scene; }
    bool operator==(const Binding& o) const { return sameControl(o) && sameAction(o); }
};

struct Settings {
    Layout layout = Layout::Off;
    int noteBase = 36;          // NoteGrid: the bottom-left pad's note
    int channel = 0;            // learned controls, NoteGrid and program changes: 1..16, 0 = any
    bool programScenes = false; // program change n launches scene n + 1
    std::vector<Binding> learned;

    bool operator==(const Settings& o) const {
        return layout == o.layout && noteBase == o.noteBase && channel == o.channel &&
               programScenes == o.programScenes && learned == o.learned;
    }
    json_t* toJson() const;
    static Settings fromJson(const json_t* j);
};

/// The part of the launcher the grid shows: its first track (in the song's order) and first scene.
struct Window {
    int track = 0, scene = 0;
    bool operator==(const Window& o) const { return track == o.track && scene == o.scene; }
};

/// What a controller holds down between messages (an APC mini's Shift).
struct InputState {
    bool shift = false;
};

/// What one incoming message does, under `s` with the window at `w`; Kind::None for a release or anything unmapped.
Action decode(const Settings& s, const Window& w, InputState& state, const uint8_t* bytes, int size);
/// The control a pressed message is, for learning; false for releases and other messages.
bool controlOf(const uint8_t* bytes, int size, Binding& out);

/// The window moved by one step of `kind` (Up, Down, Left, Right), kept so it shows `tracks` x `scenes` as fully as
/// they allow.
Window moved(const Window& w, Kind kind, int tracks, int scenes);
/// The window kept inside a launcher of `tracks` x `scenes` (after tracks or scenes went away).
Window clamped(const Window& w, int tracks, int scenes);

/// One pad's state on the launcher.
struct Cell {
    enum class State : uint8_t { Empty, Clip, Playing, Queued, Recording, Stopping } state = State::Empty;
    uint32_t color = 0; // 0xRRGGBB, the clip's
    bool operator==(const Cell& o) const { return state == o.state && color == o.color; }
};

/// What the controller shows: the window's cells (row 0 is its first scene), whether each of its scenes holds
/// clips, whether each track plays, and which ways the window can move.
struct Surface {
    std::array<std::array<Cell, kSize>, kSize> cells{}; // [row][column]
    std::array<bool, kSize> sceneHasClips{};
    std::array<bool, kSize> trackPlays{};
    bool up = false, down = false, left = false, right = false;
    bool anyPlays = false;
};

using Message = std::vector<uint8_t>;

/// The messages that light `s` on the controller. `lit` holds what each light shows now (by light, the last
/// messages sent), so only changes go out; clear it to send everything again (a device that just came).
std::vector<Message> lights(Layout layout, const Surface& s, std::map<int, Message>& lit);
/// The messages that put the controller in the mode Tessera drives it in, and that give it back (both may be empty).
std::vector<Message> enter(Layout layout);
std::vector<Message> leave(Layout layout);

/// The pad colour (0..127 in the 128-colour list these controllers share) nearest `rgb`.
int paletteIndex(uint32_t rgb);

} // namespace tessera::clipControl
