#pragma once

#include "DeviceLookup.hpp"
#include "DeviceTypes.hpp"
#include "TrackCommands.hpp"

#include <map>
#include <optional>

/// Undoable edits of device chains and modulators (docs/DEVICES.md §4). Every factory returns null when the
/// edit isn't possible or changes nothing, so no empty undo step is recorded.
namespace tessera {

/// The chain parts of a song an edit touched: the song's modulators, the devices and modulators of some tracks, and
/// the own chains of some clips. Only touched parts are stored.
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
    /// A clip's own chain and whether it has one and plays through it.
    struct ClipChain {
        ClipId clip;
        bool hasOwn = false, playsOwn = false;
        std::vector<Device> devices;
        std::vector<Modulator> modulators;
        bool operator==(const ClipChain& o) const {
            return clip == o.clip && hasOwn == o.hasOwn && playsOwn == o.playsOwn && devices == o.devices &&
                   modulators == o.modulators;
        }
    };
    std::vector<ClipChain> clips;
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

/// A new device at `index` in a chain, a track's or a clip's own (clamped; -1 = the end). Null at kMaxDevices. A Playheads device
/// goes first with two playheads (only Motion devices may come before it), and only when the track has none; other
/// devices never go before it. A Motion device goes anywhere; by default (-1) before the Playheads device, named
/// "Motion N" in a colour of its own.
CommandPtr addDevice(const Song& song, ChainOwner owner, DeviceType type, int index, const NextId& nextId,
                     DeviceId* added = nullptr);
/// Removes a device with its modulators, and every route that pointed at them.
/// Removing (or moving away) the last device and modulator of a clip's own chain sends the clip back to its track's
/// devices, in the same undo step.
CommandPtr removeDevice(const Song& song, DeviceId id);
/// Moves a device to `index` in a chain (the same or another, a track's or a clip's own). Its own modulators move
/// along; routes that end up out of reach are removed.
CommandPtr moveDevice(const Song& song, DeviceId id, ChainOwner to, int index);
/// A copy right after the original, with new IDs. Routes inside the copy point at the copy.
CommandPtr duplicateDevice(const Song& song, DeviceId id, const NextId& nextId);
/// A copy at `index` of a chain (positions counted in the chain as it is; -1 = the end), as Ctrl+drag makes. New
/// IDs; routes inside the copy point at the copy, and any that end up out of reach go.
CommandPtr copyDevice(const Song& song, DeviceId id, ChainOwner to, int index, const NextId& nextId);
/// Pastes copied strip items (docs/DECISIONS.md §163) into chain `to`, devices from `index` on (-1 = the end):
/// `modulators` go into that chain's own section (the track's, or the clip's), `songModulators` into the song's.
/// With `into`, every pasted modulator goes into that section instead, whatever level it was copied from: the song's,
/// a track's, a clip's or a device's (§182). All get new IDs, and names not yet used there; moves between pasted
/// things follow the copies; moves now out of reach go, counted in `dropped`. What doesn't fit (kMaxDevices, a
/// second Playheads, full sections) is left out. Null when nothing was pasted.
/// How a dragged modulator would land at `to`: whether there is room, and how many of its moves can't reach a
/// setting from there (they are dropped by a move, and by a copy).
struct ModulatorLanding {
    bool room = false;
    bool same = false; // `to` is where it already lives
    int lost = 0;
};
ModulatorLanding modulatorLanding(const Song& song, ModulatorId id, ModulatorPlace to);
/// Moves a modulator to another section (docs/DECISIONS.md §194), keeping its ID, so lanes and moves that turn its
/// own settings keep working. Its moves that can't reach their setting from there go, counted in `dropped`. Null
/// when it is already there or the section is full.
CommandPtr moveModulator(const Song& song, ModulatorId id, ModulatorPlace to, int* dropped = nullptr);
CommandPtr pasteChainItems(const Song& song, ChainOwner to, int index, std::vector<Device> devices,
                           std::vector<Modulator> modulators, std::vector<Modulator> songModulators,
                           const NextId& nextId, int* dropped = nullptr,
                           std::optional<ModulatorPlace> into = std::nullopt);
CommandPtr setDeviceOn(const Song& song, DeviceId id, bool on);
CommandPtr setDeviceDrumLanes(const Song& song, DeviceId id, bool affect);
CommandPtr renameDevice(const Song& song, DeviceId id, const std::string& name);

/// A device's or modulator's dice switch (docs/DECISIONS.md §163): a number 1..kMaxVariation, or kNewEachPlay.
CommandPtr setDeviceVariation(const Song& song, DeviceId id, int32_t variation);
CommandPtr setModulatorVariation(const Song& song, ModulatorId id, int32_t variation);
/// An Arpeggiator's own Pattern line and its dice together, as one undo step (an empty line: made from the held
/// notes again).
CommandPtr setArpLine(const Song& song, DeviceId id, std::vector<uint8_t> line, int32_t variation,
                      const std::string& name);
/// An Arpeggiator's Pattern line, its dice and some of its settings (display values by knob: steps, row lengths)
/// together, as one undo step: a drag that paints steps, a new length, a reset. Null when nothing changes.
CommandPtr setArpPattern(const Song& song, DeviceId id, std::vector<uint8_t> line, int32_t variation,
                         const std::vector<std::pair<int, float>>& settings, const std::string& name);
/// The song as one run with roll `roll` plays it: everything on "New every play" fixed to that run's number. A bake or
/// export renders this, so what it writes can be kept.
Song songRolled(const Song& song, uint32_t roll);
/// Keep it, after a bake on roll `roll`: what played on "New every play" on track `track` (its devices, its clips'
/// own chains and chance, their modulators) and the song's modulators take that run's numbers. One undo step; null
/// when nothing there was on New.
CommandPtr keepRoll(const Song& song, TrackId track, uint32_t roll);

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
/// Turns a playhead on or off: its On setting, which a curve or a lane can also move.
CommandPtr setPlayheadOn(const Song& song, PlayheadId id, bool on);
CommandPtr setPlayheadSolo(const Song& song, PlayheadId id, bool solo);
/// Replaces a playhead's drawn path (sorted, values kept in 0..1), as one undo step named `name`.
CommandPtr setPlayheadPath(const Song& song, PlayheadId id, std::vector<AutoPoint> path,
                           const std::string& name = "Draw path");

// ---- Note motion (docs/design/PLAYHEADS.md §6) --------------------------------------------------------------

/// Moves notes of a clip with a Motion device, or (an invalid `motion`) stops them moving. One undo step.
CommandPtr setNotesMotion(const Song& song, ClipId clip, const std::vector<NoteId>& notes, DeviceId motion);

// ---- A clip's own chain (docs/DECISIONS.md §157) ------------------------------------------------------------

/// Makes the clip play through its own devices. The first time it gets them as a copy of its track's chain (new
/// IDs), with the track's modulators that move those devices, so it sounds the same until changed; its notes that
/// move with a Motion device move with the copy. Afterwards it switches back to the chain it kept. Null for an audio
/// clip, one that already plays its own, or one no track uses the first time.
CommandPtr playOwnChain(const Song& song, ClipId clip, const NextId& nextId);
/// Makes the clip play through its track's devices again. It keeps its own chain, to switch back to.
CommandPtr playTrackChain(const Song& song, ClipId clip);
/// Throws the clip's own chain away (with routes that pointed into it): it plays through its track's again.
CommandPtr deleteOwnChain(const Song& song, ClipId clip);
/// Makes the clip's own chain the track's: the track's devices become a copy of it (new IDs), its modulators join the
/// track's, and the clip plays through the track's chain (keeping its own). Notes of other clips that moved with a
/// Motion device of the old chain stop moving.
CommandPtr useOwnChainForTrack(const Song& song, ClipId clip, TrackId track, const NextId& nextId);

// ---- Modulators ---------------------------------------------------------------------------------------------

/// A new modulator at `place`, named "LFO", "LFO 2"... Null when that level is full.
CommandPtr addModulator(const Song& song, const ModulatorPlace& place, ModulatorType type, const NextId& nextId,
                        ModulatorId* added = nullptr);
/// Removes a modulator, and every route that pointed at its settings.
CommandPtr removeModulator(const Song& song, ModulatorId id);
CommandPtr renameModulator(const Song& song, ModulatorId id, const std::string& name);
/// The drawn values of a Steps modulator (at most kMaxSteps, each clamped to 0..1).
CommandPtr setSteps(const Song& song, ModulatorId id, std::vector<float> steps);
/// What a Curve modulator draws: how long the drawing is, its points, and the segment of it that plays.
struct CurveDrawing {
    Tick length = 4 * kPpq;
    std::vector<AutoPoint> points;
    Tick segmentStart = 0, segmentEnd = 4 * kPpq;
};
/// Makes `d` a drawing a Curve may hold: a length of 1/16 to kMaxCurveLength, points sorted with values 0..1 (points
/// past the end are kept, so lengthening again brings them back), a segment of at least a 1/16 inside the length (one
/// past the end slides back in, as long as before unless the drawing is shorter).
void tidyCurve(CurveDrawing& d);
/// A Curve modulator's drawing (tidied first).
CommandPtr setCurve(const Song& song, ModulatorId id, CurveDrawing drawing);
/// Starts moving `target` with a drawn shape, in one undo step: a new Curve modulator at `place`, named `name` (what it
/// moves), turning the target all the way, so a point's height is the setting's own value. Without `drawing` it gets
/// a new Curve's usual shape. Null when there is no room there or the target is out of its reach.
CommandPtr animateWithCurve(const Song& song, const ModulatorPlace& place, const ParamRef& target,
                            const std::string& name, std::optional<CurveDrawing> drawing, const NextId& nextId,
                            ModulatorId* added = nullptr);

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
/// Gives a copied clip's own chain new IDs and points its routes, its lanes and its notes' Motion devices at the
/// copies.
void renewClipChain(Clip& clip, const NextId& nextId);

} // namespace tessera
