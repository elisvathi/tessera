#pragma once

#include "../model/ArrangementEdits.hpp"
#include "../model/AutomationEdits.hpp"
#include "../model/Chord.hpp"
#include "../model/NoteEdits.hpp"
#include "../model/Render.hpp"
#include "../model/ScaleRef.hpp"
#include "../model/Types.hpp"
#include "interaction/Axes.hpp"
#include "interaction/Navigation.hpp"
#include "interaction/Snap.hpp"

#include <jansson.h>

#include <map>
#include <optional>
#include <set>

namespace tessera {

enum class Pane { Top, Detail };

/// Where an editor is shown: the full-window overlay, or inside the widened Tessera panel.
enum class Surface { Overlay, Panel };

/// Ghost notes in the piano roll (EDITOR_UX §4.1): other notes drawn faintly behind the clip being edited.
enum class GhostSource { None, Track, AllTracks, OtherClips };

/// The per-note lanes the piano roll's lower pane can show (EDITOR_UX §4.6).
enum class NoteLane { Velocity, Release, Chance, VelocitySpread, Repeats, Micro, Pitch, Timbre, Pressure };

/// Note editing tools (EDITOR_UX §4.2-4.3). The zoom tool is a hold key (Z), not a selectable tool.
enum class Tool : int { Pointer, Draw, Paint, Erase, Slice, Mute, Velocity };

/// The selected notes and the clip they belong to. Not saved: a selection lasts for the session.
struct NoteSelection {
    ClipId clip;
    edits::IdSet notes;

    bool empty() const { return notes.empty(); }
    bool contains(NoteId id) const { return notes.count(id) > 0; }
    bool operator==(const NoteSelection& o) const { return clip == o.clip && notes == o.notes; }
};

/// Selected automation points: indices into one lane's points. Not saved.
struct AutoSelection {
    LaneOwner owner;
    AutomationId lane;
    autoEdits::Selection points;

    bool empty() const { return points.empty(); }
    bool on(LaneOwner o, AutomationId l) const { return owner == o && lane == l; }
};

/// A time range across tracks in the arranger (EDITOR_UX §6.4).
struct ArrangerRange {
    Tick from = 0, to = 0;
    std::vector<TrackId> tracks;

    bool empty() const { return to <= from || tracks.empty(); }
    bool covers(TrackId id) const { return std::find(tracks.begin(), tracks.end(), id) != tracks.end(); }
};

/// Layout and view settings of one editor surface. The overlay and the panel each keep their own, so zooming
/// one never moves the other.
struct ViewState {
    float topFraction = 0.45f; // share of the height above the splitter
    float headerWidth = 190;   // track header column, px at 100% scale
    bool inspectorOpen = true;
    Pane focus = Pane::Detail;
    std::optional<Pane> maximized;
    bool deviceStrip = true;        // the selected track's devices show above its notes (docs/DEVICES.md §1.1)
    bool drumLanes = true;          // drum tracks show lanes and step cells (G switches to the keys)
    float launcherPanelWidth = 233; // px: the clips beside the timeline, three scenes by default
    bool clipsWide = false;         // performing: the clips take the width, the timeline folds to a strip (F2)

    // A zero zoom means "not set yet": the view fits its content the first time it opens.
    TimeAxis::State arranger{0, 0};
    TimeAxis::State detail{0, 0};
    PitchAxis::State pitch{0, 0};
    double trackScroll = 0;
    double launcherScrollX = 0;              // px across the track columns
    double launcherScrollY = 0;              // px down the scenes
    std::map<uint64_t, double> trackHeights; // by TrackId value
    std::set<uint64_t> foldedGroups;         // by GroupId value
    std::set<uint64_t> openAutomation;       // tracks showing their automation lanes, by TrackId value
    std::map<uint64_t, double> laneHeights;  // by AutomationId value
    uint64_t lowerLane = 0;                  // the piano roll's lower pane: 0 = a note lane, else a clip lane
    NoteLane noteLane = NoteLane::Velocity;

    double defaultTrackHeight = 48; // for tracks never resized in this view

    double trackHeight(TrackId id) const {
        auto it = trackHeights.find(id.value);
        return it == trackHeights.end() ? defaultTrackHeight : it->second;
    }
    double laneHeight(AutomationId id) const {
        auto it = laneHeights.find(id.value);
        return it == laneHeights.end() ? defaultTrackHeight * 1.2 : it->second;
    }

    /// Forgets what belongs to one song: zoom and scroll, and sizes and folds kept per track, group or lane. The
    /// layout (splitter, header width, inspector, maximized pane) stays.
    void forgetSong();

