#include "DeviceCommands.hpp"

#include "DeviceTypes.hpp"
#include "Devices.hpp"
#include "Modulators.hpp"
#include "Playheads.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace tessera {

// ---- ReplaceChains ------------------------------------------------------------------------------------------

ChangeSet ReplaceChains::changes() const {
    if (before.songModulators || after.songModulators)
        return ChangeSet::all();
    ChangeSet c;
    for (const ChainParts::TrackChain& t : after.tracks)
        c.tracks.push_back(t.track);
    for (const ChainParts::ClipChain& k : after.clips)
        c.clips.push_back(k.clip);
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
    for (const ChainParts::ClipChain& k : parts.clips) {
        Clip* clip = song.findClip(k.clip);
        assert(clip && "ReplaceChains on a missing clip");
        if (!clip)
            continue;
        clip->hasOwnChain = k.hasOwn;
        clip->playsOwnChain = k.playsOwn;
        clip->devices = k.devices;
        clip->modulators = k.modulators;
    }
}

namespace {

/// The parts of a song chain edits work on. Of each clip only its own chain comes along: notes can be many and no
/// chain edit reads them.
Song chainsOf(const Song& song) {
    Song work;
    work.timeSig = song.timeSig;
    work.tracks = song.tracks;
    work.modulators = song.modulators;
    work.nextId = song.nextId;
    for (const auto& [id, c] : song.clips) {
        Clip& k = work.clips[id];
        k.id = id;
        k.audio = c.audio ? std::optional<AudioContent>(AudioContent()) : std::nullopt;
        k.hasOwnChain = c.hasOwnChain;
        k.playsOwnChain = c.playsOwnChain;
        k.devices = c.devices;
        k.modulators = c.modulators;
    }
    return work;
}

/// A clip's chain as a part of a ChainParts.
ChainParts::ClipChain clipChainOf(const Clip& c) {
    return {c.id, c.hasOwnChain, c.playsOwnChain, c.devices, c.modulators};
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
    for (const auto& [id, b] : work.clips) {
        const Clip* a = song.findClip(id);
        assert(a && "chain edits never add clips");
        if (!a || clipChainOf(*a) == clipChainOf(b))
            continue;
        before.clips.push_back(clipChainOf(*a));
        after.clips.push_back(clipChainOf(b));
    }
    if (!before.songModulators && before.tracks.empty() && before.clips.empty())
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
    case ModLevel::Clip: {
        Clip* c = song.findClip(place.clip);
        return c && !c->isAudio() ? &c->modulators : nullptr;
    }
    case ModLevel::Device: {
        Device* d = findDevice(song, place.device);
        return d && ownerOf(song, place.device).clip == place.clip ? &d->modulators : nullptr;
    }
    }
    return nullptr;
}

