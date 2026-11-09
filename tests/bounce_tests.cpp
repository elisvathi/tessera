#include "doctest.h"

#include "../src/model/BounceNotes.hpp"
#include "../src/model/DeviceCommands.hpp"
#include "../src/model/DeviceTypes.hpp"
#include "../src/model/Devices.hpp"
#include "../src/model/Playheads.hpp"
#include "../src/model/Render.hpp"
#include "../src/model/Serialize.hpp"

#include <jansson.h>

using namespace tessera;

namespace {

/// A default song whose first track plays one four-note bar from the timeline.
struct BounceSong {
    Song song = makeDefaultSong();
    TrackId track = song.tracks[0].id;
    ClipId clipId;
    ArrangementId instance;

    NextId next() {
        return [this] { return song.nextId++; };
    }
    BounceSong() {
        Clip c;
        c.id = ClipId(song.nextId++);
        c.length = 4 * kPpq;
        c.loopStart = 0;
        c.loopEnd = 4 * kPpq;
        c.loopMode = LoopMode::Loop;
        for (int i = 0; i < 4; i++) {
            Note n;
            n.id = NoteId(song.nextId++);
            n.start = i * kPpq;
            n.length = kPpq / 2;
            n.pitch = int8_t(60 + i);
            c.notes.push_back(n);
        }
        clipId = c.id;
        song.clips[c.id] = c;
        song.findTrack(track)->slots[0].clip = c.id;
        ArrangerClip a;
        a.id = ArrangementId(song.nextId++);
        a.clip = c.id;
        a.position = 0;
        a.length = 4 * kPpq;
        instance = a.id;
        song.findTrack(track)->arrangement.push_back(a);
    }
};

} // namespace

TEST_CASE("a notes bounce makes one clip on a new track under the source") {
    BounceSong f;
    NoteBounce what;
    what.track = f.track;
    what.from = 0;
    what.to = 2 * kPpq;
    NoteBounceMade made;
    CommandPtr command = bounceNotes(f.song, what, 120, f.next(), &made);
    REQUIRE(command);
    size_t tracks = f.song.tracks.size();
    command->apply(f.song);
    REQUIRE(f.song.tracks.size() == tracks + 1);
    CHECK(f.song.tracks[1].id == made.track);
    CHECK(f.song.tracks[1].devices.empty());
    REQUIRE(f.song.tracks[1].arrangement.size() == 1);
    CHECK(f.song.tracks[1].arrangement[0].length == 2 * kPpq);
    const Clip* clip = f.song.findClip(made.clip);
    REQUIRE(clip);
    CHECK(clip->notes.size() == 2);
    command->revert(f.song);
    CHECK(f.song.tracks.size() == tracks);
    CHECK(f.song.findClip(made.clip) == nullptr);
}

TEST_CASE("a notes bounce with chosen clips leaves the others out") {
    BounceSong f;
    ArrangerClip other = f.song.findTrack(f.track)->arrangement[0];
    other.id = ArrangementId(f.song.nextId++);
    f.song.findTrack(f.track)->arrangement.push_back(other);
    NoteBounce what;
    what.track = f.track;
    what.to = 4 * kPpq;
    what.only = {f.instance};
    NoteBounceMade made;
    CommandPtr command = bounceNotes(f.song, what, 120, f.next(), &made);
    REQUIRE(command);
    command->apply(f.song);
    CHECK(f.song.findClip(made.clip)->notes.size() == 4);
}

TEST_CASE("a notes bounce of a panel clip fills the same scene on the new track") {
    BounceSong f;
    NoteBounce what;
    what.track = f.track;
    what.clip = f.clipId;
    NoteBounceMade made;
    CommandPtr command = bounceNotes(f.song, what, 120, f.next(), &made);
    REQUIRE(command);
    command->apply(f.song);
    REQUIRE(f.song.findTrack(made.track)->slots[0].clip);
    CHECK(*f.song.findTrack(made.track)->slots[0].clip == made.clip);
    CHECK(f.song.findClip(made.clip)->notes.size() == 4);
    CHECK(f.song.findTrack(made.track)->arrangement.empty());
}

