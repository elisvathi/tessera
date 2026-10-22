#include "doctest.h"

#include "../src/engine/Recorder.hpp"
#include "../src/model/AutomationEdits.hpp"
#include "../src/model/Json.hpp"
#include "../src/model/Serialize.hpp"

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
    std::vector<std::vector<MidiControl>> unlinked;
    bool channelsFull = false;
    void controlsNotRecorded(TrackId, const std::vector<MidiControl>& controls, bool full) override {
        unlinked.push_back(controls);
        channelsFull = full;
    }
};

/// Gives the first track a lane that records `control` on AUTO channel `output`, as "Add lane" does.
void addTrackMidiLane(Song& song, MidiControl control, int output) {
    song.tracks[0].automation.push_back(makeMidiLane(song.newId<AutomationId>(), control, output, 0));
}

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
    s.newControlLanes = false;
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

TEST_CASE("arranger: a new clip over exactly the recorded stretch, stacked over what was there or replacing it") {
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

    for (ArrangerRecordMode mode : {ArrangerRecordMode::Stack, ArrangerRecordMode::Replace}) {
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
        // Stack: the old clip stays whole, the take on top. Replace: the old clip split around the new one.
        bool overdub = mode == ArrangerRecordMode::Stack;
        REQUIRE(arr.size() == (overdub ? 2u : 3u));
        const ArrangerClip* rec1 = nullptr;
        for (const ArrangerClip& a : arr)
            if (a.clip != old.id)
                rec1 = &a;
        REQUIRE(rec1);
        CHECK(rec1->position == kBar + kBar / 2); // punch-in to punch-out, not rounded to bars
        CHECK(rec1->length == 30);
        const Clip* made = host.song_.findClip(rec1->clip);
        REQUIRE(made);
        CHECK(made->notes.size() == 1); // only what was played
        if (overdub) {
            CHECK(arr[0].length == 4 * kBar);
        } else {
            // Only the recorded stretch is cut: the old note at bar 2 + 100 ticks, before punch-in, still plays.
            const ArrangerClip* left = nullptr;
            for (const ArrangerClip& a : arr)
                if (a.clip == old.id && a.position == 0)
                    left = &a;
            REQUIRE(left);
            CHECK(left->length == kBar + kBar / 2);
        }
    }
}

TEST_CASE("arranger: each loop pass becomes its own undo step") {
    Song song = recordSong();
    song.loopStart = 0;
    song.loopEnd = 2 * kBar;
    FakeHost host(song);
    host.mode = PlayMode::Arranger;
    Recorder rec(host);
    rec.settings.arranger = ArrangerRecordMode::Stack;
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
    // Stack: each pass is its own clip, the second stacked over the first.
    const std::vector<ArrangerClip>& arr = host.song_.tracks[0].arrangement;
    REQUIRE(arr.size() == 2);
    CHECK(arr[0].position == 0);
    CHECK(arr[0].length == 2 * kBar);
    CHECK(arr[1].position == 0);
    CHECK(arr[1].length == 30); // the second pass stopped 30 ticks in
    CHECK(host.song_.findClip(arr[1].clip)->notes.size() == 1);
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
        song, song.tracks[0].id, 0, ticksPerBar(song.timeSig), Take({played}), [&next] { return next++; }, &clip,
        nullptr);
    REQUIRE(c);
    c->apply(song);
    REQUIRE(song.findClip(clip)->notes.size() == 1);
    CHECK(song.findClip(clip)->notes[0].channel == 3);
}

namespace {

/// A take lane for `control` with values at the given ticks.
TakeLane takeLane(MidiControl control, std::vector<std::pair<Tick, float>> values) {
    TakeLane l;
    l.control = control;
    for (auto [t, v] : values) {
        AutoPoint p;
        p.t = t;
        p.value = v;
        l.points.push_back(p);
    }
    return l;
}

MidiControl cc(uint8_t n) { return {MidiControl::Kind::Cc, n}; }

} // namespace

TEST_CASE("record: a take's controls become clip lanes; a CC with a lane on the track keeps its channel") {
    Song song = recordSong();
    addTrackMidiLane(song, cc(64), 3);
    song.tracks[0].automation[0].rangeMax = 5;
    Take take;
    TakeNote n;
    n.pressureExpr = {{0, 0.2f, 0}, {1, 0.9f, 0}};
    take.notes.push_back(n);
    take.lanes.push_back(takeLane({MidiControl::Kind::PitchBend, 0}, {{0, 0.5f}, {kPpq, 1}}));
    take.lanes.push_back(takeLane(cc(1), {{0, 0}, {kPpq, 1}}));
    take.lanes.push_back(takeLane(cc(64), {{0, 1}}));
    uint64_t next = 100;
    ClipId clip;
    CommandPtr c = recordNewClip(song, song.tracks[0].id, 0, kBar, take, [&next] { return next++; }, &clip);
    REQUIRE(c);
    c->apply(song);
    const Clip* made = song.findClip(clip);
    REQUIRE(made->notes.size() == 1);
    CHECK(made->notes[0].pressureExpr.size() == 2);
    REQUIRE(made->automation.size() == 3);
    CHECK(made->automation[0].drive == LaneDrive::Pitch);
    CHECK(made->automation[0].name == "Pitch bend");
    CHECK(made->automation[1].name == "Mod wheel (CC 1)");
    CHECK(made->automation[1].outputIndex == 0); // the first free channel
    CHECK(made->automation[2].name == "Sustain (CC 64)");
    CHECK(made->automation[2].midi == cc(64));
    CHECK(made->automation[2].outputIndex == 3); // as the track's lane for it, with its range
    CHECK(made->automation[2].rangeMax == 5);
    CHECK(made->automation[2].id != song.tracks[0].automation[0].id);
    c->revert(song);
    CHECK(!song.findClip(clip));
}

