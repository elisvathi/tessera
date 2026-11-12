#include "doctest.h"

#include "../src/model/Playheads.hpp"

#include <set>
#include <tuple>
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

#include "../src/model/AutomationEdits.hpp"
#include "../src/model/DeviceCommands.hpp"
#include "../src/model/DeviceTypes.hpp"
#include "../src/model/Devices.hpp"
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
    CHECK(devices::display(start, d.playheads[1].params[playheads::knob::kStart]) == 0);
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
    applyUndoable(song, setPlayheadOn(song, copy, false));
    applyUndoable(song, renamePlayhead(song, copy, "Bells"));
    CHECK(firstDevice(song).playheads[3].channel == 9);
    CHECK_FALSE(isOn(firstDevice(song).playheads[3]));
    CHECK(firstDevice(song).playheads[3].name == "Bells");
    CHECK(song.voicesOf(song.tracks[0]) == 4); // four playheads, one note at a time each (muted ones keep theirs)

    applyUndoable(song, removePlayhead(song, third));
    CHECK(firstDevice(song).playheads.size() == 3);
}

TEST_CASE("Auto voices make room for every echo and for a fast playhead's overlapping notes") {
    Song song = songWithTrack();
    TrackId track = song.tracks[0].id;
    // One note at a time in the clip (a fresh track's clips are empty: one voice).
    REQUIRE(song.voicesOf(song.tracks[0]) == 1);

    DeviceId echo;
    applyUndoable(song, addDevice(song, track, DeviceType::Echo, -1, idsOf(song), &echo));
    applyUndoable(song,
                  setParam(song, {ParamRef::Kind::Device, echo.value, uint16_t(chain::knob::kRepeats)},
                           devices::normalize(devices::deviceInfo(DeviceType::Echo).params[chain::knob::kRepeats], 2)));
    CHECK(song.voicesOf(song.tracks[0]) == 3); // the note and its two repeats can all sound together
    applyUndoable(song, removeDevice(song, echo));

    applyUndoable(song, addDevice(song, track, DeviceType::Playheads, -1, idsOf(song)));
    applyUndoable(song, removePlayhead(song, firstDevice(song).playheads[1].id));
    PlayheadId head = firstDevice(song).playheads[0].id;
    const auto& rate = devices::playheadInfo().params[playheads::knob::kRate];
    int twice = int(std::find(rate.choices.begin(), rate.choices.end(), "2x") - rate.choices.begin());
    REQUIRE(twice < int(rate.choices.size()));
    applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, head.value, uint16_t(playheads::knob::kRate)},
                                 devices::normalize(rate, float(twice))));
    CHECK(song.voicesOf(song.tracks[0]) == 1); // following the playhead, its notes shrink with it: no overlap
    const auto& length = devices::playheadInfo().params[playheads::knob::kLength];
    applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, head.value, uint16_t(playheads::knob::kLength)},
                                 devices::normalize(length, float(playheads::LengthMode::AsWritten))));
    CHECK(song.voicesOf(song.tracks[0]) == 2); // twice as fast, notes as long as written: two overlap
}

TEST_CASE("a playhead's own Octave, Steps and Velocity move into devices in its chain, curves and all") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    Playhead& p = firstDevice(song).playheads[0];
    const auto& info = devices::playheadInfo();
    p.params[playheads::knob::kOctave] = devices::normalize(info.params[playheads::knob::kOctave], -2);
    p.params[playheads::knob::kVelocity] = devices::normalize(info.params[playheads::knob::kVelocity], 20);
    Modulator curve = devices::makeModulator(ModulatorType::Curve, song.newId<ModulatorId>());
    Modulation route;
    route.target = {ParamRef::Kind::Playhead, p.id.value, uint16_t(playheads::knob::kOctave)};
    route.amount = 0.5f;
    curve.routes.push_back(route);
    song.tracks[0].modulators.push_back(curve);

    normalize(song);
    const Playhead& after = firstDevice(song).playheads[0];
    CHECK(devices::display(info.params[playheads::knob::kOctave], after.params[playheads::knob::kOctave]) == 0);
    CHECK(devices::display(info.params[playheads::knob::kVelocity], after.params[playheads::knob::kVelocity]) == 0);
    REQUIRE(after.branch.size() == 2);
    const Device& transpose = after.branch[0];
    CHECK(transpose.type == DeviceType::Transpose);
    CHECK(devices::display(devices::deviceInfo(DeviceType::Transpose).params[chain::knob::kOctaves],
                           transpose.params[chain::knob::kOctaves]) == -2);
    CHECK(after.branch[1].type == DeviceType::Velocity);
    CHECK(devices::display(devices::deviceInfo(DeviceType::Velocity).params[chain::knob::kMin],
                           after.branch[1].params[chain::knob::kMin]) == doctest::Approx(20));
    const Modulation& moved = song.tracks[0].modulators[0].routes.at(0);
    CHECK(moved.target == ParamRef{ParamRef::Kind::Device, transpose.id.value, uint16_t(chain::knob::kOctaves)});
    CHECK(moved.amount == 0.5f);

    normalize(song); // and again changes nothing
    CHECK(firstDevice(song).playheads[0].branch.size() == 2);
}

