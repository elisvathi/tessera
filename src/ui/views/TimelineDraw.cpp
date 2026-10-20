#include "TimelineDraw.hpp"

#include "../Theme.hpp"

#include <cmath>

namespace tessera::timeline {

void drawGrid(NVGcontext* vg, const TimeAxis& axis, TimeSig ts, float height, Tick snap) {
    constexpr double kMinSnapLinePx = 4;
    const theme::Palette& c = theme::colors();
    Tick line = gridSpacing(axis.pxPerTick(), ts).line;
    if (snap > 0 && double(snap) * axis.pxPerTick() >= kMinSnapLinePx)
        line = snap;
    Tick first = Tick(std::floor(axis.left() / double(line))) * line;

    // One path per strength keeps the draw calls to three, whatever the zoom.
    const NVGcolor colors[3] = {c.line0, c.line1, c.line2};
    for (int strength = 0; strength < 3; strength++) {
        nvgBeginPath(vg);
        for (Tick t = first; t <= axis.right(); t += line) {
            if (int(lineStrength(t, ts)) != strength)
                continue;
            float x = std::round(float(axis.tickToX(double(t)))) + 0.5f;
            nvgMoveTo(vg, x, 0);
            nvgLineTo(vg, x, height);
        }
        nvgStrokeColor(vg, colors[strength]);
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
}

void drawPlayhead(NVGcontext* vg, float x, float height, bool cap) {
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, 0);
    nvgLineTo(vg, x, height);
    nvgStrokeColor(vg, c.accent);
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);
    if (cap) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x - 5, 0);
        nvgLineTo(vg, x + 5, 0);
        nvgLineTo(vg, x, 6);
        nvgClosePath(vg);
        nvgFillColor(vg, c.accent);
        nvgFill(vg);
    }
}

std::string positionLabel(Tick tick, Tick labelStep, TimeSig ts) {
    BarBeatTick bbt = toBarBeatTick(tick, ts);
    std::string s = std::to_string(bbt.bar);
    if (labelStep < ticksPerBar(ts))
        s += "." + std::to_string(bbt.beat);
    if (labelStep < ticksPerBeat(ts))
        s += "." + std::to_string(bbt.tick / (kPpq / 4) + 1);
    return s;
}

std::string positionText(double tick, TimeSig ts) {
    BarBeatTick bbt = toBarBeatTick(Tick(std::floor(tick)), ts);
    return std::to_string(bbt.bar) + "." + std::to_string(bbt.beat) + "." + std::to_string(bbt.tick / (kPpq / 4) + 1);
}

void drawOffscreenArrow(NVGcontext* vg, bool right, float width, float y) {
    const theme::Palette& c = theme::colors();
    float x = right ? width - 4 : 4;
    float d = right ? -1.f : 1.f;
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, y);
    nvgLineTo(vg, x + d * 9, y - 6);
    nvgLineTo(vg, x + d * 9, y + 6);
    nvgClosePath(vg);
    nvgFillColor(vg, c.accent);
    nvgFill(vg);
}

} // namespace tessera::timeline
