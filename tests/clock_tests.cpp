#include "doctest.h"

#include "../src/engine/ClockSync.hpp"

#include <cmath>
#include <random>

using namespace tessera;

namespace {

constexpr double kRate = 48000;

/// Feeds pulses every `period` samples (with `jitter`), returning the song position at each pulse.
std::vector<double> run(ClockSync& clock, int pulses, double period, double jitter = 0, uint32_t seed = 1) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> shake(-jitter, jitter);
    std::vector<double> at;
    double position = 0;
    double next = 0;
    int64_t sample = 0;
    while (int(at.size()) < pulses) {
        bool pulse = double(sample) >= next;
        if (pulse)
            next += period + shake(rng);
        position += clock.step(pulse);
        if (pulse)
            at.push_back(position);
        sample++;
    }
    return at;
}

} // namespace

TEST_CASE("clock: steady 24 PPQN locks the position to the pulses and reads the tempo") {
    ClockSync clock;
    // 120 BPM at 48 kHz, 24 PPQN: a pulse every 1000 samples, 40 ticks apart.
    std::vector<double> at = run(clock, 49, 1000);
    for (size_t k = 0; k < at.size(); k++)
        CHECK(at[k] == doctest::Approx(double(k) * 40));
    CHECK(clock.bpm(kRate) == doctest::Approx(120));
    CHECK(clock.running());
}

TEST_CASE("clock: jitter never moves the song off the pulses, and the tempo is smoothed") {
    ClockSync clock;
    std::vector<double> at = run(clock, 97, 1000, 60, 7);
    for (size_t k = 0; k < at.size(); k++)
        CHECK(at[k] == doctest::Approx(double(k) * 40));
    CHECK(clock.bpm(kRate) == doctest::Approx(120).epsilon(0.02));
}

TEST_CASE("clock: a stopped clock holds the song at the next pulse") {
    ClockSync clock;
    run(clock, 10, 1000);
    double moved = 0;
    for (int i = 0; i < 20000; i++)
        moved += clock.step(false);
    CHECK(moved <= 40.0 + 1e-9); // up to the next pulse's tick, no further
    CHECK_FALSE(clock.running());
}

TEST_CASE("clock: PPQN and a tempo change") {
    ClockSync clock;
    clock.setPpqn(4);
    std::vector<double> slow = run(clock, 20, 6000); // 120 BPM at 4 PPQN
    CHECK(slow.back() == doctest::Approx(19 * 240));
    CHECK(clock.bpm(kRate) == doctest::Approx(120));
    std::vector<double> fast = run(clock, 20, 4000); // 180 BPM
    CHECK(clock.bpm(kRate) == doctest::Approx(180));
    (void)fast;
}

TEST_CASE("clock: reset makes the next pulse tick 0 again") {
    ClockSync clock;
    run(clock, 5, 1000);
    clock.resetPhase();
    CHECK(clock.step(true) == 0); // the pulse after a reset is the start
    double moved = 0;
    for (int i = 0; i < 999; i++)
        moved += clock.step(false);
    CHECK(moved == doctest::Approx(40).epsilon(0.01));
}