size_t limitAt(ModLevel level) {
    switch (level) {
    case ModLevel::Song:
        return kMaxSongModulators;
    case ModLevel::Track:
    case ModLevel::Clip:
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

/// A name without the number freeName() adds ("LFO 2" -> "LFO"), so a pasted copy is numbered afresh.
std::string baseName(const std::string& name) {
    size_t space = name.find_last_of(' ');
    if (space == std::string::npos || space == 0 || space + 1 == name.size())
        return name;
    for (size_t i = space + 1; i < name.size(); i++)
        if (name[i] < '0' || name[i] > '9')
            return name;
    return name.substr(0, space);
}

std::vector<Device>::iterator deviceIn(std::vector<Device>& chain, DeviceId id) {
    return std::find_if(chain.begin(), chain.end(), [&](const Device& d) { return d.id == id; });
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

/// How many Motion devices lead the chain: the only devices allowed before Playheads.
int leadingMotions(const std::vector<Device>& chain) {
    int n = 0;
    while (n < int(chain.size()) && chain[size_t(n)].type == DeviceType::Motion)
        n++;
    return n;
}

/// The first chain position other devices may take: after the Playheads device, which only Motion devices precede.
int firstFree(const std::vector<Device>& chain) {
    for (size_t i = 0; i < chain.size(); i++)
        if (chain[i].type == DeviceType::Playheads)
            return int(i) + 1;
    return 0;
}

bool hasPlayheads(const std::vector<Device>& chain) {
    return std::any_of(chain.begin(), chain.end(), [](const Device& d) { return d.type == DeviceType::Playheads; });
}

/// Puts a device into a chain at `at` (clamped): a Playheads device goes first but for leading Motion devices,
/// others after it; a Motion device may go anywhere.
void insertDevice(std::vector<Device>& chain, int at, Device device) {
    if (device.type == DeviceType::Playheads)
        at = leadingMotions(chain);
    else if (device.type == DeviceType::Motion)
        at = std::clamp(at, 0, int(chain.size()));
    else
        at = std::clamp(at, firstFree(chain), int(chain.size()));
    chain.insert(chain.begin() + at, std::move(device));
}

/// The clips a chain plays: the clip itself, or every clip in the track's slots and on its timeline.
std::vector<const Clip*> clipsPlayedBy(const Song& song, ChainOwner owner) {
    std::vector<const Clip*> clips;
    if (owner.isClip()) {
        if (const Clip* c = song.findClip(owner.clip))
            clips.push_back(c);
        return clips;
    }
    const Track* t = song.findTrack(owner.track);
    if (!t)
        return clips;
    for (const ClipSlot& s : t->slots)
        if (const Clip* c = s.clip ? song.findClip(*s.clip) : nullptr)
            clips.push_back(c);
    for (const ArrangerClip& a : t->arrangement)
        if (const Clip* c = song.findClip(a.clip))
            clips.push_back(c);
    return clips;
}

/// The playhead the device gets next: named and coloured by its place, on the lowest channel nobody uses. It starts
/// where its segment starts, so where it reads is set by the segment alone.
Playhead nextPlayhead(const Device& device, PlayheadId id) {
    Playhead p = devices::makePlayhead(id, int(device.playheads.size()));
    int channel = 1;
    while (channel < 16 && std::any_of(
                               device.playheads.begin(), device.playheads.end(),
                               [&](const Playhead& o) { return o.channel == channel; }))
        channel++;
    p.channel = uint8_t(channel);
    return p;
}

TrackId trackOfPlayhead(const Song& song, PlayheadId id) {
    TrackId track;
    findPlayhead(song, id, &track);
    return track;
}

std::string playheadName(const Song& song, PlayheadId id) {
    const Playhead* p = findPlayhead(song, id);
    return p ? p->name : "playhead";
}

/// Runs `edit` on a playhead and the device holding it.
template <typename Edit>
CommandPtr playheadEdit(const Song& song, PlayheadId id, const std::string& name, Edit&& edit) {
    return chainEdit(song, name, [&](Song& work) {
        DeviceId deviceId;
        Playhead* p = findPlayhead(work, id, nullptr, &deviceId);
        return p && edit(*findDevice(work, deviceId), *p);
    });
}

std::string modulatorName(const Song& song, ModulatorId id) {
    const Modulator* m = findModulator(song, id);
    return m ? m->name : "modulator";
}

} // namespace

// ---- Devices ------------------------------------------------------------------------------------------------

CommandPtr addDevice(const Song& song, ChainOwner owner, DeviceType type, int index, const NextId& nextId,
                     DeviceId* added) {
    const std::vector<Device>* existing = devicesOf(song, owner);
    if (!existing || deviceCount(*existing) >= kMaxDevices)
        return nullptr;
    if (owner.isClip() && song.findClip(owner.clip)->isAudio())
        return nullptr;
    if (type == DeviceType::Playheads && hasPlayheads(*existing))
        return nullptr;
    Device device = devices::makeDevice(type, DeviceId(nextId()));
    if (type == DeviceType::Groove) {
        // Its grid suits the notes it plays: a pattern of 1/8s wouldn't move at all on 1/16s.
        std::vector<const Clip*> clips = clipsPlayedBy(song, owner);
        const devices::ParamInfo& grid = devices::deviceInfo(type).params[size_t(chain::knob::kGrooveGrid)];
        device.params[size_t(chain::knob::kGrooveGrid)] =
            devices::normalize(grid, swingStepFor(clips) == kPpq / 2 ? 1.f : 0.f);
    }
    if (added)
        *added = device.id;
    if (type == DeviceType::Playheads)
        for (int i = 0; i < 2; i++)
            device.playheads.push_back(nextPlayhead(device, PlayheadId(nextId())));
    if (type == DeviceType::Motion) {
        // "Motion 1", "Motion 2"…, each in a colour no playhead or other Motion on the track wears.
        std::vector<int> used;
        int count = 0;
        for (const Device& d : *existing) {
            if (d.type == DeviceType::Motion) {
                used.push_back(d.color);
                count++;
            }
            for (const Playhead& p : d.playheads)
                used.push_back(p.color);
        }
        device.name = "Motion " + std::to_string(count + 1);
        // From orange on: the first colours are the playheads' own.
        for (int c = 5; c < 13; c++)
            if (std::find(used.begin(), used.end(), c % 8) == used.end()) {
                device.color = uint8_t(c % 8);
                break;
            }
        // A new one moves the notes the playheads read: before them, after any Motion already there.
        if (index < 0)
            index = leadingMotions(*existing);
    }
    return chainEdit(song, "Add " + device.name, [&](Song& work) {
        std::vector<Device>& chain = *devicesOf(work, owner);
        insertDevice(chain, index < 0 ? int(chain.size()) : index, device);
        return true;
    });
}

namespace {

/// A clip whose own chain an edit emptied (no device, no modulator left) plays through its track's devices again,
/// instead of playing dry with a chain that has nothing in it (docs/DECISIONS.md §189). Clips that were already empty
/// before the edit are left alone.
void dropEmptiedChains(const Song& before, Song& work) {
    for (auto& [id, clip] : work.clips) {
        const Clip* was = before.findClip(id);
        if (clip.hasOwnChain && clip.devices.empty() && clip.modulators.empty() && was &&
            (!was->devices.empty() || !was->modulators.empty()))
            clip.hasOwnChain = clip.playsOwnChain = false;
    }
}

} // namespace

CommandPtr removeDevice(const Song& song, DeviceId id) {
    if (!findDevice(song, id))
        return nullptr;
    return chainEdit(song, "Remove " + deviceName(song, id), [&](Song& work) {
        std::vector<Device>& chain = *chainOf(work, id);
        chain.erase(std::find_if(chain.begin(), chain.end(), [&](const Device& d) { return d.id == id; }));
        pruneRoutes(work);
        dropEmptiedChains(song, work);
        return true;
    });
}

CommandPtr moveDevice(const Song& song, DeviceId id, ChainOwner to, int index) {
    ChainOwner from = ownerOf(song, id);
    if (!from.valid())
        return nullptr;
    const std::vector<Device>* target = devicesOf(song, to);
    if (!target || (to != from && deviceCount(*target) >= kMaxDevices))
        return nullptr;
    if (to.isClip() && song.findClip(to.clip)->isAudio())
        return nullptr;
    PlayheadId inBranch;
    chainOf(const_cast<Song&>(song), id, &inBranch);
    if (inBranch.valid())
        return nullptr; // branch devices stay in their branch
    if (findDevice(song, id)->type == DeviceType::Playheads && to != from && hasPlayheads(*target))
        return nullptr;
    return chainEdit(song, "Move " + deviceName(song, id), [&](Song& work) {
        std::vector<Device>& fromChain = *devicesOf(work, from);
        auto it = deviceIn(fromChain, id);
        int oldIndex = int(it - fromChain.begin());
        Device device = std::move(*it);
        fromChain.erase(it);
        std::vector<Device>& toChain = *devicesOf(work, to);
        int at = index < 0 ? int(toChain.size()) : index;
        // `index` counts positions in the chain as it was, so moving right past itself lands one earlier.
        if (to == from && at > oldIndex)
            at--;
        insertDevice(toChain, at, std::move(device));
        pruneRoutes(work);
        dropEmptiedChains(song, work);
        return true;
    });
}

CommandPtr duplicateDevice(const Song& song, DeviceId id, const NextId& nextId) {
    TrackId trackId;
    const Device* original = findDevice(song, id, &trackId);
    if (!original)
        return nullptr;
    ChainOwner owner = ownerOf(song, id);
    PlayheadId inBranch;
    chainOf(const_cast<Song&>(song), id, &inBranch);
    if (inBranch.valid()) {
        const Playhead* p = findPlayhead(song, inBranch);
        if (int(p->branch.size()) >= kMaxBranchDevices || deviceCount(*devicesOf(song, owner)) >= kMaxDevices)
            return nullptr;
        Device copy = *original;
        copy.id = DeviceId(nextId());
        return chainEdit(song, "Duplicate " + original->name, [&](Song& work) {
            std::vector<Device>& branch = *chainOf(work, id);
            auto at = std::find_if(branch.begin(), branch.end(), [&](const Device& d) { return d.id == id; });
            branch.insert(at + 1, copy);
            return true;
        });
    }
    const std::vector<Device>& chain = *devicesOf(song, owner);
    int at = int(std::find_if(chain.begin(), chain.end(), [&](const Device& d) { return d.id == id; }) - chain.begin());
    CommandPtr command = copyDevice(song, id, owner, at + 1, nextId);
    return command ? std::make_unique<ReplaceChains>("Duplicate " + original->name,
                                                     static_cast<ReplaceChains&>(*command).partsBefore(),
                                                     static_cast<ReplaceChains&>(*command).partsAfter())
                   : nullptr;
}

CommandPtr copyDevice(const Song& song, DeviceId id, ChainOwner to, int index, const NextId& nextId) {
    const Device* original = findDevice(song, id);
    const std::vector<Device>* target = devicesOf(song, to);
    if (!original || !target || (to.isClip() && song.findClip(to.clip)->isAudio()))
        return nullptr;
    int adds = 1;
    for (const Playhead& p : original->playheads)
        adds += int(p.branch.size());
    if (deviceCount(*target) + adds > kMaxDevices)
        return nullptr;
    if (original->type == DeviceType::Playheads && hasPlayheads(*target))
        return nullptr;
    std::vector<Device> copy = {*original};
    std::vector<Modulator> noTrackModulators;
    ChainRemap remap;
    renewIds(copy, noTrackModulators, nextId, remap);
    retargetRoutes(copy, noTrackModulators, remap);
    return chainEdit(song, "Copy " + original->name, [&](Song& work) {
        std::vector<Device>& chain = *devicesOf(work, to);
        insertDevice(chain, index < 0 ? int(chain.size()) : index, std::move(copy.front()));
        pruneRoutes(work);
        return true;
    });
}

CommandPtr pasteChainItems(const Song& song, ChainOwner to, int index, std::vector<Device> devices,
                           std::vector<Modulator> modulators, std::vector<Modulator> songModulators,
                           const NextId& nextId, int* dropped, std::optional<ModulatorPlace> into) {
    const std::vector<Device>* target = devicesOf(song, to);
    if (!target || (to.isClip() && song.findClip(to.clip)->isAudio()))
        return nullptr;
    if (into) {
        // A device's section is found through the chain it is in: a track's, or a clip's own.
        if (into->level == ModLevel::Device)
            into->clip = ownerOf(song, into->device).clip;
        Song probe = chainsOf(song);
        if (!modulatorsAt(probe, *into))
            return nullptr;
    }
    // The devices and their section's modulators renew together, so moves from one to the other follow the copies.
    ChainRemap remap, songRemap;
    renewIds(devices, modulators, nextId, remap);
    retargetRoutes(devices, modulators, remap);
    std::vector<Device> none;
    renewIds(none, songModulators, nextId, songRemap);
    retargetRoutes(none, songModulators, songRemap);
    auto countRoutes = [&](const Song& s) {
        int n = 0;
        forEachModulator(s, [&](const Modulator& m, const ModulatorPlace&) { n += int(m.routes.size()); });
        return n;
    };
    int before = countRoutes(song);
    bool any = false;
    CommandPtr command = chainEdit(song, "Paste", [&](Song& work) {
        std::vector<Device>& chain = *devicesOf(work, to);
        int at = index < 0 ? int(chain.size()) : std::min(index, int(chain.size()));
        for (Device& d : devices) {
            int adds = 1;
            for (const Playhead& p : d.playheads)
                adds += int(p.branch.size());
            if (deviceCount(chain) + adds > kMaxDevices || (d.type == DeviceType::Playheads && hasPlayheads(chain)))
                continue;
            insertDevice(chain, at++, std::move(d));
            any = true;
        }
        ModulatorPlace chainPlace{to.isClip() ? ModLevel::Clip : ModLevel::Track, to.track, DeviceId(), to.clip};
        ModulatorPlace songPlace{ModLevel::Song, TrackId(), DeviceId(), ClipId()};
        auto put = [&](std::vector<Modulator>& mods, const ModulatorPlace& place) {
            std::vector<Modulator>* section = modulatorsAt(work, place);
            for (Modulator& m : mods)
                if (section && section->size() < limitAt(place.level)) {
                    m.name = freeName(*section, baseName(m.name));
                    section->push_back(std::move(m));
                    any = true;
                }
        };
        put(modulators, into ? *into : chainPlace);
        put(songModulators, into ? *into : songPlace);
        int pasted = countRoutes(work) - before;
        int pruned = pruneRoutes(work);
        if (dropped)
            *dropped = std::min(pruned, std::max(0, pasted));
        return any;
    });
    return command;
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

CommandPtr setDeviceVariation(const Song& song, DeviceId id, int32_t variation) {
    variation = clampVariation(variation);
    return chainEdit(song, variation < 0 ? "New every play" : "Variation " + std::to_string(variation),
                     [&](Song& work) {
                         Device* d = findDevice(work, id);
                         if (!d || d->variation == variation)
                             return false;
                         d->variation = variation;
                         return true;
                     });
}

CommandPtr setArpLine(const Song& song, DeviceId id, std::vector<uint8_t> line, int32_t variation,
                      const std::string& name) {
    if (line.size() > 16)
        line.resize(16);
    variation = clampVariation(variation);
    return chainEdit(song, name, [&](Song& work) {
        Device* d = findDevice(work, id);
        if (!d || d->type != DeviceType::Arpeggiator || (d->line == line && d->variation == variation))
            return false;
        d->line = line;
        d->variation = variation;
        return true;
    });
}

CommandPtr setArpPattern(const Song& song, DeviceId id, std::vector<uint8_t> line, int32_t variation,
                         const std::vector<std::pair<int, float>>& settings, const std::string& name) {
    if (line.size() > 16)
        line.resize(16);
    variation = clampVariation(variation);
    const auto& info = devices::deviceInfo(DeviceType::Arpeggiator).params;
    return chainEdit(song, name, [&](Song& work) {
        Device* d = findDevice(work, id);
        if (!d || d->type != DeviceType::Arpeggiator)
            return false;
        bool changed = d->line != line || d->variation != variation;
        d->line = line;
        d->variation = variation;
        while (d->params.size() < info.size())
            d->params.push_back(devices::defaultNormalized(info[d->params.size()]));
        for (const auto& [k, value] : settings) {
            if (k < 0 || size_t(k) >= info.size())
                continue;
            float v = devices::normalize(info[size_t(k)], value);
            changed |= d->params[size_t(k)] != v;
            d->params[size_t(k)] = v;
        }
        return changed;
    });
}

CommandPtr setModulatorVariation(const Song& song, ModulatorId id, int32_t variation) {
    variation = clampVariation(variation);
    return chainEdit(song, variation < 0 ? "New every play" : "Variation " + std::to_string(variation),
                     [&](Song& work) {
                         Modulator* m = findModulator(work, id);
                         if (!m || m->variation == variation)
                             return false;
                         m->variation = variation;
                         return true;
                     });
}

namespace {

/// Fixes a "New every play" switch to what roll `roll` plays it on. True when it changed.
bool fix(int32_t& variation, uint32_t roll, uint64_t id) {
    if (variation >= 1)
        return false;
    variation = int32_t(rolledVariation(roll, id));
    return true;
}

/// Fixes the switches of a chain's devices (their playheads' own devices too) and their modulators.
bool fixChain(std::vector<Device>& devices, uint32_t roll) {
    bool any = false;
    for (Device& d : devices) {
        any |= fix(d.variation, roll, d.id.value);
        for (Modulator& m : d.modulators)
            any |= fix(m.variation, roll, m.id.value);
        for (Playhead& p : d.playheads)
            any |= fixChain(p.branch, roll);
    }
    return any;
}

/// Fixes a clip's chance and its own chain.
bool fixClip(Clip& c, uint32_t roll) {
    bool any = fix(c.variation, roll, c.id.value);
    any |= fixChain(c.devices, roll);
    for (Modulator& m : c.modulators)
        any |= fix(m.variation, roll, m.id.value);
    return any;
}

} // namespace

Song songRolled(const Song& song, uint32_t roll) {
    Song out = song;
    for (Modulator& m : out.modulators)
        fix(m.variation, roll, m.id.value);
    for (Track& t : out.tracks) {
        fixChain(t.devices, roll);
        for (Modulator& m : t.modulators)
            fix(m.variation, roll, m.id.value);
    }
    for (auto& [id, c] : out.clips)
        fixClip(c, roll);
    return out;
}

CommandPtr keepRoll(const Song& song, TrackId track, uint32_t roll) {
    const Track* t = song.findTrack(track);
    if (!t)
        return nullptr;
    std::vector<ClipId> clips;
    for (const ClipSlot& slot : t->slots)
        if (slot.clip)
            clips.push_back(*slot.clip);
    for (const ArrangerClip& a : t->arrangement)
        clips.push_back(a.clip);
    auto all = std::make_unique<CompoundCommand>("Keep this variation");
    Song work = song;
    if (CommandPtr chains = chainEdit(work, "Keep this variation", [&](Song& w) {
            bool any = false;
            for (Modulator& m : w.modulators)
                any |= fix(m.variation, roll, m.id.value);
            Track* wt = w.findTrack(track);
            any |= fixChain(wt->devices, roll);
            for (Modulator& m : wt->modulators)
                any |= fix(m.variation, roll, m.id.value);
            for (ClipId id : clips)
                if (Clip* c = w.findClip(id)) {
                    any |= fixChain(c->devices, roll);
                    for (Modulator& m : c->modulators)
                        any |= fix(m.variation, roll, m.id.value);
                }
            return any;
        })) {
        chains->apply(work);
        all->add(std::move(chains));
    }
    std::sort(clips.begin(), clips.end());
    clips.erase(std::unique(clips.begin(), clips.end()), clips.end());
    for (ClipId id : clips) {
        const Clip* c = work.findClip(id);
        if (!c || c->variation >= 1)
            continue;
        if (CommandPtr chance = setClipField(work, id, &Clip::variation, int32_t(rolledVariation(roll, id.value)),
                                             "Keep this variation")) {
            chance->apply(work);
            all->add(std::move(chance));
        }
    }
    return all->empty() ? nullptr : std::move(all);
}

CommandPtr setNotesMotion(const Song& song, ClipId clip, const std::vector<NoteId>& notes, DeviceId motion) {
    const Clip* c = song.findClip(clip);
    const Device* d = motion.valid() ? findDevice(song, motion) : nullptr;
    if (!c || (motion.valid() && (!d || d->type != DeviceType::Motion)))
        return nullptr;
    std::vector<Note> after = c->notes;
    for (Note& n : after)
        if (std::find(notes.begin(), notes.end(), n.id) != notes.end())
            n.motion = motion;
    return replaceNotes(d ? "Move notes with " + d->name : "Stop notes moving", clip, c->notes, after);
}

CommandPtr setParam(const Song& song, const ParamRef& ref, float value) {
    std::string label = "Set ";
    if (ref.kind == ParamRef::Kind::Device) {
        if (const Device* d = findDevice(song, DeviceId(ref.owner)))
            if (const devices::ParamInfo* p = devices::paramInfo(d->type, ref.param))
                label += d->name + " " + p->name;
    } else if (ref.kind == ParamRef::Kind::Playhead) {
        if (const Playhead* ph = findPlayhead(song, PlayheadId(ref.owner)))
            if (const devices::ParamInfo* p = devices::playheadParamInfo(ref.param))
                label += ph->name + " " + p->name;
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

// ---- Playheads ----------------------------------------------------------------------------------------------

CommandPtr addPlayhead(const Song& song, DeviceId deviceId, const NextId& nextId, PlayheadId* added) {
    const Device* device = findDevice(song, deviceId);
    if (!device || device->type != DeviceType::Playheads || int(device->playheads.size()) >= playheads::kMaxPlayheads)
        return nullptr;
    Playhead p = nextPlayhead(*device, PlayheadId(nextId()));
    if (added)
        *added = p.id;
    return chainEdit(song, "Add " + p.name, [&](Song& work) {
        findDevice(work, deviceId)->playheads.push_back(p);
        return true;
    });
}

CommandPtr addBranchDevice(const Song& song, PlayheadId id, DeviceType type, int index, const NextId& nextId) {
    const Playhead* p = findPlayhead(song, id);
    const std::vector<Device>* chain = devicesOf(song, ownerOf(song, ParamRef{ParamRef::Kind::Playhead, id.value, 0}));
    if (!p || !chain || type == DeviceType::Playheads || int(p->branch.size()) >= kMaxBranchDevices ||
        deviceCount(*chain) >= kMaxDevices)
        return nullptr;
    Device device = devices::makeDevice(type, DeviceId(nextId()));
    return playheadEdit(song, id, "Add " + device.name + " to " + p->name, [&](Device&, Playhead& ph) {
        int at = index < 0 ? int(ph.branch.size()) : std::min(index, int(ph.branch.size()));
        ph.branch.insert(ph.branch.begin() + at, device);
        return true;
    });
}

CommandPtr removePlayhead(const Song& song, PlayheadId id) {
    return playheadEdit(song, id, "Remove " + playheadName(song, id), [&](Device& d, Playhead&) {
        d.playheads.erase(
            std::find_if(d.playheads.begin(), d.playheads.end(), [&](const Playhead& p) { return p.id == id; }));
        return true;
    });
}

CommandPtr duplicatePlayhead(const Song& song, PlayheadId id, const NextId& nextId, PlayheadId* added) {
    const Device* device = nullptr;
    DeviceId deviceId;
    if (findPlayhead(song, id, nullptr, &deviceId))
        device = findDevice(song, deviceId);
    if (!device || int(device->playheads.size()) >= playheads::kMaxPlayheads)
        return nullptr;
    const Playhead* source = findPlayhead(song, id);
    if (deviceCount(*song.findTrack(trackOfPlayhead(song, id))) + int(source->branch.size()) > kMaxDevices)
        return nullptr;
    PlayheadId fresh(nextId());
    std::vector<DeviceId> branchIds;
    for (size_t k = 0; k < source->branch.size(); k++)
        branchIds.push_back(DeviceId(nextId()));
    if (added)
        *added = fresh;
    return playheadEdit(song, id, "Duplicate " + playheadName(song, id), [&](Device& d, Playhead& original) {
        Playhead copy = original;
        copy.id = fresh;
        for (size_t k = 0; k < copy.branch.size(); k++)
            copy.branch[k].id = branchIds[k];
        copy.name = original.name + " copy";
        copy.color = uint8_t(d.playheads.size() % 16);
        auto at = std::find_if(d.playheads.begin(), d.playheads.end(), [&](const Playhead& p) { return p.id == id; });
        d.playheads.insert(at + 1, copy);
        return true;
    });
}

CommandPtr renamePlayhead(const Song& song, PlayheadId id, const std::string& name) {
    if (name.empty())
        return nullptr;
    return playheadEdit(song, id, "Rename " + playheadName(song, id), [&](Device&, Playhead& p) {
        p.name = name;
        return true;
    });
}

CommandPtr setPlayheadChannel(const Song& song, PlayheadId id, int channel) {
    return playheadEdit(song, id, "Set " + playheadName(song, id) + " channel", [&](Device&, Playhead& p) {
        p.channel = uint8_t(std::clamp(channel, 0, 16));
        return true;
    });
}

CommandPtr setPlayheadOn(const Song& song, PlayheadId id, bool on) {
    return playheadEdit(song, id, std::string("Turn ") + playheadName(song, id) + (on ? " on" : " off"),
                        [&](Device&, Playhead& p) {
                            if (p.params.size() <= size_t(playheads::knob::kOn) || playheads::isOn(p) == on)
                                return false;
                            p.params[size_t(playheads::knob::kOn)] = on ? 1.f : 0.f;
                            return true;
                        });
}

CommandPtr setPlayheadPath(const Song& song, PlayheadId id, std::vector<AutoPoint> path, const std::string& name) {
    std::stable_sort(path.begin(), path.end(), [](const AutoPoint& a, const AutoPoint& b) { return a.t < b.t; });
    for (AutoPoint& p : path) {
        p.value = std::clamp(p.value, 0.f, 1.f);
        p.t = std::max<Tick>(0, p.t);
    }
    return playheadEdit(song, id, name, [&](Device&, Playhead& p) {
        if (p.path == path)
            return false;
        p.path = path;
        return true;
    });
}

CommandPtr setPlayheadSolo(const Song& song, PlayheadId id, bool solo) {
    return playheadEdit(song, id, (solo ? "Solo " : "Unsolo ") + playheadName(song, id), [&](Device&, Playhead& p) {
        p.solo = solo;
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
        dropEmptiedChains(song, work);
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

void tidyCurve(CurveDrawing& d) {
    d.length = std::clamp<Tick>(d.length, kPpq / 4, kMaxCurveLength);
    std::stable_sort(d.points.begin(), d.points.end(),
                     [](const AutoPoint& a, const AutoPoint& b) { return a.t < b.t; });
    for (AutoPoint& p : d.points) {
        p.value = std::isfinite(p.value) ? std::clamp(p.value, 0.f, 1.f) : 0.5f;
        p.t = std::clamp<Tick>(p.t, 0, kMaxCurveLength);
    }
    // A segment past the end slides back inside, keeping its length unless the drawing is shorter.
    Tick span = std::clamp<Tick>(d.segmentEnd - d.segmentStart, kPpq / 4, d.length);
    d.segmentStart = std::clamp<Tick>(d.segmentStart, 0, d.length - span);
    d.segmentEnd = d.segmentStart + span;
}

CommandPtr setCurve(const Song& song, ModulatorId id, CurveDrawing drawing) {
    tidyCurve(drawing);
    return chainEdit(song, "Draw " + modulatorName(song, id), [&](Song& work) {
        Modulator* m = findModulator(work, id);
        if (!m || m->type != ModulatorType::Curve ||
            (m->length == drawing.length && m->points == drawing.points && m->segmentStart == drawing.segmentStart &&
             m->segmentEnd == drawing.segmentEnd))
            return false;
        m->length = drawing.length;
        m->points = drawing.points;
        m->segmentStart = drawing.segmentStart;
        m->segmentEnd = drawing.segmentEnd;
        return true;
    });
}

CommandPtr animateWithCurve(const Song& song, ChainOwner owner, const ParamRef& target, const std::string& name,
                            CurveDrawing drawing, const NextId& nextId, ModulatorId* added) {
    tidyCurve(drawing);
    const std::vector<Modulator>* existing = modulatorsOf(song, owner);
    if (!existing || existing->size() >= size_t(kMaxTrackModulators))
        return nullptr;
    ModulatorId id(nextId());
    Modulation route;
    route.kind = Modulation::Kind::Param;
    route.target = target;
    route.amount = 1;
    ModulatorPlace place;
    place.level = owner.isClip() ? ModLevel::Clip : ModLevel::Track;
    place.track = owner.isClip() ? trackUsing(song, owner.clip) : owner.track;
    place.clip = owner.clip;
    if (!reachable(song, place, id, route))
        return nullptr;
    if (added)
        *added = id;
    return chainEdit(song, "Animate " + name, [&](Song& work) {
        std::vector<Modulator>* list = modulatorsOf(work, owner);
        if (!list)
            return false;
        Modulator m = devices::makeModulator(ModulatorType::Curve, id);
        m.name = freeName(*list, name);
        m.length = drawing.length;
        m.points = drawing.points;
        m.segmentStart = drawing.segmentStart;
        m.segmentEnd = drawing.segmentEnd;
        m.routes.push_back(route);
        list->push_back(m);
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
        case ModLevel::Clip:
            return "A clip's modulator only reaches the clip's own devices";
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
        Modulator* m = findModulator(work, id);
        m->routes.push_back(r);
        if (mods::drawnRoute(*m) == int(m->routes.size()) - 1)
            m->routes.back().amount = 1; // a Curve is drawn in the setting it reaches first
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
        const Modulator* m = findModulator(work, id);
        if (!r || !m || mods::drawnRoute(*m) == index)
            return false;
        r->amount = std::clamp(amount, -1.f, 1.f);
        return true;
    });
}

// ---- A clip's own chain --------------------------------------------------------------------------------------

namespace {

/// Points the notes that move with a copied Motion device at the copy.
void retargetMotions(std::vector<Note>& notes, const ChainRemap& remap) {
    for (Note& n : notes)
        if (n.motion.valid()) {
            auto it = remap.find(n.motion.value);
            if (it != remap.end())
                n.motion = DeviceId(it->second);
        }
}

/// Track modulators that move something in `chain` (by device, playhead or device modulator IDs), copied for a clip's
/// own chain: with only those routes, renewed and pointed at the copies through `remap`.
std::vector<Modulator> modulatorsFor(const std::vector<Modulator>& trackMods, const ChainRemap& remap,
                                     const NextId& nextId, ChainRemap& renewed) {
    std::vector<Modulator> out;
    for (const Modulator& m : trackMods) {
        Modulator copy = m;
        copy.routes.clear();
        for (const Modulation& r : m.routes)
            if (r.kind == Modulation::Kind::Param && remap.count(r.target.owner)) {
                Modulation moved = r;
                retarget(moved.target, remap);
                copy.routes.push_back(moved);
            }
        if (copy.routes.empty())
            continue;
        uint64_t fresh = nextId();
        renewed[m.id.value] = fresh;
        copy.id = ModulatorId(fresh);
        out.push_back(copy);
    }
    return out;
}

} // namespace

CommandPtr playOwnChain(const Song& song, ClipId clipId, const NextId& nextId) {
    const Clip* clip = song.findClip(clipId);
    if (!clip || clip->isAudio() || clip->usesOwnChain())
        return nullptr;
    if (clip->hasOwnChain)
        return chainEdit(song, "Play " + clip->name + " through its own devices", [&](Song& work) {
            work.findClip(clipId)->playsOwnChain = true;
            return true;
        });
    const Track* track = song.findTrack(trackUsing(song, clipId));
    if (!track)
        return nullptr;
    // A copy of the track's chain, so the clip sounds the same until it is changed.
    std::vector<Device> devices = track->devices;
    std::vector<Modulator> none;
    ChainRemap remap;
    renewIds(devices, none, nextId, remap);
    retargetRoutes(devices, none, remap);
    ChainRemap renewed;
    std::vector<Modulator> modulators = modulatorsFor(track->modulators, remap, nextId, renewed);
    std::vector<Note> notes = clip->notes;
    retargetMotions(notes, remap);
    auto chain = chainEdit(song, "Give " + clip->name + " its own devices", [&](Song& work) {
        Clip& c = *work.findClip(clipId);
        c.hasOwnChain = c.playsOwnChain = true;
        c.devices = devices;
        c.modulators = modulators;
        return true;
    });
    if (!chain || notes == clip->notes)
        return chain;
    auto both = std::make_unique<CompoundCommand>(chain->name());
    both->add(std::move(chain));
    both->add(replaceNotes(both->name(), clipId, clip->notes, notes));
    return both;
}

CommandPtr playTrackChain(const Song& song, ClipId clipId) {
    const Clip* clip = song.findClip(clipId);
    if (!clip || !clip->usesOwnChain())
        return nullptr;
    return chainEdit(song, "Play " + clip->name + " through the track's devices", [&](Song& work) {
        work.findClip(clipId)->playsOwnChain = false;
        return true;
    });
}

CommandPtr deleteOwnChain(const Song& song, ClipId clipId) {
    const Clip* clip = song.findClip(clipId);
    if (!clip || !clip->hasOwnChain)
        return nullptr;
    return chainEdit(song, "Delete " + clip->name + "'s own devices", [&](Song& work) {
        Clip& c = *work.findClip(clipId);
        c.hasOwnChain = c.playsOwnChain = false;
        c.devices.clear();
        c.modulators.clear();
        pruneRoutes(work);
        return true;
    });
}

CommandPtr useOwnChainForTrack(const Song& song, ClipId clipId, TrackId trackId, const NextId& nextId) {
    const Clip* clip = song.findClip(clipId);
    const Track* track = song.findTrack(trackId);
    if (!clip || !track || !clip->hasOwnChain ||
        track->modulators.size() + clip->modulators.size() > size_t(kMaxTrackModulators))
        return nullptr;
    std::vector<Device> devices = clip->devices;
    std::vector<Modulator> modulators = clip->modulators;
    ChainRemap remap;
    renewIds(devices, modulators, nextId, remap);
    retargetRoutes(devices, modulators, remap);
    return chainEdit(song, "Use " + clip->name + "'s devices for " + track->name, [&](Song& work) {
        Track& t = *work.findTrack(trackId);
        t.devices = devices;
        t.modulators.insert(t.modulators.end(), modulators.begin(), modulators.end());
        work.findClip(clipId)->playsOwnChain = false;
        pruneRoutes(work);
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
        for (Playhead& p : d.playheads) {
            renew(p.id);
            for (Device& b : p.branch)
                renew(b.id);
        }
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

void renewClipChain(Clip& clip, const NextId& nextId) {
    if (clip.devices.empty() && clip.modulators.empty())
        return;
    ChainRemap remap;
    renewIds(clip.devices, clip.modulators, nextId, remap);
    retargetRoutes(clip.devices, clip.modulators, remap);
    for (AutomationEnvelope& lane : clip.automation)
        retarget(lane.paramTarget, remap);
    retargetMotions(clip.notes, remap);
}

} // namespace tessera
