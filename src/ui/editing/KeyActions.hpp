#pragma once

#include "../../model/KeyEdits.hpp"
#include "../EditorContext.hpp"

#include <optional>
#include <string>

/// The key menus (transport and piano roll) and the chord stamp menu (DESIGN.md §13, EDITOR_UX §4.2).
namespace tessera::keyActions {

/// "C Minor", or "No key" for chromatic.
std::string keyLabel(const Song& song, const ScaleRef& key);
/// A key label for a narrow header: "F#m" for "F# Minor", "C" for "C Major", "D Dor" for "D Dorian".
std::string shortKeyLabel(const std::string& label);

/// The key picker (one popover: Song | This clip, roots, scales, what happens to notes). Without a clip it picks
/// the song key only. The piano roll previews the choice; Enter or closing it applies it as one undo step.
/// With `withSettings`, the key's switches follow under the picker: highlight the key's rows, snap notes to the
/// key, fold to the key's rows (the notes header's key chip, which replaced three icons).
void openKeyPicker(EditorContext& ctx, std::optional<ClipId> clip = std::nullopt, bool withSettings = false);

/// Making and editing custom scales, starting from the clip's key (or the song's).
void openCustomScalesMenu(EditorContext& ctx, std::optional<ClipId> clip = std::nullopt);

void openChordMenu(EditorContext& ctx);
/// The chord stamp's choices (shapes, inversion, voicing), for a menu or submenu.
void appendChordItems(rack::ui::Menu* menu, EditorContext& ctx);
/// The chord shape Draw places now, or "Off".
std::string chordLabel(EditorContext& ctx);

} // namespace tessera::keyActions
