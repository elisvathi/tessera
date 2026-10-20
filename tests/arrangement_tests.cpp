#include "doctest.h"

#include "../src/model/ArrangementEdits.hpp"

#include <algorithm>

using namespace tessera;
using namespace tessera::arrange;

namespace {

constexpr Tick kBeat = kPpq;
constexpr Tick kBar = 4 * kPpq;

/// A one-beat looping clip: a short note at 0 and one at 1/2 beat that runs past the loop end.
Clip beatClip(Song& song) {
    Clip c;
    c.id = song.newId<ClipId>();
    c.length = kBeat;
    c.loopStart = 0;
    c.loopEnd = kBeat;
    Note a;
    a.id = song.newId<NoteId>();
    a.start = 0;
    a.length = kBeat / 4;
    a.pitch = 60;
    Note b = a;
    b.id = song.newId<NoteId>();
    b.start = kBeat / 2;
    b.length = kBeat;
    b.pitch = 64;
    c.notes = {a, b};
    return c;
}

ArrangerClip instance(Song& song, ClipId clip, Tick position, Tick length, Tick offset = 0) {
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = clip;
    a.position = position;
    a.length = length;
    a.offset = offset;
    return a;
}

std::vector<std::pair<Tick, Tick>> spans(const std::vector<TimedNote>& notes) {
    std::vector<std::pair<Tick, Tick>> out;
    for (const TimedNote& n : notes)
        out.push_back({n.start, n.end});
    return out;
}

std::vector<TimedNote> render(const Clip& clip, const ArrangerClip& a) {
    std::vector<TimedNote> out;
    renderInstance(clip, a, out);
    return out;
}

/// What the whole song's arrangement plays on one track, as (start, end, pitch).
std::vector<std::tuple<Tick, Tick, int>> played(const Song& song, TrackId track) {
    std::vector<TimedNote> notes;
    for (const ArrangerClip& a : song.findTrack(track)->arrangement)
        if (!a.muted)
            renderInstance(*song.findClip(a.clip), a, notes);
    std::vector<std::tuple<Tick, Tick, int>> out;
    for (const TimedNote& n : notes)
        out.push_back({n.start, n.end, n.pitch});
    std::sort(out.begin(), out.end());
    return out;
}

/// A song with two tracks and the beat clip in the pool.
struct Fixture {
    Song song = makeDefaultSong();
    ClipId clip;
    TrackId t0, t1;

    Fixture() {
        addDemoClip(song);
        Clip c = beatClip(song);
        clip = c.id;
        song.clips[clip] = c;
        t0 = song.tracks[0].id;
        t1 = song.tracks[1].id;
    }
    IdSource ids() {
        return [this] { return song.newId<ArrangementId>(); };
    }
    NextId next() {
        return [this] { return song.nextId++; };
    }
    std::vector<TrackId> order() const {
        std::vector<TrackId> o;
        for (const Track& t : song.tracks)
            o.push_back(t.id);
        return o;
    }
    ArrangementId add(TrackId track, Tick position, Tick length, Tick offset = 0) {
        ArrangerClip a = instance(song, clip, position, length, offset);
        song.findTrack(track)->arrangement.push_back(a);
        normalize(song);
        return a.id;
    }
    void perform(CommandPtr c) {
        REQUIRE(c);
        Song original = song;
        c->apply(song);
        Song applied = song;
        c->revert(song);
        Song expected = original;
        expected.nextId = song.nextId;
        REQUIRE(song == expected);
        c->apply(song);
        REQUIRE(song == applied);
    }
};

} // namespace

TEST_CASE("an instance repeats its clip's loop, cutting notes at each seam and at its end") {
    Song song;
    Clip c = beatClip(song);
    ArrangerClip a = instance(song, c.id, kBar, 2 * kBeat + kBeat / 2);
    CHECK(spans(render(c, a)) == std::vector<std::pair<Tick, Tick>>{{kBar, kBar + 240},
                                                                    {kBar + 480, kBar + 960},
                                                                    {kBar + 960, kBar + 1200},
                                                                    {kBar + 1440, kBar + 1920},
                                                                    {kBar + 1920, kBar + 2160}});
    // The instance ends at kBar + 2400, so the note at 2400 (the next half beat) is not played.
}

