#pragma once

#include "../model/Types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace tessera {

/// Turns the REC V/OCT and GATE inputs into notes, one note at a time per poly channel. A rising gate starts a
/// note at that moment's V/OCT. While the gate stays high, a move of V/OCT to another semitone ends the note and
/// starts one at the new pitch, the way a mono keyboard plays legato. Used on the audio thread, so it never
/// allocates.
class CvNoteInput {
  public:
    static constexpr float kGateLow = 0.1f;
    static constexpr float kGateHigh = 1.f;
    // V/OCT must pass the semitone's edge by this much, so noise on a pitch halfway between two keys can't flicker.
    static constexpr float kPitchHysteresis = 0.1f;
    // A new pitch must hold this long before it counts: one key's V/OCT lands at once, a slewed move does not.
    static constexpr double kSettleSeconds = 0.001;

    struct Event {
        uint8_t channel = 0;
        int8_t pitch = 60;
        float velocity = 0.8f;
        bool on = true;
    };
    /// At most a note off and a note on per channel and sample.
    using Events = std::array<Event, 2>;

    /// Sets how many samples a new pitch must hold. Call it when the sample rate changes.
    void setSampleRate(double sampleRate) { settle = std::max(1, int(std::lround(sampleRate * kSettleSeconds))); }

    /// Feeds one sample of one channel: its gate and V/OCT voltages and its velocity (0..1). Writes the notes that
    /// start or end into `out` in order and returns how many.
    int process(int channel, float gate, float voct, float velocity, Events& out) {
        Channel& c = channels[size_t(channel)];
        bool wasHigh = c.high;
        if (gate >= kGateHigh)
            c.high = true;
        else if (gate <= kGateLow)
            c.high = false;

        if (c.high != wasHigh) {
            c.pending = -1;
            if (c.high)
                c.pitch = pitchOf(voct);
            out[0] = {uint8_t(channel), c.pitch, c.high ? velocity : 0.5f, c.high};
            return 1;
        }
        if (!c.high)
            return 0;

        float semis = voct * 12.f + 60.f;
        if (std::fabs(semis - float(c.pitch)) <= 0.5f + kPitchHysteresis) {
            c.pending = -1;
            return 0;
        }
        int8_t next = pitchOf(voct);
        if (next != c.pending) {
            c.pending = next;
            c.pendingSamples = 0;
        }
        if (++c.pendingSamples < settle)
            return 0;
        out[0] = {uint8_t(channel), c.pitch, 0.5f, false};
        out[1] = {uint8_t(channel), next, velocity, true};
        c.pitch = next;
        c.pending = -1;
        return 2;
    }

    /// Whether `channel`'s gate is high.
    bool held(int channel) const { return channels[size_t(channel)].high; }

  private:
    struct Channel {
        bool high = false;
        int8_t pitch = 60;   // the sounding note's pitch while high
        int8_t pending = -1; // a new pitch waiting to settle; -1 = none
        int pendingSamples = 0;
    };

    /// The MIDI note nearest to a V/OCT voltage (0 V = C4 = 60).
    static int8_t pitchOf(float voct) { return int8_t(std::clamp(int(std::lround(voct * 12.f)) + 60, 0, 127)); }

    std::array<Channel, kMaxVoices> channels{};
    int settle = 48;
};

} // namespace tessera
