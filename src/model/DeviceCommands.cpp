#include "DeviceCommands.hpp"

#include "DeviceTypes.hpp"

#include <algorithm>
#include <cassert>

namespace tessera {

// ---- ReplaceChains ------------------------------------------------------------------------------------------

ChangeSet ReplaceChains::changes() const {
    if (before.songModulators || after.songModulators)
        return ChangeSet::all();
    ChangeSet c;
    for (const ChainParts::TrackChain& t : after.tracks)
        c.tracks.push_back(t.track);
    return c;
}

void ReplaceChains::set(Song& song, const ChainParts& parts) {
    if (parts.songModulators)
        song.modulators = *parts.songModulators;
    for (const ChainParts::TrackChain& t : parts.tracks) {
        Track* track = song.findTrack(t.track);
        assert(track && "ReplaceChains on a missing track");
        if (!track)
            continue;
        track->devices = t.devices;
        track->modulators = t.modulators;
    }
}

namespace {

/// The parts of a song chain edits work on. Clips are left out: they can be large and no chain edit reads them.
Song chainsOf(const Song& song) {
    Song work;
    work.timeSig = song.timeSig;
    work.tracks = song.tracks;
    work.modulators = song.modulators;
    work.nextId = song.nextId;
    return work;
}

/// Runs `edit` on a copy of the song's chains and turns what it changed into one command. `edit` returns
/// false to refuse; a refused or empty edit gives null.
template <typename Edit>
CommandPtr chainEdit(const Song& song, const std::string& name, Edit&& edit) {
    Song work = chainsOf(song);
    if (!edit(work))
        return nullptr;
    assert(work.tracks.size() == song.tracks.size() && "chain edits never add or remove tracks");

    ChainParts before, after;
    if (!(work.modulators == song.modulators)) {
        before.songModulators = song.modulators;
        after.songModulators = work.modulators;
    }
    for (size_t i = 0; i < song.tracks.size(); i++) {
        const Track& a = song.tracks[i];
        const Track& b = work.tracks[i];
        if (a.devices == b.devices && a.modulators == b.modulators)
            continue;
        before.tracks.push_back({a.id, a.devices, a.modulators});
        after.tracks.push_back({b.id, b.devices, b.modulators});
    }
    if (!before.songModulators && before.tracks.empty())
        return nullptr;
    return std::make_unique<ReplaceChains>(name, std::move(before), std::move(after));
}

/// The modulator list at a place, or nullptr when the place doesn't exist.
std::vector<Modulator>* modulatorsAt(Song& song, const ModulatorPlace& place) {
    switch (place.level) {
    case ModLevel::Song:
        return &song.modulators;
    case ModLevel::Track: {
        Track* t = song.findTrack(place.track);
        return t ? &t->modulators : nullptr;
    }
    case ModLevel::Device: {
        TrackId track;
        Device* d = findDevice(song, place.device, &track);
        return d && track == place.track ? &d->modulators : nullptr;
    }
    }
    return nullptr;
}

size_t limitAt(ModLevel level) {
    switch (level) {
    case ModLevel::Song:
        return kMaxSongModulators;
    case ModLevel::Track:
        return kMaxTrackModulators;
    case ModLevel::Device:
    default:
        return kMaxDeviceModulators;
    }
}

/// "LFO", then "LFO 2", "LFO 3"... so modulators of one type stay apart in menus.
std::string freeName(const std::vector<Modulator>& mods, const std::string& base) {
    auto taken = [&](const std::string& name) {
        return std::any_of(mods.begin(), mods.end(), [&](const Modulator& m) { return m.name == name; });
    };
    if (!taken(base))
        return base;
    for (int n = 2;; n++) {
        std::string name = base + " " + std::to_string(n);
        if (!taken(name))
            return name;
    }
}

std::vector<Device>::iterator deviceIn(Track& track, DeviceId id) {
    return std::find_if(track.devices.begin(), track.devices.end(), [&](const Device& d) { return d.id == id; });
}

Modulation* routeAt(Song& song, ModulatorId id, int index) {
    Modulator* m = findModulator(song, id);
    if (!m || index < 0 || index >= int(m->routes.size()))
        return nullptr;
    return &m->routes[size_t(index)];
}

bool sameTarget(const Modulation& a, const Modulation& b) {
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case Modulation::Kind::Param:
        return a.target == b.target;
    case Modulation::Kind::AutoChannel:
        return a.track == b.track && a.channel == b.channel;
    case Modulation::Kind::RackKnob:
    default:
        return a.knob.moduleId == b.knob.moduleId && a.knob.paramId == b.knob.paramId;
    }
}

std::string deviceName(const Song& song, DeviceId id) {
    const Device* d = findDevice(song, id);
    return d ? d->name : "device";
}

std::string modulatorName(const Song& song, ModulatorId id) {
    const Modulator* m = findModulator(song, id);
    return m ? m->name : "modulator";
}

} // namespace

