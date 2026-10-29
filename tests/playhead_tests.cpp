#include "doctest.h"

#include "../src/model/Playheads.hpp"

#include <set>
#include <vector>

using namespace tessera;
using namespace tessera::playheads;

namespace {

constexpr double kBeat = double(kPpq);

struct N {
    double start, end;
    int pitch;
};

struct Hit {
    double time;
    int pitch;
    bool operator==(const Hit& o) const { return std::abs(time - o.time) < 1e-6 && pitch == o.pitch; }
};

/// Four quarter notes in a one-bar region: pitches 60, 62, 64, 65 on beats 1-4.
std::vector<N> fourBeats() { return {{0, 480, 60}, {960, 1440, 62}, {1920, 2400, 64}, {2880, 3360, 65}}; }

Motion oneBar() {
    Motion m;
    m.regionStart = 0;
    m.regionLength = 4 * kBeat;
    return m;
}

/// Plays `m` over [0, until) in windows of `step` ticks, as the engine does sample by sample.
std::vector<Hit> play(const Motion& m, const std::vector<N>& notes, double until, double step = 7.3) {
    std::vector<uint32_t> byEnd = orderByEnd(notes);
    std::vector<Hit> hits;
    for (double a = 0; a < until; a += step) {
        double b = std::min(until, a + step);
        Sweep s[kMaxSweeps];
        int n = sweeps(m, a, b, s);
        for (int i = 0; i < n; i++)
            crossed(s[i], notes, byEnd,
                    [&](const N& note, double pos) { hits.push_back({timeAt(m, s[i], pos), note.pitch}); });
    }
    return hits;
}

} // namespace

TEST_CASE("a playhead at 1x going forward plays each note at its start") {
    std::vector<Hit> hits = play(oneBar(), fourBeats(), 8 * kBeat);
    CHECK(hits ==
          std::vector<Hit>{{0, 60}, {960, 62}, {1920, 64}, {2880, 65}, {3840, 60}, {4800, 62}, {5760, 64}, {6720, 65}});
}

TEST_CASE("the window size doesn't change what plays") {
    CHECK(play(oneBar(), fourBeats(), 8 * kBeat, 1) == play(oneBar(), fourBeats(), 8 * kBeat, 500));
}

TEST_CASE("a playhead at 2x plays twice as fast") {
    Motion m = oneBar();
    m.rate = 2;
    std::vector<Hit> hits = play(m, fourBeats(), 4 * kBeat);
    CHECK(hits ==
          std::vector<Hit>{{0, 60}, {480, 62}, {960, 64}, {1440, 65}, {1920, 60}, {2400, 62}, {2880, 64}, {3360, 65}});
}

TEST_CASE("a later start begins the canon partway in") {
    Motion m = oneBar();
    m.start = 2 * kBeat;
    std::vector<Hit> hits = play(m, fourBeats(), 4 * kBeat);
    CHECK(hits == std::vector<Hit>{{0, 64}, {960, 65}, {1920, 60}, {2880, 62}});
}

TEST_CASE("going backward plays each note when the playhead reaches its end") {
    Motion m = oneBar();
    m.direction = Direction::Reverse;
    // From the region's start it wraps to the end at once: the last note ends at 3360, reached 480 ticks later.
    std::vector<Hit> hits = play(m, fourBeats(), 4 * kBeat);
    CHECK(hits == std::vector<Hit>{{480, 65}, {1440, 64}, {2400, 62}, {3360, 60}});
}

TEST_CASE("a note that ends at the region's end plays as soon as a backward playhead wraps there") {
    std::vector<N> notes = {{0, 960, 60}, {2880, 3840, 67}};
    Motion m = oneBar();
    m.direction = Direction::Reverse;
    std::vector<Hit> hits = play(m, notes, 4 * kBeat);
    CHECK(hits == std::vector<Hit>{{0, 67}, {2880, 60}});
}

TEST_CASE("ping-pong turns at the region's edges without playing the turning note twice") {
    Motion m = oneBar();
    m.direction = Direction::PingPong;
    std::vector<Hit> hits = play(m, fourBeats(), 8 * kBeat);
    // Forward over beats 1-4, then back: beat 4's note ends before the turn, so going back reaches it 480 later.
    CHECK(hits ==
          std::vector<Hit>{{0, 60}, {960, 62}, {1920, 64}, {2880, 65}, {4320, 65}, {5280, 64}, {6240, 62}, {7200, 60}});
}

TEST_CASE("freeze plays nothing") {
    Motion m = oneBar();
    m.direction = Direction::Freeze;
    CHECK(play(m, fourBeats(), 8 * kBeat).empty());
}

