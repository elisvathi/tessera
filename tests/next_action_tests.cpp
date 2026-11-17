#include "doctest.h"

#include "../src/engine/Engine.hpp"
#include "../src/model/Json.hpp"
#include "../src/model/NextAction.hpp"
#include "../src/model/Serialize.hpp"

#include <cmath>
#include <set>
#include <vector>

using namespace tessera;

namespace {

using Kind = NextActionTarget::Kind;

/// Where `action` goes from `current` on a track whose slots are filled as `filled` says ('x' = a clip).
NextActionTarget pick(NextAction action, const char* filled, int current, int previous = -1, double random = 0,
                      NextActionScope scope = NextActionScope::Track) {
    std::vector<bool> f;
    for (const char* c = filled; *c; c++)
        f.push_back(*c == 'x');
    return pickNextAction(action, scope, f, current, previous, random);
}

bool launches(NextActionTarget t, int slot) { return t.kind == Kind::Launch && t.slot == slot; }

} // namespace

TEST_CASE("next actions: next, previous, first and last skip empty slots and stop at the ends") {
    CHECK(launches(pick(NextAction::Next, "x.xx", 0), 2));
    CHECK(pick(NextAction::Next, "x.xx", 3).kind == Kind::Stop);
    CHECK(launches(pick(NextAction::Previous, "x.xx", 2), 0));
    CHECK(pick(NextAction::Previous, "x.xx", 0).kind == Kind::Stop);
    CHECK(launches(pick(NextAction::First, ".xxx", 3), 1));
    CHECK(launches(pick(NextAction::Last, "xxx.", 0), 2));
    CHECK(launches(pick(NextAction::Last, "xxx.", 2), 2)); // the last one starts over
    CHECK(pick(NextAction::None, "xx", 0).kind == Kind::Keep);
    CHECK(pick(NextAction::Stop, "xx", 0).kind == Kind::Stop);
}

TEST_CASE("next actions: round robin wraps to the first clip") {
    CHECK(launches(pick(NextAction::RoundRobin, ".x.x", 1), 3));
    CHECK(launches(pick(NextAction::RoundRobin, ".x.x", 3), 1));
    CHECK(launches(pick(NextAction::RoundRobin, "x", 0), 0)); // alone: it starts over
}

TEST_CASE("next actions: random can pick the playing clip, other never does") {
    std::set<int> random, other;
    for (int i = 0; i < 30; i++) {
        double r = i / 30.0;
        random.insert(pick(NextAction::Random, "xx.x", 0, -1, r).slot);
        other.insert(pick(NextAction::Other, "xx.x", 0, -1, r).slot);
    }
    CHECK(random == std::set<int>{0, 1, 3});
    CHECK(other == std::set<int>{1, 3});
    CHECK(pick(NextAction::Other, "x..", 0, -1, 0.5).kind == Kind::Keep); // no other clip: it plays on
    CHECK(launches(pick(NextAction::Random, "xx", 0, -1, 1.0), 1));       // a number of 1 still lands in range
}

TEST_CASE("next actions: back goes to the clip that played before, or stops") {
    CHECK(launches(pick(NextAction::Back, "xxx", 0, 2), 2));
    CHECK(pick(NextAction::Back, "xxx", 0, -1).kind == Kind::Stop);
    CHECK(pick(NextAction::Back, "x.x", 0, 1).kind == Kind::Stop); // its slot is empty now
}

TEST_CASE("next actions: a block is the run of clips between empty slots") {
    auto block = NextActionScope::Block;
    CHECK(pick(NextAction::Next, "xx.xx", 1, -1, 0, block).kind == Kind::Stop);
    CHECK(launches(pick(NextAction::RoundRobin, "xx.xx", 4, -1, 0, block), 3));
    CHECK(launches(pick(NextAction::First, "xx.xx", 4, -1, 0, block), 3));
    CHECK(launches(pick(NextAction::Last, "xx.xx", 0, -1, 0, block), 1));
    for (int i = 0; i < 10; i++) {
        NextActionTarget t = pick(NextAction::Random, "xx.xxx", 4, -1, i / 10.0, block);
        CHECK(t.slot >= 3);
    }
    CHECK(launches(pick(NextAction::Back, "xx.xx", 4, 0, 0, block), 0)); // back is about what played, not where
}

TEST_CASE("next actions: the chance picks between the two actions") {
    NextActionSettings s;
    s.action = NextAction::Next;
    s.otherwise = NextAction::Other;
    s.chance = 75;
    CHECK(rollNextAction(s, 0.74) == NextAction::Next);
    CHECK(rollNextAction(s, 0.75) == NextAction::Other);
    s.chance = 100;
    CHECK(rollNextAction(s, 0.999) == NextAction::Next);
    CHECK(s.any());
    s.action = NextAction::None;
    CHECK_FALSE(s.any()); // 100% "keep looping": the "otherwise" never comes up
    s.chance = 50;
    CHECK(s.any());
}