TEST_CASE("each playhead channel gets its own block of voices, in playhead order, muted ones too") {
    Song song = songWithTrack();
    TrackId track = song.tracks[0].id;
    applyUndoable(song, addDevice(song, track, DeviceType::Playheads, -1, idsOf(song)));
    PlayheadId third;
    applyUndoable(song, addPlayhead(song, firstDevice(song).id, idsOf(song), &third)); // channels 1, 2, 3
    // The first playhead echoes (2 repeats: three voices a note); the third is muted.
    PlayheadId first = firstDevice(song).playheads[0].id;
    applyUndoable(song, addBranchDevice(song, first, DeviceType::Echo, -1, idsOf(song)));
    DeviceId echo = firstDevice(song).playheads[0].branch.at(0).id;
    applyUndoable(song,
                  setParam(song, {ParamRef::Kind::Device, echo.value, uint16_t(chain::knob::kRepeats)},
                           devices::normalize(devices::deviceInfo(DeviceType::Echo).params[chain::knob::kRepeats], 2)));
    applyUndoable(song, setPlayheadOn(song, third, false));

    // The muted third keeps its block, so muting it moves no one else's voices.
    std::vector<Song::VoiceBlock> blocks = song.voiceBlocks(song.tracks[0]);
    REQUIRE(blocks.size() == 3);
    CHECK(blocks[0].part == 1);
    CHECK(blocks[0].size == 3);
    CHECK(blocks[1].part == 2);
    CHECK(blocks[1].size == 1);
    CHECK(blocks[2].part == 3);
    CHECK(blocks[2].size == 1);
    CHECK(song.voicesOf(song.tracks[0]) == 5);

    // A fixed voice count is per channel.
    applyUndoable(song, setTrackField(song, track, &Track::polyphony, 5, "Voices"));
    blocks = song.voiceBlocks(song.tracks[0]);
    REQUIRE(blocks.size() == 3);
    CHECK(blocks[0].size == 5);
    CHECK(blocks[1].size == 5);

    // More than a track holds (16 channels of 16): every channel keeps voices of its own, smaller.
    applyUndoable(song, setTrackField(song, track, &Track::polyphony, 16, "Voices"));
    for (int i = 0; i < 13; i++)
        applyUndoable(song, addPlayhead(song, firstDevice(song).id, idsOf(song)));
    applyUndoable(song, setPlayheadOn(song, third, true));
    blocks = song.voiceBlocks(song.tracks[0]);
    REQUIRE(blocks.size() == 16);
    int total = 0;
    for (const Song::VoiceBlock& b : blocks) {
        CHECK(b.size == 4);
        total += b.size;
    }
    CHECK(total == kMaxTrackVoices);
}

TEST_CASE("a playhead's notes last while it is on them: Length follows the playhead, also in older songs") {
    const devices::ParamInfo& length = devices::playheadInfo().params[playheads::knob::kLength];
    auto follows = [&](const Playhead& p) {
        return playheads::LengthMode(std::lround(devices::display(length, p.params[playheads::knob::kLength]))) ==
               playheads::LengthMode::FollowsPlayhead;
    };
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    CHECK(follows(firstDevice(song).playheads[0]));

    // A song saved before (schema 6) with "As written": it now follows the playhead too.
    applyUndoable(song, setParam(song,
                                 {ParamRef::Kind::Playhead, firstDevice(song).playheads[0].id.value,
                                  uint16_t(playheads::knob::kLength)},
                                 devices::normalize(length, float(playheads::LengthMode::AsWritten))));
    json::Ref j(songToJson(song));
    REQUIRE(migrateSong(j.get(), 6));
    Song loaded = songFromJson(j.get(), Song::kSchemaVersion);
    CHECK(follows(loaded.tracks[0].devices[0].playheads[0]));
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

TEST_CASE("a song saved before the longer choice lists keeps its rates, pieces, lanes and curves") {
    // Schema 7 saved Rate as 0..1 of 15 choices (1x was 7, 2x 10) and Piece of 6 (1/4 was 3).
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    Playhead& p = firstDevice(song).playheads[0];
    PlayheadId id = p.id;
    p.params[knob::kRate] = 10.f / 14;     // 2x
    p.params[knob::kRepeatSize] = 3.f / 5; // 1/4
    p.params[knob::kPathCycle] = 2.f / 4;  // 4 bars
    ParamRef rate{ParamRef::Kind::Playhead, id.value, uint16_t(knob::kRate)};
    // A lane on Rate: 1x, then 16x.
    AutomationEnvelope lane = makeKnobLane(AutomationId(song.nextId++), rate, "Rate", 7.f / 14, true, 0);
    lane.points = {{0, 7.f / 14, CurveType::Hold, 0}, {960, 1, CurveType::Hold, 0}};
    song.tracks[0].automation.push_back(lane);
    // A curve drawn on Rate: as set (2x), then two choices up (4x), then one down (3/2x).
    Modulator curve = devices::makeModulator(ModulatorType::Curve, ModulatorId(song.nextId++));
    curve.points = {{0, 0.5f, CurveType::Hold, 0},
                    {960, 0.5f + 1.f / 14, CurveType::Hold, 0},
                    {1920, 0.5f - 1.f / 28, CurveType::Hold, 0}};
    Modulation route;
    route.target = rate;
    route.amount = 1;
    curve.routes.push_back(route);
    song.tracks[0].modulators.push_back(curve);

    json::Ref j(songToJson(song));
    Song loaded = songFromJson(j.get(), 7);
    const Playhead& q = loaded.tracks[0].devices[0].playheads[0];
    const devices::TypeInfo& info = devices::playheadInfo();
    auto shown = [&](int k, float v) { return devices::format(info.params[size_t(k)], v); };
    CHECK(shown(knob::kRate, q.params[knob::kRate]) == "2x");
    CHECK(shown(knob::kRepeatSize, q.params[knob::kRepeatSize]) == "1/4");
    CHECK(shown(knob::kPathCycle, q.params[knob::kPathCycle]) == "4 bars");
    const AutomationEnvelope& l = loaded.tracks[0].automation.back();
    CHECK(shown(knob::kRate, l.points[0].value) == "1x");
    CHECK(shown(knob::kRate, l.points[1].value) == "16x");
    // The curve's points still reach 4x and 3/2x from the 2x as set (more choices lie between now).
    const Modulator& c = loaded.tracks[0].modulators.back();
    REQUIRE(c.points.size() == 3);
    auto reached = [&](float v) { return shown(knob::kRate, q.params[knob::kRate] + (v - 0.5f) * 2); };
    CHECK(reached(c.points[0].value) == "2x");
    CHECK(reached(c.points[1].value) == "4x");
    CHECK(reached(c.points[2].value) == "3/2x");
    // A song saved now loads as it is.
    json::Ref again(songToJson(loaded));
    CHECK(songFromJson(again.get(), Song::kSchemaVersion) == loaded);
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
    // Playhead 1 forward from the start (channel 1); playhead 2 backward from the clip's end (channel 2), meeting
    // each note at its end: 65 (ends 3360) after 480 ticks, then 64, 62 and 60.
    std::vector<std::pair<double, int>> want = {{0, 6001},    {480, 6502},  {960, 6201},  {1440, 6402},
                                                {1920, 6401}, {2400, 6202}, {2880, 6501}, {3360, 6002}};
    std::sort(want.begin(), want.end());
    CHECK(got == want);

    o.devices = render::DeviceMode::Bypassed;
    CHECK(render::trackNotes(song, song.tracks[0].id, o, {}).size() == 4);
}

TEST_CASE("MIDI export retriggers the notes a playhead jumps into, as the engine does") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    Note n;
    n.id = song.newId<NoteId>();
    n.length = 4 * kPpq; // one long note
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
    const auto& info = devices::playheadInfo();
    applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, head.value, uint16_t(playheads::knob::kRepeatSize)},
                                 devices::normalize(info.params[playheads::knob::kRepeatSize],
                                                    float(choiceIndex(repeatNames(), "1/16")))));
    applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, head.value, uint16_t(playheads::knob::kRepeatTimes)},
                                 devices::normalize(info.params[playheads::knob::kRepeatTimes], 4)));

    std::vector<std::pair<Tick, Tick>> beat2; // start and length of each note in beat 2
    for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, render::Options{}, {}))
        if (e.start >= kPpq && e.start < 2 * kPpq)
            beat2.push_back({Tick(e.start), Tick(e.end - e.start)});
    std::sort(beat2.begin(), beat2.end());
    // Every repeated sixteenth gets the same 240-tick window, including the move to the next sixteenth.
    CHECK(beat2 == std::vector<std::pair<Tick, Tick>>{{960, 240}, {1200, 240}, {1440, 240}, {1680, 240}});
}

