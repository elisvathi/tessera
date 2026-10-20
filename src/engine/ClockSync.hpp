#pragma once

#include "../model/Types.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace tessera {

/// Follows an external clock (DESIGN.md §7): pulses at `ppqn` per quarter note set the song position, so it
/// never drifts from the clock, and the time between pulses is filled in smoothly at the measured tempo.
///
/// Pulse k after a reset is tick k * (960 / ppqn). Between pulses the position moves at the averaged pulse rate
/// but never past the next pulse's tick, so a slowing or stopped clock holds the song there; a pulse that comes
/// early jumps it forward. Audio-thread safe: no allocation, plain arithmetic.
class ClockSync {
  public:
    static constexpr int kAverage = 8; // pulses averaged for the tempo, smoothing jitter

    void setPpqn(int value) { ppqn = std::clamp(value, 1, int(kPpq)); }
    int pulsesPerQuarter() const { return ppqn; }

    /// The next pulse is tick 0 again (transport start, RESET).
    void resetPhase() {
        pulses = 0;
        position = 0;
    }

    /// One sample. `pulse` is a rising edge on this sample. Returns how many ticks the song moves on this sample.
    double step(bool pulse) {
        samplesSince++;
        if (pulse) {
            if (seenPulse && samplesSince > 0)
                addInterval(double(samplesSince));
            seenPulse = true;
            samplesSince = 0;
            double target = double(pulses) * tickPerPulse();
            pulses++;
            double moved = std::max(0.0, target - position);
            position = std::max(position, target);
            return moved;
        }
        if (!running() || pulses == 0)
            return 0;
        // Towards the next pulse's tick at the measured rate, stopping just short of it.
        double limit = double(pulses) * tickPerPulse();
        double rate = tickPerPulse() / interval;
        double next = std::min(position + rate, limit);
        double moved = next - position;
        position = next;
        return moved;
    }

    /// The clock's tempo, once two pulses have come in; 0 before.
    double bpm(double sampleRate) const {
        return count > 0 && interval > 0 ? 60.0 * sampleRate / (interval * double(ppqn)) : 0;
    }
    /// Pulses are arriving: the last one came no more than four average intervals ago.
    bool running() const { return count > 0 && double(samplesSince) < 4 * interval; }

  private:
    double tickPerPulse() const { return double(kPpq) / double(ppqn); }

    void addInterval(double samples) {
        // A gap far longer than usual is the clock restarting, not a slower tempo.
        if (count > 0 && samples > 8 * interval) {
            count = 0;
            sum = 0;
            head = 0;
        }
        if (count == kAverage) {
            sum -= intervals[size_t(head)];
        } else {
            count++;
        }
        intervals[size_t(head)] = samples;
        sum += samples;
        head = (head + 1) % kAverage;
        interval = sum / double(count);
    }

    int ppqn = 24;
    int64_t pulses = 0;  // since the last reset
    double position = 0; // ticks since the last reset
    int64_t samplesSince = 0;
    bool seenPulse = false;
    std::array<double, kAverage> intervals{};
    int head = 0, count = 0;
    double sum = 0, interval = 0; // average samples between pulses
};

} // namespace tessera
