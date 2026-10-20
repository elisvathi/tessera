#include "doctest.h"

#include "../src/engine/Recorder.hpp"
#include "../src/model/Json.hpp"

#include <cmath>

using namespace tessera;

namespace {

constexpr double kSampleRate = 48000;
constexpr double kBpm = 93.75; // 1/32 tick per sample
constexpr Tick kBar = 4 * kPpq;

/// Two instrument tracks (the first armed) with 4 empty slots each.
Song recordSong() {
    Song song;
    song.scenes.resize(4);
    for (int i = 0; i < 2; i++) {
        Track t;
        t.id = song.newId<TrackId>();
        t.name = "T" + std::to_string(i + 1);
        t.polyphony = 4;
        t.armed = i == 0;
        t.slots.resize(4);
        song.tracks.push_back(t);
    }
    return song;
}

struct EngineRig {
    Song song;
    BusSlots slots;
    std::unique_ptr<Snapshot> snapshot;
    Engine engine;
    RecordQueue queue;
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;

    explicit EngineRig(Song s) : song(std::move(s)) {
        slots.sync(song);
        snapshot = buildSnapshot(song, slots);
        engine.setSampleRate(kSampleRate);
        engine.setTempo(kBpm);
        engine.setSnapshot(snapshot.get());
        engine.setRecordQueue(&queue);
    }
    void run(int samples) {
        for (int i = 0; i < samples; i++)
            engine.process(frames.data(), events);
    }
    bool gate(size_t track) const { return frames[size_t(slots.slotOf(song.tracks[track].id))].gate[0] > 5.f; }
    std::vector<RecordEvent> drain() {
        std::vector<RecordEvent> out;
        RecordEvent e;
        while (queue.pop(e))
            out.push_back(e);
        return out;
    }
};

/// Applies commands straight to its song and remembers what it was asked.
struct FakeHost : RecorderHost {
    Song song_;
    PlayMode mode = PlayMode::Launcher;
    int scene = 0;
    std::vector<std::string> steps;
    struct Launch {
        TrackId track;
        int slot;
        double at;
    };
    std::vector<Launch> launches;

    explicit FakeHost(Song s) : song_(std::move(s)) {}
    const Song& song() override { return song_; }
    void perform(CommandPtr command) override {
        if (!command)
            return;
        steps.push_back(command->name());
        command->apply(song_);
    }
    uint64_t nextId() override { return song_.nextId++; }
    void launchAt(TrackId track, int slot, double playTick) override { launches.push_back({track, slot, playTick}); }
    PlayMode playMode() override { return mode; }
    int targetScene() override { return scene; }
};

RecordEvent punch(RecordEvent::Kind kind, double play, double song, bool playing = true) {
    RecordEvent e;
    e.kind = kind;
    e.playTick = play;
    e.songTick = song;
    e.playing = playing;
    e.recording = kind != RecordEvent::Kind::PunchOut;
    return e;
}

RecordEvent note(const Track& t, int pitch, bool on, double play, double song, float velocity = 0.8f) {
    RecordEvent e;
    e.track = t.id.value;
    e.pitch = int8_t(pitch);
    e.on = on;
    e.velocity = velocity;
    e.playTick = play;
    e.songTick = song;
    e.playing = true;
    e.recording = true;
    return e;
}

const Clip* slotClip(const Song& song, size_t track, int slot) {
    const ClipSlot& s = song.tracks[track].slots[size_t(slot)];
    return s.clip ? song.findClip(*s.clip) : nullptr;
}

} // namespace

TEST_CASE("record settings survive a save and load") {
    RecordSettings s;
    s.countInBars = 2;
    s.clipBars = 4;
    s.arranger = ArrangerRecordMode::Replace;
    s.quantize = kPpq / 4;
    s.strength = 0.5f;
    s.toKey = true;
    json::Ref j(s.toJson());
    CHECK(RecordSettings::fromJson(j.get()) == s);
    CHECK(RecordSettings::fromJson(nullptr) == RecordSettings());
    CHECK(wholeBars(1, TimeSig{}) == kBar);
    CHECK(wholeBars(kBar, TimeSig{}) == kBar);
    CHECK(wholeBars(kBar + 1, TimeSig{}) == 2 * kBar);
}

