#pragma once

#include <array>

namespace tessera {

/// Turns button taps into a tempo: the average of the last few intervals. A pause longer than
/// kTimeout starts a new measurement, so one stray tap never yields a silly tempo.
class TapTempo {
  public:
    static constexpr double kTimeout = 2.0; // seconds
    static constexpr int kMaxIntervals = 4;

    /// Returns the new tempo in BPM, or 0 when there aren't enough taps yet.
    double tap(double timeSeconds) {
        double interval = timeSeconds - lastTap;
        lastTap = timeSeconds;
        if (!hasLast || interval <= 0 || interval > kTimeout) {
            hasLast = true;
            count = 0;
            next = 0;
            return 0;
        }
        intervals[next] = interval;
        next = (next + 1) % kMaxIntervals;
        if (count < kMaxIntervals)
            count++;
        double sum = 0;
        for (int i = 0; i < count; i++)
            sum += intervals[i];
        return 60.0 * count / sum;
    }

  private:
    std::array<double, kMaxIntervals> intervals{};
    int count = 0;
    int next = 0;
    double lastTap = 0;
    bool hasLast = false;
};

} // namespace tessera
