#pragma once

#include "Commands.hpp"
#include "Song.hpp"
#include "TrackCommands.hpp"

#include <functional>
#include <set>
#include <string>
#include <vector>

/// A clip's edit list (docs/design/AUDIO_EDITING.md §2): pure functions that cut, move and trim its parts, and the
/// commands that store the result. Linked copies share the list, because it lives in the clip. Files never change.
namespace tessera::regions {

using List = std::vector<AudioRegion>;
using Ids = std::set<AudioRegionId>;
using NewId = std::function<AudioRegionId()>;

/// The shortest a part can be, in ticks.
constexpr Tick kMinLength = 10;

/// The sample rate and length of each file a clip plays: its own, and one per take. Takes can come from another
/// recording session, so they can have another rate.
struct Files {
    double rate = 48000;
    int64_t frames = 0;
    std::vector<std::pair<double, int64_t>> takes;

    Files() = default;
    Files(double rate, int64_t frames = 0) : rate(rate), frames(frames) {}

    /// The sample rate of take `take` (-1 the clip's own).
    double rateOf(int take) const { return take >= 0 && size_t(take) < takes.size() ? takes[size_t(take)].first : rate; }
    /// The length in frames of take `take` (-1 the clip's own).
    int64_t framesOf(int take) const {
        return take >= 0 && size_t(take) < takes.size() ? takes[size_t(take)].second : frames;
    }
};

/// The files of `a` in `song`.
Files filesOf(const Song& song, const AudioContent& a);

/// The clip's parts, in order. A clip with no edits has one, over the whole clip, with an invalid ID until it is stored.
List of(const Clip& clip, const Files& files);
/// The frame of its take that part `r` plays at clip tick `tick`, through that take's warp markers. Past the part's
/// ends it carries on, which is how the editor shows the audio a trim would bring back.
double frameAt(const AudioContent& a, const AudioRegion& r, double tick, const Files& files);
/// The frame take `take` plays at clip tick `tick` when nothing has been moved: how a take lines up with its clip.
double takeFrame(const AudioContent& a, int take, double tick, const Files& files);
/// The clip tick of take `take`'s frame `frame` when nothing has been moved: the inverse of takeFrame().
double takeTick(const AudioContent& a, int take, double frame, const Files& files);
/// How far part `r` is ahead of its take in clip ticks: tick + shift is the tick the unmoved take plays this audio at.
/// Only for parts that play forwards.
double shift(const AudioContent& a, const AudioRegion& r, const Files& files);
/// The warp markers of take `take` (-1 the clip's own).
const std::vector<WarpMarker>& warpsOf(const AudioContent& a, int take);
/// The part that plays at `tick`, skipping muted parts when asked; null in a gap. Where two overlap, the later one.
const AudioRegion* at(const List& list, double tick, bool includeMuted = false);

/// Each part that `tick` falls inside becomes two. The right part gets a new ID.
List cut(const List& list, const AudioContent& a, Tick tick, const Files& files, const NewId& newId);
/// Part `id` starts at `to`, or as near as its file and its length allow; the audio stays where it was.
List trimStart(const List& list, AudioRegionId id, Tick to, const AudioContent& a, const Files& files);
/// Part `id` ends at `to`, or as near as its file and its length allow.
List trimEnd(const List& list, AudioRegionId id, Tick to, const AudioContent& a, const Files& files);
/// The audio inside part `id` slides `frames` later in the file, inside the file's ends.
List slip(const List& list, AudioRegionId id, int64_t frames, const AudioContent& a, const Files& files);
/// Parts in `ids` move together by `delta` ticks, staying inside the clip.
List move(const List& list, const Ids& ids, Tick delta, Tick clipLength);
/// The overlap rule inside a clip, as on the timeline: a changed part wins over a part it covers, one it sits inside
/// is split around it, and where three would sound the earliest stops. Other overlaps stay: they are crossfades.
List settle(const List& list, const Ids& changed, const AudioContent& a, const Files& files, const NewId& newId);

/// The ticks of the clip where take `take` has audio, as [from, to). Empty when it has none there.
std::pair<Tick, Tick> takeSpan(const Clip& clip, int take, const Files& files);
/// Comping: ticks [from, to) now play take `take`, lined up as recorded. Parts there are cut back or removed, gaps are
/// filled, and a neighbour already playing on in that take joins up.
List comp(const List& list, const AudioContent& a, int take, Tick from, Tick to, const Files& files, const NewId& newId);
/// Parts `ids` play take `take` instead, in the same place, lined up as recorded.
List retake(const List& list, const Ids& ids, int take, const AudioContent& a, const Files& files);

/// Stores `list` as the clip's parts, as one undo step named `name`. A list reaching past the clip's end makes the clip
/// longer, and a loop over the whole clip follows. Null when nothing changed.
CommandPtr setRegions(const Song& song, ClipId clip, List list, const std::string& name, const NextId& nextId);
/// Removes parts `ids` and closes the gaps: later parts move earlier, the clip gets shorter, and timeline copies that
/// played the whole clip shrink with it. Null when no part would be left.
CommandPtr removeClosingGaps(const Song& song, ClipId clip, const Ids& ids, const NextId& nextId);

/// The whole clip plays take `take`, leaving any comp behind. Null when it already does.
CommandPtr useTake(const Song& song, ClipId clip, int take, const NextId& nextId);
/// Adds `take` to the clip. A clip playing one take from end to end moves on to the new one, as a new pass should; a
/// comp stays as it is. Null when the clip isn't audio.
CommandPtr addTake(const Song& song, ClipId clip, AudioTake take, const NextId& nextId);
/// Removes take `take`. Parts playing it go back to the clip's own take. The file stays until Remove unused audio.
CommandPtr removeTake(const Song& song, ClipId clip, int take);
/// Renames take `take`. An empty name means its number.
CommandPtr renameTake(const Song& song, ClipId clip, int take, const std::string& name);

/// A take's name: its own, or "Take 2", where take -1 is "Take 1".
std::string takeName(const AudioContent& a, int take);
/// What the title of a clip with takes shows: "take 3", or "comp" when parts play different takes.
std::string playingText(const Clip& clip);

} // namespace tessera::regions
