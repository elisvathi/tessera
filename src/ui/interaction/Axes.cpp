#include "Axes.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

// ---- TimeAxis -----------------------------------------------------------------------------------------------

void TimeAxis::setZoomLimits(double minPxPerTick, double maxPxPerTick) {
    minPpt = minPxPerTick;
    maxPpt = std::max(minPxPerTick, maxPxPerTick);
    clamp();
}

void TimeAxis::clamp() {
    // A NaN would reach every position drawn from then on and never recover.
    if (!std::isfinite(ppt))
        ppt = minPpt;
    if (!std::isfinite(leftTick))
        leftTick = 0;
    ppt = std::clamp(ppt, minPpt, maxPpt);
    double maxLeft = std::max(0.0, contentEnd - visibleTicks());
    leftTick = std::clamp(leftTick, 0.0, maxLeft);
}

void TimeAxis::zoomAt(double x, double factor) { setZoom(ppt * factor, x); }

void TimeAxis::setZoom(double pxPerTick, double anchorX) {
    double anchorTick = xToTick(anchorX);
    ppt = std::clamp(pxPerTick, minPpt, maxPpt);
    leftTick = anchorTick - anchorX / ppt;
    clamp();
}

void TimeAxis::showRange(double start, double end) {
    if (end <= start)
        end = start + 1;
    double margin = (end - start) * 0.02;
    ppt = std::clamp(width / (end - start + 2 * margin), minPpt, maxPpt);
    // Center the range when the zoom limit stops it from filling the view.
    double center = (start + end) / 2;
    leftTick = center - visibleTicks() / 2;
    clamp();
}

void TimeAxis::animateZoomAt(double x, double factor, double seconds) {
    double target = std::clamp((anim.active ? anim.toPpt : ppt) * factor, minPpt, maxPpt);
    anim.active = true;
    anim.anchorX = x;
    anim.anchorTick = xToTick(x);
    anim.fromPpt = ppt;
    anim.toPpt = target;
    anim.elapsed = 0;
    anim.duration = seconds;
}

bool TimeAxis::step(double dt) {
    if (!anim.active)
        return false;
    anim.elapsed += dt;
    double t = anim.duration > 0 ? std::min(1.0, anim.elapsed / anim.duration) : 1.0;
    double eased = 1 - std::pow(1 - t, 3);
    // Interpolate zoom geometrically, so each frame feels like the same amount of zoom.
    ppt = anim.fromPpt * std::pow(anim.toPpt / anim.fromPpt, eased);
    leftTick = anim.anchorTick - anim.anchorX / ppt;
    clamp();
    if (t >= 1)
        anim.active = false;
    return anim.active;
}

void TimeAxis::restore(State s) {
    anim.active = false;
    if (s.pxPerTick > 0)
        ppt = s.pxPerTick;
    leftTick = s.left;
    clamp();
}

// ---- Grid ---------------------------------------------------------------------------------------------------

