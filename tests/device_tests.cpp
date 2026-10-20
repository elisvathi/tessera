#include "doctest.h"

#include "../src/model/DeviceCommands.hpp"
#include "../src/model/DeviceTypes.hpp"
#include "../src/model/Json.hpp"
#include "../src/model/Serialize.hpp"
#include "RandomSong.hpp"

#include <cmath>
#include <set>

using namespace tessera;

namespace {

NextId idsFrom(Song& song) {
    return [&song] { return song.nextId++; };
}

/// Applies a command, checks that revert gives back the song exactly, then applies it again for the next step.
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

Device& deviceAt(Song& song, int track, int index) { return song.tracks[size_t(track)].devices[size_t(index)]; }

DeviceId addDeviceTo(Song& song, int track, DeviceType type) {
    applyChecked(song, addDevice(song, song.tracks[size_t(track)].id, type, -1, idsFrom(song)));
    return song.tracks[size_t(track)].devices.back().id;
}

ModulatorId addModulatorAt(Song& song, const ModulatorPlace& place, ModulatorType type = ModulatorType::Lfo) {
    ModulatorId id;
    applyChecked(song, addModulator(song, place, type, idsFrom(song), &id));
    return id;
}

Modulation toKnob(DeviceId device, int param, float amount = 0.5f) {
    Modulation m;
    m.target = {ParamRef::Kind::Device, device.value, uint16_t(param)};
    m.amount = amount;
    return m;
}

ModulatorPlace songLevel() { return {ModLevel::Song, TrackId(), DeviceId()}; }
ModulatorPlace trackLevel(const Song& song, int track) {
    return {ModLevel::Track, song.tracks[size_t(track)].id, DeviceId()};
}
ModulatorPlace deviceLevel(const Song& song, int track, DeviceId device) {
    return {ModLevel::Device, song.tracks[size_t(track)].id, device};
}

Song throughJson(const Song& song) {
    json::Ref j(songToJson(song));
    char* text = json_dumps(j.get(), JSON_COMPACT);
    REQUIRE(text != nullptr);
    json_error_t error;
    json::Ref loaded(json_loads(text, 0, &error));
    free(text);
    REQUIRE(loaded.get() != nullptr);
    return songFromJson(loaded.get(), Song::kSchemaVersion);
}

} // namespace

TEST_CASE("device and modulator tables are complete and consistent") {
    std::set<std::string> deviceNames;
    for (int t = 0; t < kDeviceTypeCount; t++) {
        const devices::TypeInfo& info = devices::deviceInfo(DeviceType(t));
        CAPTURE(t);
        REQUIRE(info.name != nullptr);
        CHECK(deviceNames.insert(info.name).second);
        CHECK(!info.params.empty());
        CHECK(info.params.size() <= size_t(kMaxDeviceParams));
    }
    for (int t = 0; t < kModulatorTypeCount; t++) {
        const devices::TypeInfo& info = devices::modulatorInfo(ModulatorType(t));
        CAPTURE(t);
        REQUIRE(info.name != nullptr);
        CHECK(info.params.size() <= size_t(kMaxDeviceParams));
    }

    auto checkParam = [](const devices::ParamInfo& p) {
        CAPTURE(p.name);
        CHECK(p.max > p.min);
        CHECK(p.def >= p.min);
        CHECK(p.def <= p.max);
        if (p.kind == devices::ParamInfo::Kind::Exponential)
            CHECK(p.min > 0);
        if (p.kind == devices::ParamInfo::Kind::Choice)
            CHECK(p.choices.size() >= 2);
        // The default survives normalize -> display.
        CHECK(devices::display(p, devices::defaultNormalized(p)) == doctest::Approx(p.def).epsilon(0.001));
        // Every value survives display -> normalize -> display.
        for (int i = 0; i <= 20; i++) {
            float n = float(i) / 20;
            float shown = devices::display(p, n);
            CHECK(devices::display(p, devices::normalize(p, shown)) == doctest::Approx(shown).epsilon(0.001));
            CHECK(!devices::format(p, n).empty());
        }
    };
    for (int t = 0; t < kDeviceTypeCount; t++)
        for (const devices::ParamInfo& p : devices::deviceInfo(DeviceType(t)).params)
            checkParam(p);
    for (int t = 0; t < kModulatorTypeCount; t++)
        for (const devices::ParamInfo& p : devices::modulatorInfo(ModulatorType(t)).params)
            checkParam(p);
}

