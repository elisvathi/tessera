#include "TrackRows.hpp"

#include <algorithm>

namespace tessera {

std::vector<TrackRow> buildTrackRows(const Song& song, const std::set<uint64_t>& foldedGroups,
                                     const std::set<uint64_t>& openAutomation) {
    std::vector<TrackRow> rows;
    std::set<uint64_t> shown;
    auto addTrack = [&](const Track& t, GroupId group, bool inGroup) {
        rows.push_back({TrackRow::Kind::Track, t.id, group, inGroup});
        if (!openAutomation.count(t.id.value))
            return;
        for (const AutomationEnvelope& lane : t.automation)
            rows.push_back({TrackRow::Kind::Lane, t.id, group, inGroup, lane.id});
        rows.push_back({TrackRow::Kind::AddLane, t.id, group, inGroup});
    };
    for (const Track& t : song.tracks) {
        const TrackGroup* group = nullptr;
        for (const TrackGroup& g : song.groups)
            if (std::find(g.tracks.begin(), g.tracks.end(), t.id) != g.tracks.end())
                group = &g;
        if (!group) {
            addTrack(t, GroupId(), false);
            continue;
        }
        if (shown.insert(group->id.value).second)
            rows.push_back({TrackRow::Kind::Group, TrackId(), group->id, false});
        if (!foldedGroups.count(group->id.value))
            addTrack(t, group->id, true);
    }
    for (const TrackGroup& g : song.groups)
        if (!shown.count(g.id.value))
            rows.push_back({TrackRow::Kind::Group, TrackId(), g.id, false});
    return rows;
}

std::vector<TrackId> trackRange(const Song& song, TrackId a, TrackId b) {
    int ia = song.trackIndex(a), ib = song.trackIndex(b);
    if (ia < 0 || ib < 0)
        return ib >= 0 ? std::vector<TrackId>{b} : std::vector<TrackId>{};
    if (ia > ib)
        std::swap(ia, ib);
    std::vector<TrackId> out;
    for (int i = ia; i <= ib; i++)
        out.push_back(song.tracks[size_t(i)].id);
    return out;
}

std::vector<TrackId> toggled(std::vector<TrackId> selection, TrackId id) {
    auto it = std::find(selection.begin(), selection.end(), id);
    if (it != selection.end())
        selection.erase(it);
    else
        selection.push_back(id);
    return selection;
}

} // namespace tessera
