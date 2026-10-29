#include "DeviceCommands.hpp"

#include "DeviceTypes.hpp"
#include "Devices.hpp"
#include "Playheads.hpp"

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

/// The first chain position other devices may take: after the Playheads device, which always comes first.
int firstFree(const Track& track) {
    return !track.devices.empty() && track.devices.front().type == DeviceType::Playheads ? 1 : 0;
}

bool hasPlayheads(const Track& track) {
    return std::any_of(track.devices.begin(), track.devices.end(),
                       [](const Device& d) { return d.type == DeviceType::Playheads; });
}

/// Puts a device into a chain at `at` (clamped): a Playheads device always goes first, others after it.
void insertDevice(Track& track, int at, Device device) {
    if (device.type == DeviceType::Playheads)
        at = 0;
    else
        at = std::clamp(at, firstFree(track), int(track.devices.size()));
    track.devices.insert(track.devices.begin() + at, std::move(device));
}

/// The playhead the device gets next: named and coloured by its place, on the lowest channel nobody uses,
/// starting a beat after the one before so a canon is heard at once.
Playhead nextPlayhead(const Device& device, PlayheadId id) {
    int index = int(device.playheads.size());
    Playhead p = devices::makePlayhead(id, index);
    int channel = 1;
    while (channel < 16 && std::any_of(device.playheads.begin(), device.playheads.end(),
                                       [&](const Playhead& o) { return o.channel == channel; }))
        channel++;
    p.channel = uint8_t(channel);
    const devices::ParamInfo& start = devices::playheadInfo().params[playheads::knob::kStart];
    p.params[playheads::knob::kStart] = devices::normalize(start, float(4 * index));
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

CommandPtr addDevice(const Song& song, TrackId trackId, DeviceType type, int index, const NextId& nextId) {
    const Track* existing = song.findTrack(trackId);
    if (!existing || deviceCount(*existing) >= kMaxDevices)
        return nullptr;
    if (type == DeviceType::Playheads && hasPlayheads(*existing))
        return nullptr;
    Device device = devices::makeDevice(type, DeviceId(nextId()));
    if (type == DeviceType::Groove) {
        // Its grid suits the track's notes: a pattern of 1/8s wouldn't move at all on 1/16s.
        std::vector<const Clip*> clips;
        for (const ClipSlot& s : existing->slots)
            if (const Clip* c = s.clip ? song.findClip(*s.clip) : nullptr)
                clips.push_back(c);
        for (const ArrangerClip& a : existing->arrangement)
            if (const Clip* c = song.findClip(a.clip))
                clips.push_back(c);
        const devices::ParamInfo& grid = devices::deviceInfo(type).params[size_t(chain::knob::kGrooveGrid)];
        device.params[size_t(chain::knob::kGrooveGrid)] =
            devices::normalize(grid, swingStepFor(clips) == kPpq / 2 ? 1.f : 0.f);
    }
    if (type == DeviceType::Playheads)
        for (int i = 0; i < 2; i++)
            device.playheads.push_back(nextPlayhead(device, PlayheadId(nextId())));
    return chainEdit(song, "Add " + device.name, [&](Song& work) {
        Track& track = *work.findTrack(trackId);
        insertDevice(track, index < 0 ? int(track.devices.size()) : index, device);
        return true;
    });
}

CommandPtr removeDevice(const Song& song, DeviceId id) {
    TrackId trackId;
    if (!findDevice(song, id, &trackId))
        return nullptr;
    return chainEdit(song, "Remove " + deviceName(song, id), [&](Song& work) {
        std::vector<Device>& chain = *chainOf(work, id);
        chain.erase(std::find_if(chain.begin(), chain.end(), [&](const Device& d) { return d.id == id; }));
        pruneRoutes(work);
        return true;
    });
}

CommandPtr moveDevice(const Song& song, DeviceId id, TrackId toTrack, int index) {
    TrackId fromTrack;
    if (!findDevice(song, id, &fromTrack))
        return nullptr;
    const Track* target = song.findTrack(toTrack);
    if (!target || (toTrack != fromTrack && deviceCount(*target) >= kMaxDevices))
        return nullptr;
    PlayheadId inBranch;
    chainOf(const_cast<Song&>(song), id, &inBranch);
    if (inBranch.valid())
        return nullptr; // branch devices stay in their branch
    if (findDevice(song, id)->type == DeviceType::Playheads && toTrack != fromTrack && hasPlayheads(*target))
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
        insertDevice(to, at, std::move(device));
        pruneRoutes(work);
        return true;
    });
}

