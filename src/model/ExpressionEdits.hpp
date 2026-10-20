#pragma once

#include "Note.hpp"

#include <vector>

/// Editing a note's expression curves (EDITOR_UX §4.6): pitch bend, timbre and pressure, each a list of points
/// across the note (pos 0..1), flat before the first and after the last point.
namespace tessera::expr {

enum class Curve { Pitch, Timbre, Pressure };

struct Info {
    const char* name;
    float min, max;
    float rest; // the value when the note has no curve
};
const Info& info(Curve c);

std::vector<ExprPoint>& points(Note& note, Curve c);
const std::vector<ExprPoint>& points(const Note& note, Curve c);

/// Adds a point and returns its index. On an empty curve a resting point is added at the note's start first,
/// so the new point bends away from rest instead of moving the whole note.
size_t addPoint(std::vector<ExprPoint>& curve, float pos, float value, float rest);
/// Moves point `i`, keeping it between its neighbours so the order never changes.
void movePoint(std::vector<ExprPoint>& curve, size_t i, float pos, float value);
void removePoint(std::vector<ExprPoint>& curve, size_t i);

/// A picked point of a note's curve, found again by where it is: indices shift as points come and go.
struct PointRef {
    NoteId note;
    float pos, value;
    bool operator==(const PointRef& o) const { return note == o.note && pos == o.pos && value == o.value; }
};

/// The index of the point at (pos, value) on a curve, or -1 when there is none.
int findPoint(const std::vector<ExprPoint>& curve, float pos, float value);
/// Deletes the picked points of one curve. A curve left with only resting points is dropped, so the note plays
/// straight again. Returns how many points went.
size_t removePoints(std::vector<Note>& notes, Curve c, const std::vector<PointRef>& picked);

/// Keeps all of a note's curves where they were in time after its start or length changed (a resize, a slice, a
/// typed length), so lengthening a note doesn't stretch its bend. Points go back to their old time in the new
/// span; parts that now fall outside the note are cut off, with a point at the edge holding the value the curve
/// had there. Stretching time on purpose scales curves with the note and doesn't call this.
void keepInTime(Note& note, Tick oldStart, Tick oldLength);

} // namespace tessera::expr
