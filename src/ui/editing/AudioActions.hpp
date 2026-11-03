#pragma once

#include <string>

/// Editor actions for audio that reach into the rack (docs/design/AUDIO_MIXER.md §8.2).
namespace tessera {

class TesseraModule;

namespace audioActions {

/// Whether the patch has one of Rack's Audio modules, for offering the Connect helper.
bool audioModuleInPatch();

/// Cables MAIN L and R to the first two inputs of the first Audio module in the patch, as one Rack undo step.
/// Changes nothing when those inputs are in use or there is no Audio module. Returns what happened, for a toast.
std::string connectMainToAudio(TesseraModule* module);

} // namespace audioActions
} // namespace tessera
