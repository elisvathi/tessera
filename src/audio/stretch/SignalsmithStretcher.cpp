#include "Stretchers.hpp"

#include <signalsmith-stretch.h>

#include <algorithm>
#include <vector>

namespace tessera {

namespace {

// A fixed seed, so the same job always renders the same samples (the render cache relies on it).
constexpr long kSeed = 0x7e55e7a;

/// Signalsmith Stretch behind the TimeStretcher interface.
class SignalsmithStretcher final : public TimeStretcher {
  public:
    using Stretch = signalsmith::stretch::SignalsmithStretch<float>;

    const char* id() const override { return "signalsmith"; }

    /// Renders like the library's exact() call, but in pieces, so progress and cancel work on long files.
    bool render(const StretchJob& job, float* const* out, int64_t outFrames,
                const std::function<bool(double)>& progress) override {
        Stretch stretch(kSeed);
        configure(stretch, job);
        double rate = double(job.inFrames) / double(std::max<int64_t>(outFrames, 1));
        int64_t seekFrames = stretch.outputSeekLength(float(rate));
        if (job.inFrames < seekFrames)
            return renderPadded(job, out, outFrames, seekFrames, progress);

        std::vector<const float*> in(job.channels);
        std::vector<float*> dst(job.channels);
        auto pointAt = [&](int64_t inPos, int64_t outPos) {
            for (int c = 0; c < job.channels; c++) {
                in[c] = job.in[c] + inPos;
                dst[c] = out[c] + outPos;
            }
        };
        pointAt(0, 0);
        stretch.outputSeek(in.data(), int(seekFrames));

        int64_t bodyIn = job.inFrames - seekFrames;
        int64_t bodyOut = std::clamp<int64_t>(outFrames - int64_t(double(seekFrames) / rate), 0, outFrames);
        int64_t pieces = std::max<int64_t>(1, bodyOut / int64_t(job.sampleRate));
        for (int64_t k = 0; k < pieces; k++) {
            if (progress && !progress(double(k) / double(pieces)))
                return false;
            int64_t in0 = bodyIn * k / pieces, in1 = bodyIn * (k + 1) / pieces;
            int64_t out0 = bodyOut * k / pieces, out1 = bodyOut * (k + 1) / pieces;
            pointAt(seekFrames + in0, out0);
            stretch.process(in.data(), int(in1 - in0), dst.data(), int(out1 - out0));
        }
        pointAt(0, bodyOut);
        stretch.flush(dst.data(), int(outFrames - bodyOut), float(rate));
        if (progress)
            progress(1);
        return true;
    }

  private:
    /// Picks block sizes, pitch and formant handling from the sound type.
    static void configure(Stretch& stretch, const StretchJob& job) {
        float rate = float(job.sampleRate);
        if (job.sound == SoundType::Drums)
            // Shorter blocks smear attacks less, at some cost to low notes.
            stretch.configure(job.channels, int(rate * 0.06f), int(rate * 0.015f));
        else
            stretch.presetDefault(job.channels, rate);
        if (job.sound == SoundType::Voice) {
            stretch.setTransposeSemitones(float(job.semitones), 8000 / rate);
            stretch.setFormantFactor(1, true);
        } else {
            stretch.setTransposeSemitones(float(job.semitones));
        }
    }

    /// A sound shorter than the stretcher's window: render it with silence after it, then keep the front.
    bool renderPadded(const StretchJob& job, float* const* out, int64_t outFrames, int64_t paddedFrames,
                      const std::function<bool(double)>& progress) {
        std::vector<std::vector<float>> padded(job.channels, std::vector<float>(paddedFrames, 0.f));
        std::vector<const float*> in(job.channels);
        for (int c = 0; c < job.channels; c++) {
            std::copy(job.in[c], job.in[c] + job.inFrames, padded[c].begin());
            in[c] = padded[c].data();
        }
        int64_t paddedOut = std::max(stretchedFrames(paddedFrames, job.ratio), outFrames);
        std::vector<std::vector<float>> result(job.channels, std::vector<float>(paddedOut));
        std::vector<float*> dst(job.channels);
        for (int c = 0; c < job.channels; c++)
            dst[c] = result[c].data();
        StretchJob longer = job;
        longer.in = in.data();
        longer.inFrames = paddedFrames;
        longer.ratio = double(paddedOut) / double(paddedFrames);
        if (!render(longer, dst.data(), paddedOut, progress))
            return false;
        for (int c = 0; c < job.channels; c++)
            std::copy(result[c].begin(), result[c].begin() + outFrames, out[c]);
        return true;
    }
};

} // namespace

std::unique_ptr<TimeStretcher> makeSignalsmithStretcher() { return std::make_unique<SignalsmithStretcher>(); }

} // namespace tessera
