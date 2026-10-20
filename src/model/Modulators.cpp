#include "Modulators.hpp"

#include "DeviceTypes.hpp"
#include "NoteOperators.hpp"

#include <algorithm>

namespace tessera::mods {

namespace {

constexpr uint32_t kRandomSalt = 0x10F0A11Du;
constexpr double kPi = 3.14159265358979323846;

float shown(ModulatorType type, const float* knobs, int k) {
    const devices::ParamInfo* p = devices::paramInfo(type, k);
    return p && knobs ? devices::display(*p, knobs[k]) : 0.f;
}

float randomAt(uint32_t seed, uint64_t id, int64_t cycle) {
    return float(operators::random(seed, id, cycle, 0, kRandomSalt)) * 2 - 1;
}

float lfo(uint64_t id, const float* knobs, State& state, const Clock& clock) {
    constexpr ModulatorType kType = ModulatorType::Lfo;
    Shape shape = Shape(int(shown(kType, knobs, knob::kShape)));
    Restart restart = Restart(int(shown(kType, knobs, knob::kRestart)));
    double bar = double(ticksPerBar(clock.timeSig));
    double phase;
    if (shown(kType, knobs, knob::kSync) > 0.5f) {
        // Synced: a function of the song position, so it lines up with the music and restarts with the transport.
        double rate = double(devices::rateTicks(int(shown(kType, knobs, knob::kSyncedRate)), clock.timeSig));
        double from = 0;
        if (restart == Restart::EachBar)
            from = std::floor(clock.songTick / bar) * bar;
        else if (restart == Restart::EachNote && std::isfinite(state.noteAt))
            from = state.noteAt;
        phase = (clock.songTick - from) / rate;
    } else {
        if (restart == Restart::EachBar) {
            int64_t b = int64_t(std::floor(clock.songTick / bar));
            if (b != state.bar && state.bar != INT64_MIN)
                state.phase = 0;
            state.bar = b;
        }
        if (restart == Restart::EachNote && state.noteNow)
            state.phase = 0;
        phase = state.phase;
        state.phase += double(shown(kType, knobs, knob::kRate)) * clock.seconds;
    }
    phase += double(shown(kType, knobs, knob::kPhase)) / 360;
    double cycle = std::floor(phase);
    int64_t c = int64_t(cycle);
    float u = lfoShape(shape, phase - cycle, randomAt(clock.seed, id, c), randomAt(clock.seed, id, c + 1));
    return shown(kType, knobs, knob::kBipolar) > 0.5f ? u : (u + 1) / 2;
}

/// Bars or steps that move on the song's grid, faded into the next over the last `smooth` share of each.
float stepped(double position, float thisValue, float nextValue, float smooth) {
    double frac = position - std::floor(position);
    if (smooth <= 0 || frac < 1 - smooth)
        return thisValue;
    float t = float((frac - (1 - smooth)) / smooth);
    t = (1 - std::cos(kPi * t)) / 2;
    return thisValue + (nextValue - thisValue) * t;
}

float envelope(const float* knobs, State& state, const Clock& clock) {
    constexpr ModulatorType kType = ModulatorType::Envelope;
    double attack = double(shown(kType, knobs, knob::kAttack)) / 1000;
    double decay = double(shown(kType, knobs, knob::kDecay)) / 1000;
    float sustain = shown(kType, knobs, knob::kSustain) / 100;
    double release = double(shown(kType, knobs, knob::kRelease)) / 1000;
    double t = state.envTime;
    float level;
    if (t < state.envHold) {
        if (t < attack)
            level = float(t / attack);
        else if (t < attack + decay)
            level = 1 - (1 - sustain) * float((t - attack) / decay);
        else
            level = sustain;
        state.releaseFrom = level;
    } else {
        double r = t - state.envHold;
        level = r < release ? state.releaseFrom * float(1 - r / release) : 0;
    }
    state.envTime += clock.seconds;
    state.envLevel = level;
    return level;
}

} // namespace

void trigger(State& state, double songTick, double lengthTicks, const NoteValues& values, const Clock& clock) {
    state.noteAt = songTick;
    state.noteNow = true;
    state.envTime = 0;
    state.envHold = std::isfinite(lengthTicks) ? lengthTicks / std::max(1.0, clock.ticksPerSecond) : 1e9;
    state.note = values;
}

float lfoShape(Shape shape, double frac, float thisCycle, float nextCycle) {
    switch (shape) {
    case Shape::Sine:
        return float(std::sin(2 * kPi * frac));
    case Shape::Triangle:
        // Starts at 0 rising, like the sine, so switching shapes keeps the timing.
        return float(frac < 0.25 ? 4 * frac : frac < 0.75 ? 2 - 4 * frac : 4 * frac - 4);
    case Shape::SawUp:
        return float(2 * frac - 1);
    case Shape::SawDown:
        return float(1 - 2 * frac);
    case Shape::Square:
        return frac < 0.5 ? 1.f : -1.f;
    case Shape::SampleHold:
        return thisCycle;
    case Shape::SmoothRandom: {
        float t = float((1 - std::cos(kPi * frac)) / 2);
        return thisCycle + (nextCycle - thisCycle) * t;
    }
    }
    return 0;
}

bool bipolar(ModulatorType type, const float* knobs) {
    switch (type) {
    case ModulatorType::Lfo:
        return shown(type, knobs, knob::kBipolar) > 0.5f;
    case ModulatorType::Steps:
        return shown(type, knobs, knob::kStepBipolar) > 0.5f;
    case ModulatorType::Random:
        return shown(type, knobs, knob::kRandomBipolar) > 0.5f;
    default:
        return false;
    }
}

float evaluate(ModulatorType type, uint64_t id, const float* knobs, State& state, const Clock& clock,
               const float* steps, int stepCount) {
    float out = 0;
    switch (type) {
    case ModulatorType::Lfo:
        out = lfo(id, knobs, state, clock);
        break;
    case ModulatorType::Macro:
        out = shown(type, knobs, knob::kValue) / 100;
        break;
    case ModulatorType::Envelope:
        out = envelope(knobs, state, clock);
        break;
    case ModulatorType::Steps: {
        // Drawn values, one per step of the chosen rate, on the song's grid.
        int count = std::max(1, int(shown(type, knobs, knob::kStepCount)));
        double rate = double(devices::rateTicks(int(shown(type, knobs, knob::kStepRate)), clock.timeSig));
        double position = clock.songTick / rate;
        int64_t step = int64_t(std::floor(position));
        auto valueAt = [&](int64_t k) {
            size_t i = size_t(((k % count) + count) % count);
            return steps && int(i) < stepCount ? steps[i] : 0.5f;
        };
        out = stepped(position, valueAt(step), valueAt(step + 1), shown(type, knobs, knob::kStepSmooth) / 100);
        if (shown(type, knobs, knob::kStepBipolar) > 0.5f)
            out = out * 2 - 1;
        break;
    }
    case ModulatorType::Random: {
        double rate = double(devices::rateTicks(int(shown(type, knobs, knob::kRandomRate)), clock.timeSig));
        double position = clock.songTick / rate;
        int64_t step = int64_t(std::floor(position));
        float a = (randomAt(clock.seed, id, step) + 1) / 2, b = (randomAt(clock.seed, id, step + 1) + 1) / 2;
        out = stepped(position, a, b, shown(type, knobs, knob::kRandomSmooth) / 100);
        if (shown(type, knobs, knob::kRandomBipolar) > 0.5f)
            out = out * 2 - 1;
        break;
    }
    case ModulatorType::Ramp: {
        double length = double(std::max(1, int(shown(type, knobs, knob::kBars)))) * double(ticksPerBar(clock.timeSig));
        double t = clock.songTick / length;
        t = shown(type, knobs, knob::kThen) > 0.5f ? t - std::floor(t) : std::min(1.0, t);
        // Curve bends it: above 0 it moves early and settles, below 0 it waits and then rushes.
        float curve = shown(type, knobs, knob::kCurve) / 100;
        float v = float(t);
        if (curve > 0)
            v = 1 - std::pow(1 - v, 1 + 3 * curve);
        else if (curve < 0)
            v = std::pow(v, 1 - 3 * curve);
        out = shown(type, knobs, knob::kDirection) > 0.5f ? 1 - v : v;
        break;
    }
    case ModulatorType::Note: {
        const NoteValues& n = state.note;
        float values[] = {n.velocity, n.pitch, n.release, n.timbre, n.pressure};
        float target = values[std::clamp(int(shown(type, knobs, knob::kFollows)), 0, 4)];
        double smoothMs = double(shown(type, knobs, knob::kSmooth));
        float alpha = float(1 - std::exp(-clock.seconds * 1000 / std::max(1.0, smoothMs)));
        out = state.out + (target - state.out) * alpha;
        break;
    }
    }
    state.noteNow = false;
    state.out = out;
    return out;
}

} // namespace tessera::mods