// ---- Devices ------------------------------------------------------------------------------------------------

CommandPtr addDevice(const Song& song, TrackId trackId, DeviceType type, int index, const NextId& nextId) {
    const Track* existing = song.findTrack(trackId);
    if (!existing || int(existing->devices.size()) >= kMaxDevices)
        return nullptr;
    Device device = devices::makeDevice(type, DeviceId(nextId()));
    return chainEdit(song, "Add " + device.name, [&](Song& work) {
        Track& track = *work.findTrack(trackId);
        int at = index < 0 ? int(track.devices.size()) : std::min(index, int(track.devices.size()));
        track.devices.insert(track.devices.begin() + at, device);
        return true;
    });
}

CommandPtr removeDevice(const Song& song, DeviceId id) {
    TrackId trackId;
    if (!findDevice(song, id, &trackId))
        return nullptr;
    return chainEdit(song, "Remove " + deviceName(song, id), [&](Song& work) {
        Track& track = *work.findTrack(trackId);
        track.devices.erase(deviceIn(track, id));
        pruneRoutes(work);
        return true;
    });
}

CommandPtr moveDevice(const Song& song, DeviceId id, TrackId toTrack, int index) {
    TrackId fromTrack;
    if (!findDevice(song, id, &fromTrack))
        return nullptr;
    const Track* target = song.findTrack(toTrack);
    if (!target || (toTrack != fromTrack && int(target->devices.size()) >= kMaxDevices))
        return nullptr;
    return chainEdit(song, "Move " + deviceName(song, id), [&](Song& work) {
        Track& from = *work.findTrack(fromTrack);
        auto it = deviceIn(from, id);
        int oldIndex = int(it - from.devices.begin());
        Device device = std::move(*it);
        from.devices.erase(it);
        Track& to = *work.findTrack(toTrack);
        int at = index < 0 ? int(to.devices.size()) : index;
        // `index` counts positions in the chain as it was, so moving right past itself lands one earlier.
        if (toTrack == fromTrack && at > oldIndex)
            at--;
        at = std::clamp(at, 0, int(to.devices.size()));
        to.devices.insert(to.devices.begin() + at, std::move(device));
        pruneRoutes(work);
        return true;
    });
}

CommandPtr duplicateDevice(const Song& song, DeviceId id, const NextId& nextId) {
    TrackId trackId;
    const Device* original = findDevice(song, id, &trackId);
    if (!original)
        return nullptr;
    const Track& track = *song.findTrack(trackId);
    int at = int(std::find_if(track.devices.begin(), track.devices.end(), [&](const Device& d) { return d.id == id; }) -
                 track.devices.begin());
    CommandPtr command = copyDevice(song, id, trackId, at + 1, nextId);
    return command ? std::make_unique<ReplaceChains>("Duplicate " + original->name,
                                                     static_cast<ReplaceChains&>(*command).partsBefore(),
                                                     static_cast<ReplaceChains&>(*command).partsAfter())
                   : nullptr;
}

