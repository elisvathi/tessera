#pragma once

#include "Song.hpp"

#include <functional>

/// Finding devices, modulators and knobs in a song, and the rules for what a modulator may reach
/// (docs/DEVICES.md §1.2). Shared by the commands, the song's repair on load, and the engine's snapshot.
namespace tessera {

/// Where a modulator lives: the song, a track, a clip's own chain, or a device in a track's or a clip's chain.
struct ModulatorPlace {
    ModLevel level = ModLevel::Song;
    TrackId track;   // Track level; Device and Clip levels: the track the chain plays on (for a clip, the first track
                     // using it; invalid when none does)
    DeviceId device; // Device level
    ClipId clip;     // Clip level, and Device level for a device in a clip's own chain
};

/// Whose device chain something is: a track's, or a clip's own (when `clip` is set).
struct ChainOwner {
    TrackId track;
    ClipId clip;
    ChainOwner() = default;
    ChainOwner(TrackId t) : track(t) {}
    ChainOwner(ClipId c) : clip(c) {}
    bool isClip() const { return clip.valid(); }
    bool valid() const { return track.valid() || clip.valid(); }
    bool operator==(const ChainOwner& o) const { return track == o.track && clip == o.clip; }
    bool operator!=(const ChainOwner& o) const { return !(*this == o); }
};

/// The devices and the modulators of a chain owner (a track's own modulators, or a clip's), or nullptr when it
/// doesn't exist.
std::vector<Device>* devicesOf(Song& song, ChainOwner owner);
const std::vector<Device>* devicesOf(const Song& song, ChainOwner owner);
std::vector<Modulator>* modulatorsOf(Song& song, ChainOwner owner);
const std::vector<Modulator>* modulatorsOf(const Song& song, ChainOwner owner);
/// Whose chain a device is in (a branch device: the chain of its Playheads device). Invalid when it isn't found.
ChainOwner ownerOf(const Song& song, DeviceId id);
/// Whose chain a knob or setting belongs to: its device's, its playhead's, its modulator's (invalid for a song
/// modulator's settings).
ChainOwner ownerOf(const Song& song, const ParamRef& ref);
/// The first track whose launcher slots or timeline use the clip; invalid when none does.
TrackId trackUsing(const Song& song, ClipId clip);

/// A device in a track's chain, a clip's own chain, or one of their playheads' branches. `track` gets the track it
/// plays on (for a clip's device, the first track using the clip).
Device* findDevice(Song& song, DeviceId id, TrackId* track = nullptr);
const Device* findDevice(const Song& song, DeviceId id, TrackId* track = nullptr);
Modulator* findModulator(Song& song, ModulatorId id, ModulatorPlace* place = nullptr);
const Modulator* findModulator(const Song& song, ModulatorId id, ModulatorPlace* place = nullptr);

/// A playhead, with the track and the Playheads device it is in.
Playhead* findPlayhead(Song& song, PlayheadId id, TrackId* track = nullptr, DeviceId* device = nullptr);
const Playhead* findPlayhead(const Song& song, PlayheadId id, TrackId* track = nullptr, DeviceId* device = nullptr);


/// The list a device sits in: its track's chain, or a playhead's branch (then `playhead` says whose).
std::vector<Device>* chainOf(Song& song, DeviceId id, PlayheadId* playhead = nullptr);

/// The devices of a chain, its playheads' branches included (they share the kMaxDevices limit).
int deviceCount(const std::vector<Device>& chain);
inline int deviceCount(const Track& track) { return deviceCount(track.devices); }

/// The chain's Playheads device when it has one and it is on, else nullptr.
const Device* activePlayheads(const std::vector<Device>& chain);
inline const Device* activePlayheads(const Track& track) { return activePlayheads(track.devices); }
/// The chain `clip`'s notes go through on track `t`: the clip's own while it plays through it, else the track's.
inline const std::vector<Device>& chainPlaying(const Track& t, const Clip* clip) {
    return clip && clip->usesOwnChain() ? clip->devices : t.devices;
}
/// The Playheads device that reads `clip` on track `t` (in the chain it plays through), when it is on.
inline const Device* activePlayheads(const Track& t, const Clip* clip) {
    return activePlayheads(chainPlaying(t, clip));
}

/// The knob or setting a reference points at (normalized), or nullptr when it doesn't exist.
float* findParam(Song& song, const ParamRef& ref);
const float* findParam(const Song& song, const ParamRef& ref);

/// The track a knob or setting belongs to; invalid for song modulators' settings.
TrackId trackOf(const Song& song, const ParamRef& ref);

/// Whether the modulator `self` living at `place` may have route `m`: the target exists, is inside its reach
/// (song: anything; track: its track, the own chains of its clips included; clip: the clip's own chain; device: its
/// device), and isn't one of its own settings.
bool reachable(const Song& song, const ModulatorPlace& place, ModulatorId self, const Modulation& m);

/// Calls `f` for every modulator in the song with where it lives.
void forEachModulator(Song& song, const std::function<void(Modulator&, const ModulatorPlace&)>& f);
void forEachModulator(const Song& song, const std::function<void(const Modulator&, const ModulatorPlace&)>& f);

/// How many routes point at a knob or setting (a knob takes at most kMaxModulationsPerKnob).
int modulationsOn(const Song& song, const ParamRef& ref);

/// Removes every route that is no longer reachable (its target went away, or moved out of reach). Returns how
/// many were removed.
int pruneRoutes(Song& song);

} // namespace tessera
