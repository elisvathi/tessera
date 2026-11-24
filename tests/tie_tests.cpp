#include "doctest.h"

#include "../src/model/NoteOperators.hpp"
#include "../src/model/Serialize.hpp"
#include "../src/model/Ties.hpp"

#include <jansson.h>

using namespace tessera;
using namespace tessera::ties;

namespace {

/// A note for these tests, in clip ticks.
Note note(uint64_t id, Tick start, Tick length, int pitch, bool tied = false) {
    Note n;
    n.id = NoteId(id);
    n.start = start;
    n.length = length;
    n.pitch = int8_t(pitch);
    n.tied = tied;
    return n;
}

/// The note with `id` in `notes`.
const Note& byId(const NoteList& notes, uint64_t id) {
    for (const Note& n : notes)
        if (n.id.value == id)
            return n;
    FAIL("no such note");
    return notes.front();
}

constexpr Tick Q = kPpq; // a quarter note

} // namespace

TEST_CASE("Ties: a tied note continues the note that ends where it starts") {
    NoteList notes = {note(1, 0, Q, 48), note(2, Q, Q, 51, true), note(3, 3 * Q, Q, 55, true)};
    std::vector<int> p = partners(notes);
    CHECK(p[0] == -1);
    CHECK(p[1] == 0);
    CHECK(p[2] == -1); // a gap before it: the tie is broken and it plays as a normal note
}

TEST_CASE("Ties: overlapping notes tie, other channels and muted notes don't") {
    NoteList notes = {note(1, 0, Q + 10, 48), note(2, Q, Q, 51, true)};
    CHECK(partners(notes)[1] == 0);
    notes[0].channel = 2;
    CHECK(partners(notes)[1] == -1);
    notes[0].channel = 1;
    notes[0].muted = true;
    CHECK(partners(notes)[1] == -1);
}

TEST_CASE("Ties: chord to chord, each note continues the nearest pitch once") {
    NoteList notes = {note(1, 0, Q, 60),          note(2, 0, Q, 64),          note(3, 0, Q, 67),
                      note(4, Q, Q, 62, true),    note(5, Q, Q, 65, true),    note(6, Q, Q, 69, true)};
    std::vector<int> p = partners(notes);
    CHECK(p[3] == 0);
    CHECK(p[4] == 1);
    CHECK(p[5] == 2);
}

TEST_CASE("Ties: tying closes small gaps and trims overlaps") {
    NoteList notes = {note(1, 0, Q - 40, 48), note(2, Q, Q / 2, 51), note(3, Q + Q / 2 + 20, Q, 53)};
    IdSet tied;
    NoteList out = tie(notes, {NoteId(1), NoteId(2), NoteId(3)}, &tied);
    CHECK(tied.size() == 2);
    CHECK(byId(out, 1).end() == Q);
    CHECK(byId(out, 2).end() == Q + Q / 2 + 20);
    CHECK(byId(out, 2).tied);
    CHECK(byId(out, 3).tied);
    CHECK_FALSE(byId(out, 1).tied);

    // Far apart: nothing happens.
    NoteList apart = {note(1, 0, Q, 48), note(2, 3 * Q, Q, 51)};
    CHECK_FALSE(tie(apart, {NoteId(1), NoteId(2)})[1].tied);
}

TEST_CASE("Ties: one selected note ties to the note before it") {
    NoteList notes = {note(1, 0, Q + 30, 48), note(2, Q, Q, 51)};
    NoteList out = tie(notes, {NoteId(2)});
    CHECK(byId(out, 2).tied);
    CHECK(byId(out, 1).end() == Q);
    CHECK(allTied(out, {NoteId(2)}));
    CHECK_FALSE(allTied(notes, {NoteId(2)}));
}