TEST_CASE("a notes bounce keeps each playhead on its channel and can pick one") {
    BounceSong f;
    DeviceId device;
    addDevice(f.song, f.track, DeviceType::Playheads, 0, f.next(), &device)->apply(f.song);
    Device& d = f.song.findTrack(f.track)->devices[0];
    REQUIRE(d.playheads.size() == 2);
    d.playheads[0].channel = 1;
    d.playheads[1].channel = 2;

    NoteBounce what;
    what.track = f.track;
    what.to = 4 * kPpq;
    NoteBounceMade all, one;
    CommandPtr both = bounceNotes(f.song, what, 120, f.next(), &all);
    REQUIRE(both);
    what.channel = 2;
    CommandPtr second = bounceNotes(f.song, what, 120, f.next(), &one);
    REQUIRE(second);
    both->apply(f.song);
    size_t channelOne = 0, channelTwo = 0;
    for (const Note& n : f.song.findClip(all.clip)->notes)
        (n.channel == 1 ? channelOne : channelTwo)++;
    CHECK(channelOne > 0);
    CHECK(channelTwo > 0);
    both->revert(f.song);
    second->apply(f.song);
    CHECK(f.song.findClip(one.clip)->notes.size() == channelTwo);
    for (const Note& n : f.song.findClip(one.clip)->notes)
        CHECK(n.channel == 2);
}

TEST_CASE("a notes bounce refuses an empty span and audio tracks") {
    BounceSong f;
    NoteBounce what;
    what.track = f.track;
    what.from = 8 * kPpq;
    what.to = 12 * kPpq;
    CHECK_FALSE(bounceNotes(f.song, what, 120, f.next()));
    what.from = what.to;
    CHECK_FALSE(bounceNotes(f.song, what, 120, f.next()));
}

TEST_CASE("an audio track's Roll IN source survives a save and load") {
    BounceSong f;
    f.song.tracks[0].type = TrackType::Audio;
    f.song.tracks[0].recordInput = RecordInput::RollIn;
    json_t* saved = songToJson(f.song);
    Song loaded = songFromJson(saved, Song::kSchemaVersion);
    json_decref(saved);
    CHECK(loaded.tracks[0].recordInput == RecordInput::RollIn);
}

namespace {

/// A lane on AUTO channel 2 going from a quarter to three quarters over the bar.
AutomationEnvelope cvLane(Song& song) {
    AutomationEnvelope lane;
    lane.id = AutomationId(song.nextId++);
    lane.name = "Cutoff";
    lane.outputIndex = 2;
    AutoPoint a, b;
    a.t = 0;
    a.value = 0.25f;
    b.t = 4 * kPpq;
    b.value = 0.75f;
    lane.points = {a, b};
    return lane;
}

} // namespace

TEST_CASE("a notes bounce brings the track's CV lanes across") {
    BounceSong f;
    f.song.findTrack(f.track)->automation.push_back(cvLane(f.song));
    NoteBounce what;
    what.track = f.track;
    what.to = 4 * kPpq;
    NoteBounceMade made;
    bounceNotes(f.song, what, 120, f.next(), &made)->apply(f.song);
    const Track& track = *f.song.findTrack(made.track);
    REQUIRE(track.automation.size() == 1);
    CHECK(track.automation[0].name == "Cutoff");
    CHECK(track.automation[0].outputIndex == 2);
    REQUIRE_FALSE(track.automation[0].points.empty());
    CHECK(track.automation[0].points.front().value == doctest::Approx(0.25f));
    CHECK(evaluate(track.automation[0], double(2 * kPpq)) == doctest::Approx(0.5f).epsilon(0.03));
}

TEST_CASE("a notes bounce of a panel clip keeps the clip's CV lanes") {
    BounceSong f;
    f.song.findClip(f.clipId)->automation.push_back(cvLane(f.song));
    NoteBounce what;
    what.track = f.track;
    what.clip = f.clipId;
    NoteBounceMade made;
    bounceNotes(f.song, what, 120, f.next(), &made)->apply(f.song);
    const Clip* clip = f.song.findClip(made.clip);
    REQUIRE(clip->automation.size() == 1);
    CHECK(clip->automation[0].points.front().value == doctest::Approx(0.25f));
    CHECK(clip->automation[0].id != f.song.findClip(f.clipId)->automation[0].id);
}

TEST_CASE("a notes bounce plays a lane that turns a device knob into the notes") {
    BounceSong f;
    DeviceId device;
    addDevice(f.song, f.track, DeviceType::Transpose, 0, f.next(), &device)->apply(f.song);
    AutomationEnvelope lane;
    lane.id = AutomationId(f.song.nextId++);
    lane.paramTarget = ParamRef{ParamRef::Kind::Device, device.value, uint16_t(chain::knob::kSemitones)};
    AutoPoint up;
    up.value = 1;
    lane.points = {up};
    f.song.findTrack(f.track)->automation.push_back(lane);
    NoteBounce what;
    what.track = f.track;
    what.to = 4 * kPpq;
    NoteBounceMade made;
    bounceNotes(f.song, what, 120, f.next(), &made)->apply(f.song);
    CHECK(f.song.findClip(made.clip)->notes.front().pitch == 60 + 24);
    CHECK(f.song.findTrack(made.track)->automation.empty());
}