TEST_CASE("quantizeStarts pulls starts toward the grid by the strength") {
    edits::NoteList notes(2);
    notes[0].id = NoteId(1);
    notes[0].start = 230;
    notes[1].id = NoteId(2);
    notes[1].start = 500;
    auto full = quantizeStarts(notes, {NoteId(1), NoteId(2)}, 240, 1);
    CHECK(full[0].start == 240);
    CHECK(full[1].start == 480);
    auto half = quantizeStarts(notes, {NoteId(2)}, 240, 0.5f);
    CHECK(half[0].start == 230);
    CHECK(half[1].start == 490);
}

TEST_CASE("record with count-in: clicks first, then plays and punches in") {
    EngineRig rig(recordSong());
    rig.engine.toggleRecord(1);
    CHECK(rig.engine.countingIn());
    CHECK_FALSE(rig.engine.isPlaying());
    int beats = 0;
    int64_t barSamples = kBar * 32;
    for (int64_t i = 0; i < barSamples; i++) {
        rig.run(1);
        beats += rig.events.beat;
    }
    CHECK(beats == 5); // 4 count-in clicks, then the song's own first downbeat
    CHECK(rig.engine.isPlaying());
    CHECK(rig.engine.recording());
    auto events = rig.drain();
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == RecordEvent::Kind::PunchIn);
    CHECK(events[0].songTick == doctest::Approx(0).epsilon(1e-3));

    rig.engine.toggleRecord(1); // punch out, keep playing
    CHECK_FALSE(rig.engine.recording());
    CHECK(rig.engine.isPlaying());
    CHECK(rig.drain().back().kind == RecordEvent::Kind::PunchOut);
}

TEST_CASE("record while playing punches in at once; stop punches out") {
    EngineRig rig(recordSong());
    rig.engine.play();
    rig.run(100);
    rig.engine.toggleRecord(2);
    CHECK(rig.engine.recording());
    rig.engine.stop();
    auto events = rig.drain();
    REQUIRE(events.size() == 2);
    CHECK(events[1].kind == RecordEvent::Kind::PunchOut);
    CHECK_FALSE(events[1].playing); // the take ended with the transport
}

TEST_CASE("live notes reach armed tracks with a matching input, and monitoring plays them") {
    Song song = recordSong();
    song.tracks[1].armed = true;
    song.tracks[1].recordInput = RecordInput::Midi;
    song.tracks[1].midiChannel = 2;
    EngineRig rig(song);

    rig.engine.liveNote({LiveSource::Cv, 0, 64, 0.5f, true});
    rig.run(1);
    CHECK(rig.gate(0));                                         // armed, input Any
    CHECK_FALSE(rig.gate(1));                                   // MIDI only
    rig.engine.liveNote({LiveSource::Midi, 1, 67, 0.5f, true}); // MIDI channel 2
    rig.run(1);
    CHECK(rig.gate(1));
    rig.engine.liveNote({LiveSource::Cv, 0, 64, 0, false});
    rig.engine.liveNote({LiveSource::Midi, 1, 67, 0, false});
    rig.run(1);
    CHECK_FALSE(rig.gate(0));
    CHECK_FALSE(rig.gate(1));

    // Not recording: only the capture buffer's copies arrive.
    for (const RecordEvent& e : rig.drain())
        CHECK(e.track == 0);

    rig.engine.toggleRecord(0);
    rig.engine.liveNote({LiveSource::Cv, 0, 60, 0.5f, true});
    auto events = rig.drain();
    REQUIRE(events.size() == 3); // punch in, buffer copy, track 1
    CHECK(events[2].track == song.tracks[0].id.value);
    CHECK(events[2].recording);
}

