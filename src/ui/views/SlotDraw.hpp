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
/// The mark of a clip played by playheads: one short stripe per playhead of `device` (the Playheads device of the
/// chain the clip plays through), in its colour (hollow when muted), stacked from `topRight` down and leftwards.
/// Draws nothing for a null `device`.
void drawPlayheadMark(NVGcontext* vg, const Device* device, rack::math::Vec topRight, float maxHeight);
/// A clip its track's playheads play: the notes written in it aren't what plays, so it shows where each playhead
/// goes (as the time map draws it). One band of `area` per playhead (when they fit; else they share it) with its path
/// in its colour, from its segment's start (bottom) to its end (top), over `x0`..`x1` for `e0`..`e1` ticks since the
/// clip started. The paths follow the settings as set; an off playhead's is faint. `line` separates the bands.
void drawPlayheadPaths(NVGcontext* vg, const Song& song, const Track& track, const Device& device, const Clip& clip,
                       rack::math::Rect area, float x0, float x1, double e0, double e1, NVGcolor line, float alpha);

} // namespace slotDraw

} // namespace tessera
