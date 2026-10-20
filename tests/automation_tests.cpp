#include "doctest.h"

#include "../src/model/AutomationEdits.hpp"

#include <cmath>

using namespace tessera;
using namespace tessera::autoEdits;

namespace {

AutoPoint pt(Tick t, float v, CurveType c = CurveType::Linear, float tension = 0) {
    AutoPoint p;
    p.t = t;
    p.value = v;
    p.curve = c;
    p.tension = tension;
    return p;
}

AutomationEnvelope laneWith(Points points) {
    AutomationEnvelope l;
    l.points = std::move(points);
    return l;
}

} // namespace

TEST_CASE("every curve type starts and ends on its points") {
    for (CurveType c : {CurveType::Linear, CurveType::Exponential, CurveType::SCurve})
        for (float tension : {-1.f, -0.5f, 0.f, 0.5f, 1.f}) {
            CHECK(curveShape(c, tension, 0) == doctest::Approx(0));
            CHECK(curveShape(c, tension, 1) == doctest::Approx(1));
            double prev = 0;
            for (double x = 0.05; x <= 1; x += 0.05) {
                double y = curveShape(c, tension, x);
                CHECK(y >= prev - 1e-9); // never turns back
                prev = y;
            }
        }
    CHECK(curveShape(CurveType::Linear, 0, 0.25) == doctest::Approx(0.25));
    CHECK(curveShape(CurveType::Linear, 0.5f, 0.25) > 0.25); // positive tension rises early
    CHECK(curveShape(CurveType::Linear, -0.5f, 0.25) < 0.25);
    CHECK(curveShape(CurveType::SCurve, 0, 0.5) == doctest::Approx(0.5));
    CHECK(curveShape(CurveType::Exponential, 0, 0.5) < 0.5);
}

TEST_CASE("evaluate: holds before and after, interpolates between, jumps on equal times") {
    AutomationEnvelope empty;
    CHECK(evaluate(empty, 100) == 0);
    empty.bipolar = true;
    CHECK(evaluate(empty, 100) == 0.5f);

    AutomationEnvelope l =
        laneWith({pt(100, 0.2f), pt(200, 0.6f, CurveType::Hold), pt(300, 1), pt(300, 0), pt(400, 0.5f)});
    CHECK(evaluate(l, 0) == doctest::Approx(0.2f));
    CHECK(evaluate(l, 150) == doctest::Approx(0.4f));
    CHECK(evaluate(l, 250) == doctest::Approx(0.6f)); // hold
    CHECK(evaluate(l, 300) == doctest::Approx(0));    // the later of two points at one time wins
    CHECK(evaluate(l, 350) == doctest::Approx(0.25f));
    CHECK(evaluate(l, 1000) == doctest::Approx(0.5f));

    // The playback cursor gives the same answers moving forward, and after jumping back.
    size_t cursor = 0;
    for (double t = 0; t < 500; t += 7)
        CHECK(evaluateAt(l.points, t, cursor, 0) == doctest::Approx(evaluate(l, t)));
    CHECK(evaluateAt(l.points, 120, cursor, 0) == doctest::Approx(evaluate(l, 120)));
}

TEST_CASE("volts follow the lane range") {
    AutomationEnvelope l;
    l.rangeMin = -5;
    l.rangeMax = 5;
    CHECK(toVolts(l, 0.5f) == doctest::Approx(0));
    CHECK(toVolts(l, 1) == doctest::Approx(5));
    CHECK(fromVolts(l, -5) == doctest::Approx(0));
}

TEST_CASE("lanes are added, edited, moved and removed as commands") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    TrackId track = song.tracks[0].id;
    LaneOwner owner = LaneOwner::ofTrack(track);
    Song before = song;
    AutomationEnvelope lane = makeLane(song.newId<AutomationId>(), freeOutput(song.tracks[0].automation), 0x123456);
    CHECK(lane.name == "Auto 1");
    Song afterId = song;
    CommandPtr add = addLane(song, owner, lane);
    add->apply(song);
    REQUIRE(song.tracks[0].automation.size() == 1);
    CHECK(freeOutput(song.tracks[0].automation) == 1);

    AutomationEnvelope edited = lane;
    edited.points = {pt(0, 0.5f)};
    CommandPtr edit = replaceLane(song, owner, edited, "Add point");
    edit->apply(song);
    CHECK(findLane(song, owner, lane.id)->points.size() == 1);
    CHECK(replaceLane(song, owner, edited, "same") == nullptr);

    AutomationEnvelope second = makeLane(song.newId<AutomationId>(), 1, 0);
    CommandPtr add2 = addLane(song, owner, second);
    add2->apply(song);
    CommandPtr move = moveLane(song, owner, second.id, 0);
    move->apply(song);
    CHECK(song.tracks[0].automation[0].id == second.id);
    CommandPtr remove = removeLane(song, owner, lane.id);
    remove->apply(song);
    CHECK(song.tracks[0].automation.size() == 1);

    remove->revert(song);
    move->revert(song);
    add2->revert(song);
    edit->revert(song);
    add->revert(song);
    song.nextId = before.nextId;
    CHECK(song == before);
}