TEST_CASE("zero-curve ratchets stay linear across playhead repeats") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    Note n;
    n.id = song.newId<NoteId>();
    n.length = 4 * kPpq;
    n.pitch = 60;
    n.ops.repeats = 2;
    n.ops.repeatCurve = 0;
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
    const auto& info = devices::playheadInfo();
    applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, head.value, uint16_t(playheads::knob::kRepeatSize)},
                                 devices::normalize(info.params[playheads::knob::kRepeatSize],
                                                    float(choiceIndex(repeatNames(), "1/16")))));
    applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, head.value, uint16_t(playheads::knob::kRepeatTimes)},
                                 devices::normalize(info.params[playheads::knob::kRepeatTimes], 4)));

    render::Options o;
    o.operators = render::OperatorMode::Rendered;
    std::vector<Tick> starts;
    for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, o, {}))
        if (e.start <= kPpq)
            starts.push_back(Tick(e.start));
    CHECK(starts == std::vector<Tick>{0, 120, 240, 360, 480, 600, 720, 840, 960});
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
    std::vector<AutoPoint> path = {
        {0, 0, CurveType::Linear, 0}, {960, 0.5f, CurveType::Linear, 0}, {1920, 0, CurveType::Linear, 0}};
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
    steps.params[mods::knob::kStepCount] =
        devices::normalize(devices::modulatorInfo(ModulatorType::Steps).params[0], 2);
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

TEST_CASE("a Curve plays its segment round and round from when it started, around no change") {
    Modulator m = devices::makeModulator(ModulatorType::Curve, ModulatorId(1));
    m.length = 4 * kPpq;
    m.points = {{0, 0.5f, CurveType::Linear, 0}, {kPpq, 1, CurveType::Hold, 0}, {2 * kPpq, 0, CurveType::Hold, 0}};
    m.segmentStart = 0;
    m.segmentEnd = 2 * kPpq;
    mods::Drawn drawn = mods::drawnOf(m);
    auto at = [&](double since, double song = 0) {
        mods::State state;
        mods::Clock clock;
        clock.songTick = song;
        clock.sinceClip = since;
        return mods::evaluate(m.type, m.id.value, m.params.data(), state, clock, nullptr, 0, &drawn);
    };
    CHECK(mods::bipolar(m.type, m.params.data()));
    CHECK(at(0) == doctest::Approx(0));          // 0.5 drawn: no change
    CHECK(at(kPpq / 2) == doctest::Approx(0.5)); // halfway up the ramp to 1
    CHECK(at(kPpq) == doctest::Approx(1));
    CHECK(at(1.5 * kPpq) == doctest::Approx(1)); // a step holds
    CHECK(at(2 * kPpq) == doctest::Approx(0));   // the segment starts over: the last two beats never play
    // Counted from the clip's start, whatever the song's position; without a clip, from the song's start.
    CHECK(at(kPpq, 7 * kPpq) == doctest::Approx(1));
    CHECK(at(NAN, 2 * kPpq + kPpq) == doctest::Approx(1));
    // A segment inside the drawing starts there.
    m.segmentStart = kPpq;
    m.segmentEnd = 3 * kPpq;
    drawn = mods::drawnOf(m);
    CHECK(mods::drawnPlace(drawn, 0) == doctest::Approx(kPpq));
    CHECK(mods::drawnPlace(drawn, 2.5 * kPpq) == doctest::Approx(1.5 * kPpq));
    CHECK(at(kPpq + 1) == doctest::Approx(-1)); // two beats into the drawing: the 0 step
}

