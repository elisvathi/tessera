#pragma once

#include "Device.hpp"

#include <cmath>
#include <cstdint>

/// Modulator outputs (docs/DEVICES.md §3). Pure and allocation free; the engine calls evaluate() once per control
/// block for each modulator. Randomness depends only on the song seed, the modulator and the cycle, so a loop
/// plays (and exports) the same each time.
namespace tessera::mods {

namespace knob {
enum Lfo { kShape, kSync, kRate, kSyncedRate, kPhase, kBipolar, kRestart };
enum Macro { kValue };
enum Envelope { kAttack, kDecay, kSustain, kRelease };
enum Steps { kStepCount, kStepRate, kStepSmooth, kStepBipolar };
enum Random { kRandomRate, kRandomSmooth, kRandomBipolar };
enum Ramp { kBars, kDirection, kThen, kCurve };
enum NoteFollow { kFollows, kSmooth };
} // namespace knob

enum class Shape : uint8_t { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom };
enum class Restart : uint8_t { Free, EachBar, EachNote };

/// What the last note on a modulator's track was, for Envelope and Note.
struct NoteValues {
    float velocity = 0, pitch = 0, release = 0, timbre = 0, pressure = 0; // 0..1 (pitch: note / 127)
};

/// What a modulator remembers between blocks.
struct State {
    uint64_t owner = 0;   // the modulator ID this state belongs to (a reused slot starts over)
    double phase = 0;     // free-running (Hz) LFOs: cycles since the last restart
    double noteAt = NAN;  // song tick of the last note, for "each note"
    bool noteNow = false; // a note started since the last block
    int64_t bar = INT64_MIN;
    float out = 0;
    // Envelope: seconds since the note started, how long it is held (infinite while a key is down), the level when
    // it was let go.
    double envTime = 1e9, envHold = 0;
    float envLevel = 0, releaseFrom = 0;
    NoteValues note; // Note: the last note's values
};

/// Where the song is for this block.
struct Clock {
    double songTick = 0;
    double seconds = 0; // length of this block
    TimeSig timeSig;
    uint32_t seed = 1;
    double ticksPerSecond = 1920; // the tempo, for envelopes timing a note's length
};

/// The modulator's output: -1..1 when bipolar, else 0..1. `steps` are a Steps modulator's drawn values.
float evaluate(ModulatorType type, uint64_t id, const float* knobs, State& state, const Clock& clock,
               const float* steps = nullptr, int stepCount = 0);
bool bipolar(ModulatorType type, const float* knobs);
/// A note started on the modulator's track, lasting `lengthTicks` (infinite while a key is held).
void trigger(State& state, double songTick, double lengthTicks, const NoteValues& values, const Clock& clock);
/// A held key was let go: an envelope still holding starts its release now.
inline void releaseNote(State& state) {
    if (state.envTime < state.envHold)
        state.envHold = state.envTime;
}

/// One LFO cycle at `frac` (0..1) as -1..1; random shapes take their values for this cycle and the next.
float lfoShape(Shape shape, double frac, float thisCycle, float nextCycle);

} // namespace tessera::mods
