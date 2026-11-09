#pragma once

#include "Arrangement.hpp"
#include "Commands.hpp"
#include "TrackCommands.hpp"

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// Pure editing operations on arranger instances, used by the arranger's gestures, menus and keys.
///
/// They work on a Layout: every instance of the song with the track it sits on, so one edit can move clips
/// across tracks. Each returns the edited layout. Gestures recompute their preview from the starting layout
/// on every mouse move, then hand before/after to replaceArrangement() for one undo step.
namespace tessera::arrange {

struct Placed {
    TrackId track;
    ArrangerClip clip;

    bool operator==(const Placed& o) const { return track == o.track && clip == o.clip; }
};
using Layout = std::vector<Placed>;
using IdSet = std::unordered_set<ArrangementId>;
using IdSource = std::function<ArrangementId()>;

/// Every instance of the song, track by track in display order, each track sorted by position.
Layout layoutOf(const Song& song);
Layout pick(const Layout& layout, const IdSet& ids);
IdSet idsOf(const Layout& layout);
const Placed* find(const Layout& layout, ArrangementId id);
/// Clips that overlap on one track play together and are drawn stacked: each instance gets the highest sub-row free
/// for its whole span. Instances are taken by layer (ArrangerClip::layer, 0 by default, so a lower layer claims its
/// row first), then start, then ID, so the layout is stable and a clip with nothing overlapping sits on top. `rows`
/// is at least 1. Where stacked clips share an AUTO
/// channel, the clip on the lower row plays it. With `flat` (audio tracks, which never stack: their overlaps are
/// crossfades) every instance is on row 0.
struct Stacking {
    std::unordered_map<ArrangementId, int> row;
    int rows = 1;
};
Stacking stackOf(const Layout& layout, TrackId track, bool flat = false);
/// Whether instance `id` overlaps a clip in the sub-row above (`direction` -1) or below (+1), to swap rows with.
bool canMoveInStack(const Layout& layout, ArrangementId id, int direction);
/// Swaps instance `id` with the overlapping clips in the sub-row above (-1) or below (+1). Every clip on the track
/// keeps the row it showed, so only those two places change.
Layout moveInStack(const Layout& layout, ArrangementId id, int direction);

/// The time span [first start, last end) of `ids`; {0, 0} when there are none.
std::pair<Tick, Tick> span(const Layout& layout, const IdSet& ids);

/// Moves instances by `dt` and `trackDelta` rows of `order` (the track display order). The whole selection
/// stops together at time 0 and at the first and last track.
Layout move(const Layout& layout, const IdSet& ids, Tick dt, const std::vector<TrackId>& order, int trackDelta);
/// Copies of `ids` moved like move(), with fresh IDs; the copies play the same clips (linked).
Layout duplicate(const Layout& layout, const IdSet& ids, Tick dt, const std::vector<TrackId>& order, int trackDelta,
                 const IdSource& newId, IdSet* added = nullptr);
/// Moves the end. A looping clip repeats into the new space; a non-looping one shows silence.
Layout resizeEnd(const Layout& layout, const IdSet& ids, Tick delta, Tick minLength);
/// Moves the start, keeping the content where it is on the timeline (the offset follows).
Layout trimStart(const Layout& layout, const IdSet& ids, Tick delta, Tick minLength, const ClipPool& clips);
/// Moves the end and scales the content's speed with it (Alt+drag edge).
Layout stretchEnd(const Layout& layout, const IdSet& ids, Tick delta, Tick minLength);
/// Slides the content inside the instances by `delta` timeline ticks; the instances stay put.
Layout slip(const Layout& layout, const IdSet& ids, Tick delta, const ClipPool& clips);
/// Splits every instance in `ids` that `at` falls inside. Right parts get fresh IDs, collected in `rightParts`.
Layout split(const Layout& layout, const IdSet& ids, Tick at, const ClipPool& clips, const IdSource& newId,
             IdSet* rightParts = nullptr);
Layout setMuted(const Layout& layout, const IdSet& ids, bool muted);
Layout transpose(const Layout& layout, const IdSet& ids, int semitones);
Layout setVelocityScale(const Layout& layout, const IdSet& ids, float scale);

/// The overlap rule: an instance in `changed` wins over the others on its track. Ones it covers are removed,
/// ones it overlaps are trimmed, and one it sits inside is split around it. Audio instances follow settleAudio().
Layout resolveOverlaps(const Layout& layout, const IdSet& changed, const ClipPool& clips, const IdSource& newId);

/// How far the parts of a split audio clip reach into the clip dropped inside it: each seam crossfades over this.
constexpr Tick kSeamCrossfade = kPpq / 8;

/// The audio overlap rule (docs/design/AUDIO_MIXER.md §6.3), whether or not clips stack: on an audio track an
/// overlap is a crossfade, so clips stay where they are, but a clip a changed one covers goes, one a changed clip
/// sits inside is split around it (crossfading at each seam), and where three would sound at once the earliest
/// stops. Audio instances always sit on layer 0: audio tracks never stack.
Layout settleAudio(const Layout& layout, const IdSet& changed, const ClipPool& clips, const IdSource& newId);

/// Instances on the timeline replaced by others: deletes what left the layout, adds what is new. Clips no
/// longer used anywhere leave the pool with it.
class EditArrangement : public Command {
  public:
    EditArrangement(std::string name, Layout removed, Layout added)
        : label(std::move(name)), removed(std::move(removed)), added(std::move(added)) {}

