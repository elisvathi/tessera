#include "TimeStretcher.hpp"

#include "Resample.hpp"
#include "stretch/Stretchers.hpp"

#include <algorithm>
#include <cmath>
#include <new>

namespace tessera {

namespace {

// The user's pick after the Phase 0 blind listening test (3 Nov), and the only library Tessera ships.
constexpr const char* kDefaultStretcher = "signalsmith";

/// Makes a stretcher by id, or returns null for an id Tessera doesn't have.
std::unique_ptr<TimeStretcher> makeBuilt(const std::string& id) {
    if (id == "signalsmith")
        return makeSignalsmithStretcher();
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
    for (const char* id : {"signalsmith"})
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
