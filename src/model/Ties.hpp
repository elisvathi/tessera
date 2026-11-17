#pragma once

#include "NoteEdits.hpp"

#include <vector>

/// Tied notes and their glides (docs/design/TIES_VOICES.md). A tied note continues the note before it with no new
/// gate; its glide is a pitch line across the joint.
namespace tessera::ties {

using edits::IdSet;
using edits::NoteList;

/// Gaps up to this long close up when notes are tied; notes further apart stay apart.
constexpr Tick kTieReach = kPpq / 4;
/// A note a tie continues plays this much longer than written, so the tied note always finds it still sounding
/// and takes its voice over instead of starting a new gate (end and start are worked out apart and may round).
constexpr Tick kTieOverlap = 1;

/// For each of `notes` (in clip order), the index of the note its tie continues, or -1: not tied, or nothing to
/// continue. The partner is a note on the same channel that starts earlier and ends at or after the tied note's
/// start (touching or overlapping), but before its end; with several, the nearest in pitch. Each note is continued
/// by at most one other. Muted notes take no part. In a loop [loopStart, loopEnd) a tied note at the loop start
/// with no partner continues a note that runs to the loop end, as the loop plays round.
std::vector<int> partners(const NoteList& notes, Tick loopStart = 0, Tick loopEnd = 0);
/// For each note, whether a tie continues it, from partners().
std::vector<char> continued(const std::vector<int>& partners);

/// A glide in clip ticks: the pitch leaves `p0` at `from`, passes the joint, and arrives at `p1` at `to`.
struct Glide {
    Tick from = 0, joint = 0, to = 0;
    int p0 = 60, p1 = 60;
    float shape = 0;
    bool moves() const { return to > from && p0 != p1; }
};

/// The glide of tied note `b` from its partner `a`, its ends kept inside the two notes.
Glide glideOf(const Note& a, const Note& b);
/// Where a glide of `shape` is at `x` (0..1 across it), also 0..1: the same bend as a pitch curve segment.
double shaped(float shape, double x);
/// The pitch along `g` at clip tick `t`, in semitones (`p0` before it, `p1` after).
double pitchAt(const Glide& g, double t);

/// Ties the notes in `ids`. One note continues the note before it on its channel; several continue one another,
/// so a selection becomes one line. Each joins the earlier note nearest in pitch that ends within kTieReach of its
/// start: that note is stretched or trimmed to end exactly where it starts. `tiedIds` gets the notes tied.
NoteList tie(const NoteList& notes, const IdSet& ids, IdSet* tiedIds = nullptr);
/// Unties the tied notes among `ids`. Keeping the glide writes its line into the two notes' own pitch curves.
NoteList untie(const NoteList& notes, const IdSet& ids, bool keepGlide);
/// Whether every note of `ids` that could be tied already is, so the Tie key unties instead.
bool allTied(const NoteList& notes, const IdSet& ids);
/// For an imported part (sorted by start): where a channel plays one line and its notes only overlap where one
/// hands over to the next (as legato playing on a mono keyboard writes them), those overlaps become ties. A
/// channel with chords, or overlaps longer than a hand-over, is left as it is.
NoteList tieHandovers(const NoteList& notes);

} // namespace tessera::ties
