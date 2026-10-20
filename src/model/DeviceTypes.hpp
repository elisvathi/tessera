#pragma once

#include "Device.hpp"

#include <string>
#include <vector>

/// What each device and modulator knob means (docs/DEVICES.md §2–3): name, range, unit, default. Knobs are
/// stored normalized (0..1); these tables turn that into what the user sees. The lists are append-only, so
/// saved patches keep their meaning: new knobs go at the end.
namespace tessera::devices {

struct ParamInfo {
    enum class Kind : uint8_t { Number, Integer, Exponential, Choice, Toggle };
    const char* name;
    Kind kind = Kind::Number;
    float min = 0, max = 1; // display units; Exponential needs min > 0
    float def = 0;          // display units (a choice's index, 0/1 for a toggle)
    const char* unit = "";  // "%", "st", "ms", "Hz", "°", "bars"...
    std::vector<std::string> choices;

    /// Knobs that can go below zero draw from their middle.
    bool bipolar() const { return kind != Kind::Choice && kind != Kind::Toggle && min < 0 && max > 0; }
};

struct TypeInfo {
    const char* name;
    std::vector<ParamInfo> params;
    // For the browser: its group ("Pitch", "Rhythm", "Feel"; "Motion" etc. for modulators), what it does in a few
    // words, and other words people search for ("delay" finds Echo).
    const char* group = "";
    const char* description = "";
    const char* keywords = "";
};

const TypeInfo& deviceInfo(DeviceType type);
const TypeInfo& modulatorInfo(ModulatorType type);
/// The parameter list a reference points into, or nullptr when the type is unknown.
const ParamInfo* paramInfo(DeviceType type, int param);
const ParamInfo* paramInfo(ModulatorType type, int param);

/// Normalized (0..1) to display units and back. Integers and choices land on whole values.
float display(const ParamInfo& p, float normalized);
float normalize(const ParamInfo& p, float value);
float defaultNormalized(const ParamInfo& p);
/// "+3 st", "75%", "1/16", "On": a knob's value as the user reads it.
std::string format(const ParamInfo& p, float normalized);

/// A new device or modulator with every knob at its default and the type's name.
Device makeDevice(DeviceType type, DeviceId id);
Modulator makeModulator(ModulatorType type, ModulatorId id);

/// Rates and lengths offered by synced knobs, from 8 bars down to 1/64, with triplets and dotted values.
const std::vector<std::string>& rateNames();
/// The length of rate choice `index` in ticks (bars follow the time signature).
Tick rateTicks(int index, TimeSig ts);

} // namespace tessera::devices
