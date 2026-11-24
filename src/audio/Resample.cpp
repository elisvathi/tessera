#include "Resample.hpp"

#include <samplerate.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace tessera {

namespace {

constexpr int64_t kChunk = 8192;

/// Frees a libsamplerate converter.
struct SrcDelete {
    /// Called by unique_ptr.
    void operator()(SRC_STATE* state) const { src_delete(state); }
};

} // namespace

bool resampleBest(const float* const* in, int channels, int64_t inFrames, float* const* out, int64_t outFrames,
                  const RenderProgress& progress) {
    for (int c = 0; c < channels; c++)
        std::fill(out[c], out[c] + outFrames, 0.f);
    if (inFrames <= 0 || outFrames <= 0)
        return true;
    int error = 0;
    std::unique_ptr<SRC_STATE, SrcDelete> state(src_new(SRC_SINC_BEST_QUALITY, channels, &error));
    if (!state)
        return false;

    double ratio = double(outFrames) / double(inFrames);
    int64_t outRoom = int64_t(double(kChunk) * ratio) + 256;
    std::vector<float> inBuf(size_t(kChunk * channels)), outBuf(size_t(outRoom * channels));
    int64_t read = 0, written = 0;
    while (true) {
        if (progress && !progress(double(read) / double(inFrames)))
            return false;
        int64_t n = std::min(kChunk, inFrames - read);
        for (int64_t i = 0; i < n; i++)
            for (int c = 0; c < channels; c++)
                inBuf[size_t(i * channels + c)] = in[c][read + i];
        SRC_DATA data = {};
        data.data_in = inBuf.data();
        data.data_out = outBuf.data();
        data.input_frames = long(n);
        data.output_frames = long(outRoom);
        data.src_ratio = ratio;
        data.end_of_input = read + n >= inFrames ? 1 : 0;
        if (src_process(state.get(), &data) != 0)
            return false;
        for (long i = 0; i < data.output_frames_gen && written < outFrames; i++, written++)
            for (int c = 0; c < channels; c++)
                out[c][written] = outBuf[size_t(i * channels + c)];
        read += data.input_frames_used;
        // After the last input, keep asking until the converter has nothing more to give.
        if ((data.end_of_input && data.output_frames_gen == 0) || written >= outFrames)
            break;
    }
    if (progress)
        progress(1);
    return true;
}

} // namespace tessera