TEST_CASE("Curve modulators are drawn and added with undo, and survive saving") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    PlayheadId id = firstDevice(song).playheads[0].id;
    ParamRef octave{ParamRef::Kind::Playhead, id.value, uint16_t(playheads::knob::kDirection)};
    ModulatorId added;
    applyUndoable(song,
                  animateWithCurve(song, song.tracks[0].id, octave, "1 Direction",
                                   {4 * kPpq, {{0, 0.5f, CurveType::Hold, 0}}, 0, 4 * kPpq}, idsOf(song), &added));
    REQUIRE(song.tracks[0].modulators.size() == 1);
    const Modulator& m = song.tracks[0].modulators[0];
    CHECK(m.id == added);
    CHECK(m.type == ModulatorType::Curve);
    REQUIRE(m.routes.size() == 1);
    CHECK(m.routes[0].target == octave);
    CHECK(m.routes[0].amount == 1);
    CHECK(m.segmentStart == 0);
    CHECK(m.segmentEnd == 4 * kPpq);
    // Points come back sorted with values 0..1; one past the end is kept, so a longer drawing brings it back. A
    // segment longer than the drawing is cut to fit it.
    applyUndoable(
        song,
        setCurve(song, added,
                 {2 * kPpq, {{3 * kPpq, 2, CurveType::Linear, 0}, {0, 0.25f, CurveType::Linear, 0}}, kPpq, 8 * kPpq}));
    const Modulator& drawn = song.tracks[0].modulators[0];
    CHECK(drawn.points ==
          std::vector<AutoPoint>{{0, 0.25f, CurveType::Linear, 0}, {3 * kPpq, 1, CurveType::Linear, 0}});
    CHECK(drawn.length == 2 * kPpq);
    CHECK(drawn.segmentStart == 0);
    CHECK(drawn.segmentEnd == 2 * kPpq);
    // A shorter drawing slides the segment back inside, as long as it was.
    applyUndoable(song, setCurve(song, added, {16 * kPpq, drawn.points, 12 * kPpq, 16 * kPpq}));
    applyUndoable(song, setCurve(song, added, {8 * kPpq, drawn.points, 12 * kPpq, 16 * kPpq}));
    CHECK(song.tracks[0].modulators[0].segmentStart == 4 * kPpq);
    CHECK(song.tracks[0].modulators[0].segmentEnd == 8 * kPpq);
    CHECK(savedAndLoaded(song) == song);
    // The route the curve is drawn in has no amount of its own.
    CHECK(setRouteAmount(song, added, 0, 0.5f) == nullptr);
}

TEST_CASE("a playhead curve saved before curves were modulators loads as a Curve modulator") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    const Playhead& p = firstDevice(song).playheads[0];
    json::Ref j(songToJson(song));
    // The old form: [setting, length, points], the points setting the value itself (Octave 0 is 0.5, +1 is 0.625).
    json_t* track = json_array_get(json_object_get(j.get(), "tracks"), 0);
    json_t* head =
        json_array_get(json_object_get(json_array_get(json_object_get(track, "devices"), 0), "playheads"), 0);
    json_object_set_new(head, "curves",
                        json_pack("[[iI[[Ifif],[Ifif]]]]", playheads::knob::kOctave, static_cast<json_int_t>(8 * kPpq),
                                  json_int_t(0), 0.5, 1, 0.0, static_cast<json_int_t>(4 * kPpq), 0.625, 1, 0.0));
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
    // Octave has since moved into a Transpose device in the playhead's chain; the curve went with it.
    const Playhead& after = loaded.tracks[0].devices[0].playheads[0];
    REQUIRE(!after.branch.empty());
    CHECK(m.routes[0].target ==
          ParamRef{ParamRef::Kind::Device, after.branch[0].id.value, uint16_t(chain::knob::kOctaves)});
    CHECK(m.routes[0].amount == 1);
}

TEST_CASE("a Curve saved before it had a segment loads drawn at full scale, playing as it did") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Transpose, -1, idsOf(song)));
    uint64_t transpose = firstDevice(song).id.value;
    Modulator curve = devices::makeModulator(ModulatorType::Curve, song.newId<ModulatorId>());
    curve.length = 2 * kPpq;
    curve.points = {{0, 0.5f, CurveType::Hold, 0}, {kPpq, 1, CurveType::Hold, 0}};
    Modulation first, second;
    first.target = {ParamRef::Kind::Device, transpose, 0};
    first.amount = 0.25f;
    second.target = {ParamRef::Kind::Device, transpose, 1};
    second.amount = 0.125f;
    curve.routes = {first, second};
    song.tracks[0].modulators.push_back(curve);
    json::Ref j(songToJson(song));
    json_t* saved =
        json_array_get(json_object_get(json_array_get(json_object_get(j.get(), "tracks"), 0), "modulators"), 0);
    json_object_del(saved, "segment");
    Song loaded = songFromJson(j.get(), Song::kSchemaVersion);
    REQUIRE(loaded.tracks[0].modulators.size() == 1);
    const Modulator& m = loaded.tracks[0].modulators[0];
    CHECK(m.segmentStart == 0);
    CHECK(m.segmentEnd == 2 * kPpq);
    // 1 at a quarter of the way became 0.5 + 0.25 * 0.5: the same turn, with the first route turning all the way.
    REQUIRE(m.points.size() == 2);
    CHECK(m.points[1].value == doctest::Approx(0.625));
    CHECK(m.routes[0].amount == 1);
    CHECK(m.routes[1].amount == doctest::Approx(0.5));
}