TEST_CASE("repeat plays each segment its number of times, then moves on one segment") {
    Motion m = oneBar();
    m.repeatSize = kBeat;
    m.repeatTimes = 2;
    std::vector<Hit> hits = play(m, fourBeats(), 8 * kBeat);
    CHECK(hits ==
          std::vector<Hit>{{0, 60}, {960, 60}, {1920, 62}, {2880, 62}, {3840, 64}, {4800, 64}, {5760, 65}, {6720, 65}});
}

TEST_CASE("a region reads only part of the clip and loops over it") {
    Motion m = oneBar();
    m.regionStart = 2 * kBeat;
    m.regionLength = 2 * kBeat;
    std::vector<Hit> hits = play(m, fourBeats(), 4 * kBeat);
    CHECK(hits == std::vector<Hit>{{0, 64}, {960, 65}, {1920, 64}, {2880, 65}});
}

TEST_CASE("restarting every bar brings the playhead back to its start") {
    Motion m = oneBar();
    m.rate = 0.5;
    m.restartEvery = 4 * kBeat;
    std::vector<Hit> hits = play(m, fourBeats(), 8 * kBeat);
    CHECK(hits == std::vector<Hit>{{0, 60}, {1920, 62}, {3840, 60}, {5760, 62}});
}

TEST_CASE("bend moves notes inside its cycle but keeps the cycle's ends") {
    Motion m = oneBar();
    m.bend = Bend::EaseIn;
    m.bendAmount = 1;
    m.bendCycle = 4 * kBeat;
    std::vector<Hit> hits = play(m, fourBeats(), 8 * kBeat);
    REQUIRE(hits.size() == 8);
    CHECK(hits[0] == Hit{0, 60});
    CHECK(hits[1].time > 960); // easing in: slow at first, so the second note comes late
    CHECK(hits[4] == Hit{3840, 60});
    for (Bend b : {Bend::EaseIn, Bend::EaseOut, Bend::EaseInOut, Bend::Swing, Bend::Stairs}) {
        double last = 0;
        for (int i = 0; i <= 100; i++) {
            double w = warp(b, 1, i / 100.0);
            CHECK(w >= last - 1e-12);
            last = w;
        }
        CHECK(warp(b, 1, 0) == doctest::Approx(0));
        CHECK(warp(b, 1, 1) == doctest::Approx(1));
    }
}

TEST_CASE("the position for drawing follows the same path") {
    Motion m = oneBar();
    CHECK(positionAt(m, 960) == doctest::Approx(960));
    CHECK(positionAt(m, 4000) == doctest::Approx(160));
    m.direction = Direction::Reverse;
    CHECK(positionAt(m, 960) == doctest::Approx(2880));
    m.direction = Direction::PingPong;
    CHECK(positionAt(m, 4800) == doctest::Approx(2880));
    m.direction = Direction::Freeze;
    m.start = 500;
    CHECK(positionAt(m, 4800) == doctest::Approx(500));
}

TEST_CASE("notes repeating with their own length inside the region are read at their own place") {
    // A lane 3 steps long (720 ticks) with one hit at its start, inside a one-bar region: forward it plays at 0, 720,
    // 1440, 2160, 2880, 3600; the region wrap at 3840 starts the lane again.
    std::vector<N> lane = {{0, 120, 36}};
    std::vector<uint32_t> byEnd = orderByEnd(lane);
    Motion m = oneBar();
    std::vector<double> at;
    Sweep s[kMaxSweeps];
    int n = sweeps(m, 0, 4 * kBeat, s);
    for (int i = 0; i < n; i++)
        crossedRepeating(s[i], 0, 0, 720, lane, byEnd,
                         [&](const N&, double pos) { at.push_back(timeAt(m, s[i], pos)); });
    CHECK(at == std::vector<double>{0, 720, 1440, 2160, 2880, 3600});

    at.clear();
    m.direction = Direction::Reverse;
    n = sweeps(m, 0, 4 * kBeat, s);
    for (int i = 0; i < n; i++)
        crossedRepeating(s[i], 0, 0, 720, lane, byEnd,
                         [&](const N&, double pos) { at.push_back(timeAt(m, s[i], pos)); });
    // Backward from the region's end (3840): the hits end at 3720, 3000, 2280, 1560, 840, 120.
    CHECK(at == std::vector<double>{120, 840, 1560, 2280, 3000, 3720});
}

// ---- The Playheads device in a song ---------------------------------------------------------------------------

#include "../src/model/DeviceCommands.hpp"
#include "../src/model/Devices.hpp"
#include "../src/model/DeviceTypes.hpp"
#include "../src/model/Json.hpp"
#include "../src/model/Serialize.hpp"

