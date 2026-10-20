#pragma once

#include <rack.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

/// Small NanoVG helpers shared by all views.
namespace tessera::draw {

struct TextStyle {
    std::shared_ptr<rack::window::Font> font;
    float size;
    NVGcolor color;
    int align = NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE;
    float letterSpacing = 0;
};

/// Draws text if the font is loaded. Returns false when it isn't (e.g. before the window exists).
bool text(NVGcontext* vg, const TextStyle& style, float x, float y, const std::string& s);

/// Draws text wrapped to `width`, starting at the top-left (x, y).
bool textBox(NVGcontext* vg, const TextStyle& style, float x, float y, float width, const std::string& s);

/// Width of `s` in the given style (0 when the font isn't loaded).
float textWidth(NVGcontext* vg, const TextStyle& style, const std::string& s);

/// Shortens `s` with a trailing ellipsis until it fits `maxWidth` in the given style.
std::string ellipsize(NVGcontext* vg, const TextStyle& style, const std::string& s, float maxWidth);

void roundedRect(NVGcontext* vg, rack::math::Rect r, float radius, NVGcolor fill);
void roundedRectStroke(NVGcontext* vg, rack::math::Rect r, float radius, NVGcolor stroke, float width);

/// Samples a span from x0 to x1 for drawing a curve: calls plot(x, first) at its start, every `step` px, and its
/// end, but only over the part between `left` and `right` (a step of margin each side). Used for every curve drawn
/// across a note, which can be far wider than the screen when zoomed in. The loop counts its steps instead of
/// stepping until x reaches the end: Rack builds with unsafe float math, where that test can stay false forever
/// (a loop like that once filled the memory with line points and froze the computer).
template <typename Plot>
void sampleSpan(float x0, float x1, float left, float right, float step, Plot&& plot) {
    float from = std::max(x0, left - step), to = std::min(x1, right + step);
    if (!(to >= from) || !(step > 0)) // also false for NaN
        return;
    int steps = int(std::ceil((to - from) / step));
    plot(from, true);
    for (int i = 1; i <= steps; i++)
        plot(i == steps ? to : from + step * float(i), false);
}

} // namespace tessera::draw
