#include "AutomationEdits.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace tessera {

std::vector<AutomationEnvelope>* lanesOf(Song& song, LaneOwner owner) {
    if (owner.track.valid()) {
        Track* t = song.findTrack(owner.track);
        return t ? &t->automation : nullptr;
    }
    Clip* c = song.findClip(owner.clip);
    return c ? &c->automation : nullptr;
}

const std::vector<AutomationEnvelope>* lanesOf(const Song& song, LaneOwner owner) {
    return lanesOf(const_cast<Song&>(song), owner);
}

const AutomationEnvelope* findLane(const Song& song, LaneOwner owner, AutomationId id) {
    if (const std::vector<AutomationEnvelope>* lanes = lanesOf(song, owner))
        for (const AutomationEnvelope& l : *lanes)
            if (l.id == id)
                return &l;
    return nullptr;
}

void EditLane::set(Song& song, const std::optional<AutomationEnvelope>& out,
                   const std::optional<AutomationEnvelope>& in) const {
    std::vector<AutomationEnvelope>* lanes = lanesOf(song, owner);
    if (!lanes)
        return;
    AutomationId id = out ? out->id : in->id;
    auto it = std::find_if(lanes->begin(), lanes->end(), [&](const AutomationEnvelope& l) { return l.id == id; });
    if (out && in && it != lanes->end()) {
        *it = *in;
        return;
    }
    if (out && it != lanes->end())
        lanes->erase(it);
    if (in && !out)
        lanes->insert(lanes->begin() + std::clamp<std::ptrdiff_t>(index, 0, std::ptrdiff_t(lanes->size())), *in);
}

std::vector<FoundLane> lanesTurning(const Song& song, const ParamRef& knob) {
    std::vector<FoundLane> out;
    for (const Track& t : song.tracks)
        for (const AutomationEnvelope& l : t.automation)
            if (l.paramTarget == knob)
                out.push_back({LaneOwner::ofTrack(t.id), &l});
    for (const auto& [id, clip] : song.clips)
        for (const AutomationEnvelope& l : clip.automation)
            if (l.paramTarget == knob)
                out.push_back({LaneOwner::ofClip(id), &l});
    return out;
}

int freeOutput(const std::vector<AutomationEnvelope>& lanes) {
    for (int o = 0; o < kMaxAutomationLanes; o++)
        if (std::none_of(lanes.begin(), lanes.end(),
                         [o](const AutomationEnvelope& l) { return drivesOutput(l) && l.outputIndex == o; }))
            return o;
    return -1;
}

AutomationEnvelope makeLane(AutomationId id, int output, uint32_t color) {
    AutomationEnvelope lane;
    lane.id = id;
    lane.outputIndex = std::clamp(output, 0, kMaxAutomationLanes - 1);
    lane.name = "Auto " + std::to_string(lane.outputIndex + 1);
    lane.color = color;
    return lane;
}

AutomationEnvelope makeKnobLane(AutomationId id, const ParamRef& target, std::string name, float value, bool steps,
                                uint32_t color) {
    AutomationEnvelope lane;
    lane.id = id;
    lane.name = std::move(name);
    lane.color = color;
    lane.paramTarget = target;
    lane.rangeMin = 0;
    lane.rangeMax = 1;
    AutoPoint start;
    start.t = 0;
    start.value = std::clamp(value, 0.f, 1.f);
    start.curve = steps ? CurveType::Hold : CurveType::Linear;
    lane.points.push_back(start);
    return lane;
}

CommandPtr addLane(const Song& song, LaneOwner owner, AutomationEnvelope lane) {
    const std::vector<AutomationEnvelope>* lanes = lanesOf(song, owner);
    if (!lanes || !lane.id.valid())
        return nullptr;
    // AUTO lanes are limited by the channels, knob lanes by what the engine plays.
    bool knob = lane.paramTarget.valid();
    int same = int(std::count_if(lanes->begin(), lanes->end(),
                                 [knob](const AutomationEnvelope& l) { return l.paramTarget.valid() == knob; }));
    if (same >= (knob ? kMaxKnobLanes : kMaxAutomationLanes))
        return nullptr;
    return std::make_unique<EditLane>("Add " + lane.name, owner, int(lanes->size()), std::nullopt, std::move(lane));
}

