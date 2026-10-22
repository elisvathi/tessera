#include "doctest.h"

#include "../src/engine/ClockSync.hpp"
#include "../src/engine/Engine.hpp"
#include "../src/engine/SpscQueue.hpp"
#include "../src/engine/TapTempo.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <thread>
#include <vector>

using namespace tessera;

namespace {

// 93.75 BPM at 48 kHz is exactly 1/32 tick per sample, so expected sample numbers are exact.
constexpr double kSampleRate = 48000;
constexpr double kBpm = 93.75;
constexpr int64_t kSamplesPerTick = 32;
constexpr int kRetriggerGap = 48; // 1 ms at 48 kHz

struct NoteSpec {
    Tick start;
    Tick length;
    int pitch;
};

Song songWith(const std::vector<NoteSpec>& notes, Tick loopLength, int polyphony) {
    Song song;
    Track track;
    track.id = song.newId<TrackId>();
    track.polyphony = polyphony;
    track.slots.resize(1);

    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = loopLength;
    clip.loopEnd = loopLength;
    for (const NoteSpec& spec : notes) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = spec.start;
        n.length = spec.length;
        n.pitch = int8_t(spec.pitch);
        clip.notes.push_back(n);
    }
    std::sort(clip.notes.begin(), clip.notes.end(), noteOrder);
    track.slots[0].clip = clip.id;
    song.clips.emplace(clip.id, clip);
    song.tracks.push_back(track);
    return song;
}

/// One mono track whose slot i holds a clip with `clips[i]`; an empty list leaves the slot empty.
Song songWithSlots(const std::vector<std::vector<NoteSpec>>& clips, Tick loopLength) {
    Song song = songWith(clips.empty() ? std::vector<NoteSpec>{} : clips[0], loopLength, 1);
    Track& track = song.tracks[0];
    if (clips.empty() || clips[0].empty()) {
        song.clips.clear();
        track.slots[0].clip.reset();
    }
    for (size_t i = 1; i < clips.size(); i++) {
        track.slots.emplace_back();
        song.scenes.emplace_back();
        if (clips[i].empty())
            continue;
        Song other = songWith(clips[i], loopLength, 1);
        Clip clip = other.clips.begin()->second;
        clip.id = song.newId<ClipId>();
        for (Note& n : clip.notes)
            n.id = song.newId<NoteId>();
        track.slots[i].clip = clip.id;
        song.clips.emplace(clip.id, clip);
    }
    while (song.scenes.size() < track.slots.size())
        song.scenes.emplace_back();
    return song;
}

/// Drives an Engine one sample at a time, like Tessera's process() does.
struct Harness {
    Song song;
    BusSlots slots;
    std::unique_ptr<Snapshot> snapshot;
    Engine engine;
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    int64_t sample = -1; // index of the sample last processed

    explicit Harness(Song s) : song(std::move(s)) {
        slots.sync(song);
        snapshot = buildSnapshot(song, slots);
        engine.setSampleRate(kSampleRate);
        engine.setTempo(kBpm);
        engine.setSnapshot(snapshot.get());
        engine.launch({LaunchEvent::Kind::Scene, 0, 0, true});
    }

    void step() {
        engine.process(frames.data(), events);
        sample++;
    }
    void runTo(int64_t target) {
        while (sample < target)
            step();
    }
    const TrackFrame& frame(size_t track = 0) const { return frames[slots.slotOf(song.tracks[track].id)]; }
    bool gate(int channel, size_t track = 0) const { return frame(track).gate[channel] > 5.f; }

    /// Samples (from now, up to `until`) where channel's gate goes high.
    std::vector<int64_t> risingEdges(int channel, int64_t until) {
        std::vector<int64_t> edges;
        bool was = sample >= 0 && gate(channel);
        while (sample < until) {
            step();
            bool now = gate(channel);
            if (now && !was)
                edges.push_back(sample);
            was = now;
        }
        return edges;
    }
};

} // namespace

TEST_CASE("note-on and note-off land on exact samples") {
    Harness h(songWith({{0, 240, 60}, {480, 240, 62}}, 4 * kPpq, 1));

    h.step();
    CHECK(h.gate(0));
    CHECK(h.frame().voct[0] == doctest::Approx(0.f));
    CHECK(h.frame().vel[0] == doctest::Approx(8.f));

    h.runTo(240 * kSamplesPerTick - 1);
    CHECK(h.gate(0)); // last sample of the note
    h.step();
    CHECK_FALSE(h.gate(0)); // first sample after it

    h.runTo(480 * kSamplesPerTick - 1);
    CHECK_FALSE(h.gate(0));
    h.step();
    CHECK(h.gate(0));
    CHECK(h.frame().voct[0] == doctest::Approx(2.f / 12.f));
}

