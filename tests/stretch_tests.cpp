#include "doctest.h"

#include "../src/audio/Resample.hpp"
#include "../src/audio/TimeStretcher.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace tessera;

// The same checks run on every stretcher in the build (docs/design/AUDIO_MIXER.md §9.1), so a library can be
// swapped in only if it passes them all.

namespace {

constexpr double kRate = 48000;
constexpr double kPi = 3.14159265358979323846;

/// A stereo sound: one vector of samples per channel.
using Sound = std::vector<std::vector<float>>;

/// Renders `sound` through the stretcher with this id. Returns the stretched sound, or an empty one if cancelled.
Sound stretch(const std::string& id, const Sound& sound, double ratio, double semitones = 0,
              SoundType type = SoundType::FullMix, const std::function<bool(double)>& progress = {}) {
    std::vector<const float*> in;
    for (auto& channel : sound)
        in.push_back(channel.data());
    StretchJob job;
    job.in = in.data();
    job.channels = int(sound.size());
    job.inFrames = int64_t(sound[0].size());
    job.sampleRate = kRate;
    job.ratio = ratio;
    job.semitones = semitones;
    job.sound = type;
    int64_t outFrames = stretchedFrames(job.inFrames, ratio);
    Sound out(sound.size(), std::vector<float>(size_t(outFrames), 99.f));
    std::vector<float*> dst;
    for (auto& channel : out)
        dst.push_back(channel.data());
    auto stretcher = makeStretcher(id);
    REQUIRE(stretcher);
    CHECK(std::string(stretcher->id()) == id);
    if (!stretcher->render(job, dst.data(), outFrames, progress))
        return {};
    return out;
}

/// A sine at `hz`, the same in both channels.
Sound sine(double hz, double seconds, float level = 0.5f) {
    std::vector<float> s(size_t(seconds * kRate));
    for (size_t i = 0; i < s.size(); i++)
        s[i] = level * float(std::sin(2 * kPi * hz * double(i) / kRate));
    return {s, s};
}

/// A click every half second (120 BPM): a 2 ms burst of a 3 kHz tone, sharp at the start, then silence.
Sound clickTrack(double seconds) {
    std::vector<float> s(size_t(seconds * kRate), 0.f);
    for (size_t beat = size_t(0.25 * kRate); beat < s.size(); beat += size_t(0.5 * kRate))
        for (size_t i = 0; i < size_t(0.002 * kRate) && beat + i < s.size(); i++)
            s[beat + i] = 0.8f * float(std::sin(2 * kPi * 3000 * double(i) / kRate)) *
                          float(std::exp(-double(i) / (0.0007 * kRate)));
    return {s, s};
}

/// The pitch of a steady tone, from the time between its first and last upward zero crossings in a window.
double pitchHz(const std::vector<float>& s, size_t from, size_t to) {
    double first = -1, last = -1;
    int crossings = 0;
    for (size_t i = from + 1; i < to; i++)
        if (s[i - 1] < 0 && s[i] >= 0) {
            double t = double(i - 1) + double(-s[i - 1]) / double(s[i] - s[i - 1]);
            if (first < 0)
                first = t;
            last = t;
            crossings++;
        }
    REQUIRE(crossings > 10);
    return (crossings - 1) * kRate / (last - first);
}

/// Where the click near `expected` starts: the first sample whose 1 ms loudness reaches half the loudest nearby.
double clickStart(const std::vector<float>& s, double expected) {
    size_t window = size_t(0.001 * kRate);
    size_t from = size_t(std::max(0.0, expected - 0.1 * kRate));
    size_t to = std::min(s.size() - window, size_t(expected + 0.1 * kRate));
    std::vector<double> loud(to - from);
    double peak = 0;
    for (size_t i = from; i < to; i++) {
        double sum = 0;
        for (size_t j = 0; j < window; j++)
            sum += double(s[i + j]) * s[i + j];
        loud[i - from] = sum;
        peak = std::max(peak, sum);
    }
    for (size_t i = 0; i < loud.size(); i++)
        if (loud[i] >= 0.5 * peak)
            return double(from + i);
    return -1;
}

double cents(double hz, double expectedHz) { return 1200 * std::log2(hz / expectedHz); }

} // namespace