namespace {

NextId idsOf(Song& song) {
    return [&song] { return song.nextId++; };
}

/// Applies a command, checks that undo gives back the song exactly, then applies it again.
void applyUndoable(Song& song, CommandPtr command) {
    REQUIRE(command != nullptr);
    Song before = song;
    command->apply(song);
    Song after = song;
    command->revert(song);
    CHECK(song == before);
    command->apply(song);
    CHECK(song == after);
}

Song songWithTrack() {
    Song song;
    normalize(song);
    return song;
}

Device& firstDevice(Song& song) { return song.tracks[0].devices.front(); }

Song savedAndLoaded(const Song& song) {
    json::Ref j(songToJson(song));
    char* text = json_dumps(j.get(), JSON_COMPACT);
    REQUIRE(text != nullptr);
    json::Ref loaded(json_loads(text, 0, nullptr));
    free(text);
    return songFromJson(loaded.get(), Song::kSchemaVersion);
}

} // namespace

TEST_CASE("a new Playheads device comes first with two playheads, a beat apart, on channels 1 and 2") {
    Song song = songWithTrack();
    TrackId track = song.tracks[0].id;
    applyUndoable(song, addDevice(song, track, DeviceType::Echo, -1, idsOf(song)));
    applyUndoable(song, addDevice(song, track, DeviceType::Playheads, -1, idsOf(song)));
    const Device& d = firstDevice(song);
    REQUIRE(d.type == DeviceType::Playheads);
    REQUIRE(d.playheads.size() == 2);
    CHECK(d.playheads[0].channel == 1);
    CHECK(d.playheads[1].channel == 2);
    const devices::ParamInfo& start = devices::playheadInfo().params[playheads::knob::kStart];
    CHECK(devices::display(start, d.playheads[0].params[playheads::knob::kStart]) == 0);
    CHECK(devices::display(start, d.playheads[1].params[playheads::knob::kStart]) == 4);
    CHECK(d.playheads[0].id != d.playheads[1].id);

    SUBCASE("a track takes only one") {
        CHECK(addDevice(song, track, DeviceType::Playheads, -1, idsOf(song)) == nullptr);
    }
    SUBCASE("other devices never go before it") {
        applyUndoable(song, addDevice(song, track, DeviceType::Chord, 0, idsOf(song)));
        CHECK(song.tracks[0].devices[0].type == DeviceType::Playheads);
        CHECK(song.tracks[0].devices[1].type == DeviceType::Chord);
        DeviceId echo = song.tracks[0].devices[2].id;
        applyUndoable(song, moveDevice(song, echo, track, 0));
        CHECK(song.tracks[0].devices[0].type == DeviceType::Playheads);
        CHECK(song.tracks[0].devices[1].id == echo);
    }
}

TEST_CASE("playheads are added, duplicated, changed and removed with undo") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    DeviceId device = firstDevice(song).id;

    PlayheadId third;
    applyUndoable(song, addPlayhead(song, device, idsOf(song), &third));
    REQUIRE(firstDevice(song).playheads.size() == 3);
    CHECK(firstDevice(song).playheads[2].id == third);
    CHECK(firstDevice(song).playheads[2].channel == 3);

    ParamRef rate{ParamRef::Kind::Playhead, third.value, uint16_t(playheads::knob::kRate)};
    applyUndoable(song, setParam(song, rate, 0.25f));
    CHECK(firstDevice(song).playheads[2].params[playheads::knob::kRate] == 0.25f);

    PlayheadId copy;
    applyUndoable(song, duplicatePlayhead(song, third, idsOf(song), &copy));
    REQUIRE(firstDevice(song).playheads.size() == 4);
    CHECK(firstDevice(song).playheads[3].id == copy);
    CHECK(firstDevice(song).playheads[3].params == firstDevice(song).playheads[2].params);

    applyUndoable(song, setPlayheadChannel(song, copy, 9));
    applyUndoable(song, setPlayheadMute(song, copy, true));
    applyUndoable(song, renamePlayhead(song, copy, "Bells"));
    CHECK(firstDevice(song).playheads[3].channel == 9);
    CHECK(firstDevice(song).playheads[3].mute);
    CHECK(firstDevice(song).playheads[3].name == "Bells");
    CHECK(song.voicesOf(song.tracks[0]) == 3); // three playheads playing, one note at a time each

    applyUndoable(song, removePlayhead(song, third));
    CHECK(firstDevice(song).playheads.size() == 3);
}