CommandPtr removeLane(const Song& song, LaneOwner owner, AutomationId id) {
    const std::vector<AutomationEnvelope>* lanes = lanesOf(song, owner);
    if (!lanes)
        return nullptr;
    for (size_t i = 0; i < lanes->size(); i++)
        if ((*lanes)[i].id == id)
            return std::make_unique<EditLane>("Delete " + (*lanes)[i].name, owner, int(i), (*lanes)[i], std::nullopt);
    return nullptr;
}

CommandPtr replaceLane(const Song& song, LaneOwner owner, const AutomationEnvelope& after, const std::string& name) {
    const AutomationEnvelope* before = findLane(song, owner, after.id);
    if (!before || *before == after)
        return nullptr;
    return std::make_unique<EditLane>(name, owner, 0, *before, after);
}

CommandPtr moveLane(const Song& song, LaneOwner owner, AutomationId id, int toIndex) {
    const std::vector<AutomationEnvelope>* lanes = lanesOf(song, owner);
    if (!lanes)
        return nullptr;
    for (size_t i = 0; i < lanes->size(); i++) {
        if ((*lanes)[i].id != id)
            continue;
        toIndex = std::clamp(toIndex, 0, int(lanes->size()) - 1);
        if (toIndex == int(i))
            return nullptr;
        auto out = std::make_unique<CompoundCommand>("Move " + (*lanes)[i].name);
        out->add(std::make_unique<EditLane>("Move lane", owner, int(i), (*lanes)[i], std::nullopt));
        out->add(std::make_unique<EditLane>("Move lane", owner, toIndex, std::nullopt, (*lanes)[i]));
        return out;
    }
    return nullptr;
}

const std::vector<VoltageRange>& voltageRanges() {
    static const std::vector<VoltageRange> ranges = {
        {"0 to 10 V", 0, 10, false},    {"-5 to 5 V", -5, 5, true}, {"0 to 5 V", 0, 5, false},
        {"-10 to 10 V", -10, 10, true}, {"0 to 1 V", 0, 1, false},
    };
    return ranges;
}

namespace autoEdits {

namespace {

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

/// How far p's value is from the straight line through a and b at p's time.
double lineDistance(const AutoPoint& p, const AutoPoint& a, const AutoPoint& b) {
    if (b.t == a.t)
        return std::fabs(double(p.value - a.value));
    double x = double(p.t - a.t) / double(b.t - a.t);
    double onLine = double(a.value) + x * double(b.value - a.value);
    return std::fabs(double(p.value) - onLine);
}

void rdp(const Points& s, size_t first, size_t last, float tolerance, std::vector<bool>& keep) {
    if (last <= first + 1)
        return;
    double worst = -1;
    size_t at = first;
    for (size_t i = first + 1; i < last; i++) {
        double d = lineDistance(s[i], s[first], s[last]);
        if (d > worst) {
            worst = d;
            at = i;
        }
    }
    if (worst > double(tolerance)) {
        keep[at] = true;
        rdp(s, first, at, tolerance, keep);
        rdp(s, at, last, tolerance, keep);
    }
}

} // namespace

Selection sortPoints(Points& points, const Selection& selected) {
    std::vector<size_t> order(points.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return points[a].t < points[b].t; });
    Points sorted;
    Selection moved;
    for (size_t i = 0; i < order.size(); i++) {
        sorted.push_back(points[order[i]]);
        if (selected.count(order[i]))
            moved.insert(i);
    }
    points = std::move(sorted);
    return moved;
}

size_t addPoint(Points& points, Tick t, float value, CurveType curve) {
    AutoPoint p;
    p.t = std::max<Tick>(0, t);
    p.value = clamp01(value);
    p.curve = curve;
    auto it = std::upper_bound(points.begin(), points.end(), p.t, [](Tick v, const AutoPoint& q) { return v < q.t; });
    size_t index = size_t(it - points.begin());
    points.insert(it, p);
    return index;
}

size_t insertOnCurve(Points& points, Tick t, float rest) {
    size_t cursor = 0;
    float value = evaluateAt(points, double(t), cursor, rest);
    CurveType curve = CurveType::Linear;
    float tension = 0;
    for (const AutoPoint& p : points)
        if (p.t <= t) {
            curve = p.curve;
            tension = p.tension;
        }
    size_t index = addPoint(points, t, value, curve);
    points[index].tension = tension;
    return index;
}

