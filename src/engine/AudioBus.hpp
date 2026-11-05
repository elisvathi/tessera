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

/// The way back: what output modules' IN jacks receive, to Tessera (§8.3 "the return bus"). Pure C++.
///
/// Each module gets its own slot, handed out on the UI thread when its binding resolves, so modules sharing a track
/// never write the same place. A module writes {sample, track, L, R} into buffer n & 1 at sample n; Tessera reads
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
    void write(int64_t frame, int slot, TrackId track, float l, float r) {
        if (slot < 0 || slot >= kSlots)
            return;
        Entry& e = buffers[size_t(frame & 1)][size_t(slot)];
        e.track = track.value;
        e.l = l;
        e.r = r;
        e.sample.store(frame, std::memory_order_release);
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
        if (!std::isfinite(e.l) || !std::isfinite(e.r))
            return false;
        l = e.l;
        r = e.r;
        return true;
    }

  private:
    struct Entry {
        std::atomic<int64_t> sample{INT64_MIN};
        uint64_t track = 0;
        float l = 0, r = 0;
    };
    std::array<std::array<Entry, kSlots>, 2> buffers{};
    std::array<bool, kSlots> used{};
};

} // namespace tessera