TEST_CASE("a voice reused while its gate is high drops low for 1 ms first") {
    // Legato notes on a mono track share one voice.
    Harness h(songWith({{0, 240, 60}, {240, 240, 64}}, 4 * kPpq, 1));
    int64_t boundary = 240 * kSamplesPerTick;

    h.runTo(boundary - 1);
    CHECK(h.gate(0));
    h.step();
    CHECK_FALSE(h.gate(0));
    CHECK(h.frame().voct[0] == doctest::Approx(4.f / 12.f)); // pitch moves at once, the gate waits

    h.runTo(boundary + kRetriggerGap - 1);
    CHECK_FALSE(h.gate(0));
    h.step();
    CHECK(h.gate(0));
}

TEST_CASE("voices are used round-robin and the oldest is stolen") {
    Harness h(songWith({{0, 2000, 60}, {10, 2000, 62}, {20, 2000, 64}}, 4 * kPpq, 2));

    h.runTo(20 * kSamplesPerTick - 1);
    CHECK(h.gate(0));
    CHECK(h.gate(1));
    CHECK(h.frame().voct[1] == doctest::Approx(2.f / 12.f));

    h.step(); // third note steals voice 0, which held the oldest note
    CHECK(h.frame().voct[0] == doctest::Approx(4.f / 12.f));
    CHECK_FALSE(h.gate(0));
    CHECK(h.gate(1));
    h.runTo(h.sample + kRetriggerGap);
    CHECK(h.gate(0));
}

TEST_CASE("VoiceAllocator: lowering polyphony releases the voices above it") {
    VoiceAllocator v;
    v.setPolyphony(4);
    for (int i = 0; i < 4; i++)
        v.noteOn(int8_t(60 + i), 1, 0.5f, 1000);
    CHECK(v.gate(3));
    v.setPolyphony(2);
    CHECK_FALSE(v.gate(2));
    CHECK_FALSE(v.gate(3));
    CHECK(v.gate(0));
}

TEST_CASE("clips loop, including notes right at the loop end") {
    Harness h(songWith({{0, 100, 60}, {kPpq - 1, 1, 67}}, kPpq, 2));
    int64_t loop = kPpq * kSamplesPerTick;

    std::vector<int64_t> edges0 = h.risingEdges(0, 3 * loop - 1);
    CHECK(edges0 == std::vector<int64_t>{0, loop, 2 * loop});

    Harness h2(songWith({{0, 100, 60}, {kPpq - 1, 1, 67}}, kPpq, 2));
    std::vector<int64_t> edges1 = h2.risingEdges(1, 2 * loop - 1);
    CHECK(edges1 == std::vector<int64_t>{(kPpq - 1) * kSamplesPerTick, loop + (kPpq - 1) * kSamplesPerTick});
}

TEST_CASE("stop releases every gate on the next sample") {
    Harness h(songWith({{0, 4 * kPpq, 60}, {0, 4 * kPpq, 64}}, 4 * kPpq, 4));
    h.runTo(100);
    CHECK(h.gate(0));
    CHECK(h.gate(1));
    h.engine.stop();
    h.step();
    CHECK_FALSE(h.gate(0));
    CHECK_FALSE(h.gate(1));
    CHECK_FALSE(h.events.running);
}

TEST_CASE("reset restarts clips from their start and reports a reset event") {
    Harness h(songWith({{0, 100, 60}}, 4 * kPpq, 1));
    h.runTo(200 * kSamplesPerTick);
    CHECK_FALSE(h.gate(0));
    h.engine.reset();
    h.step();
    CHECK(h.events.reset);
    CHECK(h.engine.position() == doctest::Approx(1.0 / kSamplesPerTick));
    h.runTo(h.sample + kRetriggerGap);
    CHECK(h.gate(0));
}

TEST_CASE("transport: bar, beat and clock events line up with note timing") {
    Harness h(songWith({}, 4 * kPpq, 1));
    const int64_t beat = kPpq * kSamplesPerTick;
    const int64_t sixteenth = beat / 4;

    std::vector<int64_t> bars, beats, clockRises;
    bool clockWas = false;
    while (h.sample < 2 * 4 * beat - 1) {
        h.step();
        if (h.events.bar)
            bars.push_back(h.sample);
        if (h.events.beat)
            beats.push_back(h.sample);
        if (h.events.clock && !clockWas)
            clockRises.push_back(h.sample);
        if (h.sample == sixteenth / 2 - 1)
            CHECK(h.events.clock); // high for exactly the first half of each step
        if (h.sample == sixteenth / 2)
            CHECK_FALSE(h.events.clock);
        clockWas = h.events.clock;
    }
    CHECK(bars == std::vector<int64_t>{0, 4 * beat});
    CHECK(beats.size() == 8);
    CHECK(beats[1] == beat);
    CHECK(clockRises.size() == 32);
    CHECK(clockRises[1] == sixteenth);
}

