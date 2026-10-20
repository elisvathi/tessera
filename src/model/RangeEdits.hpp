#pragma once

#include "ArrangementEdits.hpp"
#include "AutomationEdits.hpp"

#include <vector>

/// Time-range edits on the arranger (EDITOR_UX §6.4): they act on the clips and the track automation of the
/// given tracks together, each as one undo step.
namespace tessera::rangeEdits {

/// Empties [from, to) on `tracks`: clips are split at the edges and their inside removed; automation inside
/// is removed, keeping the curve outside the range as it was.
CommandPtr clearRange(const Song& song, const std::vector<TrackId>& tracks, Tick from, Tick to, const NextId& nextId);
/// Removes [from, to) on `tracks` and closes the gap: everything after moves left.
CommandPtr deleteTime(const Song& song, const std::vector<TrackId>& tracks, Tick from, Tick to, const NextId& nextId);
/// Opens `length` ticks of silence at `at` on `tracks`: everything from there moves right (clips across `at` are
/// split); automation holds its value through the gap.
CommandPtr insertSilence(const Song& song, const std::vector<TrackId>& tracks, Tick at, Tick length,
                         const NextId& nextId);
/// Copies [from, to) of `tracks` right after itself, over what was there.
CommandPtr duplicateRange(const Song& song, const std::vector<TrackId>& tracks, Tick from, Tick to,
                          const NextId& nextId);

/// "Automation follows clips": for each instance moved (or copied) between `before` and `after`, the track
/// automation under its old span goes along, to the lanes on the same AUTO channels of the track it lands on.
/// Null when no automation moves.
CommandPtr automationFollowing(const Song& song, const arrange::Layout& before, const arrange::Layout& after);

// Pure point helpers, also used by the editors.

/// The points of [from, to] with the curve's own values at both edges, shifted so `from` lands on `at`.
autoEdits::Points copySpan(const autoEdits::Points& points, float rest, Tick from, Tick to, Tick at);
/// Puts `segment` (from copySpan) over [at, end], keeping the curve on both sides as it was (jumps at the edges).
autoEdits::Points pasteSpan(const autoEdits::Points& points, float rest, const autoEdits::Points& segment, Tick at,
                            Tick end);
/// The curve with [from, to] emptied: points at both edges keep the curve outside as it was.
autoEdits::Points clearSpan(const autoEdits::Points& points, float rest, Tick from, Tick to);

} // namespace tessera::rangeEdits