CommandPtr copyDevice(const Song& song, DeviceId id, TrackId toTrack, int index, const NextId& nextId) {
    const Device* original = findDevice(song, id);
    const Track* target = song.findTrack(toTrack);
    if (!original || !target || int(target->devices.size()) >= kMaxDevices)
        return nullptr;
    std::vector<Device> copy = {*original};
    std::vector<Modulator> noTrackModulators;
    ChainRemap remap;
    renewIds(copy, noTrackModulators, nextId, remap);
    retargetRoutes(copy, noTrackModulators, remap);
    return chainEdit(song, "Copy " + original->name, [&](Song& work) {
        Track& track = *work.findTrack(toTrack);
        int at = index < 0 ? int(track.devices.size()) : std::min(index, int(track.devices.size()));
        track.devices.insert(track.devices.begin() + at, std::move(copy.front()));
        pruneRoutes(work);
        return true;
    });
}

CommandPtr setDeviceOn(const Song& song, DeviceId id, bool on) {
    return chainEdit(song, (on ? "Turn on " : "Bypass ") + deviceName(song, id), [&](Song& work) {
        Device* d = findDevice(work, id);
        if (!d)
            return false;
        d->on = on;
        return true;
    });
}

CommandPtr setDeviceDrumLanes(const Song& song, DeviceId id, bool affect) {
    return chainEdit(song,
                     (affect ? "Let " : "Keep ") + deviceName(song, id) + (affect ? " change" : " off") + " drum lanes",
                     [&](Song& work) {
                         Device* d = findDevice(work, id);
                         if (!d)
                             return false;
                         d->affectDrumLanes = affect;
                         return true;
                     });
}

CommandPtr renameDevice(const Song& song, DeviceId id, const std::string& name) {
    if (name.empty())
        return nullptr;
    return chainEdit(song, "Rename " + deviceName(song, id), [&](Song& work) {
        Device* d = findDevice(work, id);
        if (!d)
            return false;
        d->name = name;
        return true;
    });
}

CommandPtr setParam(const Song& song, const ParamRef& ref, float value) {
    std::string label = "Set ";
    if (ref.kind == ParamRef::Kind::Device) {
        if (const Device* d = findDevice(song, DeviceId(ref.owner)))
            if (const devices::ParamInfo* p = devices::paramInfo(d->type, ref.param))
                label += d->name + " " + p->name;
    } else if (const Modulator* m = findModulator(song, ModulatorId(ref.owner))) {
        if (const devices::ParamInfo* p = devices::paramInfo(m->type, ref.param))
            label += m->name + " " + p->name;
    }
    return chainEdit(song, label, [&](Song& work) {
        float* param = findParam(work, ref);
        if (!param)
            return false;
        *param = std::clamp(value, 0.f, 1.f);
        return true;
    });
}

// ---- Modulators ---------------------------------------------------------------------------------------------

CommandPtr addModulator(const Song& song, const ModulatorPlace& place, ModulatorType type, const NextId& nextId,
                        ModulatorId* added) {
    Song probe = chainsOf(song);
    std::vector<Modulator>* list = modulatorsAt(probe, place);
    if (!list || list->size() >= limitAt(place.level))
        return nullptr;
    Modulator mod = devices::makeModulator(type, ModulatorId(nextId()));
    mod.name = freeName(*list, mod.name);
    if (added)
        *added = mod.id;
    return chainEdit(song, "Add " + mod.name, [&](Song& work) {
        modulatorsAt(work, place)->push_back(mod);
        return true;
    });
}

CommandPtr removeModulator(const Song& song, ModulatorId id) {
    ModulatorPlace place;
    if (!findModulator(song, id, &place))
        return nullptr;
    return chainEdit(song, "Remove " + modulatorName(song, id), [&](Song& work) {
        std::vector<Modulator>& list = *modulatorsAt(work, place);
        list.erase(std::find_if(list.begin(), list.end(), [&](const Modulator& m) { return m.id == id; }));
        pruneRoutes(work);
        return true;
    });
}

CommandPtr renameModulator(const Song& song, ModulatorId id, const std::string& name) {
    if (name.empty())
        return nullptr;
    return chainEdit(song, "Rename " + modulatorName(song, id), [&](Song& work) {
        Modulator* m = findModulator(work, id);
        if (!m)
            return false;
        m->name = name;
        return true;
    });
}

