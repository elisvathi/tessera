#include "Stretchers.hpp"

#include <rubberband/RubberBandStretcher.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace tessera {

namespace {

using RubberBand::RubberBandStretcher;

constexpr size_t kBlock = 4096;

/// Rubber Band (its R3 "finer" engine, offline) behind the TimeStretcher interface.
class RubberBandAdapter final : public TimeStretcher {
  public:
    const char* id() const override { return "rubberband"; }

    /// Studies the whole sound first (offline mode needs it), then processes it and collects the output.
    bool render(const StretchJob& job, float* const* out, int64_t outFrames,
                const std::function<bool(double)>& progress) override {
        for (int c = 0; c < job.channels; c++)
            std::fill(out[c], out[c] + outFrames, 0.f);
        if (job.inFrames <= 0 || outFrames <= 0)
            return true;

        RubberBandStretcher stretcher(size_t(std::lround(job.sampleRate)), size_t(job.channels), options(job.sound),
                                      double(outFrames) / double(job.inFrames), std::pow(2.0, job.semitones / 12));
        stretcher.setExpectedInputDuration(size_t(job.inFrames));
        stretcher.setMaxProcessSize(kBlock);

        std::vector<const float*> in(job.channels);
        auto pointAt = [&](int64_t pos) {
            for (int c = 0; c < job.channels; c++)
                in[c] = job.in[c] + pos;
        };
        // Studying is quick next to processing, so it gets the first tenth of the progress bar.
        for (int64_t pos = 0; pos < job.inFrames; pos += kBlock) {
            if (progress && !progress(0.1 * double(pos) / double(job.inFrames)))
                return false;
            size_t n = size_t(std::min<int64_t>(kBlock, job.inFrames - pos));
            pointAt(pos);
            stretcher.study(in.data(), n, pos + int64_t(n) >= job.inFrames);
        }

        Collector collect{stretcher, out, outFrames, job.channels};
        for (int64_t pos = 0; pos < job.inFrames; pos += kBlock) {
            if (progress && !progress(0.1 + 0.9 * double(pos) / double(job.inFrames)))
                return false;
            size_t n = size_t(std::min<int64_t>(kBlock, job.inFrames - pos));
            pointAt(pos);
            stretcher.process(in.data(), n, pos + int64_t(n) >= job.inFrames);
            collect.drain();
        }
        if (progress)
            progress(1);
        return true;
    }

  private:
    /// Reads whatever output the stretcher has ready into `out`, dropping anything past its end.
    struct Collector {
        RubberBandStretcher& stretcher;
        float* const* out;
        int64_t outFrames;
        int channels;
        int64_t written = 0;
        std::vector<std::vector<float>> spill = std::vector<std::vector<float>>(channels, std::vector<float>(kBlock));
        std::vector<float*> dst = std::vector<float*>(channels);

        /// Retrieves until the stretcher has nothing ready. After the final process() call, offline mode with no
        /// threads has everything ready, so one drain collects the rest.
        void drain() {
            for (int avail = stretcher.available(); avail > 0; avail = stretcher.available()) {
                size_t n = std::min<size_t>(size_t(avail), kBlock);
                bool fits = written + int64_t(n) <= outFrames;
                for (int c = 0; c < channels; c++)
                    dst[c] = fits ? out[c] + written : spill[c].data();
                size_t got = stretcher.retrieve(dst.data(), n);
                if (!fits) {
                    int64_t keep = std::max<int64_t>(0, std::min<int64_t>(int64_t(got), outFrames - written));
                    for (int c = 0; c < channels; c++)
                        std::copy(spill[c].begin(), spill[c].begin() + keep, out[c] + written);
                }
                written += int64_t(got);
            }
        }
    };

    /// Maps a sound type to the library's options. Offline, the finer engine, and no threads of its own.
    static RubberBandStretcher::Options options(SoundType sound) {
        RubberBandStretcher::Options o = RubberBandStretcher::OptionProcessOffline |
                                         RubberBandStretcher::OptionEngineFiner |
                                         RubberBandStretcher::OptionThreadingNever;
        switch (sound) {
        case SoundType::Drums:
            o |= RubberBandStretcher::OptionTransientsCrisp | RubberBandStretcher::OptionChannelsTogether;
            break;
        case SoundType::Melody:
            o |= RubberBandStretcher::OptionTransientsSmooth;
            break;
        case SoundType::Voice:
            o |= RubberBandStretcher::OptionFormantPreserved;
            break;
        case SoundType::FullMix:
            o |= RubberBandStretcher::OptionChannelsTogether;
            break;
        }
        return o;
    }
};

} // namespace

std::unique_ptr<TimeStretcher> makeRubberBandStretcher() { return std::make_unique<RubberBandAdapter>(); }

} // namespace tessera
