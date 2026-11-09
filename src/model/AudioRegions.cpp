#include "AudioRegions.hpp"

#include "ArrangementEdits.hpp"
#include "AudioEdits.hpp"

#include <algorithm>
#include <cmath>

namespace tessera::regions {

namespace {

bool startsBefore(const AudioRegion& x, const AudioRegion& y) { return x.start < y.start; }

/// `r` starting at `to`, its audio staying where it is in the clip. No limits: the caller has checked them.
AudioRegion withStart(AudioRegion r, Tick to, const AudioContent& a, double rate) {
    if (!r.reverse)
        r.sourceFrame = std::max<int64_t>(0, std::llround(frameAt(a, r, double(to), rate)));
    r.length = r.end() - to;
    r.start = to;
    return r;
}

/// `r` ending at `to`, its audio staying where it is in the clip. No limits: the caller has checked them.
AudioRegion withEnd(AudioRegion r, Tick to, const AudioContent& a, double rate) {
    if (r.reverse)
        r.sourceFrame = std::max<int64_t>(0, std::llround(frameAt(a, r, double(to), rate)));
    r.length = to - r.start;
    return r;
}

/// Applies `change` to the part with `id`.
template <typename F>
List changeOne(const List& list, AudioRegionId id, F change) {
    List out = list;
    for (AudioRegion& r : out)
        if (r.id == id)
            r = change(r);
    return out;
}

} // namespace

List of(const Clip& clip, double rate) {
    if (!clip.audio)
        return {};
    const AudioContent& a = *clip.audio;
    if (!a.regions.empty())
        return a.regions;
    AudioRegion r;
    r.length = clip.length;
    r.reverse = a.reverse;
    r.sourceFrame = a.reverse || a.warps.empty() ? a.sourceStart : std::llround(warpedFrame(a, 0, rate));
    return {r};
}

double frameAt(const AudioContent& a, const AudioRegion& r, double tick, double rate) {
    if (r.reverse)
        return double(r.sourceFrame) + (double(r.end()) - tick) * framesPerTick(a, rate);
    return warpedFrame(a, warpedTick(a, double(r.sourceFrame), rate) + tick - double(r.start), rate);
}

double shift(const AudioContent& a, const AudioRegion& r, double rate) {
    return warpedTick(a, double(r.sourceFrame), rate) - double(r.start);
}

const AudioRegion* at(const List& list, double tick, bool includeMuted) {
    auto after = std::upper_bound(list.begin(), list.end(), tick,
                                  [](double t, const AudioRegion& r) { return t < double(r.start); });
    // At most two parts overlap, and a muted part can sit between them, so three steps back is enough.
    for (int steps = 0; after != list.begin() && steps < 3; steps++) {
        --after;
        if (tick < double(after->end()) && (includeMuted || !after->muted))
            return &*after;
    }
    return nullptr;
}

List cut(const List& list, const AudioContent& a, Tick tick, double rate, const NewId& newId) {
    List out;
    for (const AudioRegion& r : list) {
        if (tick < r.start + kMinLength || tick > r.end() - kMinLength) {
            out.push_back(r);
            continue;
        }
        AudioRegion left = withEnd(r, tick, a, rate), right = withStart(r, tick, a, rate);
        left.fadeOut = Fade{};
        right.fadeIn = Fade{};
        right.id = newId();
        out.push_back(left);
        out.push_back(right);
    }
    return out;
}

List trimStart(const List& list, AudioRegionId id, Tick to, const AudioContent& a, double rate, int64_t fileFrames) {
    return changeOne(list, id, [&](const AudioRegion& r) {
        Tick end = r.end();
        double earliest;
        if (r.reverse) {
            double perTick = framesPerTick(a, rate);
            earliest = double(end) - (double(fileFrames) - double(r.sourceFrame)) / std::max(perTick, 1e-9);
        } else {
            earliest = double(r.start) + warpedTick(a, 0, rate) - warpedTick(a, double(r.sourceFrame), rate);
        }
        Tick limit = Tick(std::ceil(earliest));
        Tick target = std::clamp<Tick>(to, std::max<Tick>(0, limit), std::max<Tick>(r.start, end - kMinLength));
        return withStart(r, target, a, rate);
    });
}

List trimEnd(const List& list, AudioRegionId id, Tick to, const AudioContent& a, double rate, int64_t fileFrames) {
    return changeOne(list, id, [&](const AudioRegion& r) {
        double latest;
        if (r.reverse) {
            double perTick = framesPerTick(a, rate);
            latest = double(r.end()) + double(r.sourceFrame) / std::max(perTick, 1e-9);
        } else {
            latest = double(r.start) + warpedTick(a, double(fileFrames), rate) - warpedTick(a, double(r.sourceFrame), rate);
        }
        Tick limit = Tick(std::floor(latest));
        Tick target = std::clamp<Tick>(to, std::min<Tick>(r.end(), r.start + kMinLength), std::max(limit, r.start + kMinLength));
        return withEnd(r, target, a, rate);
    });
}

List slip(const List& list, AudioRegionId id, int64_t frames, const AudioContent& a, double rate, int64_t fileFrames) {
    return changeOne(list, id, [&](AudioRegion r) {
        double low = std::min(frameAt(a, r, double(r.start), rate), frameAt(a, r, double(r.end()), rate));
        double high = std::max(frameAt(a, r, double(r.start), rate), frameAt(a, r, double(r.end()), rate));
        double most = std::max(0.0, double(fileFrames) - high);
        r.sourceFrame += std::llround(std::clamp(double(frames), -low, most));
        return r;
    });
}

List move(const List& list, const Ids& ids, Tick delta, Tick clipLength) {
    Tick first = clipLength, last = 0;
    for (const AudioRegion& r : list)
        if (ids.count(r.id)) {
            first = std::min(first, r.start);
            last = std::max(last, r.end());
        }
    delta = std::max(delta, -first);
    delta = std::min(delta, std::max<Tick>(0, clipLength - last));
    List out = list;
    for (AudioRegion& r : out)
        if (ids.count(r.id))
            r.start += delta;
    std::stable_sort(out.begin(), out.end(), startsBefore);
    return out;
}

List settle(const List& list, const Ids& changed, const AudioContent& a, double rate, const NewId& newId) {
    List mine, moved;
    for (const AudioRegion& r : list)
        (changed.count(r.id) ? moved : mine).push_back(r);
    for (const AudioRegion& m : moved) {
        List next;
        for (const AudioRegion& p : mine) {
            if (m.end() <= p.start || m.start >= p.end()) {
                next.push_back(p);
                continue;
            }
            if (m.start <= p.start && p.end() <= m.end())
                continue;
            Tick seam = std::min<Tick>(arrange::kSeamCrossfade, m.length / 4);
            if (p.start < m.start && p.end() > m.end()) {
                AudioRegion left = withEnd(p, m.start + seam, a, rate), right = withStart(p, m.end() - seam, a, rate);
                left.fadeOut = Fade{};
                right.fadeIn = Fade{};
                right.id = newId();
                next.push_back(left);
                next.push_back(right);
            } else if (p.start == m.start) {
                AudioRegion later = withStart(p, m.end() - seam, a, rate);
                later.fadeIn = Fade{};
                next.push_back(later);
            } else if (p.end() == m.end()) {
                AudioRegion earlier = withEnd(p, m.start + seam, a, rate);
                earlier.fadeOut = Fade{};
                next.push_back(earlier);
            } else {
                next.push_back(p); // a plain overlap: the crossfade
            }
        }
        mine = std::move(next);
    }
    mine.insert(mine.end(), moved.begin(), moved.end());
    std::stable_sort(mine.begin(), mine.end(), startsBefore);
    for (size_t i = 0; i + 2 < mine.size(); i++)
        if (mine[i].end() > mine[i + 2].start && !changed.count(mine[i].id)) {
            Tick stop = std::max(mine[i].start + 1, mine[i + 2].start);
            mine[i] = withEnd(mine[i], stop, a, rate);
            mine[i].fadeOut = Fade{};
        }
    return mine;
}

CommandPtr setRegions(const Song& song, ClipId clip, List list, const std::string& name, const NextId& nextId) {
    const Clip* c = song.findClip(clip);
    if (!c || !c->audio || list.empty())
        return nullptr;
    Tick end = 0;
    for (AudioRegion& r : list) {
        if (!r.id.valid())
            r.id = AudioRegionId(nextId());
        end = std::max(end, r.end());
    }
    std::stable_sort(list.begin(), list.end(), startsBefore);
    Tick length = std::max(c->length, end);
    auto out = std::make_unique<CompoundCommand>(name);
    out->add(editAudio(song, clip, [&](AudioContent& a) {
        // The parts carry the direction now. A reversed clip never used its warp markers, so none must wake up.
        if (a.reverse)
            a.warps.clear();
        a.reverse = false;
        a.regions = list;
    }, name));
    if (length != c->length) {
        if (c->loopStart == 0 && c->loopEnd >= c->length)
            out->add(setClipField(song, clip, &Clip::loopEnd, length, name));
        out->add(setClipField(song, clip, &Clip::length, length, name));
    }
    return out->empty() ? nullptr : std::move(out);
}

CommandPtr removeClosingGaps(const Song& song, ClipId clip, const Ids& ids, const NextId& nextId) {
    const Clip* c = song.findClip(clip);
    const AudioSource* src = c ? song.sourceOf(*c) : nullptr;
    if (!c || !c->audio || !src)
        return nullptr;
    List before = of(*c, src->sampleRate), kept;
    std::vector<std::pair<Tick, Tick>> holes;
    for (const AudioRegion& r : before) {
        if (ids.count(r.id))
            holes.push_back({r.start, r.end()});
        else
            kept.push_back(r);
    }
    if (kept.empty() || holes.empty())
        return nullptr;
    // Holes may overlap each other; what closes is the time they cover together.
    std::vector<std::pair<Tick, Tick>> merged;
    for (const auto& h : holes)
        if (!merged.empty() && h.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, h.second);
        else
            merged.push_back(h);
    auto closedBefore = [&](Tick t) {
        Tick total = 0;
        for (const auto& [from, to] : merged)
            total += std::max<Tick>(0, std::min(to, t) - from);
        return total;
    };
    for (AudioRegion& r : kept)
        r.start -= closedBefore(r.start);
    Tick length = std::max<Tick>(1, c->length - closedBefore(c->length));

    std::string name = "Delete and close the gap";
    auto out = std::make_unique<CompoundCommand>(name);
    out->add(editAudio(song, clip, [&](AudioContent& a) {
        a.reverse = false;
        for (AudioRegion& r : kept)
            if (!r.id.valid())
                r.id = AudioRegionId(nextId());
        a.regions = kept;
    }, name));
    if (c->loopStart == 0 && c->loopEnd >= c->length) {
        out->add(setClipField(song, clip, &Clip::loopEnd, length, name));
    } else {
        Tick loopEnd = std::clamp<Tick>(c->loopEnd - closedBefore(c->loopEnd), 1, length);
        out->add(setClipField(song, clip, &Clip::loopStart,
                              std::clamp<Tick>(c->loopStart - closedBefore(c->loopStart), 0, loopEnd - 1), name));
        out->add(setClipField(song, clip, &Clip::loopEnd, loopEnd, name));
    }
    out->add(setClipField(song, clip, &Clip::length, length, name));
    arrange::Layout layout = arrange::layoutOf(song), changed = layout;
    for (arrange::Placed& p : changed)
        if (p.clip.clip == clip && p.clip.offset == 0 && p.clip.length == c->length)
            p.clip.length = length;
    out->add(arrange::replaceArrangement(song, name, layout, changed));
    return out;
}

} // namespace tessera::regions
