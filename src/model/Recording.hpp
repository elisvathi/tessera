#pragma once

#include "ArrangementEdits.hpp"
#include "Commands.hpp"
#include "NoteEdits.hpp"

#include <jansson.h>

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

    bool operator==(const RecordSettings& o) const {
        return countInBars == o.countInBars && clipBars == o.clipBars && arranger == o.arranger &&
               quantize == o.quantize && strength == o.strength && toKey == o.toKey;
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
};

/// The smallest whole number of bars that holds `length` (at least one bar).
Tick wholeBars(Tick length, TimeSig ts);

/// A new looping clip of `length` holding the take, in an empty launcher slot. `nextId` gives the clip and
/// note IDs; `noteIds` receives the notes' IDs. Null when the slot is taken or doesn't exist.
CommandPtr recordNewClip(const Song& song, TrackId track, int slot, Tick length, const std::vector<TakeNote>& take,
                         const NextId& nextId, ClipId* clipId = nullptr, edits::IdSet* noteIds = nullptr,
                         const std::string& name = "");

/// Adds the take to an existing clip (loop overdub: every pass merges). Notes land modulo the clip loop.
CommandPtr recordIntoClip(const Song& song, ClipId clip, const std::vector<TakeNote>& take, const NextId& nextId,
                          edits::IdSet* noteIds = nullptr);

/// A new clip on the timeline over [from, to) holding the take (timeline ticks). With Overdub, notes the track
/// already plays in that range are merged into it; with Replace they are dropped. Either way the new clip
/// replaces the old ones there (they are trimmed or split around it).
CommandPtr recordToArrangement(const Song& song, TrackId track, Tick from, Tick to, const std::vector<TakeNote>& take,
                               ArrangerRecordMode mode, const NextId& nextId, ClipId* clipId = nullptr,
                               edits::IdSet* noteIds = nullptr);

/// Moves note starts toward the grid by `strength` (0..1). Lengths stay.
edits::NoteList quantizeStarts(const edits::NoteList& notes, const edits::IdSet& ids, Tick grid, float strength);

/// The recorded notes of a clip quantized, as a separate undo step so undo brings back the played timing.
CommandPtr quantizeTake(const Song& song, ClipId clip, const edits::IdSet& ids, Tick grid, float strength);

} // namespace tessera
