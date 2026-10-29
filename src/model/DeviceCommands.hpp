#pragma once

#include "DeviceLookup.hpp"
#include "TrackCommands.hpp"

#include <map>
#include <optional>

/// Undoable edits of device chains and modulators (docs/DEVICES.md §4). Every factory returns null when the
/// edit isn't possible or changes nothing, so no empty undo step is recorded.
namespace tessera {

/// The chain parts of a song an edit touched: the song's modulators, and the devices and modulators of some
/// tracks. Only touched parts are stored.
struct ChainParts {
    std::optional<std::vector<Modulator>> songModulators;
    struct TrackChain {
        TrackId track;
        std::vector<Device> devices;
        std::vector<Modulator> modulators;
        bool operator==(const TrackChain& o) const {
            return track == o.track && devices == o.devices && modulators == o.modulators;
        }
    };
    std::vector<TrackChain> tracks;
};

/// Swaps chain parts between two states. Moving a device between tracks, removing one (and the routes that
/// pointed at it) and every knob change are all expressed this way.
class ReplaceChains : public Command {
  public:
    ReplaceChains(std::string name, ChainParts before, ChainParts after)
        : label(std::move(name)), before(std::move(before)), after(std::move(after)) {}

    void apply(Song& song) override { set(song, after); }
    void revert(Song& song) override { set(song, before); }
    std::string name() const override { return label; }
    ChangeSet changes() const override;
    const ChainParts& partsBefore() const { return before; }
    const ChainParts& partsAfter() const { return after; }

  private:
    static void set(Song& song, const ChainParts& parts);
    std::string label;
    ChainParts before, after;
};

// ---- Devices ------------------------------------------------------------------------------------------------

/// A new device at `index` in the track's chain (clamped; -1 = the end). Null at kMaxDevices. A Playheads device
/// goes first with two playheads, and only when the track has none; other devices never go before it.
CommandPtr addDevice(const Song& song, TrackId track, DeviceType type, int index, const NextId& nextId);
/// Removes a device with its modulators, and every route that pointed at them.
CommandPtr removeDevice(const Song& song, DeviceId id);
/// Moves a device to `index` in a track's chain (the same track or another). Its own modulators move along;
/// routes that end up out of reach are removed.
CommandPtr moveDevice(const Song& song, DeviceId id, TrackId toTrack, int index);
/// A copy right after the original, with new IDs. Routes inside the copy point at the copy.
CommandPtr duplicateDevice(const Song& song, DeviceId id, const NextId& nextId);
/// A copy at `index` of a track's chain (positions counted in the chain as it is; -1 = the end), as Ctrl+drag
/// makes. New IDs; routes inside the copy point at the copy, and any that end up out of reach go.
CommandPtr copyDevice(const Song& song, DeviceId id, TrackId toTrack, int index, const NextId& nextId);
CommandPtr setDeviceOn(const Song& song, DeviceId id, bool on);
CommandPtr setDeviceDrumLanes(const Song& song, DeviceId id, bool affect);
CommandPtr renameDevice(const Song& song, DeviceId id, const std::string& name);

/// Sets a device knob or modulator setting (normalized 0..1). One command per drag.
CommandPtr setParam(const Song& song, const ParamRef& ref, float value);

// ---- Playheads (docs/design/PLAYHEADS.md) ------------------------------------------------------------------

/// A new playhead at the end of a Playheads device. Null when it already has playheads::kMaxPlayheads.
CommandPtr addPlayhead(const Song& song, DeviceId device, const NextId& nextId, PlayheadId* added = nullptr);
CommandPtr removePlayhead(const Song& song, PlayheadId id);
/// A new device in a playhead's branch at `index` (-1 = the end). Null when the branch has kMaxBranchDevices or the
/// track kMaxDevices. Removing, renaming, bypassing and setting knobs of branch devices use the device commands.
CommandPtr addBranchDevice(const Song& song, PlayheadId id, DeviceType type, int index, const NextId& nextId);
/// A copy right after the original, with a new ID and every setting the same.
CommandPtr duplicatePlayhead(const Song& song, PlayheadId id, const NextId& nextId, PlayheadId* added = nullptr);
CommandPtr renamePlayhead(const Song& song, PlayheadId id, const std::string& name);
/// The channel put on the playhead's notes: 1..16, or 0 to keep each note's own.
CommandPtr setPlayheadChannel(const Song& song, PlayheadId id, int channel);
CommandPtr setPlayheadMute(const Song& song, PlayheadId id, bool mute);
CommandPtr setPlayheadSolo(const Song& song, PlayheadId id, bool solo);
/// Sets the curve that moves one of a playhead's settings (repeating every `length` ticks), or with no points
/// removes it. One undo step.
CommandPtr setPlayheadCurve(const Song& song, PlayheadId id, int param, Tick length, std::vector<AutoPoint> points);
/// Replaces a playhead's drawn path (sorted, values kept in 0..1), as one undo step named `name`.
CommandPtr setPlayheadPath(const Song& song, PlayheadId id, std::vector<AutoPoint> path,
                           const std::string& name = "Draw path");

// ---- Note motion (docs/design/PLAYHEADS.md §6) --------------------------------------------------------------

/// Swaps a clip's motion groups and notes between two states: every motion edit is expressed this way.
class ReplaceClipMotions : public Command {
  public:
    struct Parts {
        std::vector<MotionGroup> motions;
        std::vector<Note> notes;
    };
    ReplaceClipMotions(std::string name, ClipId clip, Parts before, Parts after)
        : label(std::move(name)), clip(clip), before(std::move(before)), after(std::move(after)) {}