Points deletePoints(const Points& points, const Selection& selected) {
    Points out;
    for (size_t i = 0; i < points.size(); i++)
        if (!selected.count(i))
            out.push_back(points[i]);
    return out;
}

Points movePoints(const Points& points, const Selection& selected, Tick dt, float dv, Selection* moved) {
    Points out = points;
    for (size_t i : selected) {
        if (i >= out.size())
            continue;
        out[i].t = std::max<Tick>(0, out[i].t + dt);
        out[i].value = clamp01(out[i].value + dv);
    }
    Selection after = sortPoints(out, selected);
    if (moved)
        *moved = after;
    return out;
}

Points setValue(const Points& points, const Selection& selected, float value) {
    Points out = points;
    for (size_t i : selected)
        if (i < out.size())
            out[i].value = clamp01(value);
    return out;
}

Points setCurve(const Points& points, const Selection& selected, CurveType curve) {
    Points out = points;
    for (size_t i : selected)
        if (i < out.size()) {
            out[i].curve = curve;
            out[i].tension = 0;
        }
    return out;
}

Points simplify(const Points& samples, float tolerance) {
    if (samples.size() <= 2)
        return samples;
    std::vector<bool> keep(samples.size(), false);
    keep.front() = keep.back() = true;
    rdp(samples, 0, samples.size() - 1, tolerance, keep);
    Points out;
    for (size_t i = 0; i < samples.size(); i++)
        if (keep[i])
            out.push_back(samples[i]);
    return out;
}

Points replaceRange(const Points& points, Tick from, Tick to, const Points& drawn) {
    if (to < from)
        std::swap(from, to);
    Points out;
    for (const AutoPoint& p : points)
        if (p.t < from || p.t > to)
            out.push_back(p);
    for (AutoPoint p : drawn) {
        p.value = clamp01(p.value);
        out.push_back(p);
    }
    sortPoints(out, {});
    return out;
}

Points thin(const Points& points, float tolerance) {
    // Only runs of plain lines can be thinned without changing curves or steps.
    Points out;
    size_t i = 0;
    while (i < points.size()) {
        size_t j = i;
        while (j + 1 < points.size() && points[j].curve == CurveType::Linear && points[j].tension == 0)
            j++;
        Points run(points.begin() + long(i), points.begin() + long(j) + 1);
        Points kept = simplify(run, tolerance);
        out.insert(out.end(), kept.begin(), kept.end());
        i = j + 1;
    }
    return out;
}

Points quantizeTimes(const Points& points, const Selection& selected, Tick grid) {
    if (grid <= 0)
        return points;
    Points out = points;
    for (size_t i : selected)
        if (i < out.size())
            out[i].t = Tick(std::llround(double(out[i].t) / double(grid))) * grid;
    sortPoints(out, {});
    return out;
}

Points invert(const Points& points, const Selection& selected) {
    Points out = points;
    for (size_t i : selected)
        if (i < out.size())
            out[i].value = 1.f - out[i].value;
    return out;
}

Points reverse(const Points& points, const Selection& selected) {
    if (selected.size() < 2)
        return points;
    std::vector<size_t> idx(selected.begin(), selected.end());
    Tick from = points[idx.front()].t, to = points[idx.back()].t;
    Points out = points;
    size_t n = idx.size();
    for (size_t k = 0; k < n; k++) {
        const AutoPoint& src = points[idx[n - 1 - k]];
        AutoPoint& dst = out[idx[k]];
        dst.t = from + to - src.t;
        dst.value = src.value;
        // The segment that now starts here was the one that ended at `src`.
        if (k + 1 < n) {
            const AutoPoint& seg = points[idx[n - 2 - k]];
            dst.curve = seg.curve;
            dst.tension = -seg.tension;
        }
    }
    sortPoints(out, {});
    return out;
}

Points randomize(const Points& points, const Selection& selected, float amount, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> spread(-1.f, 1.f);
    Points out = points;
    for (size_t i : selected)
        if (i < out.size())
            out[i].value = clamp01(out[i].value + spread(rng) * amount);
    return out;
}