TEST_CASE("record: an overdub writes into the lane that has the control, only over the stretch it covers") {
    Song song = recordSong();
    addTrackMidiLane(song, cc(1), 0);
    uint64_t next = 100;
    ClipId clip;
    Take first;
    first.lanes.push_back(takeLane(cc(1), {{0, 0.1f}, {kBar - 1, 0.1f}}));
    first.notes.push_back(TakeNote{});
    CommandPtr made = recordNewClip(song, song.tracks[0].id, 0, kBar, first, [&next] { return next++; }, &clip);
    made->apply(song);

    // A pass later, wrapping into the loop: only [kPpq, 2 kPpq] changes.
    Take second;
    second.lanes.push_back(takeLane(cc(1), {{kBar + kPpq, 0.8f}, {kBar + 2 * kPpq, 0.9f}}));
    CommandPtr over = recordIntoClip(song, clip, second, [&next] { return next++; });
    REQUIRE(over);
    over->apply(song);
    const Clip* c = song.findClip(clip);
    REQUIRE(c->automation.size() == 1);
    const auto& pts = c->automation[0].points;
    REQUIRE(pts.size() == 4);
    CHECK(pts[0].t == 0);
    CHECK(pts[1].t == kPpq);
    CHECK(pts[1].value == doctest::Approx(0.8f));
    CHECK(pts[3].t == kBar - 1);
    over->revert(song);
    CHECK(song.findClip(clip)->automation[0].points.size() == 2);
}

TEST_CASE("record: a lane marked don't-record keeps its points, and a CC reuses its channel across clips") {
    Song song = recordSong();
    addTrackMidiLane(song, cc(20), 0);
    addTrackMidiLane(song, cc(1), 1);
    uint64_t next = 100;
    ClipId a, b;
    Take take;
    take.notes.push_back(TakeNote{});
    take.lanes.push_back(takeLane(cc(20), {{0, 0.3f}}));
    take.lanes.push_back(takeLane(cc(1), {{0, 0.3f}}));
    recordNewClip(song, song.tracks[0].id, 0, kBar, take, [&next] { return next++; }, &a)->apply(song);
    Take other;
    other.notes.push_back(TakeNote{});
    other.lanes.push_back(takeLane(cc(1), {{0, 0.6f}}));
    recordNewClip(song, song.tracks[0].id, 1, kBar, other, [&next] { return next++; }, &b)->apply(song);
    CHECK(song.findClip(b)->automation[0].outputIndex == song.findClip(a)->automation[1].outputIndex);

    song.findClip(a)->automation[0].noRecord = true;
    Take again;
    again.lanes.push_back(takeLane(cc(20), {{0, 0.9f}}));
    CHECK(!recordIntoClip(song, a, again, [&next] { return next++; })); // nothing changes: no empty undo step
    CHECK(song.findClip(a)->automation[0].points[0].value == doctest::Approx(0.3f));
}

TEST_CASE("record: thinning keeps the shape with few points") {
    std::vector<double> ticks;
    std::vector<float> values;
    for (int i = 0; i <= 100; i++) {
        ticks.push_back(i);
        values.push_back(i <= 50 ? i / 50.f : 1.f); // a ramp, then flat
    }
    std::vector<size_t> kept = thinValues(ticks, values, 0.01f, 64);
    REQUIRE(kept.size() == 3);
    CHECK(kept[1] == 50);
    // A wiggle needs more points than allowed: the tolerance loosens until it fits.
    for (int i = 0; i <= 100; i++)
        values[size_t(i)] = float(i % 2);
    CHECK(thinValues(ticks, values, 0.01f, 8).size() <= 8);
}

TEST_CASE("record: lanes save and load their drive and MIDI control") {
    Clip clip;
    clip.id = ClipId(1);
    AutomationEnvelope lane = makeMidiLane(AutomationId(5), {MidiControl::Kind::PitchBend, 0}, 0, 0);
    lane.noRecord = true;
    clip.automation.push_back(lane);
    clip.automation.push_back(makeLane(AutomationId(6), 2, 0));
    json_t* j = clipToJson(clip);
    Clip back = clipFromJson(j);
    json_decref(j);
    REQUIRE(back.automation.size() == 2);
    CHECK(back.automation[0] == clip.automation[0]);
    CHECK(back.automation[1] == clip.automation[1]);
}

