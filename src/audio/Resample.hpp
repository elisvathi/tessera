#pragma once

#include "TimeStretcher.hpp"

#include <cstdint>

namespace tessera {

/// Resamples a whole sound to exactly `outFrames` frames with libsamplerate's best sinc converter, lined up with
/// the input. Used on worker threads: to bring a file to Rack's sample rate, and to move pitch after a stretch.
/// Returns false when cancelled through `progress` or when the converter can't start.
bool resampleBest(const float* const* in, int channels, int64_t inFrames, float* const* out, int64_t outFrames,
                  const RenderProgress& progress = {});

} // namespace tessera
