#pragma once

#include "Note.hpp"

#include <cstdint>

namespace tessera {

/// A per-note expression curve's value at `pos` (0..1 across the note): straight lines between its points, bent
/// by each point's tension. Empty curves give `rest`.
float expressionAt(const std::vector<ExprPoint>& points, float pos, float rest);

} // namespace tessera

/// How a note plays on one pass of its clip (EDITOR_UX §4.5). Pure and allocation free, so the engine runs it on
/// the audio thread and MIDI export can run the same code to bake what plays.
namespace tessera::operators {

/// Randomness for one decision: the same inputs always give the same number in [0, 1), so a pattern repeats
/// within a session and "Reseed" (a new song seed) gives a new variation.
double random(uint32_t seed, uint64_t clip, int64_t pass, uint64_t note, uint32_t salt);

/// What a pass knows when it decides whether a note plays.
struct PassContext {
    uint32_t seed = 1;
    uint64_t clip = 0;          // ClipId value
    int64_t pass = 0;           // 0 = the first time through the clip since it started
    bool fill = false;          // the FILL button or gate
    bool previousPlayed = true; // the nearest earlier note of this clip on this pass played (true when none)
};

/// Chance, occurrence and recurrence together: does the note play on this pass?
bool plays(const Note& note, const PassContext& pass);

/// The note's velocity on this pass, with its velocity spread applied.
float velocity(const Note& note, const PassContext& pass);

/// Ticks of a 1/16, the unit of micro-timing and timing spread.
constexpr double kSixteenth = double(kPpq) / 4;
/// The note's fixed micro-timing offset in ticks (negative = early).
inline double microTicks(const Note& note) { return double(note.microOffset) * kSixteenth; }
/// The furthest timing spread can move the note either way, in ticks.
inline double spreadTicks(const Note& note) { return double(note.ops.timingSpread) * kSixteenth; }
/// This pass's random timing offset in [-spreadTicks, spreadTicks].
double timingOffset(const Note& note, const PassContext& pass);

/// How many hits the note plays (1 without repeats). Negative repeat counts mean hits per beat.
int hitCount(const Note& note, Tick ticksPerBeat);

/// One hit of a (repeated) note: when it starts and how long it is, in ticks from the note's start, and its
/// velocity before spread.
struct Hit {
    double start;
    double length;
    float velocity;
};
/// Hit `i` of `count`. With repeats, hits are spaced by the repeat curve (positive accelerates), last `repeatGate`
/// of their spacing, and their velocity moves from the note's to note velocity × repeatVelEnd.
Hit hit(const Note& note, int i, int count);

} // namespace tessera::operators