TEST_CASE("the song loop wraps the position and fires the loop event") {
    Song song = songWith({}, 4 * kPpq, 1);
    song.loopStart = 0;
    song.loopEnd = 4 * kPpq;
    Harness h(song);
    h.engine.setLoopEnabled(true);
    const int64_t loopSamples = 4 * kPpq * kSamplesPerTick;

    h.runTo(loopSamples - 1);
    CHECK(h.events.loop);
    CHECK(h.engine.position() == doctest::Approx(0.0));
    h.step();
    CHECK(h.events.bar); // the bar at the loop start fires in the same sample it would without a loop
    CHECK_FALSE(h.events.loop);
}

TEST_CASE("frames: muted tracks stay silent and unused slots carry no track") {
    Song song = songWith({{0, 1000, 60}}, 4 * kPpq, 1);
    song.tracks[0].mute = true;
    Harness h(song);
    h.runTo(100);
    CHECK_FALSE(h.gate(0));
    CHECK(h.frame().trackId == song.tracks[0].id.value);
    CHECK(h.frame().channels == 1);

    int used = h.slots.slotOf(song.tracks[0].id);
    for (int s = 0; s < kMaxTracks; s++)
        if (s != used)
            CHECK(h.frames[s].trackId == 0);
}

TEST_CASE("tap tempo averages recent taps and restarts after a pause") {
    TapTempo tap;
    CHECK(tap.tap(10.0) == 0);
    CHECK(tap.tap(10.5) == doctest::Approx(120));
    CHECK(tap.tap(11.0) == doctest::Approx(120));
    CHECK(tap.tap(11.6) == doctest::Approx(60.0 * 3 / 1.6));

    CHECK(tap.tap(20.0) == 0); // long pause: start over
    CHECK(tap.tap(21.0) == doctest::Approx(60));
}

TEST_CASE("audition notes sound while stopped, and end on note-off or by themselves") {
    Harness h(songWith({}, 4 * kPpq, 2));
    h.engine.stop();
    uint64_t track = h.song.tracks[0].id.value;

    h.engine.audition({track, 67, 0.5f, 0, true});
    h.step();
    CHECK(h.gate(0));
    CHECK(h.frame().voct[0] == doctest::Approx(7.f / 12.f));
    CHECK(h.frame().vel[0] == doctest::Approx(5.f));
    h.runTo(h.sample + 1000);
    CHECK(h.gate(0)); // held until the note-off
    h.engine.audition({track, 67, 0, 0, false});
    h.step();
    CHECK_FALSE(h.gate(0));

    h.engine.audition({track, 60, 0.8f, 0.01f, true}); // 10 ms = 480 samples
    h.step();
    int v = h.gate(0) ? 0 : 1;
    CHECK(h.gate(v));
    h.runTo(h.sample + 478);
    CHECK(h.gate(v));
    h.runTo(h.sample + 2);
    CHECK_FALSE(h.gate(v));
}

TEST_CASE("SpscQueue passes items in order between two threads and never blocks") {
    SpscQueue<int, 64> queue;
    std::atomic<bool> done{false};
    std::vector<int> received;
    std::thread consumer([&] {
        int item;
        for (;;) {
            if (queue.pop(item)) {
                received.push_back(item);
            } else if (done.load()) {
                while (queue.pop(item)) // anything pushed just before `done`
                    received.push_back(item);
                break;
            }
        }
    });
    int sent = 0;
    while (sent < 20000)
        if (queue.push(sent))
            sent++;
    done.store(true);
    consumer.join();
    REQUIRE(received.size() == 20000);
    for (int i = 0; i < 20000; i++)
        REQUIRE(received[size_t(i)] == i);
}

// ---- Launcher -----------------------------------------------------------------------------------------------

namespace {

constexpr int64_t kBarSamples = 4 * kPpq * kSamplesPerTick;

LaunchEvent launchSlot(const Harness& h, int slot, bool immediate = false) {
    return {LaunchEvent::Kind::Slot, h.song.tracks[0].id.value, slot, immediate};
}

} // namespace

TEST_CASE("a launched clip waits for the next bar, then starts on that exact sample") {
    Harness h(songWithSlots({{{0, 100, 60}}, {{0, 100, 72}}}, 4 * kPpq));
    h.runTo(kBarSamples / 3);
    h.engine.launch(launchSlot(h, 1));
    CHECK(h.engine.playingSlot(0) == 0);
    CHECK(h.engine.queuedSlot(0) == 1);

    std::vector<int64_t> edges = h.risingEdges(0, kBarSamples + 10);
    CHECK(edges == std::vector<int64_t>{kBarSamples});
    CHECK(h.frame().voct[0] == doctest::Approx(1.f));
    CHECK(h.engine.playingSlot(0) == 1);
    CHECK(h.engine.queuedSlot(0) == kNothingQueued);
    CHECK(h.engine.clipPosition(0) == doctest::Approx(10.0 / kSamplesPerTick + 1.0 / kSamplesPerTick));
}