    /// Starting layout for the in-panel editor, which is only 380 px tall at 100% rack zoom.
    static ViewState compact() {
        ViewState v;
        v.headerWidth = 150;
        v.defaultTrackHeight = 24; // the shortest a track row gets: a module is only 380 px tall
        v.topFraction = 0.42f;
        v.inspectorOpen = false;
        v.launcherPanelWidth = 160; // two scenes: the panel editor is small and the timeline needs room
        v.deviceStrip = false;
        return v;
    }
};

/// Editor settings kept by the Tessera module, so they survive closing the editor and are saved in the patch
/// under "ui". UI thread only.
/// The key picker's choice while it is open: the piano roll shows it before it is applied.
struct KeyPreview {
    bool active = false;
    ScaleRef song;                   // the song key
    ClipId clip;                     // the clip whose own key is being picked, if any
    std::optional<ScaleRef> clipKey; // that clip's key; empty = it follows the song
};

struct EditorState {
    static constexpr int kPanelMinHp = 24; // the controls alone
    static constexpr int kPanelEditorMinHp = 48;
    static constexpr int kPanelMaxHp = 160;

    // Shared by both surfaces: what you work on follows you.
    TrackId selectedTrack;               // the track the piano roll shows
    ClipId selectedClip;                 // the clip it shows, when that clip is on the selected track
    std::vector<TrackId> selectedTracks; // header multi-selection, session only
    FollowMode follow = FollowMode::Page;

    float uiScale = 0; // overlay size; 0 = follow Rack's UI scale
    int panelHp = kPanelMinHp;

    // Editing, shared by both surfaces
    Tool tool = Tool::Pointer;
    SnapSetting snap;
    Tick lastLength = kPpq / 4; // the Draw tool uses the length and velocity of the last note touched
    float lastVelocity = 0.8f;
    uint8_t lastChannel = 1;  // ...and its channel
    bool audition = true;     // play notes as they are drawn or moved
    bool transformBox = true; // show the stretch frame around multi-note selections
    bool noteBadges = true;   // chance, repeats and conditions drawn on notes
    GhostSource ghosts = GhostSource::None;
    std::vector<ClipId> layered;   // clips opened together (EDITOR_UX §4.9), session only
    render::Options exportOptions; // the MIDI export dialog's last settings
    TrackId ghostTrack;            // GhostSource::Track
    // Scale tools (DESIGN.md §13). They do nothing while the key is chromatic.
    bool scaleHighlight = true;                 // tint scale rows, mark the root, flag notes outside the key
    bool scaleSnap = false;                     // new and moved notes land on the key; Up/Down step through it
    bool scaleFold = false;                     // only rows of the key are shown
    bool keyMovesNotes = true;                  // a key change moves notes to the new key, same degrees
    KeyPreview keyPreview;                      // session only
    ChordStamp chord;                           // Draw places this chord
    NoteSelection selection;                    // session only
    arrange::IdSet clipSelection;               // arranger instances, session only
    AutoSelection autoSelection;                // session only
    std::optional<autoEdits::Shape> stampShape; // armed: the next drag in a lane stamps this shape
    ArrangerRange range;                        // the arranger's time selection, session only
    bool rangeTool = false;                     // drags in the arranger select time ranges
    bool ripple = false;                        // edit mode: deleting closes gaps (EDITOR_UX §6.4)
    bool stackClips = true;                     // overlapping clips on a track stack and play together
    bool automationFollowsClips = true;         // moving or copying clips carries track automation
    // Playing notes into the editor (EDITOR_UX §4.2, §4.10). Session only.
    bool stepInput = false;    // played notes are written at the step cursor
    Tick stepCursor = 0;       // clip ticks
    bool keyboardPlay = false; // the computer keyboard plays notes
    int keyboardOctave = 0;    // octaves above or below C4
    float keyboardVelocity = 100.f / 127;

    ViewState overlay;
    ViewState panel = ViewState::compact();

    ViewState& view(Surface s) { return s == Surface::Overlay ? overlay : panel; }

    /// A new song starts clean: selections, the open clip, layered and ghost clips, the step cursor and both views'
    /// per-song state are forgotten. Preferences (tools, snap, scale switches, export settings, layout, the panel's
    /// width) stay.
    void forgetSong();

    json_t* toJson() const;
    /// Missing or bad fields keep their defaults. Reads the single-view layout of milestone 3 as the overlay.
    static EditorState fromJson(const json_t* j);
};

} // namespace tessera
