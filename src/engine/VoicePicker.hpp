#pragma once

#include "TrackFrame.hpp"

#include <array>

namespace tessera {

/// Which of a track's voices a Track Out sends on its poly channels (at most kMaxVoices). A track with 16 voices or
/// fewer goes out as it is. With more (playheads, each channel with voices of its own), an output set to one channel
/// sends that channel's voices, and one set to All packs the sounding voices into its 16 channels: a voice goes back
/// to the channel it used last when that is free (so a patch hears the same playhead in the same place), else to the
/// first free one, and when all are taken it may take one from a voice later in the track's order (the playheads
/// after it), never from one before. Kept per output, on the audio thread.
class VoicePicker {
  public:
    /// Fills `out` with the frame voice for each output channel (-1: none) and returns how many channels to send.
    int pick(const TrackFrame& frame, int channel, std::array<int8_t, kMaxVoices>& out) {
        int voices = frame.channels;
        out.fill(-1);
        if (voices <= kMaxVoices) {
            for (int v = 0; v < voices; v++)
                out[size_t(v)] = int8_t(v);
            return voices;
        }
        if (channel > 0) {
            int n = 0;
            for (int v = 0; v < voices && n < kMaxVoices; v++)
                if (frame.part[v] == channel)
                    out[size_t(n++)] = int8_t(v);
            return n;
        }
        pack(frame);
        out = slotVoice;
        return kMaxVoices;
    }

  private:
    /// Gives every sounding voice a channel, in the track's order, keeping the ones they had.
    void pack(const TrackFrame& frame) {
        auto sounding = [&](int v) { return v >= 0 && frame.gate[v] > 0; };
        for (int v = 0; v < frame.channels; v++) {
            if (!sounding(v) || (lastSlot[size_t(v)] >= 0 && slotVoice[size_t(lastSlot[size_t(v)])] == v))
                continue;
            int slot = -1;
            int last = lastSlot[size_t(v)];
            if (last >= 0 && !sounding(slotVoice[size_t(last)]))
                slot = last;
            for (int s = 0; slot < 0 && s < kMaxVoices; s++)
                if (!sounding(slotVoice[size_t(s)]))
                    slot = s;
            if (slot < 0) {
                // All taken: the one held by the voice latest in the order, if it comes after this one.
                int latest = -1;
                for (int s = 0; s < kMaxVoices; s++)
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