TEST_CASE("playheads survive saving and loading, and a broken file is repaired") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    PlayheadId id = firstDevice(song).playheads[1].id;
    applyUndoable(song, setPlayheadChannel(song, id, 0));
    applyUndoable(song, setPlayheadSolo(song, id, true));
    applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, id.value, 1}, 1));
    CHECK(savedAndLoaded(song) == song);

    SUBCASE("a second Playheads device and one placed late are put right") {
        Song broken = song;
        Device extra = devices::makeDevice(DeviceType::Playheads, DeviceId(broken.nextId++));
        broken.tracks[0].devices.insert(broken.tracks[0].devices.begin(),
                                        devices::makeDevice(DeviceType::Echo, DeviceId(broken.nextId++)));
        broken.tracks[0].devices.push_back(extra);
        Song loaded = savedAndLoaded(broken);
        REQUIRE(loaded.tracks[0].devices.size() == 2);
        CHECK(loaded.tracks[0].devices[0].type == DeviceType::Playheads);
        CHECK(loaded.tracks[0].devices[0].playheads.size() == 2);
        CHECK(loaded.tracks[0].devices[1].type == DeviceType::Echo);
    }
}

TEST_CASE("a new playhead reads as 1x forward from the clip's start") {
    const devices::TypeInfo& info = devices::playheadInfo();
    Playhead p = devices::makePlayhead(PlayheadId(1), 0);
    CHECK(devices::format(info.params[playheads::knob::kRate], p.params[playheads::knob::kRate]) == "1x");
    CHECK(devices::format(info.params[playheads::knob::kDirection], p.params[playheads::knob::kDirection]) ==
          "Forward");
    playheads::Motion m = playheads::motionFrom(p.params.data(), nullptr, 0, 3840, TimeSig());
    CHECK(m.rate == 1);
    CHECK(m.regionLength == 3840);
    CHECK(m.repeatSize == 0);
}

// ---- MIDI export -------------------------------------------------------------------------------------------------

#include "../src/model/Modulators.hpp"
#include "../src/model/Render.hpp"

TEST_CASE("MIDI export with devices applied plays what the playheads read") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    for (auto [start, pitch] : {std::pair<Tick, int>{0, 60}, {960, 62}, {1920, 64}, {2880, 65}}) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = start;
        n.length = 480;
        n.pitch = int8_t(pitch);
        clip.notes.push_back(n);
    }
    song.clips.emplace(clip.id, clip);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = clip.id;
    a.position = 4 * kPpq;
    a.length = 4 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    PlayheadId second = firstDevice(song).playheads[1].id;
    applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, second.value, uint16_t(playheads::knob::kDirection)},
                                 devices::normalize(devices::playheadInfo().params[playheads::knob::kDirection], 1)));

    render::Options o;
    std::vector<std::pair<double, int>> got;
    for (const render::ExportNote& n : render::trackNotes(song, song.tracks[0].id, o, {}))
        got.push_back({n.start - 4 * kPpq, n.pitch * 100 + n.channel});
    // Playhead 1 forward from the start (channel 1); playhead 2 backward from beat 2 (channel 2): the note ending
    // at beat 2 (60, ends 480) is 480 ticks back, then 65 (ends 3360) after wrapping, and so on.
    std::vector<std::pair<double, int>> want = {{0, 6001},    {480, 6002},  {960, 6201},  {1920, 6401},
                                                {1440, 6502}, {2400, 6402}, {2880, 6501}, {3360, 6202}};
    std::sort(want.begin(), want.end());
    CHECK(got == want);

    o.devices = render::DeviceMode::Bypassed;
    CHECK(render::trackNotes(song, song.tracks[0].id, o, {}).size() == 4);
}

TEST_CASE("each playhead has a branch of its own devices, sharing the track's limit") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    PlayheadId second = firstDevice(song).playheads[1].id;
    applyUndoable(song, addBranchDevice(song, second, DeviceType::Transpose, -1, idsOf(song)));
    applyUndoable(song, addBranchDevice(song, second, DeviceType::Echo, 0, idsOf(song)));
    const std::vector<Device>& branch = firstDevice(song).playheads[1].branch;
    REQUIRE(branch.size() == 2);
    CHECK(branch[0].type == DeviceType::Echo);
    CHECK(branch[1].type == DeviceType::Transpose);
    CHECK(addBranchDevice(song, second, DeviceType::Playheads, -1, idsOf(song)) == nullptr);

    DeviceId transpose = branch[1].id;
    applyUndoable(song, setParam(song, {ParamRef::Kind::Device, transpose.value, 0}, 0.75f));
    applyUndoable(song, setDeviceOn(song, transpose, false));
    applyUndoable(song, duplicateDevice(song, transpose, idsOf(song)));
    CHECK(firstDevice(song).playheads[1].branch.size() == 3);
    applyUndoable(song, removeDevice(song, transpose));
    CHECK(firstDevice(song).playheads[1].branch.size() == 2);
    CHECK(deviceCount(song.tracks[0]) == 3);

    PlayheadId copy;
    applyUndoable(song, duplicatePlayhead(song, second, idsOf(song), &copy));
    const Playhead* dup = findPlayhead(song, copy);
    REQUIRE(dup != nullptr);
    CHECK(dup->branch.size() == 2);
    CHECK(dup->branch[0].id != firstDevice(song).playheads[1].branch[0].id);
    CHECK(savedAndLoaded(song) == song);
}