namespace {

/// A per-note live value from MIDI channel `channel`.
LiveValue noteValue(int channel, int pitch, NoteCurve curve, float value) {
    LiveValue v;
    v.channel = uint8_t(channel);
    v.pitch = int8_t(pitch);
    v.curve = curve;
    v.value = value;
    return v;
}

/// A value MIDI channel `channel` shares.
LiveValue channelValue(int channel, MidiControl control, float value) {
    LiveValue v;
    v.channel = uint8_t(channel);
    v.perNote = false;
    v.control = control;
    v.value = value;
    return v;
}

} // namespace

TEST_CASE("live expression: a key's pressure and bend reach the voice it plays, and only that one") {
    EngineRig rig(recordSong());
    size_t slot = size_t(rig.slots.slotOf(rig.song.tracks[0].id));
    rig.engine.liveNote({LiveSource::Midi, 0, 60, 0.8f, true});
    rig.engine.liveNote({LiveSource::Midi, 0, 64, 0.8f, true});
    rig.engine.liveValue(noteValue(0, 64, NoteCurve::Pressure, 0.5f));
    rig.engine.liveValue(noteValue(0, 64, NoteCurve::Pitch, 12));
    rig.run(1);
    const TrackFrame& f = rig.frames[slot];
    CHECK(f.press[0] == doctest::Approx(0));
    CHECK(f.press[1] == doctest::Approx(5));
    CHECK(f.voct[1] == doctest::Approx(pitchToVolts(64) + 1));

    // A new note on the voice starts flat (voices are handed out in turn: the fourth new note gets voice 1).
    rig.engine.liveNote({LiveSource::Midi, 0, 64, 0, false});
    rig.engine.liveNote({LiveSource::Midi, 0, 60, 0, false});
    for (int pitch : {62, 65, 67, 69})
        rig.engine.liveNote({LiveSource::Midi, 0, int8_t(pitch), 0.8f, true});
    rig.run(1);
    CHECK(f.press[1] == doctest::Approx(0));
}

TEST_CASE("live expression: an MPE value for the whole channel reaches its one note") {
    EngineRig rig(recordSong());
    size_t slot = size_t(rig.slots.slotOf(rig.song.tracks[0].id));
    LiveNote n{LiveSource::Midi, 3, 60, 0.8f, true};
    n.partChannel = 0;
    rig.engine.liveNote(n);
    LiveValue v = noteValue(3, -1, NoteCurve::Timbre, 1);
    v.partChannel = 0;
    rig.engine.liveValue(v);
    rig.run(1);
    CHECK(rig.frames[slot].timbre[0] == doctest::Approx(10));
}

TEST_CASE("live expression: channel bend and pressure play on every voice of an armed, monitoring track") {
    Song song = recordSong();
    EngineRig rig(song);
    size_t slot = size_t(rig.slots.slotOf(rig.song.tracks[0].id));
    rig.engine.liveNote({LiveSource::Midi, 0, 60, 0.8f, true});
    rig.engine.liveValue(channelValue(0, {MidiControl::Kind::PitchBend, 0}, 1));
    rig.engine.liveValue(channelValue(0, {MidiControl::Kind::Pressure, 0}, 0.3f));
    rig.run(1);
    const TrackFrame& f = rig.frames[slot];
    CHECK(f.voct[0] == doctest::Approx(pitchToVolts(60) + 2 / 12.f)); // the default ±2 semitones
    CHECK(f.press[0] == doctest::Approx(3));
    // The track that isn't armed hears nothing of it.
    size_t other = size_t(rig.slots.slotOf(rig.song.tracks[1].id));
    CHECK(rig.frames[other].press[0] == doctest::Approx(0));
}

TEST_CASE("live expression: a CC plays on the AUTO channel of the lane that records it") {
    Song song = recordSong();
    AutomationEnvelope lane = makeMidiLane(AutomationId(900), {MidiControl::Kind::Cc, 1}, 2, 0);
    song.tracks[0].automation.push_back(lane);
    EngineRig rig(song);
    size_t slot = size_t(rig.slots.slotOf(rig.song.tracks[0].id));
    rig.engine.liveValue(channelValue(0, {MidiControl::Kind::Cc, 1}, 0.5f));
    rig.engine.liveValue(channelValue(0, {MidiControl::Kind::Cc, 2}, 1)); // no lane yet: the first free channel
    rig.engine.liveValue(channelValue(0, {MidiControl::Kind::Cc, 0}, 1)); // bank select: nowhere
    rig.run(1);
    REQUIRE(rig.frames[slot].autoChannels >= 3);
    CHECK(rig.frames[slot].automation[2] == doctest::Approx(5));
    CHECK(rig.frames[slot].automation[0] == doctest::Approx(10));
    CHECK(rig.frames[slot].automation[1] == doctest::Approx(0));
}

TEST_CASE("live expression: with new lanes off, a CC with no lane plays nowhere") {
    EngineRig rig(recordSong());
    rig.engine.setNewControlLanes(false);
    size_t slot = size_t(rig.slots.slotOf(rig.song.tracks[0].id));
    rig.engine.liveValue(channelValue(0, {MidiControl::Kind::Cc, 2}, 1));
    rig.run(1);
    CHECK(rig.frames[slot].automation[0] == doctest::Approx(0));
}

