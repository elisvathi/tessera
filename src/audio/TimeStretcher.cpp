#include "TimeStretcher.hpp"

#include "Resample.hpp"
#include "stretch/Stretchers.hpp"

#include <algorithm>
#include <cmath>
#include <new>

#if !defined(TESSERA_STRETCH_SIGNALSMITH) && !defined(TESSERA_STRETCH_RUBBERBAND)
#error "Build at least one time stretcher: set STRETCHERS in the Makefile"
#endif

namespace tessera {

namespace {

// Until the Phase 0 listening test picks one, the MIT library is the default: it can ship in any product.
constexpr const char* kDefaultStretcher = "signalsmith";

/// Makes a stretcher by id, or returns null when this build doesn't include it.
std::unique_ptr<TimeStretcher> makeBuilt(const std::string& id) {
#ifdef TESSERA_STRETCH_SIGNALSMITH
    if (id == "signalsmith")
        return makeSignalsmithStretcher();
#endif
#ifdef TESSERA_STRETCH_RUBBERBAND
    if (id == "rubberband")
        return makeRubberBandStretcher();
#endif
    return nullptr;
}

} // namespace

int64_t stretchedFrames(int64_t inFrames, double ratio) { return int64_t(std::llround(double(inFrames) * ratio)); }

bool TimeStretcher::render(const StretchJob& job, float* const* out, int64_t outFrames,
                           const RenderProgress& progress) {
    LengthJob length;
    length.in = job.in;
    length.channels = job.channels;
    length.inFrames = job.inFrames;
    length.sampleRate = job.sampleRate;
    length.sound = job.sound;
    if (job.semitones == 0)
        return stretchLength(length, out, outFrames, progress);

    // Stretch to a length that the pitch change then shrinks back: up 3 semitones, the stretch is 1.19 times
    // longer, and resampling it to the asked length raises every frequency by that same factor.
    double pitch = std::pow(2.0, job.semitones / 12);
    if (job.sound == SoundType::Voice)
        length.formantFactor = 1 / pitch;
    int64_t longFrames = std::max<int64_t>(1, stretchedFrames(outFrames, pitch));
    std::vector<std::vector<float>> stretched;
    std::vector<float*> dst;
    try {
        stretched.assign(size_t(job.channels), std::vector<float>(size_t(longFrames)));
    } catch (const std::bad_alloc&) {
        return false;
    }
    for (auto& channel : stretched)
        dst.push_back(channel.data());
    auto part = [&](double from, double to) -> RenderProgress {
        if (!progress)
            return {};
        return [&progress, from, to](double f) { return progress(from + (to - from) * f); };
    };
    if (!stretchLength(length, dst.data(), longFrames, part(0, 0.9)))
        return false;
    if (!resampleBest(dst.data(), job.channels, longFrames, out, outFrames, part(0.9, 1)))
        return false;
    return true;
}

std::vector<std::string> availableStretchers() {
    std::vector<std::string> ids;
    if (makeBuilt(kDefaultStretcher))
        ids.push_back(kDefaultStretcher);
    for (const char* id : {"signalsmith", "rubberband"})
        if (id != std::string(kDefaultStretcher) && makeBuilt(id))
            ids.push_back(id);
    return ids;
}

std::unique_ptr<TimeStretcher> makeStretcher(const std::string& id) {
    if (auto stretcher = makeBuilt(id))
        return stretcher;
    return makeBuilt(availableStretchers().front());
}

} // namespace tessera