    void apply(Song& song) override { set(song, after); }
    void revert(Song& song) override { set(song, before); }
    std::string name() const override { return label; }
    ChangeSet changes() const override { return ChangeSet::clip(clip); }

  private:
    void set(Song& song, const Parts& parts) const;
    std::string label;
    ClipId clip;
    Parts before, after;
};

/// A new motion group in a clip, with the given notes moved into it (out of any other). Null at kMaxMotionGroups
/// or without notes.
CommandPtr addMotion(const Song& song, ClipId clip, const std::vector<NoteId>& notes, const NextId& nextId,
                     MotionId* added = nullptr);
/// Removes a motion group; its notes stay, without motion.
CommandPtr removeMotion(const Song& song, MotionId id);
/// Moves notes into a motion group, or (an invalid `motion`) out of their group.
CommandPtr setNotesMotion(const Song& song, ClipId clip, const std::vector<NoteId>& notes, MotionId motion);

// ---- Modulators ---------------------------------------------------------------------------------------------

/// A new modulator at `place`, named "LFO", "LFO 2"... Null when that level is full.
CommandPtr addModulator(const Song& song, const ModulatorPlace& place, ModulatorType type, const NextId& nextId,
                        ModulatorId* added = nullptr);
/// Removes a modulator, and every route that pointed at its settings.
CommandPtr removeModulator(const Song& song, ModulatorId id);
CommandPtr renameModulator(const Song& song, ModulatorId id, const std::string& name);
/// The drawn values of a Steps modulator (at most kMaxSteps, each clamped to 0..1).
CommandPtr setSteps(const Song& song, ModulatorId id, std::vector<float> steps);

/// Adds a route. Null when the target is out of reach, the modulator has kMaxRoutes routes, the knob already
/// takes kMaxModulationsPerKnob modulations, or the modulator already reaches that target.
CommandPtr addRoute(const Song& song, ModulatorId id, const Modulation& route);
CommandPtr removeRoute(const Song& song, ModulatorId id, int index);
/// Sets how far a route moves its target (-1..1). One command per drag.
CommandPtr setRouteAmount(const Song& song, ModulatorId id, int index, float amount);

/// Why addRoute would refuse, as the user reads it, or empty when it would succeed.
std::string routeProblem(const Song& song, ModulatorId id, const Modulation& route);

// ---- Copying ------------------------------------------------------------------------------------------------

/// Old ID value -> new ID value, for devices and modulators that were copied.
using ChainRemap = std::map<uint64_t, uint64_t>;

/// Gives copied devices and modulators new IDs and records the mapping.
void renewIds(std::vector<Device>& devices, std::vector<Modulator>& modulators, const NextId& nextId,
              ChainRemap& remap);
/// Points a reference at the copy when its owner was copied.
void retarget(ParamRef& ref, const ChainRemap& remap);
/// Points every route of the copied devices and modulators at the copies.
void retargetRoutes(std::vector<Device>& devices, std::vector<Modulator>& modulators, const ChainRemap& remap);

} // namespace tessera