TEST_CASE("live expression: armed tracks report values to the recorder while recording") {
    EngineRig rig(recordSong());
    rig.engine.toggleRecord(0);
    rig.engine.liveNote({LiveSource::Midi, 0, 60, 0.8f, true});
    rig.drain();
    rig.engine.liveValue(noteValue(0, 60, NoteCurve::Pressure, 0.7f));
    rig.engine.liveValue(channelValue(0, {MidiControl::Kind::PitchBend, 0}, 0));
    std::vector<RecordEvent> events;
    for (const RecordEvent& e : rig.drain())
        if (e.track != 0) // the capture buffer's copies aside
            events.push_back(e);
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == RecordEvent::Kind::NoteValue);
    CHECK(events[0].pitch == 60);
    CHECK(events[0].value == doctest::Approx(0.7f));
    CHECK(events[1].kind == RecordEvent::Kind::ChannelValue);
    CHECK(events[1].value == doctest::Approx(0.5f)); // bend centre is the lane's middle
}

TEST_CASE("live expression: a pitch bend lane plays on V/OCT and rests at 0 without one") {
    Song song = recordSong();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = kBar;
    Note n;
    n.id = song.newId<NoteId>();
    n.length = kBar;
    clip.notes.push_back(n);
    AutomationEnvelope bend = makeMidiLane(song.newId<AutomationId>(), {MidiControl::Kind::PitchBend, 0}, 0, 0);
    AutoPoint p;
    p.value = 1;
    bend.points.push_back(p);
    clip.automation.push_back(bend);
    song.tracks[1].slots[0].clip = clip.id;
    song.clips[clip.id] = clip;
    EngineRig rig(song);
    size_t slot = size_t(rig.slots.slotOf(rig.song.tracks[1].id));
    rig.engine.launch({LaunchEvent::Kind::Slot, rig.song.tracks[1].id.value, 0, true, false});
    rig.engine.play();
    rig.run(200);
    CHECK(rig.frames[slot].voct[0] == doctest::Approx(pitchToVolts(60) + 2 / 12.f));
    rig.engine.launch({LaunchEvent::Kind::Slot, rig.song.tracks[1].id.value, 1, true, false}); // an empty slot
    rig.run(20000);
    CHECK(rig.frames[slot].voct[0] == doctest::Approx(pitchToVolts(60)));
}

namespace {

/// A recorded value event for an armed track.
RecordEvent valueEvent(const Track& t, RecordEvent::Kind kind, double play, float value) {
    RecordEvent e = note(t, 60, true, play, play);
    e.kind = kind;
    e.value = value;
    return e;
}

} // namespace

TEST_CASE("recorder: what a held key played becomes the note's curves") {
    FakeHost host(recordSong());
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 0, 0));
    for (int i = 1; i <= 10; i++) {
        RecordEvent e = valueEvent(t, RecordEvent::Kind::NoteValue, i * kPpq / 10.0, i / 10.f);
        e.curve = NoteCurve::Pressure;
        rec.handle(e);
    }
    RecordEvent other = valueEvent(t, RecordEvent::Kind::NoteValue, kPpq / 2.0, 1);
    other.pitch = 61; // no such note held: ignored
    rec.handle(other);
    rec.handle(note(t, 60, false, kPpq, kPpq));
    rec.handle(punch(RecordEvent::Kind::PunchOut, kBar, kBar));
    const Clip* c = slotClip(host.song_, 0, 0);
    REQUIRE(c);
    REQUIRE(c->notes.size() == 1);
    const auto& curve = c->notes[0].pressureExpr;
    // A straight rise from 0 at the start: two points are enough.
    REQUIRE(curve.size() == 2);
    CHECK(curve[0].pos == doctest::Approx(0));
    CHECK(curve[0].value == doctest::Approx(0));
    CHECK(curve[1].pos == doctest::Approx(1));
    CHECK(curve[1].value == doctest::Approx(1));
    CHECK(c->notes[0].pitchExpr.empty());
}

TEST_CASE("recorder: moved controls become lanes in the take's clip, in the same undo step") {
    FakeHost host(recordSong());
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 0, 0));
    for (int i = 0; i <= 4; i++) {
        RecordEvent wheel = valueEvent(t, RecordEvent::Kind::ChannelValue, i * kPpq / 4.0, i / 4.f);
        wheel.control = {MidiControl::Kind::Cc, 1};
        rec.handle(wheel);
    }
    double at = 0;
    for (float v : {0.f, 1.f, 1.f, 0.f}) {
        RecordEvent pedal = valueEvent(t, RecordEvent::Kind::ChannelValue, at += kPpq / 4.0, v);
        pedal.control = {MidiControl::Kind::Cc, 64};
        rec.handle(pedal);
    }
    rec.handle(note(t, 60, false, kPpq, kPpq));
    rec.handle(punch(RecordEvent::Kind::PunchOut, kBar, kBar));
    REQUIRE(host.steps.size() == 1);
    const Clip* c = slotClip(host.song_, 0, 0);
    REQUIRE(c);
    REQUIRE(c->automation.size() == 2);
    CHECK(c->automation[0].name == "Mod wheel (CC 1)");
    CHECK(c->automation[0].points.size() == 2); // a straight ramp
    CHECK(c->automation[1].name == "Sustain (CC 64)");
    REQUIRE(c->automation[1].points.size() == 3); // off, on, off
    CHECK(c->automation[1].points[1].curve == CurveType::Hold);
    CHECK(host.unlinked.empty());
}

