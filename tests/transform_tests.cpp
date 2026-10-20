#include "doctest.h"

#include "../src/model/Transforms.hpp"

using namespace tessera;
using namespace tessera::transforms;

namespace {

Note note(uint64_t id, Tick start, Tick length, int pitch, float velocity = 0.8f) {
    Note n;
    n.id = NoteId(id);
    n.start = start;
    n.length = length;
    n.pitch = int8_t(pitch);
    n.velocity = velocity;
    return n;
}

IdSet all(const NoteList& notes) { return edits::idsOf(notes); }

const Note& byId(const NoteList& notes, uint64_t id) {
    for (const Note& n : notes)
        if (n.id.value == id)
            return n;
    FAIL("no note " << id);
    return notes.front();
}

struct Ids {
    uint64_t next = 100;
    NoteId operator()() { return NoteId(next++); }
};

constexpr Tick k16 = kPpq / 4;

} // namespace

TEST_CASE("transforms: quantize moves starts toward the grid, with strength and range") {
    NoteList notes{note(1, 250, 100, 60), note(2, 10, 100, 62), note(3, 470, 100, 64)};
    QuantizeOptions o;
    NoteList full = quantize(notes, all(notes), o);
    CHECK(byId(full, 1).start == 240);
    CHECK(byId(full, 2).start == 0);
    CHECK(byId(full, 3).start == 480);
    CHECK(byId(full, 1).length == 100);

    o.strength = 0.5f;
    CHECK(byId(quantize(notes, all(notes), o), 1).start == 245);

    // Notes already within the range keep their feel.
    o.strength = 1;
    o.range = 15;
    NoteList ranged = quantize(notes, all(notes), o);
    CHECK(byId(ranged, 1).start == 250);
    CHECK(byId(ranged, 2).start == 10);

    // End only: the start stays, the end lands on the grid.
    QuantizeOptions ends;
    ends.ends = Ends::End;
    CHECK(byId(quantize(notes, all(notes), ends), 1).start == 250);
    CHECK(byId(quantize(notes, all(notes), ends), 1).end() == 480);
    CHECK(byId(quantize(notes, all(notes), ends), 3).end() == 480);
}

TEST_CASE("transforms: quantize with swing pulls every second step later") {
    NoteList notes{note(1, k16 + 70, 50, 60), note(2, 2 * k16 + 10, 50, 60)};
    QuantizeOptions o;
    o.swing = 1; // the off step moves half a step later
    NoteList q = quantize(notes, all(notes), o);
    CHECK(byId(q, 1).start == k16 + k16 / 2);
    CHECK(byId(q, 2).start == 2 * k16);
}

TEST_CASE("transforms: humanize is repeatable per seed and stays in bounds") {
    NoteList notes{note(1, 0, 240, 60), note(2, 480, 240, 62)};
    HumanizeOptions o;
    o.timing = 30;
    o.velocity = 0.1f;
    o.length = 0.2f;
    o.seed = 7;
    NoteList a = humanize(notes, all(notes), o), b = humanize(notes, all(notes), o);
    CHECK(a == b);
    for (const Note& n : a) {
        CHECK(n.start >= 0);
        CHECK(n.velocity <= 1.f);
        CHECK(n.length >= 1);
    }
    CHECK(std::llabs(byId(a, 2).start - 480) <= 30);
    o.seed = 8;
    CHECK(!(humanize(notes, all(notes), o) == a));
}

TEST_CASE("transforms: legato fills to the next note") {
    NoteList notes{note(1, 0, 100, 60), note(2, 480, 100, 64), note(3, 960, 100, 60)};
    NoteList l = legato(notes, all(notes), false, 0);
    CHECK(byId(l, 1).length == 480);
    CHECK(byId(l, 2).length == 480);
    CHECK(byId(l, 3).length == 100);
    NoteList same = legato(notes, all(notes), true, -10);
    CHECK(byId(same, 1).length == 950);
    CHECK(byId(same, 2).length == 100);
}

TEST_CASE("transforms: strum spreads a chord and keeps its end") {
    NoteList notes{note(1, 0, 480, 64), note(2, 0, 480, 60), note(3, 0, 480, 67)};
    NoteList up = strum(notes, all(notes), StrumDirection::Up, 20, 0);
    CHECK(byId(up, 2).start == 0);
    CHECK(byId(up, 1).start == 20);
    CHECK(byId(up, 3).start == 40);
    CHECK(byId(up, 3).end() == 480);
    NoteList down = strum(notes, all(notes), StrumDirection::Down, 20, 0);
    CHECK(byId(down, 3).start == 0);
    CHECK(byId(down, 2).start == 40);
}