TEST_CASE("a Curve saved counting passes loads as Steps stepping per pass, with the same values") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Transpose, -1, idsOf(song)));
    uint64_t transpose = firstDevice(song).id.value;
    Modulator curve = devices::makeModulator(ModulatorType::Curve, song.newId<ModulatorId>());
    curve.params[mods::knob::kCurvePerPass] = 1;
    curve.length = 3 * mods::kPassTicks;
    curve.points = {{0, 0.5f, CurveType::Hold, 0},
                    {mods::kPassTicks, 0.6f, CurveType::Hold, 0},
                    {2 * mods::kPassTicks, 0.7f, CurveType::Hold, 0}};
    Modulation route;
    route.target = {ParamRef::Kind::Device, transpose, 0};
    route.amount = 0.5f;
    curve.routes = {route};
    song.tracks[0].modulators.push_back(curve);
    json::Ref j(songToJson(song));
    json_t* saved =
        json_array_get(json_object_get(json_array_get(json_object_get(j.get(), "tracks"), 0), "modulators"), 0);
    json_object_del(saved, "segment");
    Song loaded = songFromJson(j.get(), Song::kSchemaVersion);
    REQUIRE(loaded.tracks[0].modulators.size() == 1);
    const Modulator& m = loaded.tracks[0].modulators[0];
    CHECK(m.type == ModulatorType::Steps);
    CHECK(m.id == curve.id);
    CHECK(m.steps == std::vector<float>{0.5f, 0.6f, 0.7f});
    CHECK(mods::perPass(m.type, m.params.data()));
    CHECK(mods::bipolar(m.type, m.params.data()));
    REQUIRE(m.routes.size() == 1);
    CHECK(m.routes[0].amount == 0.5f);
    for (int64_t pass = 0; pass < 3; pass++)
        CHECK(mods::passValue(m.type, m.id.value, m.params.data(), pass, TimeSig(), m.steps.data(),
                              int(m.steps.size())) == doctest::Approx(m.steps[size_t(pass)] * 2 - 1));
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
    applyUndoable(song,
                  animateWithCurve(
                      song, song.tracks[0].id, {ParamRef::Kind::Playhead, id.value, uint16_t(playheads::knob::kOctave)},
                      "Octave",
                      {8 * kPpq, {{0, 0.5f, CurveType::Hold, 0}, {4 * kPpq, 0.5625f, CurveType::Hold, 0}}, 0, 8 * kPpq},
                      idsOf(song)));
    std::vector<int> pitches;
    for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, render::Options(), {}))
        pitches.push_back(e.pitch);
    CHECK(pitches == std::vector<int>{60, 72, 60});
}

TEST_CASE("MIDI export plays a playhead's On as the engine does, cutting its notes where it turns off") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    // A long note from the start and a short one halfway through beat 2.
    for (auto [start, length, pitch] : {std::tuple{0, 3800, 60}, std::tuple{1440, 100, 64}}) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = start;
        n.length = length;
        n.pitch = int8_t(pitch);
        clip.notes.push_back(n);
    }
    song.clips.emplace(clip.id, clip);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = clip.id;
    a.length = 4 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    applyUndoable(song, removePlayhead(song, firstDevice(song).playheads[1].id));
    PlayheadId id = firstDevice(song).playheads[0].id;
    // On as set for beat 1, then off: a whole row down.
    applyUndoable(
        song, animateWithCurve(
                  song, song.tracks[0].id, {ParamRef::Kind::Playhead, id.value, uint16_t(playheads::knob::kOn)}, "On",
                  {4 * kPpq, {{0, 0.5f, CurveType::Hold, 0}, {960, 0, CurveType::Hold, 0}}, 0, 4 * kPpq}, idsOf(song)));
    std::vector<render::ExportNote> notes = render::trackNotes(song, song.tracks[0].id, render::Options(), {});
    REQUIRE(notes.size() == 1); // the note in beat 2 falls where it is off
    CHECK(notes[0].pitch == 60);
    CHECK(notes[0].start == 0);
    CHECK(notes[0].end == 960);
}

TEST_CASE("a playhead's On is a setting: undoable, saved, and a muted playhead from before loads off") {
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    PlayheadId id = firstDevice(song).playheads[1].id;
    CHECK(isOn(firstDevice(song).playheads[1]));
    applyUndoable(song, setPlayheadOn(song, id, false));
    CHECK_FALSE(isOn(firstDevice(song).playheads[1]));
    CHECK(setPlayheadOn(song, id, false) == nullptr); // already off: nothing to undo
    CHECK_FALSE(isOn(savedAndLoaded(song).tracks[0].devices[0].playheads[1]));

    // Saved when a playhead had a mute switch of its own (and no On in its settings).
    json::Ref j(songToJson(song));
    json_t* head = json_array_get(
        json_object_get(
            json_array_get(json_object_get(json_array_get(json_object_get(j.get(), "tracks"), 0), "devices"), 0),
            "playheads"),
        1);
    json_t* params = json_object_get(head, "params");
    json_array_remove(params, json_array_size(params) - 1);
    json_object_set_new(head, "mute", json_true());
    Song loaded = songFromJson(j.get(), Song::kSchemaVersion);
    CHECK_FALSE(isOn(loaded.tracks[0].devices[0].playheads[1]));
    CHECK(isOn(loaded.tracks[0].devices[0].playheads[0]));
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
                        json_pack("[{s:I,s:s,s:i,s:[fffff]}]", "id", static_cast<json_int_t>(group), "name", "Motion A",
                                  "color", 3, "params", 0.75, 0.0, 0.0, 0.5, 1.0));
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
                                 devices::normalize(devices::playheadInfo().params[playheads::knob::kRate],
                                                    float(choiceIndex(rateNames(), "2x")))));
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

