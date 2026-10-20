#pragma once

#include "../module/OutputModuleBase.hpp"

#include <rack.hpp>

#include <string>

namespace tessera {

/// The context menus Track Out and Drum Out share. `what` names the module in undo steps ("Track Out").
void appendSourceMenu(rack::ui::Menu* menu, OutputModuleBase* m, const std::string& what);
/// Tracks of the type the module outputs, each with its number, name and colour.
void appendTrackMenu(rack::ui::Menu* menu, OutputModuleBase* m, const std::string& what);

} // namespace tessera
