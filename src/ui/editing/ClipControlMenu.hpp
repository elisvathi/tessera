#pragma once

#include "../../model/ClipControl.hpp"

#include <rack.hpp>

#include <string>

namespace tessera {
class TesseraModule;
}

/// Menus for clip control (docs/DECISIONS.md §179): launching clips from a MIDI controller.
namespace tessera::clipControlMenu {

/// The Clip control submenu's items: the controller, its MIDI input and its lights, the channel, the pads' first
/// note, program changes for scenes, and forgetting learned controls.
void append(rack::ui::Menu* menu, TesseraModule* m);
/// Learn MIDI / Forget for one thing the controller can do (a slot, a scene), in that thing's own menu.
void appendLearnItems(rack::ui::Menu* menu, TesseraModule* m, const clipControl::Binding& action);
/// "Note C3, ch 1", "CC 64, ch 2", "Program 5": a learned control, as the menus name it.
std::string controlText(const clipControl::Binding& b);

} // namespace tessera::clipControlMenu