TEST_CASE("a curve can turn a playhead's Repeat and Direction, as it does its Rate") {
    // A clip of four beats, C D E F, played for two bars on the timeline by one playhead.
    auto pitchesWith = [](int k, float bar2Value) {
        Song song = songWithTrack();
        Clip clip;
        clip.id = song.newId<ClipId>();
        clip.length = clip.loopEnd = 4 * kPpq;
        int pitch[] = {60, 62, 64, 65};
        for (int b = 0; b < 4; b++) {
            Note n;
            n.id = song.newId<NoteId>();
            n.start = Tick(b) * kPpq;
            n.length = 240;
            n.pitch = int8_t(pitch[b]);
            clip.notes.push_back(n);
        }
        song.clips.emplace(clip.id, clip);
        ArrangerClip a;
        a.id = song.newId<ArrangementId>();
        a.clip = clip.id;
        a.length = 8 * kPpq;
        song.tracks[0].arrangement.push_back(a);
        applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
        applyUndoable(song, removePlayhead(song, firstDevice(song).playheads[1].id));
        PlayheadId id = firstDevice(song).playheads[0].id;
        const devices::ParamInfo& info = devices::playheadInfo().params[size_t(k)];
        // A point's height adds to the setting: 0.5 leaves it as set.
        float up = 0.5f + devices::normalize(info, bar2Value) / 2;
        applyUndoable(song, animateWithCurve(song, song.tracks[0].id, {ParamRef::Kind::Playhead, id.value, uint16_t(k)},
                                             info.name,
                                             {8 * kPpq, {{0, 0.5f, CurveType::Hold, 0}, {4 * kPpq, up, CurveType::Hold, 0}},
                                              0, 8 * kPpq},
                                             idsOf(song)));
        std::vector<int> pitches;
        for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, render::Options(), {}))
            pitches.push_back(e.pitch);
        return pitches;
    };
    const std::vector<std::string>& sizes = playheads::repeatNames();
    float quarter = float(std::find(sizes.begin(), sizes.end(), "1/4") - sizes.begin());
    // Bar 1 as written; bar 2 plays each beat twice (Times is 2), from beat 3, where its travel is by then.
    CHECK(pitchesWith(playheads::knob::kRepeatSize, quarter) == std::vector<int>{60, 62, 64, 65, 64, 64, 65, 65});
    // Bar 2 backward.
    CHECK(pitchesWith(playheads::knob::kDirection, float(playheads::Direction::Reverse)) ==
          std::vector<int>{60, 62, 64, 65, 65, 64, 62, 60});
}

TEST_CASE("a Curve respaces a playhead's repeats inside each repeat group, which keeps its length") {
    // A note held over the whole clip, read with Repeat 1/16, Times 4: one repeat group per beat, four hits in it.
    auto hits = [](int curve) {
        Song song = songWithTrack();
        Clip clip;
        clip.id = song.newId<ClipId>();
        clip.length = clip.loopEnd = 4 * kPpq;
        Note n;
        n.id = song.newId<NoteId>();
        n.length = 4 * kPpq;
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
        PlayheadId id = firstDevice(song).playheads[0].id;
        const auto& info = devices::playheadInfo();
        auto set = [&](int k, float v) {
            applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, id.value, uint16_t(k)},
                                         devices::normalize(info.params[size_t(k)], v)));
        };
        const std::vector<std::string>& sizes = playheads::repeatNames();
        set(playheads::knob::kRepeatSize, float(std::find(sizes.begin(), sizes.end(), "1/16") - sizes.begin()));
        set(playheads::knob::kRepeatTimes, 4);
        if (curve > 0)
            set(playheads::knob::kCurve, float(curve));
        std::vector<std::pair<double, double>> out; // start, length in the first beat
        for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, render::Options(), {}))
            if (e.start < kPpq)
                out.push_back({double(e.start), double(e.end - e.start)});
        return out;
    };
    const std::vector<std::string>& curves = playheads::curveNames();
    int easeIn = int(std::find(curves.begin(), curves.end(), "Ease in") - curves.begin());
    REQUIRE(easeIn < int(curves.size()));

    std::vector<std::pair<double, double>> straight = hits(0);
    REQUIRE(straight.size() == 4);
    for (int k = 0; k < 4; k++)
        CHECK(straight[size_t(k)].first == doctest::Approx(240 * k).epsilon(0.005));

    // Ease in (t cubed): the k-th of four hits comes at the cube root of k/4 of the beat, so they speed up, and each
    // note lasts until the next hit (Retrigger, following the playhead).
    std::vector<std::pair<double, double>> eased = hits(easeIn);
    REQUIRE(eased.size() == 4);
    std::vector<double> at;
    for (int k = 0; k <= 4; k++)
        at.push_back(double(kPpq) * std::cbrt(k / 4.0));
    for (int k = 0; k < 4; k++) {
        CHECK(eased[size_t(k)].first == doctest::Approx(at[size_t(k)]).epsilon(0.005));
        CHECK(eased[size_t(k)].second == doctest::Approx(at[size_t(k) + 1] - at[size_t(k)]).epsilon(0.01));
    }
}

TEST_CASE("without Repeat, a Curve changes nothing") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    for (int b = 0; b < 4; b++) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = Tick(b) * kPpq;
        n.length = 240;
        n.pitch = 60;
        clip.notes.push_back(n);
    }
    song.clips.emplace(clip.id, clip);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = clip.id;
    a.length = 4 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    applyUndoable(song, removePlayhead(song, firstDevice(song).playheads[1].id));
    PlayheadId id = firstDevice(song).playheads[0].id;
    const auto& info = devices::playheadInfo().params[playheads::knob::kCurve];
    applyUndoable(song, setParam(song, {ParamRef::Kind::Playhead, id.value, uint16_t(playheads::knob::kCurve)},
                                 devices::normalize(info, 3)));
    std::vector<Tick> starts;
    for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, render::Options(), {}))
        starts.push_back(Tick(e.start));
    CHECK(starts == std::vector<Tick>{0, 960, 1920, 2880});
}