TEST_CASE("a note-off reaches the tracks its note-on went to, even after disarming") {
    EngineRig rig(recordSong());
    rig.engine.liveNote({LiveSource::Cv, 0, 60, 0.8f, true});
    rig.run(1);
    CHECK(rig.gate(0));
    Song disarmed = rig.song;
    disarmed.tracks[0].armed = false;
    auto next = buildSnapshot(disarmed, rig.slots);
    rig.engine.setSnapshot(next.get());
    rig.engine.liveNote({LiveSource::Cv, 0, 60, 0, false});
    rig.run(1);
    CHECK_FALSE(rig.gate(0));
}

TEST_CASE("live notes can snap to the song key") {
    Song song = recordSong();
    song.scale = ScaleRef{0, "major"};
    EngineRig rig(song);
    rig.engine.setLiveToKey(true);
    rig.engine.toggleRecord(0);
    rig.engine.liveNote({LiveSource::Cv, 0, 61, 0.8f, true});
    auto events = rig.drain();
    CHECK(events.back().pitch == 60);
}

TEST_CASE("launcher: a new clip as long as the take, rounded up to the bar, carrying on in time") {
    FakeHost host(recordSong());
    host.scene = 1;
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    // Punch in half a bar into the song: the clip starts at the bar.
    rec.handle(punch(RecordEvent::Kind::PunchIn, 1000 + kBar / 2, kBar / 2));
    rec.handle(note(t, 60, true, 1000 + kBar / 2 + 10, kBar / 2 + 10));
    rec.handle(note(t, 60, false, 1000 + kBar, kBar));
    rec.handle(note(t, 64, true, 1000 + kBar + 100, kBar + 100)); // still held at punch-out
    rec.handle(punch(RecordEvent::Kind::PunchOut, 1000 + kBar + 200, kBar + 200));

    const Clip* clip = slotClip(host.song_, 0, 1);
    REQUIRE(clip);
    CHECK(clip->length == 2 * kBar);
    REQUIRE(clip->notes.size() == 2);
    CHECK(clip->notes[0].start == kBar / 2 + 10);
    CHECK(clip->notes[0].length == kBar / 2 - 10);
    CHECK(clip->notes[1].start == kBar + 100);
    CHECK(clip->notes[1].length == 100); // ended at punch-out
    REQUIRE(host.launches.size() == 1);
    CHECK(host.launches[0].slot == 1);
    CHECK(host.launches[0].at == doctest::Approx(1000));
    CHECK(host.steps == std::vector<std::string>{"Record 2 notes"});
}

TEST_CASE("launcher: nothing played makes no clip; a stop doesn't relaunch") {
    FakeHost host(recordSong());
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(punch(RecordEvent::Kind::PunchOut, 500, 500));
    CHECK(host.steps.empty());
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 10, 10));
    rec.handle(note(t, 60, false, 20, 20));
    rec.handle(punch(RecordEvent::Kind::PunchOut, 30, 30, false));
    CHECK(slotClip(host.song_, 0, 0));
    CHECK(host.launches.empty());
}

TEST_CASE("launcher: a fixed length commits when full, then each loop pass overdubs") {
    FakeHost host(recordSong());
    Recorder rec(host);
    rec.settings.clipBars = 1;
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 0, 0));
    rec.handle(note(t, 60, false, 100, 100));
    rec.update(kBar - 1);
    CHECK(host.steps.empty());
    rec.update(kBar);
    const Clip* clip = slotClip(host.song_, 0, 0);
    REQUIRE(clip);
    CHECK(clip->length == kBar);
    REQUIRE(host.launches.size() == 1);
    CHECK(host.launches[0].at == 0);

    // Second pass: a note at 1.5 bars lands at half a bar, committed when the pass ends.
    rec.handle(note(t, 62, true, kBar + kBar / 2, kBar + kBar / 2));
    rec.handle(note(t, 62, false, kBar + kBar / 2 + 50, kBar + kBar / 2 + 50));
    rec.update(2 * kBar - 1);
    CHECK(host.steps.size() == 1);
    rec.update(2 * kBar + 5);
    REQUIRE(host.steps.size() == 2);
    clip = slotClip(host.song_, 0, 0);
    REQUIRE(clip->notes.size() == 2);
    CHECK(clip->notes[1].start == kBar / 2);
    rec.handle(punch(RecordEvent::Kind::PunchOut, 2 * kBar + 10, 2 * kBar + 10));
    CHECK(host.steps.size() == 2);
}

