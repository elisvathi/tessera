#pragma once

#include "../model/Types.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace tessera {

/// One stereo sample of a track's sound before its fader (or the master mix before the master fader), for a Track
/// Out's OUT jacks (docs/design/AUDIO_MIXER.md §8.3). Volts.
struct AudioOutFrame {
    uint64_t trackId = 0;
    float l = 0, r = 0;
};

/// What one output module sent back for a sample: its IN after its own gains, and how it joins its track's chain.
struct ReturnFrame {
    float l = 0, r = 0;
    /// What is kept of the sound before this module: 1 when it adds (its OUT has no cable), less when it is an
    /// effect loop whose return replaces the sound by its Mix (docs/DECISIONS.md §153).
    float dry = 1;
    bool loop = false;
    int delay = 0; // an effect loop's round trip in samples, so the dry sound waits for the wet one
};

/// Where Volume and Pan lanes on a track's parts (a drum lane IN, a Track Out's IN, the clips) have them now, per
/// track slot. The audio player writes it once a block; output modules read their own part's entry and play it in
/// place of their saved level. Each value is its own atomic, so a reader never sees a torn number; a block's mix of
/// old and new entries lasts one block at most.
class PartLevels {
  public:
    static constexpr int kPerSlot = 12;

    /// Writer (audio player): entry `i` of `slot` says part `key` sits at `db` and `pan` (NAN: no lane on it).
    /// Key 0 clears it.
    void set(int slot, int i, uint64_t key, float db, float pan) {
        if (slot < 0 || slot >= kMaxTracks || i < 0 || i >= kPerSlot)
            return;
        Entry& e = entries[size_t(slot)][size_t(i)];
        e.db.store(db, std::memory_order_relaxed);
        e.pan.store(pan, std::memory_order_relaxed);
        e.key.store(key, std::memory_order_release);
    }
    /// Reader: where part `key` of the track in `slot` is now; false when no lane moves it. `db` and `pan` come back
    /// NAN for whichever has no lane.
    bool get(int slot, uint64_t key, float& db, float& pan) const {
        if (slot < 0 || slot >= kMaxTracks || key == 0)
            return false;
        for (const Entry& e : entries[size_t(slot)]) {
            uint64_t k = e.key.load(std::memory_order_acquire);
            if (k == 0)
                return false; // entries fill from the front: a slot without lanes costs one load
            if (k == key) {
                db = e.db.load(std::memory_order_relaxed);
                pan = e.pan.load(std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

  private:
    struct Entry {
        std::atomic<uint64_t> key{0};
        std::atomic<float> db{NAN}, pan{NAN};
    };
    std::array<std::array<Entry, kPerSlot>, kMaxTracks> entries{};
};

/// The way back: what output modules' IN jacks receive, to Tessera (§8.3 "the return bus"), and the way out to each
/// of them: the sound of its track up to its place in the track's chain, for its OUT. Pure C++.
///
/// Each module gets its own slot, handed out on the UI thread when its binding resolves, so modules sharing a track
/// never write the same place. A writer writes {sample, track, ...} into buffer n & 1 at sample n; the reader reads
/// buffer (n - 1) & 1 and uses a slot only when it holds sample n - 1 for the track it expects. So a module that is
/// missing, bypassed or removed simply drops out, whatever the module order.
class ReturnBus {
  public:
    static constexpr int kSlots = 128;

    // UI thread
    /// A free slot for one module, or -1 when all are taken.
    int claim() {
        for (int i = 0; i < kSlots; i++)
            if (!used[size_t(i)]) {
                used[size_t(i)] = true;
                return i;
            }
        return -1;
    }
    void release(int slot) {
        if (slot >= 0 && slot < kSlots)
            used[size_t(slot)] = false;
    }

    // Output module (audio thread), at sample `frame`
    void write(int64_t frame, int slot, TrackId track, float l, float r) { write(frame, slot, track, {l, r}); }
    void write(int64_t frame, int slot, TrackId track, const ReturnFrame& back) {
        if (slot < 0 || slot >= kSlots)
            return;
        Entry& e = buffers[size_t(frame & 1)][size_t(slot)];
        e.track = track.value;
        e.back = back;
        e.sample.store(frame, std::memory_order_release);
    }
    /// What Tessera sent to slot `slot`'s OUT at sample `frame - 1`. False, with silence, when nothing did.
    bool readOut(int64_t frame, int slot, TrackId track, float& l, float& r) const {
        l = r = 0;
        if (slot < 0 || slot >= kSlots)
            return false;
        const OutEntry& e = outs[size_t((frame - 1) & 1)][size_t(slot)];
        if (e.sample.load(std::memory_order_acquire) != frame - 1 || e.track != track.value)
            return false;
        l = e.l;
        r = e.r;
        return true;
    }

    // Tessera (audio thread), at sample `frame`
    /// What slot `slot` held for `track` at sample `frame - 1`. False, with silence, when nothing came: the module
    /// didn't run, the slot belongs to another track, or the signal wasn't a number (NaN or infinity).
    bool read(int64_t frame, int slot, TrackId track, float& l, float& r) const {
        l = r = 0;
        if (slot < 0 || slot >= kSlots)
            return false;
        const Entry& e = buffers[size_t((frame - 1) & 1)][size_t(slot)];
        if (e.sample.load(std::memory_order_acquire) != frame - 1 || e.track != track.value)
            return false;
        if (!std::isfinite(e.back.l) || !std::isfinite(e.back.r))
            return false;
        l = e.back.l;
        r = e.back.r;
        return true;
    }
    /// The same with how it joins the chain, for an effect loop.
    bool read(int64_t frame, int slot, TrackId track, ReturnFrame& back) const {
        back.l = back.r = 0;
        back.dry = 1;
        back.loop = false;
        if (slot < 0 || slot >= kSlots)
            return false;
        const Entry& e = buffers[size_t((frame - 1) & 1)][size_t(slot)];
        if (e.sample.load(std::memory_order_acquire) != frame - 1 || e.track != track.value)
            return false;
        if (!std::isfinite(e.back.l) || !std::isfinite(e.back.r) || !std::isfinite(e.back.dry))
            return false;
        back = e.back;
        return true;
    }
    /// Sends `l`, `r` to slot `slot`'s OUT.
    void writeOut(int64_t frame, int slot, TrackId track, float l, float r) {
        if (slot < 0 || slot >= kSlots)
            return;
        OutEntry& e = outs[size_t(frame & 1)][size_t(slot)];
        e.track = track.value;
        e.l = l;
        e.r = r;
        e.sample.store(frame, std::memory_order_release);
    }

  private:
    struct Entry {
        std::atomic<int64_t> sample{INT64_MIN};
        uint64_t track = 0;
        ReturnFrame back;
    };
    struct OutEntry {
        std::atomic<int64_t> sample{INT64_MIN};
        uint64_t track = 0;
        float l = 0, r = 0;
    };
    std::array<std::array<Entry, kSlots>, 2> buffers{};
    std::array<std::array<OutEntry, kSlots>, 2> outs{};
    std::array<bool, kSlots> used{};
};

} // namespace tessera
