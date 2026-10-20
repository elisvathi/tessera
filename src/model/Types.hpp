#pragma once

#include <cstdint>
#include <functional>

namespace tessera {

using Tick = int64_t;

constexpr Tick kPpq = 960;
constexpr int kMaxTracks = 64;
constexpr int kMaxVoices = 16;
constexpr int kMaxAutomationLanes = 8;
constexpr int kMaxKnobLanes = 16; // lanes that turn device knobs, per track and per clip

/// A strongly typed ID. The Tag keeps a TrackId from being passed where a ClipId is expected.
/// Value 0 means "no ID".
template <typename Tag>
struct Id {
    uint64_t value = 0;

    constexpr Id() = default;
    constexpr explicit Id(uint64_t v) : value(v) {}

    constexpr bool valid() const { return value != 0; }
    constexpr bool operator==(Id o) const { return value == o.value; }
    constexpr bool operator!=(Id o) const { return value != o.value; }
    constexpr bool operator<(Id o) const { return value < o.value; }
};

using TrackId = Id<struct TrackTag>;
using ClipId = Id<struct ClipTag>;
using NoteId = Id<struct NoteTag>;
using ArrangementId = Id<struct ArrangementTag>;
using AutomationId = Id<struct AutomationTag>;
using GroupId = Id<struct GroupTag>;
using MarkerId = Id<struct MarkerTag>;
using DeviceId = Id<struct DeviceTag>;
using ModulatorId = Id<struct ModulatorTag>;

struct TimeSig {
    int num = 4;
    int den = 4;

    bool operator==(const TimeSig& o) const { return num == o.num && den == o.den; }
    bool operator!=(const TimeSig& o) const { return !(*this == o); }
};

constexpr Tick ticksPerBeat(TimeSig ts) { return kPpq * 4 / ts.den; }
constexpr Tick ticksPerBar(TimeSig ts) { return ticksPerBeat(ts) * ts.num; }

/// Bars, beats and ticks are 1-based for display ("1.1.0" is the song start).
struct BarBeatTick {
    int64_t bar;
    int beat;
    Tick tick;
};

inline BarBeatTick toBarBeatTick(Tick t, TimeSig ts) {
    Tick bar = ticksPerBar(ts);
    Tick beat = ticksPerBeat(ts);
    // Floor division so negative positions (count-in) still read sensibly.
    int64_t barIndex = t >= 0 ? t / bar : -((-t + bar - 1) / bar);
    Tick inBar = t - barIndex * bar;
    return {barIndex + 1, int(inBar / beat) + 1, inBar % beat};
}

} // namespace tessera

namespace std {
template <typename Tag>
struct hash<tessera::Id<Tag>> {
    size_t operator()(tessera::Id<Tag> id) const noexcept { return hash<uint64_t>()(id.value); }
};
} // namespace std
