#pragma once

#include "../interaction/DeviceTags.hpp"

#include <rack.hpp>

#include <string>

/// The pictures on the device cards (docs/DEVICES.md §1.1): each shows one idea with a few bold shapes on a dark
/// screen, grey outlines for notes going in and the card's colour for notes coming out. The strip caches each
/// picture in a framebuffer and calls this only when its values change.
namespace tessera::tiles {

/// Draws picture `tile` of a `type` card in a `width` × kDeviceTile box at the origin: its screen, its drawing for
/// the values `v`, in `tint`, and `caption` along its bottom.
void drawTile(NVGcontext* vg, DeviceType type, int tile, const tags::Values& v, float width, NVGcolor tint,
              const std::string& caption);

/// Draws a value as a tag or a panel chip reads it, starting at `x` on the line `cy`: stacked fractions, a small unit
/// and a small T or D. Returns its width; with `draw` false it only measures.
float drawValue(NVGcontext* vg, float x, float cy, const std::string& label, float size, NVGcolor color,
                bool draw = true);

/// A small drawing of a playhead direction (0..3) or step order (4..7) for the Direction panel, in `r`.
void drawDirectionIcon(NVGcontext* vg, rack::math::Rect r, int value, NVGcolor color);

} // namespace tessera::tiles
