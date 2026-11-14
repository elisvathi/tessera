#include "doctest.h"

#include "../src/model/DeviceCommands.hpp"
#include "../src/model/DeviceTypes.hpp"
#include "../src/model/Devices.hpp"
#include "../src/model/Json.hpp"
#include "../src/model/LauncherCommands.hpp"
#include "../src/model/Serialize.hpp"

using namespace tessera;

namespace {

NextId idsFrom(Song& song) {
    return [&song] { return song.nextId++; };
}

/// Applies a command, checks that undo gives back the song exactly, then applies it again.
void applyChecked(Song& song, CommandPtr command) {
    REQUIRE(command != nullptr);
    Song before = song;
    command->apply(song);
    Song after = song;
    command->revert(song);
    CHECK(song == before);
    command->apply(song);
    CHECK(song == after);
}

/// One track with an Arpeggiator and an Echo, an LFO on the Arpeggiator's Gate, and two clips in its slots ("Verse"
/// and "Fill"); Fill's first note moves with a Motion device of the track.
struct Rig {
    Song song;
    ClipId verse, fill;
    DeviceId arp, echo, motion;
    ModulatorId lfo;

    Rig() {
        normalize(song);
        Track& t = song.tracks[0];
        song.scenes.resize(2);
        t.slots.resize(2);
        for (int i = 0; i < 2; i++) {
            Clip c;
            c.id = song.newId<ClipId>();
            c.name = i ? "Fill" : "Verse";
            c.length = c.loopEnd = 4 * kPpq;
            Note n;
            n.id = song.newId<NoteId>();
            n.pitch = 60;
            n.length = kPpq;
            c.notes.push_back(n);
            song.clips[c.id] = c;
            t.slots[size_t(i)].clip = c.id;
            (i ? fill : verse) = c.id;
        }
        applyChecked(song, addDevice(song, t.id, DeviceType::Arpeggiator, -1, idsFrom(song), &arp));
        applyChecked(song, addDevice(song, song.tracks[0].id, DeviceType::Echo, -1, idsFrom(song), &echo));
        applyChecked(song, addDevice(song, song.tracks[0].id, DeviceType::Motion, -1, idsFrom(song), &motion));
        ModulatorPlace place{ModLevel::Track, song.tracks[0].id, DeviceId(), ClipId()};
        applyChecked(song, addModulator(song, place, ModulatorType::Lfo, idsFrom(song), &lfo));
        Modulation route;
        route.target = {ParamRef::Kind::Device, arp.value, uint16_t(chain::knob::kGate)};
        route.amount = 0.3f;
        applyChecked(song, addRoute(song, lfo, route));
        song.clips[fill].notes[0].motion = motion;
    }
    Clip& clip(ClipId id) { return song.clips[id]; }
};

Song throughJson(const Song& song) {
    json::Ref j(songToJson(song));
    return songFromJson(j.get(), Song::kSchemaVersion);
}

} // namespace

