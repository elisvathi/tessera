#include "DeviceLookup.hpp"

#include "DeviceTypes.hpp"

#include <algorithm>

namespace tessera {

namespace {

/// Calls `f(devices, owner)` for every chain in the song: each track's, then each clip's own.
template <typename SongT, typename F>
void forEachChain(SongT& song, F&& f) {
    for (auto& t : song.tracks)
        f(t.devices, ChainOwner(t.id));
    for (auto& [id, c] : song.clips)
        if (!c.devices.empty())
            f(c.devices, ChainOwner(id));
}

/// The track a chain plays on: the track itself, or the first track using the clip.
TrackId trackOfOwner(const Song& song, ChainOwner owner) {
    return owner.isClip() ? trackUsing(song, owner.clip) : owner.track;
}

template <typename SongT, typename DeviceT>
DeviceT* findDeviceIn(SongT& song, DeviceId id, ChainOwner* owner) {
    DeviceT* found = nullptr;
    forEachChain(song, [&](auto& chain, ChainOwner o) {
        for (auto& d : chain) {
            if (found)
                return;
            if (d.id == id)
                found = &d;
            for (auto& p : d.playheads)
                for (auto& b : p.branch)
                    if (!found && b.id == id)
                        found = &b;
            if (found && owner)
                *owner = o;
        }
    });
    return found;
}

template <typename SongT, typename ModT>
ModT* findModulatorIn(SongT& song, ModulatorId id, ModulatorPlace* place) {
    for (auto& m : song.modulators)
        if (m.id == id) {
            if (place)
                *place = {ModLevel::Song, TrackId(), DeviceId(), ClipId()};
            return &m;
        }
    for (auto& t : song.tracks)
        for (auto& m : t.modulators)
            if (m.id == id) {
                if (place)
                    *place = {ModLevel::Track, t.id, DeviceId(), ClipId()};
                return &m;
            }
    for (auto& [cid, c] : song.clips)
        for (auto& m : c.modulators)
            if (m.id == id) {
                if (place)
                    *place = {ModLevel::Clip, trackUsing(song, cid), DeviceId(), cid};
                return &m;
            }
    ModT* found = nullptr;
    forEachChain(song, [&](auto& chain, ChainOwner o) {
        for (auto& d : chain)
            for (auto& m : d.modulators)
                if (!found && m.id == id) {
                    found = &m;
                    if (place)
                        *place = {ModLevel::Device, trackOfOwner(song, o), d.id, o.clip};
                }
    });
    return found;
}

template <typename SongT, typename PlayheadT>
PlayheadT* findPlayheadIn(SongT& song, PlayheadId id, ChainOwner* owner, DeviceId* device) {
    PlayheadT* found = nullptr;
    forEachChain(song, [&](auto& chain, ChainOwner o) {
        for (auto& d : chain)
            for (auto& p : d.playheads)
                if (!found && p.id == id) {
                    found = &p;
                    if (owner)
                        *owner = o;
                    if (device)
                        *device = d.id;
                }
    });
    return found;
}

} // namespace

Playhead* findPlayhead(Song& song, PlayheadId id, TrackId* track, DeviceId* device) {
    return const_cast<Playhead*>(findPlayhead(static_cast<const Song&>(song), id, track, device));
}
const Playhead* findPlayhead(const Song& song, PlayheadId id, TrackId* track, DeviceId* device) {
    ChainOwner owner;
    const Playhead* p = findPlayheadIn<const Song, const Playhead>(song, id, &owner, device);
    if (p && track)
        *track = trackOfOwner(song, owner);
    return p;
}

std::vector<Device>* chainOf(Song& song, DeviceId id, PlayheadId* playhead) {
    std::vector<Device>* found = nullptr;
    forEachChain(song, [&](std::vector<Device>& chain, ChainOwner) {
        for (Device& d : chain) {
            if (found)
                return;
            if (d.id == id) {
                if (playhead)
                    *playhead = PlayheadId();
                found = &chain;
            }
            for (Playhead& p : d.playheads)
                if (!found && std::any_of(p.branch.begin(), p.branch.end(), [&](const Device& b) { return b.id == id; })) {
                    if (playhead)
                        *playhead = p.id;
                    found = &p.branch;
                }
        }
    });
    return found;
}

int deviceCount(const std::vector<Device>& chain) {
    int n = int(chain.size());
    for (const Device& d : chain)
        for (const Playhead& p : d.playheads)
            n += int(p.branch.size());
    return n;
}

const Device* activePlayheads(const std::vector<Device>& chain) {
    for (const Device& d : chain)
        if (d.type == DeviceType::Playheads)
            return d.on ? &d : nullptr;
    return nullptr;
}

std::vector<Device>* devicesOf(Song& song, ChainOwner owner) {
    return const_cast<std::vector<Device>*>(devicesOf(static_cast<const Song&>(song), owner));
}
const std::vector<Device>* devicesOf(const Song& song, ChainOwner owner) {
    if (owner.isClip()) {
        const Clip* c = song.findClip(owner.clip);
        return c ? &c->devices : nullptr;
    }
    const Track* t = song.findTrack(owner.track);
    return t ? &t->devices : nullptr;
}
std::vector<Modulator>* modulatorsOf(Song& song, ChainOwner owner) {
    return const_cast<std::vector<Modulator>*>(modulatorsOf(static_cast<const Song&>(song), owner));
}
const std::vector<Modulator>* modulatorsOf(const Song& song, ChainOwner owner) {
    if (owner.isClip()) {
        const Clip* c = song.findClip(owner.clip);
        return c ? &c->modulators : nullptr;
    }
    const Track* t = song.findTrack(owner.track);
    return t ? &t->modulators : nullptr;
}

ChainOwner ownerOf(const Song& song, DeviceId id) {
    ChainOwner owner;
    findDeviceIn<const Song, const Device>(song, id, &owner);
    return owner;
}

ChainOwner ownerOf(const Song& song, const ParamRef& ref) {
    if (ref.kind == ParamRef::Kind::Device)
        return ownerOf(song, DeviceId(ref.owner));
    if (ref.kind == ParamRef::Kind::Playhead) {
        ChainOwner owner;
        findPlayheadIn<const Song, const Playhead>(song, PlayheadId(ref.owner), &owner, nullptr);
        return owner;
    }
    ModulatorPlace place;
    if (!findModulator(song, ModulatorId(ref.owner), &place))
        return ChainOwner();
    switch (place.level) {
    case ModLevel::Track:
        return ChainOwner(place.track);
    case ModLevel::Clip:
        return ChainOwner(place.clip);
    case ModLevel::Device:
        return place.clip.valid() ? ChainOwner(place.clip) : ChainOwner(place.track);
    case ModLevel::Song:
    default:
        return ChainOwner();
    }
}

TrackId trackUsing(const Song& song, ClipId clip) {
    for (const Track& t : song.tracks) {
        for (const ClipSlot& s : t.slots)
            if (s.clip == clip)
                return t.id;
        for (const ArrangerClip& a : t.arrangement)
            if (a.clip == clip)
                return t.id;
    }
    return TrackId();
}

Device* findDevice(Song& song, DeviceId id, TrackId* track) {
    return const_cast<Device*>(findDevice(static_cast<const Song&>(song), id, track));
}
const Device* findDevice(const Song& song, DeviceId id, TrackId* track) {
    ChainOwner owner;
    const Device* d = findDeviceIn<const Song, const Device>(song, id, &owner);
    if (d && track)
        *track = trackOfOwner(song, owner);
    return d;
}
Modulator* findModulator(Song& song, ModulatorId id, ModulatorPlace* place) {
    return const_cast<Modulator*>(findModulator(static_cast<const Song&>(song), id, place));
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
    } else if (ref.kind == ParamRef::Kind::Playhead) {
        if (const Playhead* p = findPlayhead(song, PlayheadId(ref.owner)))
            params = &p->params;
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
    if (ref.kind == ParamRef::Kind::Playhead) {
        findPlayhead(song, PlayheadId(ref.owner), &track);
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
    // A set (the Note Filter's channels) is only ever picked by hand.
    if (m.target.kind == ParamRef::Kind::Device)
        if (const Device* d = findDevice(song, DeviceId(m.target.owner)))
            if (const devices::ParamInfo* info = devices::paramInfo(d->type, m.target.param); info && !info->movable())
                return false;
    if (m.target.kind == ParamRef::Kind::Modulator && m.target.owner == self.value)
        return false; // its own settings would feed back on themselves
    switch (place.level) {
    case ModLevel::Song:
        return true;
    case ModLevel::Track:
        return trackOf(song, m.target) == place.track;
    case ModLevel::Clip: {
        ChainOwner owner = ownerOf(song, m.target);
        return owner.isClip() && owner.clip == place.clip;
    }
    case ModLevel::Device: {
        if (m.target.kind == ParamRef::Kind::Device)
            return m.target.owner == place.device.value;
        if (m.target.kind == ParamRef::Kind::Playhead) {
            DeviceId device;
            findPlayhead(song, PlayheadId(m.target.owner), nullptr, &device);
            return device == place.device;
        }
        ModulatorPlace target;
        findModulator(song, ModulatorId(m.target.owner), &target);
        return target.level == ModLevel::Device && target.device == place.device;
    }
    }
    return false;
}

void forEachModulator(Song& song, const std::function<void(Modulator&, const ModulatorPlace&)>& f) {
    for (Modulator& m : song.modulators)
        f(m, {ModLevel::Song, TrackId(), DeviceId(), ClipId()});
    for (Track& t : song.tracks) {
        for (Modulator& m : t.modulators)
            f(m, {ModLevel::Track, t.id, DeviceId(), ClipId()});
        for (Device& d : t.devices)
            for (Modulator& m : d.modulators)
                f(m, {ModLevel::Device, t.id, d.id, ClipId()});
    }
    for (auto& [id, c] : song.clips) {
        if (c.modulators.empty() && c.devices.empty())
            continue;
        TrackId track = trackUsing(song, id);
        for (Modulator& m : c.modulators)
            f(m, {ModLevel::Clip, track, DeviceId(), id});
        for (Device& d : c.devices)
            for (Modulator& m : d.modulators)
                f(m, {ModLevel::Device, track, d.id, id});
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
