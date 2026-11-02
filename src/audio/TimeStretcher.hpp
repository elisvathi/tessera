#pragma once

#include "../model/SoundType.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tessera {

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

/// A time-stretching library behind one interface, so Tessera can swap libraries without other code changing.
///
/// Used only on worker threads: a render allocates, takes seconds and may use the library's own threads.
class TimeStretcher {
  public:
    virtual ~TimeStretcher() = default;

    /// The library's short name, such as "signalsmith". It goes into the render cache key.
    virtual const char* id() const = 0;

    /// Renders the whole job into `out`, which holds `outFrames` frames per channel (normally inFrames * ratio,
    /// rounded). The output lines up with the input: no added delay at the start. Calls progress() now and then
    /// with a fraction from 0 to 1; when it returns false the render stops. Returns false when cancelled.
    virtual bool render(const StretchJob& job, float* const* out, int64_t outFrames,
                        const std::function<bool(double)>& progress) = 0;
};

/// The number of output frames a job makes: its input length times its ratio, rounded.
int64_t stretchedFrames(int64_t inFrames, double ratio);

/// Makes the stretcher with this id, or the default one when that id isn't built.
std::unique_ptr<TimeStretcher> makeStretcher(const std::string& id);

/// The ids of every stretcher in this build, the default first.
std::vector<std::string> availableStretchers();

} // namespace tessera