TEST_CASE("point edits: add, move with the selection following, delete, curve") {
    Points p;
    addPoint(p, 200, 0.5f);
    addPoint(p, 100, 0.2f);
    size_t mid = addPoint(p, 150, 2.f);
    CHECK(mid == 1);
    CHECK(p[1].value == 1.f); // clamped
    Selection moved;
    Points q = movePoints(p, {0}, 120, 0.1f, &moved);
    CHECK(q[2].t == 220); // passed the point at 200
    CHECK(moved == Selection{2});
    CHECK(q[2].value == doctest::Approx(0.3f));
    CHECK(deletePoints(p, {0, 2}).size() == 1);
    CHECK(setCurve(p, {1}, CurveType::Hold)[1].curve == CurveType::Hold);

    Points r = {pt(0, 0), pt(100, 1, CurveType::Hold)};
    size_t i = insertOnCurve(r, 50, 0);
    CHECK(r[i].value == doctest::Approx(0.5f));
}

TEST_CASE("simplify keeps corners and drops points on a line") {
    Points samples;
    for (int i = 0; i <= 100; i++)
        samples.push_back(pt(i * 10, i <= 50 ? i / 50.f : (100 - i) / 50.f));
    Points s = simplify(samples, 0.01f);
    REQUIRE(s.size() == 3);
    CHECK(s[1].t == 500);
    CHECK(thin(samples, 0.01f).size() == 3);
}

TEST_CASE("replaceRange, quantize, invert, reverse, randomize") {
    Points p = {pt(0, 0), pt(100, 0.5f), pt(200, 1), pt(300, 0.2f)};
    Points r = replaceRange(p, 50, 250, {pt(60, 0.9f)});
    REQUIRE(r.size() == 3);
    CHECK(r[1].t == 60);
    CHECK(quantizeTimes({pt(0, 0), pt(110, 1)}, {1}, 96)[1].t == 96);
    CHECK(invert(p, {1})[1].value == doctest::Approx(0.5f));
    CHECK(invert(p, {2})[2].value == doctest::Approx(0));
    Points rev = reverse(p, {0, 1, 2, 3});
    CHECK(rev[0].value == doctest::Approx(0.2f));
    CHECK(rev[3].value == doctest::Approx(0));
    CHECK(rev[3].t == 300);
    Points rnd = randomize(p, {0, 1, 2, 3}, 0.2f, 7);
    CHECK(rnd == randomize(p, {0, 1, 2, 3}, 0.2f, 7)); // same seed, same result
    for (size_t i = 0; i < p.size(); i++)
        CHECK(std::fabs(rnd[i].value - p[i].value) <= 0.2f + 1e-6);
}

