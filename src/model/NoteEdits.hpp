#pragma once

#include "Note.hpp"
#include "Scale.hpp"

#include <functional>
#include <unordered_set>
#include <vector>

/// Pure note-editing operations used by the editor's gestures and keyboard commands.
///
/// Each takes a clip's full note list and returns the edited list (sorted). Gestures recompute their preview
/// from the starting list on every mouse move, then hand before/after to replaceNotes() for one undo step.
namespace tessera::edits {

using NoteList = std::vector<Note>;
using IdSet = std::unordered_set<NoteId>;
using IdSource = std::function<NoteId()>;

/// The notes in `ids`, in clip order.
NoteList pick(const NoteList& notes, const IdSet& ids);
IdSet idsOf(const NoteList& notes);

/// Moves notes in time and pitch. Times stop at 0, pitches at 0..127.
NoteList move(const NoteList& notes, const IdSet& ids, Tick deltaTime, int deltaPitch);

/// Moves note ends (or starts) by `delta`, keeping every length at least `minLength`.
NoteList resizeEnd(const NoteList& notes, const IdSet& ids, Tick delta, Tick minLength);
NoteList resizeStart(const NoteList& notes, const IdSet& ids, Tick delta, Tick minLength);

/// Scales the timing of notes around `anchor` (starts and lengths), e.g. the transform box or Alt+Shift edge.
NoteList stretchTime(const NoteList& notes, const IdSet& ids, Tick anchor, double factor);

/// Sets each note's pitch to `f(pitch)`, clamped to 0..127.
NoteList mapPitches(const NoteList& notes, const IdSet& ids, const std::function<int(int)>& f);
/// Moves each note by `steps` notes of the scale, so chords stay in key.
NoteList stepInScale(const NoteList& notes, const IdSet& ids, const Scale& scale, int steps);
/// Moves notes outside the scale to the nearest scale note ("fit to scale").
NoteList fitToScale(const NoteList& notes, const IdSet& ids, const Scale& scale);
/// Moves notes into another key keeping their degrees (see tessera::moveToKey).
NoteList moveToKey(const NoteList& notes, const IdSet& ids, const Scale& from, const Scale& to);

/// Maps pitches from the range [fromLow, fromHigh] onto [toLow, toHigh], keeping their order.
NoteList scalePitch(const NoteList& notes, const IdSet& ids, int fromLow, int fromHigh, int toLow, int toHigh);

/// Adds `delta` (0..1 scale) to velocities, clamped.
NoteList changeVelocity(const NoteList& notes, const IdSet& ids, float delta);
/// Sets velocities along a straight line from (t0, v0) to (t1, v1), by each note's start.
NoteList velocityLine(const NoteList& notes, const IdSet& ids, Tick t0, float v0, Tick t1, float v1);

NoteList setMuted(const NoteList& notes, const IdSet& ids, bool muted);

/// Copies of `ids` shifted by `offset`, with fresh IDs. Returns the full list; `added` receives the copies' IDs.
NoteList duplicate(const NoteList& notes, const IdSet& ids, Tick offset, const IdSource& newId, IdSet* added = nullptr);
/// How far Ctrl+D shifts the copy of `ids`: the selection's length rounded up to whole steps, so repeated copies
/// line up. The step is the bar when the selection is longer than half a bar, the beat when it is longer than half
/// a beat, else `grid` (never less than `grid`), so a one-bar phrase repeats on the next bar. A note that hangs past
/// a step by less than an eighth of it (strummed or humanized) doesn't push the copy a step later; the copy always
/// starts after the last selected note starts.
Tick duplicateOffset(const NoteList& notes, const IdSet& ids, Tick grid, TimeSig ts);

/// Splits the note at `at` (inside it) into two. The right part gets a fresh ID.
NoteList slice(const NoteList& notes, NoteId id, Tick at, const IdSource& newId);
/// Splits a note into `parts` equal parts.
NoteList sliceEqual(const NoteList& notes, NoteId id, int parts, const IdSource& newId);

/// The overlap rule (EDITOR_UX §4.3): a note in `changed` that covers another note of the same pitch replaces
/// it; one that overlaps it partly trims it. Notes in `changed` are never touched.
NoteList resolveOverlaps(const NoteList& notes, const IdSet& changed);

/// The time span [first start, last end) of the notes in `ids`; {0, 0} when there are none.
std::pair<Tick, Tick> span(const NoteList& notes, const IdSet& ids);

} // namespace tessera::edits