TEST_CASE("MIDI export keeps a playhead's place when a curve changes its Rate, as the engine does") {
    // C D E F a beat apart; a curve doubles Rate from beat 3. The playhead carries on from E at twice the speed:
    // E, F half a beat later, then round to C and D. It must not jump to where 2x from the start would be.
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    int pitches[] = {60, 62, 64, 65};
    for (int b = 0; b < 4; b++) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = Tick(b) * kPpq;
        n.length = 240;
        n.pitch = int8_t(pitches[b]);
        clip.notes.push_back(n);
    }
    song.clips.emplace(clip.id, clip);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = clip.id;
    a.length = 4 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    applyUndoable(song, removePlayhead(song, firstDevice(song).playheads[1].id));
    PlayheadId id = firstDevice(song).playheads[0].id;
    const devices::ParamInfo& rate = devices::playheadInfo().params[playheads::knob::kRate];
    const std::vector<std::string>& rates = playheads::rateNames();
    float twice = devices::normalize(rate, float(std::find(rates.begin(), rates.end(), "2x") - rates.begin()));
    float once = devices::normalize(rate, float(std::find(rates.begin(), rates.end(), "1x") - rates.begin()));
    // A point's height adds to the knob: 0.5 leaves it as set, 0.5 + d / 2 turns it by d.
    float up = 0.5f + (twice - once) / 2;
    applyUndoable(
        song,
        animateWithCurve(
            song, song.tracks[0].id, {ParamRef::Kind::Playhead, id.value, uint16_t(playheads::knob::kRate)}, "Rate",
            {8 * kPpq, {{0, 0.5f, CurveType::Hold, 0}, {2 * kPpq, up, CurveType::Hold, 0}}, 0, 8 * kPpq}, idsOf(song)));
    std::vector<std::pair<Tick, int>> got;
    for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, render::Options(), {}))
        got.push_back({e.start, e.pitch});
    CHECK(got == std::vector<std::pair<Tick, int>>{{0, 60}, {960, 62}, {1920, 64}, {2400, 65}, {2880, 60}, {3360, 62}});
}

TEST_CASE("a playhead without a segment of its own keeps it when a note is added past the clip's notes") {
    // A clip that doesn't loop, its loop range one bar, a note on each beat; a second bar on the timeline.
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = 2 * 4 * kPpq;
    clip.loopStart = 0;
    clip.loopEnd = 4 * kPpq;
    clip.loopMode = LoopMode::Off;
    for (int b = 0; b < 4; b++) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = Tick(b) * kPpq;
        n.length = 240;
        n.pitch = 60;
        clip.notes.push_back(n);
    }
    ClipId id = clip.id;
    song.clips.emplace(clip.id, clip);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = clip.id;
    a.length = 2 * 4 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    applyUndoable(song, removePlayhead(song, firstDevice(song).playheads[1].id));
    auto starts = [&] {
        std::vector<Tick> out;
        for (const render::ExportNote& e : render::trackNotes(song, song.tracks[0].id, render::Options(), {}))
            out.push_back(e.start);
        return out;
    };
    std::vector<Tick> before = starts();
    CHECK(before == std::vector<Tick>{0, 960, 1920, 2880, 3840, 4800, 5760, 6720});

    // A note drawn after the bar: the segment (the loop range) stays, so what plays doesn't move.
    Note late;
    late.id = song.newId<NoteId>();
    late.start = 4 * kPpq + 240;
    late.length = 240;
    late.pitch = 72;
    song.clips.at(id).notes.push_back(late);
    song.clips.at(id).sortNotes();
    CHECK(starts() == before);
}

// ---- Offset ----------------------------------------------------------------------------------------------------

TEST_CASE("Offset choices run from a bar early through 0 to a bar late, in note values") {
    const std::vector<std::string>& names = offsetNames();
    REQUIRE(names.size() % 2 == 1);
    size_t zero = names.size() / 2;
    CHECK(names[zero] == "0");
    CHECK(names.front() == "-1 bar");
    CHECK(names.back() == "+1 bar");
    TimeSig ts;
    CHECK(offsetTicks(int(zero), ts) == 0);
    CHECK(offsetTicks(choiceIndex(names, "+1/8"), ts) == doctest::Approx(480));
    CHECK(offsetTicks(choiceIndex(names, "-1/16 T"), ts) == doctest::Approx(-160));
    CHECK(offsetTicks(choiceIndex(names, "+5/16"), ts) == doctest::Approx(1200));
    for (size_t i = 1; i < names.size(); i++)
        CHECK(offsetTicks(int(i), ts) > offsetTicks(int(i) - 1, ts)); // in order, so a curve sweeps it evenly
    const devices::ParamInfo& info = devices::playheadInfo().params[knob::kTimeOffset];
    CHECK(info.name == std::string("Offset"));
    CHECK(int(std::lround(devices::display(info, devices::defaultNormalized(info)))) == int(zero));
}

TEST_CASE("a late Offset plays every note that much later; an early one skips what would come before the start") {
    Motion late = oneBar();
    late.offset = 480;
    CHECK(play(late, fourBeats(), 8 * kBeat) ==
          std::vector<Hit>{
              {480, 60}, {1440, 62}, {2400, 64}, {3360, 65}, {4320, 60}, {5280, 62}, {6240, 64}, {7200, 65}});
    CHECK(positionAt(late, 1000) == doctest::Approx(520));
    CHECK(positionAt(late, 100) == doctest::Approx(0)); // waiting at its start

    Motion early = oneBar();
    early.offset = -240;
    // The 60 on beat 1 would come before the clip started, so the first pass skips it; the loop brings it back.
    CHECK(play(early, fourBeats(), 2 * kBeat * 4) ==
          std::vector<Hit>{
              {720, 62}, {1680, 64}, {2640, 65}, {3600, 60}, {4560, 62}, {5520, 64}, {6480, 65}, {7440, 60}});
}

