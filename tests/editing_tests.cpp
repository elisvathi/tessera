#include "doctest.h"

#include "../src/model/NoteEdits.hpp"
#include "../src/ui/editing/NoteHitTest.hpp"
#include "../src/ui/interaction/Snap.hpp"

#include <algorithm>
#include <memory>

using namespace tessera;
using namespace tessera::edits;

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

const Note* find(const NoteList& notes, uint64_t id) {
    for (const Note& n : notes)
        if (n.id.value == id)
            return &n;
    return nullptr;
}

IdSet ids(std::initializer_list<uint64_t> values) {
    IdSet out;
    for (uint64_t v : values)
        out.insert(NoteId(v));
    return out;
}

IdSource counter(uint64_t start) {
    auto next = std::make_shared<uint64_t>(start);
    return [next] { return NoteId((*next)++); };
}

} // namespace

TEST_CASE("snap: fixed divisions, triplets and dotted values") {
    TimeSig ts{4, 4};
    CHECK(divisionTicks(GridDivision::Sixteenth, GridFeel::Straight, ts) == 240);
    CHECK(divisionTicks(GridDivision::Eighth, GridFeel::Triplet, ts) == 320);
    CHECK(divisionTicks(GridDivision::Quarter, GridFeel::Dotted, ts) == 1440);
    CHECK(divisionTicks(GridDivision::Bar, GridFeel::Straight, TimeSig{3, 4}) == 2880);

    SnapSetting s;
    s.adaptive = false;
    CHECK(snapStep(s, 0.1, ts) == 240);
    s.enabled = false;
    CHECK(snapStep(s, 0.1, ts) == 0);
    CHECK(snapPosition(130, 240) == 240);
    CHECK(snapPosition(130.4, 0) == 130);
}

TEST_CASE("snap: moved notes land on the grid, and never move against the drag") {
    CHECK(snapMoveDelta(480, 250, 240) == 240);   // on grid: lands on grid
    CHECK(snapMoveDelta(500, 250, 240) == 220);   // off grid by 20: lands on 720
    CHECK(snapMoveDelta(500, -250, 240) == -260); // to the left too: lands on 240
    CHECK(snapMoveDelta(500, -30, 240) == -20);   // the nearest line is on the drag's side
    CHECK(snapMoveDelta(500, 30, 240) == 0);      // the nearest line (480) is behind the drag: stay
    CHECK(snapMoveDelta(500, 100, 240) == 220);
    CHECK(snapMoveDelta(500, 130, 0) == 130); // snap off
}

TEST_CASE("snap: events pull magnetically, otherwise the grid wins") {
    CHECK(snapToEvents(1000, {990, 2000}, 20, 240) == 990);
    CHECK(snapToEvents(1100, {990, 2000}, 20, 240) == 1200);
}

TEST_CASE("snap: Ctrl+1..4 narrow, widen, triplet and toggle") {
    SnapSetting s; // adaptive
    SnapSetting n = narrower(s);
    CHECK_FALSE(n.adaptive);
    CHECK(n.division == GridDivision::ThirtySecond);
    CHECK(wider(wider(n)).division == GridDivision::Eighth);
    CHECK(narrower(narrower(narrower(n))).division == GridDivision::SixtyFourth); // stops at 1/64
    CHECK(toggledTriplet(n).feel == GridFeel::Triplet);
    CHECK_FALSE(toggledSnap(n).enabled);
    CHECK(snapLabel(n) == "1/32");
    CHECK(snapLabel(toggledTriplet(wider(n))) == "1/16T");
}

TEST_CASE("edits: move and resize clamp to valid values and keep the list sorted") {
    NoteList notes = {note(1, 0, 240, 60), note(2, 480, 240, 127), note(3, 960, 240, 64)};
    NoteList moved = move(notes, ids({2, 3}), -600, 2);
    CHECK(find(moved, 2)->start == 0);
    CHECK(find(moved, 2)->pitch == 127);
    CHECK(find(moved, 3)->start == 360);
    CHECK(std::is_sorted(moved.begin(), moved.end(), noteOrder));

    NoteList shorter = resizeEnd(notes, ids({1}), -1000, 60);
    CHECK(find(shorter, 1)->length == 60);
    NoteList earlier = resizeStart(notes, ids({2}), -100, 60);
    CHECK(find(earlier, 2)->start == 380);
    CHECK(find(earlier, 2)->end() == 720);
    NoteList tooLate = resizeStart(notes, ids({2}), 1000, 60);
    CHECK(find(tooLate, 2)->length == 60);
}

