#pragma once

#include "../model/Commands.hpp"
#include "../model/Device.hpp"
#include "../model/NoteOperators.hpp"
#include "../model/Scale.hpp"
#include "../model/Song.hpp"

#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tessera {

constexpr uint16_t kNoExtras = 0xFFFF;

/// A note flattened for playback: transpose, gate scaling and loop truncation are already applied.
struct NoteEvent {
    Tick start;
    Tick end;
    int8_t pitch;
    float velocity;
    float releaseVelocity;
    uint16_t extras = kNoExtras; // index into its list's NoteExtras, for notes with operators or curves
    int32_t pass = -1;           // arrangement notes: which time through their clip; -1 = worked out live
    uint8_t key = 0;             // the key it was written in: 0 = the song's, k = TrackPlayback::keys[k - 1]
    uint8_t channel = 1;         // the note's channel, 1..16
};

/// What a note with operators, micro-timing or expression curves needs at playback (EDITOR_UX §4.5-4.6).
/// Its event was moved earlier by its micro-timing and by `early`, the room timing spread needs; the engine adds
/// `early` back plus a random spread, so it only ever delays.
struct NoteExtras {
    Note note;
    uint64_t clip = 0; // ClipId value, for the randomness
    double early = 0;
    double stretch = 1; // played ticks per written tick (a timeline clip's stretch): curves span the written note
};

/// A drum lane that loops on its own length inside a looping clip (polymeter). Its notes are not in the clip's
/// main list.
struct LaneLoop {
    Tick start = 0; // clip ticks
    Tick length = 0;
    std::vector<NoteEvent> notes; // sorted by start, all inside [start, start + length)
};

/// One automation lane as the engine plays it (muted lanes are left out).
struct LanePlayback {
    int output = 0;
    float rangeMin = 0, rangeMax = 10;
    float rest = 0;
    float slewMs = 0;
    CombineMode combine = CombineMode::Override;
    std::vector<AutoPoint> points;
};

/// A lane that turns a device knob or modulator setting (docs/DEVICES.md §1.3): while the transport plays it sets
/// the knob's base value, and modulation adds on top.
struct KnobLanePlayback {
    ParamRef target;
    float rest = 0;
    CombineMode combine = CombineMode::Override;
    std::vector<AutoPoint> points;
};

struct ClipPlayback {
    Tick loopStart = 0;
    Tick loopEnd = 0;
    bool looping = true;
    Tick launchQuantize = -1;     // this clip's own launch grid in ticks; -1 = the song's
    std::vector<NoteEvent> notes; // sorted by start, all inside [loopStart, loopEnd)
    std::vector<LaneLoop> laneLoops;
    std::vector<LanePlayback> lanes; // clip automation, clip time
    std::vector<KnobLanePlayback> knobLanes;
    std::vector<NoteExtras> extras; // for notes and lane-loop notes with extras

    Tick loopLength() const { return loopEnd - loopStart; }
};

/// An arranger instance whose clip has automation: what's needed to find clip time from song time.
struct InstanceLanes {
    Tick position = 0, end = 0;
    double offset = 0; // clip tick at the instance start
    double stretch = 1;
    bool looping = false;
    Tick spanStart = 0, spanEnd = 0; // the loop (looping) or the whole clip
    std::shared_ptr<const ClipPlayback> clip;

    /// Clip time at song tick `tick`, or NaN where the instance plays nothing.
    double clipTime(double tick) const;
};

/// One drum lane as the engine plays it.
struct DrumLanePlayback {
    int8_t pitch = 36;
    int output = 0;      // the channel on the Drum Out's poly outputs
    float gateMs = 0;    // 0 = a 1 ms trigger
    int choke = 0;       // 0 = none
    bool audible = true; // mute and solo already applied
};

