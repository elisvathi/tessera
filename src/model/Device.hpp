#pragma once

#include "Automation.hpp"
#include "ParamRef.hpp"
#include "Types.hpp"

#include <string>
#include <vector>

/// Device chains and modulators (docs/DEVICES.md): MIDI effects on a track's notes, and modulators that turn
/// their knobs. Values are stored normalized (0..1); DeviceTypes.hpp says what each one means.
namespace tessera {

enum class DeviceType : uint8_t {
    Transpose,
    Scale,
    Velocity,
    NoteLength,
    Chord,
    Arpeggiator,
    Echo,
    Random,
    NoteFilter,
    Humanize,
    Groove,
    Playheads, // reads the clip with several playheads; first in the chain but for Motion (docs/design/PLAYHEADS.md)
    Motion,    // moves the notes that joined it a little on every pass; may sit before Playheads
    NoteRepeat // plays every note again and again while it lasts, chords included
};
constexpr int kDeviceTypeCount = 14;

enum class ModulatorType : uint8_t { Lfo, Envelope, Steps, Random, Ramp, Macro, Note, Curve };
constexpr int kModulatorTypeCount = 8;

/// Where a modulator lives, which decides what it may reach (docs/DEVICES.md §1.2).
enum class ModLevel : uint8_t { Song, Track, Device };

constexpr int kMaxDevices = 16;      // per track
constexpr int kMaxDeviceParams = 16; // knobs per device
constexpr int kMaxSongModulators = 16;
constexpr int kMaxTrackModulators = 8;
constexpr int kMaxDeviceModulators = 4;
constexpr int kMaxRoutes = 8; // targets per modulator
constexpr int kMaxModulationsPerKnob = 4;
constexpr int kMaxSteps = 16;
/// The longest drawing a Curve modulator can have: 256 bars of 4/4.
constexpr Tick kMaxCurveLength = 256 * 4 * kPpq;

/// One route from a modulator: to a knob or setting in Tessera, to a track's AUTO output channel, or to a knob
/// of another Rack module.
struct Modulation {
    enum class Kind : uint8_t { Param, AutoChannel, RackKnob };
    Kind kind = Kind::Param;
    ParamRef target;     // Param
    TrackId track;       // AutoChannel
    int8_t channel = 0;  // AutoChannel: 0..7
    ParamTarget knob;    // RackKnob
    float amount = 0.5f; // -1..1 of the target's full range

    bool operator==(const Modulation& o) const {
        return kind == o.kind && target == o.target && track == o.track && channel == o.channel && knob == o.knob &&
               amount == o.amount;
    }
};

struct Modulator {
    ModulatorId id;
    ModulatorType type = ModulatorType::Lfo;
    std::string name;
    std::vector<float> params; // normalized, the type's list
    std::vector<float> steps;  // Steps: the drawn values, 0..1
    std::vector<Modulation> routes;
    /// Curve: the drawn shape, points over `length` ticks (0.5 = no change, it moves its targets both ways; its
    /// first route turns its setting all the way, so a point's height is the setting's own value). Only the segment
    /// [segmentStart, segmentEnd) plays, round and round from when the track's clip started.
    Tick length = 0;
    std::vector<AutoPoint> points;
    Tick segmentStart = 0, segmentEnd = 0;

    bool operator==(const Modulator& o) const {
        return id == o.id && type == o.type && name == o.name && params == o.params && steps == o.steps &&
               routes == o.routes && length == o.length && points == o.points && segmentStart == o.segmentStart &&
               segmentEnd == o.segmentEnd;
    }
};

struct Device;

constexpr int kMaxBranchDevices = 4; // per playhead

/// One reader of the clip inside a Playheads device (docs/design/PLAYHEADS.md).
struct Playhead {
    PlayheadId id;
    std::string name;
    uint8_t color = 0;         // index into the playhead colours
    std::vector<float> params; // normalized, the playhead parameter list (DeviceTypes.hpp playheadInfo)
    uint8_t channel = 1;       // 1..16 put on its notes; 0 = each note keeps its own
    bool solo = false;         // whether it plays is its On setting (playheads::knob::kOn), which curves can move
    /// Devices only this playhead's notes go through, before the track's chain (never a Playheads device). They
    /// count toward the track's kMaxDevices.
    std::vector<Device> branch;
    /// A drawn path (docs/design/PLAYHEADS.md §4.2), used when its Path knob says Drawn: points at the playhead's
    /// own time within its path cycle (ticks), each giving a place in its region (0 = start, 1 = end).
    std::vector<AutoPoint> path;

    bool operator==(const Playhead& o) const;
};

struct Device {
    DeviceId id;
    DeviceType type = DeviceType::Transpose;
    std::string name;
    bool on = true;
    bool affectDrumLanes = false;      // pitch devices leave drum lanes alone unless this is set
    std::vector<float> params;         // normalized, the type's list
    std::vector<Modulator> modulators; // device level
    std::vector<Playhead> playheads;   // Playheads devices only
    uint8_t color = 0;                 // Motion devices: index into the playhead colours, marking their notes

    bool operator==(const Device& o) const {
        return id == o.id && type == o.type && name == o.name && on == o.on && affectDrumLanes == o.affectDrumLanes &&
               params == o.params && modulators == o.modulators && playheads == o.playheads && color == o.color;
    }
};

inline bool Playhead::operator==(const Playhead& o) const {
    return id == o.id && name == o.name && color == o.color && params == o.params && channel == o.channel &&
           solo == o.solo && branch == o.branch && path == o.path;
}

} // namespace tessera
