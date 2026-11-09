#include "AudioRegions.hpp"

#include "ArrangementEdits.hpp"
#include "AudioEdits.hpp"

#include <algorithm>
#include <cmath>

namespace tessera::regions {

namespace {

bool startsBefore(const AudioRegion& x, const AudioRegion& y) { return x.start < y.start; }

/// `r` starting at `to`, its audio staying where it is in the clip. No limits: the caller has checked them.
AudioRegion withStart(AudioRegion r, Tick to, const AudioContent& a, const Files& files) {
    if (!r.reverse)
        r.sourceFrame = std::max<int64_t>(0, std::llround(frameAt(a, r, double(to), files)));
    r.length = r.end() - to;
    r.start = to;
    return r;
}

/// `r` ending at `to`, its audio staying where it is in the clip. No limits: the caller has checked them.
AudioRegion withEnd(AudioRegion r, Tick to, const AudioContent& a, const Files& files) {
    if (r.reverse)
        r.sourceFrame = std::max<int64_t>(0, std::llround(frameAt(a, r, double(to), files)));
    r.length = to - r.start;
    return r;
}

/// How many ticks before part `r`'s start it would have to begin to play frame `frame` first. Negative means after.
double shiftToFrame(const AudioContent& a, const AudioRegion& r, double frame, const Files& files) {
    return takeTick(a, r.take, double(r.sourceFrame), files) - takeTick(a, r.take, frame, files);
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

/// Where take `take` starts in its file when it has no markers.
int64_t startOf(const AudioContent& a, int take) {
    return take >= 0 && size_t(take) < a.takes.size() ? a.takes[size_t(take)].sourceStart : a.sourceStart;
}

} // namespace

Files filesOf(const Song& song, const AudioContent& a) {
    Files files;
    if (const AudioSource* s = song.findSource(a.source)) {
        files.rate = s->sampleRate;
        files.frames = s->frames;
    }
    for (const AudioTake& t : a.takes) {
        const AudioSource* s = song.findSource(t.source);
        files.takes.push_back({s ? s->sampleRate : files.rate, s ? s->frames : 0});
    }
    return files;
}

const std::vector<WarpMarker>& warpsOf(const AudioContent& a, int take) {
    return take >= 0 && size_t(take) < a.takes.size() ? a.takes[size_t(take)].warps : a.warps;
}

List of(const Clip& clip, const Files& files) {
    if (!clip.audio)
        return {};
    const AudioContent& a = *clip.audio;
    if (!a.regions.empty())
        return a.regions;
    AudioRegion r;
    r.length = clip.length;
    r.reverse = a.reverse;
    r.sourceFrame = a.reverse || a.warps.empty() ? a.sourceStart : std::llround(takeFrame(a, -1, 0, files));
    return {r};
}

double takeFrame(const AudioContent& a, int take, double tick, const Files& files) {
    return warpedFrame(warpsOf(a, take), startOf(a, take), framesPerTick(a, files.rateOf(take)), tick);
}

double takeTick(const AudioContent& a, int take, double frame, const Files& files) {
    return warpedTick(warpsOf(a, take), startOf(a, take), framesPerTick(a, files.rateOf(take)), frame);
}

double frameAt(const AudioContent& a, const AudioRegion& r, double tick, const Files& files) {
    if (r.reverse)
        return double(r.sourceFrame) + (double(r.end()) - tick) * framesPerTick(a, files.rateOf(r.take));
    return takeFrame(a, r.take, takeTick(a, r.take, double(r.sourceFrame), files) + tick - double(r.start), files);
}

double shift(const AudioContent& a, const AudioRegion& r, const Files& files) {
    return takeTick(a, r.take, double(r.sourceFrame), files) - double(r.start);
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

List cut(const List& list, const AudioContent& a, Tick tick, const Files& files, const NewId& newId) {
    List out;
    for (const AudioRegion& r : list) {
        if (tick < r.start + kMinLength || tick > r.end() - kMinLength) {
            out.push_back(r);
            continue;
        }
        AudioRegion left = withEnd(r, tick, a, files), right = withStart(r, tick, a, files);
        left.fadeOut = Fade{};
        right.fadeIn = Fade{};
        right.id = newId();
        out.push_back(left);
        out.push_back(right);
    }
    return out;
}

List trimStart(const List& list, AudioRegionId id, Tick to, const AudioContent& a, const Files& files) {
    return changeOne(list, id, [&](const AudioRegion& r) {
        Tick end = r.end();
        double earliest;
        if (r.reverse) {
            double perTick = framesPerTick(a, files.rateOf(r.take));
            earliest = double(end) - (double(files.framesOf(r.take)) - double(r.sourceFrame)) / std::max(perTick, 1e-9);
        } else {
            earliest = double(r.start) - shiftToFrame(a, r, 0, files);
        }
        Tick limit = Tick(std::ceil(earliest));
        Tick target = std::clamp<Tick>(to, std::max<Tick>(0, limit), std::max<Tick>(r.start, end - kMinLength));
        return withStart(r, target, a, files);
    });
}

List trimEnd(const List& list, AudioRegionId id, Tick to, const AudioContent& a, const Files& files) {
    return changeOne(list, id, [&](const AudioRegion& r) {
        double latest;
        if (r.reverse) {
            double perTick = framesPerTick(a, files.rateOf(r.take));
            latest = double(r.end()) + double(r.sourceFrame) / std::max(perTick, 1e-9);
        } else {
            latest = double(r.start) - shiftToFrame(a, r, double(files.framesOf(r.take)), files);
        }
        Tick limit = Tick(std::floor(latest));
        Tick target =
            std::clamp<Tick>(to, std::min<Tick>(r.end(), r.start + kMinLength), std::max(limit, r.start + kMinLength));
        return withEnd(r, target, a, files);
    });
}

List slip(const List& list, AudioRegionId id, int64_t frames, const AudioContent& a, const Files& files) {
    return changeOne(list, id, [&](AudioRegion r) {
        double first = frameAt(a, r, double(r.start), files), last = frameAt(a, r, double(r.end()), files);
        double low = std::min(first, last), high = std::max(first, last);
        double most = std::max(0.0, double(files.framesOf(r.take)) - high);
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

List settle(const List& list, const Ids& changed, const AudioContent& a, const Files& files, const NewId& newId) {
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
                AudioRegion left = withEnd(p, m.start + seam, a, files), right = withStart(p, m.end() - seam, a, files);
                left.fadeOut = Fade{};
                right.fadeIn = Fade{};
                right.id = newId();
                next.push_back(left);
                next.push_back(right);
            } else if (p.start == m.start) {
                AudioRegion later = withStart(p, m.end() - seam, a, files);
                later.fadeIn = Fade{};
                next.push_back(later);
            } else if (p.end() == m.end()) {
                AudioRegion earlier = withEnd(p, m.start + seam, a, files);
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
            mine[i] = withEnd(mine[i], stop, a, files);
            mine[i].fadeOut = Fade{};
        }
    return mine;
}

std::pair<Tick, Tick> takeSpan(const Clip& clip, int take, const Files& files) {
    if (!clip.audio)
        return {0, 0};
    double from = takeTick(*clip.audio, take, 0, files);
    double to = takeTick(*clip.audio, take, double(files.framesOf(take)), files);
    Tick first = std::max<Tick>(0, Tick(std::ceil(from)));
    Tick last = std::min<Tick>(clip.length, Tick(std::floor(to)));
    return last > first ? std::make_pair(first, last) : std::make_pair(Tick(0), Tick(0));
}

List comp(const List& list, const AudioContent& a, int take, Tick from, Tick to, const Files& files, const NewId& newId) {
    if (to <= from)
        return list;
    AudioRegion added;
    added.id = newId();
    added.start = from;
    added.length = to - from;
    added.take = take;
    added.sourceFrame = std::max<int64_t>(0, std::llround(takeFrame(a, take, double(from), files)));
    List out;
    for (const AudioRegion& r : list) {
        if (r.end() <= from || r.start >= to) {
            out.push_back(r);
            continue;
        }
        if (r.start < from) {
            AudioRegion left = withEnd(r, from, a, files);
            left.fadeOut = Fade{};
            out.push_back(left);
        }
        if (r.end() > to) {
            AudioRegion right = withStart(r, to, a, files);
            right.fadeIn = Fade{};
            if (r.start < from)
                right.id = newId();
            out.push_back(right);
        }
    }
    out.push_back(added);
    std::stable_sort(out.begin(), out.end(), startsBefore);
    // A neighbour already playing on in that take is the same stretch of audio: one part, not two.
    auto joins = [&](const AudioRegion& x, const AudioRegion& y) {
        return x.end() == y.start && x.take == y.take && x.gainDb == y.gainDb && x.muted == y.muted && !x.reverse &&
               !y.reverse && x.fadeOut.length == 0 && y.fadeIn.length == 0 &&
               std::abs(frameAt(a, x, double(y.start), files) - double(y.sourceFrame)) < 2;
    };
    for (size_t i = 0; i + 1 < out.size();) {
        if (joins(out[i], out[i + 1]) && (out[i].id == added.id || out[i + 1].id == added.id)) {
            AudioRegionId keep = out[i].id == added.id ? out[i + 1].id : out[i].id;
            out[i].length += out[i + 1].length;
            out[i].fadeOut = out[i + 1].fadeOut;
            out.erase(out.begin() + std::ptrdiff_t(i) + 1);
            out[i].id = keep;
            added.id = keep;
            continue;
        }
        i++;
    }
    return out;
}

List retake(const List& list, const Ids& ids, int take, const AudioContent& a, const Files& files) {
    List out = list;
    for (AudioRegion& r : out)
        if (ids.count(r.id)) {
            r.take = take;
            r.sourceFrame = std::max<int64_t>(0, std::llround(takeFrame(a, take, double(r.start), files)));
        }
    return out;
}

CommandPtr useTake(const Song& song, ClipId clip, int take, const NextId& nextId) {
    const Clip* c = song.findClip(clip);
    if (!c || !c->audio)
        return nullptr;
    Files files = filesOf(song, *c->audio);
    std::string name = "Play " + takeName(*c->audio, take);
    return editAudio(song, clip, [&](AudioContent& a) {
        if (take < 0 && !a.reverse) {
            a.regions.clear();
            return;
        }
        AudioRegion r;
        r.id = AudioRegionId(nextId());
        r.length = c->length;
        r.take = take;
        r.reverse = a.reverse;
        r.sourceFrame = std::max<int64_t>(0, std::llround(takeFrame(a, take, 0, files)));
        // A whole-clip part already playing that take stays as it is, with its level and fades.
        if (a.regions.size() == 1 && a.regions[0].take == take && a.regions[0].start == 0 &&
            a.regions[0].length == c->length)
            return;
        a.reverse = false;
        a.regions = {r};
    }, name);
}

CommandPtr addTake(const Song& song, ClipId clip, AudioTake take, const NextId& nextId) {
    const Clip* c = song.findClip(clip);
    if (!c || !c->audio)
        return nullptr;
    return editAudio(song, clip, [&](AudioContent& a) {
        a.takes.push_back(take);
        int index = int(a.takes.size()) - 1;
        if (a.regions.empty()) {
            AudioRegion r;
            r.id = AudioRegionId(nextId());
            r.length = c->length;
            r.take = index;
            r.reverse = a.reverse;
            r.sourceFrame = take.sourceStart;
            a.reverse = false;
            a.regions = {r};
        } else if (a.regions.size() == 1 && a.regions[0].start == 0 && a.regions[0].length >= c->length) {
            a.regions[0].take = index;
            a.regions[0].sourceFrame = take.sourceStart;
        }
    }, "Record take " + std::to_string(c->audio->takes.size() + 2));
}

CommandPtr removeTake(const Song& song, ClipId clip, int take) {
    const Clip* c = song.findClip(clip);
    if (!c || !c->audio || take < 0 || size_t(take) >= c->audio->takes.size())
        return nullptr;
    Files files = filesOf(song, *c->audio);
    return editAudio(song, clip, [&](AudioContent& a) {
        for (AudioRegion& r : a.regions) {
            if (r.take == take) {
                r.take = -1;
                r.sourceFrame = std::max<int64_t>(0, std::llround(takeFrame(a, -1, double(r.start), files)));
            } else if (r.take > take) {
                r.take--;
            }
        }
        a.takes.erase(a.takes.begin() + take);
    }, "Delete take");
}

CommandPtr renameTake(const Song& song, ClipId clip, int take, const std::string& name) {
    const Clip* c = song.findClip(clip);
    if (!c || !c->audio || take < 0 || size_t(take) >= c->audio->takes.size())
        return nullptr;
    return editAudio(song, clip, [&](AudioContent& a) { a.takes[size_t(take)].name = name; }, "Rename take");
}

std::string takeName(const AudioContent& a, int take) {
    if (take >= 0 && size_t(take) < a.takes.size() && !a.takes[size_t(take)].name.empty())
        return a.takes[size_t(take)].name;
    return "Take " + std::to_string(take + 2);
}

std::string playingText(const Clip& clip) {
    if (!clip.audio || clip.audio->takes.empty())
        return "";
    const List& parts = clip.audio->regions;
    int take = parts.empty() ? -1 : parts.front().take;
    for (const AudioRegion& r : parts)
        if (r.take != take)
            return "comp";
    return "take " + std::to_string(take + 2);
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
    List before = of(*c, filesOf(song, *c->audio)), kept;
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
