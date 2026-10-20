#include "AutomationTake.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

void AutomationTake::sample(Tick tick, float value, double seconds) {
    bool changed = seen && std::fabs(value - last) > 1e-4f;
    if (changed)
        lastChange = seconds;
    seen = true;
    last = value;
    bool was = active;
    switch (mode) {
    case AutoRecordMode::Write:
        active = true;
        break;
    case AutoRecordMode::Latch:
        latched |= changed;
        active = latched;
        break;
    case AutoRecordMode::Touch:
    default:
        active = seconds - lastChange <= kTouchRelease;
        break;
    }
    if (!active)
        return;
    if (!was || runs.empty())
        runs.emplace_back();
    AutoPoint p;
    p.t = std::max<Tick>(0, tick);
    p.value = std::clamp(value, 0.f, 1.f);
    // Readings are in time order; a reading at the same tick replaces the last one.
    if (!runs.back().empty() && runs.back().back().t >= p.t)
        runs.back().back() = p;
    else
        runs.back().push_back(p);
}

autoEdits::Points AutomationTake::apply(const autoEdits::Points& points) const {
    autoEdits::Points out = points;
    for (const autoEdits::Points& run : runs) {
        if (run.empty())
            continue;
        autoEdits::Points thinned = autoEdits::simplify(run, kThinTolerance);
        out = autoEdits::replaceRange(out, run.front().t, run.back().t, thinned);
    }
    return out;
}

} // namespace tessera
