#pragma once

#include "Automation.hpp"
#include "SoundType.hpp"
#include "Types.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

/// Audio in the song: the files (sources), what an audio clip plays of one, fades, and the mixer settings of a
/// track and of the master (docs/design/AUDIO_MIXER.md §4).
namespace tessera {

/// The voltage a full-scale sample of an imported file plays at. 5 V reads -6 dB on the meters, level with rack
/// oscillators. Still open: the user decides it by ear before release (DECISIONS §100).
constexpr float kImportVolts = 5;
/// Full scale of the sound card: 10 V is 0 dB on every meter, on MAIN and in exported files.
constexpr float kFullScaleVolts = 10;
/// At or below this a fader or gain is silent (shown as "-inf").
constexpr float kSilentDb = -100;
constexpr float kMaxFaderDb = 6;
constexpr float kMaxClipGainDb = 24;

/// Gain factor for a level in dB; 0 at or below kSilentDb.
inline float dbToGain(float db) { return db <= kSilentDb ? 0.f : std::pow(10.f, db / 20); }
/// The level in dB of a gain factor; kSilentDb for silence.
inline float gainToDb(float gain) {
    return gain <= 0 ? kSilentDb : std::fmax(kSilentDb, 20 * std::log10(gain));
}

/// One file in the patch. Never changed after import or recording: edits make new sources.
struct AudioSource {
    AudioSourceId id;
    std::string name;         // shown to the user, from the file name
    std::string file;         // path inside the module's patch storage, e.g. "audio/a1f3-drumloop.wav"
    uint64_t contentHash = 0; // finds the same file again after a paste, a duplicate or a moved patch
    int channels = 2;         // 1 or 2 (more are mixed down on import)
    int fileChannels = 2;     // what the file had, for the "6→2" mark
    double sampleRate = 48000;
    int64_t frames = 0;
    double tempo = 0;          // what the file was played at: detected, from the file, or set; 0 = none
    float volts = kImportVolts; // the voltage a full-scale sample plays at

    double seconds() const { return sampleRate > 0 ? double(frames) / sampleRate : 0; }

    bool operator==(const AudioSource& o) const {
        return id == o.id && name == o.name && file == o.file && contentHash == o.contentHash &&
               channels == o.channels && fileChannels == o.fileChannels && sampleRate == o.sampleRate &&
               frames == o.frames && tempo == o.tempo && volts == o.volts;
    }
};

/// How a clip meets the song tempo (§5.1).
enum class TempoFollow : uint8_t {
    Stretch, // same pitch, follows the tempo: loops and music
    Repitch, // faster and higher, like tape
    Off,     // its own speed and pitch: one-shots, speech
};

constexpr double kMinClipTempo = 20;
constexpr double kMaxClipTempo = 999;

/// A warp marker (§3.5): pins a frame of the file to a clip tick, so the audio between markers stretches to fit.
struct WarpMarker {
    double tick = 0;   // clip ticks
    int64_t frame = 0; // the source's own frames

    bool operator==(const WarpMarker& o) const { return tick == o.tick && frame == o.frame; }
};

/// Another take recorded over an audio clip (docs/design/AUDIO_EDITING.md §4): its file, from the frame that plays at
/// clip tick 0, its own warp markers, and a name when the user gave it one.
struct AudioTake {
    AudioSourceId source;
    int64_t sourceStart = 0;
    std::string name;
    std::vector<WarpMarker> warps;

    bool operator==(const AudioTake& o) const {
        return source == o.source && sourceStart == o.sourceStart && name == o.name && warps == o.warps;
    }
};

/// A fade at one end of a timeline instance or of a part of a clip. `curve` bends it: 0 is straight (equal power in a crossfade).
struct Fade {
    Tick length = 0;
    float curve = 0; // -1..1

    bool operator==(const Fade& o) const { return length == o.length && curve == o.curve; }
    bool operator!=(const Fade& o) const { return !(*this == o); }
};

/// One part of a clip's edit list: which stretch of a take plays when, inside the clip. Files never change, so a cut,
/// a move or a fade is only a change to this list (docs/design/AUDIO_EDITING.md §2).
struct AudioRegion {
    AudioRegionId id;
    Tick start = 0, length = 0; // where it sits, in clip ticks
    int take = -1;              // which take it plays: -1 the clip's own, k its takes[k]
    // The frame of the take that plays at `start`. A reversed part plays backwards from its span's end, so this is
    // the lowest frame it plays.
    int64_t sourceFrame = 0;
    float gainDb = 0; // added to the clip's gain
    bool muted = false, reverse = false;
    Fade fadeIn, fadeOut;

    Tick end() const { return start + length; }

