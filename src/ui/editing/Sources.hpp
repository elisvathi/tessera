#pragma once

#include "../../model/AudioSource.hpp"
#include "../../model/Types.hpp"

#include <rack.hpp>

#include <string>
#include <vector>

/// A track's sources (docs/design/AUDIO_MIXER.md §7.8): the clips of an audio track or the master's mix, and each
/// output module whose audio inputs bring sound to the track, with their Level and Use.
namespace tessera {

class TesseraModule;
class EditorContext;
class OutputModuleBase;

namespace sources {

/// One source as the list shows it. `module` is null for the clips (or the master's mix).
struct Row {
    OutputModuleBase* module = nullptr;
    std::string name;
    SourceLevel level;
    bool countedOnce = false; // the same cable as another source: it adds nothing and has no level
};

/// The sources of `track` (or the master) now: "Clips" first on an audio track, "Mix" first on the master when it
/// has other sources, then the modules by place in the rack ("Track Out 1", "Track Out 2", "Drum Out"). UI thread.
std::vector<Row> rowsOf(TesseraModule& m, TrackId track);

/// Whether the list and its levels show: two or more sources, or one that is off or not at 0 dB (§7.8: levels show
/// only when they are needed).
bool needsList(const std::vector<Row>& rows);

/// Sets a source's Level and Use as one undo step: a song edit for the clips or mix, a Rack history step for a
/// module (its patch data).
void set(TesseraModule& m, TrackId track, const Row& row, SourceLevel level, const std::string& name);

/// The sources list (the strip chip's menu when needsList()): per source, Use, a live meter, a Level slider and
/// Show in the rack; then Add a Track Out for this track.
void openList(EditorContext& ctx, TrackId track);

/// The setting of one input of `track`: an output module's, or (`module` -1) the track's clips.
SourceLevel inputLevel(EditorContext& ctx, TrackId track, int64_t module);
/// Saves an input's setting as one undo step named `name`.
void commitInput(EditorContext& ctx, TrackId track, int64_t module, SourceLevel next, const std::string& name);
/// Plays a level and pan at once while a mixer strip is dragged, before the commit.
void previewInput(EditorContext& ctx, TrackId track, int64_t module, float db, float pan);
/// Ends previewInput().
void endInputPreview(EditorContext& ctx, TrackId track, int64_t module);
/// Whether any input of `track` is soloed.
bool anyInputSolo(EditorContext& ctx, TrackId track);

/// Use and a Level slider for one output module, for its own right-click menu (one value shown in two places).
void appendModuleItems(rack::ui::Menu* menu, OutputModuleBase* module);

} // namespace sources
} // namespace tessera