TEST_CASE("knob values read as the user expects") {
    const devices::ParamInfo* semis = devices::paramInfo(DeviceType::Transpose, 0);
    REQUIRE(semis);
    CHECK(devices::format(*semis, devices::normalize(*semis, 3)) == "+3 st");
    CHECK(devices::format(*semis, devices::normalize(*semis, -12)) == "-12 st");
    CHECK(semis->bipolar());

    const devices::ParamInfo* rate = devices::paramInfo(DeviceType::Arpeggiator, 1);
    REQUIRE(rate);
    CHECK(devices::format(*rate, devices::defaultNormalized(*rate)) == "1/16");
    CHECK(!rate->bipolar());

    const devices::ParamInfo* hold = devices::paramInfo(DeviceType::Arpeggiator, 6);
    REQUIRE(hold);
    CHECK(devices::format(*hold, 1) == "On");

    const devices::ParamInfo* lfoRate = devices::paramInfo(ModulatorType::Lfo, 2);
    REQUIRE(lfoRate);
    CHECK(devices::display(*lfoRate, 0) == doctest::Approx(0.01));
    CHECK(devices::display(*lfoRate, 1) == doctest::Approx(50));

    CHECK(devices::rateTicks(3, TimeSig{4, 4}) == 4 * kPpq);
    CHECK(devices::rateTicks(3, TimeSig{3, 4}) == 3 * kPpq);
    CHECK(devices::rateTicks(7, TimeSig{}) == kPpq / 4);
    CHECK(devices::rateTicks(12, TimeSig{}) == kPpq / 3);
    CHECK(devices::rateTicks(15, TimeSig{}) == kPpq * 3 / 4);
    CHECK(devices::paramInfo(DeviceType::Transpose, 99) == nullptr);
}

TEST_CASE("new devices and modulators start at their defaults") {
    Device d = devices::makeDevice(DeviceType::Arpeggiator, DeviceId(7));
    CHECK(d.name == "Arpeggiator");
    CHECK(d.on);
    REQUIRE(d.params.size() == devices::deviceInfo(DeviceType::Arpeggiator).params.size());
    const devices::ParamInfo& octaves = devices::deviceInfo(DeviceType::Arpeggiator).params[3];
    CHECK(devices::display(octaves, d.params[3]) == 1);

    Modulator steps = devices::makeModulator(ModulatorType::Steps, ModulatorId(8));
    CHECK(steps.steps.size() == 8);
    CHECK(steps.routes.empty());
}

TEST_CASE("device commands are reversible") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    DeviceId arp = addDeviceTo(song, 0, DeviceType::Arpeggiator);
    DeviceId chord = addDeviceTo(song, 0, DeviceType::Chord);
    CHECK(song.tracks[0].devices.size() == 2);

    // Insert at the front.
    applyChecked(song, addDevice(song, song.tracks[0].id, DeviceType::Transpose, 0, idsFrom(song)));
    CHECK(deviceAt(song, 0, 0).type == DeviceType::Transpose);

    applyChecked(song, setDeviceOn(song, arp, false));
    CHECK_FALSE(findDevice(song, arp)->on);
    CHECK(setDeviceOn(song, arp, false) == nullptr); // nothing changes

    applyChecked(song, renameDevice(song, chord, "Pad chords"));
    CHECK(findDevice(song, chord)->name == "Pad chords");
    CHECK(renameDevice(song, chord, "") == nullptr);

    applyChecked(song, setDeviceDrumLanes(song, chord, true));
    CHECK(findDevice(song, chord)->affectDrumLanes);

    ParamRef rate{ParamRef::Kind::Device, arp.value, 1};
    CommandPtr set = setParam(song, rate, 0.75f);
    REQUIRE(set);
    CHECK(set->name() == "Set Arpeggiator Rate");
    applyChecked(song, std::move(set));
    CHECK(*findParam(song, rate) == 0.75f);
    applyChecked(song, setParam(song, rate, 3)); // clamped
    CHECK(*findParam(song, rate) == 1);
    CHECK(setParam(song, {ParamRef::Kind::Device, arp.value, 99}, 0.5f) == nullptr);

    applyChecked(song, duplicateDevice(song, arp, idsFrom(song)));
    CHECK(song.tracks[0].devices.size() == 4);
    CHECK(deviceAt(song, 0, 2).type == DeviceType::Arpeggiator);
    CHECK(deviceAt(song, 0, 2).id != arp);

    applyChecked(song, removeDevice(song, chord));
    CHECK(findDevice(song, chord) == nullptr);
    CHECK(removeDevice(song, chord) == nullptr);
}

