#include "doctest.h"

#include "../src/model/RangeEdits.hpp"

using namespace tessera;

namespace {

constexpr Tick kBar = 4 * kPpq;

/// One track with a 4-bar clip placed at bar 0 and bar 6, and a lane ramping 0..1 over 8 bars.
struct RangeSong {
    Song song;
    TrackId track;
    ClipId clip;
    AutomationId lane;

    RangeSong() {
        Track t;
        t.id = track = song.newId<TrackId>();
        Clip c;
        c.id = clip = song.newId<ClipId>();
        c.length = c.loopEnd = 4 * kBar;
        song.clips[c.id] = c;
        for (Tick at : {Tick(0), 6 * kBar}) {
            ArrangerClip a;
            a.id = song.newId<ArrangementId>();
            a.clip = c.id;
            a.position = at;
            a.length = 4 * kBar;
            t.arrangement.push_back(a);
        }
        AutomationEnvelope l = makeLane(lane = song.newId<AutomationId>(), 0, 0);
        AutoPoint p0, p1;
        p0.t = 0;
        p0.value = 0;
        p1.t = 8 * kBar;
        p1.value = 1;
        l.points = {p0, p1};
        t.automation.push_back(l);
        song.tracks.push_back(t);
    }
    NextId ids() {
        return [this] { return song.nextId++; };
    }
    const Track& tr() const { return song.tracks[0]; }
    float at(Tick t) const { return evaluate(tr().automation[0], double(t)); }
};

} // namespace

TEST_CASE("clear range: clips are cut at its edges, automation outside keeps its shape") {
    RangeSong r;
    Song before = r.song;
    CommandPtr c = rangeEdits::clearRange(r.song, {r.track}, kBar, 2 * kBar, r.ids());
    REQUIRE(c);
    c->apply(r.song);
    const auto& arr = r.tr().arrangement;
    REQUIRE(arr.size() == 3);
    CHECK(arr[0].position == 0);
    CHECK(arr[0].length == kBar);
    CHECK(arr[1].position == 2 * kBar);
    CHECK(arr[1].length == 2 * kBar);
    CHECK(r.at(kBar / 2) == doctest::Approx(1.f / 16));
    CHECK(r.at(3 * kBar) == doctest::Approx(3.f / 8));
    c->revert(r.song);
    r.song.nextId = before.nextId;
    CHECK(r.song == before);
}

TEST_CASE("delete time closes the gap for clips and automation") {
    RangeSong r;
    CommandPtr c = rangeEdits::deleteTime(r.song, {r.track}, 4 * kBar, 6 * kBar, r.ids());
    REQUIRE(c);
    c->apply(r.song);
    const auto& arr = r.tr().arrangement;
    REQUIRE(arr.size() == 2);
    CHECK(arr[1].position == 4 * kBar);          // the second clip moved left by 2 bars
    CHECK(r.at(6 * kBar) == doctest::Approx(1)); // what was at bar 8 is now at bar 6
    CHECK(r.at(2 * kBar) == doctest::Approx(0.25f));
}

TEST_CASE("insert silence opens a gap and holds automation through it") {
    RangeSong r;
    CommandPtr c = rangeEdits::insertSilence(r.song, {r.track}, 2 * kBar, kBar, r.ids());
    REQUIRE(c);
    c->apply(r.song);
    const auto& arr = r.tr().arrangement;
    REQUIRE(arr.size() == 3); // the first clip split at bar 2
    CHECK(arr[1].position == 3 * kBar);
    CHECK(arr[2].position == 7 * kBar);
    CHECK(r.at(2 * kBar + kBar / 2) == doctest::Approx(0.25f)); // held
    CHECK(r.at(9 * kBar) == doctest::Approx(1));
}

TEST_CASE("duplicate range copies clips and automation after itself") {
    RangeSong r;
    CommandPtr c = rangeEdits::duplicateRange(r.song, {r.track}, 0, kBar, r.ids());
    REQUIRE(c);
    c->apply(r.song);
    const auto& arr = r.tr().arrangement;
    bool copyAtBar1 = false;
    for (const ArrangerClip& a : arr)
        copyAtBar1 |= a.position == kBar && a.length == kBar;
    CHECK(copyAtBar1);
    CHECK(r.at(kBar + kBar / 2) == doctest::Approx(r.at(kBar / 2)));
}

TEST_CASE("automation follows a moved clip, and a copy carries a copy") {
    RangeSong r;
    // A bump under the first clip only.
    AutomationEnvelope& l = r.song.tracks[0].automation[0];
    AutoPoint a, b, c;
    a.t = 0;
    a.value = 0;
    b.t = kBar;
    b.value = 1;
    c.t = 2 * kBar;
    c.value = 0;
    l.points = {a, b, c};
    arrange::Layout before = arrange::layoutOf(r.song);
    arrange::Layout moved = before;
    moved[0].clip.position = 12 * kBar;
    CommandPtr cmd = rangeEdits::automationFollowing(r.song, before, moved);
    REQUIRE(cmd);
    cmd->apply(r.song);
    CHECK(r.at(kBar) == doctest::Approx(0));      // gone from where the clip was
    CHECK(r.at(13 * kBar) == doctest::Approx(1)); // and there where it went
    cmd->revert(r.song);

    arrange::Layout copied = before;
    arrange::Placed copy = before[0];
    copy.clip.id = ArrangementId(999);
    copy.clip.position = 2 * kBar; // nearer the first instance than the second: a copy of the first
    copied.push_back(copy);
    CommandPtr dup = rangeEdits::automationFollowing(r.song, before, copied);
    REQUIRE(dup);
    dup->apply(r.song);
    CHECK(r.at(kBar) == doctest::Approx(1)); // the original stays
    CHECK(r.at(3 * kBar) == doctest::Approx(1));

    // Trimming isn't moving.
    arrange::Layout trimmed = before;
    trimmed[0].clip.position += kPpq;
    trimmed[0].clip.offset += kPpq;
    trimmed[0].clip.length -= kPpq;
    CHECK(rangeEdits::automationFollowing(r.song, before, trimmed) == nullptr);
}