    bool operator==(const AudioRegion& o) const {
        return id == o.id && start == o.start && length == o.length && take == o.take && sourceFrame == o.sourceFrame &&
               gainDb == o.gainDb && muted == o.muted && reverse == o.reverse && fadeIn == o.fadeIn &&
               fadeOut == o.fadeOut;
    }
};

/// What an audio clip plays of its source.
struct AudioContent {
    AudioSourceId source;
    int64_t sourceStart = 0; // the source frame at clip tick 0
    TempoFollow follow = TempoFollow::Stretch;
    SoundType sound = SoundType::FullMix;
    double sourceTempo = 120; // the clip tempo: the one place speed is set
    float gainDb = 0;         // kSilentDb..kMaxClipGainDb
    int8_t semitones = 0;     // -24..24
    float cents = 0;          // -50..50
    bool reverse = false;     // plays the clip's window of the source backwards
    // Sorted by tick, frames rising too. With markers, they set where the file plays (sourceStart only matters
    // without them); past the first and last, the file plays at the clip tempo. Ignored on a reversed clip.
    std::vector<WarpMarker> warps;
    // Takes recorded over this clip besides its own (take 1). Each part of `regions` says which one it plays.
    std::vector<AudioTake> takes;
    // The clip's edit list, sorted by start. Empty means one part over the whole clip, as every unedited clip has; with
    // parts here, `reverse` is off and each part says for itself.
    std::vector<AudioRegion> regions;

    /// Pitch factor of the semitones and cents (2 = an octave up).
    double pitchFactor() const { return std::exp2((semitones + double(cents) / 100) / 12); }

    bool operator==(const AudioContent& o) const {
        return source == o.source && sourceStart == o.sourceStart && follow == o.follow && sound == o.sound &&
               sourceTempo == o.sourceTempo && gainDb == o.gainDb && semitones == o.semitones && cents == o.cents &&
               reverse == o.reverse && warps == o.warps && takes == o.takes && regions == o.regions;
    }
};

/// Where 0 dB sits on a fader's travel (0..1).
constexpr float kZeroDbFaderPos = 0.75f;

/// A fader's position (0..1) for a level: below 0 dB the travel follows the fourth root of the gain, so -12 dB sits
/// about half way and the useful range gets most of the fader; above it, up to kMaxFaderDb in the last quarter. The
/// mixer, the track headers and fader automation lanes share it.
inline float faderPosOfDb(float db) {
    if (db <= kSilentDb)
        return 0;
    if (db >= 0)
        return kZeroDbFaderPos + (1 - kZeroDbFaderPos) * std::min(db, kMaxFaderDb) / kMaxFaderDb;
    return kZeroDbFaderPos * std::pow(dbToGain(db), 0.25f);
}

/// The level at a fader position (0..1); the inverse of faderPosOfDb().
inline float faderDbOfPos(float pos) {
    if (pos <= 0.001f)
        return kSilentDb;
    if (pos >= kZeroDbFaderPos)
        return (pos - kZeroDbFaderPos) / (1 - kZeroDbFaderPos) * kMaxFaderDb;
    return gainToDb(std::pow(pos / kZeroDbFaderPos, 4.f));
}

/// Seconds of one tick at `bpm`.
inline double secondsPerTick(double bpm) { return 60.0 / (bpm * double(kPpq)); }

/// Source frames per clip tick: one tick at the clip tempo, in the source's own sample rate.
inline double framesPerTick(const AudioContent& a, double sampleRate) {
    return secondsPerTick(a.sourceTempo) * sampleRate;
}

/// What take `take` of a clip plays: -1 (or out of range) the clip's own, k its takes[k], with that take's markers.
inline AudioContent takeContent(const AudioContent& a, int take) {
    if (take < 0 || take >= int(a.takes.size()))
        return a;
    AudioContent t = a;
    t.source = a.takes[size_t(take)].source;
    t.sourceStart = a.takes[size_t(take)].sourceStart;
    t.warps = a.takes[size_t(take)].warps;
    return t;
}

/// The source frame clip tick `tick` plays: straight from `sourceStart` at `perTick` frames a tick, or through the
/// warp markers, at that same rate past the first and the last.
inline double warpedFrame(const std::vector<WarpMarker>& w, int64_t sourceStart, double perTick, double tick) {
    if (w.empty())
        return double(sourceStart) + tick * perTick;
    if (tick <= w.front().tick)
        return double(w.front().frame) + (tick - w.front().tick) * perTick;
    if (tick >= w.back().tick)
        return double(w.back().frame) + (tick - w.back().tick) * perTick;
    size_t i = 1;
    while (w[i].tick < tick)
        i++;
    const WarpMarker& a0 = w[i - 1];
    const WarpMarker& a1 = w[i];
    double span = a1.tick - a0.tick;
    return span > 0 ? double(a0.frame) + (tick - a0.tick) / span * double(a1.frame - a0.frame) : double(a1.frame);
}

/// The clip tick at which source frame `frame` plays: the inverse of warpedFrame().
inline double warpedTick(const std::vector<WarpMarker>& w, int64_t sourceStart, double perTick, double frame) {
    if (perTick <= 0)
        return 0;
    if (w.empty())
        return (frame - double(sourceStart)) / perTick;
    if (frame <= double(w.front().frame))
        return w.front().tick + (frame - double(w.front().frame)) / perTick;
    if (frame >= double(w.back().frame))
        return w.back().tick + (frame - double(w.back().frame)) / perTick;
    size_t i = 1;
    while (double(w[i].frame) < frame)
        i++;
    const WarpMarker& a0 = w[i - 1];
    const WarpMarker& a1 = w[i];
    double span = double(a1.frame - a0.frame);
    return span > 0 ? a0.tick + (frame - double(a0.frame)) / span * (a1.tick - a0.tick) : a1.tick;
}

/// The source frame clip tick `tick` plays. A reversed clip ignores its markers.
inline double warpedFrame(const AudioContent& a, double tick, double sampleRate) {
    static const std::vector<WarpMarker> none;
    return warpedFrame(a.reverse ? none : a.warps, a.sourceStart, framesPerTick(a, sampleRate), tick);
}

/// The clip tick at which source frame `frame` plays: the inverse of warpedFrame().
inline double warpedTick(const AudioContent& a, double frame, double sampleRate) {
    static const std::vector<WarpMarker> none;
    return warpedTick(a.reverse ? none : a.warps, a.sourceStart, framesPerTick(a, sampleRate), frame);
}

/// Clip ticks covering `frames` frames of a source at `sampleRate`, at the clip tempo.
inline Tick ticksForFrames(const AudioContent& a, int64_t frames, double sampleRate) {
    double perTick = framesPerTick(a, sampleRate);
    return perTick > 0 ? Tick(std::llround(double(frames) / perTick)) : 0;
}

/// How many song ticks one clip tick lasts. Stretch clips follow the song: 1. Off clips play at their own speed,
/// so at a faster song tempo they cover more song ticks (§5.4). This is the one place the model needs the tempo; it
/// is passed in, never stored.
inline double songTicksPerClipTick(const AudioContent& a, double songTempo) {
    return a.follow == TempoFollow::Off && a.sourceTempo > 0 ? songTempo / a.sourceTempo : 1.0;
}

/// A track's mixer strip settings.
struct Channel {
    float volumeDb = 0; // kSilentDb..kMaxFaderDb
    float pan = 0;      // -1..1

