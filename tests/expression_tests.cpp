#include "doctest.h"

#include "../src/model/ExpressionEdits.hpp"
#include "../src/model/NoteEdits.hpp"
#include "../src/model/NoteOperators.hpp"

using namespace tessera;

TEST_CASE("expression: the first point on an empty curve bends away from rest") {
    std::vector<ExprPoint> curve;
    size_t i = expr::addPoint(curve, 0.5f, 2.f, 0.f);
    REQUIRE(curve.size() == 2);
    CHECK(i == 1);
    CHECK(curve[0].pos == 0.f);
    CHECK(curve[0].value == 0.f);
    CHECK(expressionAt(curve, 0.f, 0.f) == doctest::Approx(0));
    CHECK(expressionAt(curve, 0.25f, 0.f) == doctest::Approx(1));
    CHECK(expressionAt(curve, 1.f, 0.f) == doctest::Approx(2));

    // A point at the very start needs no resting point.
    std::vector<ExprPoint> start;
    expr::addPoint(start, 0.f, 0.7f, 0.f);
    CHECK(start.size() == 1);
}

TEST_CASE("expression: points stay in order") {
    std::vector<ExprPoint> curve;
    expr::addPoint(curve, 0.8f, 1.f, 0.f);
    size_t mid = expr::addPoint(curve, 0.4f, 0.5f, 0.f);
    CHECK(mid == 1);
    REQUIRE(curve.size() == 3);
    CHECK(curve[1].pos == doctest::Approx(0.4f));

    // Moving past a neighbour stops at it.
    expr::movePoint(curve, 1, 0.95f, 0.3f);
    CHECK(curve[1].pos == doctest::Approx(0.8f));
    CHECK(curve[1].value == doctest::Approx(0.3f));
    expr::movePoint(curve, 1, -1.f, 0.3f);
    CHECK(curve[1].pos == doctest::Approx(0.f));

    expr::removePoint(curve, 1);
    CHECK(curve.size() == 2);
    expr::removePoint(curve, 9);
    CHECK(curve.size() == 2);
}

TEST_CASE("expression: each curve has its own list and range") {
    Note n;
    expr::addPoint(expr::points(n, expr::Curve::Timbre), 0.f, 0.6f, 0.f);
    CHECK(n.timbreExpr.size() == 1);
    CHECK(n.pitchExpr.empty());
    CHECK(n.pressureExpr.empty());
    CHECK(expr::info(expr::Curve::Pitch).min == -24);
    CHECK(expr::info(expr::Curve::Pressure).max == 1);
}

TEST_CASE("expression: a resized or sliced note keeps its curves where they were in time") {
    Note n;
    n.id = NoteId(1);
    n.start = 0;
    n.length = 960;
    n.pitchExpr = {{0, 0, 0}, {0.5f, 0, 0}, {1, 4, 0.3f}}; // hold for a beat, then glide up to +4 by tick 960

    SUBCASE("longer: the glide still ends at tick 960, then holds") {
        edits::NoteList out = edits::resizeEnd({n}, {n.id}, 960, 1);
        const Note& r = out[0];
        REQUIRE(r.pitchExpr.size() == 3);
        CHECK(r.pitchExpr[1].pos == doctest::Approx(0.25));
        CHECK(r.pitchExpr[2].pos == doctest::Approx(0.5));
        CHECK(r.pitchExpr[2].tension == doctest::Approx(0.3f));
        CHECK(expressionAt(r.pitchExpr, 0.9f, 0) == doctest::Approx(4));
    }
    SUBCASE("shorter: the glide is cut where the note now ends, at the value it had there") {
        edits::NoteList out = edits::resizeEnd({n}, {n.id}, -240, 1);
        const Note& r = out[0];
        REQUIRE(r.pitchExpr.size() == 3);
        CHECK(r.pitchExpr[1].pos == doctest::Approx(480.0 / 720.0));
        CHECK(r.pitchExpr[2].pos == doctest::Approx(1));
        CHECK(r.pitchExpr[2].value == doctest::Approx(expressionAt(n.pitchExpr, 0.75f, 0)));
    }
    SUBCASE("start moved later: the head is cut, the glide stays at its ticks") {
        edits::NoteList out = edits::resizeStart({n}, {n.id}, 240, 1);
        const Note& r = out[0];
        REQUIRE(r.pitchExpr.size() == 3);
        CHECK(r.pitchExpr[0].pos == 0);
        CHECK(r.pitchExpr[0].value == doctest::Approx(0));
        CHECK(r.pitchExpr[1].pos == doctest::Approx(240.0 / 720.0));
    }
    SUBCASE("slice: each half plays its own part of the curve") {
        int next = 50;
        edits::NoteList out = edits::slice({n}, n.id, 720, [&] { return NoteId(uint64_t(next++)); });
        REQUIRE(out.size() == 2);
        const Note& left = out[0];
        const Note& right = out[1];
        CHECK(expressionAt(left.pitchExpr, 1, 0) == doctest::Approx(expressionAt(n.pitchExpr, 0.75f, 0)));
        CHECK(expressionAt(right.pitchExpr, 0, 0) == doctest::Approx(expressionAt(n.pitchExpr, 0.75f, 0)));
        CHECK(expressionAt(right.pitchExpr, 1, 0) == doctest::Approx(4));
    }
}

TEST_CASE("expression: picked points are found by place and deleted together") {
    Note a, b;
    a.id = NoteId(1);
    b.id = NoteId(2);
    a.pitchExpr = {{0, 0, 0}, {0.5f, 0, 0}, {1, 4, 0}};
    b.pitchExpr = {{0, 0, 0}, {1, 3, 0}};
    CHECK(expr::findPoint(a.pitchExpr, 0.5f, 0) == 1);
    CHECK(expr::findPoint(a.pitchExpr, 0.5f, 1) == -1);

    std::vector<Note> notes = {a, b};
    size_t n = expr::removePoints(notes, expr::Curve::Pitch, {{NoteId(1), 0.5f, 0}, {NoteId(2), 1, 3}});
    CHECK(n == 2);
    CHECK(notes[0].pitchExpr.size() == 2); // the start and the glide's end are left
    CHECK(notes[1].pitchExpr.empty());     // only the resting start was left: the curve goes
}
