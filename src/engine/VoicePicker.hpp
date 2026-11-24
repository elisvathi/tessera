#pragma once

#include "TrackFrame.hpp"

#include <algorithm>
#include <array>

namespace tessera {

/// Which of a track's voices a Track Out sends on its poly channels (at most kMaxVoices). A track without playheads
/// goes out as it is. With playheads (each channel with voices of its own), only the voices a channel has needed so
/// far are sent, so a one-note bass sends one: an output set to one channel sends that channel's, and one set to All
/// packs them into its channels: a voice goes back to the channel it used last when that is free (so a patch hears
/// the same playhead in the same place), else to the first free one, and when all are taken it may take one from a
/// voice later in the track's order (the playheads after it), never from one before. Kept per output, on the audio
/// thread.
class VoicePicker {
  public:
    /// Fills `out` with the frame voice for each output channel (-1: none) and returns how many channels to send.
    int pick(const TrackFrame& frame, int channel, std::array<int8_t, kMaxVoices>& out) {
        int voices = frame.channels;
        out.fill(-1);
        if (!frame.byChannel) {
            int n = 0;
            for (; n < kMaxVoices && n < voices; n++)
                out[size_t(n)] = int8_t(n);
            return n;
        }
        if (channel > 0) {
            int n = 0;
            for (int v = 0; v < voices && n < kMaxVoices; v++)
                if (frame.part[v] == channel && frame.used[v])
                    out[size_t(n++)] = int8_t(v);
            return std::max(n, 1);
        }
        int count = 0;
        for (int v = 0; v < voices; v++)
            count += frame.used[v] ? 1 : 0;
        count = std::clamp(count, 1, kMaxVoices);
        pack(frame, count);
        out = slotVoice;
        return count;
    }

  private:
    /// Gives every sounding voice one of the first `count` channels, in the track's order, keeping the ones they had.
    void pack(const TrackFrame& frame, int count) {
        auto sounding = [&](int v) { return v >= 0 && frame.gate[v] > 0; };
        // A channel past the count (it shrank: the song stopped) is free again.
        for (int s = count; s < kMaxVoices; s++)
            slotVoice[size_t(s)] = -1;
        for (int v = 0; v < frame.channels; v++) {
            if (!sounding(v) || (lastSlot[size_t(v)] >= 0 && lastSlot[size_t(v)] < count &&
                                 slotVoice[size_t(lastSlot[size_t(v)])] == v))
                continue;
            int slot = -1;
            int last = lastSlot[size_t(v)];
            if (last >= 0 && last < count && !sounding(slotVoice[size_t(last)]))
                slot = last;
            for (int s = 0; slot < 0 && s < count; s++)
                if (!sounding(slotVoice[size_t(s)]))
                    slot = s;
            if (slot < 0) {
                // All taken: the one held by the voice latest in the order, if it comes after this one.
                int latest = -1;
                for (int s = 0; s < count; s++)
                    if (slotVoice[size_t(s)] > v && (latest < 0 || slotVoice[size_t(s)] > slotVoice[size_t(latest)]))
                        latest = s;
                slot = latest;
            }
            if (slot < 0)
                continue; // everything ahead of it in the order is sounding: it isn't sent
            slotVoice[size_t(slot)] = int8_t(v);
            lastSlot[size_t(v)] = int8_t(slot);
        }
    }

    std::array<int8_t, kMaxVoices> slotVoice = filled<kMaxVoices>();
    std::array<int8_t, kMaxTrackVoices> lastSlot = filled<kMaxTrackVoices>();

    template <size_t N>
    static std::array<int8_t, N> filled() {
        std::array<int8_t, N> a;
        a.fill(-1);
        return a;
    }
};

} // namespace tessera
