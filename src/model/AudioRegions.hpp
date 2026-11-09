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

/// The clip's parts, in order. A clip with no edits has one, over the whole clip, with an invalid ID until it is stored.
List of(const Clip& clip, double rate);
/// The frame of its file that part `r` plays at clip tick `tick`, through the clip's warp markers. Past the part's
/// ends it carries on, which is how the editor shows the audio a trim would bring back.
double frameAt(const AudioContent& a, const AudioRegion& r, double tick, double rate);
/// How far part `r` is ahead of its file in clip ticks: tick + shift is the tick the unedited clip plays this audio at.
/// Only for parts that play forwards.
double shift(const AudioContent& a, const AudioRegion& r, double rate);
/// The part that plays at `tick`, skipping muted parts when asked; null in a gap. Where two overlap, the later one.
const AudioRegion* at(const List& list, double tick, bool includeMuted = false);

/// Each part that `tick` falls inside becomes two. The right part gets a new ID.
List cut(const List& list, const AudioContent& a, Tick tick, double rate, const NewId& newId);
/// Part `id` starts at `to`, or as near as its file and its length allow; the audio stays where it was.
List trimStart(const List& list, AudioRegionId id, Tick to, const AudioContent& a, double rate, int64_t fileFrames);
/// Part `id` ends at `to`, or as near as its file and its length allow.
List trimEnd(const List& list, AudioRegionId id, Tick to, const AudioContent& a, double rate, int64_t fileFrames);
/// The audio inside part `id` slides `frames` later in the file, inside the file's ends.
List slip(const List& list, AudioRegionId id, int64_t frames, const AudioContent& a, double rate, int64_t fileFrames);
/// Parts in `ids` move together by `delta` ticks, staying inside the clip.
List move(const List& list, const Ids& ids, Tick delta, Tick clipLength);
/// The overlap rule inside a clip, as on the timeline: a changed part wins over a part it covers, one it sits inside
/// is split around it, and where three would sound the earliest stops. Other overlaps stay: they are crossfades.
List settle(const List& list, const Ids& changed, const AudioContent& a, double rate, const NewId& newId);

/// Stores `list` as the clip's parts, as one undo step named `name`. A list reaching past the clip's end makes the clip
/// longer, and a loop over the whole clip follows. Null when nothing changed.
CommandPtr setRegions(const Song& song, ClipId clip, List list, const std::string& name, const NextId& nextId);
/// Removes parts `ids` and closes the gaps: later parts move earlier, the clip gets shorter, and timeline copies that
/// played the whole clip shrink with it. Null when no part would be left.
CommandPtr removeClosingGaps(const Song& song, ClipId clip, const Ids& ids, const NextId& nextId);

} // namespace tessera::regions
