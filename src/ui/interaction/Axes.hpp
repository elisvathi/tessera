#pragma once

#include "../../model/Types.hpp"

#include <vector>

/// Coordinate math for every editor view: time <-> x, pitch <-> y, track <-> y, with zoom and scroll.
/// No Rack dependency, so the anchoring math is unit tested. Positions are view-local pixels.
namespace tessera {

/// Maps ticks to x. `left` is the tick at the view's left edge.
class TimeAxis {
  public:
    void setViewWidth(double px) {
        width = px > 1 ? px : 1;
        clamp();
    }
    double viewWidth() const { return width; }

    /// Zoom range in pixels per tick.
    void setZoomLimits(double minPxPerTick, double maxPxPerTick);
    /// The last tick worth scrolling to: the view stops scrolling right once this tick reaches the right edge.
    void setContentEnd(double tick) {
        contentEnd = tick;
        clamp();
    }

    double pxPerTick() const { return ppt; }
    double left() const { return leftTick; }
    double right() const { return leftTick + width / ppt; }
    double visibleTicks() const { return width / ppt; }

    double tickToX(double tick) const { return (tick - leftTick) * ppt; }
    double xToTick(double x) const { return leftTick + x / ppt; }

    /// Multiplies the zoom by `factor`, keeping the tick under `x` in place.
    void zoomAt(double x, double factor);
    void setZoom(double pxPerTick, double anchorX);
    void scrollByPx(double dx) { scrollTo(leftTick + dx / ppt); }
    void scrollTo(double tick) {
        leftTick = tick;
        clamp();
    }
    /// Fits [start, end) into the view with a small margin on both sides.
    void showRange(double start, double end);

    /// Eased zoom for wheel and keyboard input. Repeated calls during an animation compound.
    void animateZoomAt(double x, double factor, double seconds);
    /// Advances the animation. Returns true while it is still running.
    bool step(double dt);
    bool animating() const { return anim.active; }

    struct State {
        double left;
        double pxPerTick;
        bool operator==(const State& o) const { return left == o.left && pxPerTick == o.pxPerTick; }
    };
    State state() const { return {leftTick, ppt}; }
    void restore(State s);

  private:
    void clamp();

    double width = 800;
    double ppt = 0.05;
    double leftTick = 0;
    double minPpt = 1e-4;
    double maxPpt = 1;
    double contentEnd = 1e9;

    struct Animation {
        bool active = false;
        double anchorX = 0, anchorTick = 0;
        double fromPpt = 0, toPpt = 0;
        double elapsed = 0, duration = 0;
    } anim;
};

/// Which grid lines to draw for the current zoom.
struct GridSpacing {
    Tick line;  // spacing of the finest visible lines, at least `minPx` apart
    Tick label; // spacing of ruler labels, at least `minLabelPx` apart
};

GridSpacing gridSpacing(double pxPerTick, TimeSig ts, double minPx = 6, double minLabelPx = 40);

enum class LineStrength { Sub, Beat, Bar };
LineStrength lineStrength(Tick tick, TimeSig ts);

/// Snaps a tick to the nearest multiple of `step`.
double snapTick(double tick, Tick step);

/// Maps pitches to rows, top row = highest pitch. Row height is the vertical zoom.
/// Rows can be folded to a subset of pitches (scale fold, drum lanes); then a row is one shown pitch.
class PitchAxis {
  public:
    static constexpr double kMinRow = 6;
    static constexpr double kMaxRow = 40;
    static constexpr int kRows = 128;

    PitchAxis();

    void setViewHeight(double px) {
        height = px > 1 ? px : 1;
        clamp();
    }
    double viewHeight() const { return height; }
    double rowHeight() const { return row; }
    double top() const { return topRow; } // in rows from the top row

