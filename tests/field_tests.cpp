#include "doctest.h"

#include "../src/model/NoteFields.hpp"
#include "../src/model/TrackColors.hpp"

#include <algorithm>
#include <set>

using namespace tessera;
using namespace tessera::fields;

namespace {

edits::NoteList fourNotes() {
    edits::NoteList notes(4);
    float v[] = {0.2f, 0.4f, 0.6f, 0.8f};
    for (int i = 0; i < 4; i++) {
        notes[size_t(i)].id = NoteId(uint64_t(i + 1));
        notes[size_t(i)].start = i * kPpq;
        notes[size_t(i)].velocity = v[i];
    }
    return notes;
}

edits::IdSet all(const edits::NoteList& n) { return edits::idsOf(n); }

double vel(const edits::NoteList& n, size_t i) { return get(n[i], Field::Velocity); }

} // namespace

TEST_CASE("fields read and write in display units, clamped") {
    Note n;
    set(n, Field::Velocity, 150);
    CHECK(n.velocity == 1.f);
    set(n, Field::Chance, 25);
    CHECK(n.ops.chance == doctest::Approx(0.25f));
    set(n, Field::Repeats, 3.6);
    CHECK(n.ops.repeats == 4);
    set(n, Field::Length, 0.5);
    CHECK(n.length == kPpq / 2);
    set(n, Field::Micro, -80);
    CHECK(n.microOffset == doctest::Approx(-0.5f));
    CHECK(format(Field::Pitch, 60) == "C4");
    CHECK(format(Field::Velocity, 64.4) == "64%");
    CHECK(format(Field::Repeats, -4) == "4 per beat");
    CHECK(format(Field::Length, 1) == "1 beat");
    CHECK(format(Field::Length, 0.5) == "0.5 beats");
}

TEST_CASE("start reads as a ruler position and length in bars and beats") {
    CHECK(format(Field::Start, 0) == "1.1.1");
    CHECK(format(Field::Start, 3) == "1.4.1");
    CHECK(format(Field::Start, 4.25) == "2.1.2");
    CHECK(format(Field::Start, 1.0 + 120.0 / kPpq) == "1.2.1.120");
    CHECK(format(Field::Start, 3, TimeSig{3, 4}) == "2.1.1");
    CHECK(format(Field::Length, 4) == "1 bar");
    CHECK(format(Field::Length, 6) == "1 bar 2 beats");
    CHECK(format(Field::Length, 8) == "2 bars");
    CHECK(format(Field::Length, 1.5, TimeSig{6, 8}) == "3 beats");

    edits::NoteList n = fourNotes();
    edits::NoteList out;
    auto first = [&out, id = n[0].id] {
        return std::find_if(out.begin(), out.end(), [id](const Note& x) { return x.id == id; });
    };
    REQUIRE(applyTyped(n, {n[0].id}, Field::Start, "2.3.2", out));
    CHECK(first()->start == 4 * kPpq + 2 * kPpq + kPpq / 4);
    REQUIRE(applyTyped(n, {n[0].id}, Field::Start, "=3", out));
    CHECK(first()->start == 8 * kPpq);
    REQUIRE(applyTyped(n, {n[0].id}, Field::Start, "+1", out));
    CHECK(first()->start == kPpq);
    CHECK_FALSE(applyTyped(n, {n[0].id}, Field::Start, "0.1", out));
    CHECK_FALSE(applyTyped(n, {n[0].id}, Field::Start, "1.2.3.4", out));
    // In 6/8 a beat is an eighth note, as the ruler counts it.
    REQUIRE(applyTyped(n, {n[0].id}, Field::Length, "3", out, TimeSig{6, 8}));
    CHECK(first()->length == 3 * kPpq / 2);
}

TEST_CASE("offset keeps differences; spread widens or narrows around the middle") {
    edits::NoteList n = fourNotes();
    auto o = offset(n, all(n), Field::Velocity, 10);
    CHECK(vel(o, 0) == doctest::Approx(30));
    CHECK(vel(o, 3) == doctest::Approx(90));
    auto flat = spread(n, all(n), Field::Velocity, 0);
    for (size_t i = 0; i < 4; i++)
        CHECK(vel(flat, i) == doctest::Approx(50));
    auto wide = spread(n, all(n), Field::Velocity, 2);
    CHECK(vel(wide, 0) == doctest::Approx(0));
    CHECK(vel(wide, 3) == doctest::Approx(100));
    auto [lo, hi] = range(n, all(n), Field::Velocity);
    CHECK(lo == doctest::Approx(20));
    CHECK(hi == doctest::Approx(80));
}