struct TrackPlayback {
    TrackId id;
    int busSlot = -1;
    int polyphony = 1;
    bool audible = true;
    bool drum = false;
    // Recording: an armed track takes live notes from its input; with monitor on it plays them too.
    bool armed = false;
    bool monitor = true;
    RecordInput input = RecordInput::Any;
    int midiChannel = 0; // 1..16, 0 = any
    // What the track plays besides its clips, without arming: the external inputs, or another track's notes.
    ListenSource listen = ListenSource::None;
    TrackId listenTrack;
    std::vector<DrumLanePlayback> drumLanes; // drum tracks only
    std::array<int8_t, 128> laneOfPitch{};   // drum lane per pitch, -1 for none
    int drumChannels = 0;                    // highest lane output + 1
    /// One entry per launcher slot (scene); null where the slot is empty.
    std::vector<std::shared_ptr<const ClipPlayback>> slots;
    /// The clip each slot was built from, to know when to rebuild.
    std::vector<ClipId> slotClips;
    /// The arrangement as it plays, in song ticks, sorted by start (muted instances left out).
    std::vector<NoteEvent> arrangement;
    /// The clips the arrangement uses, to know when to rebuild.
    std::vector<ClipId> arrangementClips;
    /// Automation: the track's own lanes (arranger time), instances with clip lanes (sorted by position), and how
    /// many AUTO channels the track sends (the highest lane output + 1, over track and clip lanes).
    std::vector<LanePlayback> lanes;
    std::vector<KnobLanePlayback> knobLanes;
    std::vector<InstanceLanes> arrangementLanes;
    std::vector<NoteExtras> arrangementExtras;
    int autoChannels = 0;
    bool voiceLanes = false; // some lane plays into the voices (pitch bend or pressure)
    /// Live monitoring of MIDI controls: where each CC plays (from the lanes that record it; output -1 = nowhere)
    /// and the pitch bend range, so what you hear while playing matches what the take will play.
    struct LiveControl {
        int8_t output = -1;
        float low = 0, high = 10; // volts at 0 and 1
    };
    std::array<LiveControl, 128> ccLive{};
    LiveControl bendLive{kPitchLaneSlot, -2 / 12.f, 2 / 12.f};
    // AUTO channels no lane of the track uses, in order: a CC moved with no lane yet plays on the next one, as the
    // lane its take makes will.
    std::array<int8_t, kMaxAutomationLanes> freeAuto{};
    int freeAutoCount = 0;
    /// Keys of clips with their own key, for the devices that work in a key.
    std::vector<Scale> keys;

    const ClipPlayback* slot(int index) const {
        return index >= 0 && index < int(slots.size()) ? slots[size_t(index)].get() : nullptr;
    }
};

// ---- Devices and modulators (docs/DEVICES.md §5) ------------------------------------------------------------

/// Upper bounds the engine sizes its tables by, so it never allocates.
constexpr int kMaxModulatorSlots =
    kMaxSongModulators + kMaxTracks * (kMaxTrackModulators + kMaxDevices * kMaxDeviceModulators);
constexpr int kMaxKnobs = (kMaxTracks * kMaxDevices + kMaxModulatorSlots) * kMaxDeviceParams;

/// A device as the engine runs it. Its knob values start at `knobs` in the knob table.
struct DevicePlayback {
    DeviceType type = DeviceType::Transpose;
    bool on = true;
    bool affectDrums = false;
    uint64_t id = 0;
    int knobs = 0;
};

/// One modulation onto a knob or an AUTO channel: a modulator (by slot) and how far it moves its target.
struct ModSource {
    int slot = 0;
    float amount = 0;
};

struct AutoModulation {
    int channel = 0;
    ModSource source;
};

/// A track's device chain.
struct ChainPlayback {
    std::vector<DevicePlayback> devices;
    /// Changes when devices are added, removed, reordered, switched on or off: their notes in flight are dropped.
    uint64_t structure = 0;
    bool active = false; // some device is on
    /// Modulators added onto the track's AUTO outputs, and how many channels that needs.
    std::vector<AutoModulation> autoRoutes;
    int autoChannels = 0;
    /// Slots of the track's own modulators (track and device level), which its notes restart.
    std::vector<int> modulatorSlots;
};