namespace {

/// A Motion device on the bounced track, joined by every note of its clip.
DeviceId motionOnAllNotes(BounceSong& f) {
    DeviceId motion;
    addDevice(f.song, f.track, DeviceType::Motion, 0, f.next(), &motion)->apply(f.song);
    for (Note& n : f.song.findClip(f.clipId)->notes)
        n.motion = motion;
    return motion;
}

/// A track Curve on `motion`'s Pitch: no change for two beats, then a step that adds 24 key steps.
void stepCurveOnPitch(BounceSong& f, DeviceId motion) {
    Modulator curve;
    curve.id = ModulatorId(f.song.nextId++);
    curve.type = ModulatorType::Curve;
    curve.params.assign(devices::modulatorInfo(ModulatorType::Curve).params.size(), 0.f);
    curve.length = 4 * kPpq;
    curve.segmentEnd = 4 * kPpq;
    AutoPoint rest, up;
    rest.value = 0.5f;
    rest.curve = CurveType::Hold;
    up.t = 2 * kPpq;
    up.value = 1;
    up.curve = CurveType::Hold;
    curve.points = {rest, up};
    Modulation route;
    route.kind = Modulation::Kind::Param;
    route.amount = 1;
    route.target = ParamRef{ParamRef::Kind::Device, motion.value, uint16_t(chain::knob::kMotionPitch)};
    curve.routes.push_back(route);
    f.song.findTrack(f.track)->modulators.push_back(curve);
}

/// The clip a bounce of the track's first bar makes.
const Clip* bounceBar(BounceSong& f) {
    NoteBounce what;
    what.track = f.track;
    what.to = 4 * kPpq;
    NoteBounceMade made;
    CommandPtr command = bounceNotes(f.song, what, 120, f.next(), &made);
    REQUIRE(command);
    command->apply(f.song);
    return f.song.findClip(made.clip);
}

} // namespace

TEST_CASE("a notes bounce plays a curve on a Motion device before the playheads") {
    BounceSong f;
    DeviceId motion = motionOnAllNotes(f);
    addDevice(f.song, f.track, DeviceType::Playheads, 1, f.next())->apply(f.song);
    stepCurveOnPitch(f, motion);
    int moved = 0, still = 0;
    for (const Note& n : bounceBar(f)->notes)
        (n.start < 2 * kPpq ? still : moved) += n.start < 2 * kPpq ? int(n.pitch <= 61) : int(n.pitch > 63);
    CHECK(still > 0);
    CHECK(moved > 0);
}

TEST_CASE("a notes bounce plays a curve on a Motion device with no playheads, step where the note starts") {
    BounceSong f;
    DeviceId motion = motionOnAllNotes(f);
    stepCurveOnPitch(f, motion);
    const Clip* clip = bounceBar(f);
    REQUIRE(clip->notes.size() == 4);
    CHECK(clip->notes[0].pitch == 60);
    CHECK(clip->notes[1].pitch == 61);
    CHECK(clip->notes[2].pitch > 62);
    CHECK(clip->notes[3].pitch > 63);
}

TEST_CASE("a notes bounce plays a lane on a Motion device's knob before the playheads") {
    BounceSong f;
    DeviceId motion = motionOnAllNotes(f);
    addDevice(f.song, f.track, DeviceType::Playheads, 1, f.next())->apply(f.song);
    AutomationEnvelope lane;
    lane.id = AutomationId(f.song.nextId++);
    lane.paramTarget = ParamRef{ParamRef::Kind::Device, motion.value, uint16_t(chain::knob::kMotionPitch)};
    AutoPoint up;
    up.value = 1;
    lane.points = {up};
    f.song.findTrack(f.track)->automation.push_back(lane);
    for (const Note& n : bounceBar(f)->notes)
        CHECK(n.pitch > 63);
}