TEST_CASE("recorder: a moved CC gets a clip lane; bank select and a CC sent once don't") {
    FakeHost host(recordSong());
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    auto move = [&](MidiControl control, double at, float value) {
        RecordEvent e = valueEvent(t, RecordEvent::Kind::ChannelValue, at, value);
        e.control = control;
        rec.handle(e);
    };
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 0, 0));
    move(cc(1), kPpq, 0.2f);
    move(cc(1), 2 * kPpq, 0.6f);
    move(cc(0), kPpq, 1);    // bank select
    move(cc(7), kPpq, 0.8f); // a volume sent once
    rec.handle(note(t, 60, false, kPpq, kPpq));
    rec.handle(punch(RecordEvent::Kind::PunchOut, kBar, kBar));
    const Clip* c = slotClip(host.song_, 0, 0);
    REQUIRE(c);
    REQUIRE(c->automation.size() == 1);
    CHECK(c->automation[0].midi == cc(1));
    CHECK(host.song_.tracks[0].automation.empty()); // the track itself gets no lane
    CHECK(host.unlinked.empty());
}

TEST_CASE("recorder: with every AUTO channel in use a new CC is left out and reported") {
    Song song = recordSong();
    for (int o = 0; o < kMaxAutomationLanes; o++)
        song.tracks[0].automation.push_back(makeLane(song.newId<AutomationId>(), o, 0));
    FakeHost host(song);
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 0, 0));
    for (float v : {0.f, 1.f}) {
        RecordEvent e = valueEvent(t, RecordEvent::Kind::ChannelValue, kPpq * (1 + v), v);
        e.control = cc(1);
        rec.handle(e);
    }
    rec.handle(note(t, 60, false, kPpq, kPpq));
    rec.handle(punch(RecordEvent::Kind::PunchOut, kBar, kBar));
    REQUIRE(slotClip(host.song_, 0, 0));
    CHECK(slotClip(host.song_, 0, 0)->automation.empty());
    REQUIRE(host.unlinked.size() == 1);
    CHECK(host.channelsFull);
}

TEST_CASE("recorder: with new lanes off, a CC with no lane is not recorded and is reported once per recording") {
    FakeHost host(recordSong());
    Recorder rec(host);
    rec.settings.newControlLanes = false;
    const Track& t = host.song_.tracks[0];
    auto move = [&](MidiControl control, double at) {
        RecordEvent e = valueEvent(t, RecordEvent::Kind::ChannelValue, at, 0.5f);
        e.control = control;
        rec.handle(e);
    };
    rec.settings.clipBars = 1;
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 0, 0));
    move(cc(1), kPpq);
    move(cc(0), kPpq); // bank select: never a lane, never reported
    rec.handle(note(t, 60, false, kPpq, kPpq));
    rec.update(kBar + 1); // the first pass ends
    move(cc(1), kBar + kPpq);
    rec.handle(punch(RecordEvent::Kind::PunchOut, 2 * kBar, 2 * kBar));
    const Clip* c = slotClip(host.song_, 0, 0);
    REQUIRE(c);
    CHECK(c->automation.empty());
    REQUIRE(host.unlinked.size() == 1);
    REQUIRE(host.unlinked[0].size() == 1);
    CHECK(host.unlinked[0][0] == cc(1));
    CHECK(!host.channelsFull);
}

TEST_CASE("recorder: a take of only control moves still makes a clip") {
    FakeHost host(recordSong());
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    RecordEvent bend = valueEvent(t, RecordEvent::Kind::ChannelValue, kPpq, 0.8f);
    bend.control = {MidiControl::Kind::PitchBend, 0};
    rec.handle(bend);
    rec.handle(punch(RecordEvent::Kind::PunchOut, kBar, kBar));
    const Clip* c = slotClip(host.song_, 0, 0);
    REQUIRE(c);
    CHECK(c->notes.empty());
    REQUIRE(c->automation.size() == 1);
    CHECK(c->automation[0].drive == LaneDrive::Pitch);
}

namespace {

/// A knob lane reading for the recorder, on the timeline (no clip playing).
RecordEvent laneReading(const Track& t, const AutomationEnvelope& lane, double play, float value, double seconds) {
    RecordEvent e = note(t, 60, true, play, play);
    e.kind = RecordEvent::Kind::LaneValue;
    e.lane = lane.id.value;
    e.value = value;
    e.seconds = seconds;
    return e;
}

/// A track lane that records from AUTO REC in Write mode.
AutomationEnvelope cvLane(Song& song, int output) {
    AutomationEnvelope lane = makeLane(song.newId<AutomationId>(), output, 0);
    lane.source = AutoSource::Cv;
    lane.recordMode = AutoRecordMode::Write;
    return lane;
}

} // namespace