TEST_CASE("launcher: a track playing a clip overdubs it, wrapping into the loop") {
    Song song = recordSong();
    Clip c;
    c.id = song.newId<ClipId>();
    c.length = 2 * kBar;
    c.loopStart = kBar;
    c.loopEnd = 2 * kBar;
    song.clips[c.id] = c;
    song.tracks[0].slots[2].clip = c.id;
    FakeHost host(song);
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 5000, 5000));
    RecordEvent on = note(t, 70, true, 500 + kBar + 30, 0);
    on.slot = 2;
    on.clipStart = 500; // launched at play tick 500: the loop has wrapped once
    rec.handle(on);
    RecordEvent off = on;
    off.on = false;
    off.playTick += 60;
    rec.handle(off);
    rec.handle(punch(RecordEvent::Kind::PunchOut, 500 + kBar + 100, 0));
    const Clip* clip = host.song_.findClip(c.id);
    REQUIRE(clip->notes.size() == 1);
    CHECK(clip->notes[0].start == kBar + 30);
    CHECK(clip->notes[0].length == 60);
    CHECK(host.launches.empty());
}

TEST_CASE("arranger: a new clip over whole bars, overdub merging what was there") {
    Song song = recordSong();
    Clip old;
    old.id = song.newId<ClipId>();
    old.length = 4 * kBar;
    old.loopEnd = 4 * kBar;
    Note n;
    n.id = song.newId<NoteId>();
    n.start = kBar + 100; // inside the recorded range once placed at 0
    n.length = 50;
    n.pitch = 40;
    old.notes.push_back(n);
    Note outside = n;
    outside.id = song.newId<NoteId>();
    outside.start = 3 * kBar;
    old.notes.push_back(outside);
    song.clips[old.id] = old;
    ArrangerClip inst;
    inst.id = song.newId<ArrangementId>();
    inst.clip = old.id;
    inst.length = 4 * kBar;
    song.tracks[0].arrangement.push_back(inst);

    for (ArrangerRecordMode mode : {ArrangerRecordMode::Overdub, ArrangerRecordMode::Replace}) {
        FakeHost host(song);
        host.mode = PlayMode::Arranger;
        Recorder rec(host);
        rec.settings.arranger = mode;
        const Track& t = host.song_.tracks[0];
        rec.handle(punch(RecordEvent::Kind::PunchIn, 0, kBar + kBar / 2));
        rec.handle(note(t, 60, true, 10, kBar + kBar / 2 + 10));
        rec.handle(note(t, 60, false, 20, kBar + kBar / 2 + 20));
        rec.handle(punch(RecordEvent::Kind::PunchOut, 30, kBar + kBar / 2 + 30));

        const std::vector<ArrangerClip>& arr = host.song_.tracks[0].arrangement;
        REQUIRE(arr.size() == 3); // the old clip split around the new one
        const ArrangerClip* rec1 = nullptr;
        for (const ArrangerClip& a : arr)
            if (a.clip != old.id)
                rec1 = &a;
        REQUIRE(rec1);
        CHECK(rec1->position == kBar);
        CHECK(rec1->length == kBar);
        const Clip* made = host.song_.findClip(rec1->clip);
        REQUIRE(made);
        size_t expected = mode == ArrangerRecordMode::Overdub ? 2 : 1;
        CHECK(made->notes.size() == expected);
    }
}