TEST_CASE("moving a device reorders the chain or takes it to another track") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    DeviceId a = addDeviceTo(song, 0, DeviceType::Transpose);
    DeviceId b = addDeviceTo(song, 0, DeviceType::Velocity);
    DeviceId c = addDeviceTo(song, 0, DeviceType::Echo);

    // Dropping before position 3 (the end) of the chain as it was.
    applyChecked(song, moveDevice(song, a, song.tracks[0].id, 3));
    CHECK(deviceAt(song, 0, 0).id == b);
    CHECK(deviceAt(song, 0, 1).id == c);
    CHECK(deviceAt(song, 0, 2).id == a);

    applyChecked(song, moveDevice(song, a, song.tracks[0].id, 0));
    CHECK(deviceAt(song, 0, 0).id == a);
    CHECK(moveDevice(song, a, song.tracks[0].id, 0) == nullptr);

    applyChecked(song, moveDevice(song, b, song.tracks[1].id, -1));
    CHECK(song.tracks[0].devices.size() == 2);
    REQUIRE(song.tracks[1].devices.size() == 1);
    CHECK(deviceAt(song, 1, 0).id == b);
}

TEST_CASE("modulator commands are reversible and respect each level's limit") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    DeviceId arp = addDeviceTo(song, 0, DeviceType::Arpeggiator);

    ModulatorId lfo = addModulatorAt(song, songLevel());
    ModulatorId lfo2 = addModulatorAt(song, songLevel());
    CHECK(findModulator(song, lfo)->name == "LFO");
    CHECK(findModulator(song, lfo2)->name == "LFO 2");

    for (int i = 0; i < kMaxDeviceModulators; i++)
        addModulatorAt(song, deviceLevel(song, 0, arp), ModulatorType::Steps);
    CHECK(addModulator(song, deviceLevel(song, 0, arp), ModulatorType::Lfo, idsFrom(song)) == nullptr);
    // A device on another track isn't a place.
    CHECK(addModulator(song, deviceLevel(song, 1, arp), ModulatorType::Lfo, idsFrom(song)) == nullptr);

    for (int i = 0; i < kMaxTrackModulators; i++)
        addModulatorAt(song, trackLevel(song, 1), ModulatorType::Random);
    CHECK(addModulator(song, trackLevel(song, 1), ModulatorType::Lfo, idsFrom(song)) == nullptr);

    applyChecked(song, renameModulator(song, lfo, "Wobble"));
    CHECK(findModulator(song, lfo)->name == "Wobble");

    ModulatorId steps = song.tracks[0].devices[0].modulators[0].id;
    std::vector<float> drawn(20, 2.f);
    applyChecked(song, setSteps(song, steps, drawn));
    CHECK(findModulator(song, steps)->steps.size() == size_t(kMaxSteps));
    CHECK(findModulator(song, steps)->steps[0] == 1);

    applyChecked(song, setParam(song, {ParamRef::Kind::Modulator, lfo.value, 2}, 0.25f));
    CHECK(findModulator(song, lfo)->params[2] == 0.25f);

    applyChecked(song, removeModulator(song, lfo2));
    CHECK(findModulator(song, lfo2) == nullptr);
}