TEST_CASE("MIDI export plays a playhead's Offset as the engine does: set, or moved by a lane, cut at the clip's end") {
    Song song = songWithTrack();
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    for (auto [start, pitch] : {std::pair<Tick, int>{0, 60}, {960, 62}, {3600, 64}}) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = start;
        n.length = 120;
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
    applyUndoable(song, removePlayhead(song, firstDevice(song).playheads[1].id));
    PlayheadId id = firstDevice(song).playheads[0].id;
    ParamRef offset{ParamRef::Kind::Playhead, id.value, uint16_t(knob::kTimeOffset)};
    const devices::ParamInfo& info = devices::playheadInfo().params[knob::kTimeOffset];
    float late = devices::normalize(info, float(choiceIndex(offsetNames(), "+1/8")));
    SUBCASE("set") { applyUndoable(song, setParam(song, offset, late)); }
    SUBCASE("by a lane") {
        AutomationEnvelope lane = makeKnobLane(song.newId<AutomationId>(), offset, "Playhead · Offset",
                                               devices::defaultNormalized(info), true, 0);
        lane.points = {{0, late, CurveType::Hold, 0}};
        song.tracks[0].automation.push_back(lane);
    }
    std::vector<std::pair<double, int>> got;
    for (const render::ExportNote& n : render::trackNotes(song, song.tracks[0].id, render::Options(), {}))
        got.push_back({n.start - 4 * kPpq, n.pitch});
    // An 1/8 late; the 64 would sound past the clip's end, so it doesn't.
    CHECK(got == std::vector<std::pair<double, int>>{{480, 60}, {1440, 62}});
}

TEST_CASE(
    "a song saved before the longer Arpeggiator list keeps its pattern, its lane and one in a playhead's branch") {
    // Schema 8 saved Pattern as 0..1 of 7 choices: Up, Down, Up-down, Down-up, Random, As played, Chord.
    Song song = songWithTrack();
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Arpeggiator, -1, idsOf(song)));
    Device& arp = song.tracks[0].devices.back();
    arp.params[chain::knob::kPattern] = 4.f / 6; // Random
    ParamRef pattern{ParamRef::Kind::Device, arp.id.value, uint16_t(chain::knob::kPattern)};
    AutomationEnvelope lane = makeKnobLane(AutomationId(song.nextId++), pattern, "Pattern", 0, true, 0);
    lane.points = {{0, 3.f / 6, CurveType::Hold, 0}, {960, 1, CurveType::Hold, 0}}; // Down-up, then Chord
    song.tracks[0].automation.push_back(lane);
    applyUndoable(song, addDevice(song, song.tracks[0].id, DeviceType::Playheads, -1, idsOf(song)));
    Device branchArp = devices::makeDevice(DeviceType::Arpeggiator, DeviceId(song.nextId++));
    branchArp.params[chain::knob::kPattern] = 5.f / 6; // As played
    song.tracks[0].devices.front().playheads[0].branch.push_back(branchArp);

    json::Ref j(songToJson(song));
    Song loaded = songFromJson(j.get(), 8);
    const devices::ParamInfo& info = devices::deviceInfo(DeviceType::Arpeggiator).params[chain::knob::kPattern];
    const Device* a = nullptr;
    for (const Device& d : loaded.tracks[0].devices)
        if (d.type == DeviceType::Arpeggiator)
            a = &d;
    REQUIRE(a != nullptr);
    CHECK(devices::format(info, a->params[chain::knob::kPattern]) == "Random");
    const AutomationEnvelope& l = loaded.tracks[0].automation.back();
    CHECK(devices::format(info, l.points[0].value) == "Down-up");
    CHECK(devices::format(info, l.points[1].value) == "Chord");
    const Device& b = loaded.tracks[0].devices.front().playheads[0].branch.front();
    CHECK(devices::format(info, b.params[chain::knob::kPattern]) == "As played");
    json::Ref again(songToJson(loaded));
    CHECK(songFromJson(again.get(), Song::kSchemaVersion) == loaded);
}

TEST_CASE("a playhead reads the note on its segment's start every pass, and never the one on its end") {
    // The user's song: two half notes, G at 0 and E at 7680. Playhead 1 reads bars 1 and 2 (0 to 7680), playhead 2
    // half of bar 3 (7680 to 9600), both at 2x. The engine adds the playhead's own time up a control block at a
    // time, and that sum rounds: it could stop a hair short of a wrap, so a sweep ended a hair past the segment
    // (reading the note there) and the next began a hair after its start (missing the note there).
    std::vector<N> notes = {{0, 1920, 55}, {7680, 9600, 52}};
    std::vector<uint32_t> byEnd = orderByEnd(notes);
    // 32 samples at 44.1 kHz and 120 BPM, at 2x: the windows Rack reads in.
    const double step = 2 * (120.0 / 60 * 960 / 44100 * 32);
    struct Case {
        double regionStart, regionLength;
        int pitch;      // the note on the segment's start: once every pass
        int strayPitch; // the note on the segment's end: never
    };
    for (Case c : {Case{0, 7680, 55, 52}, Case{7680, 1920, 52, -1}}) {
        Motion m;
        m.regionStart = c.regionStart;
        m.regionLength = c.regionLength;
        int hits = 0, strays = 0;
        double own = 0, until = 400 * c.regionLength;
        while (own < until) {
            double next = own + step;
            Sweep s[kMaxSweeps];
            int n = sweeps(m, own, next, s);
            for (int i = 0; i < n; i++)
                crossed(s[i], notes, byEnd, [&](const N& note, double) {
                    hits += note.pitch == c.pitch;
                    strays += note.pitch == c.strayPitch;
                });
            own = next;
        }
        CAPTURE(c.regionStart);
        CHECK(hits == int(std::ceil(own / c.regionLength))); // once each time it starts its segment again
        CHECK(strays == 0);
    }
    // The two windows caught in Rack, exactly.
    Motion m;
    m.regionLength = 7680;
    for (auto [a, b] :
         {std::pair<double, double>{15359.912925168997, 15362.69931972682}, {30718.519727898583, 30721.306122456408}}) {
        Sweep s[kMaxSweeps];
        int n = sweeps(m, a, b, s);
        int g = 0, e = 0;
        for (int i = 0; i < n; i++) {
            CHECK(s[i].to <= 7680);
            crossed(s[i], notes, byEnd, [&](const N& note, double) { (note.pitch == 55 ? g : e)++; });
        }
        CHECK(g == 1);
        CHECK(e == 0);
    }
}
