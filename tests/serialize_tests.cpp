#include "doctest.h"

#include "../src/model/Devices.hpp"
#include "../src/model/Json.hpp"
#include "../src/model/Serialize.hpp"
#include "RandomSong.hpp"

#include <cstdlib>
#include <string>

using namespace tessera;

namespace {

/// Song -> JSON text -> Song, the same path a saved patch takes.
Song throughText(const Song& song) {
    json::Ref j(songToJson(song));
    char* text = json_dumps(j.get(), JSON_COMPACT);
    REQUIRE(text != nullptr);
    json_error_t error;
    json::Ref loaded(json_loads(text, 0, &error));
    free(text);
    REQUIRE(loaded.get() != nullptr);
    return songFromJson(loaded.get(), Song::kSchemaVersion);
}

json::Ref parse(const char* text) {
    json_error_t error;
    json::Ref j(json_loads(text, 0, &error));
    REQUIRE(j.get() != nullptr);
    return json::Ref(j.release());
}

} // namespace

TEST_CASE("random songs survive a save and load unchanged") {
    for (uint32_t seed = 1; seed <= 50; seed++) {
        CAPTURE(seed);
        Song original = testing::RandomSong(seed).make();
        Song loaded = throughText(original);
        REQUIRE(loaded == original);
    }
}

TEST_CASE("the default song survives a save and load unchanged") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    normalize(song);
    CHECK(throughText(song) == song);
}

TEST_CASE("normalize is idempotent and leaves a valid song alone") {
    for (uint32_t seed = 1; seed <= 20; seed++) {
        Song song = testing::RandomSong(seed).make();
        Song again = song;
        normalize(again);
        CHECK(again == song);
    }
}

TEST_CASE("notes are stored as compact arrays, with defaults left out") {
    Song song;
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = kPpq;
    Note plain;
    plain.id = song.newId<NoteId>();
    Note fancy = plain;
    fancy.id = song.newId<NoteId>();
    fancy.muted = true;
    fancy.colorTag = 3;
    fancy.ops.chance = 0.5f;
    clip.notes = {plain, fancy};

    json::Ref j(clipToJson(clip));
    const json_t* notes = json_object_get(j.get(), "notes");
    REQUIRE(json_array_size(notes) == 2);

    const json_t* first = json_array_get(notes, 0);
    CHECK(json_array_size(first) == 8); // no extras object
    CHECK(json::intAt(first, 6, -1) == 0);

    const json_t* second = json_array_get(notes, 1);
    REQUIRE(json_array_size(second) == 9);
    CHECK(json::intAt(second, 6, -1) == (1 | (3 << 8)));
    const json_t* ops = json_object_get(json_array_get(second, 8), "ops");
    CHECK(json_object_size(ops) == 1); // only the operator that differs from its default
    CHECK(json::getNumber(ops, "chance", 0) == doctest::Approx(0.5));
    CHECK(json_object_get(json_array_get(second, 8), "ch") == nullptr); // channel 1 is the default

    clip.notes[0].channel = 7;
    json::Ref k(clipToJson(clip));
    const json_t* withChannel = json_array_get(json_object_get(k.get(), "notes"), 0);
    CHECK(json::getInt(json_array_get(withChannel, 8), "ch", 0) == 7);
}

TEST_CASE("loading tolerates missing, wrong-typed and out-of-range fields") {
    json::Ref j = parse(R"({
        "timeSig": [7, 3],
        "swing": "lots",
        "loop": [960, 0],
        "nextId": 2,
        "clips": [
            {"id": 5, "length": 960, "notes": [[0, 0, 200, 2.0], [10], "junk", [100, 50, 60, 0.5, 0.5, 0, 0, 5]]},
            "not a clip"
        ],
        "tracks": [
            {"id": 9, "polyphony": 99, "slots": [5, null, 77], "arrangement": [{"clip": 404}]},
            {"id": 9, "name": "Duplicate id"}
        ],
        "scenes": [{}],
        "groups": [{"id": 3, "tracks": [9, 12345]}]
    })");
    Song s = songFromJson(j.get(), Song::kSchemaVersion);

    CHECK(s.timeSig.num == 7);
    CHECK(s.timeSig.den == 4); // 3 isn't a valid denominator
    CHECK(s.swing == 0);
    CHECK(s.loopEnd > s.loopStart);

    const Clip* clip = s.findClip(ClipId(5));
    REQUIRE(clip != nullptr);
    REQUIRE(clip->notes.size() == 2); // "[10]" and "junk" are dropped
    CHECK(clip->notes[0].length == 1);
    CHECK(clip->notes[0].pitch == 127);
    CHECK(clip->notes[0].velocity == 1.f);
    CHECK(clip->notes[0].id.valid());
    CHECK(clip->notes[1].id.value != 5); // a note can't share the clip's ID

    REQUIRE(s.tracks.size() == 2);
    CHECK(s.tracks[0].polyphony == kMaxVoices);
    CHECK(s.tracks[0].id != s.tracks[1].id);
    CHECK(s.scenes.size() == 3); // grown to the longest slot list
    CHECK(s.tracks[0].slots[0].clip == ClipId(5));
    CHECK_FALSE(s.tracks[0].slots[2].clip.has_value()); // clip 77 doesn't exist
    CHECK(s.tracks[1].slots.size() == 3);
    CHECK(s.tracks[0].arrangement.empty()); // instance of a missing clip
    CHECK(s.groups[0].tracks.size() == 1);

    // Fresh IDs never collide with loaded ones.
    CHECK(s.nextId > 9);
}