CommandPtr setSteps(const Song& song, ModulatorId id, std::vector<float> steps) {
    if (steps.size() > size_t(kMaxSteps))
        steps.resize(size_t(kMaxSteps));
    for (float& s : steps)
        s = std::clamp(s, 0.f, 1.f);
    return chainEdit(song, "Draw " + modulatorName(song, id) + " steps", [&](Song& work) {
        Modulator* m = findModulator(work, id);
        if (!m)
            return false;
        m->steps = steps;
        return true;
    });
}

std::string routeProblem(const Song& song, ModulatorId id, const Modulation& route) {
    ModulatorPlace place;
    const Modulator* m = findModulator(song, id, &place);
    if (!m)
        return "The modulator is gone";
    if (!reachable(song, place, id, route)) {
        if (route.kind == Modulation::Kind::Param && route.target.kind == ParamRef::Kind::Modulator &&
            route.target.owner == id.value)
            return "A modulator can't turn its own settings";
        switch (place.level) {
        case ModLevel::Track:
            return "A track modulator only reaches its own track";
        case ModLevel::Device:
            return "A device modulator only reaches its own device";
        case ModLevel::Song:
        default:
            return "That target doesn't exist";
        }
    }
    if (std::any_of(m->routes.begin(), m->routes.end(), [&](const Modulation& r) { return sameTarget(r, route); }))
        return "Already modulated by " + m->name;
    if (int(m->routes.size()) >= kMaxRoutes)
        return m->name + " already has " + std::to_string(kMaxRoutes) + " targets";
    if (route.kind == Modulation::Kind::Param && modulationsOn(song, route.target) >= kMaxModulationsPerKnob)
        return "This knob already has " + std::to_string(kMaxModulationsPerKnob) + " modulators";
    return "";
}

CommandPtr addRoute(const Song& song, ModulatorId id, const Modulation& route) {
    if (!routeProblem(song, id, route).empty())
        return nullptr;
    return chainEdit(song, "Modulate with " + modulatorName(song, id), [&](Song& work) {
        Modulation r = route;
        r.amount = std::clamp(r.amount, -1.f, 1.f);
        findModulator(work, id)->routes.push_back(r);
        return true;
    });
}

CommandPtr removeRoute(const Song& song, ModulatorId id, int index) {
    return chainEdit(song, "Remove modulation", [&](Song& work) {
        Modulator* m = findModulator(work, id);
        if (!m || index < 0 || index >= int(m->routes.size()))
            return false;
        m->routes.erase(m->routes.begin() + index);
        return true;
    });
}

CommandPtr setRouteAmount(const Song& song, ModulatorId id, int index, float amount) {
    return chainEdit(song, "Set modulation amount", [&](Song& work) {
        Modulation* r = routeAt(work, id, index);
        if (!r)
            return false;
        r->amount = std::clamp(amount, -1.f, 1.f);
        return true;
    });
}

// ---- Copying ------------------------------------------------------------------------------------------------

void renewIds(std::vector<Device>& devices, std::vector<Modulator>& modulators, const NextId& nextId,
              ChainRemap& remap) {
    auto renew = [&](auto& id) {
        uint64_t fresh = nextId();
        remap[id.value] = fresh;
        id.value = fresh;
    };
    for (Device& d : devices) {
        renew(d.id);
        for (Modulator& m : d.modulators)
            renew(m.id);
    }
    for (Modulator& m : modulators)
        renew(m.id);
}

void retarget(ParamRef& ref, const ChainRemap& remap) {
    auto it = remap.find(ref.owner);
    if (it != remap.end())
        ref.owner = it->second;
}

void retargetRoutes(std::vector<Device>& devices, std::vector<Modulator>& modulators, const ChainRemap& remap) {
    auto fix = [&](Modulator& m) {
        for (Modulation& r : m.routes)
            if (r.kind == Modulation::Kind::Param)
                retarget(r.target, remap);
    };
    for (Device& d : devices)
        for (Modulator& m : d.modulators)
            fix(m);
    for (Modulator& m : modulators)
        fix(m);
}

} // namespace tessera