TEST_CASE("offset, stretch, transpose and velocity scale shape what an instance plays") {
    Song song;
    Clip c = beatClip(song);
    ArrangerClip a = instance(song, c.id, 0, kBeat, kBeat / 2);
    std::vector<TimedNote> notes = render(c, a);
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].start == 0); // the note at 1/2 beat plays first
    CHECK(notes[0].pitch == 64);
    CHECK(notes[1].start == kBeat / 2); // then the loop comes round to the note at 0

    ArrangerClip slow = instance(song, c.id, 0, 2 * kBeat);
    slow.stretch = 2;
    slow.transpose = 12;
    slow.velocityScale = 0.5f;
    notes = render(c, slow);
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].end == 480);
    CHECK(notes[1].start == kBeat);
    CHECK(notes[0].pitch == 72);
    CHECK(notes[0].velocity == doctest::Approx(0.4f));
}

TEST_CASE("a non-looping clip plays once, then the instance is silent") {
    Song song;
    Clip c = beatClip(song);
    c.loopMode = LoopMode::Off;
    ArrangerClip a = instance(song, c.id, 0, 4 * kBeat);
    CHECK(render(c, a).size() == 2);
    CHECK(clipTimeAt(c, a, 100) == doctest::Approx(100.0));
    CHECK_FALSE(clipTimeAt(c, a, double(2 * kBeat)).has_value());
    CHECK(loopSeams(c, a).empty());
}

TEST_CASE("a point of a clip maps back to the song, in the loop pass the playhead is in") {
    Song song;
    Clip c = beatClip(song);
    // One-beat loop, placed at bar 2 for three beats, starting half a beat into the clip.
    ArrangerClip a = instance(song, c.id, kBar, 3 * kBeat, kBeat / 2);
    CHECK(songTickAt(c, a, 600, 0) == doctest::Approx(double(kBar + 120)));
    // Before the offset: the first pass never plays it, the second does.
    CHECK(songTickAt(c, a, 100, 0) == doctest::Approx(double(kBar + 580)));
    // With the playhead in the third pass, the same point of the clip lands in that pass.
    CHECK(songTickAt(c, a, 100, double(kBar + 1450)) == doctest::Approx(double(kBar + 1540)));
    for (double u : {0.0, 100.0, 479.0, 480.0, 900.0}) {
        CAPTURE(u);
        std::optional<double> tick = songTickAt(c, a, u, double(kBar + 2000));
        REQUIRE(tick);
        CHECK(clipTimeAt(c, a, *tick) == doctest::Approx(u));
    }
    CHECK_FALSE(songTickAt(c, a, double(kBeat), 0).has_value()); // past the clip

    Clip once = c;
    once.loopMode = LoopMode::Off;
    ArrangerClip b = instance(song, once.id, 0, 4 * kBeat);
    CHECK(songTickAt(once, b, 100, double(3 * kBeat)) == doctest::Approx(100.0));
}

TEST_CASE("clip time and loop seams follow the offset and the stretch") {
    Song song;
    Clip c = beatClip(song);
    ArrangerClip a = instance(song, c.id, kBar, 3 * kBeat, kBeat / 2);
    CHECK(clipTimeAt(c, a, double(kBar)) == doctest::Approx(480.0));
    CHECK(clipTimeAt(c, a, double(kBar + 480)) == doctest::Approx(0.0));
    CHECK_FALSE(clipTimeAt(c, a, double(kBar - 1)).has_value());
    CHECK(loopSeams(c, a) == std::vector<Tick>{kBar + 480, kBar + 1440, kBar + 2400});
    CHECK(normalizeOffset(c, -kBeat / 4) == 720);
    CHECK(normalizeOffset(c, kBeat + 10) == 10);
}

