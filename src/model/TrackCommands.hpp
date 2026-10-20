#pragma once

#include "Commands.hpp"

#include <functional>
#include <optional>

/// Commands that add, remove, reorder and group tracks (DESIGN.md §4 "Tracks are dynamic").
namespace tessera {

/// Hands out fresh ID values (the module's shared ID counter).
using NextId = std::function<uint64_t()>;

/// The order of tracks and their grouping: everything a reorder, group or ungroup changes.
struct TrackLayout {
    std::vector<TrackId> order;
    std::vector<TrackGroup> groups;

    static TrackLayout of(const Song& song);
    bool operator==(const TrackLayout& o) const { return order == o.order && groups == o.groups; }
};

/// Switches between two layouts of the same tracks. Each group's member list follows the track order.
class ChangeTrackLayout : public Command {
  public:
    ChangeTrackLayout(std::string name, TrackLayout before, TrackLayout after)
        : label(std::move(name)), before(std::move(before)), after(std::move(after)) {}

    void apply(Song& song) override { set(song, after); }
    void revert(Song& song) override { set(song, before); }
    std::string name() const override { return label; }
    ChangeSet changes() const override { return ChangeSet::all(); }

  private:
    static void set(Song& song, const TrackLayout& layout);
    std::string label;
    TrackLayout before, after;
};

/// Inserts a track (and clips that only it uses) at a position.
class AddTrack : public Command {
  public:
    AddTrack(std::string name, int index, Track track, std::vector<Clip> clips)
        : label(std::move(name)), index(index), track(std::move(track)), clips(std::move(clips)) {}

    void apply(Song& song) override;
    void revert(Song& song) override;
    std::string name() const override { return label; }
    ChangeSet changes() const override { return ChangeSet::all(); }

  private:
    std::string label;
    int index;
    Track track;
    std::vector<Clip> clips;
};

/// Removes a track, the clips only it uses, and its group membership. Undo puts all of it back with the same
/// IDs, so output modules bound to the track find it again.
class RemoveTrack : public Command {
  public:
    explicit RemoveTrack(TrackId id) : id(id) {}

    void apply(Song& song) override;
    void revert(Song& song) override;
    std::string name() const override { return "Delete track"; }
    ChangeSet changes() const override { return ChangeSet::all(); }

  private:
    TrackId id;
    // Captured on apply
    int index = -1;
    Track track;
    std::vector<Clip> clips;
    std::vector<std::pair<GroupId, size_t>> memberships; // group and position within it
    std::vector<Modulator> songModulators;               // before song modulators lost their routes into the track
};

/// A new empty track of `type` at `index` (clamped). Null at kMaxTracks.
CommandPtr addTrack(const Song& song, TrackType type, int index, const NextId& nextId);

/// Copies of tracks, each placed right after its original and in the same group. Clips are copied too, as
/// independent clips (editing the copy never changes the original). Null at kMaxTracks.
CommandPtr duplicateTracks(const Song& song, const std::vector<TrackId>& ids, const NextId& nextId);

/// Deletes tracks. Null when that would remove every track: a song always keeps at least one.
CommandPtr removeTracks(const Song& song, const std::vector<TrackId>& ids);

/// Moves tracks, keeping their relative order, so they sit before the track at `before` in the current
/// order (`before` == tracks.size() means the end). `group` is the group they end up in (none: top level).
CommandPtr moveTracks(const Song& song, const std::vector<TrackId>& ids, int before, std::optional<GroupId> group);

/// Puts tracks into a new group, gathered together at the position of the first one.
CommandPtr groupTracks(const Song& song, const std::vector<TrackId>& ids, GroupId id, const std::string& name,
                       uint32_t color);

/// Removes a group; its tracks stay where they are.
CommandPtr ungroup(const Song& song, GroupId id);

/// The group a track belongs to, if any.
const TrackGroup* groupOf(const Song& song, TrackId track);

} // namespace tessera