TEST_CASE("launch quantize: the song's grid, a clip's own grid, and immediate launches") {
    Song song = songWithSlots({{{0, 100, 60}}, {{0, 100, 72}}}, 4 * kPpq);
    song.launchQuantize = LaunchQuantize::Quarter;
    Harness h(song);
    h.runTo(100 * kSamplesPerTick);
    h.engine.launch(launchSlot(h, 1));
    h.runTo(kPpq * kSamplesPerTick - 1);
    CHECK(h.engine.playingSlot(0) == 0);
    h.step();
    CHECK(h.engine.playingSlot(0) == 1);

    // The clip's own setting wins over the song's.
    Song own = song;
    own.clips.at(*own.tracks[0].slots[1].clip).launchQuantize = LaunchQuantize::Off;
    Harness h2(own);
    h2.runTo(100 * kSamplesPerTick);
    h2.engine.launch(launchSlot(h2, 1));
    CHECK(h2.engine.playingSlot(0) == 1);

    Harness h3(song);
    h3.runTo(100 * kSamplesPerTick);
    h3.engine.launch(launchSlot(h3, 1, true));
    CHECK(h3.engine.playingSlot(0) == 1);
}

TEST_CASE("switching clips releases the old clip's notes at the switch") {
    Harness h(songWithSlots({{{0, 8 * kPpq, 60}}, {}}, 8 * kPpq));
    h.runTo(100);
    CHECK(h.gate(0));
    h.engine.launch({LaunchEvent::Kind::StopTrack, h.song.tracks[0].id.value, 0});
    CHECK(h.engine.queuedSlot(0) == kNoSlot);
    h.runTo(kBarSamples - 1);
    CHECK(h.gate(0));
    h.step();
    CHECK_FALSE(h.gate(0));
    CHECK(h.engine.playingSlot(0) == kNoSlot);
    CHECK(std::isnan(h.engine.clipPosition(0)));
}

TEST_CASE("launching while stopped starts at once and starts the transport") {
    Harness h(songWithSlots({{{0, 100, 60}}, {{0, 100, 72}}}, 4 * kPpq));
    h.runTo(1000);
    h.engine.stop();
    h.engine.launch(launchSlot(h, 1));
    CHECK(h.engine.isPlaying());
    CHECK(h.engine.playingSlot(0) == 1);
    h.step();
    CHECK(h.frame().voct[0] == doctest::Approx(1.f));
    h.runTo(h.sample + kRetriggerGap); // the voice was high until the stop, so it retriggers
    CHECK(h.gate(0));

    // A restore (start = false) doesn't start the transport.
    h.engine.stop();
    h.engine.launch({LaunchEvent::Kind::Slot, h.song.tracks[0].id.value, 0, true, false});
    CHECK_FALSE(h.engine.isPlaying());
    CHECK(h.engine.playingSlot(0) == 0);
}

TEST_CASE("scene launch: empty slots stop their track, and a scene tempo comes with the scene") {
    Song song = songWithSlots({{{0, 100, 60}}, {}}, 4 * kPpq);
    song.scenes[0].tempo = 140.0;
    Harness h(song);
    h.step();
    CHECK(h.events.tempo == doctest::Approx(140.0)); // the harness launched scene 0
    h.runTo(1000);
    h.engine.launch({LaunchEvent::Kind::Scene, 0, 1});
    CHECK(h.engine.queuedSlot(0) == kNoSlot);
    h.engine.stop(); // a stop carries out what was queued
    CHECK(h.engine.playingSlot(0) == kNoSlot);
    CHECK(h.engine.queuedSlot(0) == kNothingQueued);
}

TEST_CASE("the playing clip is found again after slots move, and stops when its clip is gone") {
    Song song = songWithSlots({{{0, 100, 60}}, {{0, 100, 72}}}, 4 * kPpq);
    Harness h(song);
    h.engine.launch(launchSlot(h, 1, true));
    h.runTo(10);

    // A scene inserted above: the clip moves from slot 1 to slot 2.
    Song moved = h.song;
    moved.tracks[0].slots.insert(moved.tracks[0].slots.begin(), ClipSlot{});
    moved.scenes.emplace_back();
    auto snap = buildSnapshot(moved, h.slots);
    h.engine.setSnapshot(snap.get());
    CHECK(h.engine.playingSlot(0) == 2);

    // The clip deleted: the track stops.
    Song gone = moved;
    gone.tracks[0].slots[2].clip.reset();
    auto snap2 = buildSnapshot(gone, h.slots);
    h.engine.setSnapshot(snap2.get());
    CHECK(h.engine.playingSlot(0) == kNoSlot);
    h.step();
    CHECK_FALSE(h.gate(0));
}

TEST_CASE("a one-shot clip stops after its last note") {
    // Without a loop the notes set the length, not the clip's 1-beat region.
    Song song = songWithSlots({{{0, 100, 60}, {2 * kPpq, 100, 62}}}, kPpq);
    song.clips.begin()->second.loopMode = LoopMode::Off;
    Harness h(song);
    h.runTo(2 * kPpq * kSamplesPerTick + 10);
    CHECK(h.engine.playingSlot(0) == 0);
    CHECK(h.gate(0)); // the note past the region plays
    h.runTo((2 * kPpq + 100) * kSamplesPerTick - 2);
    CHECK(h.engine.playingSlot(0) == 0);
    h.runTo((2 * kPpq + 100) * kSamplesPerTick + 1);
    CHECK(h.engine.playingSlot(0) == kNoSlot);
}