TEST_CASE("moving instances: across tracks, stopping together at the edges") {
    Fixture f;
    ArrangementId a = f.add(f.t0, kBar, kBar);
    ArrangementId b = f.add(f.t1, 2 * kBar, kBar);
    Layout before = layoutOf(f.song);

    Layout moved = move(before, {a, b}, -3 * kBar, f.order(), 1);
    CHECK(find(moved, a)->clip.position == 0); // a reached 0; b kept the same distance
    CHECK(find(moved, b)->clip.position == kBar);
    CHECK(find(moved, a)->track == f.t1);
    CHECK(find(moved, b)->track == f.song.tracks[2].id);

    Layout down = move(before, {a, b}, 0, f.order(), 10);
    CHECK(find(down, b)->track == f.song.tracks.back().id);
    CHECK(find(down, a)->track == f.song.tracks[f.song.tracks.size() - 2].id);

    f.perform(replaceArrangement(f.song, "Move", before, moved));
    CHECK(f.song.findTrack(f.t1)->arrangement.size() == 1);
}

TEST_CASE("trimming the start keeps the content in place; splitting changes nothing you hear") {
    Fixture f;
    ArrangementId a = f.add(f.t0, kBar, 2 * kBar);
    auto heard = played(f.song, f.t0);

    Layout before = layoutOf(f.song);
    Layout trimmed = trimStart(before, {a}, kBeat / 2, 1, f.song.clips);
    CHECK(find(trimmed, a)->clip.position == kBar + kBeat / 2);
    CHECK(find(trimmed, a)->clip.offset == kBeat / 2);
    f.perform(replaceArrangement(f.song, "Trim", before, trimmed));
    auto after = played(f.song, f.t0);
    CHECK(std::includes(heard.begin(), heard.end(), after.begin(), after.end()));
    CHECK(after.size() == heard.size() - 1); // only the first short note was cut away

    Layout cut = split(layoutOf(f.song), {a}, kBar + kBar, f.song.clips, f.ids());
    CHECK(cut.size() == 2);
    f.perform(replaceArrangement(f.song, "Split", layoutOf(f.song), cut));
    CHECK(played(f.song, f.t0) == after);
}

TEST_CASE("extend loops the content; Alt+edge stretches it; slip slides it") {
    Fixture f;
    ArrangementId a = f.add(f.t0, 0, kBeat);
    Layout before = layoutOf(f.song);

    Layout longer = resizeEnd(before, {a}, kBeat, 1);
    CHECK(find(longer, a)->clip.length == 2 * kBeat);
    CHECK(find(longer, a)->clip.stretch == 1);

    Layout stretched = stretchEnd(before, {a}, kBeat, 1);
    CHECK(find(stretched, a)->clip.length == 2 * kBeat);
    CHECK(find(stretched, a)->clip.stretch == doctest::Approx(2.f));

    Layout slipped = slip(before, {a}, kBeat / 4, f.song.clips);
    CHECK(find(slipped, a)->clip.offset == 3 * kBeat / 4); // wrapped around the loop
    CHECK(find(slipped, a)->clip.position == 0);
}

