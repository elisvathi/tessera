#pragma once

#include "SoundType.hpp"
#include "Types.hpp"

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

    /// Pitch factor of the semitones and cents (2 = an octave up).
    double pitchFactor() const { return std::exp2((semitones + double(cents) / 100) / 12); }

    bool operator==(const AudioContent& o) const {
        return source == o.source && sourceStart == o.sourceStart && follow == o.follow && sound == o.sound &&
               sourceTempo == o.sourceTempo && gainDb == o.gainDb && semitones == o.semitones && cents == o.cents &&
               reverse == o.reverse;
    }
};

/// Seconds of one tick at `bpm`.
inline double secondsPerTick(double bpm) { return 60.0 / (bpm * double(kPpq)); }

/// Source frames per clip tick: one tick at the clip tempo, in the source's own sample rate.
inline double framesPerTick(const AudioContent& a, double sampleRate) {
    return secondsPerTick(a.sourceTempo) * sampleRate;
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

/// A fade at one end of a timeline instance. `curve` bends it: 0 is straight (equal power in a crossfade).
struct Fade {
    Tick length = 0;
    float curve = 0; // -1..1

    bool operator==(const Fade& o) const { return length == o.length && curve == o.curve; }
    bool operator!=(const Fade& o) const { return !(*this == o); }
};

/// A track's mixer strip settings.
struct Channel {
    float volumeDb = 0; // kSilentDb..kMaxFaderDb
    float pan = 0;      // -1..1

    bool operator==(const Channel& o) const { return volumeDb == o.volumeDb && pan == o.pan; }
    bool operator!=(const Channel& o) const { return !(*this == o); }
};

/// The master: the sum of every track, out through MAIN. Not a Track: it has no clips, voices or export. It has its
/// own ID from the song's counter, so output modules and mixer automation can point at it like at a track.
struct MasterChannel {
    TrackId id;
    float volumeDb = 0;

    bool operator==(const MasterChannel& o) const { return id == o.id && volumeDb == o.volumeDb; }
    bool operator!=(const MasterChannel& o) const { return !(*this == o); }
};

} // namespace tessera
