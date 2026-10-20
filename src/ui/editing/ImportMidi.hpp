#pragma once

#include "../../model/Clip.hpp"
#include "../EditorContext.hpp"
#include "ProjectFile.hpp"

#include <functional>
#include <string>

namespace tessera {

class TesseraModule;

/// Importing Standard MIDI Files (docs/DECISIONS.md §60): into a launcher slot, onto the timeline, or as new
/// tracks. The file is read on the UI thread; each import is one undo step.
namespace importMidi {

/// Asks for a .mid file; "" when cancelled.
std::string pickFile();

/// Reads `path` and makes one clip of it: straight away for a file with one part, otherwise after asking which
/// part (or all of them, as one clip). Says so when the file can't be read or has no notes.
void chooseClip(EditorContext& ctx, const std::string& path, std::function<void(Clip)> use);

/// Asks for a file and puts its clip into an empty launcher slot.
void toSlot(EditorContext& ctx, TrackId track, int slot);

/// Asks for a file and adds it as new tracks, with its tempo (its own undo step, when it differs) and time
/// signature.
void asTracks(TesseraModule* m, projectFile::Say say);

} // namespace importMidi

} // namespace tessera
