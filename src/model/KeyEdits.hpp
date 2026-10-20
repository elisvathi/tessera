#pragma once

#include "Commands.hpp"
#include "Scale.hpp"

#include <optional>

/// Key changes: the song key, a clip's own key, and custom scales. All are undoable commands.
namespace tessera {

/// The key a clip is edited in: its own override, or the song key.
const ScaleRef& clipKeyRef(const Song& song, const Clip& clip);
Scale clipScale(const Song& song, const Clip& clip);

/// True when a drum track uses the clip. Drum notes pick lanes, so key changes never move them.
bool usedByDrumTrack(const Song& song, ClipId clip);

/// Sets the song key. With `moveNotes`, notes of every clip that follows the song key move to the same degree
/// of the new key ("move notes to new key"); otherwise pitches stay. One undo step either way.
CommandPtr changeSongKey(const Song& song, const ScaleRef& key, bool moveNotes);

/// Sets a clip's own key, or with nullopt makes it follow the song key again. `moveNotes` as above.
CommandPtr changeClipKey(const Song& song, ClipId clip, const std::optional<ScaleRef>& key, bool moveNotes);

/// True when changing from one key to another would move any notes, so the editor knows whether to ask.
bool keyChangeMovesNotes(const Song& song, const std::vector<ClipId>& clips, const Scale& from, const Scale& to);

/// The clips that follow the song key and could move with it.
std::vector<ClipId> clipsFollowingSongKey(const Song& song);

/// Adds a custom scale (or replaces the one with the same id).
CommandPtr putCustomScale(const Song& song, const CustomScale& scale);
/// Removes a custom scale. Keys that used it fall back to Chromatic until it is restored with undo.
CommandPtr deleteCustomScale(const Song& song, const std::string& id);

} // namespace tessera