TEST_CASE("randomize, ramp and set") {
    edits::NoteList n = fourNotes();
    auto r = randomize(n, all(n), Field::Velocity, 40, 60, 3);
    CHECK(r == randomize(n, all(n), Field::Velocity, 40, 60, 3));
    for (size_t i = 0; i < 4; i++) {
        CHECK(vel(r, i) >= 40 - 1e-4);
        CHECK(vel(r, i) <= 60 + 1e-4);
    }
    auto up = ramp(n, all(n), Field::Velocity, 100, 10);
    CHECK(vel(up, 0) == doctest::Approx(100));
    CHECK(vel(up, 3) == doctest::Approx(10));
    CHECK(vel(up, 1) == doctest::Approx(70));
    auto s = setAll(n, {NoteId(2)}, Field::Velocity, 5);
    CHECK(vel(s, 1) == doctest::Approx(5));
    CHECK(vel(s, 0) == doctest::Approx(20));
}

TEST_CASE("typed edits: set, add, subtract, percent, scale") {
    edits::NoteList n = fourNotes(), out;
    REQUIRE(applyTyped(n, all(n), Field::Velocity, "64", out));
    CHECK(vel(out, 2) == doctest::Approx(64));
    REQUIRE(applyTyped(n, all(n), Field::Velocity, "=10", out));
    CHECK(vel(out, 0) == doctest::Approx(10));
    REQUIRE(applyTyped(n, all(n), Field::Velocity, "+10", out));
    CHECK(vel(out, 0) == doctest::Approx(30));
    REQUIRE(applyTyped(n, all(n), Field::Velocity, "-5", out));
    CHECK(vel(out, 0) == doctest::Approx(15));
    REQUIRE(applyTyped(n, all(n), Field::Velocity, "-50%", out));
    CHECK(vel(out, 3) == doctest::Approx(40));
    REQUIRE(applyTyped(n, all(n), Field::Velocity, "*0.5", out));
    CHECK(vel(out, 1) == doctest::Approx(20));
    REQUIRE(applyTyped(n, all(n), Field::Micro, "=-20", out));
    CHECK(get(out[0], Field::Micro) == doctest::Approx(-20));
    CHECK_FALSE(applyTyped(n, all(n), Field::Velocity, "abc", out));
    CHECK_FALSE(applyTyped(n, all(n), Field::Velocity, "/0", out));
}

TEST_CASE("fields: operators turn on with a starting value and off to the defaults") {
    edits::NoteList notes = fourNotes();
    edits::IdSet two{NoteId(1), NoteId(2)};
    edits::NoteList on = setOperator(notes, two, Operator::Chance, true);
    CHECK(on[0].ops.chance == doctest::Approx(0.5f));
    CHECK(on[2].ops.chance == 1.f);
    CHECK(isOn(on[0], Operator::Chance));
    CHECK(!isOn(on[2], Operator::Chance));

    // A note that has it on keeps its value.
    on[1].ops.chance = 0.3f;
    edits::NoteList again = setOperator(on, two, Operator::Chance, true);
    CHECK(again[1].ops.chance == doctest::Approx(0.3f));

    edits::NoteList off = setOperator(again, two, Operator::Chance, false);
    CHECK(off == notes);

    edits::NoteList rec = setOperator(notes, two, Operator::Recurrence, true);
    CHECK(rec[0].ops.recurrenceLength == 2);
    CHECK(rec[0].ops.recurrenceMask == 0b01);
    CHECK(setOperator(rec, two, Operator::Recurrence, false) == notes);
    CHECK(setOperator(setOperator(notes, two, Operator::Repeats, true), two, Operator::Repeats, false) == notes);
    CHECK(setOperator(setOperator(notes, two, Operator::Spread, true), two, Operator::Spread, false) == notes);
    CHECK(setOperator(setOperator(notes, two, Operator::Occurrence, true), two, Operator::Occurrence, false) == notes);
}

