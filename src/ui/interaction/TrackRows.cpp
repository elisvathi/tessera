#include "TrackRows.hpp"

#include "../../model/Sends.hpp"

#include <algorithm>

namespace tessera {

namespace {

/// The parts of `track` the spec knows, or none.
const std::vector<SendFrom>& partsOf(const MixRowsSpec& spec, TrackId track) {
    static const std::vector<SendFrom> none;
    auto it = spec.parts.find(track.value);
    return it == spec.parts.end() ? none : it->second;
}

/// Where a lane's row goes under its track: the part it moves (a Volume or Pan lane on a part, or the part that
/// sends for a Send lane), when the track has that part; otherwise the track itself. A lane whose part is gone stays
/// in sight with the track's own lanes.
SendFrom laneHome(const Track& t, const MixRowsSpec& spec, const AutomationEnvelope& lane) {
    if (lane.part == 0)
        return {};
    for (SendFrom p : partsOf(spec, t.id))
        if (p.key() == lane.part)
            return p;
    return {};
}

/// Whether a lane sits under a send row: a Send lane whose send `t` still has. Other lanes, and Send lanes whose
/// send went, sit with their part or with the track.
bool underSend(const Track& t, const MixRowsSpec& spec, const AutomationEnvelope& lane) {
    if (!lane.sendTo.valid())
        return false;
    SendFrom from = laneHome(t, spec, lane);
    if (from.key() != lane.part)
        return false;
    return std::any_of(t.sends.begin(), t.sends.end(),
                       [&](const Send& s) { return s.to == lane.sendTo && s.from == from; });
}

} // namespace

std::vector<TrackId> mixChildren(const Song& song, const MixRowsSpec& spec, TrackId track, SendFrom part) {
    std::vector<TrackId> out;
    const Track* t = song.findTrack(track);
    if (!t)
        return out;
    for (const AutomationEnvelope& lane : t->automation)
        if (!underSend(*t, spec, lane) && laneHome(*t, spec, lane) == part)
            out.push_back(track);
    if (part.whole())
        for (size_t k = 0; k < partsOf(spec, track).size(); k++)
            out.push_back(track);
    for (const Send& send : t->sends)
        if (send.from == part)
            out.push_back(send.to);
    if (part.whole())
        for (const SendRef& r : sendsInto(song, track))
            out.push_back(r.track);
    return out;
}

std::vector<TrackRow> buildTrackRows(const Song& song, const std::set<uint64_t>& foldedGroups,
                                     const std::set<uint64_t>& openAutomation, bool master, const MixRowsSpec* mix) {
    std::vector<TrackRow> rows;
    std::set<uint64_t> shown;
    auto mixRow = [&](const Track& t, GroupId group, bool inGroup, TrackRow::Mix kind, SendFrom from, TrackId other,
                      int depth) {
        TrackRow r{TrackRow::Kind::Mix, t.id, group, inGroup};
        r.mix = kind;
        r.from = from;
        r.other = other;
        r.depth = depth;
        rows.push_back(r);
    };
    auto laneRow = [&](const Track& t, GroupId group, bool inGroup, const AutomationEnvelope& lane, int depth) {
        TrackRow r{TrackRow::Kind::Lane, t.id, group, inGroup, lane.id};
        r.depth = depth;
        rows.push_back(r);
    };
    // The send rows of `from` (the whole track or a part), each with its Send lanes right under it.
    auto addSends = [&](const Track& t, GroupId group, bool inGroup, SendFrom from, int depth) {
        for (const Send& send : t.sends) {
            if (!(send.from == from))
                continue;
            mixRow(t, group, inGroup, TrackRow::Mix::Send, from, send.to, depth);
            for (const AutomationEnvelope& lane : t.automation)
                if (underSend(t, *mix, lane) && lane.sendTo == send.to && lane.part == from.key())
                    laneRow(t, group, inGroup, lane, depth + 1);
        }
    };
    // Under an open track (§201): its own lanes, then each part (its lanes and sends under its own arrow), then the
    // track's sends with their lanes, the sends it receives, and the add-lane row.
    auto addOpen = [&](const Track& t, GroupId group, bool inGroup) {
        static const MixRowsSpec none;
        const MixRowsSpec& spec = mix ? *mix : none;
        for (const AutomationEnvelope& lane : t.automation)
            if (!underSend(t, spec, lane) && laneHome(t, spec, lane).whole())
                laneRow(t, group, inGroup, lane, 1);
        if (mix) {
            for (SendFrom part : partsOf(*mix, t.id)) {
                mixRow(t, group, inGroup, TrackRow::Mix::Part, part, TrackId(), 1);
                if (!mix->open.count({t.id.value, part.key()}))
                    continue;
                for (const AutomationEnvelope& lane : t.automation)
                    if (!underSend(t, *mix, lane) && laneHome(t, *mix, lane) == part)
                        laneRow(t, group, inGroup, lane, 2);
                addSends(t, group, inGroup, part, 2);
            }
            addSends(t, group, inGroup, SendFrom(), 1);
            for (const SendRef& r : sendsInto(song, t.id))
                mixRow(t, group, inGroup, TrackRow::Mix::Receive, r.send.from, r.track, 1);
        }
        rows.push_back({TrackRow::Kind::AddLane, t.id, group, inGroup});
    };
    auto addTrack = [&](const Track& t, GroupId group, bool inGroup) {
        rows.push_back({TrackRow::Kind::Track, t.id, group, inGroup});
        bool open = mix ? mix->open.count({t.id.value, 0}) > 0 : openAutomation.count(t.id.value) > 0;
        if (open)
            addOpen(t, group, inGroup);
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
    if (master) {
        rows.push_back({TrackRow::Kind::Master, song.master.id, GroupId(), false});
        if (openAutomation.count(song.master.id.value))
            for (const AutomationEnvelope& lane : song.master.automation)
                rows.push_back({TrackRow::Kind::Lane, song.master.id, GroupId(), false, lane.id});
    }
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