const char* shapeName(Shape shape) {
    static const char* const names[] = {"Sine",         "Triangle",      "Saw up",   "Saw down", "Square",
                                        "Random steps", "Smooth random", "Exp rise", "Exp fall"};
    return names[int(shape)];
}

Points stampShape(const Points& points, Tick from, Tick to, Shape shape, double cycles, float low, float high,
                  double phase, uint32_t seed) {
    if (to <= from || cycles <= 0)
        return points;
    Points drawn;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    double cycle = double(to - from) / cycles;
    auto at = [&](double ticks, float x, CurveType curve, float tension = 0) {
        AutoPoint p;
        p.t = from + Tick(std::llround(ticks));
        p.value = low + (high - low) * x;
        p.curve = curve;
        p.tension = tension;
        drawn.push_back(p);
    };
    int whole = int(std::ceil(cycles - 1e-9));
    // A cycle starting `phase` early, so the stamp can begin anywhere in its wave. One extra cycle on each side
    // gives the wave its true values at both ends of the range.
    double offset = -phase * cycle;
    for (int c = -1; c <= whole; c++) {
        double s = offset + c * cycle;
        switch (shape) {
        case Shape::Sine:
            // Peaks and troughs joined by S-curves read as a sine.
            at(s + cycle * 0.25, 1, CurveType::SCurve);
            at(s + cycle * 0.75, 0, CurveType::SCurve);
            break;
        case Shape::Triangle:
            at(s, 0, CurveType::Linear);
            at(s + cycle * 0.5, 1, CurveType::Linear);
            break;
        case Shape::SawUp:
            at(s, 0, CurveType::Linear);
            at(s + cycle - 1, 1, CurveType::Linear);
            break;
        case Shape::SawDown:
            at(s, 1, CurveType::Linear);
            at(s + cycle - 1, 0, CurveType::Linear);
            break;
        case Shape::Square:
            at(s, 1, CurveType::Hold);
            at(s + cycle * 0.5, 0, CurveType::Hold);
            break;
        case Shape::RandomSteps:
            at(s, unit(rng), CurveType::Hold);
            break;
        case Shape::SmoothRandom:
            at(s, unit(rng), CurveType::SCurve);
            break;
        case Shape::ExpRise:
            at(s, 0, CurveType::Exponential);
            at(s + cycle - 1, 1, CurveType::Linear);
            break;
        case Shape::ExpFall:
            at(s, 1, CurveType::Exponential, -1);
            at(s + cycle - 1, 0, CurveType::Linear);
            break;
        }
    }
    AutomationEnvelope wave;
    wave.points = drawn;
    sortPoints(wave.points, {});
    auto pointAt = [&](Tick t) {
        AutoPoint p;
        p.t = t;
        p.value = evaluate(wave, double(t));
        for (const AutoPoint& q : wave.points)
            if (q.t <= t) {
                p.curve = q.curve;
                p.tension = q.tension;
            }
        return p;
    };
    Points inside{pointAt(from)};
    for (const AutoPoint& p : wave.points)
        if (p.t > from && p.t < to)
            inside.push_back(p);
    inside.push_back(pointAt(to));
    return replaceRange(points, from, to, inside);
}

Points scaleSelection(const Points& points, const Selection& selected, float anchor, float factor, Tick pivot,
                      double stretch) {
    Points out = points;
    for (size_t i : selected) {
        if (i >= out.size())
            continue;
        out[i].value = clamp01(anchor + (out[i].value - anchor) * factor);
        out[i].t = std::max<Tick>(0, pivot + Tick(std::llround(double(out[i].t - pivot) * stretch)));
    }
    sortPoints(out, {});
    return out;
}

Points tilt(const Points& points, const Selection& selected, float anchor, float startFactor, float endFactor) {
    if (selected.empty())
        return points;
    Tick from = points[*selected.begin()].t, to = points[*selected.rbegin()].t;
    Points out = points;
    for (size_t i : selected) {
        if (i >= out.size())
            continue;
        double x = to > from ? double(out[i].t - from) / double(to - from) : 0;
        float factor = float(startFactor + (endFactor - startFactor) * x);
        out[i].value = clamp01(anchor + (out[i].value - anchor) * factor);
    }
    return out;
}

} // namespace autoEdits

} // namespace tessera