TEST_CASE("fields: recurrence passes and length") {
    edits::NoteList notes = fourNotes();
    edits::IdSet one{NoteId(1)};
    // Clicking pass 3 of a note without recurrence makes a 3-pass cycle, every pass on, then turns pass 3 off.
    edits::NoteList r = setRecurrencePass(notes, one, 2, false);
    CHECK(r[0].ops.recurrenceLength == 3);
    CHECK((r[0].ops.recurrenceMask & 0b111) == 0b011);
    // Longer: new passes play. Shorter and back: the passes come back on.
    edits::NoteList longer = setRecurrenceLength(r, one, 4);
    CHECK((longer[0].ops.recurrenceMask & 0b1111) == 0b1011);
    edits::NoteList shorter = setRecurrenceLength(r, one, 2);
    CHECK(shorter[0].ops.recurrenceLength == 2);
    CHECK((setRecurrenceLength(shorter, one, 3)[0].ops.recurrenceMask & 0b111) == 0b111);
    // A cycle of 1 is "off".
    CHECK(!isOn(setRecurrenceLength(r, one, 1)[0], Operator::Recurrence));
}

TEST_CASE("the histogram tools: mean, spread around the mean, chaos, counts") {
    edits::NoteList notes = fourNotes(); // 20, 40, 60, 80
    edits::NoteList one = notes;
    one[3].velocity = 0.2f; // 20, 40, 60, 20: mean 35
    CHECK(mean(one, all(one), Field::Velocity) == doctest::Approx(35));

    SUBCASE("spread narrows and widens around the mean, not the middle of the range") {
        edits::NoteList narrow = spreadAroundMean(one, all(one), Field::Velocity, 0);
        for (size_t i = 0; i < 4; i++)
            CHECK(vel(narrow, i) == doctest::Approx(35).epsilon(0.01));
        edits::NoteList wide = spreadAroundMean(notes, all(notes), Field::Velocity, 2);
        CHECK(vel(wide, 0) == doctest::Approx(0)); // 50 - 2 * 30, clamped
        CHECK(vel(wide, 1) == doctest::Approx(30));
        CHECK(vel(wide, 3) == doctest::Approx(100)); // clamped
    }
    SUBCASE("chaos scatters within its reach, the same way for the same seed") {
        edits::NoteList a = chaos(notes, all(notes), Field::Velocity, 0.1, 7);
        bool moved = false;
        for (size_t i = 0; i < 4; i++) {
            CHECK(std::abs(vel(a, i) - vel(notes, i)) <= 10.001);
            moved |= std::abs(vel(a, i) - vel(notes, i)) > 0.5;
        }
        CHECK(moved);
        CHECK(a == chaos(notes, all(notes), Field::Velocity, 0.1, 7));
        // Further along the same drag (same seed): every note moves further the same way.
        edits::NoteList b = chaos(notes, all(notes), Field::Velocity, 0.2, 7);
        for (size_t i = 1; i < 3; i++)
            CHECK(vel(b, i) - vel(notes, i) == doctest::Approx(2 * (vel(a, i) - vel(notes, i))).epsilon(0.02));
        CHECK(chaos(notes, all(notes), Field::Velocity, 0, 7) == notes);
    }
    SUBCASE("the histogram counts notes per step") {
        std::vector<int> h = histogram(one, all(one), Field::Velocity, 0, 100, 5);
        CHECK(h == std::vector<int>{0, 2, 1, 1, 0});
        CHECK(histogram(one, all(one), Field::Velocity, 30, 50, 2) == std::vector<int>{2, 2}); // outside: the ends
    }
}

TEST_CASE("channel 1 keeps the track's colour; the 16 channels all differ, the first ones far apart") {
    for (uint32_t base : kTrackColors) {
        CHECK(channelColor(base, 1) == base);
        std::set<uint32_t> seen;
        for (int ch = 1; ch <= kMaxNoteChannel; ch++)
            seen.insert(channelColor(base, ch));
        CHECK(seen.size() == 16);
    }
    // Channel 2 sits opposite the track's colour on the ring.
    CHECK(channelColor(kTrackColors[0], 2) == kTrackColors[8]);
    CHECK(channelColor(kTrackColors[0], 7) == channelColor(0xEF8070, 7)); // a custom colour uses its nearest
    CHECK(fields::format(Field::Channel, 3) == "3");
}
