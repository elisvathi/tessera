#pragma once

#include "Axes.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

/// Small pieces of navigation behavior shared by every view (EDITOR_UX §2-3). No Rack dependency.
namespace tessera {

/// Back / forward through zoom-to-* jumps, like a browser history.
class ZoomHistory {
  public:
    /// Records the view before a jump. Anything "forward" of the current point is dropped.
    void push(TimeAxis::State before) {
        entries.resize(position);
        entries.push_back(before);
        position = entries.size();
    }
    /// Returns the view to go back to; `current` becomes reachable with forward().
    std::optional<TimeAxis::State> back(TimeAxis::State current) {
        if (position == 0)
            return std::nullopt;
        if (position == entries.size())
            entries.push_back(current);
        return entries[--position];
    }
    std::optional<TimeAxis::State> forward() {
        if (position + 1 >= entries.size())
            return std::nullopt;
        return entries[++position];
    }

  private:
    std::vector<TimeAxis::State> entries;
    size_t position = 0;
};

enum class FollowMode : int { Off, Page, Continuous };

/// Following pauses while the user works the view, then resumes this long after they stop.
constexpr double kFollowPauseSeconds = 2.0;

/// Where the view's left edge should go to keep the playhead in sight, or nothing to leave it be.
/// Page jumps a screen at a time; Continuous holds the playhead at a third of the width.
inline std::optional<double> followScroll(FollowMode mode, double playhead, const TimeAxis& axis) {
    // NaN: the shown clip isn't playing.
    if (!std::isfinite(playhead))
        return std::nullopt;
    double visible = axis.visibleTicks();
    switch (mode) {
    case FollowMode::Off:
        return std::nullopt;
    case FollowMode::Page:
        if (playhead >= axis.left() && playhead < axis.right())
            return std::nullopt;
        return playhead;
    case FollowMode::Continuous:
        return playhead - visible / 3;
    }
    return std::nullopt;
}

/// Edge auto-scroll while dragging: speed in px per second for one axis, from the pointer position.
/// Zero away from the edges, rising to `maxSpeed` at `band` px into the edge zone, and past the edge.
inline double autoScrollSpeed(double pos, double size, double band = 24, double maxSpeed = 1500) {
    // A small view (the panel editor) scrolls only once the pointer leaves it: an edge zone inside it would
    // cover most of its height and scroll the content away during any drag.
    double zone = size >= 8 * band ? band : 0;
    if (pos < zone)
        return -maxSpeed * std::min(1.0, (zone - pos) / band);
    if (pos > size - zone)
        return maxSpeed * std::min(1.0, (pos - (size - zone)) / band);
    return 0;
}

/// Shift-drag constrains movement to one axis, decided by the first few pixels of movement.
class AxisLock {
  public:
    static constexpr double kDecidePx = 4;
    enum class Axis { Undecided, Horizontal, Vertical };

    void reset() { axis = Axis::Undecided; }

    /// Returns the drag offset with the locked axis zeroed. Without Shift the offset passes through.
    void apply(double& dx, double& dy, bool shift) {
        if (!shift) {
            axis = Axis::Undecided;
            return;
        }
        if (axis == Axis::Undecided && std::hypot(dx, dy) >= kDecidePx)
            axis = std::abs(dx) >= std::abs(dy) ? Axis::Horizontal : Axis::Vertical;
        if (axis == Axis::Horizontal)
            dy = 0;
        else if (axis == Axis::Vertical)
            dx = 0;
        else
            dx = dy = 0;
    }
    Axis current() const { return axis; }

  private:
    Axis axis = Axis::Undecided;
};

/// Zoom factor for a scroll-wheel or trackpad delta. Small trackpad deltas give small, smooth steps.
inline double wheelZoomFactor(double delta) { return std::exp(delta * 0.004); }

} // namespace tessera
