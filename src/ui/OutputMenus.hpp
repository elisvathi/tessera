#pragma once

#include "../module/OutputModuleBase.hpp"

#include <rack.hpp>

#include <string>

namespace tessera {

/// The context menus Track Out and Drum Out share. `what` names the module in undo steps ("Track Out").
void appendSourceMenu(rack::ui::Menu* menu, OutputModuleBase* m, const std::string& what);
/// Tracks of the type the module outputs, each with its number, name and colour.
void appendTrackMenu(rack::ui::Menu* menu, OutputModuleBase* m, const std::string& what);
/// All, or one channel 1..16; each channel names the playheads of the bound track that play on it.
void appendChannelMenu(rack::ui::Menu* menu, OutputModuleBase* m);
/// Who plays on `channel` of the module's track: its playheads' names ("Lead, Bells"), "nothing" when it has
/// playheads but none there, or empty when it has no playheads (its notes keep their own channels).
std::string channelPlayers(OutputModuleBase* m, int channel);

} // namespace tessera