TEST_CASE("modulators reach only what their level allows") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    DeviceId mine = addDeviceTo(song, 0, DeviceType::Arpeggiator);
    DeviceId sibling = addDeviceTo(song, 0, DeviceType::Velocity);
    DeviceId other = addDeviceTo(song, 1, DeviceType::Chord);

    ModulatorId global = addModulatorAt(song, songLevel());
    ModulatorId track = addModulatorAt(song, trackLevel(song, 0));
    ModulatorId local = addModulatorAt(song, deviceLevel(song, 0, mine));
    ModulatorId otherLocal = addModulatorAt(song, deviceLevel(song, 1, other));

    // Song level: anything.
    CHECK(routeProblem(song, global, toKnob(other, 0)).empty());
    CHECK(routeProblem(song, global, toKnob(mine, 0)).empty());

    // Track level: its own track only, including device modulators' settings there.
    CHECK(routeProblem(song, track, toKnob(sibling, 0)).empty());
    CHECK(!routeProblem(song, track, toKnob(other, 0)).empty());
    Modulation toLocal;
    toLocal.target = {ParamRef::Kind::Modulator, local.value, 2};
    CHECK(routeProblem(song, track, toLocal).empty());
    Modulation toOtherLocal;
    toOtherLocal.target = {ParamRef::Kind::Modulator, otherLocal.value, 2};
    CHECK(!routeProblem(song, track, toOtherLocal).empty());

    // Device level: its own device only.
    CHECK(routeProblem(song, local, toKnob(mine, 1)).empty());
    CHECK(routeProblem(song, local, toKnob(sibling, 0)) == "A device modulator only reaches its own device");

    // Never its own settings.
    CHECK(routeProblem(song, local, toLocal) == "A modulator can't turn its own settings");

    // Knobs that don't exist.
    CHECK(!routeProblem(song, global, toKnob(mine, 99)).empty());
    CHECK(!routeProblem(song, global, toKnob(DeviceId(9999), 0)).empty());

    // AUTO outputs: song modulators reach any track, track modulators only their own.
    Modulation out;
    out.kind = Modulation::Kind::AutoChannel;
    out.track = song.tracks[2].id;
    out.channel = 3;
    CHECK(routeProblem(song, global, out).empty());
    CHECK(!routeProblem(song, track, out).empty());
    out.track = song.tracks[0].id;
    CHECK(routeProblem(song, track, out).empty());
    out.channel = 8;
    CHECK(!routeProblem(song, track, out).empty());

    // Rack knobs anywhere.
    Modulation knob;
    knob.kind = Modulation::Kind::RackKnob;
    knob.knob = {42, 3, 0, 1};
    CHECK(routeProblem(song, local, knob).empty());
}

TEST_CASE("routes are limited per modulator and per knob, and never doubled") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    DeviceId arp = addDeviceTo(song, 0, DeviceType::Arpeggiator);
    std::vector<ModulatorId> mods;
    for (int i = 0; i < kMaxModulationsPerKnob + 1; i++)
        mods.push_back(addModulatorAt(song, songLevel()));

    applyChecked(song, addRoute(song, mods[0], toKnob(arp, 1, 0.3f)));
    CHECK(findModulator(song, mods[0])->routes.size() == 1);
    CHECK(addRoute(song, mods[0], toKnob(arp, 1, -0.3f)) == nullptr);
    CHECK(modulationsOn(song, {ParamRef::Kind::Device, arp.value, 1}) == 1);

    for (int i = 1; i < kMaxModulationsPerKnob; i++)
        applyChecked(song, addRoute(song, mods[size_t(i)], toKnob(arp, 1)));
    CHECK(addRoute(song, mods.back(), toKnob(arp, 1)) == nullptr);
    CHECK(routeProblem(song, mods.back(), toKnob(arp, 1)) == "This knob already has 4 modulators");

    for (int p = 1; p < kMaxRoutes; p++) {
        Modulation knob;
        knob.kind = Modulation::Kind::RackKnob;
        knob.knob = {77, p, 0, 1};
        applyChecked(song, addRoute(song, mods[0], knob));
    }
    CHECK(findModulator(song, mods[0])->routes.size() == size_t(kMaxRoutes));
    CHECK(addRoute(song, mods[0], toKnob(arp, 0)) == nullptr);

    applyChecked(song, setRouteAmount(song, mods[0], 0, -2));
    CHECK(findModulator(song, mods[0])->routes[0].amount == -1);
    CHECK(setRouteAmount(song, mods[0], 99, 0.5f) == nullptr);

    applyChecked(song, removeRoute(song, mods[0], 0));
    CHECK(findModulator(song, mods[0])->routes.size() == size_t(kMaxRoutes - 1));
    CHECK(removeRoute(song, mods[0], 99) == nullptr);
}