TEST_CASE("every build has a default stretcher, and an unknown id falls back to it") {
    auto ids = availableStretchers();
    REQUIRE(!ids.empty());
    auto fallback = makeStretcher("no such library");
    REQUIRE(fallback);
    CHECK(std::string(fallback->id()) == ids.front());
}

TEST_CASE("stretchers fill exactly the output they are given, with no stray values") {
    for (const auto& id : availableStretchers()) {
        CAPTURE(id);
        for (double ratio : {0.75, 1.0, 1.33}) {
            CAPTURE(ratio);
            Sound out = stretch(id, sine(220, 1.5), ratio);
            REQUIRE(out.size() == 2);
            CHECK(out[0].size() == size_t(stretchedFrames(int64_t(1.5 * kRate), ratio)));
            for (auto& channel : out)
                for (float v : channel)
                    REQUIRE((std::isfinite(v) && std::fabs(v) < 2.f));
            // The middle of the output carries the tone.
            double sum = 0;
            size_t mid = out[0].size() / 2;
            for (size_t i = mid; i < mid + 4800; i++)
                sum += double(out[0][i]) * out[0][i];
            CHECK(std::sqrt(sum / 4800) > 0.2);
        }
    }
}

TEST_CASE("a stretched sine keeps its pitch within 1 cent") {
    for (const auto& id : availableStretchers()) {
        CAPTURE(id);
        for (double ratio : {0.75, 0.9, 1.1, 1.33}) {
            CAPTURE(ratio);
            Sound out = stretch(id, sine(440, 2.0), ratio, 0, SoundType::Melody);
            size_t n = out[0].size();
            CHECK(std::fabs(cents(pitchHz(out[0], n / 4, 3 * n / 4), 440)) < 1.0);
        }
    }
}

// Low notes matter most: Signalsmith's own pitch shifting tuned a 110 Hz tone 20 cents sharp, which is why pitch is
// done by resampling after the stretch, the same for every library.
TEST_CASE("a pitch change lands within 1 cent, on low notes too, and leaves the length alone") {
    for (const auto& id : availableStretchers()) {
        CAPTURE(id);
        for (double hz : {55.0, 110.0, 440.0})
            for (double semitones : {-3.0, 3.0, 7.0}) {
                CAPTURE(hz);
                CAPTURE(semitones);
                for (SoundType type : {SoundType::Melody, SoundType::Voice}) {
                    Sound out = stretch(id, sine(hz, 2.0), 1.1, semitones, type);
                    size_t n = out[0].size();
                    CHECK(n == size_t(stretchedFrames(int64_t(2.0 * kRate), 1.1)));
                    double expected = hz * std::pow(2, semitones / 12);
                    CHECK(std::fabs(cents(pitchHz(out[0], n / 4, 3 * n / 4), expected)) < 1.0);
                }
            }
    }
}

TEST_CASE("resampling a 1 kHz sine from 44.1 to 48 kHz stays clean (THD+N under -90 dB)") {
    const double from = 44100;
    std::vector<float> s(size_t(2 * from));
    for (size_t i = 0; i < s.size(); i++)
        s[i] = 0.5f * float(std::sin(2 * kPi * 1000 * double(i) / from));
    std::vector<float> out(size_t(2 * kRate));
    const float* in[1] = {s.data()};
    float* dst[1] = {out.data()};
    REQUIRE(resampleBest(in, 1, int64_t(s.size()), dst, int64_t(out.size())));
    // Fit the ideal 1 kHz sine to the middle of the output; whatever is left over is distortion and noise.
    size_t a = out.size() / 4, b = 3 * out.size() / 4;
    double sc = 0, ss = 0, cc = 0, xs = 0, xc = 0;
    for (size_t i = a; i < b; i++) {
        double t = 2 * kPi * 1000 * double(i) / kRate;
        xs += out[i] * std::sin(t);
        xc += out[i] * std::cos(t);
        ss += std::sin(t) * std::sin(t);
        cc += std::cos(t) * std::cos(t);
        sc += std::sin(t) * std::cos(t);
    }
    double det = ss * cc - sc * sc;
    double ks = (xs * cc - xc * sc) / det, kc = (xc * ss - xs * sc) / det;
    double signal = 0, rest = 0;
    for (size_t i = a; i < b; i++) {
        double t = 2 * kPi * 1000 * double(i) / kRate;
        double ideal = ks * std::sin(t) + kc * std::cos(t);
        signal += ideal * ideal;
        rest += (out[i] - ideal) * (out[i] - ideal);
    }
    CHECK(std::sqrt(ks * ks + kc * kc) == doctest::Approx(0.5).epsilon(0.001));
    CHECK(10 * std::log10(rest / signal) < -90);
}