// ---- Arranger mode ------------------------------------------------------------------------------------------

namespace {

/// The harness song with its clip also placed on the timeline at `position` for `length` ticks.
Song songWithInstance(const std::vector<NoteSpec>& notes, Tick loopLength, Tick position, Tick length) {
    Song song = songWith(notes, loopLength, 1);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = song.clips.begin()->first;
    a.position = position;
    a.length = length;
    song.tracks[0].arrangement.push_back(a);
    return song;
}

void arrangerMode(Harness& h) {
    h.engine.setPlayMode(PlayMode::Arranger);
    h.engine.reset();
}

} // namespace

TEST_CASE("arranger mode plays the timeline on exact samples; launcher mode leaves it silent") {
    Harness h(songWithInstance({{0, 100, 72}}, kPpq, 2 * kPpq, 2 * kPpq));
    arrangerMode(h);
    CHECK(h.engine.playingSlot(0) == kNoSlot); // the launched clip gave way to the arrangement
    std::vector<int64_t> edges = h.risingEdges(0, 5 * kPpq * kSamplesPerTick);
    CHECK(edges == std::vector<int64_t>{2 * kPpq * kSamplesPerTick, 3 * kPpq * kSamplesPerTick});
    CHECK(h.frame().voct[0] == doctest::Approx(1.f));

    Harness quiet(songWithInstance({{0, 100, 72}}, kPpq, 2 * kPpq, 2 * kPpq));
    quiet.engine.launch({LaunchEvent::Kind::StopAll, 0, 0, true});
    CHECK(quiet.risingEdges(0, 5 * kPpq * kSamplesPerTick).empty());
}

TEST_CASE("a Replace take silences the armed track's timeline clips while it records; Stack and Merge keep them") {
    for (bool replace : {true, false}) {
        Song song = songWithInstance({{0, 100, 72}}, kPpq, 2 * kPpq, 2 * kPpq);
        song.tracks[0].armed = true;
        Harness h(song);
        arrangerMode(h);
        h.engine.setReplaceTakes(replace);
        h.engine.toggleRecord(0);
        CHECK(h.engine.recording());
        CHECK(h.risingEdges(0, 5 * kPpq * kSamplesPerTick).empty() == replace);
    }
}

TEST_CASE("at the loop wrap, arrangement notes stop and the loop start plays again") {
    // A note from 1 beat that lasts 4 beats, looping over the first 2 beats.
    Harness h(songWithInstance({{0, 4 * kPpq, 60}}, 4 * kPpq, kPpq, 4 * kPpq));
    arrangerMode(h);
    h.song.loopStart = 0;
    h.engine.setLoopEnabled(true);
    Song looped = h.song;
    looped.loopEnd = 2 * kPpq;
    auto snap = buildSnapshot(looped, h.slots);
    h.engine.setSnapshot(snap.get());

    int64_t loopEnd = 2 * kPpq * kSamplesPerTick;
    h.runTo(loopEnd - 1);
    CHECK(h.gate(0));
    h.step();
    CHECK_FALSE(h.gate(0)); // released at the wrap; nothing starts at tick 0
    std::vector<int64_t> edges = h.risingEdges(0, loopEnd + kPpq * kSamplesPerTick + 10);
    CHECK(edges == std::vector<int64_t>{loopEnd + kPpq * kSamplesPerTick});
}

TEST_CASE("a note cut short by the song loop keeps its curve's written timing, stretched with the instance") {
    // An 8-beat note that bends up 8 semitones across itself, as the editor draws it; the song loop is 4 beats.
    for (float stretch : {1.f, 2.f}) {
        Song song = songWithInstance({{0, 8 * kPpq, 60}}, 8 * kPpq, 0, Tick(8 * kPpq * stretch));
        song.clips.begin()->second.notes[0].pitchExpr = {{0, 0, 0}, {1, 8, 0}};
        song.tracks[0].arrangement[0].stretch = stretch;
        song.loopStart = 0;
        song.loopEnd = 4 * kPpq;
        Harness h(song);
        arrangerMode(h);
        h.engine.setLoopEnabled(true);
        // Two played beats in: 2/stretch written beats, so 2/stretch semitones up (it was 4 when the curve was
        // squeezed into the part before the loop end).
        h.runTo(2 * kPpq * kSamplesPerTick);
        REQUIRE(h.gate(0));
        CHECK(h.frame().voct[0] * 12 == doctest::Approx(2 / stretch).epsilon(0.01));
    }
}

