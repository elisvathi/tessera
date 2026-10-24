#pragma once

#include "../EditorContext.hpp"

#include <rack.hpp>

namespace tessera {

struct LauncherStatus;

/// One launcher slot, as the launcher grid and the arranger's launcher panel draw it: empty (a stop square),
/// recording (red), or a clip in its colour with its name, a miniature of its notes and a launch button
/// (green while playing, with a progress ring; blinking while queued). `hovered` lights the slot,
/// `hotLaunch` the launch or stop button.
namespace slotDraw {

/// The launch button: a square at the cell's left.
inline rack::math::Rect launchRect(const rack::math::Rect& cell) {
    return rack::math::Rect(cell.pos, rack::math::Vec(cell.size.y, cell.size.y));
}

void drawSlot(NVGcontext* vg, EditorContext& ctx, rack::math::Rect cell, TrackId track, int scene,
              const LauncherStatus& status, bool hovered, bool hotLaunch);

void drawTriangle(NVGcontext* vg, rack::math::Vec c, float size, NVGcolor color);
void drawSquare(NVGcontext* vg, rack::math::Vec c, float size, NVGcolor color);
void drawRing(NVGcontext* vg, rack::math::Vec c, float radius, float progress, NVGcolor color);
void drawNotePreview(NVGcontext* vg, const Clip& clip, const std::vector<DrumLane>* lanes, rack::math::Rect r,
                     NVGcolor color);
bool blinkOn();

} // namespace slotDraw

} // namespace tessera
