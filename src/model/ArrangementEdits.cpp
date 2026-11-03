#include "ArrangementEdits.hpp"

#include "LauncherCommands.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

namespace tessera::arrange {

namespace {

Tick roundTick(double t) { return Tick(std::llround(t)); }

const Clip* clipOf(const ClipPool& clips, ClipId id) {
    auto it = clips.find(id);
    return it == clips.end() ? nullptr : &it->second;
}

/// Moves an instance's start to `start`, keeping its content in place on the timeline.
void trimStartTo(ArrangerClip& a, Tick start, const ClipPool& clips) {
    Tick d = start - a.position;
    Tick offset = a.offset + roundTick(double(d) / double(a.stretch));
    const Clip* clip = clipOf(clips, a.clip);
    a.offset = clip ? normalizeOffset(*clip, offset) : std::max<Tick>(0, offset);
    a.position = start;
    a.length -= d;
}

bool sortsBefore(const Placed& x, const Placed& y) {
    if (x.clip.position != y.clip.position)
        return x.clip.position < y.clip.position;
    return x.clip.id.value < y.clip.id.value;
}

/// Sorted by track display order, then position, so layouts compare equal whatever order edits left them in.
Layout sorted(Layout layout, const std::vector<TrackId>& order) {
    std::unordered_map<TrackId, size_t> rank;
    for (size_t i = 0; i < order.size(); i++)
        rank[order[i]] = i;
    std::stable_sort(layout.begin(), layout.end(), [&](const Placed& x, const Placed& y) {
        size_t rx = rank.count(x.track) ? rank[x.track] : order.size();
        size_t ry = rank.count(y.track) ? rank[y.track] : order.size();
        return rx != ry ? rx < ry : sortsBefore(x, y);
    });
    return layout;
}

template <typename F>
Layout forEach(const Layout& layout, const IdSet& ids, F f) {
    Layout out = layout;
    for (Placed& p : out)
        if (ids.count(p.clip.id))
            f(p.clip);
    return out;
}

/// How far the selection may shift through `order` without any instance leaving it.
int clampTrackDelta(const Layout& layout, const IdSet& ids, const std::vector<TrackId>& order, int delta) {
    int lo = int(order.size()), hi = -1;
    for (const Placed& p : layout) {
        if (!ids.count(p.clip.id))
            continue;
        int i = int(std::find(order.begin(), order.end(), p.track) - order.begin());
        if (i >= int(order.size()))
            return 0;
        lo = std::min(lo, i);
        hi = std::max(hi, i);
    }
    if (hi < 0)
        return 0;
    return std::clamp(delta, -lo, int(order.size()) - 1 - hi);
}

TrackId shifted(TrackId track, const std::vector<TrackId>& order, int delta) {
    auto it = std::find(order.begin(), order.end(), track);
    if (it == order.end())
        return track;
    return order[size_t((it - order.begin()) + delta)];
}

Tick clampTimeDelta(const Layout& layout, const IdSet& ids, Tick dt) {
    Tick earliest = std::numeric_limits<Tick>::max();
    for (const Placed& p : layout)
        if (ids.count(p.clip.id))
            earliest = std::min(earliest, p.clip.position);
    return earliest == std::numeric_limits<Tick>::max() ? 0 : std::max(dt, -earliest);
}

std::vector<TrackId> trackOrder(const Song& song) {
    std::vector<TrackId> order;
    for (const Track& t : song.tracks)
        order.push_back(t.id);
    return order;
}

} // namespace

Stacking stackOf(const Layout& layout, TrackId track, bool flat) {
    std::vector<const ArrangerClip*> mine;
    for (const Placed& p : layout)
        if (p.track == track)
            mine.push_back(&p.clip);
    if (flat) {
        Stacking out;
        for (const ArrangerClip* a : mine)
            out.row[a->id] = 0;
        return out;
    }
    std::sort(mine.begin(), mine.end(), [](const ArrangerClip* a, const ArrangerClip* b) {
        if (a->layer != b->layer)
            return a->layer < b->layer;
        return a->position != b->position ? a->position < b->position : a->id.value < b->id.value;
    });
    Stacking out;
    std::vector<std::vector<std::pair<Tick, Tick>>> taken; // the spans each sub-row holds
    for (const ArrangerClip* a : mine) {
        Tick start = a->position, end = instanceEnd(*a);
        auto fits = [&](size_t r) {
            for (auto [s, e] : taken[r])
                if (s < end && start < e)
                    return false;
            return true;
        };
        size_t r = 0;
        while (r < taken.size() && !fits(r))
            r++;
        if (r >= taken.size())
            taken.resize(r + 1);
        taken[r].push_back({start, end});
        out.row[a->id] = int(r);
    }
    out.rows = std::max(1, int(taken.size()));
    return out;
}

namespace {

/// The clips of instance `id`'s track in the sub-row `row`, overlapping it.
std::vector<ArrangementId> overlapping(const Layout& layout, const Stacking& stack, const Placed& at, int row) {
    std::vector<ArrangementId> out;
    for (const Placed& p : layout)
        if (p.track == at.track && p.clip.id != at.clip.id && stack.row.at(p.clip.id) == row &&
            p.clip.position < instanceEnd(at.clip) && at.clip.position < instanceEnd(p.clip))
            out.push_back(p.clip.id);
    return out;
}

} // namespace

bool canMoveInStack(const Layout& layout, ArrangementId id, int direction) {
    const Placed* at = find(layout, id);
    if (!at)
        return false;
    Stacking stack = stackOf(layout, at->track);
    int row = stack.row.at(id) + direction;
    return row >= 0 && row <= kMaxStackLayer && !overlapping(layout, stack, *at, row).empty();
}

Layout moveInStack(const Layout& layout, ArrangementId id, int direction) {
    if (!canMoveInStack(layout, id, direction))
        return layout;
    const Placed* at = find(layout, id);
    Stacking stack = stackOf(layout, at->track);
    int row = stack.row.at(id);
    std::vector<ArrangementId> others = overlapping(layout, stack, *at, row + direction);
    Layout out = layout;
    for (Placed& p : out) {
        if (p.track != at->track)
            continue;
        int r = stack.row.at(p.clip.id);
        if (p.clip.id == id)
            r = row + direction;
        else if (std::find(others.begin(), others.end(), p.clip.id) != others.end())
            r = row;
        p.clip.layer = int8_t(std::clamp(r, 0, kMaxStackLayer));
    }
    return out;
}

Layout layoutOf(const Song& song) {
    Layout out;
    for (const Track& t : song.tracks)
        for (const ArrangerClip& a : t.arrangement)
            out.push_back({t.id, a});
    return out;
}

Layout pick(const Layout& layout, const IdSet& ids) {
    Layout out;
    for (const Placed& p : layout)
        if (ids.count(p.clip.id))
            out.push_back(p);
    return out;
}

IdSet idsOf(const Layout& layout) {
    IdSet ids;
    for (const Placed& p : layout)
        ids.insert(p.clip.id);
    return ids;
}

const Placed* find(const Layout& layout, ArrangementId id) {
    for (const Placed& p : layout)
        if (p.clip.id == id)
            return &p;
    return nullptr;
}

std::pair<Tick, Tick> span(const Layout& layout, const IdSet& ids) {
    Tick lo = std::numeric_limits<Tick>::max(), hi = std::numeric_limits<Tick>::min();
    for (const Placed& p : layout) {
        if (!ids.count(p.clip.id))
            continue;
        lo = std::min(lo, p.clip.position);
        hi = std::max(hi, instanceEnd(p.clip));
    }
    return lo > hi ? std::pair<Tick, Tick>{0, 0} : std::pair<Tick, Tick>{lo, hi};
}

Layout move(const Layout& layout, const IdSet& ids, Tick dt, const std::vector<TrackId>& order, int trackDelta) {
    dt = clampTimeDelta(layout, ids, dt);
    trackDelta = clampTrackDelta(layout, ids, order, trackDelta);
    Layout out = layout;
    for (Placed& p : out) {
        if (!ids.count(p.clip.id))
            continue;
        p.clip.position += dt;
        p.track = shifted(p.track, order, trackDelta);
    }
    return out;
}

Layout duplicate(const Layout& layout, const IdSet& ids, Tick dt, const std::vector<TrackId>& order, int trackDelta,
                 const IdSource& newId, IdSet* added) {
    Layout moved = move(pick(layout, ids), ids, dt, order, trackDelta);
    Layout out = layout;
    for (Placed& p : moved) {
        p.clip.id = newId();
        if (added)
            added->insert(p.clip.id);
        out.push_back(p);
    }
    return out;
}

Layout resizeEnd(const Layout& layout, const IdSet& ids, Tick delta, Tick minLength) {
    return forEach(layout, ids, [&](ArrangerClip& a) { a.length = std::max(minLength, a.length + delta); });
}

Layout trimStart(const Layout& layout, const IdSet& ids, Tick delta, Tick minLength, const ClipPool& clips) {
    return forEach(layout, ids, [&](ArrangerClip& a) {
        Tick start = std::clamp(a.position + delta, Tick(0), instanceEnd(a) - minLength);
        // A non-looping clip has nothing before its start to reveal.
        const Clip* clip = clipOf(clips, a.clip);
        if (clip && !clipSpan(*clip).looping)
            start = std::max(start, a.position - roundTick(double(a.offset) * double(a.stretch)));
        trimStartTo(a, start, clips);
    });
}

Layout stretchEnd(const Layout& layout, const IdSet& ids, Tick delta, Tick minLength) {
    return forEach(layout, ids, [&](ArrangerClip& a) {
        if (a.length <= 0)
            return;
        Tick length = std::max(minLength, a.length + delta);
        float stretch =
            std::clamp(float(double(a.stretch) * double(length) / double(a.length)), kMinStretch, kMaxStretch);
        a.length = std::max(minLength, roundTick(double(a.length) * double(stretch) / double(a.stretch)));
        a.stretch = stretch;
    });
}

Layout slip(const Layout& layout, const IdSet& ids, Tick delta, const ClipPool& clips) {
    return forEach(layout, ids, [&](ArrangerClip& a) {
        // Dragging right moves the content right, so the instance starts earlier in the clip.
        Tick offset = a.offset - roundTick(double(delta) / double(a.stretch));
        const Clip* clip = clipOf(clips, a.clip);
        a.offset = clip ? normalizeOffset(*clip, offset) : std::max<Tick>(0, offset);
    });
}

Layout split(const Layout& layout, const IdSet& ids, Tick at, const ClipPool& clips, const IdSource& newId,
             IdSet* rightParts) {
    Layout out;
    for (const Placed& p : layout) {
        const ArrangerClip& a = p.clip;
        if (!ids.count(a.id) || at <= a.position || at >= instanceEnd(a)) {
            out.push_back(p);
            continue;
        }
        Placed left = p, right = p;
        left.clip.length = at - a.position;
        left.clip.fadeOut = Fade{};
        right.clip.id = newId();
        right.clip.fadeIn = Fade{};
        trimStartTo(right.clip, at, clips);
        if (rightParts)
            rightParts->insert(right.clip.id);
        out.push_back(left);
        out.push_back(right);
    }
    return out;
}

Layout setMuted(const Layout& layout, const IdSet& ids, bool muted) {
    return forEach(layout, ids, [&](ArrangerClip& a) { a.muted = muted; });
}

Layout transpose(const Layout& layout, const IdSet& ids, int semitones) {
    return forEach(layout, ids,
                   [&](ArrangerClip& a) { a.transpose = int8_t(std::clamp(a.transpose + semitones, -48, 48)); });
}

Layout setVelocityScale(const Layout& layout, const IdSet& ids, float scale) {
    return forEach(layout, ids, [&](ArrangerClip& a) { a.velocityScale = std::clamp(scale, 0.f, 2.f); });
}

/// Whether instance `a` plays an audio clip.
static bool isAudioInstance(const ArrangerClip& a, const ClipPool& clips) {
    const Clip* c = clipOf(clips, a.clip);
    return c && c->audio;
}

Layout settleAudio(const Layout& layout, const IdSet& changed, const ClipPool& clips, const IdSource& newId) {
    std::vector<TrackId> tracks;
    for (const Placed& p : layout)
        if (changed.count(p.clip.id) && isAudioInstance(p.clip, clips) &&
            std::find(tracks.begin(), tracks.end(), p.track) == tracks.end())
            tracks.push_back(p.track);
    if (tracks.empty())
        return layout;
    Layout out;
    for (const Placed& p : layout)
        if (std::find(tracks.begin(), tracks.end(), p.track) == tracks.end())
            out.push_back(p);
    for (TrackId track : tracks) {
        std::vector<ArrangerClip> mine, moved;
        for (const Placed& p : layout)
            if (p.track == track)
                (changed.count(p.clip.id) ? moved : mine).push_back(p.clip);
        // A clip the changed ones cover goes; one a changed clip sits inside is split around it, its parts reaching
        // a little into the changed clip so each seam crossfades.
        for (const ArrangerClip& c : moved) {
            Tick cs = c.position, ce = instanceEnd(c);
            std::vector<ArrangerClip> next;
            for (ArrangerClip& p : mine) {
                Tick ps = p.position, pe = instanceEnd(p);
                if (ce <= ps || cs >= pe) {
                    next.push_back(p);
                    continue;
                }
                if (cs <= ps && pe <= ce)
                    continue;
                Tick seam = std::min<Tick>(kSeamCrossfade, (ce - cs) / 4);
                if (ps < cs && pe > ce) {
                    ArrangerClip left = p, right = p;
                    left.length = cs + seam - ps;
                    left.fadeOut = Fade{};
                    right.id = newId();
                    right.fadeIn = Fade{};
                    trimStartTo(right, ce - seam, clips);
                    next.push_back(left);
                    next.push_back(right);
                } else if (ps == cs) {
                    // Starting together the whole changed clip would be one long crossfade: the other starts later.
                    trimStartTo(p, ce - seam, clips);
                    p.fadeIn = Fade{};
                    next.push_back(p);
                } else if (pe == ce) {
                    p.length = cs + seam - ps;
                    p.fadeOut = Fade{};
                    next.push_back(p);
                } else {
                    next.push_back(p); // a plain overlap: the crossfade
                }
            }
            mine = std::move(next);
        }
        for (const ArrangerClip& c : moved)
            mine.push_back(c);
        std::sort(mine.begin(), mine.end(), [](const ArrangerClip& x, const ArrangerClip& y) {
            return x.position != y.position ? x.position < y.position : x.id.value < y.id.value;
        });
        // At most two clips sound at once: where a third begins, the earliest of the three stops. A changed clip
        // keeps its length; then the third starts where the earliest ends instead.
        for (int pass = 0; pass < 8; pass++) {
            bool fixed = true;
            for (size_t i = 0; i + 2 < mine.size(); i++) {
                ArrangerClip& first = mine[i];
                ArrangerClip& third = mine[i + 2];
                if (instanceEnd(first) <= third.position)
                    continue;
                fixed = false;
                if (!changed.count(first.id)) {
                    first.length = third.position - first.position;
                    first.fadeOut = Fade{};
                } else if (!changed.count(third.id)) {
                    trimStartTo(third, instanceEnd(first), clips);
                    third.fadeIn = Fade{};
                } else {
                    fixed = true; // several changed clips overlap each other: they are what the user placed
                }
            }
            mine.erase(std::remove_if(mine.begin(), mine.end(), [](const ArrangerClip& a) { return a.length <= 0; }),
                       mine.end());
            std::sort(mine.begin(), mine.end(), [](const ArrangerClip& x, const ArrangerClip& y) {
                return x.position != y.position ? x.position < y.position : x.id.value < y.id.value;
            });
            if (fixed)
                break;
        }
        for (const ArrangerClip& a : mine) {
            ArrangerClip q = a;
            q.layer = 0;
            out.push_back({track, q});
        }
    }
    return out;
}

Layout resolveOverlaps(const Layout& layout, const IdSet& changed, const ClipPool& clips, const IdSource& newId) {
    Layout out;
    for (const Placed& p : layout) {
        if (changed.count(p.clip.id)) {
            out.push_back(p);
            continue;
        }
        // Cut away every part of this instance that a changed instance on the same track covers. Audio clips follow
        // their own rule below: their overlaps are crossfades.
        std::vector<ArrangerClip> pieces{p.clip};
        for (const Placed& c : layout) {
            if (!changed.count(c.clip.id) || c.track != p.track || isAudioInstance(c.clip, clips))
                continue;
            Tick cs = c.clip.position, ce = instanceEnd(c.clip);
            std::vector<ArrangerClip> next;
            for (ArrangerClip piece : pieces) {
                Tick ps = piece.position, pe = instanceEnd(piece);
                if (ce <= ps || cs >= pe) {
                    next.push_back(piece);
                    continue;
                }
                if (ps < cs) {
                    ArrangerClip left = piece;
                    left.length = cs - ps;
                    next.push_back(left);
                }
                if (pe > ce) {
                    ArrangerClip right = piece;
                    trimStartTo(right, ce, clips);
                    next.push_back(right);
                }
            }
            pieces = std::move(next);
        }
        // The first surviving piece keeps the ID, so a trimmed instance stays the same instance.
        for (size_t i = 0; i < pieces.size(); i++) {
            Placed q{p.track, pieces[i]};
            if (i > 0)
                q.clip.id = newId();
            out.push_back(q);
        }
    }
    return settleAudio(out, changed, clips, newId);
}

// ---- Commands -----------------------------------------------------------------------------------------------

ChangeSet EditArrangement::changes() const {
    ChangeSet c;
    for (const Layout* l : {&removed, &added})
        for (const Placed& p : *l)
            if (std::find(c.tracks.begin(), c.tracks.end(), p.track) == c.tracks.end())
                c.tracks.push_back(p.track);
    return c;
}

void EditArrangement::swap(Song& song, const Layout& out, const Layout& in) {
    for (const Placed& p : out) {
        Track* t = song.findTrack(p.track);
        assert(t);
        if (!t)
            continue;
        auto& arr = t->arrangement;
        arr.erase(std::remove_if(arr.begin(), arr.end(), [&](const ArrangerClip& a) { return a.id == p.clip.id; }),
                  arr.end());
    }
    for (const Placed& p : in) {
        Track* t = song.findTrack(p.track);
        assert(t);
        if (!t)
            continue;
        auto& arr = t->arrangement;
        auto at = std::upper_bound(arr.begin(), arr.end(), p, [](const Placed& x, const ArrangerClip& a) {
            return sortsBefore(x, Placed{TrackId(), a});
        });
        arr.insert(at, p.clip);
    }
}

CommandPtr replaceArrangement(const Song& song, const std::string& name, const Layout& before, const Layout& after) {
    std::unordered_map<ArrangementId, const Placed*> was, now;
    for (const Placed& p : before)
        was[p.clip.id] = &p;
    for (const Placed& p : after)
        now[p.clip.id] = &p;
    Layout removed, added;
    std::map<ClipId, int> useDelta;
    for (const Placed& p : before) {
        auto it = now.find(p.clip.id);
        if (it == now.end() || !(*it->second == p)) {
            removed.push_back(p);
            useDelta[p.clip.clip]--;
        }
    }
    for (const Placed& p : after) {
        auto it = was.find(p.clip.id);
        if (it == was.end() || !(*it->second == p)) {
            added.push_back(p);
            useDelta[p.clip.clip]++;
        }
    }
    if (removed.empty() && added.empty())
        return nullptr;
    std::vector<TrackId> order = trackOrder(song);
    auto compound = std::make_unique<CompoundCommand>(name);
    compound->add(std::make_unique<EditArrangement>(name, sorted(removed, order), sorted(added, order)));
    for (const auto& [clip, delta] : useDelta)
        if (delta < 0 && song.clipUses(clip) + delta <= 0)
            if (const Clip* c = song.findClip(clip))
                compound->add(std::make_unique<RemoveClip>(*c));
    return compound;
}

CommandPtr placeClip(const Song& song, TrackId track, ClipId clip, Tick position, const IdSource& newId, bool stack) {
    const Clip* c = song.findClip(clip);
    if (!c || !song.findTrack(track))
        return nullptr;
    ClipSpan s = clipSpan(*c);
    // An empty clip without a loop has no length of its own yet: it goes in at its nominal length.
    Tick length = s.length() > 0 ? s.length() : std::max<Tick>(1, c->length);
    Placed p{track, ArrangerClip{}};
    p.clip.id = newId();
    p.clip.clip = clip;
    p.clip.position = std::max<Tick>(0, position);
    p.clip.length = length;
    p.clip.offset = s.start;
    Layout before = layoutOf(song);
    Layout after = before;
    after.push_back(p);
    after = stack ? settleAudio(after, {p.clip.id}, song.clips, newId)
                  : resolveOverlaps(after, {p.clip.id}, song.clips, newId);
    return replaceArrangement(song, "Place clip", before, after);
}

CommandPtr drawClip(const Song& song, TrackId track, Tick position, Tick length, ClipId clip, ArrangementId instance,
                    const IdSource& newId, bool stack) {
    const Track* t = song.findTrack(track);
    if (!t || length < 1 || !clip.valid())
        return nullptr;
    Clip c;
    c.id = clip;
    c.name = newClipName(song);
    c.color = t->color;
    c.length = length;
    c.loopEnd = length;
    return placeNewClip(song, track, position, std::move(c), instance, newId, stack, "Draw clip");
}

CommandPtr placeNewClip(const Song& song, TrackId track, Tick position, Clip clip, ArrangementId instance,
                        const IdSource& newId, bool stack, const std::string& name) {
    if (!song.findTrack(track) || !clip.id.valid())
        return nullptr;
    Placed p{track, ArrangerClip{}};
    p.clip.id = instance;
    p.clip.clip = clip.id;
    p.clip.position = std::max<Tick>(0, position);
    p.clip.length = std::max<Tick>(1, clip.playEnd() - clip.playStart());
    Layout before = layoutOf(song);
    Layout after = before;
    after.push_back(p);
    if (clip.audio) {
        // The new clip isn't in the pool yet, and the audio rule asks the pool what kind it is.
        ClipPool pool;
        for (const Placed& q : after)
            if (q.track == track)
                if (const Clip* c = song.findClip(q.clip.clip))
                    pool.emplace(c->id, *c);
        pool[clip.id] = clip;
        after = settleAudio(after, {instance}, pool, newId);
    } else if (!stack) {
        after = resolveOverlaps(after, {instance}, song.clips, newId);
    }
    auto compound = std::make_unique<CompoundCommand>(name);
    compound->add(std::make_unique<AddClip>(std::move(clip)));
    compound->add(replaceArrangement(song, name, before, after));
    return compound;
}

CommandPtr glue(const Song& song, const IdSet& ids, const NextId& nextId) {
    Layout before = layoutOf(song);
    Layout chosen = pick(before, ids);
    if (chosen.empty())
        return nullptr;
    TrackId track = chosen.front().track;
    for (const Placed& p : chosen)
        if (p.track != track)
            return nullptr;
    auto [start, end] = span(before, ids);
    const Clip* first = song.findClip(chosen.front().clip.clip);
    if (!first || end <= start)
        return nullptr;
    if (first->audio) {
        // Audio glues without rendering only where the parts are pieces of one clip, end to end, each playing on
        // from where the last stopped (a split undone by hand). Other audio waits for rendering (Phase 3).
        std::sort(chosen.begin(), chosen.end(),
                  [](const Placed& a, const Placed& b) { return a.clip.position < b.clip.position; });
        for (size_t i = 0; i < chosen.size(); i++) {
            const ArrangerClip& a = chosen[i].clip;
            if (a.clip != first->id || a.muted)
                return nullptr;
            if (i > 0) {
                const ArrangerClip& prev = chosen[i - 1].clip;
                if (a.position != instanceEnd(prev) || a.offset != prev.offset + prev.length)
                    return nullptr;
            }
        }
        if (chosen.size() < 2)
            return nullptr;
        Placed whole = chosen.front();
        whole.clip.length = end - start;
        whole.clip.fadeOut = chosen.back().clip.fadeOut;
        Layout after;
        for (const Placed& p : before)
            if (!ids.count(p.clip.id))
                after.push_back(p);
        after.push_back(whole);
        return replaceArrangement(song, "Glue " + countClips(chosen.size()), before, after);
    }

    Clip glued;
    glued.id = ClipId(nextId());
    glued.name = first->name;
    glued.color = first->color;
    glued.length = end - start;
    glued.loopStart = 0;
    glued.loopEnd = glued.length;
    glued.loopMode = LoopMode::Loop;
    std::vector<TimedNote> notes;
    for (const Placed& p : chosen)
        if (!p.clip.muted)
            if (const Clip* c = song.findClip(p.clip.clip))
                renderInstance(*c, p.clip, notes);
    for (const TimedNote& tn : notes) {
        Note n = *tn.source;
        n.id = NoteId(nextId());
        n.start = tn.start - start;
        n.length = tn.end - tn.start;
        n.pitch = tn.pitch;
        n.velocity = tn.velocity;
        glued.notes.push_back(std::move(n));
    }
    glued.sortNotes();

    Placed instance{track, ArrangerClip{}};
    instance.clip.id = ArrangementId(nextId());
    instance.clip.clip = glued.id;
    instance.clip.position = start;
    instance.clip.length = glued.length;
    Layout after;
    for (const Placed& p : before)
        if (!ids.count(p.clip.id))
            after.push_back(p);
    after.push_back(instance);

    auto compound = std::make_unique<CompoundCommand>("Glue " + countClips(chosen.size()));
    compound->add(std::make_unique<AddClip>(std::move(glued)));
    compound->add(replaceArrangement(song, "Glue clips", before, after));
    return compound;
}

CommandPtr makeUnique(const Song& song, const IdSet& ids, const NextId& nextId) {
    Layout before = layoutOf(song);
    Layout after = before;
    auto compound = std::make_unique<CompoundCommand>("Make unique");
    for (Placed& p : after) {
        if (!ids.count(p.clip.id) || song.clipUses(p.clip.clip) <= 1)
            continue;
        const Clip* c = song.findClip(p.clip.clip);
        if (!c)
            continue;
        Clip copy = copyClip(*c, nextId);
        p.clip.clip = copy.id;
        compound->add(std::make_unique<AddClip>(std::move(copy)));
    }
    CommandPtr edit = replaceArrangement(song, "Make unique", before, after);
    if (!edit)
        return nullptr;
    compound->add(std::move(edit));
    return compound;
}

std::string countClips(size_t n) { return n == 1 ? "1 clip" : std::to_string(n) + " clips"; }

CommandPtr scaleClip(const Song& song, ClipId id, double factor) {
    const Clip* c = song.findClip(id);
    if (!c || !(factor > 0) || factor == 1)
        return nullptr;
    auto scaled = [factor](Tick t) { return Tick(std::llround(double(t) * factor)); };
    std::vector<Note> notes = c->notes;
    for (Note& n : notes) {
        Tick end = scaled(n.end());
        n.start = scaled(n.start);
        n.length = std::max<Tick>(1, end - n.start);
    }
    std::vector<AutomationEnvelope> lanes = c->automation;
    for (AutomationEnvelope& lane : lanes)
        for (AutoPoint& p : lane.points)
            p.t = scaled(p.t);

    std::string name = factor == 2     ? "Clip length \u00d72"
                       : factor == 0.5 ? "Clip length \u00d7\u00bd"
                                       : "Clip length " + std::to_string(int(std::lround(factor * 100))) + "%";
    auto out = std::make_unique<CompoundCommand>(name);
    out->add(replaceNotes(name, id, c->notes, notes));
    out->add(setClipField(song, id, &Clip::length, std::max<Tick>(1, scaled(c->length)), name));
    out->add(setClipField(song, id, &Clip::loopStart, scaled(c->loopStart), name));
    out->add(setClipField(song, id, &Clip::loopEnd, scaled(c->loopEnd), name));
    out->add(setClipField(song, id, &Clip::automation, lanes, name));

    Layout before = layoutOf(song);
    Layout after = before;
    for (Placed& p : after)
        if (p.clip.clip == id) {
            p.clip.length = std::max<Tick>(1, scaled(p.clip.length));
            p.clip.offset = scaled(p.clip.offset);
        }
    out->add(replaceArrangement(song, name, before, after));
    return out->empty() ? nullptr : std::move(out);
}

} // namespace tessera::arrange
