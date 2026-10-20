#include "TrackCommands.hpp"

#include "DeviceCommands.hpp"
#include "LauncherCommands.hpp"

#include "TrackColors.hpp"

#include <algorithm>
#include <cassert>
#include <set>

namespace tessera {

namespace {

std::vector<ClipId> clipsUsedBy(const Track& t) {
    std::vector<ClipId> out;
    for (const ClipSlot& s : t.slots)
        if (s.clip && std::find(out.begin(), out.end(), *s.clip) == out.end())
            out.push_back(*s.clip);
    for (const ArrangerClip& a : t.arrangement)
        if (std::find(out.begin(), out.end(), a.clip) == out.end())
            out.push_back(a.clip);
    return out;
}

bool usedByOtherTrack(const Song& song, TrackId self, ClipId clip) {
    for (const Track& t : song.tracks) {
        if (t.id == self)
            continue;
        std::vector<ClipId> used = clipsUsedBy(t);
        if (std::find(used.begin(), used.end(), clip) != used.end())
            return true;
    }
    return false;
}

int indexIn(const std::vector<TrackId>& order, TrackId id) {
    auto it = std::find(order.begin(), order.end(), id);
    return it == order.end() ? -1 : int(it - order.begin());
}

/// `ids` sorted by their position in the song.
std::vector<TrackId> inSongOrder(const Song& song, const std::vector<TrackId>& ids) {
    std::vector<TrackId> out;
    for (const Track& t : song.tracks)
        if (std::find(ids.begin(), ids.end(), t.id) != ids.end())
            out.push_back(t.id);
    return out;
}

void removeFromGroups(std::vector<TrackGroup>& groups, const std::vector<TrackId>& ids) {
    for (TrackGroup& g : groups)
        g.tracks.erase(std::remove_if(g.tracks.begin(), g.tracks.end(),
                                      [&](TrackId t) { return std::find(ids.begin(), ids.end(), t) != ids.end(); }),
                       g.tracks.end());
}

std::string countTracks(size_t n) { return std::to_string(n) + (n == 1 ? " track" : " tracks"); }

} // namespace

const TrackGroup* groupOf(const Song& song, TrackId track) {
    for (const TrackGroup& g : song.groups)
        if (std::find(g.tracks.begin(), g.tracks.end(), track) != g.tracks.end())
            return &g;
    return nullptr;
}

TrackLayout TrackLayout::of(const Song& song) {
    TrackLayout l;
    for (const Track& t : song.tracks)
        l.order.push_back(t.id);
    l.groups = song.groups;
    return l;
}

void ChangeTrackLayout::set(Song& song, const TrackLayout& layout) {
    std::vector<Track> reordered;
    reordered.reserve(song.tracks.size());
    for (TrackId id : layout.order)
        if (Track* t = song.findTrack(id))
            reordered.push_back(std::move(*t));
    assert(reordered.size() == song.tracks.size() && "a layout must name every track");
    song.tracks = std::move(reordered);
    song.groups = layout.groups;
    // Group members always follow the track order, so a group reads top to bottom like the arranger.
    for (TrackGroup& g : song.groups) {
        std::vector<TrackId> members;
        for (const Track& t : song.tracks)
            if (std::find(g.tracks.begin(), g.tracks.end(), t.id) != g.tracks.end())
                members.push_back(t.id);
        g.tracks = std::move(members);
    }
}

// ---- Add / remove -------------------------------------------------------------------------------------------

void AddTrack::apply(Song& song) {
    int at = std::clamp(index, 0, int(song.tracks.size()));
    song.tracks.insert(song.tracks.begin() + at, track);
    for (const Clip& c : clips)
        song.clips[c.id] = c;
}

void AddTrack::revert(Song& song) {
    song.tracks.erase(
        std::remove_if(song.tracks.begin(), song.tracks.end(), [&](const Track& t) { return t.id == track.id; }),
        song.tracks.end());
    for (const Clip& c : clips)
        song.clips.erase(c.id);
    removeFromGroups(song.groups, {track.id});
}

void RemoveTrack::apply(Song& song) {
    index = song.trackIndex(id);
    assert(index >= 0 && "RemoveTrack on a missing track");
    if (index < 0)
        return;
    track = song.tracks[size_t(index)];
    clips.clear();
    for (ClipId c : clipsUsedBy(track))
        if (!usedByOtherTrack(song, id, c))
            if (const Clip* clip = song.findClip(c))
                clips.push_back(*clip);
    memberships.clear();
    for (TrackGroup& g : song.groups) {
        auto it = std::find(g.tracks.begin(), g.tracks.end(), id);
        if (it != g.tracks.end()) {
            memberships.push_back({g.id, size_t(it - g.tracks.begin())});
            g.tracks.erase(it);
        }
    }
    song.tracks.erase(song.tracks.begin() + index);
    for (const Clip& c : clips)
        song.clips.erase(c.id);
    // Only song modulators reach into other tracks, so theirs are the only routes that can break.
    songModulators = song.modulators;
    pruneRoutes(song);
}

void RemoveTrack::revert(Song& song) {
    if (index < 0)
        return;
    song.modulators = songModulators;
    song.tracks.insert(song.tracks.begin() + std::min<size_t>(size_t(index), song.tracks.size()), track);
    for (const Clip& c : clips)
        song.clips[c.id] = c;
    for (const auto& [groupId, position] : memberships)
        for (TrackGroup& g : song.groups)
            if (g.id == groupId)
                g.tracks.insert(g.tracks.begin() + std::min(position, g.tracks.size()), id);
}

// ---- Factories ----------------------------------------------------------------------------------------------

CommandPtr addTrack(const Song& song, TrackType type, int index, const NextId& nextId) {
    if (int(song.tracks.size()) >= kMaxTracks)
        return nullptr;
    Track t;
    t.id = TrackId(nextId());
    t.type = type;
    int sameType =
        int(std::count_if(song.tracks.begin(), song.tracks.end(), [&](const Track& o) { return o.type == type; }));
    t.name = (type == TrackType::Drum ? "Drums " : "Track ") + std::to_string(sameType + 1);
    t.color = trackColorForIndex(int(song.tracks.size()));
    t.slots.resize(song.scenes.size());
    std::string name = type == TrackType::Drum ? "Add drum track" : "Add instrument track";
    return std::make_unique<AddTrack>(name, index, std::move(t), std::vector<Clip>{});
}

CommandPtr duplicateTracks(const Song& song, const std::vector<TrackId>& ids, const NextId& nextId) {
    std::vector<TrackId> sources = inSongOrder(song, ids);
    if (sources.empty() || song.tracks.size() + sources.size() > size_t(kMaxTracks))
        return nullptr;

    auto compound = std::make_unique<CompoundCommand>("Duplicate " + countTracks(sources.size()));
    // The layout once the copies are in, and where each copy belongs.
    TrackLayout added = TrackLayout::of(song);
    std::vector<std::pair<TrackId, TrackId>> copies; // original -> copy

    for (TrackId sourceId : sources) {
        const Track& source = *song.findTrack(sourceId);
        Track copy = source;
        copy.id = TrackId(nextId());
        copy.name = source.name + " copy";
        copy.solo = false;

        // Copied clips are independent: new IDs for clips, notes and lanes.
        std::vector<Clip> clips;
        std::map<ClipId, ClipId> remap;
        for (ClipId c : clipsUsedBy(source)) {
            const Clip* original = song.findClip(c);
            if (!original)
                continue;
            Clip clip = copyClip(*original, nextId);
            remap[c] = clip.id;
            clips.push_back(std::move(clip));
        }
        for (ClipSlot& s : copy.slots)
            if (s.clip)
                s.clip = remap[*s.clip];
        for (ArrangerClip& a : copy.arrangement) {
            a.id = ArrangementId(nextId());
            a.clip = remap[a.clip];
        }
        for (AutomationEnvelope& a : copy.automation)
            a.id = AutomationId(nextId());

        // The copy gets its own devices and modulators; everything that pointed into the original's chain
        // points into the copy's.
        ChainRemap chain;
        renewIds(copy.devices, copy.modulators, nextId, chain);
        retargetRoutes(copy.devices, copy.modulators, chain);
        auto ownTrack = [&](Modulator& m) {
            for (Modulation& r : m.routes)
                if (r.kind == Modulation::Kind::AutoChannel && r.track == sourceId)
                    r.track = copy.id;
        };
        for (Modulator& m : copy.modulators)
            ownTrack(m);
        for (Device& d : copy.devices)
            for (Modulator& m : d.modulators)
                ownTrack(m);
        for (AutomationEnvelope& a : copy.automation)
            retarget(a.paramTarget, chain);
        for (Clip& clip : clips)
            for (AutomationEnvelope& a : clip.automation)
                retarget(a.paramTarget, chain);

        int at = indexIn(added.order, sourceId) + 1;
        added.order.insert(added.order.begin() + at, copy.id);
        copies.push_back({sourceId, copy.id});
        compound->add(std::make_unique<AddTrack>("Duplicate track", at, std::move(copy), std::move(clips)));
    }

    // Copies join their original's group.
    TrackLayout grouped = added;
    for (const auto& [original, copy] : copies)
        for (TrackGroup& g : grouped.groups)
            if (std::find(g.tracks.begin(), g.tracks.end(), original) != g.tracks.end())
                g.tracks.push_back(copy);
    if (!(grouped == added))
        compound->add(std::make_unique<ChangeTrackLayout>("Group copies", added, grouped));
    return compound;
}

CommandPtr removeTracks(const Song& song, const std::vector<TrackId>& ids) {
    std::vector<TrackId> doomed = inSongOrder(song, ids);
    if (doomed.empty() || doomed.size() >= song.tracks.size())
        return nullptr;
    auto compound = std::make_unique<CompoundCommand>("Delete " + countTracks(doomed.size()));
    for (TrackId id : doomed)
        compound->add(std::make_unique<RemoveTrack>(id));
    return compound;
}

CommandPtr moveTracks(const Song& song, const std::vector<TrackId>& ids, int before, std::optional<GroupId> group) {
    std::vector<TrackId> moving = inSongOrder(song, ids);
    if (moving.empty())
        return nullptr;
    TrackLayout from = TrackLayout::of(song);
    TrackLayout to = from;
    // Insert where `before` pointed, counting only the tracks that stay.
    int insertAt = 0;
    std::vector<TrackId> staying;
    for (size_t i = 0; i < from.order.size(); i++) {
        TrackId id = from.order[i];
        if (std::find(moving.begin(), moving.end(), id) != moving.end())
            continue;
        if (int(i) < before)
            insertAt++;
        staying.push_back(id);
    }
    staying.insert(staying.begin() + insertAt, moving.begin(), moving.end());
    to.order = staying;
    removeFromGroups(to.groups, moving);
    if (group)
        for (TrackGroup& g : to.groups)
            if (g.id == *group)
                g.tracks.insert(g.tracks.end(), moving.begin(), moving.end());
    // Compare with members sorted by order, as ChangeTrackLayout will store them.
    Song preview = song;
    ChangeTrackLayout probe("", from, to);
    probe.apply(preview);
    if (TrackLayout::of(preview) == from)
        return nullptr;
    return std::make_unique<ChangeTrackLayout>(moving.size() == 1 ? "Move track" : "Move " + countTracks(moving.size()),
                                               from, to);
}

CommandPtr groupTracks(const Song& song, const std::vector<TrackId>& ids, GroupId id, const std::string& name,
                       uint32_t color) {
    std::vector<TrackId> members = inSongOrder(song, ids);
    if (members.empty() || !id.valid())
        return nullptr;
    TrackLayout from = TrackLayout::of(song);
    TrackLayout to;
    // Gather the members at the first one's position.
    for (TrackId t : from.order) {
        bool member = std::find(members.begin(), members.end(), t) != members.end();
        if (t == members.front())
            to.order.insert(to.order.end(), members.begin(), members.end());
        else if (!member)
            to.order.push_back(t);
    }
    to.groups = from.groups;
    removeFromGroups(to.groups, members);
    TrackGroup g;
    g.id = id;
    g.name = name;
    g.color = color;
    g.tracks = members;
    to.groups.push_back(g);
    return std::make_unique<ChangeTrackLayout>("Group " + countTracks(members.size()), from, to);
}

CommandPtr ungroup(const Song& song, GroupId id) {
    TrackLayout from = TrackLayout::of(song);
    TrackLayout to = from;
    auto it = std::find_if(to.groups.begin(), to.groups.end(), [&](const TrackGroup& g) { return g.id == id; });
    if (it == to.groups.end())
        return nullptr;
    to.groups.erase(it);
    return std::make_unique<ChangeTrackLayout>("Ungroup", from, to);
}

} // namespace tessera