TEST_CASE("loading something that isn't a song gives a usable empty song") {
    json::Ref j(json_string("hello"));
    Song s = songFromJson(j.get(), Song::kSchemaVersion);
    CHECK(s.tracks.size() == 1);
    CHECK(s.scenes.size() == 1);
    CHECK(s.tracks[0].slots.size() == 1);
}

TEST_CASE("migration accepts current and older schemas, flags newer ones") {
    json::Ref j(json_object());
    CHECK(migrateSong(j.get(), 0));
    CHECK(migrateSong(j.get(), Song::kSchemaVersion));
    CHECK_FALSE(migrateSong(j.get(), Song::kSchemaVersion + 1));
}

TEST_CASE("schema 1 tracks saved with the old default of 8 voices become auto") {
    json::Ref j = parse(R"({"tracks": [{"id": 1, "polyphony": 8}, {"id": 2, "polyphony": 3}]})");
    Song s = songFromJson(j.get(), 1);
    CHECK(s.tracks[0].polyphony == kAutoVoices);
    CHECK(s.tracks[1].polyphony == 3);

    json::Ref current = parse(R"({"tracks": [{"id": 1, "polyphony": 8}]})");
    CHECK(songFromJson(current.get(), Song::kSchemaVersion).tracks[0].polyphony == 8);
}

TEST_CASE("songs saved before keys could be chosen have no key") {
    json::Ref j = parse(R"({"scale": {"root": 0, "scale": "major"}})");
    CHECK(songFromJson(j.get(), 2).scale == ScaleRef{});
    CHECK(ScaleRef{}.scale == "chromatic");
    CHECK(songFromJson(j.get(), Song::kSchemaVersion).scale == (ScaleRef{0, "major"}));
}

TEST_CASE("an old Random with velocity and timing loads as Random plus Humanize, with its links moved") {
    // Before 13 Oct 2026 Random had five knobs; velocity (3) and timing (4) became the Humanize device.
    Song song = makeDefaultSong();
    Device r;
    r.id = song.newId<DeviceId>();
    r.type = DeviceType::Random;
    r.name = "Random";
    r.params = {0.25f, 1.f, 0.f, 0.5f, 0.4f};
    song.tracks[0].devices.push_back(r);
    Modulator lfo;
    lfo.id = song.newId<ModulatorId>();
    Modulation route;
    route.target = ParamRef{ParamRef::Kind::Device, r.id.value, 4};
    lfo.routes.push_back(route);
    song.modulators.push_back(lfo);
    AutomationEnvelope lane;
    lane.id = song.newId<AutomationId>();
    lane.paramTarget = ParamRef{ParamRef::Kind::Device, r.id.value, 3};
    song.tracks[0].automation.push_back(lane);

    Song loaded = throughText(song);
    const std::vector<Device>& devices = loaded.tracks[0].devices;
    REQUIRE(devices.size() == 2);
    CHECK(devices[0].type == DeviceType::Random);
    CHECK(devices[0].params == std::vector<float>{0.25f, 1.f, 0.f});
    CHECK(devices[1].type == DeviceType::Humanize);
    CHECK(devices[1].params == std::vector<float>{0.4f, 0.5f, 0.f});
    ParamRef target = loaded.modulators[0].routes[0].target;
    CHECK(target.owner == devices[1].id.value);
    CHECK(target.param == chain::knob::kHumanizeTiming);
    ParamRef lanes = loaded.tracks[0].automation[0].paramTarget;
    CHECK(lanes.owner == devices[1].id.value);
    CHECK(lanes.param == chain::knob::kHumanizeVelocity);

    // A Random that never used them just loses the two unused knobs.
    song.tracks[0].devices[0].params = {0.25f, 1.f, 0.f, 0.f, 0.f};
    CHECK(throughText(song).tracks[0].devices.size() == 1);
}

TEST_CASE("what a track listens to is saved and loaded") {
    Song song = makeDefaultSong();
    REQUIRE(song.tracks.size() >= 2);
    song.tracks[1].listen = ListenSource::TrackBefore;
    song.tracks[1].listenTrack = song.tracks[0].id;
    song.tracks[0].listen = ListenSource::External;
    song.tracks[0].recordInput = RecordInput::Midi;
    Song loaded = throughText(song);
    CHECK(loaded == song);
    CHECK(loaded.tracks[1].listen == ListenSource::TrackBefore);
    CHECK(loaded.tracks[1].listenTrack == song.tracks[0].id);
    CHECK(loaded.tracks[0].listen == ListenSource::External);
    CHECK(loaded.tracks[2].listen == ListenSource::None); // left out of the file, and None on loading
}