    /// Shows only these pitches (any order, 0..127). Empty shows all 128. The pitch at the middle of the view
    /// stays where it is.
    void setShownPitches(const std::vector<int>& pitches);
    bool folded() const { return int(rowPitch.size()) < kRows; }
    int rowCount() const { return int(rowPitch.size()); }
    /// The pitch of a row; rows past either end give the first or last row's pitch.
    int pitchAtRow(int r) const;
    /// The row a pitch is drawn on. A hidden pitch shares the row of the nearest shown pitch below it.
    int rowOf(int pitch) const;
    bool shown(int pitch) const { return pitch >= 0 && pitch < kRows && rowPitch[size_t(rowOf(pitch))] == pitch; }
    /// The row containing y (may be outside 0..rowCount-1).
    int yToRow(double y) const;
    /// Moves a pitch by whole rows: semitones, or shown pitches when folded. Stops at the first and last rows.
    int stepRows(int pitch, int rows) const;

    double pitchToY(int pitch) const { return (rowOf(pitch) - topRow) * row; }
    /// The pitch whose row contains y. Unfolded, it may be outside 0..127 when y is off the content; folded,
    /// it is the nearest shown pitch.
    int yToPitch(double y) const;

    /// The middle of the row of a pitch that may sit between semitones (a pitch bend). When folded, a pitch between
    /// two shown rows sits part way between them, so a glide crosses the hidden pitches smoothly.
    double bentPitchToY(double pitch) const;
    /// The inverse of bentPitchToY: the pitch, between semitones, at y. Past either end it is that end's pitch.
    double yToBentPitch(double y) const;

    void zoomAt(double y, double factor);
    void scrollByPx(double dy) {
        topRow += dy / row;
        clamp();
    }
    /// Centers the view on [low, high], zooming in or out as far as needed to show it, but keeping rows at least
    /// `minRow` tall: past that the range overflows the view, centred, and the rest is a scroll away.
    void showPitches(int low, int high, double minRow = kMinRow);

    struct State {
        double top;
        double rowHeight;
        bool operator==(const State& o) const { return top == o.top && rowHeight == o.rowHeight; }
    };
    State state() const { return {topRow, row}; }
    void restore(State s);

  private:
    void clamp();

    double height = 400;
    double row = 12;
    double topRow = 40;
    std::vector<int> rowPitch;    // top to bottom
    std::vector<int> rowForPitch; // 128 entries
};

/// Stacks tracks of different heights. Track height is the arranger's vertical zoom.
class TrackAxis {
  public:
    static constexpr double kMinHeight = 24;
    static constexpr double kMaxHeight = 240;

    void setViewHeight(double px) {
        height = px > 1 ? px : 1;
        clamp();
    }
    void setHeights(std::vector<double> heights);
    const std::vector<double>& heights() const { return trackHeights; }

    double scroll() const { return scrollPx; }
    double trackTop(int index) const;
    double trackHeight(int index) const { return trackHeights[size_t(index)]; }
    /// The track under y, or -1 below the last track.
    int trackAt(double y) const;
    double contentHeight() const;
    /// Extra room after the last track that scrolling can still reach (the "add track" row).
    void setFooter(double px) {
        footer = px;
        clamp();
    }

    void scrollByPx(double dy) {
        scrollPx += dy;
        clamp();
    }
    void scrollTo(double px) {
        scrollPx = px;
        clamp();
    }
    /// Scales every track height, keeping the content under `y` in place.
    void zoomAt(double y, double factor);
    /// Scrolls to `px` even past the last track, so a zoom can keep the spot under the mouse when the tracks fit
    /// the view. That empty room stays reachable only until scrolling or zooming takes it back.
    void anchorScroll(double px);
    /// Sizes all tracks equally so they fill the view (within the height limits).
    void fitAll();

  private:
    void clamp();

    double height = 400;
    double scrollPx = 0;
    double footer = 0;
    double slack = 0; // how far past the last track anchorScroll() may leave the view
    std::vector<double> trackHeights;
};

} // namespace tessera
