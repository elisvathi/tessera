#pragma once

#include "../../model/Song.hpp"

#include <map>
#include <set>
#include <utility>
#include <vector>

/// The rows of the track list: group headers and tracks, with folded groups hiding their tracks.
/// Pure, so it is unit tested.
namespace tessera {

struct TrackRow {
    /// Lane: one of a track's automation lanes, under it. AddLane: the "+ Add lane" row after them. Master: the
    /// thin Master row after every track (`track` is the master's ID), its lanes under it. Mix: a part, a send or a
    /// send it receives, under its track (docs/DECISIONS.md §197).
    enum class Kind { Track, Group, Lane, AddLane, Master, Mix };
    Kind kind = Kind::Track;
    TrackId track; // Track, Lane, AddLane, Mix
    GroupId group; // Group, or the group a track row belongs to
    bool inGroup = false;
    AutomationId lane; // Lane

    /// What a Mix row is: one part of the track (a drum lane, an input), one of its sends (from the whole track or
    /// a part), or a send from another track into it.
    enum class Mix : uint8_t { Part, Send, Receive };
    Mix mix = Mix::Part;
    SendFrom from; // Part: the part; Send: the part that sends; Receive: the sender's part
    TrackId other; // Send: the track it goes to; Receive: the track that sends
    int depth = 0; // 1 right under the track, 2 under a part

    bool isTrack() const { return kind == Kind::Track; }
    /// A row under a track that isn't a track: an automation lane, the add-lane row, or a mix row.
    bool isLane() const { return kind == Kind::Lane || kind == Kind::AddLane || kind == Kind::Mix; }
};

/// What the rows need to show tracks' mix rows: the parts each track has a mixer strip for (by TrackId value), and
/// which folds are open, as (TrackId value, part key) with key 0 for the track itself.
struct MixRowsSpec {
    std::map<uint64_t, std::vector<SendFrom>> parts;
    std::set<std::pair<uint64_t, uint64_t>> open;
};

/// The rows a fold would open: under a track (`part` whole) its parts, its own sends and the sends it receives;
/// under a part its sends. Each entry is the track whose colour that row's dot wears: the track itself for a part,
/// the receiver for a send, the sender for a received send.
std::vector<TrackId> mixChildren(const Song& song, const MixRowsSpec& spec, TrackId track, SendFrom part = {});

/// A group's header comes right before its first track. Empty groups go last. Tracks in `openAutomation` are
/// followed by their automation lanes and an add-lane row. With `master`, the Master row comes last, followed by its
/// lanes when the master is in `openAutomation` (no add-lane row: its one lane is added from its level bar).
/// With `mix`, an open track is followed by its mix rows (before its lanes): each part, with its sends under it when
/// that part is open, then the track's own sends, then the sends it receives.
std::vector<TrackRow> buildTrackRows(const Song& song, const std::set<uint64_t>& foldedGroups,
                                     const std::set<uint64_t>& openAutomation = {}, bool master = false,
                                     const MixRowsSpec* mix = nullptr);

/// Every track from `a` to `b` in display order, both included (Shift+click range selection).
std::vector<TrackId> trackRange(const Song& song, TrackId a, TrackId b);

/// Toggles `id` in `selection` (Ctrl/Cmd+click).
std::vector<TrackId> toggled(std::vector<TrackId> selection, TrackId id);

} // namespace tessera
