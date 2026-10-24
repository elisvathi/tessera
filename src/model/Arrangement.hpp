#pragma once

#include "Song.hpp"

#include <optional>
#include <vector>

/// How a clip instance on the arranger timeline plays: where its content starts (offset), how it repeats
/// (the clip's loop), how fast (stretch), and its transpose and velocity scale. Shared by the engine's
/// snapshot, the arranger's drawing, glue, and later MIDI export, so they always agree.
namespace tessera {

constexpr float kMinStretch = 1.f / 16;
constexpr float kMaxStretch = 16.f;

/// A note as an instance plays it, in timeline ticks.
struct TimedNote {
    Tick start;
    Tick end; // > start
    int8_t pitch;
    float velocity;
    float releaseVelocity;
    const Note* source; // the clip note it came from (valid while the clip is unchanged)
    int pass = 0;       // which time through the clip (or a drum lane's own loop) the instance is on
};

inline Tick instanceEnd(const ArrangerClip& a) { return a.position + a.length; }
/// How fast an instance plays its clip, kept inside [kMinStretch, kMaxStretch].
double stretchOf(const ArrangerClip& a);

/// The part of the clip an instance plays on its first pass and on every repeat, in clip ticks.
/// Non-looping clips play [0, contentEnd) once (up to their last note); looping clips repeat [loopStart, loopEnd).
/// With a drum track's `lanes`, a looping clip with lanes of their own length repeats its whole lane cycle
/// (laneCycle()), and inside it the notes not on those lanes repeat the clip's loop, `pattern` long.
struct ClipSpan {
    bool looping;
    Tick start;   // loop start (looping) or 0
    Tick end;     // loop end, or the lane cycle's end (looping), or where the content ends
    Tick pattern; // how often the notes not on a lane of their own repeat: the clip's loop, or length()
    Tick length() const { return end - start; }
    /// True when the span is a lane cycle: longer than the loop it repeats.
    bool cycles() const { return looping && pattern < length(); }
    /// Clip time `u` inside the span brought into the loop the pattern repeats, for what follows the clip loop
    /// (its automation, its notes not on a lane of their own).
    double patternTime(double u) const;
};
ClipSpan clipSpan(const Clip& clip, const std::vector<DrumLane>* lanes = nullptr);

/// Brings an offset into the range the clip can play from: [0, loop end) for looping clips (wrapping), and
/// [0, length] for the others.
Tick normalizeOffset(const Clip& clip, Tick offset, const std::vector<DrumLane>* lanes = nullptr);

/// Appends the notes `instance` plays to `out`, sorted by start. Muted notes are skipped; notes are cut at
/// loop seams and at the instance end. `transpose` adds the track's transpose to the instance's own.
/// With a drum track's `lanes`, lanes with their own loop length repeat on it (polymeter).
void renderInstance(const Clip& clip, const ArrangerClip& instance, std::vector<TimedNote>& out, int transpose = 0,
                    float gateScale = 1, const std::vector<DrumLane>* lanes = nullptr);

/// The clip time playing at timeline tick `tick`, or nothing outside the instance or after a non-looping
/// clip ran out.
std::optional<double> clipTimeAt(const Clip& clip, const ArrangerClip& instance, double tick,
                                 const std::vector<DrumLane>* lanes = nullptr);
/// The other way: the song tick where instance `a` plays clip time `u`, in the loop pass the song tick `near` is in
/// (when it is inside the instance), otherwise the first pass that plays it. Nullopt when it never plays `u`.
std::optional<double> songTickAt(const Clip& clip, const ArrangerClip& a, double u, double near,
                                 const std::vector<DrumLane>* lanes = nullptr);

/// Timeline ticks where the content repeats (loop seams) inside the instance, for drawing notches.
std::vector<Tick> loopSeams(const Clip& clip, const ArrangerClip& instance,
                            const std::vector<DrumLane>* lanes = nullptr);

/// The instance on `track` that covers `tick`, or nullptr.
const ArrangerClip* instanceAt(const Track& track, Tick tick);

} // namespace tessera
