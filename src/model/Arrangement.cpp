#include "Arrangement.hpp"

#include "DrumKit.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace tessera {

namespace {

Tick roundTick(double t) { return Tick(std::llround(t)); }

} // namespace

double stretchOf(const ArrangerClip& a) {
    return std::clamp(double(a.stretch), double(kMinStretch), double(kMaxStretch));
}

ClipSpan clipSpan(const Clip& clip, const std::vector<DrumLane>* lanes) {
    if (clip.loopMode == LoopMode::Loop && clip.loopEnd > clip.loopStart) {
        Tick loop = clip.loopEnd - clip.loopStart;
        Tick cycle = lanes ? laneCycle(clip, *lanes) : loop;
        return {true, clip.loopStart, clip.loopStart + cycle, loop};
    }
    Tick end = clip.contentEnd();
    return {false, 0, end, end};
}

double ClipSpan::patternTime(double u) const {
    if (!cycles() || pattern <= 0)
        return u;
    double r = std::fmod(u - double(start), double(pattern));
    return double(start) + (r < 0 ? r + double(pattern) : r);
}

Tick normalizeOffset(const Clip& clip, Tick offset, const std::vector<DrumLane>* lanes) {
    ClipSpan span = clipSpan(clip, lanes);
    if (!span.looping)
        return std::clamp<Tick>(offset, 0, span.end);
    if (offset >= 0 && offset < span.end)
        return offset;
    Tick L = span.length();
    Tick r = (offset - span.start) % L;
    return span.start + (r < 0 ? r + L : r);
}

namespace {

TimedNote timed(const ArrangerClip& a, const Note& n, double on, double off, int transpose) {
    TimedNote tn;
    tn.start = a.position + roundTick(on);
    tn.end = std::max(tn.start + 1, a.position + roundTick(off));
    tn.pitch = int8_t(std::clamp(int(n.pitch) + int(a.transpose) + transpose, 0, 127));
    tn.velocity = std::clamp(n.velocity * a.velocityScale, 0.f, 1.f);
    tn.releaseVelocity = n.releaseVelocity;
    tn.source = &n;
    return tn;
}

} // namespace

void renderInstance(const Clip& clip, const ArrangerClip& a, std::vector<TimedNote>& out, int transpose,
                    float gateScale, const std::vector<DrumLane>* lanes) {
    ClipSpan span = clipSpan(clip, lanes);
    if (a.length <= 0 || span.length() <= 0)
        return;
    const double stretch = stretchOf(a);
    const double total = double(a.length);
    const size_t first = out.size();

    // Drum lanes with their own loop play apart from the clip loop.
    std::vector<const DrumLane*> ownLoops;
    if (lanes)
        for (const DrumLane& l : *lanes)
            if (hasOwnLoop(l, clip))
                ownLoops.push_back(&l);
    auto ownLoop = [&](int pitch) {
        return std::any_of(ownLoops.begin(), ownLoops.end(), [pitch](const DrumLane* l) { return l->pitch == pitch; });
    };

    // Walk the instance pass by pass: from the offset to the span end, then whole loops. In a lane cycle the notes
    // repeat on the clip's loop instead, like a lane (below).
    const double offset = double(normalizeOffset(clip, a.offset, lanes));
    double u = offset; // clip time
    double t = 0;      // timeline ticks from the instance start
    int pass = 0;
    while (!span.cycles() && t < total) {
        double segEnd = double(span.end);
        if (u >= segEnd)
            break;
        auto begin = std::lower_bound(clip.notes.begin(), clip.notes.end(), u,
                                      [](const Note& n, double v) { return double(n.start) < v; });
        for (auto it = begin; it != clip.notes.end() && double(it->start) < segEnd; ++it) {
            const Note& n = *it;
            if (n.muted || ownLoop(n.pitch))
                continue;
            double on = t + (double(n.start) - u) * stretch;
            if (on >= total)
                break;
            double noteEnd = std::min(double(n.start) + double(n.length) * double(gateScale), segEnd);
            out.push_back(timed(a, n, on, std::min(t + (noteEnd - u) * stretch, total), transpose));
            out.back().pass = pass;
        }
        t += (segEnd - u) * stretch;
        pass++;
        if (!span.looping)
            break;
        u = double(span.start);
    }

    // Each own-loop lane repeats its region from where the instance starts in the clip, for the whole instance,
    // whether or not the clip loops.
    double limit = total;
    auto repeat = [&](LaneRegion region, auto plays) {
        double L = double(region.length);
        double elapsed = std::max(0.0, offset - double(span.start)); // clip time since the loop start
        for (double k = std::floor(elapsed / L); (k * L - elapsed) * stretch < limit; k++) {
            for (const Note& n : clip.notes) {
                if (n.muted || !plays(n.pitch) || n.start < region.start || n.start >= region.start + region.length)
                    continue;
                double q = double(n.start - region.start) + k * L;
                if (q < elapsed)
                    continue;
                double on = (q - elapsed) * stretch;
                if (on >= limit)
                    continue;
                double len = std::min(double(n.length) * double(gateScale), L - double(n.start - region.start));
                out.push_back(timed(a, n, on, std::min(on + len * stretch, limit), transpose));
                out.back().pass = int(k);
            }
        }
    };
    for (const DrumLane* lane : ownLoops)
        repeat(laneRegion(*lane, clip), [lane](int pitch) { return pitch == lane->pitch; });
    if (span.cycles())
        repeat(LaneRegion{span.start, span.pattern}, [&](int pitch) { return !ownLoop(pitch); });
    if (!ownLoops.empty())
        std::stable_sort(out.begin() + std::ptrdiff_t(first), out.end(),
                         [](const TimedNote& x, const TimedNote& y) { return x.start < y.start; });
}