TEST_CASE("removing or moving targets drops the routes that can no longer reach them") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    DeviceId arp = addDeviceTo(song, 0, DeviceType::Arpeggiator);
    DeviceId vel = addDeviceTo(song, 0, DeviceType::Velocity);
    ModulatorId global = addModulatorAt(song, songLevel());
    ModulatorId track = addModulatorAt(song, trackLevel(song, 0));
    ModulatorId local = addModulatorAt(song, deviceLevel(song, 0, arp));
    applyChecked(song, addRoute(song, global, toKnob(arp, 0)));
    applyChecked(song, addRoute(song, global, toKnob(vel, 0)));
    applyChecked(song, addRoute(song, track, toKnob(arp, 1)));
    applyChecked(song, addRoute(song, local, toKnob(arp, 2)));
    Modulation toLocal;
    toLocal.target = {ParamRef::Kind::Modulator, local.value, 0};
    applyChecked(song, addRoute(song, track, toLocal));

    SUBCASE("moving a device to another track keeps song and device routes, drops track routes") {
        applyChecked(song, moveDevice(song, arp, song.tracks[1].id, -1));
        CHECK(findModulator(song, global)->routes.size() == 2);
        CHECK(findModulator(song, track)->routes.empty());
        ModulatorPlace place;
        REQUIRE(findModulator(song, local, &place));
        CHECK(place.track == song.tracks[1].id);
        CHECK(findModulator(song, local)->routes.size() == 1);
    }
    SUBCASE("removing a device drops every route into it and its modulators") {
        applyChecked(song, removeDevice(song, arp));
        CHECK(findModulator(song, global)->routes.size() == 1);
        CHECK(findModulator(song, track)->routes.empty());
        CHECK(findModulator(song, local) == nullptr);
    }
    SUBCASE("removing a modulator drops routes into its settings") {
        applyChecked(song, removeModulator(song, local));
        CHECK(findModulator(song, track)->routes.size() == 1);
    }
    SUBCASE("deleting the track drops song routes into it, and undo brings them back") {
        Song before = song;
        auto remove = removeTracks(song, {song.tracks[0].id});
        REQUIRE(remove);
        remove->apply(song);
        CHECK(findModulator(song, global)->routes.empty());
        remove->revert(song);
        CHECK(song == before);
    }
}

TEST_CASE("copies of devices and tracks point their routes at the copies") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    DeviceId arp = addDeviceTo(song, 0, DeviceType::Arpeggiator);
    ModulatorId local = addModulatorAt(song, deviceLevel(song, 0, arp));
    ModulatorId track = addModulatorAt(song, trackLevel(song, 0));
    applyChecked(song, addRoute(song, local, toKnob(arp, 1)));
    applyChecked(song, addRoute(song, track, toKnob(arp, 2)));
    Modulation out;
    out.kind = Modulation::Kind::AutoChannel;
    out.track = song.tracks[0].id;
    out.channel = 1;
    applyChecked(song, addRoute(song, track, out));

    SUBCASE("duplicate device") {
        applyChecked(song, duplicateDevice(song, arp, idsFrom(song)));
        const Device& copy = song.tracks[0].devices[1];
        REQUIRE(copy.modulators.size() == 1);
        CHECK(copy.modulators[0].id != local);
        CHECK(copy.modulators[0].routes[0].target.owner == copy.id.value);
    }
    SUBCASE("duplicate track") {
        AutomationEnvelope lane;
        lane.id = song.newId<AutomationId>();
        lane.paramTarget = {ParamRef::Kind::Device, arp.value, 1};
        song.tracks[0].automation.push_back(lane);

        auto dup = duplicateTracks(song, {song.tracks[0].id}, idsFrom(song));
        REQUIRE(dup);
        Song before = song;
        dup->apply(song);
        const Track& copy = song.tracks[1];
        REQUIRE(copy.devices.size() == 1);
        DeviceId copied = copy.devices[0].id;
        CHECK(copied != arp);
        CHECK(copy.devices[0].modulators[0].routes[0].target.owner == copied.value);
        REQUIRE(copy.modulators.size() == 1);
        CHECK(copy.modulators[0].id != track);
        CHECK(copy.modulators[0].routes[0].target.owner == copied.value);
        CHECK(copy.modulators[0].routes[1].track == copy.id);
        CHECK(copy.automation[0].paramTarget.owner == copied.value);

        // Every ID in the song is still unique, so normalize has nothing to repair.
        Song normalized = song;
        normalize(normalized);
        CHECK(normalized == song);
        dup->revert(song);
        CHECK(song == before);
    }
}

TEST_CASE("chains survive a save and load") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    DeviceId arp = addDeviceTo(song, 0, DeviceType::Arpeggiator);
    ModulatorId global = addModulatorAt(song, songLevel(), ModulatorType::Steps);
    ModulatorId local = addModulatorAt(song, deviceLevel(song, 0, arp), ModulatorType::Envelope);
    applyChecked(song, addRoute(song, global, toKnob(arp, 1, -0.25f)));
    applyChecked(song, addRoute(song, local, toKnob(arp, 2)));
    Modulation knob;
    knob.kind = Modulation::Kind::RackKnob;
    knob.knob = {123, 4, 0.1f, 0.9f};
    applyChecked(song, addRoute(song, global, knob));
    AutomationEnvelope lane;
    lane.id = song.newId<AutomationId>();
    lane.paramTarget = {ParamRef::Kind::Modulator, local.value, 1};
    song.tracks[0].automation.push_back(lane);

    CHECK(throughJson(song) == song);

    for (uint32_t seed = 1; seed <= 30; seed++) {
        CAPTURE(seed);
        Song random = testing::RandomSong(seed).make();
        CHECK(throughJson(random) == random);
    }
}

