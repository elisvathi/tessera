#pragma once

#include "../../model/Types.hpp"

#include <string>
#include <vector>

/// Snapping for every editor (EDITOR_UX §4.4). No Rack dependency, so it is unit tested.
namespace tessera {

enum class GridDivision : int { Bar, Half, Quarter, Eighth, Sixteenth, ThirtySecond, SixtyFourth };
enum class GridFeel : int { Straight, Triplet, Dotted };

struct SnapSetting {
    bool enabled = true;
    bool adaptive = true; // the grid follows the zoom; `division` is used when this is off
    GridDivision division = GridDivision::Sixteenth;
    GridFeel feel = GridFeel::Straight;
    bool events = false; // also snap to other notes' starts and ends (magnetic)

    bool operator==(const SnapSetting& o) const {
        return enabled == o.enabled && adaptive == o.adaptive && division == o.division && feel == o.feel &&
               events == o.events;
    }
};

/// Ticks of one grid step for a fixed division and feel.
Tick divisionTicks(GridDivision division, GridFeel feel, TimeSig ts);

/// The snap step at the current zoom, or 0 when snapping is off.
Tick snapStep(const SnapSetting& snap, double pxPerTick, TimeSig ts);

/// The nearest multiple of `step` (unchanged when step is 0).
Tick snapPosition(double tick, Tick step);

/// The move offset for a note that started at `start` and was dragged by `delta`. The start lands on the grid,
/// also for a note that started off it, but never moves against the drag: an off-grid note stays put until
/// the pointer is past the middle of the way to the next grid line.
Tick snapMoveDelta(Tick start, double delta, Tick step);

/// Magnetic snap: the nearest of `targets` within `tolerance` ticks, else the grid.
Tick snapToEvents(double tick, const std::vector<Tick>& targets, double tolerance, Tick step);

// Ctrl/Cmd+1..4: narrower and wider grid, triplet on/off, snap on/off.
SnapSetting narrower(SnapSetting s);
SnapSetting wider(SnapSetting s);
SnapSetting toggledTriplet(SnapSetting s);
SnapSetting toggledSnap(SnapSetting s);

/// "1/16", "1/8T", "Adaptive", "Off".
std::string snapLabel(const SnapSetting& s);
std::string divisionLabel(GridDivision division, GridFeel feel);

} // namespace tessera