std::optional<double> clipTimeAt(const Clip& clip, const ArrangerClip& a, double tick,
                                 const std::vector<DrumLane>* lanes) {
    ClipSpan span = clipSpan(clip, lanes);
    double rel = tick - double(a.position);
    if (rel < 0 || rel >= double(a.length) || span.length() <= 0)
        return std::nullopt;
    double u = double(normalizeOffset(clip, a.offset, lanes)) + rel / stretchOf(a);
    if (u < double(span.end))
        return u;
    if (!span.looping)
        return std::nullopt;
    double L = double(span.length());
    return double(span.start) + std::fmod(u - double(span.end), L);
}

std::optional<double> songTickAt(const Clip& clip, const ArrangerClip& a, double u, double near,
                                 const std::vector<DrumLane>* lanes) {
    ClipSpan span = clipSpan(clip, lanes);
    if (span.length() <= 0 || u < double(span.start) || u >= double(span.end))
        return std::nullopt;
    const double s = stretchOf(a);
    const double offset = double(normalizeOffset(clip, a.offset, lanes));
    // In instance ticks: the first pass runs from the offset to the clip's end, each later pass is a whole loop.
    double firstSeam = (double(span.end) - offset) * s;
    double period = double(span.length()) * s;
    auto inPass = [&](int pass) -> double {
        if (pass == 0)
            return u >= offset ? (u - offset) * s : -1;
        return firstSeam + double(pass - 1) * period + (u - double(span.start)) * s;
    };
    int pass = 0;
    double rel = near - double(a.position);
    if (span.looping && std::isfinite(rel) && rel >= firstSeam && rel < double(a.length))
        pass = 1 + int(std::floor((rel - firstSeam) / period));
    double t = inPass(pass);
    if (t < 0)
        t = span.looping ? inPass(1) : -1; // the first pass starts after u
    if (t < 0 || t >= double(a.length))
        return std::nullopt;
    return double(a.position) + t;
}

std::vector<Tick> loopSeams(const Clip& clip, const ArrangerClip& a, const std::vector<DrumLane>* lanes) {
    std::vector<Tick> seams;
    ClipSpan span = clipSpan(clip, lanes);
    if (!span.looping || span.length() <= 0)
        return seams;
    const double stretch = stretchOf(a);
    double t = (double(span.end) - double(normalizeOffset(clip, a.offset, lanes))) * stretch;
    double step = double(span.length()) * stretch;
    for (; t < double(a.length) && seams.size() < 4096; t += step)
        if (t > 0)
            seams.push_back(a.position + roundTick(t));
    return seams;
}

const ArrangerClip* instanceAt(const Track& track, Tick tick) {
    for (const ArrangerClip& a : track.arrangement)
        if (tick >= a.position && tick < instanceEnd(a))
            return &a;
    return nullptr;
}

} // namespace tessera