TEST_SUITE("clip chains") {
    TEST_CASE("a clip's own chain starts as a copy of its track's, with the modulators that move it") {
        Rig r;
        applyChecked(r.song, playOwnChain(r.song, r.fill, idsFrom(r.song)));
        Clip& fill = r.clip(r.fill);
        CHECK(fill.hasOwnChain);
        CHECK(fill.usesOwnChain());
        REQUIRE(fill.devices.size() == 3);
        CHECK(fill.devices[0].type == DeviceType::Motion);
        CHECK(fill.devices[1].type == DeviceType::Arpeggiator);
        CHECK(fill.devices[1].id != r.arp);
        CHECK(fill.devices[1].params == r.song.tracks[0].devices[1].params);
        // The LFO came along, moving the copy.
        REQUIRE(fill.modulators.size() == 1);
        CHECK(fill.modulators[0].id != r.lfo);
        REQUIRE(fill.modulators[0].routes.size() == 1);
        CHECK(fill.modulators[0].routes[0].target.owner == fill.devices[1].id.value);
        // Its note moves with its own Motion device now.
        CHECK(fill.notes[0].motion == fill.devices[0].id);
        // The track and the other clip are as they were.
        CHECK(r.song.tracks[0].devices.size() == 3);
        CHECK(!r.clip(r.verse).hasOwnChain);
        CHECK(r.song.tracks[0].modulators[0].routes[0].target.owner == r.arp.value);
    }

    TEST_CASE("switching back to the track keeps the clip's chain, and switching again uses it") {
        Rig r;
        applyChecked(r.song, playOwnChain(r.song, r.fill, idsFrom(r.song)));
        std::vector<Device> own = r.clip(r.fill).devices;
        applyChecked(r.song, playTrackChain(r.song, r.fill));
        CHECK(r.clip(r.fill).hasOwnChain);
        CHECK(!r.clip(r.fill).usesOwnChain());
        CHECK(r.clip(r.fill).devices == own);
        CHECK(playTrackChain(r.song, r.fill) == nullptr);
        applyChecked(r.song, playOwnChain(r.song, r.fill, idsFrom(r.song)));
        CHECK(r.clip(r.fill).devices == own);
        CHECK(playOwnChain(r.song, r.fill, idsFrom(r.song)) == nullptr);
        applyChecked(r.song, deleteOwnChain(r.song, r.fill));
        CHECK(!r.clip(r.fill).hasOwnChain);
        CHECK(r.clip(r.fill).devices.empty());
        CHECK(r.clip(r.fill).modulators.empty());
    }

    TEST_CASE("lookups find a clip's devices, playheads and modulators, and say whose chain they are in") {
        Rig r;
        applyChecked(r.song, playOwnChain(r.song, r.fill, idsFrom(r.song)));
        DeviceId heads;
        applyChecked(r.song, addDevice(r.song, ChainOwner(r.fill), DeviceType::Playheads, -1, idsFrom(r.song), &heads));
        const Clip& fill = r.clip(r.fill);
        CHECK(fill.devices[1].id == heads); // after the leading Motion device
        DeviceId arp = fill.devices[2].id;
        TrackId track;
        CHECK(findDevice(r.song, arp, &track) == &fill.devices[2]);
        CHECK(track == r.song.tracks[0].id);
        CHECK(ownerOf(r.song, arp) == ChainOwner(r.fill));
        CHECK(ownerOf(r.song, r.arp) == ChainOwner(r.song.tracks[0].id));
        PlayheadId ph = fill.devices[1].playheads[0].id;
        CHECK(findPlayhead(r.song, ph) != nullptr);
        CHECK(findParam(r.song, {ParamRef::Kind::Playhead, ph.value, 0}) != nullptr);
        ModulatorPlace place;
        CHECK(findModulator(r.song, fill.modulators[0].id, &place) != nullptr);
        CHECK(place.level == ModLevel::Clip);
        CHECK(place.clip == r.fill);
        int seen = 0;
        forEachModulator(r.song, [&](const Modulator& m, const ModulatorPlace&) { seen += m.id == fill.modulators[0].id; });
        CHECK(seen == 1);
        // A second Playheads device doesn't fit in the same chain; the track's chain is another chain.
        CHECK(addDevice(r.song, ChainOwner(r.fill), DeviceType::Playheads, -1, idsFrom(r.song)) == nullptr);
        CHECK(addDevice(r.song, r.song.tracks[0].id, DeviceType::Playheads, -1, idsFrom(r.song)) != nullptr);
    }

    TEST_CASE("what a clip's modulators and the track's may reach") {
        Rig r;
        applyChecked(r.song, playOwnChain(r.song, r.fill, idsFrom(r.song)));
        DeviceId own = r.clip(r.fill).devices[1].id;
        ModulatorPlace clipPlace{ModLevel::Clip, r.song.tracks[0].id, DeviceId(), r.fill};
        ModulatorId clipLfo;
        applyChecked(r.song, addModulator(r.song, clipPlace, ModulatorType::Lfo, idsFrom(r.song), &clipLfo));
        Modulation toOwn, toTrack;
        toOwn.target = {ParamRef::Kind::Device, own.value, uint16_t(chain::knob::kGate)};
        toTrack.target = {ParamRef::Kind::Device, r.arp.value, uint16_t(chain::knob::kGate)};
        CHECK(addRoute(r.song, clipLfo, toOwn) != nullptr);
        CHECK(addRoute(r.song, clipLfo, toTrack) == nullptr);
        CHECK(routeProblem(r.song, clipLfo, toTrack) == "A clip's modulator only reaches the clip's own devices");
        // The track's own LFO reaches the devices of its clips.
        CHECK(addRoute(r.song, r.lfo, toOwn) != nullptr);
        // Drawing a curve on a clip's device makes a modulator of the clip.
        ModulatorId curve;
        applyChecked(r.song, animateWithCurve(r.song, ChainOwner(r.fill), toOwn.target, "Fill Gate", CurveDrawing(),
                                              idsFrom(r.song), &curve));
        ModulatorPlace where;
        REQUIRE(findModulator(r.song, curve, &where));
        CHECK(where.level == ModLevel::Clip);
        // Deleting the chain takes the routes that pointed into it.
        applyChecked(r.song, addRoute(r.song, r.lfo, toOwn));
        applyChecked(r.song, deleteOwnChain(r.song, r.fill));
        CHECK(r.song.tracks[0].modulators[0].routes.size() == 1);
    }

    TEST_CASE("devices move and copy between a track's chain and a clip's") {
        Rig r;
        applyChecked(r.song, playOwnChain(r.song, r.verse, idsFrom(r.song)));
        size_t before = r.clip(r.verse).devices.size();
        applyChecked(r.song, moveDevice(r.song, r.echo, ChainOwner(r.verse), -1));
        CHECK(r.clip(r.verse).devices.size() == before + 1);
        CHECK(r.clip(r.verse).devices.back().id == r.echo);
        CHECK(ownerOf(r.song, r.echo) == ChainOwner(r.verse));
        applyChecked(r.song, copyDevice(r.song, r.echo, r.song.tracks[0].id, -1, idsFrom(r.song)));
        CHECK(r.song.tracks[0].devices.back().type == DeviceType::Echo);
        CHECK(r.song.tracks[0].devices.back().id != r.echo);
        applyChecked(r.song, duplicateDevice(r.song, r.echo, idsFrom(r.song)));
        CHECK(r.clip(r.verse).devices.size() == before + 2);
    }

    TEST_CASE("an audio clip has no chain of its own") {
        Rig r;
        r.clip(r.fill).audio = AudioContent();
        CHECK(playOwnChain(r.song, r.fill, idsFrom(r.song)) == nullptr);
        CHECK(addDevice(r.song, ChainOwner(r.fill), DeviceType::Echo, -1, idsFrom(r.song)) == nullptr);
        Clip& c = r.clip(r.fill);
        c.hasOwnChain = c.playsOwnChain = true;
        c.devices.push_back(devices::makeDevice(DeviceType::Echo, DeviceId(r.song.nextId++)));
        normalize(r.song);
        CHECK(!r.clip(r.fill).hasOwnChain);
        CHECK(r.clip(r.fill).devices.empty());
    }

    TEST_CASE("a copied clip gets a copy of its chain, its lanes and notes pointing at the copy") {
        Rig r;
        applyChecked(r.song, playOwnChain(r.song, r.fill, idsFrom(r.song)));
        Clip& fill = r.clip(r.fill);
        DeviceId own = fill.devices[1].id;
        AutomationEnvelope lane;
        lane.id = r.song.newId<AutomationId>();
        lane.paramTarget = {ParamRef::Kind::Device, own.value, uint16_t(chain::knob::kGate)};
        fill.automation.push_back(lane);
        Clip copy = copyClip(fill, idsFrom(r.song));
        REQUIRE(copy.devices.size() == fill.devices.size());
        CHECK(copy.devices[1].id != own);
        CHECK(copy.automation[0].paramTarget.owner == copy.devices[1].id.value);
        CHECK(copy.modulators[0].id != fill.modulators[0].id);
        CHECK(copy.modulators[0].routes[0].target.owner == copy.devices[1].id.value);
        CHECK(copy.notes[0].motion == copy.devices[0].id);
        CHECK(copy.usesOwnChain());
    }

    TEST_CASE("a clip's chain can become the track's") {
        Rig r;
        applyChecked(r.song, playOwnChain(r.song, r.fill, idsFrom(r.song)));
        applyChecked(r.song, removeDevice(r.song, r.clip(r.fill).devices[2].id)); // its Echo
        applyChecked(r.song, useOwnChainForTrack(r.song, r.fill, r.song.tracks[0].id, idsFrom(r.song)));
        const Track& t = r.song.tracks[0];
        REQUIRE(t.devices.size() == 2);
        CHECK(t.devices[1].type == DeviceType::Arpeggiator);
        CHECK(t.devices[1].id != r.clip(r.fill).devices[1].id);
        CHECK(!r.clip(r.fill).usesOwnChain());
        CHECK(r.clip(r.fill).hasOwnChain);
        // The track's old LFO lost its target; the clip's copy joined it and moves the new Arpeggiator.
        REQUIRE(t.modulators.size() == 2);
        CHECK(t.modulators[0].routes.empty());
        CHECK(t.modulators[1].routes[0].target.owner == t.devices[1].id.value);
    }

    TEST_CASE("a clip's chain is saved and loaded; older songs have none") {
        Rig r;
        CHECK(throughJson(r.song) == r.song);
        applyChecked(r.song, playOwnChain(r.song, r.fill, idsFrom(r.song)));
        Song loaded = throughJson(r.song);
        CHECK(loaded == r.song);
        applyChecked(r.song, playTrackChain(r.song, r.fill));
        CHECK(throughJson(r.song) == r.song);
    }

    TEST_CASE("a track's voices cover the channels of its clips' own Playheads") {
        Rig r;
        CHECK(r.song.voiceBlocks(r.song.tracks[0]).size() == 1);
        applyChecked(r.song, playOwnChain(r.song, r.fill, idsFrom(r.song)));
        applyChecked(r.song, addDevice(r.song, ChainOwner(r.fill), DeviceType::Playheads, -1, idsFrom(r.song)));
        std::vector<Song::VoiceBlock> blocks = r.song.voiceBlocks(r.song.tracks[0]);
        REQUIRE(blocks.size() == 3); // the track's own block, then the playheads' channels 1 and 2
        CHECK(blocks[0].part == 0);
        CHECK(blocks[1].part == 1);
        CHECK(blocks[2].part == 2);
        // Back on the track's chain, the clip's playheads need nothing.
        applyChecked(r.song, playTrackChain(r.song, r.fill));
        CHECK(r.song.voiceBlocks(r.song.tracks[0]).size() == 1);
    }
}