TEST_CASE("recorder: on the timeline, a CV lane's take goes to the track lane, with the notes in one undo step") {
    Song song = recordSong();
    song.tracks[0].automation.push_back(cvLane(song, 2));
    FakeHost host(song);
    host.mode = PlayMode::Arranger;
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    const AutomationEnvelope& lane = t.automation[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 0, 0));
    for (int i = 0; i <= 8; i++)
        rec.handle(laneReading(t, lane, i * kPpq / 2.0, i / 8.f, i * 0.1));
    rec.handle(note(t, 60, false, kPpq, kPpq));
    rec.handle(punch(RecordEvent::Kind::PunchOut, 4 * kPpq, 4 * kPpq));
    REQUIRE(host.steps.size() == 1);
    const AutomationEnvelope& recorded = host.song_.tracks[0].automation[0];
    REQUIRE(recorded.points.size() >= 2);
    CHECK(recorded.points.front().value == doctest::Approx(0));
    CHECK(recorded.points.back().value == doctest::Approx(1));
    CHECK(host.song_.tracks[0].arrangement.size() == 1); // the notes' clip
}

TEST_CASE("recorder: in a launcher take, a CV lane's take goes to the clip's lane on the same output") {
    Song song = recordSong();
    song.tracks[0].automation.push_back(cvLane(song, 3));
    FakeHost host(song);
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    const AutomationEnvelope& lane = t.automation[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    for (int i = 0; i <= 4; i++)
        rec.handle(laneReading(t, lane, i * kPpq / 2.0, 0.5f, i * 0.1));
    rec.handle(punch(RecordEvent::Kind::PunchOut, kBar, kBar));
    const Clip* c = slotClip(host.song_, 0, 0);
    REQUIRE(c);
    REQUIRE(c->automation.size() == 1);
    CHECK(c->automation[0].outputIndex == 3);
    CHECK(c->automation[0].targets.empty());
    CHECK(host.song_.tracks[0].automation[0].points.empty()); // the track lane is untouched
}

TEST_CASE("recorder: a lane marked don't-record keeps its points") {
    Song song = recordSong();
    AutomationEnvelope kept = cvLane(song, 0);
    kept.noRecord = true;
    kept.points.push_back({0, 0.25f, CurveType::Linear, 0});
    song.tracks[0].automation.push_back(kept);
    FakeHost host(song);
    host.mode = PlayMode::Arranger;
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    rec.handle(note(t, 60, true, 0, 0));
    rec.handle(laneReading(t, t.automation[0], 0, 1, 0));
    rec.handle(laneReading(t, t.automation[0], kPpq, 1, 0.5));
    rec.handle(note(t, 60, false, kPpq, kPpq));
    rec.handle(punch(RecordEvent::Kind::PunchOut, kBar, kBar));
    REQUIRE(host.song_.tracks[0].automation[0].points.size() == 1);
    CHECK(host.song_.tracks[0].automation[0].points[0].value == doctest::Approx(0.25f));
}

TEST_CASE("lane record state: the track arm decides, then the lane's source") {
    Song song = recordSong();
    AutomationEnvelope drawn = makeLane(song.newId<AutomationId>(), 0, 0);
    AutomationEnvelope cv = cvLane(song, 1);
    AutomationEnvelope wheel = makeMidiLane(song.newId<AutomationId>(), {MidiControl::Kind::Cc, 1}, 2, 0);
    AutomationEnvelope kept = wheel;
    kept.noRecord = true;
    LaneOwner armed = LaneOwner::ofTrack(song.tracks[0].id), idle = LaneOwner::ofTrack(song.tracks[1].id);
    auto state = [&](LaneOwner o, const AutomationEnvelope& l, int inputs) {
        return laneRecordState(song, o, l, inputs, nullptr);
    };
    CHECK(state(idle, wheel, 0) == LaneRecordState::Off);
    CHECK(state(armed, drawn, 0) == LaneRecordState::Off); // drawn by hand: no warning
    CHECK(state(armed, wheel, 0) == LaneRecordState::Records);
    CHECK(state(armed, kept, 0) == LaneRecordState::Kept);
    std::string why;
    CHECK(laneRecordState(song, armed, cv, 1, nullptr, &why) == LaneRecordState::Missing);
    CHECK(why == "Nothing patched to AUTO REC channel 2");
    CHECK(state(armed, cv, 2) == LaneRecordState::Records);
    CHECK(laneSourceLabel(cv) == "AUTO REC 2");
    CHECK(laneSourceLabel(wheel) == "CC 1");
    CHECK(laneOutputLabel(makeMidiLane(AutomationId(1), {MidiControl::Kind::PitchBend, 0}, 0, 0)) == "V/OCT");
}

TEST_CASE("capture keeps what the keys played and the controls moved during the phrase") {
    Song song = recordSong();
    addTrackMidiLane(song, cc(1), 0);
    FakeHost host(song);
    Recorder rec(host);
    auto free = [](RecordEvent e, double at) {
        e.track = 0;
        e.recording = e.playing = false;
        e.freeTick = at;
        return e;
    };
    const Track& t = host.song_.tracks[0];
    RecordEvent wheel = valueEvent(t, RecordEvent::Kind::ChannelValue, 0, 0.7f);
    wheel.control = {MidiControl::Kind::Cc, 1};
    rec.handle(free(wheel, 0)); // where the wheel sat before the phrase
    rec.handle(free(note(t, 60, true, 0, 0), kBar));
    for (int i = 1; i <= 4; i++) {
        RecordEvent press = valueEvent(t, RecordEvent::Kind::NoteValue, 0, i / 4.f);
        press.curve = NoteCurve::Pressure;
        rec.handle(free(press, kBar + i * kPpq / 4.0));
    }
    wheel.value = 1;
    rec.handle(free(wheel, kBar + kPpq / 2.0));
    rec.handle(free(note(t, 60, false, 0, 0), kBar + kPpq));
    ClipId clip = rec.capture(t.id);
    const Clip* c = host.song_.findClip(clip);
    REQUIRE(c);
    REQUIRE(c->notes.size() == 1);
    REQUIRE(!c->notes[0].pressureExpr.empty());
    CHECK(c->notes[0].pressureExpr.back().value == doctest::Approx(1));
    REQUIRE(c->automation.size() == 1);
    CHECK(c->automation[0].name == "Mod wheel (CC 1)");
    const auto& pts = c->automation[0].points;
    REQUIRE(pts.size() == 2);
    CHECK(pts[0].t == 0); // the first note starts the clip on the beat
    CHECK(pts[0].value == doctest::Approx(0.7f));
    CHECK(pts[1].t == kPpq / 2);
    CHECK(pts[1].value == doctest::Approx(1));
}

TEST_CASE("stacked timeline clips: the clip on top plays the lanes it has, the ones below keep the rest") {
    Song song = recordSong();
    auto clipWith = [&](int output, float value) {
        Clip c;
        c.id = song.newId<ClipId>();
        c.length = c.loopEnd = 2 * kBar;
        AutomationEnvelope lane = makeLane(song.newId<AutomationId>(), output, 0);
        lane.points.push_back({0, value, CurveType::Linear, 0});
        c.automation.push_back(lane);
        song.clips[c.id] = c;
        return c.id;
    };
    auto place = [&](ClipId clip, Tick at, Tick length) {
        ArrangerClip a;
        a.id = song.newId<ArrangementId>();
        a.clip = clip;
        a.position = at;
        a.length = length;
        song.tracks[1].arrangement.push_back(a);
    };
    ClipId below = clipWith(0, 0.2f);
    song.clips[below].automation.push_back(makeLane(song.newId<AutomationId>(), 1, 0));
    song.clips[below].automation.back().points.push_back({0, 0.3f, CurveType::Linear, 0});
    place(below, 0, 4 * kBar);
    place(clipWith(0, 0.9f), kBar, kBar); // a later take on AUTO 1 only
    EngineRig rig(song);
    rig.engine.setPlayMode(PlayMode::Arranger);
    rig.engine.play();
    size_t slot = size_t(rig.slots.slotOf(rig.song.tracks[1].id));
    rig.run(int(kBar * 1.5 * 32)); // into the take
    CHECK(rig.frames[slot].automation[0] == doctest::Approx(9));
    CHECK(rig.frames[slot].automation[1] == doctest::Approx(3)); // the clip below still plays AUTO 2
}

namespace {

/// A song whose first track has a looping one-bar clip with one note, placed at bar 2 for two bars, starting half a
/// bar into the clip.
Song mergeSong(ClipId& clipId) {
    Song song = recordSong();
    Clip clip;
    clip.id = clipId = song.newId<ClipId>();
    clip.length = clip.loopEnd = kBar;
    clip.loopMode = LoopMode::Loop;
    Note n;
    n.id = song.newId<NoteId>();
    n.start = 0;
    n.length = kPpq;
    n.pitch = 40;
    clip.notes.push_back(n);
    song.clips[clip.id] = clip;
    ArrangerClip inst;
    inst.id = song.newId<ArrangementId>();
    inst.clip = clip.id;
    inst.position = kBar;
    inst.length = 2 * kBar;
    inst.offset = kBar / 2;
    song.tracks[0].arrangement.push_back(inst);
    return song;
}

} // namespace

TEST_CASE("arranger merge: a mod wheel pass goes into the clip that plays there, notes left alone") {
    ClipId id;
    FakeHost host(mergeSong(id));
    host.mode = PlayMode::Arranger;
    Recorder rec(host);
    CHECK(rec.settings.arranger == ArrangerRecordMode::Merge); // the default
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, kBar));
    for (int i = 0; i <= 2; i++) {
        RecordEvent wheel = valueEvent(t, RecordEvent::Kind::ChannelValue, 0, i / 2.f);
        wheel.songTick = kBar + i * kPpq;
        wheel.control = cc(1);
        rec.handle(wheel);
    }
    rec.handle(punch(RecordEvent::Kind::PunchOut, 3 * kPpq, kBar + 3 * kPpq));
    REQUIRE(host.steps.size() == 1);
    CHECK(host.song_.tracks[0].arrangement.size() == 1); // no new clip on the timeline
    const Clip* c = host.song_.findClip(id);
    CHECK(c->notes.size() == 1);
    REQUIRE(c->automation.size() == 1);
    CHECK(c->automation[0].midi == cc(1));
    const auto& pts = c->automation[0].points;
    REQUIRE(!pts.empty());
    // The instance starts half a bar into the clip: the first value lands there, and the loop wraps the rest.
    CHECK(pts.front().t <= kBar);
    for (const AutoPoint& p : pts)
        CHECK(p.t < kBar);
}

