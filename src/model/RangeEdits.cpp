#include "RangeEdits.hpp"

#include <algorithm>
#include <map>

namespace tessera::rangeEdits {

using arrange::Layout;
using arrange::Placed;
using autoEdits::Points;

namespace {

bool contains(const std::vector<TrackId>& tracks, TrackId id) {
    return std::find(tracks.begin(), tracks.end(), id) != tracks.end();
}

/// Instances on `tracks` that `at` falls strictly inside are split there.
Layout splitAt(const Layout& layout, const std::vector<TrackId>& tracks, Tick at, const ClipPool& clips,
               const NextId& nextId) {
    arrange::IdSet across;
    for (const Placed& p : layout)
        if (contains(tracks, p.track) && p.clip.position < at && instanceEnd(p.clip) > at)
            across.insert(p.clip.id);
    if (across.empty())
        return layout;
    return arrange::split(layout, across, at, clips, [&] { return ArrangementId(nextId()); });
}

/// Track lanes of `tracks` rewritten by `change`, as replaceLane commands added to `out`.
template <typename F>
void editLanes(CompoundCommand& out, const Song& song, const std::vector<TrackId>& tracks, F change) {
    for (TrackId id : tracks) {
        const Track* t = song.findTrack(id);
        if (!t)
            continue;
        for (const AutomationEnvelope& lane : t->automation) {
            AutomationEnvelope next = lane;
            next.points = change(lane.points, restValue(lane));
            out.add(replaceLane(song, LaneOwner::ofTrack(id), next, "Edit automation"));
        }
    }
}

/// Moves every point at or after `from` by `delta`.
Points shiftFrom(const Points& points, Tick from, Tick delta) {
    Points out = points;
    for (AutoPoint& p : out)
        if (p.t >= from)
            p.t = std::max<Tick>(0, p.t + delta);
    autoEdits::sortPoints(out, {});
    return out;
}

AutoPoint pointOnCurve(const Points& points, float rest, Tick t) {
    Points copy = points;
    size_t i = autoEdits::insertOnCurve(copy, t, rest);
    return copy[i];
}

} // namespace

Points copySpan(const Points& points, float rest, Tick from, Tick to, Tick at) {
    Points out;
    if (to <= from)
        return out;
    out.push_back(pointOnCurve(points, rest, from));
    for (const AutoPoint& p : points)
        if (p.t > from && p.t < to)
            out.push_back(p);
    AutoPoint end = pointOnCurve(points, rest, to);
    end.curve = CurveType::Linear;
    end.tension = 0;
    out.push_back(end);
    for (AutoPoint& p : out)
        p.t += at - from;
    return out;
}

Points pasteSpan(const Points& points, float rest, const Points& segment, Tick at, Tick end) {
    if (points.empty())
        return autoEdits::replaceRange(points, at, end, segment);
    Points drawn{pointOnCurve(points, rest, at)};
    drawn.insert(drawn.end(), segment.begin(), segment.end());
    drawn.push_back(pointOnCurve(points, rest, end));
    return autoEdits::replaceRange(points, at, end, drawn);
}

Points clearSpan(const Points& points, float rest, Tick from, Tick to) {
    if (points.empty() || to <= from)
        return points;
    AutoPoint a = pointOnCurve(points, rest, from);
    AutoPoint b = pointOnCurve(points, rest, to);
    return autoEdits::replaceRange(points, from, to, {a, b});
}

CommandPtr clearRange(const Song& song, const std::vector<TrackId>& tracks, Tick from, Tick to, const NextId& nextId) {
    if (to <= from)
        return nullptr;
    Layout before = arrange::layoutOf(song);
    Layout cut = splitAt(splitAt(before, tracks, from, song.clips, nextId), tracks, to, song.clips, nextId);
    Layout after;
    for (const Placed& p : cut)
        if (!(contains(tracks, p.track) && p.clip.position >= from && instanceEnd(p.clip) <= to))
            after.push_back(p);
    auto out = std::make_unique<CompoundCommand>("Delete range");
    out->add(arrange::replaceArrangement(song, "Delete range", before, after));
    editLanes(*out, song, tracks, [&](const Points& p, float rest) { return clearSpan(p, rest, from, to); });
    return out->empty() ? nullptr : std::move(out);
}

CommandPtr deleteTime(const Song& song, const std::vector<TrackId>& tracks, Tick from, Tick to, const NextId& nextId) {
    if (to <= from)
        return nullptr;
    Tick length = to - from;
    Layout before = arrange::layoutOf(song);
    Layout cut = splitAt(splitAt(before, tracks, from, song.clips, nextId), tracks, to, song.clips, nextId);
    Layout after;
    for (Placed p : cut) {
        if (contains(tracks, p.track)) {
            if (p.clip.position >= from && instanceEnd(p.clip) <= to)
                continue;
            if (p.clip.position >= to)
                p.clip.position -= length;
        }
        after.push_back(p);
    }
    auto out = std::make_unique<CompoundCommand>("Delete time");
    out->add(arrange::replaceArrangement(song, "Delete time", before, after));
    editLanes(*out, song, tracks, [&](const Points& p, float rest) {
        if (p.empty())
            return p;
        // The curve after the range keeps its start value where the range began.
        AutoPoint a = pointOnCurve(p, rest, from);
        AutoPoint b = pointOnCurve(p, rest, to);
        Points kept;
        for (const AutoPoint& q : p)
            if (q.t < from || q.t > to)
                kept.push_back(q);
        kept.push_back(a);
        kept.push_back(b);
        autoEdits::sortPoints(kept, {});
        Points shifted;
        for (AutoPoint q : kept) {
            if (q.t >= to && !(q == a))
                q.t -= length;
            shifted.push_back(q);
        }
        autoEdits::sortPoints(shifted, {});
        return shifted;
    });
    return out->empty() ? nullptr : std::move(out);
}

CommandPtr insertSilence(const Song& song, const std::vector<TrackId>& tracks, Tick at, Tick length,
                         const NextId& nextId) {
    if (length <= 0)
        return nullptr;
    Layout before = arrange::layoutOf(song);
    Layout after = splitAt(before, tracks, at, song.clips, nextId);
    for (Placed& p : after)
        if (contains(tracks, p.track) && p.clip.position >= at)
            p.clip.position += length;
    auto out = std::make_unique<CompoundCommand>("Insert silence");
    out->add(arrange::replaceArrangement(song, "Insert silence", before, after));
    editLanes(*out, song, tracks, [&](const Points& p, float rest) {
        if (p.empty())
            return p;
        // The value at `at` holds through the gap.
        AutoPoint hold = pointOnCurve(p, rest, at);
        hold.curve = CurveType::Hold;
        Points shifted = shiftFrom(p, at, length);
        AutoPoint resume = pointOnCurve(p, rest, at);
        resume.t = at + length;
        shifted.push_back(hold);
        shifted.push_back(resume);
        autoEdits::sortPoints(shifted, {});
        return shifted;
    });
    return out->empty() ? nullptr : std::move(out);
}

CommandPtr duplicateRange(const Song& song, const std::vector<TrackId>& tracks, Tick from, Tick to,
                          const NextId& nextId) {
    if (to <= from)
        return nullptr;
    Tick length = to - from;
    Layout before = arrange::layoutOf(song);
    Layout cut = splitAt(splitAt(before, tracks, from, song.clips, nextId), tracks, to, song.clips, nextId);
    Layout after = before;
    arrange::IdSet copies;
    for (const Placed& p : cut) {
        if (!contains(tracks, p.track) || p.clip.position < from || instanceEnd(p.clip) > to)
            continue;
        Placed copy = p;
        copy.clip.id = ArrangementId(nextId());
        copy.clip.position += length;
        after.push_back(copy);
        copies.insert(copy.clip.id);
    }
    // The pieces inside the range come from the split layout; the originals stay whole.
    after = arrange::resolveOverlaps(after, copies, song.clips, [&] { return ArrangementId(nextId()); });
    auto out = std::make_unique<CompoundCommand>("Duplicate range");
    out->add(arrange::replaceArrangement(song, "Duplicate range", before, after));
    editLanes(*out, song, tracks, [&](const Points& p, float rest) {
        if (p.empty())
            return p;
        return pasteSpan(p, rest, copySpan(p, rest, from, to, to), to, to + length);
    });
    return out->empty() ? nullptr : std::move(out);
}

CommandPtr automationFollowing(const Song& song, const Layout& before, const Layout& after) {
    struct Move {
        TrackId fromTrack, toTrack;
        Tick from, to, at;
        bool copy;
    };
    std::vector<Move> moves;
    for (const Placed& a : after) {
        const Placed* b = arrange::find(before, a.clip.id);
        if (b) {
            // Moved whole: same length and content start, somewhere else. (Trims change those, so they don't count.)
            bool moved = b->track != a.track || b->clip.position != a.clip.position;
            if (moved && b->clip.length == a.clip.length && b->clip.offset == a.clip.offset)
                moves.push_back({b->track, a.track, b->clip.position, instanceEnd(b->clip), a.clip.position, false});
            continue;
        }
        // A new instance: a copy of the nearest instance of the same clip and shape, if there is one.
        const Placed* source = nullptr;
        for (const Placed& c : before)
            if (c.clip.clip == a.clip.clip && c.clip.length == a.clip.length && c.clip.offset == a.clip.offset &&
                (!source ||
                 std::llabs(c.clip.position - a.clip.position) < std::llabs(source->clip.position - a.clip.position)))
                source = &c;
        if (source)
            moves.push_back(
                {source->track, a.track, source->clip.position, instanceEnd(source->clip), a.clip.position, true});
    }
    if (moves.empty())
        return nullptr;

    // Work on copies of the lanes, then record what changed.
    std::map<std::pair<uint64_t, uint64_t>, AutomationEnvelope> lanes;
    auto laneOf = [&](TrackId track, const AutomationEnvelope& l) -> AutomationEnvelope& {
        auto key = std::make_pair(track.value, l.id.value);
        auto it = lanes.find(key);
        if (it == lanes.end())
            it = lanes.emplace(key, l).first;
        return it->second;
    };
    // Every segment is read from the lanes as they were, so moves in one edit don't pick up each other's points.
    for (const Move& m : moves) {
        const Track* src = song.findTrack(m.fromTrack);
        const Track* dst = song.findTrack(m.toTrack);
        if (!src || !dst)
            continue;
        for (const AutomationEnvelope& from : src->automation) {
            bool any = std::any_of(from.points.begin(), from.points.end(),
                                   [&](const AutoPoint& p) { return p.t >= m.from && p.t < m.to; });
            if (!any)
                continue;
            Points segment = copySpan(from.points, restValue(from), m.from, m.to, m.at);
            if (!m.copy) {
                AutomationEnvelope& source = laneOf(m.fromTrack, from);
                source.points = clearSpan(source.points, restValue(from), m.from, m.to);
            }
            for (const AutomationEnvelope& to : dst->automation)
                if (sameTarget(to, from)) {
                    AutomationEnvelope& target = laneOf(m.toTrack, to);
                    target.points = pasteSpan(target.points, restValue(to), segment, m.at, m.at + (m.to - m.from));
                }
        }
    }
    auto out = std::make_unique<CompoundCommand>("Move automation with clips");
    for (const auto& [key, lane] : lanes)
        out->add(replaceLane(song, LaneOwner::ofTrack(TrackId(key.first)), lane, "Move automation with clips"));
    return out->empty() ? nullptr : std::move(out);
}

} // namespace tessera::rangeEdits