    void apply(Song& song) override { swap(song, removed, added); }
    void revert(Song& song) override { swap(song, added, removed); }
    std::string name() const override { return label; }
    ChangeSet changes() const override;

  private:
    static void swap(Song& song, const Layout& out, const Layout& in);
    std::string label;
    Layout removed, added;
};

/// One undo step from `before` to `after` (both full layouts). Null when nothing changed.
CommandPtr replaceArrangement(const Song& song, const std::string& name, const Layout& before, const Layout& after);

/// The instances of `cut` that are pieces of an audio clip the edit from `before` cut: a new instance that plays on
/// from an old one on the same track, and that old one. A linked copy dropped inside its own clip plays something
/// else, so it doesn't count.
IdSet audioPieces(const Layout& before, const Layout& cut, const ClipPool& clips);
/// Gives each instance of `layout` in `pieces` a clip of its own, unless it would be the clip's only user, so changing
/// one piece leaves the others alone. Returns the new clips, which must join the song before the layout does.
std::vector<Clip> separatePieces(Layout& layout, const IdSet& pieces, const Song& song, const NextId& nextId);
/// replaceArrangement() for an edit that may cut audio clips: the pieces get their own clips in the same undo step.
CommandPtr replaceArrangement(const Song& song, const std::string& name, const Layout& before, Layout after,
                              const NextId& nextId);

/// Makes a clip `factor` times as long (2 = half speed, 0.5 = double speed): its notes, loop and automation, and
/// every timeline clip that uses it, so each still plays all of it. One undo step; null when nothing changes.
CommandPtr scaleClip(const Song& song, ClipId clip, double factor);

/// An instance of `clip` at `position` on `track`, playing the clip's loop (or all of it) once, placed over
/// whatever is there.
CommandPtr placeClip(const Song& song, TrackId track, ClipId clip, Tick position, const IdSource& newId,
                     bool stack = false);
/// A new empty clip of `length` with an instance of it at `position` on `track` (the Draw tool), placed over
/// whatever is there. `clip` and `instance` are the new IDs.
CommandPtr drawClip(const Song& song, TrackId track, Tick position, Tick length, ClipId clip, ArrangementId instance,
                    const IdSource& newId, bool stack = false);
/// A new clip with its contents (an imported MIDI file) and an instance of it at `position` on `track` playing it
/// once, placed over whatever is there. `instance` is the new instance's ID; the clip keeps its own.
CommandPtr placeNewClip(const Song& song, TrackId track, Tick position, Clip clip, ArrangementId instance,
                        const IdSource& newId, bool stack, const std::string& name);
/// Joins the instances in `ids` (all on one track) into one new clip and one instance spanning them
/// (Ctrl/Cmd+J). Loops, offsets, stretch, transpose and velocity scale are baked into the new clip. Audio instances
/// join only when they are pieces of one clip end to end (they become one instance of it); otherwise null.
CommandPtr glue(const Song& song, const IdSet& ids, const NextId& nextId);
/// Makes the timeline clips on `track` that play the same thing share one clip; null when none do. Names and colours
/// don't count.
CommandPtr linkIdenticalClips(const Song& song, TrackId track);
/// Gives each instance in `ids` whose clip is also used elsewhere its own copy of the clip (Ctrl/Cmd+U).
CommandPtr makeUnique(const Song& song, const IdSet& ids, const NextId& nextId);

/// "1 clip" / "3 clips", for command names.
std::string countClips(size_t n);

} // namespace tessera::arrange