TEST_CASE("overlaps: the moved instance wins, others are trimmed, removed or split around it") {
    Fixture f;
    ArrangementId longOne = f.add(f.t0, 0, 4 * kBar);
    ArrangementId small = f.add(f.t1, kBar, kBeat);
    Layout moved = move(layoutOf(f.song), {small}, 0, f.order(), -1);
    Layout resolved = resolveOverlaps(moved, {small}, f.song.clips, f.ids());
    // The long instance is split around the small one.
    std::vector<ArrangerClip> onTrack;
    for (const Placed& p : resolved)
        if (p.track == f.t0)
            onTrack.push_back(p.clip);
    std::sort(onTrack.begin(), onTrack.end(),
              [](const ArrangerClip& x, const ArrangerClip& y) { return x.position < y.position; });
    REQUIRE(onTrack.size() == 3);
    CHECK(onTrack[0].id == longOne);
    CHECK(instanceEnd(onTrack[0]) == kBar);
    CHECK(onTrack[1].id == small);
    CHECK(onTrack[2].position == kBar + kBeat);
    CHECK(instanceEnd(onTrack[2]) == 4 * kBar);

    Layout covering = resizeEnd(resolved, {small}, 10 * kBar, 1);
    Layout covered = resolveOverlaps(covering, {small}, f.song.clips, f.ids());
    CHECK(pick(covered, {onTrack[2].id}).empty()); // fully covered: gone
    f.perform(replaceArrangement(f.song, "Move", layoutOf(f.song), covered));
}

TEST_CASE("duplicates are linked; deleting the last use of a clip removes it until undo") {
    Fixture f;
    ArrangementId a = f.add(f.t0, 0, kBar);
    IdSet added;
    Layout dup = duplicate(layoutOf(f.song), {a}, kBar, f.order(), 0, f.ids(), &added);
    REQUIRE(added.size() == 1);
    CHECK(find(dup, *added.begin())->clip.clip == f.clip);
    CHECK(find(dup, *added.begin())->clip.position == kBar);

    Layout none;
    CommandPtr remove = replaceArrangement(f.song, "Delete", layoutOf(f.song), none);
    remove->apply(f.song);
    CHECK(f.song.findClip(f.clip) == nullptr);
    remove->revert(f.song);
    CHECK(f.song.findClip(f.clip) != nullptr);
}

TEST_CASE("glue bakes several instances into one clip that plays the same") {
    Fixture f;
    ArrangementId a = f.add(f.t0, 0, kBeat + kBeat / 2);
    ArrangementId b = f.add(f.t0, 2 * kBeat, kBeat);
    Layout l = transpose(layoutOf(f.song), {b}, 5);
    f.perform(replaceArrangement(f.song, "Transpose", layoutOf(f.song), l));
    auto heard = played(f.song, f.t0);

    f.perform(glue(f.song, {a, b}, f.next()));
    const Track& t = *f.song.findTrack(f.t0);
    REQUIRE(t.arrangement.size() == 1);
    CHECK(t.arrangement[0].position == 0);
    CHECK(t.arrangement[0].length == 3 * kBeat);
    CHECK(played(f.song, f.t0) == heard);
    CHECK(f.song.findClip(f.clip) == nullptr); // nothing used the old clip any more
}

TEST_CASE("make unique gives a shared clip's instance its own copy") {
    Fixture f;
    ArrangementId a = f.add(f.t0, 0, kBeat);
    f.add(f.t1, 0, kBeat);
    f.perform(makeUnique(f.song, {a}, f.next()));
    ClipId own = f.song.findTrack(f.t0)->arrangement[0].clip;
    CHECK(own != f.clip);
    CHECK(f.song.findClip(own)->notes.size() == f.song.findClip(f.clip)->notes.size());
    CHECK(makeUnique(f.song, {a}, f.next()) == nullptr); // already unique
}

TEST_CASE("placing a clip plays its loop once and clears the space it lands on") {
    Fixture f;
    f.add(f.t0, 0, 4 * kBar);
    f.perform(placeClip(f.song, f.t0, f.clip, kBar, f.ids()));
    const Track& t = *f.song.findTrack(f.t0);
    REQUIRE(t.arrangement.size() == 3);
    CHECK(t.arrangement[1].position == kBar);
    CHECK(t.arrangement[1].length == kBeat);
}

#include "../src/engine/ArrangementCapture.hpp"