TEST_CASE("a drawn path takes the playhead where its points say, and repeats every cycle") {
    // Over a 2-beat cycle: up through the first half of the bar, then straight back down.
    std::vector<AutoPoint> path = {{0, 0, CurveType::Linear, 0}, {960, 0.5f, CurveType::Linear, 0},
                                   {1920, 0, CurveType::Linear, 0}};
    Motion m = oneBar();
    m.path = &path;
    m.pathCycle = 2 * kBeat;
    CHECK(positionAt(m, 480) == doctest::Approx(960));
    CHECK(positionAt(m, 960) == doctest::Approx(1920));
    CHECK(positionAt(m, 1440) == doctest::Approx(960));
    CHECK(positionAt(m, 1920 + 480) == doctest::Approx(960));
    // Up: 60 at 0, 62 (starts 960) at 480. Back down: 62 ends at 1440, reached at 1200; 60 ends 480, reached 1680.
    std::vector<Hit> hits = play(m, fourBeats(), 2 * kBeat);
    CHECK(hits == std::vector<Hit>{{0, 60}, {480, 62}, {1200, 62}, {1680, 60}});

    SUBCASE("drawing from the settings gives the same path") {
        Motion settings = oneBar();
        std::vector<AutoPoint> drawn = pathFromSettings(settings, 4 * kBeat, kBeat / 4);
        Motion d = oneBar();
        d.path = &drawn;
        d.pathCycle = 4 * kBeat;
        CHECK(play(d, fourBeats(), 4 * kBeat) == play(settings, fourBeats(), 4 * kBeat));
    }
}

TEST_CASE("drawing a straight path keeps only its corners") {
    Motion settings = oneBar();
    settings.start = 2 * kBeat;
    std::vector<AutoPoint> drawn = pathFromSettings(settings, 4 * kBeat, kBeat / 4);
    // From the middle up to the end, a hold and a jump back to the start, then up to the middle again.
    CHECK(drawn.size() <= 5);
    Motion d = oneBar();
    d.path = &drawn;
    d.pathCycle = 4 * kBeat;
    // The same notes, within a tick: a drawn path keeps its places as floats.
    std::vector<Hit> x = play(d, fourBeats(), 4 * kBeat), y = play(settings, fourBeats(), 4 * kBeat);
    REQUIRE(x.size() == y.size());
    for (size_t k = 0; k < x.size(); k++) {
        CHECK(x[k].pitch == y[k].pitch);
        CHECK(std::abs(x[k].time - y[k].time) <= 1.0);
    }
}

// ---- Note motion ---------------------------------------------------------------------------------------------------

TEST_CASE("notes join a Motion device whose knobs are set with undo, and it survives saving") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    for (int i = 0; i < 3; i++) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = i * kPpq;
        clip.notes.push_back(n);
    }
    song.clips.emplace(clip.id, clip);
    song.tracks[0].slots[0].clip = clip.id;
    std::vector<NoteId> two = {clip.notes[0].id, clip.notes[2].id};

    // A new Motion device goes before the Playheads device, named and coloured.
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    DeviceId group;
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Motion, -1, idsOf(song), &group));
    REQUIRE(song.tracks[0].devices.size() == 2);
    CHECK(song.tracks[0].devices[0].id == group);
    CHECK(song.tracks[0].devices[0].name == "Motion 1");
    CHECK(song.tracks[0].devices[1].type == DeviceType::Playheads);

    applyUndoable(song, setNotesMotion(song, clip.id, two, group));
    const Clip& c = song.clips.at(clip.id);
    CHECK(c.notes[0].motion == group);
    CHECK_FALSE(c.notes[1].motion.valid());

    ParamRef pitch{ParamRef::Kind::Device, group.value, 0};
    applyUndoable(song, setParam(song, pitch, 0.75f));
    CHECK(*findParam(song, pitch) == 0.75f);
    CHECK(trackOf(song, pitch) == song.tracks[0].id);
    CHECK(savedAndLoaded(song) == song);

    applyUndoable(song, setNotesMotion(song, clip.id, {clip.notes[0].id}, DeviceId()));
    CHECK_FALSE(song.clips.at(clip.id).notes[0].motion.valid());
    // Removing the device stops its notes moving.
    applyUndoable(song, removeDevice(song, group));
    Song loaded = savedAndLoaded(song);
    CHECK_FALSE(loaded.clips.at(clip.id).notes[2].motion.valid());
}

