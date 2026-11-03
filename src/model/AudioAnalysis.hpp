#pragma once

#include "SoundType.hpp"
#include "Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

/// What Tessera works out from an audio file's samples: waveform peaks for drawing, the tempo guess, whether it is
/// a one-shot, and zero crossings for clean splits (docs/design/AUDIO_MIXER.md §5.5, §6.1). Pure functions, no Rack.
namespace tessera {

/// Waveform outlines at several zoom levels: the lowest and highest sample of each bin, per channel. Level 0 bins
/// are kBaseBin frames; each level above has bins kLevelStep times longer. Stored as 16-bit values to stay small
/// (a 4-minute stereo file needs about 1.5 MB).
class PeakCache {
  public:
    static constexpr int kBaseBin = 64;
    static constexpr int kLevelStep = 8;

    PeakCache() = default;
    /// Builds every level from `channels` (all the same length).
    explicit PeakCache(const std::vector<std::vector<float>>& channels);

    int channels() const { return int(levels.empty() ? 0 : levels[0].size()); }
    int64_t frames() const { return length; }
    bool empty() const { return length == 0; }

    /// The lowest and highest sample of channel `ch` between frames [from, to), using the coarsest level that still
    /// has a bin per pixel. Both 0 outside the file.
    void range(int ch, double from, double to, float& lo, float& hi) const;

    /// Bytes held, for the memory readout.
    size_t bytes() const;

  private:
    // levels[level][channel] holds min, max pairs, one per bin.
    std::vector<std::vector<std::vector<int16_t>>> levels;
    int64_t length = 0;
};

/// Where a tempo guess came from, best first.
enum class TempoSource : uint8_t { None, File, Name, Length, Onsets };

/// What Tessera guesses about a dropped file: its tempo and how its clip should play.
struct AudioGuess {
    double bpm = 0; // 0 when there is no guess
    TempoSource source = TempoSource::None;
    int beats = 0;       // for a Length guess: how many beats the file is
    bool oneShot = false; // a single hit: plays at its own speed (Follow tempo Off), once
    bool loop = true;     // repeats when it plays: loops do, one-shots and long recordings don't
    SoundType sound = SoundType::FullMix;
    double firstBeat = 0; // seconds into the file where the onsets put beat 1
};

/// The tempo written in a file name, like "drums_92bpm.wav" or "Loop 120 BPM.aif"; 0 when there is none.
double tempoFromName(const std::string& name);

/// The tempo at which a file `seconds` long is a whole number of beats (1, 2, 4 … 64; 3, 6, 12 … in 3/4 and 6/8),
/// between 70 and 180 BPM, nearest the song tempo. 0 when none fits. `beats` gets the beat count.
double tempoFromLength(double seconds, double songTempo, TimeSig ts, int* beats = nullptr);

/// The guess made at drop time, before any samples are looked at: the file's tempo chunk, then its name, then its
/// length (steps 1 and 2 of §5.5).
AudioGuess quickGuess(const std::string& name, double fileTempo, double seconds, double songTempo, TimeSig ts);

/// How strongly the sound starts something new, every kOnsetHop seconds: rises in loudness, with more weight on
/// sharp, bright attacks.
struct OnsetCurve {
    static constexpr double kHopSeconds = 0.005;
    std::vector<float> strength;
    double hop = kHopSeconds; // seconds per value, exactly
};
OnsetCurve onsetCurve(const std::vector<std::vector<float>>& channels, double sampleRate);

/// The tempo the onsets beat at, between 60 and 200 BPM with a gentle preference for 120; 0 when they don't beat
/// at all. `firstBeat` gets the seconds of the first beat.
double tempoFromOnsets(const OnsetCurve& curve, double* firstBeat = nullptr);

/// Refines a quick guess with the samples (steps 3 and 4 of §5.5): onset analysis for files without a tempo of their
/// own, one-shot detection, and the sound type. Runs on a worker; under a second for a loop.
AudioGuess fullGuess(const AudioGuess& quick, const std::vector<std::vector<float>>& channels, double sampleRate,
                     double songTempo);

/// The frame nearest `at` (within `radius` frames) where the sound crosses zero on every channel's average, so a
/// split there doesn't click. `at` itself when there is none.
int64_t nearestZeroCrossing(const std::vector<std::vector<float>>& channels, int64_t at, int64_t radius);

} // namespace tessera