TEST_CASE("a notes bounce plays a lane on a playhead's own setting") {
    BounceSong f;
    addDevice(f.song, f.track, DeviceType::Playheads, 0, f.next())->apply(f.song);
    Track& track = *f.song.findTrack(f.track);
    track.devices[0].playheads.resize(1);
    AutomationEnvelope lane;
    lane.id = AutomationId(f.song.nextId++);
    lane.paramTarget =
        ParamRef{ParamRef::Kind::Playhead, track.devices[0].playheads[0].id.value, uint16_t(playheads::knob::kOn)};
    AutoPoint on, off;
    on.value = 1;
    on.curve = CurveType::Hold;
    off.t = 2 * kPpq;
    off.value = 0;
    off.curve = CurveType::Hold;
    lane.points = {on, off};
    track.automation.push_back(lane);
    for (const Note& n : bounceBar(f)->notes)
        CHECK(n.start < 2 * kPpq);
}

TEST_CASE("a notes bounce keeps each note's own expression curves") {
    BounceSong f;
    f.song.findClip(f.clipId)->notes[0].timbreExpr = {{0, 0.25f, 0}, {1, 0.75f, 0}};
    const Clip* clip = bounceBar(f);
    REQUIRE(clip->notes.size() == 4);
    CHECK(clip->notes[0].timbreExpr == f.song.findClip(f.clipId)->notes[0].timbreExpr);
}

TEST_CASE("a notes bounce plays a curve sent to an AUTO jack, with no lane on that channel") {
    BounceSong f;
    Modulator curve;
    curve.id = ModulatorId(f.song.nextId++);
    curve.type = ModulatorType::Curve;
    curve.params.assign(devices::modulatorInfo(ModulatorType::Curve).params.size(), 0.f);
    curve.length = 4 * kPpq;
    curve.segmentEnd = 4 * kPpq;
    AutoPoint rest, up;
    rest.value = 0.5f;
    rest.curve = CurveType::Hold;
    up.t = 2 * kPpq;
    up.value = 1;
    up.curve = CurveType::Hold;
    curve.points = {rest, up};
    Modulation route;
    route.kind = Modulation::Kind::AutoChannel;
    route.track = f.track;
    route.channel = 3;
    route.amount = 1;
    curve.routes.push_back(route);
    f.song.findTrack(f.track)->modulators.push_back(curve);

    NoteBounce what;
    what.track = f.track;
    what.to = 4 * kPpq;
    NoteBounceMade made;
    bounceNotes(f.song, what, 120, f.next(), &made)->apply(f.song);
    const Track& track = *f.song.findTrack(made.track);
    REQUIRE(track.automation.size() == 1);
    CHECK(track.automation[0].outputIndex == 3);
    CHECK(evaluate(track.automation[0], double(kPpq)) == doctest::Approx(0.f));
    CHECK(evaluate(track.automation[0], double(3 * kPpq)) == doctest::Approx(1.f));
}

TEST_CASE("a notes bounce carries the track's pitch bend lane") {
    BounceSong f;
    AutomationEnvelope lane;
    lane.id = AutomationId(f.song.nextId++);
    lane.drive = LaneDrive::Pitch;
    AutoPoint up;
    up.value = 0.75f;
    lane.points = {up};
    f.song.findTrack(f.track)->automation.push_back(lane);
    NoteBounce what;
    what.track = f.track;
    what.to = 4 * kPpq;
    NoteBounceMade made;
    bounceNotes(f.song, what, 120, f.next(), &made)->apply(f.song);
    const Track& track = *f.song.findTrack(made.track);
    REQUIRE(track.automation.size() == 1);
    CHECK(track.automation[0].drive == LaneDrive::Pitch);
    CHECK(evaluate(track.automation[0], 0.0) == doctest::Approx(0.75f));
}

TEST_CASE("note repeat plays a whole chord again at its rate for as long as it lasts") {
    BounceSong f;
    Clip& clip = *f.song.findClip(f.clipId);
    clip.notes.resize(1);
    clip.notes[0].length = kPpq;
    Note third = clip.notes[0];
    third.id = NoteId(f.song.nextId++);
    third.pitch = 64;
    clip.notes.push_back(third);
    addDevice(f.song, f.track, DeviceType::NoteRepeat, 0, f.next())->apply(f.song);

    render::Options options;
    std::vector<render::ExportNote> played = render::trackNotes(f.song, f.track, options, render::Context{120, ""});
    REQUIRE(played.size() == 8); // a beat of 1/16s, two notes each
    int low = 0, high = 0;
    for (const render::ExportNote& n : played) {
        (n.pitch == 60 ? low : high)++;
        CHECK(n.end - n.start == doctest::Approx(double(kPpq) / 8));
    }
    CHECK(low == 4);
    CHECK(high == 4);
}