TEST_CASE("edits: Ctrl+D shifts the copy by the selection rounded up to the grid") {
    const Tick bar = 4 * kPpq;
    const TimeSig ts;
    // Four bars of chords whose last note was strummed 25 ticks late (Trello: "leaves some space").
    NoteList strummed = {note(1, 0, 4 * bar, 57), note(2, 3 * bar + 25, bar, 58)};
    CHECK(duplicateOffset(strummed, ids({1, 2}), bar, ts) == 4 * bar);
    // A real overhang still rounds up a whole step.
    NoteList longer = {note(1, 0, bar + bar / 2, 60)};
    CHECK(duplicateOffset(longer, ids({1}), bar, ts) == 2 * bar);
    // Short notes: the copy never starts before the last note does.
    NoteList shortNotes = {note(1, 0, 60, 60), note(2, bar + 10, 30, 62)};
    CHECK(duplicateOffset(shortNotes, ids({1, 2}), bar, ts) == 2 * bar);
    // One short note: one grid step.
    CHECK(duplicateOffset({note(1, 0, 60, 60)}, ids({1}), kPpq / 4, ts) == kPpq / 4);
    // Only the selection counts.
    CHECK(duplicateOffset(strummed, ids({1}), bar, ts) == 4 * bar);
}

TEST_CASE("edits: Ctrl+D repeats a phrase on the next bar or beat, not where its last note ends") {
    const Tick bar = 4 * kPpq, sixteenth = kPpq / 4;
    // Trello 14 Oct: notes on beats 1-4, the last a 1/16, landed at 1.4.2.
    NoteList phrase = {note(1, 0, kPpq, 60), note(2, kPpq, kPpq, 62), note(3, 2 * kPpq, kPpq, 64),
                       note(4, 3 * kPpq, sixteenth, 65)};
    CHECK(duplicateOffset(phrase, ids({1, 2, 3, 4}), sixteenth, TimeSig{}) == bar);
    // Half a bar or less repeats on the next beat.
    CHECK(duplicateOffset(phrase, ids({1, 2}), sixteenth, TimeSig{}) == 2 * kPpq);
    CHECK(duplicateOffset(phrase, ids({1, 4}), sixteenth, TimeSig{}) == bar);
    // Two 1/16s a 1/8 apart repeat on the beat; one 1/16 on the grid.
    NoteList shortOnes = {note(1, 0, sixteenth, 60), note(2, 2 * sixteenth, sixteenth, 62)};
    CHECK(duplicateOffset(shortOnes, ids({1, 2}), sixteenth, TimeSig{}) == kPpq);
    CHECK(duplicateOffset(shortOnes, ids({1}), sixteenth, TimeSig{}) == sixteenth);
    // In 3/4 the bar is three beats.
    CHECK(duplicateOffset(phrase, ids({1, 2, 3}), sixteenth, TimeSig{3, 4}) == 3 * kPpq);
}

TEST_CASE("edits: stretch and pitch scaling") {
    NoteList notes = {note(1, 0, 240, 60), note(2, 480, 240, 64), note(3, 960, 240, 67)};
    NoteList twice = stretchTime(notes, ids({1, 2, 3}), 0, 2.0);
    CHECK(find(twice, 2)->start == 960);
    CHECK(find(twice, 2)->length == 480);

    NoteList wide = scalePitch(notes, ids({1, 2, 3}), 60, 67, 60, 74);
    CHECK(find(wide, 1)->pitch == 60);
    CHECK(find(wide, 3)->pitch == 74);
    CHECK(find(wide, 2)->pitch == 68);
}

TEST_CASE("edits: velocity change and line") {
    NoteList notes = {note(1, 0, 240, 60, 0.5f), note(2, 480, 240, 60, 0.95f), note(3, 960, 240, 60, 0.5f)};
    NoteList louder = changeVelocity(notes, ids({1, 2}), 0.1f);
    CHECK(find(louder, 1)->velocity == doctest::Approx(0.6f));
    CHECK(find(louder, 2)->velocity == 1.f);
    NoteList ramp = velocityLine(notes, ids({1, 2, 3}), 0, 0.f, 960, 1.f);
    CHECK(find(ramp, 2)->velocity == doctest::Approx(0.5f));
    CHECK(find(ramp, 3)->velocity == doctest::Approx(1.f));
}