CommandPtr duplicateDevice(const Song& song, DeviceId id, const NextId& nextId) {
    TrackId trackId;
    const Device* original = findDevice(song, id, &trackId);
    if (!original)
        return nullptr;
    PlayheadId inBranch;
    chainOf(const_cast<Song&>(song), id, &inBranch);
    if (inBranch.valid()) {
        const Playhead* p = findPlayhead(song, inBranch);
        if (int(p->branch.size()) >= kMaxBranchDevices || deviceCount(*song.findTrack(trackId)) >= kMaxDevices)
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
    if (!original || !target)
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
        Track& track = *work.findTrack(toTrack);
        insertDevice(track, index < 0 ? int(track.devices.size()) : index, std::move(copy.front()));
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

namespace {

/// Runs `edit` on a copy of a clip's motions and notes and turns what it changed into one command.
template <typename Edit>
CommandPtr motionEdit(const Song& song, ClipId clipId, const std::string& name, Edit&& edit) {
    const Clip* clip = song.findClip(clipId);
    if (!clip)
        return nullptr;
    ReplaceClipMotions::Parts before{clip->motions, clip->notes};
    ReplaceClipMotions::Parts after = before;
    if (!edit(after) || (after.motions == before.motions && after.notes == before.notes))
        return nullptr;
    return std::make_unique<ReplaceClipMotions>(name, clipId, std::move(before), std::move(after));
}

} // namespace

void ReplaceClipMotions::set(Song& song, const Parts& parts) const {
    Clip* c = song.findClip(clip);
    assert(c && "ReplaceClipMotions on a missing clip");
    if (!c)
        return;
    c->motions = parts.motions;
    c->notes = parts.notes;
}

CommandPtr addMotion(const Song& song, ClipId clip, const std::vector<NoteId>& notes, const NextId& nextId,
                     MotionId* added) {
    const Clip* c = song.findClip(clip);
    if (!c || notes.empty() || int(c->motions.size()) >= kMaxMotionGroups)
        return nullptr;
    int index = 0;
    while (std::any_of(c->motions.begin(), c->motions.end(),
                       [&](const MotionGroup& m) { return m.name == devices::makeMotion(MotionId(), index).name; }))
        index++;
    MotionGroup group = devices::makeMotion(MotionId(nextId()), index);
    if (added)
        *added = group.id;
    return motionEdit(song, clip, "Add " + group.name, [&](ReplaceClipMotions::Parts& p) {
        p.motions.push_back(group);
        bool any = false;
        for (Note& n : p.notes)
            if (std::find(notes.begin(), notes.end(), n.id) != notes.end()) {
                n.motion = group.id;
                any = true;
            }
        return any;
    });
}

CommandPtr removeMotion(const Song& song, MotionId id) {
    ClipId clip;
    const MotionGroup* group = findMotion(song, id, &clip);
    if (!group)
        return nullptr;
    return motionEdit(song, clip, "Remove " + group->name, [&](ReplaceClipMotions::Parts& p) {
        p.motions.erase(std::find_if(p.motions.begin(), p.motions.end(), [&](const MotionGroup& m) { return m.id == id; }));
        for (Note& n : p.notes)
            if (n.motion == id)
                n.motion = MotionId();
        return true;
    });
}

CommandPtr setNotesMotion(const Song& song, ClipId clip, const std::vector<NoteId>& notes, MotionId motion) {
    const Clip* c = song.findClip(clip);
    if (!c || (motion.valid() && !c->findMotion(motion)))
        return nullptr;
    return motionEdit(song, clip, motion.valid() ? "Move notes into " + c->findMotion(motion)->name : "Stop notes moving",
                      [&](ReplaceClipMotions::Parts& p) {
                          for (Note& n : p.notes)
                              if (std::find(notes.begin(), notes.end(), n.id) != notes.end())
                                  n.motion = motion;
                          return true;
                      });
}

CommandPtr setParam(const Song& song, const ParamRef& ref, float value) {
    if (ref.kind == ParamRef::Kind::Motion) {
        // Motion knobs live in a clip, not in a chain.
        ClipId clip;
        const MotionGroup* group = findMotion(song, MotionId(ref.owner), &clip);
        const devices::ParamInfo* info = devices::motionParamInfo(ref.param);
        if (!group || !info)
            return nullptr;
        return motionEdit(song, clip, "Set " + group->name + " " + info->name, [&](ReplaceClipMotions::Parts& p) {
            for (MotionGroup& m : p.motions)
                if (m.id.value == ref.owner)
                    m.params[ref.param] = std::clamp(value, 0.f, 1.f);
            return true;
        });
    }
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
    TrackId trackId;
    const Playhead* p = findPlayhead(song, id, &trackId);
    if (!p || type == DeviceType::Playheads || int(p->branch.size()) >= kMaxBranchDevices ||
        deviceCount(*song.findTrack(trackId)) >= kMaxDevices)
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
        d.playheads.erase(std::find_if(d.playheads.begin(), d.playheads.end(),
                                       [&](const Playhead& p) { return p.id == id; }));
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

CommandPtr setPlayheadMute(const Song& song, PlayheadId id, bool mute) {
    return playheadEdit(song, id, (mute ? "Mute " : "Unmute ") + playheadName(song, id), [&](Device&, Playhead& p) {
        p.mute = mute;
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

} // namespace tessera
