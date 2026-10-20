#pragma once

#include "AutomationEdits.hpp"

#include <vector>

namespace tessera {

/// One lane's recording pass (DESIGN.md §12 "Automation recording"): readings of its source go in, the lane's
/// new points come out. Pure, so the modes are unit tested.
class AutomationTake {
  public:
    /// Seconds without movement that end a touch.
    static constexpr double kTouchRelease = 0.5;
    /// How far (in lane values) recorded curves may stray when thinned.
    static constexpr float kThinTolerance = 0.004f;

    explicit AutomationTake(AutoRecordMode mode) : mode(mode) {}

    /// A reading of the source at `tick` (lane ticks), taken at wall-clock time `seconds`.
    void sample(Tick tick, float value, double seconds);
    /// Whether the last reading was written.
    bool writing() const { return active; }
    bool empty() const { return runs.empty(); }
    /// Whether a reading came in, and the last one.
    bool started() const { return seen; }
    float lastValue() const { return last; }
    /// What was written so far: one list per stretch of writing.
    const std::vector<autoEdits::Points>& written() const { return runs; }

    /// The lane's points with each written stretch put over the curve it covered, thinned.
    autoEdits::Points apply(const autoEdits::Points& points) const;

  private:
    AutoRecordMode mode;
    bool seen = false;
    bool latched = false;
    bool active = false;
    float last = 0;
    double lastChange = -1e9;
    std::vector<autoEdits::Points> runs;
};

} // namespace tessera