// Phase 0 finding (2 Nov): Rubber Band's finer engine puts clicks early as the stretch grows: about 2.5 ms at 1.33x,
// 6 ms at 2x. Allowed to fail until the user picks the default library (AUDIO_MIXER.md §9.1).
TEST_CASE("a stretched click track keeps its clicks on the beat within 1 ms" * doctest::may_fail()) {
    for (const auto& id : availableStretchers()) {
        CAPTURE(id);
        for (auto [ratio, semitones] : {std::pair{0.75, 0.0}, {1.33, 0.0}, {1.1, 3.0}}) {
            CAPTURE(ratio);
            CAPTURE(semitones);
            Sound in = clickTrack(4.0);
            Sound out = stretch(id, in, ratio, semitones, SoundType::Drums);
            for (double beat = 0.25; beat < 3.6; beat += 0.5) {
                CAPTURE(beat);
                // Measured the same way on the input, so the measure's own bias cancels out.
                double expected = clickStart(in[0], beat * kRate) * ratio;
                double start = clickStart(out[0], beat * ratio * kRate);
                REQUIRE(start >= 0);
                CHECK(std::fabs(start - expected) <= 0.001 * kRate);
            }
        }
    }
}

TEST_CASE("a sound shorter than the stretcher's window still renders") {
    for (const auto& id : availableStretchers()) {
        CAPTURE(id);
        Sound out = stretch(id, sine(440, 0.05), 1.2);
        REQUIRE(out.size() == 2);
        CHECK(out[0].size() == size_t(stretchedFrames(int64_t(0.05 * kRate), 1.2)));
        for (float v : out[0])
            REQUIRE((std::isfinite(v) && std::fabs(v) < 2.f));
    }
}

TEST_CASE("silence stays silent") {
    for (const auto& id : availableStretchers()) {
        CAPTURE(id);
        Sound quiet(2, std::vector<float>(size_t(kRate), 0.f));
        Sound out = stretch(id, quiet, 1.25, 2);
        for (auto& channel : out)
            for (float v : channel)
                REQUIRE(std::fabs(v) < 1e-6f);
    }
}

TEST_CASE("the same job always renders the same samples") {
    for (const auto& id : availableStretchers()) {
        CAPTURE(id);
        Sound a = stretch(id, clickTrack(1.0), 1.1, 1);
        Sound b = stretch(id, clickTrack(1.0), 1.1, 1);
        CHECK(a == b);
    }
}

TEST_CASE("a render stops when progress says cancel, and reports progress up to 1") {
    for (const auto& id : availableStretchers()) {
        CAPTURE(id);
        int calls = 0;
        Sound cancelled = stretch(id, sine(220, 3.0), 1.2, 0, SoundType::FullMix, [&](double) { return ++calls < 3; });
        CHECK(cancelled.empty());
        CHECK(calls == 3);

        double lastSeen = -1;
        bool rising = true;
        Sound done = stretch(id, sine(220, 3.0), 1.2, 0, SoundType::FullMix, [&](double f) {
            rising = rising && f >= lastSeen && f <= 1;
            lastSeen = f;
            return true;
        });
        CHECK(!done.empty());
        CHECK(rising);
        CHECK(lastSeen == 1.0);
    }
}
