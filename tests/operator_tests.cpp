#include "doctest.h"

#include "../src/model/NoteOperators.hpp"

#include <cmath>

using namespace tessera;
using namespace tessera::operators;

namespace {

Note noteWith(NoteOperators ops, uint64_t id = 7) {
    Note n;
    n.id = NoteId(id);
    n.length = kPpq;
    n.velocity = 0.5f;
    n.ops = ops;
    return n;
}

PassContext passNo(int64_t pass) {
    PassContext p;
    p.seed = 42;
    p.clip = 3;
    p.pass = pass;
    return p;
}

} // namespace

TEST_CASE("random is repeatable, in [0, 1), and changes with every input") {
    double a = random(1, 2, 3, 4, 5);
    CHECK(a == random(1, 2, 3, 4, 5));
    CHECK(a >= 0);
    CHECK(a < 1);
    CHECK(a != random(2, 2, 3, 4, 5));
    CHECK(a != random(1, 2, 4, 4, 5));
    CHECK(a != random(1, 2, 3, 5, 5));
}

TEST_CASE("chance plays about that share of passes, the same way each time") {
    NoteOperators ops;
    ops.chance = 0.25f;
    Note n = noteWith(ops);
    int played = 0;
    for (int p = 0; p < 2000; p++)
        played += plays(n, passNo(p));
    CHECK(played > 400);
    CHECK(played < 600);
    for (int p = 0; p < 50; p++)
        CHECK(plays(n, passNo(p)) == plays(n, passNo(p)));
    ops.chance = 0;
    CHECK_FALSE(plays(noteWith(ops), passNo(0)));
}

TEST_CASE("recurrence picks passes of a cycle") {
    NoteOperators ops;
    ops.recurrenceLength = 4;
    ops.recurrenceMask = 0b0101; // passes 1 and 3 of 4
    Note n = noteWith(ops);
    CHECK(plays(n, passNo(0)));
    CHECK_FALSE(plays(n, passNo(1)));
    CHECK(plays(n, passNo(2)));
    CHECK_FALSE(plays(n, passNo(3)));
    CHECK(plays(n, passNo(4)));
}

TEST_CASE("occurrence: first, not first, previous, fill") {
    NoteOperators ops;
    ops.occurrence = Occurrence::First;
    CHECK(plays(noteWith(ops), passNo(0)));
    CHECK_FALSE(plays(noteWith(ops), passNo(1)));
    ops.occurrence = Occurrence::NotFirst;
    CHECK_FALSE(plays(noteWith(ops), passNo(0)));
    PassContext p = passNo(2);
    ops.occurrence = Occurrence::PrevPlayed;
    p.previousPlayed = false;
    CHECK_FALSE(plays(noteWith(ops), p));
    ops.occurrence = Occurrence::PrevNotPlayed;
    CHECK(plays(noteWith(ops), p));
    ops.occurrence = Occurrence::Fill;
    CHECK_FALSE(plays(noteWith(ops), p));
    p.fill = true;
    CHECK(plays(noteWith(ops), p));
    ops.occurrence = Occurrence::NotFill;
    CHECK_FALSE(plays(noteWith(ops), p));
}

TEST_CASE("velocity and timing spread stay within their amounts") {
    NoteOperators ops;
    ops.velocitySpread = 0.2f;
    ops.timingSpread = 0.5f;
    Note n = noteWith(ops);
    bool varied = false;
    for (int p = 0; p < 100; p++) {
        float v = velocity(n, passNo(p));
        CHECK(v >= 0.3f - 1e-6f);
        CHECK(v <= 0.7f + 1e-6f);
        varied |= std::fabs(v - 0.5f) > 0.01f;
        double t = timingOffset(n, passNo(p));
        CHECK(std::fabs(t) <= 0.5 * kSixteenth + 1e-9);
    }
    CHECK(varied);
    CHECK(velocity(noteWith(NoteOperators()), passNo(0)) == 0.5f);
    Note micro = noteWith(NoteOperators());
    micro.microOffset = -0.25f;
    CHECK(microTicks(micro) == doctest::Approx(-60));
}

TEST_CASE("repeats: count, per beat, spacing curve, gate and velocity ramp") {
    NoteOperators ops;
    ops.repeats = 4;
    ops.repeatGate = 0.5f;
    ops.repeatVelEnd = 0;
    Note n = noteWith(ops);
    CHECK(hitCount(n, kPpq) == 4);
    Hit h0 = hit(n, 0, 4), h1 = hit(n, 1, 4), h3 = hit(n, 3, 4);
    CHECK(h0.start == 0);
    CHECK(h1.start == doctest::Approx(kPpq / 4));
    CHECK(h0.length == doctest::Approx(kPpq / 8));
    CHECK(h0.velocity == doctest::Approx(0.5f));
    CHECK(h3.velocity == doctest::Approx(0));

    ops.repeats = -8; // 8 per beat in a one-beat note
    CHECK(hitCount(noteWith(ops), kPpq) == 8);
    ops.repeats = 0;
    CHECK(hitCount(noteWith(ops), kPpq) == 1);
    CHECK(hit(noteWith(ops), 0, 1).length == kPpq);

    // Accelerating: gaps shrink.
    ops.repeats = 6;
    ops.repeatCurve = 1;
    Note acc = noteWith(ops);
    double gap1 = hit(acc, 1, 6).start - hit(acc, 0, 6).start;
    double gap5 = hit(acc, 5, 6).start - hit(acc, 4, 6).start;
    CHECK(gap5 < gap1);
}

