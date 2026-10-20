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
    Humanize
};
constexpr int kDeviceTypeCount = 10;

enum class ModulatorType : uint8_t { Lfo, Envelope, Steps, Random, Ramp, Macro, Note };
constexpr int kModulatorTypeCount = 7;

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

    bool operator==(const Modulator& o) const {
        return id == o.id && type == o.type && name == o.name && params == o.params && steps == o.steps &&
               routes == o.routes;
    }
};

struct Device {
    DeviceId id;
    DeviceType type = DeviceType::Transpose;
    std::string name;
    bool on = true;
    bool affectDrumLanes = false;      // pitch devices leave drum lanes alone unless this is set
    std::vector<float> params;         // normalized, the type's list
    std::vector<Modulator> modulators; // device level

    bool operator==(const Device& o) const {
        return id == o.id && type == o.type && name == o.name && on == o.on && affectDrumLanes == o.affectDrumLanes &&
               params == o.params && modulators == o.modulators;
    }
};

} // namespace tessera