TEST_CASE("transforms: flam adds a grace note before") {
    NoteList notes{note(1, 480, 240, 60, 1.f), note(2, 0, 240, 62)};
    Ids ids;
    NoteList f = flam(notes, IdSet{NoteId(1)}, 30, 0.5f, std::ref(ids));
    REQUIRE(f.size() == 3);
    const Note& grace = byId(f, 100);
    CHECK(grace.start == 450);
    CHECK(grace.length == 30);
    CHECK(grace.velocity == doctest::Approx(0.5f));
}

TEST_CASE("transforms: chop splits into parts or at the grid, with a velocity ramp") {
    NoteList notes{note(1, 0, 960, 60, 1.f)};
    Ids ids;
    NoteList parts = chop(notes, all(notes), 4, 0, 0.5f, std::ref(ids));
    REQUIRE(parts.size() == 4);
    CHECK(parts[1].start == 240);
    CHECK(parts[0].id == NoteId(1));
    CHECK(parts[3].velocity == doctest::Approx(0.5f));
    NoteList grid = chop(notes, all(notes), 0, 320, 1.f, std::ref(ids));
    CHECK(grid.size() == 3);
}

TEST_CASE("transforms: glue joins touching notes of one pitch") {
    NoteList notes{note(1, 0, 240, 60), note(2, 240, 240, 60), note(3, 600, 100, 60), note(4, 240, 100, 62)};
    NoteList g = glue(notes, all(notes));
    CHECK(g.size() == 3);
    CHECK(byId(g, 1).length == 480);
    CHECK(byId(g, 3).start == 600);
}

TEST_CASE("transforms: arpeggiate turns a chord into steps") {
    NoteList notes{note(1, 0, 960, 64), note(2, 0, 960, 60), note(3, 0, 960, 67)};
    Ids ids;
    ArpOptions o;
    o.rate = 240;
    o.gate = 0.5f;
    NoteList up = arpeggiate(notes, all(notes), o, std::ref(ids));
    REQUIRE(up.size() == 4);
    CHECK(up[0].pitch == 60);
    CHECK(up[1].pitch == 64);
    CHECK(up[2].pitch == 67);
    CHECK(up[3].pitch == 60);
    CHECK(up[1].start == 240);
    CHECK(up[1].length == 120);

    o.pattern = ArpPattern::UpDown;
    o.rate = 120;
    NoteList upDown = arpeggiate(notes, all(notes), o, std::ref(ids));
    std::vector<int> pitches;
    for (const Note& n : upDown)
        pitches.push_back(n.pitch);
    CHECK(pitches == std::vector<int>{60, 64, 67, 64, 60, 64, 67, 64});

    o.pattern = ArpPattern::Up;
    o.octaves = 2;
    o.rate = 120;
    NoteList two = arpeggiate(notes, all(notes), o, std::ref(ids));
    CHECK(two[3].pitch == 72);
}

TEST_CASE("transforms: arpeggiate follows the held notes, so moving voices and strums make one arpeggio") {
    // Trello: "Arpeggiate for these notes doesn't look ok". Bar 1: A3 and D4 held while F4 moves to E4 on beat 4.
    // Bar 2: a chord strummed one tick late, with E4 moving to D4.
    const Tick bar = 4 * kPpq;
    NoteList notes{note(1, 0, bar, 57),
                   note(2, 0, bar, 62),
                   note(3, 0, 3 * kPpq, 65),
                   note(4, 3 * kPpq, kPpq, 64),
                   note(5, bar + 1, bar, 55),
                   note(6, bar + 1, bar - 1, 60),
                   note(7, bar + 1, 3 * kPpq - 1, 64),
                   note(8, bar + 3 * kPpq, kPpq, 62)};
    Ids ids;
    ArpOptions o;
    o.rate = kPpq / 4;
    NoteList arp = arpeggiate(notes, all(notes), o, std::ref(ids));
    REQUIRE(arp.size() == 32); // one note per 1/16, never two at once
    for (size_t i = 0; i < arp.size(); i++) {
        CHECK(arp[i].start == Tick(i) * o.rate);
        CHECK(arp[i].length <= o.rate);
    }
    CHECK(arp[0].pitch == 57);
    CHECK(arp[1].pitch == 62);
    CHECK(arp[2].pitch == 65);
    CHECK(arp[12].pitch == 57); // F4 -> E4 doesn't restart the pattern or add a second arpeggio
    CHECK(arp[14].pitch == 64);
    CHECK(arp[16].pitch == 55); // the strummed chord starts on the beat, from its lowest note
}

TEST_CASE("transforms: connect walks the key between notes") {
    Scale cMajor(0, 0b101010110101);
    NoteList notes{note(1, 0, 240, 60), note(2, 960, 240, 67)};
    Ids ids;
    NoteList c = connect(notes, all(notes), cMajor, 240, std::ref(ids));
    REQUIRE(c.size() == 5);
    for (const Note& n : c)
        CHECK(cMajor.inScale(n.pitch));
    CHECK(c[1].start == 240);
    CHECK(c[1].pitch > 60);
    CHECK(c[3].pitch < 67);
}

