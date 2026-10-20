#pragma once

#include "../../model/ExpressionEdits.hpp"
#include "../../model/NoteEdits.hpp"
#include "../interaction/Axes.hpp"

#include <functional>

/// What the mouse is over in a note grid. Pure, so the priority rules are unit tested.
namespace tessera {

struct NoteHit {
    enum class Part { None, Body, StartEdge, EndEdge };
    Part part = Part::None;
    NoteId id;

    bool any() const { return part != Part::None; }
};

/// What a press at (x, y) would grab (EDITOR_UX §9): a resize edge first, then a selected note's body, then
/// any other note, else nothing. Edge zones reach `edgePx` outside the note, so narrow notes stay resizable;
/// narrow bodies are widened to stay clickable. Where edge zones overlap (touching notes), the edge of the note the
/// mouse is inside wins, then the nearest edge.
NoteHit hitTestNotes(const edits::NoteList& notes, const TimeAxis& time, const PitchAxis& pitch, double x, double y,
                     const edits::IdSet& selected, double edgePx = 6);

/// What a press with the Bend tool grabs on a note's pitch curve.
struct BendHit {
    NoteId id;
    int point = -1;       // the grabbed point; -1 means a new point goes in at `pos`
    float pos = 0;        // across the note, 0..1
    bool segment = false; // arch the segment that starts at `point` instead of moving the point

    bool any() const { return id.valid(); }
};

/// Where a note's expression curve is drawn in the grid: the y of a curve value on that note.
using CurveY = std::function<double(const Note&, float value)>;

/// The expression curve a press at (x, y) would grab, in a grid that draws each curve through `yOf`: a curve
/// point, or a segment's arch handle (in its middle, on segments at least `minSpanPx` wide that rise or fall),
/// within `pointPx` first; then the note whose time span holds x and whose curve passes nearest y (within
/// `curvePx`), else nothing. Only notes `editable` allows are looked at.
BendHit hitTestCurves(const edits::NoteList& notes, const TimeAxis& time, expr::Curve curve, const CurveY& yOf,
                      double x, double y, double pointPx, double curvePx,
                      const std::function<bool(const Note&)>& editable = nullptr, double minSpanPx = 14);

/// The pitch curves, drawn at the pitch they play: hitTestCurves with a reach of at least half a row.
BendHit hitTestBends(const edits::NoteList& notes, const TimeAxis& time, const PitchAxis& pitch, double x, double y,
                     double pointPx = 6, double curvePx = 6);

/// The segment of a curve that `pos` falls in (the index of its first point), or -1 before the first point or
/// after the last.
int segmentAt(const std::vector<ExprPoint>& points, float pos);

/// Notes whose rectangles touch the rectangle between two corners (rubber band selects what it touches).
edits::IdSet notesInRect(const edits::NoteList& notes, const TimeAxis& time, const PitchAxis& pitch, double x0,
                         double y0, double x1, double y1);

/// Notes the segment from (x0, y0) to (x1, y1) passes over, for erase / mute drags.
edits::IdSet notesAlongPath(const edits::NoteList& notes, const TimeAxis& time, const PitchAxis& pitch, double x0,
                            double y0, double x1, double y1);

/// Where a slice line from (x0, y0) to (x1, y1) crosses each note's row, as ticks: one cut per crossed note.
std::vector<std::pair<NoteId, double>> sliceLineCuts(const edits::NoteList& notes, const TimeAxis& time,
                                                     const PitchAxis& pitch, double x0, double y0, double x1,
                                                     double y1);

} // namespace tessera