TEST_CASE("an edit while a bent note sounds keeps its curve, and an edited curve takes over at once") {
    Song song = songWithInstance({{0, 8 * kPpq, 60}}, 8 * kPpq, 0, 8 * kPpq);
    Note& note = song.clips.begin()->second.notes[0];
    note.pitchExpr = {{0, 0, 0}, {1, 8, 0}};
    Harness h(song);
    arrangerMode(h);
    h.runTo(2 * kPpq * kSamplesPerTick);
    REQUIRE(h.gate(0));
    CHECK(h.frame().voct[0] * 12 == doctest::Approx(2).epsilon(0.01));

    // Any edit publishes a new snapshot: the sounding note keeps bending.
    auto same = buildSnapshot(h.song, h.slots);
    h.engine.setSnapshot(same.get());
    h.runTo(4 * kPpq * kSamplesPerTick);
    REQUIRE(h.gate(0));
    CHECK(h.frame().voct[0] * 12 == doctest::Approx(4).epsilon(0.01));

    // The curve itself edited (now down 8 semitones): the sounding note follows the new curve.
    Song edited = h.song;
    edited.clips.begin()->second.notes[0].pitchExpr = {{0, 0, 0}, {1, -8, 0}};
    auto changed = buildSnapshot(edited, h.slots);
    h.engine.setSnapshot(changed.get());
    h.runTo(6 * kPpq * kSamplesPerTick);
    REQUIRE(h.gate(0));
    CHECK(h.frame().voct[0] * 12 == doctest::Approx(-6).epsilon(0.01));
}

TEST_CASE("a sounding note whose curve is gone after an edit lets go of the old snapshot") {
    Song song = songWithInstance({{0, 8 * kPpq, 60}}, 8 * kPpq, 0, 8 * kPpq);
    song.clips.begin()->second.notes[0].pitchExpr = {{0, 0, 0}, {1, 8, 0}};
    Harness h(song);
    arrangerMode(h);
    h.runTo(2 * kPpq * kSamplesPerTick);
    REQUIRE(h.gate(0));
    // The curve is removed, and the old snapshot freed as the module does once the engine took the new one.
    Song edited = h.song;
    edited.clips.begin()->second.notes[0].pitchExpr.clear();
    auto changed = buildSnapshot(edited, h.slots);
    h.engine.setSnapshot(changed.get());
    h.snapshot.reset();
    h.runTo(3 * kPpq * kSamplesPerTick);
    REQUIRE(h.gate(0));
    CHECK(h.frame().voct[0] * 12 == doctest::Approx(0));
}

TEST_CASE("an arrangement track waiting for its next note still wraps when the loop is switched on") {
    // One note per 4 beats; the song loop covers the first 2 beats but is off.
    Song song = songWithInstance({{0, 100, 72}}, 4 * kPpq, 0, 8 * kPpq);
    song.loopStart = 0;
    song.loopEnd = 2 * kPpq;
    Harness h(song);
    arrangerMode(h);
    h.runTo(kPpq * kSamplesPerTick);
    h.engine.setLoopEnabled(true);
    int64_t wrap = 2 * kPpq * kSamplesPerTick;
    CHECK(h.risingEdges(0, wrap + 10) == std::vector<int64_t>{wrap});
}

TEST_CASE("after a long session, arrangement tracks still play the loop start on every pass") {
    // Play ticks only grow, so after a long session they round differently from song ticks: a track asleep until
    // the loop end can wake a hair late. It must still catch the wrap, or it skips the loop start for a whole pass.
    Song song = songWithInstance({{0, kPpq, 60}, {2 * kPpq, kPpq, 62}}, 4 * kPpq, 0, 4 * kPpq);
    song.loopStart = 0;
    song.loopEnd = 4 * kPpq;
    Harness h(song);
    arrangerMode(h);
    h.engine.setSampleRate(44100);
    h.engine.setTempo(120);
    // About 37 minutes of play in one sample (an external clock can step that far), then back to the start.
    h.engine.setStep(4.3e6);
    h.step();
    h.engine.seek(0);
    h.engine.setLoopEnabled(true);
    std::vector<int> notesPerPass(8, 0);
    size_t pass = 0;
    bool was = false;
    while (pass < notesPerPass.size()) {
        h.step();
        pass += h.events.loop;
        bool now = h.gate(0);
        if (now && !was && pass < notesPerPass.size())
            notesPerPass[pass]++;
        was = now;
    }
    CHECK(notesPerPass == std::vector<int>(8, 2));
}

TEST_CASE("arrangement tracks wake for every note and loop wrap on exact samples") {
    // A 2-beat clip looping inside its instance, under a song loop from half a beat to 3 beats.
    Song song = songWithInstance({{0, 100, 60}, {kPpq / 3, 50, 62}, {kPpq + 7, 100, 64}}, 2 * kPpq, 0, 8 * kPpq);
    song.loopStart = kPpq / 2;
    song.loopEnd = 3 * kPpq;
    Harness h(song);
    arrangerMode(h);
    h.engine.setLoopEnabled(true);
    // The sample where song tick t plays on pass p: each pass after the first starts from the loop start.
    auto at = [](Tick t, int pass) { return (t + pass * (3 * kPpq - kPpq / 2)) * kSamplesPerTick; };
    std::vector<int64_t> expected = {at(0, 0),        at(kPpq / 3, 0),     at(kPpq + 7, 0),
                                     at(2 * kPpq, 0), at(7 * kPpq / 3, 0), at(kPpq + 7, 1),
                                     at(2 * kPpq, 1), at(7 * kPpq / 3, 1), at(kPpq + 7, 2)};
    CHECK(h.risingEdges(0, at(kPpq + 7, 2) + 10) == expected);
}