TEST_CASE("MIDI export plays the drift too") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    Note n;
    n.id = song.newId<NoteId>();
    n.length = 240;
    clip.notes.push_back(n);
    Device group = devices::makeDevice(DeviceType::Motion, song.newId<DeviceId>());
    group.params[chain::knob::kMotionInKey] = 0; // semitones
    song.tracks[0].devices.push_back(group);
    clip.notes[0].motion = group.id;
    song.clips.emplace(clip.id, clip);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = clip.id;
    a.length = 3 * 4 * kPpq; // three passes
    song.tracks[0].arrangement.push_back(a);
    Modulator steps = devices::makeModulator(ModulatorType::Steps, song.newId<ModulatorId>());
    steps.params[mods::knob::kStepCount] = devices::normalize(devices::modulatorInfo(ModulatorType::Steps).params[0], 2);
    steps.params[mods::knob::kStepPerPass] = 1;
    steps.steps = {0, 1};
    Modulation route;
    route.target = {ParamRef::Kind::Device, group.id.value, 0};
    route.amount = 1.f / 48;
    steps.routes.push_back(route);
    song.tracks[0].modulators.push_back(steps);

    std::vector<int> pitches;
    for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, render::Options(), {}))
        pitches.push_back(e.pitch);
    CHECK(pitches == std::vector<int>{60, 61, 60});
}

TEST_CASE("an order reads a segment's steps outside in, inside out, odd then even, or shuffled") {
    std::vector<int> outside, inside, oddEven;
    for (int k = 0; k < 4; k++) {
        outside.push_back(orderedStep(Order::OutsideIn, k, 4, 0, 1));
        inside.push_back(orderedStep(Order::InsideOut, k, 4, 0, 1));
        oddEven.push_back(orderedStep(Order::OddEven, k, 4, 0, 1));
    }
    CHECK(outside == std::vector<int>{0, 3, 1, 2});
    CHECK(inside == std::vector<int>{2, 1, 3, 0});
    CHECK(oddEven == std::vector<int>{0, 2, 1, 3});
    for (int pass = 0; pass < 5; pass++) {
        std::set<int> seen;
        for (int k = 0; k < 7; k++)
            seen.insert(orderedStep(Order::Shuffle, k, 7, pass, 9));
        CHECK(seen.size() == 7); // every step once
    }

    // Beats outside in: 1, 4, 2, 3.
    Motion m = oneBar();
    m.order = Order::OutsideIn;
    m.orderStep = kBeat;
    std::vector<Hit> hits = play(m, fourBeats(), 4 * kBeat);
    CHECK(hits == std::vector<Hit>{{0, 60}, {960, 65}, {1920, 62}, {2880, 64}});
    CHECK(positionAt(m, 1000) == doctest::Approx(2880 + 40));
}

TEST_CASE("the playhead settings fit their limit") {
    CHECK(devices::playheadInfo().params.size() <= size_t(playheads::kMaxPlayheadParams));
    CHECK(devices::motionInfo().params.size() <= size_t(kMaxDeviceParams));
}

// ---- Curve modulators ------------------------------------------------------------------------------------------

#include "../src/model/Modulators.hpp"

TEST_CASE("a synced Curve modulator plays its drawn shape every cycle, around no change") {
    Modulator m = devices::makeModulator(ModulatorType::Curve, ModulatorId(1));
    m.length = 2 * kPpq;
    m.points = {{0, 0.5f, CurveType::Linear, 0}, {kPpq, 1, CurveType::Hold, 0}, {2 * kPpq, 0, CurveType::Hold, 0}};
    mods::Drawn drawn{m.points.data(), int(m.points.size()), m.length};
    auto at = [&](double tick) {
        mods::State state;
        mods::Clock clock;
        clock.songTick = tick;
        return mods::evaluate(m.type, m.id.value, m.params.data(), state, clock, nullptr, 0, &drawn);
    };
    CHECK(mods::bipolar(m.type, m.params.data()));
    CHECK(at(0) == doctest::Approx(0));         // 0.5 drawn: no change
    CHECK(at(kPpq / 2) == doctest::Approx(0.5)); // halfway up the ramp to 1
    CHECK(at(kPpq) == doctest::Approx(1));
    CHECK(at(1.5 * kPpq) == doctest::Approx(1)); // held
    CHECK(at(2 * kPpq) == doctest::Approx(0));   // the next cycle starts over
}

