#pragma once

#include "../model/Song.hpp"
#include "AudioPlayback.hpp"

#include <memory>
#include <vector>

namespace tessera {

/// A stretched render of a whole source: its length factor (render frames per prepared frame), its pitch and its
/// sound type. Renders are made by the audio store's workers; this names one (docs/design/AUDIO_MIXER.md §5.2).
struct RenderKey {
    AudioSourceId source;
    double factor = 1;    // output length / input length
    double semitones = 0; // the clip's semitones and cents together
    SoundType sound = SoundType::FullMix;
    bool reverse = false; // made from the reversed source
    // A warped clip's render (§3.5) is made straight: each stretch between markers at its own ratio, so render frame
    // (startFrame + tick * framesPerTick) * factor plays what the clip plays at that tick. Prepared frames.
    std::vector<std::pair<double, double>> warps; // (clip tick, prepared frame)
    double startFrame = 0, framesPerTick = 0;

    bool operator==(const RenderKey& o) const {
        return source == o.source && factor == o.factor && semitones == o.semitones && sound == o.sound &&
               reverse == o.reverse && warps == o.warps && startFrame == o.startFrame &&
               framesPerTick == o.framesPerTick;
    }
};

/// Where the snapshot builder gets samples from. The audio store implements it; tests use a fake.
class AudioBuffers {
  public:
    virtual ~AudioBuffers() = default;
    /// The source at the engine rate, back to front when `reversed`; null while it decodes, or when its file is
    /// missing. A reversed copy is made on first asking, so it may be null for a moment after the clip turns round.
    virtual std::shared_ptr<const AudioBuffer> prepared(AudioSourceId source, bool reversed = false) = 0;
    /// The render for `key` if it is ready, else the ready render of the same source, pitch, sound and direction
    /// whose factor is nearest, and its key in `found`; null when there is none.
    virtual std::shared_ptr<const AudioBuffer> render(const RenderKey& key, RenderKey& found) = 0;
};

/// The source rate's frames at the engine rate: how prepared buffers are measured.
inline double engineFrames(double frames, double sourceRate, double engineRate) {
    return sourceRate > 0 ? frames * engineRate / sourceRate : frames;
}

/// What the rack adds to the mix, from the output modules on the UI thread (§8.3): which return slots feed which
/// track, and each track's output bus slot. A return for the master uses the master's ID.
struct MixPlan {
    struct Return {
        TrackId track;
        int slot = -1;
        bool source = false; // the track's own sound (Drum Out's lane INs): it goes to OUT; a return doesn't
    };
    std::vector<Return> returns;
    std::vector<std::pair<TrackId, int>> busSlots;
    std::vector<TrackId> inputSolo; // tracks where an output module's input is soloed, so their clips aren't heard

    bool feeds(TrackId track) const {
        for (const Return& r : returns)
            if (r.track == track)
                return true;
        return false;
    }
};

/// Whether `track` makes sound and so has a mixer strip and plays through the audio player: an audio track, or any
/// track whose output module brings audio back on IN (§7.9).
inline bool makesSound(const Track& track, const MixPlan& plan) {
    return track.type == TrackType::Audio || plan.feeds(track.id);
}

/// What the builder found missing: renders the store should make for the tempo now.
struct AudioBuild {
    std::unique_ptr<AudioSnapshot> snapshot;
    std::vector<RenderKey> wanted; // renders the clips want at this tempo that aren't ready yet
};

/// Builds the audio snapshot for `song` at `tempo` on the UI thread: every audio track, its clips, fades and
/// crossfade overlaps, and the mixer settings. Cheap (a few audio tracks), so it is rebuilt whole when the song,
/// a buffer or the tempo changes. `keep` holds buffers that must stay alive for fades already under way.
AudioBuild buildAudioSnapshot(const Song& song, AudioBuffers& buffers, double engineRate, double tempo,
                              const std::vector<std::shared_ptr<const AudioBuffer>>& keep = {},
                              const MixPlan& plan = {});

/// The render a Stretch clip wants at `tempo`: none (factor 1, no pitch) when the prepared buffer already plays
/// it right.
bool wantsRender(const AudioContent& clip, double tempo, RenderKey& key);

} // namespace tessera
