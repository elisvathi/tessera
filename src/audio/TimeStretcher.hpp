#pragma once

#include "../model/SoundType.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tessera {

/// Reports how far a render has got, from 0 to 1. Returning false cancels the render.
using RenderProgress = std::function<bool(double)>;

/// One whole sound to stretch: its samples, how much longer it gets and how far it moves in pitch.
struct StretchJob {
    const float* const* in = nullptr;
    int channels = 0;
    int64_t inFrames = 0;
    double sampleRate = 48000;
    double ratio = 1;     // output length / input length
    double semitones = 0; // pitch change, independent of the ratio
    SoundType sound = SoundType::FullMix;
};

/// What a library is asked to do: change a sound's length and keep its pitch. With `formantFactor` other than 1 it
/// also moves the formants (a voice's character) by that factor, so a later pitch change can leave them in place.
struct LengthJob {
    const float* const* in = nullptr;
    int channels = 0;
    int64_t inFrames = 0;
    double sampleRate = 48000;
    SoundType sound = SoundType::FullMix;
    double formantFactor = 1;
};

/// A time-stretching library behind one interface, so Tessera can swap libraries without other code changing.
///
/// A library only changes length. Pitch is done the same way for every library: stretch by ratio x pitch factor,
/// then resample with libsamplerate's best sinc. That keeps notes exactly in tune, which the libraries' own pitch
/// shifting does not always do (Signalsmith tunes low notes up to 25 cents sharp).
///
/// Used only on worker threads: a render allocates and takes seconds.
class TimeStretcher {
  public:
    virtual ~TimeStretcher() = default;

    /// The library's short name, such as "signalsmith". It goes into the render cache key.
    virtual const char* id() const = 0;

    /// Renders the whole job into `out`, which holds `outFrames` frames per channel (normally
    /// stretchedFrames(inFrames, ratio)). The output lines up with the input: no added delay at the start. Returns
    /// false when cancelled through `progress` or when memory runs out.
    bool render(const StretchJob& job, float* const* out, int64_t outFrames, const RenderProgress& progress = {});

  protected:
    /// Changes the length of `job` to exactly `outFrames`, keeping its pitch, lined up with the input. Calls
    /// progress() now and then; returns false when it says cancel.
    virtual bool stretchLength(const LengthJob& job, float* const* out, int64_t outFrames,
                               const RenderProgress& progress) = 0;
};

/// The number of output frames a job makes: its input length times its ratio, rounded.
int64_t stretchedFrames(int64_t inFrames, double ratio);

/// Makes the stretcher with this id, or the default one when that id isn't built.
std::unique_ptr<TimeStretcher> makeStretcher(const std::string& id);

/// The ids of every stretcher in this build, the default first.
std::vector<std::string> availableStretchers();

} // namespace tessera
