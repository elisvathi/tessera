#pragma once

#include "../../model/AudioEdits.hpp"

#include <rack.hpp>

#include <string>
#include <vector>

/// Editor actions for audio that reach into the rack (docs/design/AUDIO_MIXER.md §8.2).
namespace tessera {

class TesseraModule;
class EditorContext;

namespace audioActions {

/// Whether the patch has one of Rack's Audio modules, for offering the Connect helper.
bool audioModuleInPatch();

/// Cables MAIN L and R to the first two inputs of the first Audio module in the patch, as one Rack undo step.
/// Changes nothing when those inputs are in use or there is no Audio module. Returns what happened, for a toast.
std::string connectMainToAudio(TesseraModule* module);

/// The audio part of a clip's menu: Follow tempo, Sound type, the clip tempo (its guess, ×2, ÷2, typed),
/// gain back to 0 dB, and Try again for a render that failed (§6.2).
void appendAudioClipMenu(rack::ui::Menu* menu, EditorContext& ctx, ClipId clip);

/// File ▸ Import audio… (Ctrl/Cmd+Shift+O): picks a file and imports it like a drop on empty space, on new tracks
/// at the playhead's bar or in their first slots, following what plays.
void importDialog(EditorContext& ctx);

/// File ▸ Remove unused audio: takes out the sources no clip plays, as one undo step, and says how many.
void removeUnused(EditorContext& ctx);

/// The toast after a drop of audio files: what came in, what couldn't be read, what didn't fit.
std::string dropMessage(const std::vector<std::string>& imported, const std::vector<std::string>& failed,
                        int skipped);

/// Asks for a missing source's file and copies it into the patch under the source's name, then loads it.
void findFile(EditorContext& ctx, AudioSourceId source);
/// The sources whose files aren't in patch storage.
std::vector<AudioSourceId> missingSources(TesseraModule& module);

/// Asks once before importing files that need a lot of memory (one over 20 minutes, or over 1 GB in all, §8.7).
/// True to go on.
bool confirmSize(const std::vector<std::string>& paths);

/// Imports dropped files into the editor's Tessera (§6.5): one undo step, the new clips selected, a toast saying
/// what came in. When the first loop set the song tempo, the toast offers to keep the old one.
/// `note` goes first in the toast ("Added an audio track: Lead plays notes").
void dropAudio(EditorContext& ctx, const std::vector<std::string>& paths, const AudioDropTarget& target,
               const std::string& note = "");

} // namespace audioActions
} // namespace tessera