struct ModulatorPlayback {
    ModulatorType type = ModulatorType::Lfo;
    uint64_t id = 0;
    int slot = 0;   // stable for the modulator's lifetime, so its phase carries over between snapshots
    int knobs = 0;  // its settings in the knob table
    int track = -1; // display index of its track; -1 for song modulators
    std::array<float, kMaxSteps> steps{};
    int stepCount = 0;
};

/// A knob with modulation on it.
struct KnobModulation {
    int knob = 0;
    std::array<ModSource, kMaxModulationsPerKnob> sources{};
    int count = 0;
};

/// Whose knobs a range of the knob table holds, for finding a knob by reference.
struct KnobOwner {
    ParamRef::Kind kind = ParamRef::Kind::Device;
    uint64_t id = 0;
    int knobs = 0;
    int count = 0;
};

/// Gives each modulator a state slot in the engine that stays the same for its lifetime. Lives on the UI thread.
class ModulatorSlots {
  public:
    /// Frees the slots of deleted modulators, then gives one to every modulator that lacks one.
    void sync(const Song& song);
    int slotOf(ModulatorId id) const;

  private:
    std::unordered_map<uint64_t, int> slots;
    std::vector<int> freeSlots;
    int used = 0;
};

/// Immutable, render-ready view of a Song. Built on the UI thread, only read by the engine.
/// Tracks are shared between snapshots, so an edit only rebuilds the tracks it touched.
struct Snapshot {
    uint64_t generation = 0; // set by SnapshotExchange
    TimeSig timeSig;
    Tick loopStart = 0;
    Tick loopEnd = 0;
    Tick launchQuantize = 0;                                  // the song's launch grid in ticks (0 = immediate)
    std::vector<std::optional<double>> sceneTempos;           // per scene: the tempo it switches to, if any
    Scale key;                                                // the song key, for quantizing live notes
    uint32_t seed = 1;                                        // randomness of note operators
    std::vector<std::shared_ptr<const TrackPlayback>> tracks; // display order

    // Devices and modulators: rebuilt with every snapshot (they are small), so track playback stays shared.
    std::vector<float> knobs;          // every device knob and modulator setting, normalized
    std::vector<KnobOwner> owners;     // sorted by kind, then ID
    std::vector<ChainPlayback> chains; // per track, display order
    std::vector<ModulatorPlayback> modulators;
    std::vector<KnobModulation> modulated; // knobs that modulators move
    std::vector<int> laneKnobs;            // knobs that knob lanes turn, sorted

    /// The knob table index of a device knob or modulator setting; -1 when it isn't there.
    int knobIndex(const ParamRef& ref) const;
};

/// Gives each track an output-bus slot that stays the same for the track's whole lifetime, so reordering
/// tracks never moves their output. Lives on the UI thread.
class BusSlots {
  public:
    /// Frees slots of deleted tracks, then gives a slot to every track that lacks one.
    void sync(const Song& song);
    int slotOf(TrackId id) const;

  private:
    std::array<TrackId, kMaxTracks> owner{};
};

/// Builds snapshots on the UI thread, reusing the previous build's tracks that an edit didn't touch.
class SnapshotBuilder {
  public:
    std::unique_ptr<Snapshot> build(const Song& song, const BusSlots& slots, const ChangeSet& changes);
    /// How many tracks the last build had to rebuild (for tests and profiling).
    int rebuiltCount() const { return rebuilt; }

    const ModulatorSlots& modulatorSlots() const { return modSlots; }

  private:
    std::unordered_map<TrackId, std::shared_ptr<const TrackPlayback>> cache;
    ModulatorSlots modSlots;
    bool lastAnySolo = false;
    int rebuilt = 0;
};

/// A full build, for tests and one-off use.
std::unique_ptr<Snapshot> buildSnapshot(const Song& song, const BusSlots& slots);

} // namespace tessera