    bool operator==(const Channel& o) const { return volumeDb == o.volumeDb && pan == o.pan; }
    bool operator!=(const Channel& o) const { return !(*this == o); }
};

/// A send: some of a track's sound going to another track's OUT, for an effect patched there (§7.6). Before the
/// fader, or after its fader and pan.
struct Send {
    TrackId to;
    float db = kSilentDb;
    bool preFader = false;

    bool operator==(const Send& o) const { return to == o.to && db == o.db && preFader == o.preFader; }
    bool operator!=(const Send& o) const { return !(*this == o); }
};

/// How much of one source a track hears (§7.8): its level and whether it is in use. An audio track's clips and the
/// master's mix are sources kept in the song; a Track Out's or Drum Out's is kept in that module.
struct SourceLevel {
    float db = 0; // kSilentDb..kMaxFaderDb
    bool use = true;
    float pan = 0;     // -1..1
    bool solo = false; // heard alone among the inputs of its track

    bool operator==(const SourceLevel& o) const { return db == o.db && use == o.use && pan == o.pan && solo == o.solo; }
    bool operator!=(const SourceLevel& o) const { return !(*this == o); }
};

/// How much of the sound before an effect loop stays at Mix `mix` (0..1): all of it up to 50%, then less, to none at
/// 100%. With loopWet() this is a dry/wet balance whose middle plays both in full (DECISIONS §153).
inline float loopDry(float mix) { return std::clamp(2 * (1 - mix), 0.f, 1.f); }
/// How much of what comes back from an effect loop is heard at Mix `mix` (0..1): none at 0%, all of it from 50%.
inline float loopWet(float mix) { return std::clamp(2 * mix, 0.f, 1.f); }

/// The master: the sum of every track, out through MAIN. Not a Track: it has no clips, voices or export. It has its
/// own ID from the song's counter, so output modules and mixer automation can point at it like at a track.
struct MasterChannel {
    TrackId id;
    float volumeDb = 0;
    /// Its automation: only a Volume lane (the master has no pan), shown under the Master row.
    std::vector<AutomationEnvelope> automation;
    /// The mix of every track as a source of the master, beside what comes back on its Track Out's IN (§7.8).
    SourceLevel mix;

    bool operator==(const MasterChannel& o) const {
        return id == o.id && volumeDb == o.volumeDb && automation == o.automation && mix == o.mix;
    }
    bool operator!=(const MasterChannel& o) const { return !(*this == o); }
};

} // namespace tessera