TEST_CASE("Curve modulators are drawn and added with undo, and survive saving") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    PlayheadId id = firstDevice(song).playheads[0].id;
    ParamRef octave{ParamRef::Kind::Playhead, id.value, uint16_t(playheads::knob::kOctave)};
    ModulatorId added;
    applyUndoable(song, animateWithCurve(song, song.tracks[0].id, octave, "1 Octave", 4 * kPpq,
                                         {{0, 0.5f, CurveType::Hold, 0}}, idsOf(song), &added));
    REQUIRE(song.tracks[0].modulators.size() == 1);
    const Modulator& m = song.tracks[0].modulators[0];
    CHECK(m.id == added);
    CHECK(m.type == ModulatorType::Curve);
    REQUIRE(m.routes.size() == 1);
    CHECK(m.routes[0].target == octave);
    CHECK(m.routes[0].amount == 1);
    // Points come back sorted and inside the cycle.
    applyUndoable(song, setCurve(song, added, 2 * kPpq,
                                 {{3 * kPpq, 2, CurveType::Linear, 0}, {0, 0.25f, CurveType::Linear, 0}}));
    CHECK(song.tracks[0].modulators[0].points ==
          std::vector<AutoPoint>{{0, 0.25f, CurveType::Linear, 0}, {2 * kPpq, 1, CurveType::Linear, 0}});
    CHECK(savedAndLoaded(song) == song);
}

TEST_CASE("a playhead curve saved before curves were modulators loads as a Curve modulator") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    const Playhead& p = firstDevice(song).playheads[0];
    json::Ref j(songToJson(song));
    // The old form: [setting, length, points], the points setting the value itself (Octave 0 is 0.5, +1 is 0.625).
    json_t* track = json_array_get(json_object_get(j.get(), "tracks"), 0);
    json_t* head = json_array_get(json_object_get(json_array_get(json_object_get(track, "devices"), 0), "playheads"), 0);
    json_object_set_new(head, "curves", json_pack("[[iI[[Ifif],[Ifif]]]]", playheads::knob::kOctave,
                                                  static_cast<json_int_t>(8 * kPpq), json_int_t(0), 0.5, 1, 0.0,
                                                  static_cast<json_int_t>(4 * kPpq), 0.625, 1, 0.0));
    Song loaded = songFromJson(j.get(), Song::kSchemaVersion);
    REQUIRE(loaded.tracks[0].modulators.size() == 1);
    const Modulator& m = loaded.tracks[0].modulators[0];
    CHECK(m.type == ModulatorType::Curve);
    CHECK(m.name == p.name + " Octave");
    CHECK(m.length == 8 * kPpq);
    REQUIRE(m.points.size() == 2);
    // Base 0.5, turning all the way: 0.5 stays at the middle, 0.625 is a sixteenth above it.
    CHECK(m.points[0].value == doctest::Approx(0.5));
    CHECK(m.points[1].value == doctest::Approx(0.5625));
    REQUIRE(m.routes.size() == 1);
    CHECK(m.routes[0].target ==
          ParamRef{ParamRef::Kind::Playhead, p.id.value, uint16_t(playheads::knob::kOctave)});
    CHECK(m.routes[0].amount == 1);
}

TEST_CASE("MIDI export plays a synced Curve modulator on a playhead setting") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    Note n;
    n.id = song.newId<NoteId>();
    n.length = 480;
    n.pitch = 60;
    clip.notes.push_back(n);
    song.clips.emplace(clip.id, clip);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = clip.id;
    a.length = 12 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    applyUndoable(song, removePlayhead(song, firstDevice(song).playheads[1].id));
    PlayheadId id = firstDevice(song).playheads[0].id;
    // Bar 1 as set, bar 2 an octave up, repeating every two bars.
    applyUndoable(song, animateWithCurve(song, song.tracks[0].id,
                                         {ParamRef::Kind::Playhead, id.value, uint16_t(playheads::knob::kOctave)},
                                         "Octave", 8 * kPpq,
                                         {{0, 0.5f, CurveType::Hold, 0}, {4 * kPpq, 0.5625f, CurveType::Hold, 0}},
                                         idsOf(song)));
    std::vector<int> pitches;
    for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, render::Options(), {}))
        pitches.push_back(e.pitch);
    CHECK(pitches == std::vector<int>{60, 72, 60});
}

