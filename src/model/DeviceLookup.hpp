#pragma once

#include "Song.hpp"

#include <functional>

/// Finding devices, modulators and knobs in a song, and the rules for what a modulator may reach
/// (docs/DEVICES.md §1.2). Shared by the commands, the song's repair on load, and the engine's snapshot.
namespace tessera {

/// Where a modulator lives: the song, a track, or a device on a track.
struct ModulatorPlace {
    ModLevel level = ModLevel::Song;
    TrackId track;   // Track and Device levels
    DeviceId device; // Device level
};

Device* findDevice(Song& song, DeviceId id, TrackId* track = nullptr);
const Device* findDevice(const Song& song, DeviceId id, TrackId* track = nullptr);
Modulator* findModulator(Song& song, ModulatorId id, ModulatorPlace* place = nullptr);
const Modulator* findModulator(const Song& song, ModulatorId id, ModulatorPlace* place = nullptr);

/// The knob or setting a reference points at (normalized), or nullptr when it doesn't exist.
float* findParam(Song& song, const ParamRef& ref);
const float* findParam(const Song& song, const ParamRef& ref);

/// The track a knob or setting belongs to; invalid for song modulators' settings.
TrackId trackOf(const Song& song, const ParamRef& ref);

/// Whether the modulator `self` living at `place` may have route `m`: the target exists, is inside its reach
/// (song: anything; track: its track; device: its device), and isn't one of its own settings.
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
