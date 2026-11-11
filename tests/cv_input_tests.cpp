#include "doctest.h"

#include "../src/engine/CvNoteInput.hpp"

#include <vector>

using namespace tessera;

namespace {

constexpr float kHigh = 10.f;

/// The V/OCT voltage of MIDI note `pitch`.
float voctOf(int pitch) { return float(pitch - 60) / 12.f; }

/// Runs `samples` samples of one channel and collects every note event.
std::vector<CvNoteInput::Event> run(CvNoteInput& in, int samples, float gate, float voct, float velocity = 0.7f,
                                    int channel = 0) {
    std::vector<CvNoteInput::Event> all;
    CvNoteInput::Events out;
    for (int i = 0; i < samples; i++) {
        int n = in.process(channel, gate, voct, velocity, out);
        all.insert(all.end(), out.begin(), out.begin() + n);
    }
    return all;
}

/// A CV input whose new pitches settle after 48 samples (1 ms at 48 kHz).
CvNoteInput input48k() {
    CvNoteInput in;
    in.setSampleRate(48000);
    return in;
}

} // namespace

TEST_SUITE("cv note input") {
    TEST_CASE("a rising gate starts a note at that moment's pitch and a falling one ends it") {
        CvNoteInput in = input48k();
        auto on = run(in, 10, kHigh, voctOf(64));
        REQUIRE(on.size() == 1);
        CHECK(on[0].on);
        CHECK(on[0].pitch == 64);
        CHECK(on[0].velocity == doctest::Approx(0.7f));
        auto off = run(in, 1, 0.f, voctOf(64));
        REQUIRE(off.size() == 1);
        CHECK_FALSE(off[0].on);
        CHECK(off[0].pitch == 64);
    }

    TEST_CASE("a new key while the gate stays high ends the note and starts the new pitch (issue #12)") {
        CvNoteInput in = input48k();
        run(in, 100, kHigh, voctOf(60));
        auto legato = run(in, 100, kHigh, voctOf(67), 0.9f);
        REQUIRE(legato.size() == 2);
        CHECK_FALSE(legato[0].on);
        CHECK(legato[0].pitch == 60);
        CHECK(legato[1].on);
        CHECK(legato[1].pitch == 67);
        CHECK(legato[1].velocity == doctest::Approx(0.9f));

        // Letting go of the second key while the first is still down: back to the first, as a mono keyboard does.
        auto back = run(in, 100, kHigh, voctOf(60));
        REQUIRE(back.size() == 2);
        CHECK(back[0].pitch == 67);
        CHECK(back[1].pitch == 60);

        auto off = run(in, 1, 0.f, voctOf(60));
        REQUIRE(off.size() == 1);
        CHECK(off[0].pitch == 60);
    }

    TEST_CASE("a new pitch counts only once it has held for the settle time") {
        CvNoteInput in = input48k();
        run(in, 10, kHigh, voctOf(60));
        CHECK(run(in, 47, kHigh, voctOf(62)).empty());
        CHECK(run(in, 1, kHigh, voctOf(62)).size() == 2);
    }

    TEST_CASE("a pitch that passes through on its way somewhere else is skipped") {
        CvNoteInput in = input48k();
        run(in, 10, kHigh, voctOf(60));
        CHECK(run(in, 20, kHigh, voctOf(61)).empty());
        auto e = run(in, 48, kHigh, voctOf(62));
        REQUIRE(e.size() == 2);
        CHECK(e[0].pitch == 60);
        CHECK(e[1].pitch == 62);
    }

    TEST_CASE("small wobbles and noise near a semitone's edge don't start notes") {
        CvNoteInput in = input48k();
        run(in, 10, kHigh, voctOf(60));
        CHECK(run(in, 200, kHigh, 0.55f / 12.f).empty()); // 0.55 semitones up: still inside the hysteresis
        CHECK(run(in, 200, kHigh, -0.4f / 12.f).empty());
        CHECK(run(in, 200, kHigh, 0.65f / 12.f).size() == 2); // past it: now it is C#
    }

    TEST_CASE("pitch moves while the gate is low do nothing") {
        CvNoteInput in = input48k();
        CHECK(run(in, 200, 0.f, voctOf(72)).empty());
        CHECK_FALSE(in.held(0));
    }

    TEST_CASE("a held V/OCT past either end of the note range plays one note, not a stream of them") {
        for (float v : {6.f, 10.f, -6.f}) {
            CAPTURE(v);
            CvNoteInput in = input48k();
            REQUIRE(run(in, 1, kHigh, v).size() == 1);
            CHECK(run(in, 48000, kHigh, v).empty());
        }
        CvNoteInput in = input48k();
        run(in, 1, kHigh, 10.f);
        auto e = run(in, 100, kHigh, voctOf(60));
        REQUIRE(e.size() == 2);
        CHECK(e[0].pitch == 127);
        CHECK(e[1].pitch == 60);
    }

    TEST_CASE("each poly channel follows its own gate and pitch") {
        CvNoteInput in = input48k();
        run(in, 10, kHigh, voctOf(60), 0.7f, 0);
        run(in, 10, kHigh, voctOf(64), 0.7f, 1);
        auto e = run(in, 100, kHigh, voctOf(65), 0.7f, 1);
        REQUIRE(e.size() == 2);
        CHECK(e[0].channel == 1);
        CHECK(e[0].pitch == 64);
        CHECK(in.held(0));
        CHECK(run(in, 100, kHigh, voctOf(60), 0.7f, 0).empty());
    }
}
