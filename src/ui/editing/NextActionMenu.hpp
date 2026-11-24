#pragma once

#include "../../model/Song.hpp"
#include "../EditorContext.hpp"

#include <rack.hpp>

#include <string>

/// A clip's next action (docs/DECISIONS.md §184): the words for it, and the menus that set it, shared by the clip
/// menu and the inspector so each choice exists once.
namespace tessera::nextActionMenu {

/// The action's name: "Keep looping", "Play next", "Round robin".
std::string label(NextAction action);
/// A few words on what it does, for menus: "random, never this one".
std::string hint(NextAction action);
/// The count with its unit: "4 loops", "1 bar".
std::string afterLabel(const NextActionSettings& next);
/// How often the main action wins: "Always", "75%".
std::string howOftenLabel(const NextActionSettings& next);
/// "Whole track" or "This block".
std::string pickFromLabel(const NextActionSettings& next);
/// What the clip menu's item reads on its right: "Play next · 4", or "Off".
std::string shortLabel(const NextActionSettings& next);
/// One sentence on what the clip will do, with the names of the clips it goes to: "Plays 4 times, then Bass 2."
std::string summary(const Song& song, const Track& track, const Clip& clip);

/// The parts of a next action a menu can set.
enum class Part { Do, After, HowOften, Otherwise, PickFrom };
/// Adds the items that set one part of the clip's next action, each one undo step.
void appendItems(rack::ui::Menu* menu, EditorContext& ctx, ClipId clip, Part part);
/// The clip menu's "Next action ▸" submenu: the actions, then After, How often, Otherwise and Pick from.
void appendMenu(rack::ui::Menu* menu, EditorContext& ctx, ClipId clip);

} // namespace tessera::nextActionMenu
