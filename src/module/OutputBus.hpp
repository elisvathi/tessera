#pragma once

#include "../engine/AudioBus.hpp"
#include "../engine/TrackFrame.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace tessera {

/// The per-sample handoff from one Tessera to its output modules. Pure C++ so it can be tested without Rack.
///
/// Tessera writes all track frames for sample n into buffer n & 1, then publishes n. A reader processing
/// sample n reads buffer (n - 1) & 1: the complete frame from the previous sample. The two never touch the
/// same buffer within a sample, and Rack's per-sample thread barrier orders sample n before n + 1.
/// So every track has exactly one sample of latency, whatever the module order or thread count.
class OutputBus {
  public:
    // Tessera (audio thread)
    TrackFrame* beginWrite(int64_t frame) { return buffers[frame & 1].data(); }
    void publish(int64_t frame) { written[frame & 1].store(frame, std::memory_order_release); }

    // Readers (audio thread)
    /// The frame `track` wrote for sample `frame - 1` in `slot`. Returns nullptr when Tessera didn't produce
    /// that frame (bypassed, removed or stalled) or the slot now belongs to another track. Readers must then
    /// hold their outputs with gates low.
    const TrackFrame* read(int64_t frame, int slot, TrackId track) const {
        int64_t previous = frame - 1;
        if (slot < 0 || slot >= kMaxTracks || !track.valid())
            return nullptr;
        if (written[previous & 1].load(std::memory_order_acquire) != previous)
            return nullptr;
        const TrackFrame* f = &buffers[previous & 1][slot];
        return f->trackId == track.value ? f : nullptr;
    }

    // Audio OUT (§8.3): one stereo sample per track slot, the master's in kMasterSlot.
    static constexpr int kMasterSlot = kMaxTracks;
    AudioOutFrame* beginAudioWrite(int64_t frame) { return audio[size_t(frame & 1)].data(); }
    /// Called only on samples where the audio player ran: without audio, OUT reads silence.
    void publishAudio(int64_t frame) { audioWritten[frame & 1].store(frame, std::memory_order_release); }
    /// What `track` (or the master, by its ID, in kMasterSlot) sent to OUT at sample `frame - 1`; nullptr when
    /// nothing did.
    const AudioOutFrame* readAudio(int64_t frame, int slot, TrackId track) const {
        int64_t previous = frame - 1;
        if (slot < 0 || slot > kMasterSlot || !track.valid())
            return nullptr;
        if (audioWritten[previous & 1].load(std::memory_order_acquire) != previous)
            return nullptr;
        const AudioOutFrame* f = &audio[size_t(previous & 1)][size_t(slot)];
        return f->trackId == track.value ? f : nullptr;
    }

    /// The return bus travels with the output bus: same owner, same lifetime, same handoff to output modules.
    ReturnBus& returns() const { return returnBus; }

  private:
    std::array<std::array<TrackFrame, kMaxTracks>, 2> buffers{};
    std::atomic<int64_t> written[2] = {{INT64_MIN}, {INT64_MIN}}; // the sample each buffer last held
    std::array<std::array<AudioOutFrame, kMaxTracks + 1>, 2> audio{};
    std::atomic<int64_t> audioWritten[2] = {{INT64_MIN}, {INT64_MIN}};
    // Written by output modules through a const bus: the only part of it they write, each to its own slot.
    mutable ReturnBus returnBus;
};

} // namespace tessera
