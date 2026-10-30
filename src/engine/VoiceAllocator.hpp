#pragma once

#include "../model/Types.hpp"

#include <algorithm>
#include <array>
#include <climits>

namespace tessera {

/// Assigns notes to a track's voices: round-robin, stealing the oldest note when all voices are busy. The voices
/// come in blocks, one per channel (a Playheads device gives each playhead channel its own, in playhead order), and a
/// note only ever takes a voice in its channel's block, so one busy channel can't cut another's notes. A track
/// without playheads has a single block. When a voice is reused while its gate is high (or was high only moments
/// ago), the gate is held low for a short gap first so downstream envelopes see a fresh rising edge.
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

    /// A channel's voices: `size` of them from `start`. Part 0 takes the notes of channels with no block of their own.
    struct Block {
        uint8_t part;
        uint8_t start, size;
    };
    static constexpr int kMaxBlocks = 17;

    /// One block of `n` voices for every note.
    void setPolyphony(int n) {
        Block one{0, 0, uint8_t(std::clamp(n, 1, kMaxVoices))};
        setBlocks(&one, 1);
    }
    /// The voices, as blocks laid end to end (at most kMaxTrackVoices in all). Voices past the end are released.
    void setBlocks(const Block* list, int count) {
        count = std::clamp(count, 1, kMaxBlocks);
        int total = 0;
        bool same = count == blockCount;
        for (int b = 0; b < count; b++) {
            Block block = list[b];
            block.start = uint8_t(std::min(total, kMaxTrackVoices - 1));
            block.size = uint8_t(std::clamp(int(block.size), 1, std::min(kMaxVoices, kMaxTrackVoices - block.start)));
            same =
                same && block.part == blocks[b].part && block.start == blocks[b].start && block.size == blocks[b].size;
            blocks[b] = block;
            total = block.start + block.size;
        }
        if (same)
            return;
        changes++;
        blockCount = count;
        poly = total;
        for (int v = poly; v < kMaxTrackVoices; v++)
            release(v);
        for (int b = 0; b < blockCount; b++)
            nexts[b] = 0;
    }
    /// All the voices, over every block.
    int polyphony() const { return poly; }
    /// The channel whose block voice `v` is in; 0 for the shared block (its notes keep their own channels).
    uint8_t partOf(int v) const {
        for (int b = 0; b < blockCount; b++)
            if (v >= blocks[b].start && v < blocks[b].start + blocks[b].size)
                return blocks[b].part;
        return 0;
    }

    void setRetriggerGap(int samples) { gap = std::max(1, samples); }

    /// Returns the voice index used.
    int noteOn(int8_t pitch, float velocity, float releaseVelocity, double endTick, uint32_t id = 0, uint8_t part = 0) {
        int b = blockFor(part);
        int v = pickVoice(b);
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
        nexts[b] = (v - blocks[b].start + 1) % blocks[b].size;
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
        for (int v = 0; v < kMaxTrackVoices; v++)
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

    /// The block a note of channel `part` plays in: its own, else the shared one (part 0), else the first.
    int blockFor(uint8_t part) const {
        int shared = 0;
        for (int b = 0; b < blockCount; b++) {
            if (blocks[b].part == part)
                return b;
            if (blocks[b].part == 0)
                shared = b;
        }
        return shared;
    }

    /// A free voice in block `b`, round-robin from where the last note went; else its oldest.
    int pickVoice(int b) const {
        const Block& block = blocks[b];
        for (int i = 0; i < block.size; i++) {
            int v = block.start + (nexts[b] + i) % block.size;
            if (!voices[v].held)
                return v;
        }
        int oldest = block.start;
        for (int v = block.start + 1; v < block.start + block.size; v++)
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
    int gap = 48;
    uint64_t order = 0;
    std::array<Block, kMaxBlocks> blocks{{{0, 0, 1}}};
    std::array<uint8_t, kMaxBlocks> nexts{};
    int blockCount = 1;
    std::array<Voice, kMaxTrackVoices> voices{};
};

} // namespace tessera