TEST_CASE("launching in arranger mode takes the track over until back to arrangement") {
    Song song = songWithInstance({{0, 100, 72}}, kPpq, 0, 8 * kPpq);
    Harness h(song);
    arrangerMode(h);
    h.runTo(10);
    CHECK_FALSE(h.engine.overridden(0));

    h.engine.launch({LaunchEvent::Kind::StopTrack, h.song.tracks[0].id.value, 0, true});
    CHECK(h.engine.overridden(0)); // stopped: the arrangement is silent on this track
    CHECK(h.risingEdges(0, 3 * kPpq * kSamplesPerTick).empty());

    h.engine.launch({LaunchEvent::Kind::BackToArrangement});
    CHECK_FALSE(h.engine.overridden(0));
    int64_t nextBeat = 4 * kPpq * kSamplesPerTick;
    CHECK(h.risingEdges(0, nextBeat).back() == nextBeat);
}

TEST_CASE("a reset clears launches queued before it, so a loaded arranger patch plays every track") {
    // Loading a patch: the default song's launch is still queued when the engine is already in Arranger mode.
    Song song = songWithInstance({{0, 100, 72}}, kPpq, 0, 8 * kPpq);
    Harness h(song);
    arrangerMode(h);
    h.engine.launch({LaunchEvent::Kind::Slot, h.song.tracks[0].id.value, 0, true, false});
    CHECK(h.engine.overridden(0));

    h.engine.launch({LaunchEvent::Kind::Reset, 0, 0, true, false});
    CHECK_FALSE(h.engine.overridden(0));
    CHECK(h.engine.playingSlot(0) == kNoSlot);
    int64_t nextBeat = 4 * kPpq * kSamplesPerTick;
    CHECK(h.risingEdges(0, nextBeat).back() == nextBeat);
}

TEST_CASE("a reset in launcher mode stops launched clips") {
    Harness h(songWithInstance({{0, 100, 72}}, kPpq, 0, 8 * kPpq));
    CHECK(h.engine.playingSlot(0) == 0);
    h.engine.launch({LaunchEvent::Kind::Reset, 0, 0, true, false});
    CHECK(h.engine.playingSlot(0) == kNoSlot);
    CHECK(h.risingEdges(0, 4 * kPpq * kSamplesPerTick).empty());
}

TEST_CASE("seeking the song position moves where the arrangement plays from") {
    Harness h(songWithInstance({{0, 100, 72}}, kPpq, 0, 8 * kPpq));
    arrangerMode(h);
    h.runTo(200 * kSamplesPerTick); // after the first note ended
    h.engine.seek(double(5 * kPpq) - 3.0 / kSamplesPerTick);
    int64_t seekedAt = h.sample;
    std::vector<int64_t> edges = h.risingEdges(0, seekedAt + 10);
    CHECK(edges == std::vector<int64_t>{seekedAt + 4}); // three samples before beat 5, then the beat's own sample
}

TEST_CASE("capture reports clip starts and stops with their song position") {
    Song song = songWithSlots({{{0, 100, 60}}, {{0, 100, 72}}}, 4 * kPpq);
    Harness h(song);
    CaptureQueue queue;
    h.runTo(kBarSamples / 2 - 1);
    h.engine.setCapture(&queue, true);
    CaptureEvent e;
    REQUIRE(queue.pop(e)); // the clip already playing is recorded from here
    CHECK(e.clip == h.song.tracks[0].slots[0].clip->value);
    CHECK(e.songTick == doctest::Approx(double(2 * kPpq)));
    CHECK(e.clipTick == doctest::Approx(double(2 * kPpq)));

    h.engine.launch(launchSlot(h, 1));
    h.runTo(kBarSamples + 5);
    REQUIRE(queue.pop(e));
    CHECK(e.clip == h.song.tracks[0].slots[1].clip->value);
    CHECK(e.songTick == doctest::Approx(double(4 * kPpq)));
    CHECK(e.clipTick == doctest::Approx(0.0));

    h.engine.stop();
    REQUIRE(queue.pop(e));
    CHECK(e.clip == 0);
    CHECK_FALSE(queue.pop(e));
}

// ---- Drum tracks --------------------------------------------------------------------------------------------

#include "../src/model/DrumKit.hpp"