TEST_CASE("shape stamps fill the range and stay inside low..high") {
    for (int s = 0; s <= int(Shape::ExpFall); s++) {
        Points p = stampShape({pt(0, 0.3f), pt(5000, 0.3f)}, 1000, 3000, Shape(s), 4, 0.2f, 0.8f, 0, 1);
        AutomationEnvelope l = laneWith(p);
        CHECK(p.front().t == 0);
        CHECK(p.back().t == 5000);
        for (const AutoPoint& q : p)
            if (q.t > 0 && q.t < 5000) {
                CHECK(q.t >= 1000);
                CHECK(q.t <= 3000);
                CHECK(q.value >= 0.2f - 1e-5);
                CHECK(q.value <= 0.8f + 1e-5);
            }
        CAPTURE(shapeName(Shape(s)));
    }
    AutomationEnvelope sine = laneWith(stampShape({}, 0, 4000, Shape::Sine, 4, 0, 1, 0, 1));
    CHECK(evaluate(sine, 0) == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(evaluate(sine, 250) == doctest::Approx(1));
    CHECK(evaluate(sine, 750) == doctest::Approx(0));
    AutomationEnvelope square = laneWith(stampShape({}, 0, 1000, Shape::Square, 2, 0, 1, 0, 1));
    CHECK(evaluate(square, 100) == 1);
    CHECK(evaluate(square, 300) == 0);
}

TEST_CASE("transform box: scale, stretch and tilt") {
    Points p = {pt(100, 1), pt(200, 0.5f), pt(300, 1)};
    Points s = scaleSelection(p, {0, 1, 2}, 0, 0.5f, 100, 2);
    CHECK(s[0].value == doctest::Approx(0.5f));
    CHECK(s[2].t == 500);
    Points t = tilt(p, {0, 1, 2}, 0, 0, 1);
    CHECK(t[0].value == doctest::Approx(0));
    CHECK(t[1].value == doctest::Approx(0.25f));
    CHECK(t[2].value == doctest::Approx(1));
}

// ---- Engine ------------------------------------------------------------------------------------------------

#include "../src/engine/Engine.hpp"

namespace {

struct AutoRig {
    Song song;
    BusSlots slots;
    std::unique_ptr<Snapshot> snapshot;
    Engine engine;
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;

    explicit AutoRig(Song s) : song(std::move(s)) {
        slots.sync(song);
        snapshot = buildSnapshot(song, slots);
        engine.setSampleRate(48000);
        engine.setTempo(93.75); // 1/32 tick per sample
        engine.setSnapshot(snapshot.get());
    }
    void run(int64_t samples) {
        for (int64_t i = 0; i < samples; i++)
            engine.process(frames.data(), events);
    }
    const TrackFrame& frame() const { return frames[size_t(slots.slotOf(song.tracks[0].id))]; }
};

Song autoSong() {
    Song song;
    song.scenes.resize(1);
    Track t;
    t.id = song.newId<TrackId>();
    t.slots.resize(1);
    song.tracks.push_back(t);
    return song;
}

} // namespace

TEST_CASE("engine: track lanes play in arranger mode, in volts, on their AUTO channel") {
    Song song = autoSong();
    AutomationEnvelope lane = makeLane(song.newId<AutomationId>(), 2, 0);
    lane.rangeMin = -5;
    lane.rangeMax = 5;
    lane.points = {pt(0, 0), pt(960, 1)};
    song.tracks[0].automation.push_back(lane);
    AutoRig rig(song);
    rig.engine.setPlayMode(PlayMode::Arranger);
    rig.engine.play();
    rig.run(480 * 32); // half way
    CHECK(rig.frame().autoChannels == 3);
    CHECK(rig.frame().automation[2] == doctest::Approx(0).epsilon(0.01));
    rig.run(480 * 32 + 100);
    CHECK(rig.frame().automation[2] == doctest::Approx(5));
    CHECK(rig.engine.automationValue(0, 2) == doctest::Approx(1));
}

TEST_CASE("engine: track lanes also play in launcher mode, along the song") {
    Song song = autoSong();
    AutomationEnvelope lane = makeLane(song.newId<AutomationId>(), 0, 0);
    lane.points = {pt(0, 0), pt(960, 1)};
    song.tracks[0].automation.push_back(lane);
    AutoRig rig(song);
    rig.engine.setPlayMode(PlayMode::Launcher);
    rig.engine.play();
    rig.run(480 * 32);
    CHECK(rig.engine.automationValue(0, 0) == doctest::Approx(0.5).epsilon(0.02));
    rig.run(480 * 32 + 100);
    CHECK(rig.engine.automationValue(0, 0) == doctest::Approx(1));
}

TEST_CASE("engine: a launcher clip's lane overrides or modulates the track lane") {
    for (CombineMode mode : {CombineMode::Override, CombineMode::Modulate}) {
        Song song = autoSong();
        AutomationEnvelope track = makeLane(song.newId<AutomationId>(), 0, 0);
        track.points = {pt(0, 0.6f)};
        song.tracks[0].automation.push_back(track);
        Clip clip;
        clip.id = song.newId<ClipId>();
        clip.length = clip.loopEnd = 4 * kPpq;
        AutomationEnvelope cl = makeLane(song.newId<AutomationId>(), 0, 0);
        cl.combine = mode;
        cl.points = {pt(0, 0.7f)};
        clip.automation.push_back(cl);
        song.clips[clip.id] = clip;
        song.tracks[0].slots[0].clip = clip.id;
        AutoRig rig(song);
        rig.engine.setPlayMode(PlayMode::Arranger);
        rig.engine.play();
        rig.run(10);
        CHECK(rig.frame().automation[0] == doctest::Approx(6)); // the track lane while on the arrangement
        rig.engine.launch({LaunchEvent::Kind::Slot, song.tracks[0].id.value, 0, true});
        rig.run(10);
        // Override: the clip lane alone. Modulate: but the launcher took the track over, so its track lane
        // stops and the clip lane is all that plays.
        CHECK(rig.frame().automation[0] == doctest::Approx(7));
    }
}

TEST_CASE("engine: arranger clip lanes play in clip time and modulate the track lane") {
    Song song = autoSong();
    AutomationEnvelope track = makeLane(song.newId<AutomationId>(), 0, 0);
    track.points = {pt(0, 0.5f)};
    song.tracks[0].automation.push_back(track);
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = kPpq;
    AutomationEnvelope cl = makeLane(song.newId<AutomationId>(), 0, 0);
    cl.combine = CombineMode::Modulate;
    cl.points = {pt(0, 0.5f), pt(kPpq, 0.9f)};
    clip.automation.push_back(cl);
    song.clips[clip.id] = clip;
    ArrangerClip inst;
    inst.id = song.newId<ArrangementId>();
    inst.clip = clip.id;
    inst.position = kPpq;
    inst.length = 2 * kPpq;
    song.tracks[0].arrangement.push_back(inst);
    AutoRig rig(song);
    rig.engine.setPlayMode(PlayMode::Arranger);
    rig.engine.seek(kPpq + kPpq / 2); // half way through the first pass of the clip
    rig.run(1);
    CHECK(rig.frame().automation[0] == doctest::Approx(7)); // 0.5 + (0.7 - 0.5)
    rig.engine.seek(2 * kPpq + kPpq / 2);                   // the second pass: the clip loops
    rig.run(1);
    CHECK(rig.frame().automation[0] == doctest::Approx(7));
    rig.engine.seek(5 * kPpq);
    rig.run(1);
    CHECK(rig.frame().automation[0] == doctest::Approx(5)); // past the instance: the track lane alone
}

TEST_CASE("engine: slew smooths jumps") {
    Song song = autoSong();
    AutomationEnvelope lane = makeLane(song.newId<AutomationId>(), 0, 0);
    lane.slewMs = 10;
    lane.points = {pt(0, 0, CurveType::Hold), pt(100, 1)};
    song.tracks[0].automation.push_back(lane);
    AutoRig rig(song);
    rig.engine.setPlayMode(PlayMode::Arranger);
    rig.engine.seek(200);
    rig.run(48); // 1 ms
    float v = rig.frame().automation[0];
    CHECK(v > 0.5f);
    CHECK(v < 2.f);
    rig.run(48 * 100);
    CHECK(rig.frame().automation[0] == doctest::Approx(10).epsilon(0.001));
}

#include "../src/model/AutomationTake.hpp"

TEST_CASE("automation takes: write records all, latch from the first move, touch while moving") {
    Points old = {pt(0, 0.5f), pt(10000, 0.5f)};
    // A knob still for 10 readings, moving for 10, then still again.
    auto feed = [&](AutomationTake& take) {
        for (int i = 0; i < 40; i++) {
            float v = i < 10 ? 0.2f : i < 20 ? 0.2f + (i - 10) * 0.05f : 0.7f;
            take.sample(i * 100, v, i * 0.1);
        }
    };
    AutomationTake write(AutoRecordMode::Write);
    feed(write);
    REQUIRE(write.written().size() == 1);
    CHECK(write.written()[0].front().t == 0);
    CHECK(write.written()[0].back().t == 3900);

    AutomationTake latch(AutoRecordMode::Latch);
    feed(latch);
    REQUIRE(latch.written().size() == 1);
    CHECK(latch.written()[0].front().t == 1100); // the first change
    CHECK(latch.written()[0].back().t == 3900);

    AutomationTake touch(AutoRecordMode::Touch);
    feed(touch);
    REQUIRE(touch.written().size() == 1);
    CHECK(touch.written()[0].front().t == 1100);
    CHECK(touch.written()[0].back().t <= 2500); // half a second after the last move
    CHECK_FALSE(touch.writing());

    // Applying replaces only the touched stretch and keeps the curve elsewhere.
    AutomationEnvelope lane = laneWith(touch.apply(old));
    CHECK(evaluate(lane, 0) == doctest::Approx(0.5f));
    CHECK(evaluate(lane, 1500) == doctest::Approx(0.45f).epsilon(0.02));
    CHECK(evaluate(lane, 10000) == doctest::Approx(0.5f));
    CHECK(lane.points.size() < 15); // thinned
}

TEST_CASE("automation takes: a touch, a pause and another touch are two stretches") {
    AutomationTake touch(AutoRecordMode::Touch);
    double t = 0;
    float v = 0;
    for (int i = 0; i < 5; i++, t += 0.1)
        touch.sample(Tick(t * 1000), v += 0.1f, t);
    for (int i = 0; i < 20; i++, t += 0.1)
        touch.sample(Tick(t * 1000), v, t);
    for (int i = 0; i < 5; i++, t += 0.1)
        touch.sample(Tick(t * 1000), v -= 0.1f, t);
    CHECK(touch.written().size() == 2);
}
