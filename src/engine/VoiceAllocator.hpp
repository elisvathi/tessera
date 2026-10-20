#pragma once

#include "../model/Types.hpp"

#include <algorithm>
#include <array>
#include <climits>

namespace tessera {

/// Assigns notes to a track's poly channels: round-robin, stealing the oldest note when all voices are busy.
/// When a voice is reused while its gate is high (or was high only moments ago), the gate is held low for a
/// short gap first so downstream envelopes see a fresh rising edge.
///
/// Per sample, call order is: release / noteOn, read gate(), then advance().
class VoiceAllocator {
  public:
    struct Voice {
        bool held = false; // a note owns this voice
        int8_t pitch = 60;
        float velocity = 0;
        float releaseVelocity = 0;
        double endTick = 0;       // when the owning note releases, on the engine's clock (infinite while open)
        uint32_t id = 0;          // the note's chain ID, so an open note can be ended later; 0 = none
        uint64_t startOrder = 0;  // larger = newer
        int64_t gateFrom = 0;     // the sample from which the gate may rise
        int64_t lowSince = -kFar; // the first sample of the gate's current low stretch
    };

    void setPolyphony(int n) {
        if (std::clamp(n, 1, kMaxVoices) != poly)
            changes++;
        poly = std::clamp(n, 1, kMaxVoices);
        for (int v = poly; v < kMaxVoices; v++)
            release(v);
        next %= poly;
    }
    int polyphony() const { return poly; }

    void setRetriggerGap(int samples) { gap = std::max(1, samples); }

    /// Returns the voice index used.
    int noteOn(int8_t pitch, float velocity, float releaseVelocity, double endTick, uint32_t id = 0) {
        int v = pickVoice();
        Voice& voice = voices[v];
        if (gate(v)) {
            // Reused while sounding: the gate drops now and rises again after the gap.
            voice.lowSince = now;
            voice.gateFrom = now + gap;
        } else {
            // Low for less than the gap: it stays low for the rest of it.
            voice.gateFrom = std::max(now, voice.lowSince + gap);
        }
        voice.held = true;
        voice.pitch = pitch;
        voice.velocity = velocity;
        voice.releaseVelocity = releaseVelocity;
        voice.endTick = endTick;
        voice.id = id;
        voice.startOrder = ++order;
        next = (v + 1) % poly;
        nextEnd = std::min(nextEnd, endTick);
        if (voice.gateFrom > now)
            nextRise = std::min(nextRise, voice.gateFrom);
        changes++;
        return v;
    }

    /// Releases every voice whose note ends before `tick`.
    void releaseEndedBefore(double tick) {
        // Called every sample: nothing to do until the earliest note end has passed.
        if (tick <= nextEnd)
            return;
        nextEnd = kNoEnd;
        for (int v = 0; v < poly; v++) {
            if (!voices[v].held)
                continue;
            if (voices[v].endTick < tick)
                release(v);
            else
                nextEnd = std::min(nextEnd, voices[v].endTick);
        }
    }

    void release(int v) {
        Voice& voice = voices[v];
        if (gate(voice))
            voice.lowSince = now;
        if (voice.held)
            changes++;
        voice.held = false;
    }

    void releaseAll() {
        for (int v = 0; v < kMaxVoices; v++)
            release(v);
        nextEnd = kNoEnd;
    }

    /// Once per sample, after the gates were read.
    void advance() {
        now++;
        if (now < nextRise)
            return;
        // A gate held low for a retrigger gap rises now: the outputs change.
        changes++;
        nextRise = kNever;
        for (int v = 0; v < poly; v++)
            if (voices[v].held && voices[v].gateFrom > now)
                nextRise = std::min(nextRise, voices[v].gateFrom);
    }
    /// Counts `samples` advances at once, for a stretch the engine skipped because quietFor() said nothing rises.
    void skip(int64_t samples) { now += samples; }
    /// No delayed gate rises within the next `samples` advances.
    bool quietFor(int64_t samples) const { return nextRise > now + samples; }
    /// Counts every change to what the voices output, so unchanged outputs needn't be written again.
    uint64_t changeCount() const { return changes; }

    /// The held voice playing note `id`, or -1.
    int find(uint32_t id) const {
        for (int v = 0; v < poly; v++)
            if (voices[v].held && voices[v].id == id)
                return v;
        return -1;
    }

    bool gate(int v) const { return gate(voices[v]); }
    /// No held voice ends at or before this tick.
    double earliestEnd() const { return nextEnd; }
    /// Some held voice has an end tick (live notes held by a key have none).
    bool anyEnding() const { return nextEnd < kNoEnd; }
    bool anyGate() const {
        for (int v = 0; v < poly; v++)
            if (gate(v))
                return true;
        return false;
    }
    const Voice& voice(int v) const { return voices[v]; }

  private:
    bool gate(const Voice& voice) const { return voice.held && now >= voice.gateFrom; }

    int pickVoice() const {
        for (int i = 0; i < poly; i++) {
            int v = (next + i) % poly;
            if (!voices[v].held)
                return v;
        }
        int oldest = 0;
        for (int v = 1; v < poly; v++)
            if (voices[v].startOrder < voices[oldest].startOrder)
                oldest = v;
        return oldest;
    }

    static constexpr int64_t kFar = int64_t(1) << 40;
    static constexpr double kNoEnd = 1e300;

    static constexpr int64_t kNever = INT64_MAX;
    // Read every sample, so they come first and share a cache line; the voices follow.
    int64_t now = 0;           // samples advanced
    int64_t nextRise = kNever; // the next sample a delayed gate rises
    uint64_t changes = 0;
    double nextEnd = kNoEnd; // no held voice ends before this
    int poly = 1;
    int next = 0;
    int gap = 48;
    uint64_t order = 0;
    std::array<Voice, kMaxVoices> voices{};
};

} // namespace tessera