TEST_CASE("the piano roll learns which pitch a playhead plays each note at") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    // A chord on beat 1 (C E G), a single D on beat 3.
    for (auto [start, pitch] : {std::pair<Tick, int>{0, 60}, {0, 64}, {0, 67}, {1920, 62}}) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = start;
        n.length = 480;
        n.pitch = int8_t(pitch);
        clip.notes.push_back(n);
    }
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    PlayheadId id = firstDevice(song).playheads[0].id;
    auto set = [&](int k, float value) {
        applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, id.value, uint16_t(k)},
                                     devices::normalize(devices::playheadInfo().params[size_t(k)], value)));
    };
    auto heard = [&] {
        const Device& d = firstDevice(song);
        return render::heardPitches(song, song.tracks[0], d, d.playheads[0], clip);
    };
    CHECK(heard() == std::vector<int>{60, 64, 67, 62});
    set(playheads::knob::kOctave, -1);
    CHECK(heard() == std::vector<int>{48, 52, 55, 50});
    set(playheads::knob::kNotes, 1); // the lowest of each chord only
    CHECK(heard() == std::vector<int>{48, -1, -1, 50});
}

TEST_CASE("a clip's motion group saved before Motion devices loads as a Motion device after Playheads") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    Note n;
    n.id = song.newId<NoteId>();
    clip.notes.push_back(n);
    song.clips.emplace(clip.id, clip);
    song.tracks[0].slots[0].clip = clip.id;
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    json::Ref j(songToJson(song));
    // The old form: the clip holds the group, the note points at it, a route turns its Pitch as a "motion" knob.
    uint64_t group = song.nextId + 100;
    json_t* clipJson = json_array_get(json_object_get(j.get(), "clips"), 0);
    json_object_set_new(clipJson, "motions",
                        json_pack("[{s:I,s:s,s:i,s:[fffff]}]", "id", static_cast<json_int_t>(group), "name",
                                  "Motion A", "color", 3, "params", 0.75, 0.0, 0.0, 0.5, 1.0));
    Song plain = songFromJson(j.get(), Song::kSchemaVersion);
    CHECK(plain.tracks[0].devices.size() == 2);
    Device* motion = nullptr;
    for (Device& d : plain.tracks[0].devices)
        if (d.type == DeviceType::Motion)
            motion = &d;
    REQUIRE(motion != nullptr);
    CHECK(motion->id.value == group);
    CHECK(motion->name == "Motion A");
    CHECK(motion->color == 3);
    CHECK(motion->params[0] == doctest::Approx(0.75));
    // Where the group acted: on each note as a playhead played it.
    CHECK(plain.tracks[0].devices[0].type == DeviceType::Playheads);
    CHECK(plain.tracks[0].devices[1].type == DeviceType::Motion);
}

TEST_CASE("MIDI export reads the notes of a Motion device before the playheads where it moved them") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    Note n;
    n.id = song.newId<NoteId>();
    n.length = 240;
    n.pitch = 60;
    clip.notes.push_back(n);
    song.clips.emplace(clip.id, clip);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = clip.id;
    a.length = 4 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    applyUndoable(song, removePlayhead(song, firstDevice(song).playheads[1].id));
    PlayheadId head = firstDevice(song).playheads[0].id;
    applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, head.value, uint16_t(playheads::knob::kRate)},
                                 devices::normalize(devices::playheadInfo().params[playheads::knob::kRate], 10)));
    DeviceId motion;
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Motion, -1, idsOf(song), &motion));
    applyUndoable(song, setNotesMotion(song, clip.id, {n.id}, motion));
    applyUndoable(song, setParam(song, {ParamRef::Kind::Device, motion.value, uint16_t(chain::knob::kMotionTime)},
                                 devices::normalize(devices::motionInfo().params[chain::knob::kMotionTime], 4)));
    applyUndoable(song, setParam(song, {ParamRef::Kind::Device, motion.value, uint16_t(chain::knob::kMotionPitch)},
                                 devices::normalize(devices::motionInfo().params[chain::knob::kMotionPitch], 1)));
    std::vector<std::pair<Tick, int>> got;
    for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, render::Options(), {}))
        got.push_back({e.start, e.pitch});
    // On beat 2 of the clip (a 2x playhead reaches it after half a beat), a step up in the song's key.
    int up = resolveScale(song.scale, song.customScales).stepBy(60, 1);
    REQUIRE(!got.empty());
    CHECK(got[0] == std::pair<Tick, int>{kPpq / 2, up});
}
