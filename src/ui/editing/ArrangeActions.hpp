#pragma once

#include "../../model/Types.hpp"

#include <rack.hpp>

namespace tessera {

class ArrangerLanes;

/// Arranger clip operations shared by menus and keyboard shortcuts, so each exists once. They act on the
/// selected clips.
namespace arrangeActions {

void deleteSelected(ArrangerLanes& lanes);
/// Copies the selection right after itself, as linked instances (Ctrl/Cmd+D).
void duplicateSelected(ArrangerLanes& lanes);
/// Splits the selected clips at `at`; with nothing selected, the clips `at` falls in on the selected track.
void split(ArrangerLanes& lanes, Tick at);
void glueSelected(ArrangerLanes& lanes);
void makeUniqueSelected(ArrangerLanes& lanes);
void setMuted(ArrangerLanes& lanes, bool muted);
void transpose(ArrangerLanes& lanes, int semitones);
void setVelocityScale(ArrangerLanes& lanes, float scale);
void resetStretch(ArrangerLanes& lanes);
/// Moves the selection by `ticks` (keyboard nudge).
void nudge(ArrangerLanes& lanes, Tick ticks);
/// Lengthens (or shortens) the selection by `ticks`.
void changeLength(ArrangerLanes& lanes, Tick ticks);

void selectAll(ArrangerLanes& lanes);
void selectNone(ArrangerLanes& lanes);
void selectInvert(ArrangerLanes& lanes);

void copy(ArrangerLanes& lanes);
void cut(ArrangerLanes& lanes);
/// Pastes on the selected track: at the mouse when `atMouse` and the mouse is over the lanes, else at the
/// playhead. Copies keep their distances in time and between tracks.
void paste(ArrangerLanes& lanes, bool atMouse);

/// Makes the instance's clip the piano roll's and focuses it; if it already was, maximizes the piano roll.
void openInEditor(ArrangerLanes& lanes, ArrangementId id);

void appendClipMenu(rack::ui::Menu* menu, ArrangerLanes& lanes, ArrangementId clicked, Tick at);
/// Right-click in empty lane space.
void appendLaneMenu(rack::ui::Menu* menu, ArrangerLanes& lanes, TrackId track, Tick at);

} // namespace arrangeActions
} // namespace tessera
