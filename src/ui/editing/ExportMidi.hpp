#pragma once

#include "../EditorContext.hpp"

#include <utility>
#include <vector>

/// MIDI export (DESIGN.md §17): the dialog, the file dialog, and writing the files. Settings are remembered per
/// patch; a toast says what was written.
namespace tessera::exportMidi {

/// The arrangement, or the tracks selected in the headers (Ctrl/Cmd+Shift+E, the transport's Export button).
void openSong(EditorContext& ctx);
/// Clips from the launcher or the arranger: one file each, or one file with a track per clip.
void openClips(EditorContext& ctx, std::vector<std::pair<TrackId, ClipId>> clips);
/// The whole song as a DAWproject file (model/DawProject), with the same Timing, Note operators, Devices and
/// Automation choices as MIDI export (they are remembered together).
void openDawProject(EditorContext& ctx);

} // namespace tessera::exportMidi