TEST_CASE("next actions are saved with the clip, and a clip without one saves nothing new") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    Clip& clip = song.clips.begin()->second;
    json_t* plain = clipToJson(clip);
    CHECK(json_object_get(plain, "next") == nullptr);
    json_decref(plain);

    clip.next.action = NextAction::RoundRobin;
    clip.next.otherwise = NextAction::Back;
    clip.next.chance = 60;
    clip.next.count = 8;
    clip.next.unit = NextActionUnit::Bars;
    clip.next.scope = NextActionScope::Block;
    json_t* j = clipToJson(clip);
    Clip back = clipFromJson(j);
    json_decref(j);
    CHECK(back.next == clip.next);
}

// ---- In the engine ------------------------------------------------------------------------------------------

namespace {

// 93.75 BPM at 48 kHz is exactly 1/32 tick per sample, so expected sample numbers are exact.
constexpr double kSampleRate = 48000;
constexpr double kBpm = 93.75;
constexpr int64_t kSamplesPerTick = 32;
constexpr int64_t kBar = 4 * kPpq * kSamplesPerTick; // samples; each test clip is one bar long

/// One mono track with a one-bar clip in each slot whose pitch is set ('.' = an empty slot). Slot 0 starts playing.
struct ActionHarness {
    Song song;
    BusSlots slots;
    std::unique_ptr<Snapshot> snapshot;
    Engine engine;
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    int64_t sample = -1;

    explicit ActionHarness(const std::vector<int>& pitches) {
        Track track;
        track.id = song.newId<TrackId>();
        track.polyphony = 1;
        for (int pitch : pitches) {
            track.slots.emplace_back();
            song.scenes.emplace_back();
            if (pitch < 0)
                continue;
            Clip clip;
            clip.id = song.newId<ClipId>();
            clip.length = clip.loopEnd = 4 * kPpq;
            Note n;
            n.id = song.newId<NoteId>();
            n.start = 0;
            n.length = 100;
            n.pitch = int8_t(pitch);
            clip.notes.push_back(n);
            track.slots.back().clip = clip.id;
            song.clips.emplace(clip.id, clip);
        }
        song.tracks.push_back(track);
    }
    Clip& clip(int slot) { return song.clips.at(*song.tracks[0].slots[size_t(slot)].clip); }
    void start() {
        slots.sync(song);
        snapshot = buildSnapshot(song, slots);
        engine.setSampleRate(kSampleRate);
        engine.setTempo(kBpm);
        engine.setSnapshot(snapshot.get());
        engine.launch({LaunchEvent::Kind::Slot, song.tracks[0].id.value, 0, true});
    }
    void runTo(int64_t target) {
        while (sample < target) {
            engine.process(frames.data(), events);
            sample++;
        }
    }
    int playing() const { return engine.playingSlot(0); }
    int queued() const { return engine.queuedSlot(0); }
};

} // namespace

TEST_CASE("a clip plays its count, shows what comes next a launch line early, then switches on the exact sample") {
    ActionHarness h({60, 62, 64});
    h.clip(0).next.action = NextAction::Next;
    h.clip(0).next.count = 3;
    h.start();
    h.runTo(2 * kBar - 2);
    CHECK(h.queued() == kNothingQueued);
    CHECK(h.engine.actionElapsed(0) == doctest::Approx(double(2 * kBar - 1) / kSamplesPerTick));
    h.runTo(2 * kBar + 1); // one bar (the launch line) before the third loop ends
    CHECK(h.queued() == 1);
    CHECK(h.playing() == 0);
    h.runTo(3 * kBar - 1);
    CHECK(h.playing() == 0);
    h.runTo(3 * kBar);
    CHECK(h.playing() == 1);
    CHECK(h.queued() == kNothingQueued);
    CHECK(h.frames[h.slots.slotOf(h.song.tracks[0].id)].gate[0] > 5.f);
    CHECK(h.frames[h.slots.slotOf(h.song.tracks[0].id)].voct[0] == doctest::Approx(2 / 12.f));
    CHECK(std::isnan(h.engine.actionElapsed(0))); // slot 1 has no next action
}

TEST_CASE("next actions: the last clip's Next stops the track; bars count the song's bars") {
    ActionHarness h({60, 62});
    h.clip(0).next.action = NextAction::Next;
    h.clip(0).next.count = 2;
    h.clip(0).next.unit = NextActionUnit::Bars;
    h.clip(0).length = h.clip(0).loopEnd = 2 * kPpq; // half a bar: two bars are four loops
    h.clip(1).next.action = NextAction::Next;
    h.clip(1).next.count = 1;
    h.start();
    h.runTo(2 * kBar);
    CHECK(h.playing() == 1);
    h.runTo(3 * kBar);
    CHECK(h.playing() == kNoSlot);
}