TEST_CASE("arranger: each loop pass becomes its own undo step") {
    Song song = recordSong();
    song.loopStart = 0;
    song.loopEnd = 2 * kBar;
    FakeHost host(song);
    host.mode = PlayMode::Arranger;
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 10, 10));
    rec.handle(note(t, 60, false, 20, 20));
    rec.handle(punch(RecordEvent::Kind::LoopWrap, 2 * kBar, 0));
    CHECK(host.steps.size() == 1);
    rec.handle(note(t, 64, true, 2 * kBar + 10, 10));
    rec.handle(note(t, 64, false, 2 * kBar + 20, 20));
    rec.handle(punch(RecordEvent::Kind::PunchOut, 2 * kBar + 30, 30));
    CHECK(host.steps.size() == 2);
    // Overdub: the last pass (up to the punch-out bar) merged with the first; the rest of the first stays.
    const std::vector<ArrangerClip>& arr = host.song_.tracks[0].arrangement;
    REQUIRE(arr.size() == 2);
    CHECK(arr[0].position == 0);
    CHECK(arr[0].length == kBar);
    CHECK(host.song_.findClip(arr[0].clip)->notes.size() == 2);
    CHECK(arr[1].position == kBar);
}

TEST_CASE("input quantize is a second undo step on the take's own notes") {
    FakeHost host(recordSong());
    Recorder rec(host);
    rec.settings.quantize = kPpq / 4;
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 230, 230));
    rec.handle(note(t, 60, false, 300, 300));
    rec.handle(punch(RecordEvent::Kind::PunchOut, 400, 400));
    CHECK(host.steps == std::vector<std::string>{"Record 1 note", "Quantize recording"});
    CHECK(slotClip(host.song_, 0, 0)->notes[0].start == 240);
}

TEST_CASE("capture turns the last phrase played into a clip") {
    FakeHost host(recordSong());
    Recorder rec(host);
    auto free = [](int pitch, bool on, double tick) {
        RecordEvent e;
        e.pitch = int8_t(pitch);
        e.on = on;
        e.freeTick = tick;
        return e;
    };
    CHECK_FALSE(rec.canCapture());
    // An old note, then 8 bars of silence, then a phrase.
    rec.handle(free(50, true, 100));
    rec.handle(free(50, false, 200));
    double t0 = 10 * kBar + kPpq + 20;
    rec.handle(free(60, true, t0));
    rec.handle(free(60, false, t0 + 100));
    rec.handle(free(62, true, t0 + kPpq));
    rec.handle(free(62, false, t0 + kPpq + 100));
    CHECK(rec.canCapture());
    ClipId id = rec.capture(host.song_.tracks[0].id);
    REQUIRE(id.valid());
    const Clip* clip = host.song_.findClip(id);
    REQUIRE(clip->notes.size() == 2);
    CHECK(clip->notes[0].start == 20); // from the beat the phrase started on
    CHECK(clip->notes[1].start == kPpq + 20);
    CHECK(clip->length == kBar);
    CHECK(host.steps == std::vector<std::string>{"Capture 2 notes"});
    CHECK_FALSE(rec.canCapture());
}

TEST_CASE("recorded notes keep the MIDI channel they came in on") {
    Song song = makeDefaultSong();
    TakeNote played;
    played.length = kPpq;
    played.channel = 3;
    uint64_t next = 100;
    ClipId clip;
    CommandPtr c = recordNewClip(
        song, song.tracks[0].id, 0, ticksPerBar(song.timeSig), {played}, [&next] { return next++; }, &clip, nullptr);
    REQUIRE(c);
    c->apply(song);
    REQUIRE(song.findClip(clip)->notes.size() == 1);
    CHECK(song.findClip(clip)->notes[0].channel == 3);
}
