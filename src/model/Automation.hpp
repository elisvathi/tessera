#pragma once

#include "ParamRef.hpp"
#include "Types.hpp"

#include <string>
#include <vector>

namespace tessera {

/// Shape of the segment that starts at a point.
enum class CurveType : uint8_t { Hold, Linear, Exponential, SCurve };

struct AutoPoint {
    Tick t = 0;
    float value = 0; // normalized 0..1
    CurveType curve = CurveType::Linear;
    float tension = 0; // -1..1, bends the segment

    bool operator==(const AutoPoint& o) const {
        return t == o.t && value == o.value && curve == o.curve && tension == o.tension;
    }
};

/// A knob on another module that a lane drives directly, without a cable.
struct ParamTarget {
    int64_t moduleId = -1;
    int paramId = 0;
    float min = 0; // sub-range of the target param, normalized 0..1
    float max = 1;

    bool operator==(const ParamTarget& o) const {
        return moduleId == o.moduleId && paramId == o.paramId && min == o.min && max == o.max;
    }
};

/// How clip automation combines with track automation on the same output.
enum class CombineMode : uint8_t { Override, Modulate };

/// What an armed lane records from: its first mapped knob, or its channel of Tessera's AUTO REC input.
enum class AutoSource : uint8_t { Knob, Cv };
/// Touch: writes while the source moves, then the old curve comes back. Latch: from the first move until
/// recording stops. Write: the whole time.
enum class AutoRecordMode : uint8_t { Touch, Latch, Write };

/// Where a lane's value goes: an AUTO channel, or into every voice of the track (pitch bend added to V/OCT,
/// pressure on PRESS where it is above the note's own).
enum class LaneDrive : uint8_t { Auto, Pitch, Pressure };

/// The MIDI control a lane records from (docs/design/EXPRESSION_INPUT.md).
struct MidiControl {
    enum class Kind : uint8_t { None, PitchBend, Pressure, Cc };
    Kind kind = Kind::None;
    uint8_t cc = 0; // 0..127, for Kind::Cc

    bool valid() const { return kind != Kind::None; }
    bool operator==(const MidiControl& o) const { return kind == o.kind && (kind != Kind::Cc || cc == o.cc); }
    bool operator!=(const MidiControl& o) const { return !(*this == o); }
};

struct AutomationEnvelope {
    AutomationId id;
    std::string name;
    uint32_t color = 0;
    int outputIndex = 0; // AUTO channel on the track's output modules, 0..7
    float rangeMin = 0;  // output volts at value 0
    float rangeMax = 10; // output volts at value 1
    bool bipolar = false;
    bool mute = false;
    float slewMs = 0; // 0..100
    CombineMode combine = CombineMode::Override;
    bool armed = false; // records while the transport records
    AutoSource source = AutoSource::Knob;
    AutoRecordMode recordMode = AutoRecordMode::Touch;
    std::vector<ParamTarget> targets; // up to 4
    std::vector<AutoPoint> points;    // sorted by t
    /// A lane that turns a device knob or modulator setting (docs/DEVICES.md §1.3) instead of driving an AUTO
    /// channel. A target that no longer exists leaves the lane in place, showing "target missing".
    ParamRef paramTarget;
    LaneDrive drive = LaneDrive::Auto;
    MidiControl midi;         // the control a take records into this lane
    bool midiIgnored = false; // "Don't record this control": takes leave the lane as it is

    bool operator==(const AutomationEnvelope& o) const {
        return id == o.id && name == o.name && color == o.color && outputIndex == o.outputIndex &&
               rangeMin == o.rangeMin && rangeMax == o.rangeMax && bipolar == o.bipolar && mute == o.mute &&
               slewMs == o.slewMs && combine == o.combine && armed == o.armed && source == o.source &&
               recordMode == o.recordMode && targets == o.targets && points == o.points && paramTarget == o.paramTarget &&
               drive == o.drive && midi == o.midi && midiIgnored == o.midiIgnored;
    }
};

/// Whether a lane drives an AUTO channel; a knob lane (paramTarget) and a pitch or pressure lane have none.
inline bool drivesOutput(const AutomationEnvelope& lane) {
    return !lane.paramTarget.valid() && lane.drive == LaneDrive::Auto;
}
/// Whether two lanes play the same thing: the same knob, the same AUTO channel, or both the pitch or pressure.
inline bool sameTarget(const AutomationEnvelope& a, const AutomationEnvelope& b) {
    if (a.paramTarget.valid() || b.paramTarget.valid())
        return a.paramTarget == b.paramTarget;
    if (a.drive != b.drive)
        return false;
    return a.drive != LaneDrive::Auto || a.outputIndex == b.outputIndex;
}

/// The value a lane rests at with no points: the middle for bipolar lanes, the bottom otherwise.
inline float restValue(const AutomationEnvelope& lane) { return lane.bipolar ? 0.5f : 0.f; }

/// Where a segment of `curve` with `tension` is at `x` (0..1 across the segment), also 0..1. Tension -1..1 bends
/// it: positive pushes the curve toward the end value early (above a rising line).
double curveShape(CurveType curve, float tension, double x);

/// The lane's value (0..1) at tick `t`. Before the first point it holds the first value, after the last the
/// last value; with no points it is restValue().
float evaluate(const AutomationEnvelope& lane, double t);
/// The same, for playback: `cursor` remembers the segment between calls, so moving forward costs O(1).
float evaluateAt(const std::vector<AutoPoint>& points, double t, size_t& cursor, float rest);

/// Lane value to output volts, and back.
inline float toVolts(const AutomationEnvelope& lane, float value) {
    return lane.rangeMin + value * (lane.rangeMax - lane.rangeMin);
}
inline float fromVolts(const AutomationEnvelope& lane, float volts) {
    float span = lane.rangeMax - lane.rangeMin;
    return span != 0 ? (volts - lane.rangeMin) / span : 0.f;
}

} // namespace tessera
