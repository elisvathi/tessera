#pragma once

#include "../../model/Types.hpp"
#include "../interaction/Gesture.hpp"

#include <memory>

namespace tessera {

class ArrangerLanes;

/// The drags of the arranger (EDITOR_UX §6.3). Like the note gestures, each recomputes its whole preview from
/// the committed layout on every update and commits one undo step on release. Positions are in the lanes'
/// local coordinates.
namespace clipGestures {

/// Drag a clip's body: moves the selection in time and across tracks (snaps to the grid and to nearby edges;
/// Alt: free; Shift: one axis). Ctrl/Cmd copies, each copy with its own clip; with `linkedCopy` (Ctrl/Cmd+Alt at
/// the press) the copies play the same clips.
std::unique_ptr<DragGesture> move(ArrangerLanes& lanes, rack::math::Vec press, ArrangementId grabbed, bool linkedCopy);
/// Alt+drag a clip's body: slides the content inside the selected clips; the clips stay put.
std::unique_ptr<DragGesture> slip(ArrangerLanes& lanes, rack::math::Vec press, ArrangementId grabbed);
/// Drag the right edge: longer or shorter (a looping clip repeats). With `stretch` (Alt), the content is
/// time-stretched with the edge instead.
std::unique_ptr<DragGesture> resizeEnd(ArrangerLanes& lanes, rack::math::Vec press, ArrangementId grabbed,
                                       bool stretch);
/// Drag the left edge: trims the start; the content stays where it is on the timeline.
std::unique_ptr<DragGesture> trimStart(ArrangerLanes& lanes, rack::math::Vec press, ArrangementId grabbed);
/// Drag on a clip's header strip: down and up scale the selection's velocity (Shift: fine); sideways moves it.
std::unique_ptr<DragGesture> header(ArrangerLanes& lanes, rack::math::Vec press, ArrangementId grabbed);
/// Drags an audio clip's fade in or fade out handle: the fade's length.
std::unique_ptr<DragGesture> fade(ArrangerLanes& lanes, rack::math::Vec press, ArrangementId grabbed, bool in);
/// Drags the dot in the middle of an audio fade up or down: bends its curve.
std::unique_ptr<DragGesture> fadeCurve(ArrangerLanes& lanes, rack::math::Vec press, ArrangementId grabbed, bool in);
/// Drags the handle on top of a crossfade sideways: right makes it longer, left shorter, evenly around its centre.
std::unique_ptr<DragGesture> crossfade(ArrangerLanes& lanes, rack::math::Vec press, ArrangementId first,
                                       ArrangementId second);
/// Drags the dot in the middle of a crossfade's X up or down: bends both of its curves.
std::unique_ptr<DragGesture> crossfadeCurve(ArrangerLanes& lanes, rack::math::Vec press, ArrangementId first,
                                            ArrangementId second);
/// Draw tool in empty space: creates an empty clip as long as the drag (at least one grid step).
std::unique_ptr<DragGesture> draw(ArrangerLanes& lanes, rack::math::Vec press);
/// Erase tool: deletes every clip the pointer passes over.
std::unique_ptr<DragGesture> erase(ArrangerLanes& lanes, rack::math::Vec press);
/// Mute tool: toggles mute on every clip the pointer passes over.
std::unique_ptr<DragGesture> mute(ArrangerLanes& lanes, rack::math::Vec press);
/// Drag from empty space: selects the clips the rectangle touches. Shift adds, Ctrl/Cmd toggles.
std::unique_ptr<DragGesture> rubberBand(ArrangerLanes& lanes, rack::math::Vec press, Modifiers mods);

} // namespace clipGestures
} // namespace tessera
