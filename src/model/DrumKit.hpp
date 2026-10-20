#pragma once

#include "Clip.hpp"
#include "NoteEdits.hpp"
#include "Track.hpp"

#include <cstdint>
#include <vector>

/// Drum tracks (DESIGN.md §9): lane presets, where a lane's pattern lives in a clip, and the pattern tools of
/// the step grid. Pure, so it is unit tested.
namespace tessera {

constexpr int kMaxChokeGroups = 8;

enum class DrumKitPreset { Standard8, Gm16 };
/// "Standard 8" (kick, snare, clap, closed hat, open hat, low tom, high tom, rim) or "16 GM-ish". The hats
/// share choke group 1, so a closed hat cuts an open one.
std::vector<DrumLane> drumKit(DrumKitPreset preset);
const char* drumKitName(DrumKitPreset preset);

/// The lane a pitch plays, or -1.
int laneOfPitch(const std::vector<DrumLane>& lanes, int pitch);

/// Where a lane's steps live in a clip: from the loop start (the clip start when it doesn't loop), for the
/// lane's own loop length (polymeter) or else the clip's loop.
struct LaneRegion {
    Tick start;
    Tick length;
    Tick steps(Tick stepLength) const { return stepLength > 0 ? std::max<Tick>(1, length / stepLength) : 1; }
};
LaneRegion laneRegion(const DrumLane& lane, const Clip& clip);
/// True when the lane loops on its own length, apart from the clip (polymeter).
bool hasOwnLoop(const DrumLane& lane, const Clip& clip);

/// Euclidean rhythm: `hits` spread as evenly as possible over `steps`, turned right by `rotation`.
std::vector<bool> euclid(int hits, int steps, int rotation = 0);

/// Pattern tools. Each takes a clip's notes and returns the edited list (sorted); only the lane's notes
/// (its pitch) inside `region` are touched.
namespace drums {

using edits::IdSource;
using edits::NoteList;

/// The lane's notes in the region.
NoteList laneNotes(const NoteList& notes, int pitch, LaneRegion region);
/// Clears the step starting at `stepStart`; with `velocity`, puts a step-long note there.
NoteList setStep(const NoteList& notes, int pitch, Tick stepStart, Tick stepLength, const float* velocity,
                 const IdSource& newId);
/// Replaces the lane with `pattern` (one entry per step).
NoteList applyPattern(const NoteList& notes, int pitch, LaneRegion region, Tick stepLength,
                      const std::vector<bool>& pattern, float velocity, const IdSource& newId);
/// Turns the lane by `by` ticks, wrapping inside the region.
NoteList rotate(const NoteList& notes, int pitch, LaneRegion region, Tick by);
/// Plays the lane backwards, step for step.
NoteList reverse(const NoteList& notes, int pitch, LaneRegion region, Tick stepLength);
/// Random steps: each step is on with probability `density`, with a velocity between `velLow` and `velHigh`.
/// The same seed always gives the same pattern.
NoteList randomize(const NoteList& notes, int pitch, LaneRegion region, Tick stepLength, float density, float velLow,
                   float velHigh, uint32_t seed, const IdSource& newId);
NoteList clear(const NoteList& notes, int pitch, LaneRegion region);
/// Puts `copied` (notes relative to 0, any pitch) into the lane, replacing what was there.
NoteList paste(const NoteList& notes, int pitch, LaneRegion region, const NoteList& copied, const IdSource& newId);

} // namespace drums
} // namespace tessera