TEST_CASE("arranger merge: notes land in the clip at the time it plays; past the clip they make a new one") {
    ClipId id;
    FakeHost host(mergeSong(id));
    host.mode = PlayMode::Arranger;
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, kBar));
    // Timeline bar 2 + 3/4 bar: half a bar in plus 3/4 is 1 1/4 bars into the clip, so 1/4 bar after the loop wraps.
    rec.handle(note(t, 60, true, 3 * kPpq, kBar + 3 * kPpq));
    rec.handle(note(t, 60, false, 3 * kPpq + 100, kBar + 3 * kPpq + 100));
    rec.handle(note(t, 62, true, 2 * kBar + 10, 3 * kBar + 10)); // after the instance ends
    rec.handle(note(t, 62, false, 2 * kBar + 20, 3 * kBar + 20));
    rec.handle(punch(RecordEvent::Kind::PunchOut, 2 * kBar + 30, 3 * kBar + 30));
    REQUIRE(host.steps.size() == 1); // one undo step for both
    const Clip* c = host.song_.findClip(id);
    REQUIRE(c->notes.size() == 2);
    const Note& added = c->notes[0].pitch == 60 ? c->notes[0] : c->notes[1];
    CHECK(added.pitch == 60);
    CHECK(added.start == kPpq);
    CHECK(added.length == 100);
    const auto& arr = host.song_.tracks[0].arrangement;
    REQUIRE(arr.size() == 2);
    const ArrangerClip& made = arr[0].clip == id ? arr[1] : arr[0];
    CHECK(made.position == 3 * kBar + 10); // from its own first note, not from where recording began
    REQUIRE(host.song_.findClip(made.clip)->notes.size() == 1);
    CHECK(host.song_.findClip(made.clip)->notes[0].pitch == 62);
}