// ---- Engine ------------------------------------------------------------------------------------------------

#include "../src/engine/Engine.hpp"

namespace {

constexpr int64_t kSamplesPerTick = 32; // 93.75 BPM at 48 kHz

struct OpRig {
    Song song;
    BusSlots slots;
    std::unique_ptr<Snapshot> snapshot;
    Engine engine;
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    int64_t sample = -1;

    explicit OpRig(std::vector<Note> notes, Tick loop = 4 * kPpq) {
        Track t;
        t.id = song.newId<TrackId>();
        t.polyphony = 4;
        t.slots.resize(1);
        song.scenes.resize(1);
        Clip c;
        c.id = song.newId<ClipId>();
        c.length = c.loopEnd = loop;
        for (Note& n : notes) {
            n.id = song.newId<NoteId>();
            c.notes.push_back(n);
        }
        c.sortNotes();
        t.slots[0].clip = c.id;
        song.clips[c.id] = c;
        song.tracks.push_back(t);
        slots.sync(song);
        snapshot = buildSnapshot(song, slots);
        engine.setSampleRate(48000);
        engine.setTempo(93.75);
        engine.setSnapshot(snapshot.get());
        engine.launch({LaunchEvent::Kind::Scene, 0, 0, true});
    }
    const TrackFrame& frame() const { return frames[size_t(slots.slotOf(song.tracks[0].id))]; }
    bool anyGate() const {
        for (int v = 0; v < 4; v++)
            if (frame().gate[v] > 5)
                return true;
        return false;
    }
    /// Samples where some gate rises, up to `until`.
    std::vector<int64_t> rises(int64_t until) {
        std::vector<int64_t> out;
        bool was = false;
        while (sample < until) {
            engine.process(frames.data(), events);
            sample++;
            bool now = anyGate();
            if (now && !was)
                out.push_back(sample);
            was = now;
        }
        return out;
    }
};

Note plainNote(Tick start, Tick length = 240) {
    Note n;
    n.start = start;
    n.length = length;
    return n;
}

} // namespace

TEST_CASE("engine: a note with chance 0 never plays; chance 1 always does") {
    Note never = plainNote(0);
    never.ops.chance = 0;
    OpRig rig({never});
    rig.engine.play();
    CHECK(rig.rises(4 * kPpq * kSamplesPerTick * 3).empty());
}

TEST_CASE("engine: positive micro-timing delays a note to the exact sample; negative plays it early") {
    Note late = plainNote(480);
    late.microOffset = 0.25f; // +60 ticks
    Note early = plainNote(1920);
    early.microOffset = -0.25f; // -60 ticks
    OpRig rig({late, early});
    rig.engine.play();
    auto rises = rig.rises(2400 * kSamplesPerTick);
    REQUIRE(rises.size() == 2);
    CHECK(rises[0] == 540 * kSamplesPerTick);
    CHECK(rises[1] == 1860 * kSamplesPerTick);
}

TEST_CASE("engine: repeats play several gates inside the note") {
    Note r = plainNote(0, 960);
    r.ops.repeats = 4;
    OpRig rig({r});
    rig.engine.play();
    auto rises = rig.rises(960 * kSamplesPerTick);
    REQUIRE(rises.size() == 4);
    CHECK(rises[1] == 240 * kSamplesPerTick);
    CHECK(rises[3] == 720 * kSamplesPerTick);
}

TEST_CASE("engine: recurrence plays only on chosen passes") {
    Note n = plainNote(0);
    n.ops.recurrenceLength = 2;
    n.ops.recurrenceMask = 0b10; // second of every two passes
    OpRig rig({n}, kPpq);
    rig.engine.play();
    auto rises = rig.rises(4 * kPpq * kSamplesPerTick);
    REQUIRE(rises.size() == 2);
    CHECK(rises[0] == kPpq * kSamplesPerTick);
    CHECK(rises[1] == 3 * kPpq * kSamplesPerTick);
}

TEST_CASE("engine: fill notes play only while FILL is on") {
    Note n = plainNote(0);
    n.ops.occurrence = Occurrence::Fill;
    OpRig rig({n}, kPpq);
    rig.engine.play();
    CHECK(rig.rises(kPpq * kSamplesPerTick).empty());
    rig.engine.setFill(true);
    CHECK(rig.rises(2 * kPpq * kSamplesPerTick).size() == 1);
}

TEST_CASE("engine: timbre and pitch curves follow the note") {
    Note n = plainNote(0, 960);
    n.timbreExpr = {{0, 0, 0}, {1, 1, 0}};
    n.pitchExpr = {{0, 0, 0}, {1, 12, 0}};
    OpRig rig({n});
    rig.engine.play();
    rig.rises(480 * kSamplesPerTick);
    CHECK(rig.frame().timbre[0] == doctest::Approx(5).epsilon(0.01));
    CHECK(rig.frame().voct[0] == doctest::Approx(0.5).epsilon(0.01)); // half an octave up, half way through
}

TEST_CASE("expressionAt interpolates and holds at the ends") {
    std::vector<ExprPoint> c = {{0.2f, 0, 0}, {0.8f, 1, 0}};
    CHECK(expressionAt(c, 0, 9) == 0);
    CHECK(expressionAt(c, 0.5f, 9) == doctest::Approx(0.5f));
    CHECK(expressionAt(c, 1, 9) == 1);
    CHECK(expressionAt({}, 0.5f, 9) == 9);
}