TEST_CASE("captured launcher playback lands on the timeline, later passes winning") {
    Fixture f;
    CaptureBuilder capture;
    uint64_t t0 = f.t0.value, clip = f.clip.value;
    capture.add({t0, clip, double(kBar), double(kBeat / 2)}); // started mid-clip
    capture.add({t0, 0, double(2 * kBar), 0});                // stopped
    capture.add({t0, clip, double(kBar + kBeat), 0});         // the loop came round: plays over part of it
    capture.closeAll(double(kBar + 2 * kBeat));
    std::vector<CapturedSegment> segments = capture.take();
    REQUIRE(segments.size() == 2);
    CHECK(capture.empty());

    f.perform(recordArrangement(f.song, segments, f.ids()));
    const auto& arr = f.song.findTrack(f.t0)->arrangement;
    REQUIRE(arr.size() == 3);
    CHECK(arr[0].position == kBar);
    CHECK(arr[0].offset == kBeat / 2);
    CHECK(instanceEnd(arr[0]) == kBar + kBeat);
    CHECK(arr[1].position == kBar + kBeat);
    CHECK(arr[1].length == kBeat);
    CHECK(arr[2].position == kBar + 2 * kBeat);
    CHECK(instanceEnd(arr[2]) == 2 * kBar);
}

TEST_CASE("drawing a clip makes an empty clip and its instance in one step") {
    Fixture f;
    ClipId clip = f.song.newId<ClipId>();
    ArrangementId id = f.song.newId<ArrangementId>();
    f.perform(drawClip(f.song, f.t1, kBar, 2 * kBar, clip, id, f.ids()));
    const Clip* c = f.song.findClip(clip);
    REQUIRE(c != nullptr);
    CHECK(c->notes.empty());
    CHECK(c->loopEnd == 2 * kBar);
    CHECK(f.song.findTrack(f.t1)->arrangement.at(0).id == id);
}

TEST_CASE("stacked clips: overlapping instances take the lowest free sub-row") {
    Fixture f;
    ArrangementId a = f.add(f.t0, 0, 4 * kBeat);
    ArrangementId b = f.add(f.t0, kBeat, 2 * kBeat);     // overlaps a
    ArrangementId c = f.add(f.t0, 2 * kBeat, 4 * kBeat); // overlaps a and b
    ArrangementId d = f.add(f.t0, 4 * kBeat, kBeat);     // a has ended: back on the first row
    ArrangementId other = f.add(f.t1, kBeat, kBeat);
    Stacking s = stackOf(layoutOf(f.song), f.t0);
    CHECK(s.rows == 3);
    CHECK(s.row[a] == 0);
    CHECK(s.row[b] == 1);
    CHECK(s.row[c] == 2);
    CHECK(s.row[d] == 0);
    CHECK(s.row.count(other) == 0);
    CHECK(stackOf(layoutOf(f.song), f.t1).rows == 1);
}

TEST_CASE("stacked clips: placing over another clip keeps both, and both play") {
    Fixture f;
    f.add(f.t0, 0, 2 * kBeat);
    f.perform(placeClip(f.song, f.t0, f.clip, kBeat, f.ids(), true));
    REQUIRE(f.song.findTrack(f.t0)->arrangement.size() == 2);
    auto notes = played(f.song, f.t0);
    // The second instance's first note starts while the first instance still plays.
    CHECK(std::count_if(notes.begin(), notes.end(), [](const auto& n) { return std::get<0>(n) == kBeat; }) == 2);

    // Without stacking the new clip trims the old one.
    Fixture g;
    g.add(g.t0, 0, 2 * kBeat);
    g.perform(placeClip(g.song, g.t0, g.clip, kBeat, g.ids()));
    const auto& arr = g.song.findTrack(g.t0)->arrangement;
    REQUIRE(arr.size() == 2);
    CHECK(arr[0].length == kBeat);
}