TEST_CASE("arranger merge: a second loop pass goes into the clip the first pass made") {
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
    rec.handle(note(t, 64, true, 2 * kBar + 10, 10));
    rec.handle(note(t, 64, false, 2 * kBar + 20, 20));
    rec.handle(punch(RecordEvent::Kind::PunchOut, 2 * kBar + 30, 30));
    CHECK(host.steps.size() == 2);
    const std::vector<ArrangerClip>& arr = host.song_.tracks[0].arrangement;
    REQUIRE(arr.size() == 1);
    CHECK(host.song_.findClip(arr[0].clip)->notes.size() == 2);
}

TEST_CASE("record settings: \"overdub\" from older saves loads as Stack; no setting means Merge") {
    json::Ref old(json_pack("{s:s}", "arranger", "overdub"));
    CHECK(RecordSettings::fromJson(old.get()).arranger == ArrangerRecordMode::Stack);
    json::Ref none(json_object());
    CHECK(RecordSettings::fromJson(none.get()).arranger == ArrangerRecordMode::Merge);
    RecordSettings s;
    s.arranger = ArrangerRecordMode::Stack;
    json::Ref j(s.toJson());
    CHECK(RecordSettings::fromJson(j.get()).arranger == ArrangerRecordMode::Stack);
}

TEST_CASE("recorder: a control the track's own lane records goes into that lane, at the song position") {
    Song song = recordSong();
    addTrackMidiLane(song, cc(1), 2);
    FakeHost host(song);
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 4 * kBar));
    rec.handle(note(t, 60, true, 0, 4 * kBar));
    for (int i = 0; i <= 2; i++) {
        RecordEvent wheel = valueEvent(t, RecordEvent::Kind::ChannelValue, i * kPpq, i / 2.f);
        wheel.songTick = 4 * kBar + i * kPpq;
        wheel.control = cc(1);
        rec.handle(wheel);
    }
    rec.handle(note(t, 60, false, kPpq, 4 * kBar + kPpq));
    rec.handle(punch(RecordEvent::Kind::PunchOut, kBar, 5 * kBar));
    REQUIRE(host.steps.size() == 1); // the clip and the track lane in one undo step
    const Clip* c = slotClip(host.song_, 0, 0);
    REQUIRE(c);
    CHECK(c->automation.empty());
    const auto& pts = host.song_.tracks[0].automation[0].points;
    REQUIRE(pts.size() >= 2);
    CHECK(pts.front().t == 4 * kBar);
    CHECK(pts.back().value == doctest::Approx(1));
}

TEST_CASE("recorder: a take of only a track lane's control makes no clip") {
    Song song = recordSong();
    addTrackMidiLane(song, cc(1), 0);
    FakeHost host(song);
    Recorder rec(host);
    const Track& t = host.song_.tracks[0];
    rec.handle(punch(RecordEvent::Kind::PunchIn, 0, 0));
    for (int i = 0; i <= 1; i++) {
        RecordEvent wheel = valueEvent(t, RecordEvent::Kind::ChannelValue, i * kPpq, float(i));
        wheel.songTick = i * kPpq;
        wheel.control = cc(1);
        rec.handle(wheel);
    }
    rec.handle(punch(RecordEvent::Kind::PunchOut, kBar, kBar));
    CHECK(!slotClip(host.song_, 0, 0));
    CHECK(host.song_.tracks[0].automation[0].points.size() == 2);
}
