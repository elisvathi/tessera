#pragma once

#include "../../model/NoteEdits.hpp"
#include "../interaction/Gesture.hpp"
#include "NoteHitTest.hpp"

#include <functional>
#include <memory>

namespace tessera {

class PianoRollView;

/// The drags of the piano roll (EDITOR_UX §4.2-4.3). Each recomputes its whole preview from the clip's
/// committed notes on every update, so Shift/Alt/Ctrl can change mid-drag and Esc simply drops the preview.
/// Release commits one undo step. Positions are in the note grid's local coordinates.
namespace noteGestures {

/// Drag a note's body: moves the selection (relative snap, Alt free, Shift locks an axis). Ctrl/Cmd copies.
std::unique_ptr<DragGesture> move(PianoRollView& view, rack::math::Vec press, NoteId grabbed);
/// Drag a note's edge: resizes the selection by the same amount. With `stretch` (Alt+Shift), scales the
/// selection's timing from its opposite edge instead.
std::unique_ptr<DragGesture> resize(PianoRollView& view, rack::math::Vec press, NoteId grabbed, bool startEdge,
                                    bool stretch);
/// Draw tool: click creates a note with the last-used length and velocity; drag right sets its length,
/// up and down its pitch.
std::unique_ptr<DragGesture> draw(PianoRollView& view, rack::math::Vec press);
/// Paint tool: one note per grid step along the path, following the mouse pitch (Shift: keep the pitch).
std::unique_ptr<DragGesture> paint(PianoRollView& view, rack::math::Vec press);
/// Erase tool (and right-drag with Draw): deletes every note the pointer passes over.
std::unique_ptr<DragGesture> erase(PianoRollView& view, rack::math::Vec press);
/// Mute tool: toggles mute on every note the pointer passes over.
std::unique_ptr<DragGesture> mute(PianoRollView& view, rack::math::Vec press);
/// Slice tool: a click splits the note under it; a drag draws a line that splits every note it crosses.
std::unique_ptr<DragGesture> slice(PianoRollView& view, rack::math::Vec press);
/// Velocity tool, or Alt+drag on a note: drag up/down changes velocity (of the selection when it is
/// selected). Shift is fine adjustment.
std::unique_ptr<DragGesture> velocity(PianoRollView& view, rack::math::Vec press, NoteId grabbed);
/// Bend tool: drags a point of a note's pitch curve up or down to another row (whole semitones; key steps with
/// scale snap; Alt free) and along the note (on the grid; Shift keeps its time). Without a point under the press,
/// one is added there. When the note is selected, the selection bends with it, by the same amount or the same
/// key steps, so a chord glides to another chord.
std::unique_ptr<DragGesture> bend(PianoRollView& view, rack::math::Vec press, const BendHit& hit);
/// The same for a timbre or pressure curve drawn in a strip: `valueAt` gives the curve value at a height of the
/// strip. Up and down is free (no rows to snap to); sideways and the selection work as for bend.
std::unique_ptr<DragGesture> shapeCurve(PianoRollView& view, rack::math::Vec press, const BendHit& hit,
                                        expr::Curve curve, std::function<double(double)> valueAt);
/// Drag a curve segment's arch handle (or Alt+drag the segment): up and down arches it, from a straight line to
/// a curve that starts slow or fast. A selected note arches the selection's segments under the same spot.
/// Shift is fine adjustment.
std::unique_ptr<DragGesture> arch(PianoRollView& view, rack::math::Vec press, const BendHit& hit, expr::Curve curve);
/// Bend tool from empty space, or a strip away from its curves: picks the curve points inside the rectangle (Shift
/// adds to them); dragging one picked point then moves them all. Only notes `inScope` allows count. With
/// `selectNotes` (the pitch grid), the notes the rectangle touches and the picked points' notes are selected too,
/// so a chord can be bent together.
std::unique_ptr<DragGesture> pickPoints(PianoRollView& view, rack::math::Vec press, expr::Curve curve, CurveY yOf,
                                        std::function<bool(const Note&)> inScope, bool selectNotes, Modifiers mods);
/// Drag from empty space: selects the notes the rectangle touches. Shift adds, Ctrl/Cmd toggles.
std::unique_ptr<DragGesture> rubberBand(PianoRollView& view, rack::math::Vec press, Modifiers mods);

/// The transform box handles around a multi-note selection.
enum class BoxHandle { None, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };
/// Side handles stretch time, top and bottom scale the pitch range, corners do both.
std::unique_ptr<DragGesture> transformBox(PianoRollView& view, rack::math::Vec press, BoxHandle handle);

} // namespace noteGestures
} // namespace tessera