TEST_CASE("stacked clips: automatic voices add up while clips overlap") {
    Fixture f;
    f.song.findTrack(f.t0)->polyphony = kAutoVoices;
    for (ClipSlot& s : f.song.findTrack(f.t0)->slots)
        s.clip.reset(); // only the timeline counts here
    f.add(f.t0, 0, 2 * kBeat);
    int alone = f.song.voicesOf(*f.song.findTrack(f.t0));
    f.add(f.t0, kBeat, 2 * kBeat);
    CHECK(f.song.voicesOf(*f.song.findTrack(f.t0)) == 2 * alone);
    // Back to back is not overlapping.
    Fixture g;
    g.song.findTrack(g.t0)->polyphony = kAutoVoices;
    for (ClipSlot& s : g.song.findTrack(g.t0)->slots)
        s.clip.reset();
    g.add(g.t0, 0, kBeat);
    g.add(g.t0, kBeat, kBeat);
    CHECK(g.song.voicesOf(*g.song.findTrack(g.t0)) == alone);
}

TEST_CASE("a clip without a loop plays up to its last note, whatever its loop region") {
    Clip c;
    c.id = ClipId(1);
    c.length = c.loopEnd = 4 * kPpq; // created as one bar
    c.loopMode = LoopMode::Off;
    Note n;
    n.id = NoteId(2);
    n.start = 6 * kPpq;
    n.length = kPpq;
    c.notes.push_back(n);
    CHECK(c.contentEnd() == 7 * kPpq);
    CHECK(clipSpan(c).end == 7 * kPpq);

    // An arranger instance cut longer in the track plays the late note, and nothing after it.
    ArrangerClip a;
    a.clip = c.id;
    a.position = 0;
    a.length = 16 * kPpq;
    std::vector<TimedNote> out;
    renderInstance(c, a, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].start == 6 * kPpq);

    c.loopMode = LoopMode::Loop;
    CHECK(clipSpan(c).end == 4 * kPpq);
}

TEST_CASE("scaleClip: x2 and x1/2 rewrite notes, loop, automation and every timeline use, as one undo step") {
    // Trello: "Clip: Scale by 200%, 50% and so on (maybe even custom)".
    Fixture f;
    Clip& c = f.song.clips[f.clip];
    AutomationEnvelope lane;
    lane.id = f.song.newId<AutomationId>();
    AutoPoint a{}, b{};
    b.t = kBeat;
    b.value = 1;
    lane.points = {a, b};
    c.automation.push_back(lane);
    f.add(f.t0, 0, 4 * kBeat);
    f.add(f.t1, kBar, 2 * kBeat, kBeat / 2);
    Song original = f.song;

    CommandPtr twice = arrange::scaleClip(f.song, f.clip, 2);
    REQUIRE(twice);
    twice->apply(f.song);
    const Clip& scaled = f.song.clips[f.clip];
    CHECK(scaled.loopEnd == 2 * kBeat);
    CHECK(scaled.length == 2 * kBeat);
    CHECK(scaled.notes[1].start == kBeat);
    CHECK(scaled.notes[1].length == 2 * kBeat);
    CHECK(scaled.automation[0].points[1].t == 2 * kBeat);
    CHECK(f.song.findTrack(f.t0)->arrangement.back().length == 8 * kBeat);
    CHECK(f.song.findTrack(f.t0)->arrangement.back().position == 0);
    CHECK(f.song.findTrack(f.t1)->arrangement.back().length == 4 * kBeat);
    CHECK(f.song.findTrack(f.t1)->arrangement.back().offset == kBeat);
    twice->revert(f.song);
    CHECK(f.song == original);

    CommandPtr half = arrange::scaleClip(f.song, f.clip, 0.5);
    REQUIRE(half);
    half->apply(f.song);
    CHECK(f.song.clips[f.clip].loopEnd == kBeat / 2);
    CHECK(f.song.clips[f.clip].notes[0].length == kBeat / 8);
    CHECK(f.song.findTrack(f.t0)->arrangement.back().length == 2 * kBeat);

    CHECK_FALSE(arrange::scaleClip(f.song, f.clip, 1));
}