TEST_CASE("edits: duplicate, slice and equal slices") {
    NoteList notes = {note(1, 0, 480, 60), note(2, 480, 480, 62)};
    IdSet added;
    NoteList dup = duplicate(notes, ids({1, 2}), 960, counter(100), &added);
    CHECK(dup.size() == 4);
    CHECK(added.size() == 2);
    CHECK(find(dup, 100)->start == 960);
    CHECK(find(dup, 101)->start == 1440);

    NoteList cut = slice(notes, NoteId(1), 120, counter(200));
    CHECK(find(cut, 1)->length == 120);
    CHECK(find(cut, 200)->start == 120);
    CHECK(find(cut, 200)->length == 360);
    CHECK(slice(notes, NoteId(1), 0, counter(200)).size() == 2); // a cut at the edge does nothing

    NoteList quarters = sliceEqual(notes, NoteId(1), 4, counter(300));
    CHECK(quarters.size() == 5);
    for (const Note& n : quarters)
        if (n.pitch == 60)
            CHECK(n.length == 120);
}

TEST_CASE("edits: the overlap rule trims or replaces older notes of the same pitch") {
    NoteList notes = {note(1, 0, 960, 60), note(2, 0, 240, 62), note(3, 480, 240, 60), note(9, 300, 300, 60)};
    // Note 9 starts inside note 1 (trim its tail) and covers note 3's head (it then starts after 9).
    NoteList out = resolveOverlaps(notes, ids({9}));
    CHECK(find(out, 1)->length == 300);
    CHECK(find(out, 3)->start == 600);
    CHECK(find(out, 3)->end() == 720);
    CHECK(find(out, 2) != nullptr); // other pitch untouched

    NoteList covered = resolveOverlaps({note(1, 100, 100, 60), note(9, 0, 480, 60)}, ids({9}));
    CHECK(covered.size() == 1);
    CHECK(covered[0].id == NoteId(9));
}

TEST_CASE("hit testing: edges beat selected bodies beat other bodies") {
    TimeAxis time;
    time.setViewWidth(1000);
    time.setZoomLimits(1e-3, 10);
    time.setContentEnd(1e6);
    time.restore({0, 0.1}); // 1 px = 10 ticks
    PitchAxis pitch;
    pitch.setViewHeight(400);
    pitch.restore({127 - 70, 10}); // pitch 70 is the top row

    NoteList notes = {note(1, 0, 1000, 60), note(2, 500, 1000, 60), note(3, 2000, 20, 60)};
    double y = pitch.pitchToY(60) + 5;

    CHECK(hitTestNotes(notes, time, pitch, 30, y, ids({})).part == NoteHit::Part::Body);
    CHECK(hitTestNotes(notes, time, pitch, 30, y, ids({})).id == NoteId(1));
    CHECK(hitTestNotes(notes, time, pitch, 98, y, ids({})).part == NoteHit::Part::EndEdge);
    CHECK(hitTestNotes(notes, time, pitch, 2, y, ids({})).part == NoteHit::Part::StartEdge);
    // Where notes 1 and 2 overlap, the selected one wins.
    CHECK(hitTestNotes(notes, time, pitch, 70, y, ids({2})).id == NoteId(2));
    CHECK(hitTestNotes(notes, time, pitch, 70, y, ids({})).id == NoteId(1));
    // A 2 px note still has a grabbable end edge just outside it.
    NoteHit tiny = hitTestNotes(notes, time, pitch, 204, y, ids({}));
    CHECK(tiny.id == NoteId(3));
    CHECK(tiny.part == NoteHit::Part::EndEdge);
    CHECK_FALSE(hitTestNotes(notes, time, pitch, 500, y, ids({})).any());
    CHECK_FALSE(hitTestNotes(notes, time, pitch, 30, pitch.pitchToY(61) + 5, ids({})).any());
}

