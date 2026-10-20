#pragma once

#include "../../model/Song.hpp"

#include <set>
#include <vector>

/// The rows of the track list: group headers and tracks, with folded groups hiding their tracks.
/// Pure, so it is unit tested.
namespace tessera {

struct TrackRow {
    /// Lane: one of a track's automation lanes, under it. AddLane: the "+ Add lane" row after them.
    enum class Kind { Track, Group, Lane, AddLane };
    Kind kind = Kind::Track;
    TrackId track; // Track, Lane, AddLane
    GroupId group; // Group, or the group a track row belongs to
    bool inGroup = false;
    AutomationId lane; // Lane

    bool isTrack() const { return kind == Kind::Track; }
    bool isLane() const { return kind == Kind::Lane || kind == Kind::AddLane; }
};

/// A group's header comes right before its first track. Empty groups go last. Tracks in `openAutomation` are
/// followed by their automation lanes and an add-lane row.
std::vector<TrackRow> buildTrackRows(const Song& song, const std::set<uint64_t>& foldedGroups,
                                     const std::set<uint64_t>& openAutomation = {});

/// Every track from `a` to `b` in display order, both included (Shift+click range selection).
std::vector<TrackId> trackRange(const Song& song, TrackId a, TrackId b);

/// Toggles `id` in `selection` (Ctrl/Cmd+click).
std::vector<TrackId> toggled(std::vector<TrackId> selection, TrackId id);

} // namespace tessera