TEST_CASE("next actions: keep looping rolls again after each count, and a launch by hand wins") {
    ActionHarness h({60, 62, 64});
    h.clip(0).next.action = NextAction::None;
    h.clip(0).next.otherwise = NextAction::Next;
    h.clip(0).next.chance = 100;
    h.clip(0).next.count = 1;
    h.clip(1).next.action = NextAction::Next;
    h.clip(1).next.count = 4;
    h.start();
    h.runTo(5 * kBar);
    CHECK(h.playing() == 0); // 100% keep looping: no next action is set at all
    CHECK(std::isnan(h.engine.actionElapsed(0)));

    ActionHarness h2({60, 62, 64});
    h2.clip(0).next.action = NextAction::Next;
    h2.clip(0).next.count = 2;
    h2.start();
    h2.runTo(kBar + 10);
    CHECK(h2.queued() == 1);
    h2.engine.launch({LaunchEvent::Kind::Slot, h2.song.tracks[0].id.value, 2});
    CHECK(h2.queued() == 2);
    h2.runTo(2 * kBar + 1);
    CHECK(h2.playing() == 2);
}

TEST_CASE("next actions: a chance of 0 always takes the other action, and keep looping starts the count over") {
    ActionHarness h({60, 62});
    h.clip(0).next.action = NextAction::Next;
    h.clip(0).next.otherwise = NextAction::None;
    h.clip(0).next.chance = 0;
    h.clip(0).next.count = 1;
    h.start();
    h.runTo(3 * kBar + 10);
    CHECK(h.playing() == 0);
    CHECK(h.queued() == kNothingQueued);
    CHECK(h.engine.actionElapsed(0) == doctest::Approx(double(10 + 1) / kSamplesPerTick));
}

TEST_CASE("next actions: held, every clip keeps looping and a picked launch is called off") {
    ActionHarness h({60, 62});
    h.clip(0).next.action = NextAction::Next;
    h.clip(0).next.count = 2;
    h.start();
    h.runTo(kBar + 10);
    CHECK(h.queued() == 1);
    h.engine.setNextActions(false);
    CHECK(h.queued() == kNothingQueued);
    h.runTo(6 * kBar);
    CHECK(h.playing() == 0);
    // Back on: the count carries on from the last whole count, so the clip moves on at a count's end.
    h.engine.setNextActions(true);
    h.runTo(8 * kBar - 1);
    CHECK(h.playing() == 0);
    h.runTo(8 * kBar);
    CHECK(h.playing() == 1);
}

TEST_CASE("next actions: back returns to the clip that played before") {
    ActionHarness h({60, 62});
    h.clip(1).next.action = NextAction::Back;
    h.clip(1).next.count = 1;
    h.start();
    h.runTo(10);
    h.engine.launch({LaunchEvent::Kind::Slot, h.song.tracks[0].id.value, 1});
    h.runTo(kBar);
    CHECK(h.playing() == 1);
    h.runTo(2 * kBar);
    CHECK(h.playing() == 0);
}

TEST_CASE("next actions: a clip that plays once does its action when it ends") {
    ActionHarness h({60, 62});
    h.clip(0).loopMode = LoopMode::Off;
    h.clip(0).next.action = NextAction::Next;
    h.clip(0).next.count = 4; // ignored: a one-shot plays once
    h.start();
    // The clip's content ends with its note, 100 ticks in.
    h.runTo(100 * kSamplesPerTick - 2);
    CHECK(h.playing() == 0);
    CHECK(h.queued() == 1);
    h.runTo(100 * kSamplesPerTick + 1);
    CHECK(h.playing() == 1);
}

TEST_CASE("next actions: set on a clip that already plays, it counts from the loop it is in; removed, it is called off") {
    ActionHarness h({60, 62});
    h.start();
    h.runTo(kBar + kBar / 2); // halfway through the second loop
    h.clip(0).next.action = NextAction::Next;
    h.clip(0).next.count = 1;
    auto withAction = buildSnapshot(h.song, h.slots);
    h.engine.setSnapshot(withAction.get());
    h.runTo(kBar + kBar / 2 + 10);
    CHECK(h.queued() == 1);
    h.runTo(2 * kBar);
    CHECK(h.playing() == 1);

    // Taken off again while its pick waits: the clip plays on.
    ActionHarness h2({60, 62});
    h2.clip(0).next.action = NextAction::Next;
    h2.clip(0).next.count = 2;
    h2.start();
    h2.runTo(kBar + 10);
    CHECK(h2.queued() == 1);
    h2.clip(0).next = NextActionSettings();
    auto without = buildSnapshot(h2.song, h2.slots);
    h2.engine.setSnapshot(without.get());
    CHECK(h2.queued() == kNothingQueued);
    h2.runTo(3 * kBar);
    CHECK(h2.playing() == 0);
}