namespace {

/// Every spacing a grid can use, finest first. All divide a bar or are whole bars, so lines always line up.
std::vector<Tick> candidateSpacings(TimeSig ts) {
    Tick beat = ticksPerBeat(ts);
    Tick bar = ticksPerBar(ts);
    std::vector<Tick> out = {beat / 16, beat / 8, beat / 4, beat / 2, beat};
    for (Tick b = bar; b <= bar * 1024; b *= 2)
        out.push_back(b);
    out.erase(std::remove_if(out.begin(), out.end(), [](Tick t) { return t < 1; }), out.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace

GridSpacing gridSpacing(double pxPerTick, TimeSig ts, double minPx, double minLabelPx) {
    std::vector<Tick> candidates = candidateSpacings(ts);
    auto first = [&](double px, Tick atLeast) {
        for (Tick c : candidates)
            if (c >= atLeast && c * pxPerTick >= px)
                return c;
        return candidates.back();
    };
    GridSpacing g;
    g.line = first(minPx, 1);
    // Labels never get finer than a sixteenth.
    g.label = first(minLabelPx, ticksPerBeat(ts) / 4);
    return g;
}

LineStrength lineStrength(Tick tick, TimeSig ts) {
    if (tick % ticksPerBar(ts) == 0)
        return LineStrength::Bar;
    if (tick % ticksPerBeat(ts) == 0)
        return LineStrength::Beat;
    return LineStrength::Sub;
}

double snapTick(double tick, Tick step) {
    if (step <= 0)
        return tick;
    return std::round(tick / double(step)) * double(step);
}

// ---- PitchAxis ----------------------------------------------------------------------------------------------

PitchAxis::PitchAxis() { setShownPitches({}); }

void PitchAxis::setShownPitches(const std::vector<int>& pitches) {
    std::vector<int> rows;
    std::vector<bool> on(kRows, pitches.empty());
    for (int p : pitches)
        if (p >= 0 && p < kRows)
            on[size_t(p)] = true;
    for (int p = kRows - 1; p >= 0; p--)
        if (on[size_t(p)])
            rows.push_back(p);
    setRows(rows);
}

void PitchAxis::setRows(const std::vector<int>& topToBottom) {
    std::vector<int> rows;
    std::vector<bool> seen(kRows, false);
    for (int p : topToBottom)
        if (p >= 0 && p < kRows && !seen[size_t(p)]) {
            seen[size_t(p)] = true;
            rows.push_back(p);
        }
    if (rows.empty())
        for (int p = kRows - 1; p >= 0; p--)
            rows.push_back(p);
    if (rows == rowPitch)
        return;

    int centre = rowPitch.empty() ? -1 : yToPitch(height / 2);
    double centreOffset = rowPitch.empty() ? 0 : topRow + height / 2 / row - rowOf(centre);
    rowPitch = std::move(rows);
    std::vector<int> rowOfShown(kRows, -1);
    for (size_t r = 0; r < rowPitch.size(); r++)
        rowOfShown[size_t(rowPitch[r])] = int(r);
    rowForPitch.assign(kRows, 0);
    // Each pitch takes the row of the nearest shown pitch at or below it, or the lowest shown pitch's above it.
    int below = -1;
    for (int p = 0; p < kRows; p++) {
        if (rowOfShown[size_t(p)] >= 0)
            below = p;
        rowForPitch[size_t(p)] = below >= 0 ? rowOfShown[size_t(below)] : -1;
    }
    int lowest = 0;
    while (rowOfShown[size_t(lowest)] < 0)
        lowest++;
    for (int p = 0; p < lowest; p++)
        rowForPitch[size_t(p)] = rowOfShown[size_t(lowest)];
    if (centre >= 0)
        topRow = rowOf(centre) + centreOffset - height / 2 / row;
    clamp();
}

int PitchAxis::pitchAtRow(int r) const { return rowPitch[size_t(std::clamp(r, 0, rowCount() - 1))]; }

int PitchAxis::rowOf(int pitch) const { return rowForPitch[size_t(std::clamp(pitch, 0, kRows - 1))]; }

int PitchAxis::yToRow(double y) const { return int(std::floor(topRow + y / row)); }

int PitchAxis::yToPitch(double y) const {
    int r = yToRow(y);
    return folded() ? pitchAtRow(r) : kRows - 1 - r;
}

int PitchAxis::stepRows(int pitch, int rows) const {
    if (!folded())
        return std::clamp(pitch - rows, 0, kRows - 1);
    // A hidden pitch sits on the row below it, which already counts as one step down.
    if (!shown(pitch) && rows > 0)
        rows--;
    return pitchAtRow(rowOf(pitch) + rows);
}

double PitchAxis::bentPitchToY(double pitch) const {
    pitch = std::clamp(pitch, 0.0, double(kRows - 1));
    int low = int(std::floor(pitch)), high = int(std::ceil(pitch));
    while (low > 0 && !shown(low))
        low--;
    while (high < kRows - 1 && !shown(high))
        high++;
    if (!shown(low))
        low = high;
    if (!shown(high))
        high = low;
    double r = rowOf(low);
    if (high != low)
        r += (pitch - low) / double(high - low) * double(rowOf(high) - rowOf(low));
    return (r + 0.5 - topRow) * row;
}

double PitchAxis::yToBentPitch(double y) const {
    double r = topRow + y / row - 0.5;
    if (r <= 0)
        return pitchAtRow(0);
    if (r >= rowCount() - 1)
        return pitchAtRow(rowCount() - 1);
    int r0 = int(std::floor(r));
    double a = pitchAtRow(r0), b = pitchAtRow(r0 + 1);
    return a + (b - a) * (r - r0);
}

void PitchAxis::zoomAt(double y, double factor) {
    double anchorRow = topRow + y / row;
    row = std::clamp(row * factor, kMinRow, kMaxRow);
    topRow = anchorRow - y / row;
    clamp();
}

void PitchAxis::showPitches(int low, int high, double minRow) {
    if (high < low)
        std::swap(low, high);
    double count = rowOf(low) - rowOf(high) + 1;
    row = std::clamp(height / (count + 2), std::clamp(minRow, kMinRow, kMaxRow), kMaxRow);
    double visibleRows = height / row;
    topRow = rowOf(high) - (visibleRows - count) / 2;
    clamp();
}

void PitchAxis::showRowsFromTop(int first, int last, double minRow) {
    if (last < first)
        std::swap(first, last);
    double count = last - first + 1;
    row = std::clamp(height / (count + footer), std::clamp(minRow, kMinRow, kMaxRow), kMaxRow);
    topRow = first;
    clamp();
}

void PitchAxis::restore(State s) {
    if (s.rowHeight > 0)
        row = s.rowHeight;
    topRow = s.top;
    clamp();
}

void PitchAxis::clamp() {
    row = std::clamp(row, kMinRow, kMaxRow);
    double maxTop = std::max(0.0, rowCount() + footer - height / row);
    topRow = std::clamp(topRow, 0.0, maxTop);
}

// ---- TrackAxis ----------------------------------------------------------------------------------------------

void TrackAxis::setHeights(std::vector<double> h) {
    for (double& v : h)
        v = std::clamp(v, kMinHeight, kMaxHeight);
    trackHeights = std::move(h);
    clamp();
}

double TrackAxis::trackTop(int index) const {
    double y = -scrollPx;
    for (int i = 0; i < index && i < int(trackHeights.size()); i++)
        y += trackHeights[size_t(i)];
    return y;
}

int TrackAxis::trackAt(double y) const {
    double top = -scrollPx;
    for (size_t i = 0; i < trackHeights.size(); i++) {
        if (y >= top && y < top + trackHeights[i])
            return int(i);
        top += trackHeights[i];
    }
    return -1;
}

double TrackAxis::contentHeight() const {
    double sum = 0;
    for (double h : trackHeights)
        sum += h;
    return sum;
}

void TrackAxis::zoomAt(double y, double factor) {
    int index = trackAt(y);
    double fraction = 0;
    if (index >= 0)
        fraction = (y - trackTop(index)) / trackHeights[size_t(index)];
    for (double& h : trackHeights)
        h = std::clamp(h * factor, kMinHeight, kMaxHeight);
    if (index >= 0) {
        double contentY = trackTop(index) + scrollPx + fraction * trackHeights[size_t(index)];
        scrollPx = contentY - y;
    }
    clamp();
}

void TrackAxis::fitAll() {
    if (trackHeights.empty())
        return;
    double h = std::clamp(height / double(trackHeights.size()), kMinHeight, kMaxHeight);
    std::fill(trackHeights.begin(), trackHeights.end(), h);
    scrollPx = 0;
    clamp();
}

void TrackAxis::anchorScroll(double px) {
    scrollPx = std::max(0.0, px);
    slack = std::max(0.0, scrollPx - std::max(0.0, contentHeight() + footer - height));
    clamp();
}

void TrackAxis::clamp() {
    double end = std::max(0.0, contentHeight() + footer - height);
    scrollPx = std::clamp(scrollPx, 0.0, end + slack);
    // Room past the end is kept only as far as the view still uses it: scrolling back up gives it up.
    slack = std::max(0.0, scrollPx - end);
}

} // namespace tessera
