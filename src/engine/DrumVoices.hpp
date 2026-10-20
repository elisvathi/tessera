#pragma once

#include "../model/Track.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace tessera {

/// The outputs of a drum track's lanes, counted in samples. A hit raises the lane's gate for a fixed time
/// (a 1 ms trigger or the lane's gate length). A lane hit again while high drops low for the retrigger gap
/// first, so the drum module downstream sees a new edge.
class DrumVoices {
  public:
    void setRetriggerGap(int samples) { gap = std::max(1, samples); }

    void hit(int lane, float velocity, int samples) {
        busy = true;
        changes++;
        Lane& l = lanes[size_t(lane)];
        l.velocity = velocity;
        if (l.high > 0 || l.wait > 0) {
            l.high = 0;
            l.wait = gap;
            l.pending = std::max(1, samples);
        } else {
            l.high = std::max(1, samples);
        }
    }
    /// Choke: the lane goes low at once.
    void cut(int lane) {
        lanes[size_t(lane)] = Lane{0, 0, 0, lanes[size_t(lane)].velocity};
        changes++;
    }
    /// Counts every change to what the lanes output.
    uint64_t changeCount() const { return changes; }
    void releaseAll() {
        for (int i = 0; i < kMaxDrumLanes; i++)
            cut(i);
    }
    /// Call once per sample, after reading the outputs.
    void advance() {
        // Instrument tracks have drum voices too: with every lane quiet there is nothing to count down.
        if (!busy)
            return;
        busy = false;
        for (Lane& l : lanes) {
            if (l.wait > 0) {
                if (--l.wait == 0) {
                    l.high = l.pending;
                    changes++;
                }
            } else if (l.high > 0) {
                if (--l.high == 0)
                    changes++;
            }
            busy |= l.wait > 0 || l.high > 0;
        }
    }

    /// Some lane is high or waiting to retrigger, so advance() has counting to do.
    bool counting() const { return busy; }
    bool gate(int lane) const { return lanes[size_t(lane)].high > 0; }
    float velocity(int lane) const { return lanes[size_t(lane)].velocity; }
    bool anyGate() const {
        return busy && std::any_of(lanes.begin(), lanes.end(), [](const Lane& l) { return l.high > 0; });
    }

  private:
    struct Lane {
        int32_t high = 0;    // samples the gate stays high
        int32_t wait = 0;    // samples low before a retrigger goes high
        int32_t pending = 0; // the retrigger's length
        float velocity = 0;
    };
    // Read every sample, so they come before the lanes.
    bool busy = false; // some lane is high or waiting
    uint64_t changes = 0;
    int gap = 48;
    std::array<Lane, kMaxDrumLanes> lanes{};
};

} // namespace tessera