TEST_CASE("transforms: reverse, retrograde and invert") {
    NoteList notes{note(1, 0, 240, 60), note(2, 480, 480, 64)};
    NoteList r = reverse(notes, all(notes));
    CHECK(byId(r, 2).start == 0);
    CHECK(byId(r, 1).start == 720);

    NoteList g = retrograde(notes, all(notes));
    CHECK(byId(g, 1).pitch == 64);
    CHECK(byId(g, 2).pitch == 60);

    Scale cMajor(0, 0b101010110101);
    NoteList inv = invert(notes, all(notes), 62, cMajor);
    CHECK(byId(inv, 1).pitch == 64); // C is one step below D, so E
    CHECK(byId(inv, 2).pitch == 60);
}

TEST_CASE("transforms: transpose in semitones or key steps") {
    Scale cMajor(0, 0b101010110101);
    NoteList notes{note(1, 0, 240, 64)};
    CHECK(transpose(notes, all(notes), 1, false, cMajor)[0].pitch == 65);
    CHECK(transpose(notes, all(notes), 2, true, cMajor)[0].pitch == 67);
    CHECK(transpose(notes, all(notes), 100, false, cMajor)[0].pitch == 127);
}

TEST_CASE("transforms: scale velocity and length, and shape velocities") {
    NoteList notes{note(1, 0, 240, 60, 0.5f), note(2, 960, 240, 60, 0.5f)};
    CHECK(scaleVelocity(notes, all(notes), ScaleMode::Percent, 1.5f)[0].velocity == doctest::Approx(0.75f));
    CHECK(scaleVelocity(notes, all(notes), ScaleMode::Set, 0.9f)[1].velocity == doctest::Approx(0.9f));
    CHECK(scaleLength(notes, all(notes), ScaleMode::Percent, 0.5)[0].length == 120);
    CHECK(scaleLength(notes, all(notes), ScaleMode::Set, 60)[1].length == 60);

    NoteList ramp = shapeVelocity(notes, all(notes), Shape::RampUp, ShapeMode::Replace, 1, 1, 1);
    CHECK(ramp[0].velocity == doctest::Approx(0));
    CHECK(ramp[1].velocity == doctest::Approx(1));
    NoteList mul = shapeVelocity(notes, all(notes), Shape::RampDown, ShapeMode::Multiply, 1, 1, 1);
    CHECK(mul[0].velocity == doctest::Approx(0.5f));
    CHECK(mul[1].velocity == doctest::Approx(0));
}

TEST_CASE("transforms: randomize keeps pitches in the key") {
    Scale cMajor(0, 0b101010110101);
    NoteList notes;
    for (uint64_t i = 1; i <= 16; i++)
        notes.push_back(note(i, Tick(i) * 240, 240, 60));
    RandomizeOptions o;
    o.pitch = 3;
    o.seed = 3;
    NoteList r = randomize(notes, all(notes), o, cMajor);
    bool moved = false;
    for (const Note& n : r) {
        CHECK(cMajor.inScale(n.pitch));
        moved = moved || n.pitch != 60;
    }
    CHECK(moved);
    CHECK(randomize(notes, all(notes), o, cMajor) == r);
}

TEST_CASE("transforms: euclidean adds hits and select picks notes") {
    Ids ids;
    IdSet added;
    NoteList e = euclidean({}, 3, 8, 0, 36, 0, k16, 0.9f, std::ref(ids), &added);
    CHECK(e.size() == 3);
    CHECK(added.size() == 3);
    CHECK(e[0].start == 0);

    NoteList notes{note(1, 0, 100, 60, 0.2f), note(2, 240, 100, 62, 0.9f), note(3, 480, 100, 60, 0.5f)};
    notes[2].muted = true;
    SelectOptions s;
    s.pitch = 60;
    CHECK(select(notes, {}, s).size() == 2);
    s.by = SelectBy::VelocityRange;
    s.low = 0.4f;
    s.high = 1;
    CHECK(select(notes, {}, s) == IdSet{NoteId(2), NoteId(3)});
    s.by = SelectBy::OffBeats;
    s.grid = 480;
    CHECK(select(notes, {}, s) == IdSet{NoteId(2)});
    s.by = SelectBy::EveryNth;
    s.every = 2;
    CHECK(select(notes, {}, s) == IdSet{NoteId(1), NoteId(3)});
    s.by = SelectBy::Muted;
    CHECK(select(notes, {}, s) == IdSet{NoteId(3)});
    // Only among the given notes.
    s.by = SelectBy::SamePitch;
    CHECK(select(notes, IdSet{NoteId(1), NoteId(2)}, s) == IdSet{NoteId(1)});
}