TEST_CASE("Ties: the glide runs from the first pitch to the second across the joint") {
    Note a = note(1, 0, Q, 48);
    Note b = note(2, Q, Q, 52, true);
    b.glideBefore = Q / 4;
    b.glideAfter = Q / 4;
    Glide g = glideOf(a, b);
    CHECK(g.from == Q - Q / 4);
    CHECK(g.to == Q + Q / 4);
    CHECK(pitchAt(g, 0) == doctest::Approx(48));
    CHECK(pitchAt(g, double(Q)) == doctest::Approx(50));
    CHECK(pitchAt(g, double(2 * Q)) == doctest::Approx(52));
    // Ends stay inside the two notes.
    b.glideBefore = 10 * Q;
    CHECK(glideOf(a, b).from == 0);
}

TEST_CASE("Ties: untie keeps the glide as the notes' own pitch curves, or drops it") {
    NoteList notes = {note(1, 0, Q, 48), note(2, Q, Q, 52, true)};
    notes[1].glideBefore = Q / 2;
    notes[1].glideAfter = Q / 2;
    NoteList kept = untie(notes, {NoteId(2)}, true);
    const Note& a = byId(kept, 1);
    const Note& b = byId(kept, 2);
    CHECK_FALSE(b.tied);
    CHECK(b.glideAfter == 0);
    // The first note starts moving halfway and reaches the middle (+2) at its end.
    CHECK(expressionAt(a.pitchExpr, 0.25f, 0) == doctest::Approx(0));
    CHECK(expressionAt(a.pitchExpr, 1.f, 0) == doctest::Approx(2));
    // The second starts 2 below its pitch and arrives halfway.
    CHECK(expressionAt(b.pitchExpr, 0.f, 0) == doctest::Approx(-2));
    CHECK(expressionAt(b.pitchExpr, 0.5f, 0) == doctest::Approx(0));
    CHECK(expressionAt(b.pitchExpr, 0.9f, 0) == doctest::Approx(0));

    NoteList dropped = untie(notes, {NoteId(2)}, false);
    CHECK(byId(dropped, 1).pitchExpr.empty());
    CHECK(byId(dropped, 2).pitchExpr.empty());
}

TEST_CASE("Ties: tying again turns a kept glide back into the tie's glide (issue #22)") {
    for (float shape : {0.f, 0.5f}) {
        NoteList notes = {note(1, 0, Q, 48), note(2, Q, Q, 52, true)};
        notes[1].glideBefore = Q / 2;
        notes[1].glideAfter = Q / 4;
        notes[1].glideShape = shape;
        NoteList again = tie(untie(notes, {NoteId(2)}, true), {NoteId(2)});
        const Note& b = byId(again, 2);
        CHECK(b.tied);
        CHECK(byId(again, 1).pitchExpr.empty());
        CHECK(b.pitchExpr.empty());
        CHECK(b.glideBefore == Q / 2);
        CHECK(b.glideAfter == Q / 4);
        CHECK(b.glideShape == doctest::Approx(shape).epsilon(0.01));
    }
}

TEST_CASE("Ties: a tie and its glide are saved and loaded") {
    Note n = note(7, Q, Q, 52, true);
    n.glideBefore = 120;
    n.glideAfter = 240;
    n.glideShape = 0.5f;
    Clip clip;
    clip.id = ClipId(3);
    clip.notes = {note(8, 0, Q, 60), n};
    json_t* j = clipToJson(clip);
    Clip back = clipFromJson(j);
    json_decref(j);
    REQUIRE(back.notes.size() == 2);
    CHECK(back.notes[1] == n);
    CHECK_FALSE(back.notes[0].tied);
}

TEST_CASE("Ties: an imported legato line gets ties at its hand-overs, chords stay chords") {
    NoteList line = {note(1, 0, Q + 30, 60), note(2, Q, Q + 20, 62), note(3, 2 * Q, Q, 64), note(4, 4 * Q, Q, 65)};
    NoteList out = tieHandovers(line);
    CHECK(byId(out, 1).end() == Q);
    CHECK(byId(out, 2).tied);
    CHECK(byId(out, 3).tied);
    CHECK_FALSE(byId(out, 4).tied); // played apart

    NoteList chords = {note(1, 0, Q, 60), note(2, 0, Q, 64), note(3, Q - 10, Q, 62)};
    NoteList same = tieHandovers(chords);
    for (const Note& n : same)
        CHECK_FALSE(n.tied);
}
