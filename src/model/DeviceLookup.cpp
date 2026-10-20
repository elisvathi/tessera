#include "DeviceLookup.hpp"

#include <algorithm>

namespace tessera {

namespace {

template <typename SongT, typename DeviceT>
DeviceT* findDeviceIn(SongT& song, DeviceId id, TrackId* track) {
    for (auto& t : song.tracks)
        for (auto& d : t.devices)
            if (d.id == id) {
                if (track)
                    *track = t.id;
                return &d;
            }
    return nullptr;
}

template <typename SongT, typename ModT>
ModT* findModulatorIn(SongT& song, ModulatorId id, ModulatorPlace* place) {
    for (auto& m : song.modulators)
        if (m.id == id) {
            if (place)
                *place = {ModLevel::Song, TrackId(), DeviceId()};
            return &m;
        }
    for (auto& t : song.tracks) {
        for (auto& m : t.modulators)
            if (m.id == id) {
                if (place)
                    *place = {ModLevel::Track, t.id, DeviceId()};
                return &m;
            }
        for (auto& d : t.devices)
            for (auto& m : d.modulators)
                if (m.id == id) {
                    if (place)
                        *place = {ModLevel::Device, t.id, d.id};
                    return &m;
                }
    }
    return nullptr;
}

} // namespace

Device* findDevice(Song& song, DeviceId id, TrackId* track) { return findDeviceIn<Song, Device>(song, id, track); }
const Device* findDevice(const Song& song, DeviceId id, TrackId* track) {
    return findDeviceIn<const Song, const Device>(song, id, track);
}
Modulator* findModulator(Song& song, ModulatorId id, ModulatorPlace* place) {
    return findModulatorIn<Song, Modulator>(song, id, place);
}
const Modulator* findModulator(const Song& song, ModulatorId id, ModulatorPlace* place) {
    return findModulatorIn<const Song, const Modulator>(song, id, place);
}

float* findParam(Song& song, const ParamRef& ref) {
    return const_cast<float*>(findParam(static_cast<const Song&>(song), ref));
}

const float* findParam(const Song& song, const ParamRef& ref) {
    const std::vector<float>* params = nullptr;
    if (ref.kind == ParamRef::Kind::Device) {
        if (const Device* d = findDevice(song, DeviceId(ref.owner)))
            params = &d->params;
    } else if (const Modulator* m = findModulator(song, ModulatorId(ref.owner))) {
        params = &m->params;
    }
    if (!params || ref.param >= params->size())
        return nullptr;
    return &(*params)[ref.param];
}

TrackId trackOf(const Song& song, const ParamRef& ref) {
    TrackId track;
    if (ref.kind == ParamRef::Kind::Device) {
        findDevice(song, DeviceId(ref.owner), &track);
        return track;
    }
    ModulatorPlace place;
    if (findModulator(song, ModulatorId(ref.owner), &place))
        return place.track;
    return TrackId();
}

bool reachable(const Song& song, const ModulatorPlace& place, ModulatorId self, const Modulation& m) {
    switch (m.kind) {
    case Modulation::Kind::RackKnob:
        return m.knob.moduleId >= 0;
    case Modulation::Kind::AutoChannel:
        if (m.channel < 0 || m.channel >= kMaxAutomationLanes || !song.findTrack(m.track))
            return false;
        return place.level == ModLevel::Song || m.track == place.track;
    case Modulation::Kind::Param:
    default:
        break;
    }
    if (!findParam(song, m.target))
        return false;
    if (m.target.kind == ParamRef::Kind::Modulator && m.target.owner == self.value)
        return false; // its own settings would feed back on themselves
    switch (place.level) {
    case ModLevel::Song:
        return true;
    case ModLevel::Track:
        return trackOf(song, m.target) == place.track;
    case ModLevel::Device: {
        if (m.target.kind == ParamRef::Kind::Device)
            return m.target.owner == place.device.value;
        ModulatorPlace target;
        findModulator(song, ModulatorId(m.target.owner), &target);
        return target.level == ModLevel::Device && target.device == place.device;
    }
    }
    return false;
}

void forEachModulator(Song& song, const std::function<void(Modulator&, const ModulatorPlace&)>& f) {
    for (Modulator& m : song.modulators)
        f(m, {ModLevel::Song, TrackId(), DeviceId()});
    for (Track& t : song.tracks) {
        for (Modulator& m : t.modulators)
            f(m, {ModLevel::Track, t.id, DeviceId()});
        for (Device& d : t.devices)
            for (Modulator& m : d.modulators)
                f(m, {ModLevel::Device, t.id, d.id});
    }
}

void forEachModulator(const Song& song, const std::function<void(const Modulator&, const ModulatorPlace&)>& f) {
    forEachModulator(const_cast<Song&>(song),
                     [&](Modulator& m, const ModulatorPlace& p) { f(static_cast<const Modulator&>(m), p); });
}

int modulationsOn(const Song& song, const ParamRef& ref) {
    int n = 0;
    forEachModulator(song, [&](const Modulator& m, const ModulatorPlace&) {
        for (const Modulation& r : m.routes)
            n += r.kind == Modulation::Kind::Param && r.target == ref;
    });
    return n;
}

int pruneRoutes(Song& song) {
    // Decide first, then remove, so every check sees the song as it was.
    std::vector<std::pair<Modulator*, std::vector<Modulation>>> kept;
    int removed = 0;
    forEachModulator(song, [&](Modulator& m, const ModulatorPlace& place) {
        std::vector<Modulation> routes;
        for (const Modulation& r : m.routes)
            if (reachable(song, place, m.id, r))
                routes.push_back(r);
            else
                removed++;
        kept.push_back({&m, std::move(routes)});
    });
    for (auto& [m, routes] : kept)
        m->routes = std::move(routes);
    return removed;
}

} // namespace tessera