TEST_CASE("hit testing: where two notes touch, each side of the seam grabs its own note's edge") {
    TimeAxis time;
    time.setViewWidth(1000);
    time.setZoomLimits(1e-3, 10);
    time.setContentEnd(1e6);
    time.restore({0, 0.1}); // 1 px = 10 ticks
    PitchAxis pitch;
    pitch.setViewHeight(400);
    pitch.restore({127 - 70, 10});
    double y = pitch.pitchToY(60) + 5;

    NoteList touching = {note(1, 0, 1000, 60), note(2, 1000, 1000, 60)}; // the seam is at x = 100
    for (const IdSet& selected : {ids({}), ids({1}), ids({2}), ids({1, 2})}) {
        NoteHit left = hitTestNotes(touching, time, pitch, 98, y, selected);
        CHECK(left.id == NoteId(1));
        CHECK(left.part == NoteHit::Part::EndEdge);
        NoteHit right = hitTestNotes(touching, time, pitch, 102, y, selected);
        CHECK(right.id == NoteId(2));
        CHECK(right.part == NoteHit::Part::StartEdge);
        CHECK(hitTestNotes(touching, time, pitch, 100, y, selected).id == NoteId(2));
    }

    // A small gap: the nearer edge wins.
    NoteList gap = {note(1, 0, 1000, 60), note(2, 1040, 1000, 60)}; // edges at x = 100 and 104
    CHECK(hitTestNotes(gap, time, pitch, 101, y, ids({})).id == NoteId(1));
    NoteHit nearB = hitTestNotes(gap, time, pitch, 103, y, ids({}));
    CHECK(nearB.id == NoteId(2));
    CHECK(nearB.part == NoteHit::Part::StartEdge);
}

TEST_CASE("hit testing: the Bend tool grabs curve points, then the curve nearest the press") {
    TimeAxis time;
    time.setViewWidth(1000);
    time.setZoomLimits(1e-3, 10);
    time.setContentEnd(1e6);
    time.restore({0, 0.1}); // 1 px = 10 ticks
    PitchAxis pitch;
    pitch.setViewHeight(400);
    pitch.restore({127 - 70, 10});

    // Note 1 glides from C4 up to E4 over its second half; note 2 is a plain E4 under the end of the glide.
    Note glide = note(1, 0, 1000, 60);
    glide.pitchExpr = {{0.5f, 0, 0}, {1, 4, 0}};
    NoteList notes = {glide, note(2, 0, 1000, 64)};

    BendHit end = hitTestBends(notes, time, pitch, 100, pitch.bentPitchToY(64));
    CHECK(end.id == NoteId(1)); // the point wins over the plain note's row
    CHECK(end.point == 1);
    // The middle of the glide is its arch handle; elsewhere on the glide a press adds a point.
    BendHit arch = hitTestBends(notes, time, pitch, 75, pitch.bentPitchToY(62));
    CHECK(arch.id == NoteId(1));
    CHECK(arch.segment);
    CHECK(arch.point == 0);
    BendHit along = hitTestBends(notes, time, pitch, 60, pitch.bentPitchToY(60.8));
    CHECK(along.id == NoteId(1));
    CHECK(along.point == -1);
    CHECK_FALSE(along.segment);
    CHECK(along.pos == doctest::Approx(0.6));
    CHECK(segmentAt(glide.pitchExpr, 0.6f) == 0);
    CHECK(segmentAt(glide.pitchExpr, 0.2f) == -1);
    BendHit plain = hitTestBends(notes, time, pitch, 20, pitch.bentPitchToY(64));
    CHECK(plain.id == NoteId(2));
    CHECK_FALSE(hitTestBends(notes, time, pitch, 20, pitch.bentPitchToY(67)).any());
    CHECK_FALSE(hitTestBends(notes, time, pitch, 300, pitch.bentPitchToY(60)).any());
}

TEST_CASE("hit testing: rubber band, paths and slice lines") {
    TimeAxis time;
    time.setViewWidth(1000);
    time.setZoomLimits(1e-3, 10);
    time.setContentEnd(1e6);
    time.restore({0, 0.1});
    PitchAxis pitch;
    pitch.setViewHeight(400);
    pitch.restore({127 - 70, 10});
    NoteList notes = {note(1, 0, 1000, 60), note(2, 1500, 500, 62), note(3, 3000, 500, 50)};

    IdSet band = notesInRect(notes, time, pitch, 90, pitch.pitchToY(62), 160, pitch.pitchToY(60) + 1);
    CHECK(band == IdSet{NoteId(1), NoteId(2)}); // touching is enough

    double y60 = pitch.pitchToY(60) + 5;
    IdSet path = notesAlongPath(notes, time, pitch, 10, y60, 400, y60);
    CHECK(path == IdSet{NoteId(1)});

    auto cuts = sliceLineCuts(notes, time, pitch, 50, pitch.pitchToY(63), 50, pitch.pitchToY(59));
    REQUIRE(cuts.size() == 1);
    CHECK(cuts[0].first == NoteId(1));
    CHECK(cuts[0].second == doctest::Approx(500));
}
