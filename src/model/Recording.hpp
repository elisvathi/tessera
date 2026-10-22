#pragma once

#include "ArrangementEdits.hpp"
#include "Commands.hpp"
#include "NoteEdits.hpp"

#include <jansson.h>

#include <string>
#include <vector>

/// Turning recorded notes into song edits (DESIGN.md §14). Each function returns one undo step.
namespace tessera {

/// What recording onto the timeline does to clips already there.
enum class ArrangerRecordMode : uint8_t { Overdub, Replace };

/// Recording settings of one Tessera, saved with it (not song content, so not undoable).
struct RecordSettings {
    static constexpr int kMaxCountInBars = 2;

    int countInBars = 0; // 0 = off
    int clipBars = 0;    // launcher: a new clip's length; 0 = until recording stops, rounded up to the bar
    ArrangerRecordMode arranger = ArrangerRecordMode::Overdub;
    Tick quantize = 0; // input quantize grid; 0 = off
    float strength = 1;
    bool toKey = false; // incoming pitches snap to the key
    bool mpe = false;   // MIDI input reads MPE: each note on its own channel (docs/design/EXPRESSION_INPUT.md)

    bool operator==(const RecordSettings& o) const {
        return countInBars == o.countInBars && clipBars == o.clipBars && arranger == o.arranger &&
               quantize == o.quantize && strength == o.strength && toKey == o.toKey && mpe == o.mpe;
    }

    json_t* toJson() const;
    /// Missing or bad fields keep their defaults.
    static RecordSettings fromJson(const json_t* j);
};

/// A recorded note in the ticks of where it lands (clip ticks or timeline ticks).
struct TakeNote {
    Tick start = 0;
    Tick length = 1;
    int8_t pitch = 60;
    float velocity = 0.8f;
    float release = 0.5f;
    uint8_t channel = 1;
    // What was played on the note itself (poly aftertouch, MPE), across the note: pitch in semitones, else 0..1.
    std::vector<ExprPoint> pitchExpr, timbreExpr, pressureExpr;
};

/// The values one MIDI control sent during a take, in the ticks of where it lands, already thinned.
struct TakeLane {
    MidiControl control;
    std::vector<AutoPoint> points; // sorted by t
};

/// Everything a take records: the notes and the controls moved while they were played.
struct Take {
    std::vector<TakeNote> notes;
    std::vector<TakeLane> lanes;

    Take() = default;
    Take(std::vector<TakeNote> notes) : notes(std::move(notes)) {}
    bool empty() const { return notes.empty() && lanes.empty(); }
};

/// The name a lane recording `control` gets, e.g. "Pitch bend", "Mod wheel (CC 1)", "CC 20".
std::string midiControlName(MidiControl control);

/// A new lane for a MIDI control: pitch bend on the voices' pitch (±2 semitones), pressure on PRESS, a CC on AUTO
/// channel `output` (0..10 V; switch controls such as sustain step instead of gliding).
AutomationEnvelope makeMidiLane(AutomationId id, MidiControl control, int output, uint32_t color);

/// The AUTO channel a CC lane of `track` should use: the one a lane for the same control has anywhere on the track,
/// else the first channel no lane of the track uses (`pending` counts too: lanes about to be added). -1 when all
/// channels are taken.
int midiLaneOutput(const Song& song, const Track& track, MidiControl control,
                   const std::vector<AutomationEnvelope>& pending);

/// Which of `values` (at increasing ticks `ticks`) to keep so the line through the kept ones stays within
/// `tolerance` of every value; never more than `maxPoints`. The first and last are always kept.
std::vector<size_t> thinValues(const std::vector<double>& ticks, const std::vector<float>& values, float tolerance,
                               size_t maxPoints);

/// The smallest whole number of bars that holds `length` (at least one bar).
Tick wholeBars(Tick length, TimeSig ts);

/// A new looping clip of `length` holding the take, in an empty launcher slot. `nextId` gives the clip and
/// note IDs; `noteIds` receives the notes' IDs. Null when the slot is taken or doesn't exist.
CommandPtr recordNewClip(const Song& song, TrackId track, int slot, Tick length, const Take& take,
                         const NextId& nextId, ClipId* clipId = nullptr, edits::IdSet* noteIds = nullptr,
                         const std::string& name = "");

/// Adds the take to an existing clip (loop overdub: every pass merges). Notes and lane values land modulo the
/// clip loop; a take's values replace the lane's points over the stretch they cover.
CommandPtr recordIntoClip(const Song& song, ClipId clip, const Take& take, const NextId& nextId,
                          edits::IdSet* noteIds = nullptr);

/// A new clip on the timeline over [from, to) holding the take (timeline ticks). With Overdub, notes the track
/// already plays in that range are merged into it; with Replace they are dropped. Either way the new clip
/// replaces the old ones there (they are trimmed or split around it).
CommandPtr recordToArrangement(const Song& song, TrackId track, Tick from, Tick to, const Take& take,
                               ArrangerRecordMode mode, const NextId& nextId, ClipId* clipId = nullptr,
                               edits::IdSet* noteIds = nullptr);

/// Moves note starts toward the grid by `strength` (0..1). Lengths stay.
edits::NoteList quantizeStarts(const edits::NoteList& notes, const edits::IdSet& ids, Tick grid, float strength);

/// The recorded notes of a clip quantized, as a separate undo step so undo brings back the played timing.
CommandPtr quantizeTake(const Song& song, ClipId clip, const edits::IdSet& ids, Tick grid, float strength);

} // namespace tessera
