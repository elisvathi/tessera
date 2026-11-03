#pragma once

#include "../model/AudioSource.hpp"
#include "../model/Types.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

/// What the audio thread needs to play audio clips and mix them: immutable, built on the UI thread, swapped in
/// through an atomic pointer like the note snapshot (docs/design/AUDIO_MIXER.md §8.3).
namespace tessera {

/// Samples ready to play, at the engine's sample rate: a source resampled from its file, or a stretched render
/// of one. Never changed once built; freed on the UI thread only.
struct AudioBuffer {
    int channels = 0; // 1 or 2; a mono buffer plays the same samples on both sides
    int64_t frames = 0;
    double rate = 0; // its sample rate; 0 = the engine's. After a rate change, old buffers play on until new ones are ready
    std::array<std::vector<float>, 2> data;

    /// Channel `c`'s samples; the first channel again for a mono buffer.
    const float* channel(int c) const { return data[size_t(c < channels ? c : 0)].data(); }
};

/// One audio clip as the engine plays it.
struct AudioClipPlay {
    uint64_t clip = 0; // ClipId value
    const AudioBuffer* prepared = nullptr; // the source at the engine rate; null while it decodes, or when missing
    const AudioBuffer* render = nullptr;   // stretched for `renderTempo`, or null
    double renderTempo = 0;
    double renderStart = 0;  // render frame of the clip window's start (clip tick 0)
    double framesPerTick = 0; // prepared frames per clip tick, at the clip tempo
    double startFrame = 0;    // prepared frame at clip tick 0
    double clipTempo = 120;
    TempoFollow follow = TempoFollow::Stretch;
    float gain = 1; // clip gain times the source's volts per full scale: samples become volts
    bool looping = false;
    double loopStart = 0, loopEnd = 0; // clip ticks
    double length = 0;                 // clip ticks: where a clip without a loop ends
    // The file ends at (or just after) the loop end, so a seam has nothing to crossfade with: it dips instead.
    bool seamDip = false;

    /// Song ticks one clip tick lasts at `songTempo` (Off clips play at their own speed).
    double songTicksPerTick(double songTempo) const {
        return follow == TempoFollow::Off && clipTempo > 0 ? songTempo / clipTempo : 1.0;
    }
};

/// A timeline instance of an audio clip, in song ticks, with its fades and the overlaps it crossfades over.
struct AudioInstancePlay {
    uint64_t id = 0; // ArrangementId value
    int clip = -1;   // index into AudioSnapshot::clips
    double position = 0;
    double length = 0;  // clip ticks (song ticks for every clip but Off ones, §5.4)
    double offset = 0;  // clip tick at the start
    Fade fadeIn, fadeOut;
    // Song ticks shared with the instance before and after it: the overlap is the crossfade.
    double overlapIn = 0, overlapOut = 0;
};

/// One audio track's clips and mixer strip.
struct AudioTrackPlay {
    TrackId id;
    std::vector<AudioInstancePlay> instances; // sorted by position
    std::vector<int> slots;                    // clip index per launcher slot, -1 when empty or not audio
    float volume = 1;                          // the fader as a gain
    float pan = 0;
    bool audible = true; // mute and solo already applied
};

/// Everything the audio thread plays, for one Tessera.
struct AudioSnapshot {
    uint64_t generation = 0;
    std::vector<AudioClipPlay> clips;
    std::vector<AudioTrackPlay> tracks;
    float masterVolume = 1;
    uint64_t masterId = 0; // the master's TrackId value
    /// Every buffer the clips point at, and recently dropped ones kept a little longer so a fade-out that started
    /// before this snapshot can finish reading them.
    std::vector<std::shared_ptr<const AudioBuffer>> buffers;

    /// Whether `b` is one of this snapshot's buffers.
    bool holds(const AudioBuffer* b) const {
        for (const auto& p : buffers)
            if (p.get() == b)
                return true;
        return false;
    }
};

} // namespace tessera