namespace {

/// A drum track (standard kit) whose one-bar clip has `notes`; `setup` may change the lanes.
Song drumSong(const std::vector<NoteSpec>& notes, const std::function<void(std::vector<DrumLane>&)>& setup = {}) {
    Song song = songWith(notes, 4 * kPpq, 1);
    Track& t = song.tracks[0];
    t.type = TrackType::Drum;
    t.drumLanes = drumKit(DrumKitPreset::Standard8);
    if (setup)
        setup(t.drumLanes);
    return song;
}

constexpr Tick kStep = kPpq / 4;
constexpr int64_t kStepSamples = kStep * kSamplesPerTick;

} // namespace

TEST_CASE("drum lanes: a 1 ms trigger on the lane's channel, with velocity and the lane's note") {
    Harness h(drumSong({{0, kStep, 36}, {kStep, kStep, 38}}));
    h.step();
    CHECK(h.frame().channels == 8);
    CHECK(h.gate(0));
    CHECK_FALSE(h.gate(1));
    CHECK(h.frame().vel[0] == doctest::Approx(8.f));
    CHECK(h.frame().voct[1] == doctest::Approx(pitchToVolts(38)));
    h.runTo(kRetriggerGap - 1);
    CHECK(h.gate(0));
    h.step();
    CHECK_FALSE(h.gate(0));
    CHECK(h.frame().vel[0] == doctest::Approx(8.f)); // velocity holds after the trigger
    CHECK(h.risingEdges(1, kStepSamples + 2) == std::vector<int64_t>{kStepSamples});
}

TEST_CASE("drum lanes: gate length, choke groups, mute and solo") {
    // Open hat (output 4) rings for 200 ms; the closed hat (output 3) one step (160 ms) later chokes it.
    Harness h(drumSong({{0, kStep, 46}, {kStep, kStep, 42}, {0, kStep, 36}},
                       [](std::vector<DrumLane>& l) { l[4].gateLengthMs = 200; }));
    h.runTo(kStepSamples - 1);
    CHECK(h.gate(4));
    h.step();
    CHECK_FALSE(h.gate(4));
    CHECK(h.gate(3));

    Harness muted(drumSong({{0, kStep, 36}, {0, kStep, 38}}, [](std::vector<DrumLane>& l) { l[0].mute = true; }));
    muted.step();
    CHECK_FALSE(muted.gate(0));
    CHECK(muted.gate(1));

    Harness solo(drumSong({{0, kStep, 36}, {0, kStep, 38}}, [](std::vector<DrumLane>& l) { l[1].solo = true; }));
    solo.step();
    CHECK_FALSE(solo.gate(0));
    CHECK(solo.gate(1));
}

TEST_CASE("drum lanes: a hit during a trigger drops low first, so it is a new edge") {
    Harness h(drumSong({{0, 1, 36}, {1, 1, 36}})); // two kicks one tick (32 samples) apart
    std::vector<int64_t> edges = h.risingEdges(0, 200);
    CHECK(edges == std::vector<int64_t>{0, kSamplesPerTick + kRetriggerGap});
}

TEST_CASE("drum lanes with their own loop length run against the clip (polymeter)") {
    // Hats loop every 3 steps over a 16-step clip: steps 0, 3, ..., 15, then 18 = bar 2 step 2.
    Harness h(
        drumSong({{0, kStep, 42}, {0, kStep, 36}}, [](std::vector<DrumLane>& l) { l[3].loopLength = 3 * kStep; }));
    std::vector<int64_t> edges = h.risingEdges(3, 19 * kStepSamples + 2);
    REQUIRE(edges.size() == 7);
    CHECK(edges[5] == 15 * kStepSamples);
    CHECK(edges[6] == 18 * kStepSamples);
}

TEST_CASE("drum audition hits the lane even while stopped") {
    Harness h(drumSong({}));
    h.engine.stop();
    h.engine.audition({h.song.tracks[0].id.value, 38, 0.5f, 0, true});
    h.step();
    CHECK(h.gate(1));
    CHECK(h.frame().vel[1] == doctest::Approx(5.f));
}

TEST_CASE("external clock: a note on beat 2 plays on the 24th pulse") {
    Harness h(songWith({{0, 120, 60}, {kPpq, 120, 62}}, 4 * kPpq, 1));
    ClockSync clock;
    constexpr int64_t kPeriod = 777; // an odd tempo, so nothing lines up by accident
    // The clock was already running before the transport started, as it is in a patch.
    for (int64_t s = 0; s < 4 * kPeriod; s++)
        clock.step(s % kPeriod == 0);
    clock.resetPhase();
    std::vector<int64_t> edges;
    bool was = false;
    for (int64_t s = 0; s < 30 * kPeriod; s++) {
        h.engine.setStep(clock.step(s % kPeriod == 0));
        h.step();
        bool now = h.gate(0);
        if (now && !was)
            edges.push_back(h.sample);
        was = now;
    }
    REQUIRE(edges.size() == 2);
    CHECK(edges[0] <= 1);
    CHECK(edges[1] >= 24 * kPeriod - 2);
    CHECK(edges[1] <= 24 * kPeriod + 2);
}
