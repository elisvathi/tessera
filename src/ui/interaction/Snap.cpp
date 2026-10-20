#include "Snap.hpp"

#include "Axes.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

Tick divisionTicks(GridDivision division, GridFeel feel, TimeSig ts) {
    Tick base = 0;
    switch (division) {
    case GridDivision::Bar:
        base = ticksPerBar(ts);
        break;
    case GridDivision::Half:
        base = kPpq * 2;
        break;
    case GridDivision::Quarter:
        base = kPpq;
        break;
    case GridDivision::Eighth:
        base = kPpq / 2;
        break;
    case GridDivision::Sixteenth:
        base = kPpq / 4;
        break;
    case GridDivision::ThirtySecond:
        base = kPpq / 8;
        break;
    case GridDivision::SixtyFourth:
        base = kPpq / 16;
        break;
    }
    switch (feel) {
    case GridFeel::Triplet:
        return base * 2 / 3;
    case GridFeel::Dotted:
        return base * 3 / 2;
    case GridFeel::Straight:
    default:
        return base;
    }
}

Tick snapStep(const SnapSetting& snap, double pxPerTick, TimeSig ts) {
    if (!snap.enabled)
        return 0;
    if (snap.adaptive) {
        Tick line = gridSpacing(pxPerTick, ts).line;
        return snap.feel == GridFeel::Triplet ? line * 2 / 3 : line;
    }
    return divisionTicks(snap.division, snap.feel, ts);
}

Tick snapPosition(double tick, Tick step) {
    if (step <= 0)
        return Tick(std::llround(tick));
    return Tick(std::llround(tick / double(step))) * step;
}

Tick snapMoveDelta(Tick start, double delta, Tick step) {
    if (step <= 0)
        return Tick(std::llround(delta));
    Tick moved = snapPosition(double(start) + delta, step) - start;
    if ((delta > 0 && moved < 0) || (delta < 0 && moved > 0))
        return 0;
    return moved;
}

Tick snapToEvents(double tick, const std::vector<Tick>& targets, double tolerance, Tick step) {
    Tick best = 0;
    double bestDistance = tolerance;
    bool found = false;
    for (Tick t : targets) {
        double d = std::fabs(double(t) - tick);
        if (d <= bestDistance) {
            bestDistance = d;
            best = t;
            found = true;
        }
    }
    return found ? best : snapPosition(tick, step);
}

namespace {

SnapSetting fixedFrom(SnapSetting s) {
    // Leaving adaptive: start from the 1/16 the user most likely sees.
    if (s.adaptive) {
        s.adaptive = false;
        s.division = GridDivision::Sixteenth;
    }
    s.enabled = true;
    return s;
}

} // namespace

SnapSetting narrower(SnapSetting s) {
    s = fixedFrom(s);
    s.division = GridDivision(std::min(int(s.division) + 1, int(GridDivision::SixtyFourth)));
    return s;
}

SnapSetting wider(SnapSetting s) {
    s = fixedFrom(s);
    s.division = GridDivision(std::max(int(s.division) - 1, int(GridDivision::Bar)));
    return s;
}

SnapSetting toggledTriplet(SnapSetting s) {
    s.feel = s.feel == GridFeel::Triplet ? GridFeel::Straight : GridFeel::Triplet;
    return s;
}

SnapSetting toggledSnap(SnapSetting s) {
    s.enabled = !s.enabled;
    return s;
}

std::string divisionLabel(GridDivision division, GridFeel feel) {
    static const char* const names[] = {"Bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64"};
    std::string s = names[int(division)];
    if (feel == GridFeel::Triplet)
        s += "T";
    else if (feel == GridFeel::Dotted)
        s += ".";
    return s;
}

std::string snapLabel(const SnapSetting& s) {
    if (!s.enabled)
        return "Snap off";
    std::string label = s.adaptive ? std::string(s.feel == GridFeel::Triplet ? "Adaptive T" : "Adaptive")
                                   : divisionLabel(s.division, s.feel);
    return s.events ? label + " + events" : label;
}

} // namespace tessera