TEST_CASE("loading repairs broken chains") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    DeviceId arp = addDeviceTo(song, 0, DeviceType::Arpeggiator);
    ModulatorId track = addModulatorAt(song, trackLevel(song, 0));
    ModulatorId global = addModulatorAt(song, songLevel());

    SUBCASE("knob values out of range and missing knobs") {
        Device& d = song.tracks[0].devices[0];
        d.params = {5, -1, NAN};
        normalize(song);
        REQUIRE(d.params.size() == devices::deviceInfo(DeviceType::Arpeggiator).params.size());
        CHECK(d.params[0] == 1);
        CHECK(d.params[1] == 0);
        CHECK(d.params[2] == 0);
        CHECK(d.params[3] == devices::defaultNormalized(devices::deviceInfo(DeviceType::Arpeggiator).params[3]));
    }
    SUBCASE("routes outside their reach, at missing targets, or past the per-knob limit") {
        DeviceId other = addDeviceTo(song, 1, DeviceType::Chord);
        findModulator(song, track)->routes = {toKnob(arp, 0), toKnob(other, 0), toKnob(DeviceId(4242), 0)};
        normalize(song);
        REQUIRE(findModulator(song, track)->routes.size() == 1);
        CHECK(findModulator(song, track)->routes[0].target.owner == arp.value);

        findModulator(song, global)->routes.clear();
        for (int i = 0; i < kMaxModulationsPerKnob + 2; i++)
            findModulator(song, global)->routes.push_back(toKnob(arp, 1)); // all on one knob
        normalize(song);
        CHECK(modulationsOn(song, {ParamRef::Kind::Device, arp.value, 1}) == kMaxModulationsPerKnob);
    }
    SUBCASE("duplicate IDs get fresh ones") {
        song.tracks[1].devices.push_back(song.tracks[0].devices[0]);
        normalize(song);
        CHECK(song.tracks[1].devices[0].id != arp);
        CHECK(song.nextId > song.tracks[1].devices[0].id.value);
    }
    SUBCASE("too many devices") {
        for (int i = 0; i < kMaxDevices + 3; i++)
            song.tracks[2].devices.push_back(devices::makeDevice(DeviceType::Echo, song.newId<DeviceId>()));
        normalize(song);
        CHECK(song.tracks[2].devices.size() == size_t(kMaxDevices));
        CHECK(addDevice(song, song.tracks[2].id, DeviceType::Echo, -1, idsFrom(song)) == nullptr);
    }
}

TEST_CASE("songs from before devices load with empty chains") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    json::Ref j(songToJson(song));
    json_object_del(j.get(), "modulators");
    Song loaded = songFromJson(j.get(), 4);
    CHECK(loaded.modulators.empty());
    for (const Track& t : loaded.tracks) {
        CHECK(t.devices.empty());
        CHECK(t.modulators.empty());
    }
}

TEST_CASE("copying a device to another place makes an independent copy") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    DeviceId arp = addDeviceTo(song, 0, DeviceType::Arpeggiator);
    addDeviceTo(song, 0, DeviceType::Velocity);
    ModulatorId local = addModulatorAt(song, deviceLevel(song, 0, arp));
    applyChecked(song, addRoute(song, local, toKnob(arp, 1)));

    applyChecked(song, copyDevice(song, arp, song.tracks[0].id, 2, idsFrom(song)));
    REQUIRE(song.tracks[0].devices.size() == 3);
    const Device& copy = song.tracks[0].devices[2];
    CHECK(copy.type == DeviceType::Arpeggiator);
    CHECK(copy.id != arp);
    CHECK(copy.modulators[0].routes[0].target.owner == copy.id.value);

    applyChecked(song, copyDevice(song, arp, song.tracks[1].id, -1, idsFrom(song)));
    CHECK(song.tracks[1].devices.size() == 1);

    CommandPtr dup = duplicateDevice(song, arp, idsFrom(song));
    REQUIRE(dup);
    CHECK(dup->name() == "Duplicate Arpeggiator");
    applyChecked(song, std::move(dup));
    CHECK(song.tracks[0].devices[1].type == DeviceType::Arpeggiator);
}
