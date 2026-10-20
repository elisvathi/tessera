#pragma once

#include <rack.hpp>

namespace tessera {

class PianoRollView;

/// The note transforms (EDITOR_UX §4.8), applied to the selection or, with nothing selected, the whole clip.
/// Transforms with settings open a small panel: the result is previewed on the grid while the settings change,
/// Apply (Enter) records one undo step, Cancel (Esc) leaves the notes as they were.
namespace transformMenu {

/// Adds the transforms (and the Select submenu) to `menu`.
void addItems(rack::ui::Menu* menu, PianoRollView& view);
/// The toolbar's ⋯ menu.
void open(PianoRollView& view);
/// Q: quantizes the selection (or the clip) to the snap grid, with the quantize settings used last.
void quantizeNow(PianoRollView& view);
/// Shift+Q: the quantize settings panel.
void openQuantize(PianoRollView& view);

} // namespace transformMenu

} // namespace tessera
