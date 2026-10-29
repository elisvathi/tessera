#include "Playheads.hpp"

#include "DeviceTypes.hpp"

namespace tessera::playheads {

namespace {

constexpr double kPi = 3.14159265358979323846;

double floorMod(double x, double m) { return x - std::floor(x / m) * m; }

/// The elapsed time inside a bend cycle, bent: the cycle's ends stay where they are.
double bent(const Motion& m, double elapsed) {
    if (m.bendCycle <= 0 || m.bendAmount <= 0 || m.bend == Bend::Straight)
        return elapsed;
    double k = std::floor(elapsed / m.bendCycle);
    double f = elapsed / m.bendCycle - k;
    return (k + warp(m.bend, m.bendAmount, f)) * m.bendCycle;
}

/// The elapsed time a bent time comes from. The shapes always rise, so halving finds it.
double unbent(const Motion& m, double b) {
    if (m.bendCycle <= 0 || m.bendAmount <= 0 || m.bend == Bend::Straight)
        return b;
    double k = std::floor(b / m.bendCycle);
    double g = b / m.bendCycle - k;
    double lo = 0, hi = 1;
    for (int i = 0; i < 48; i++) {
        double mid = (lo + hi) / 2;
        (warp(m.bend, m.bendAmount, mid) < g ? lo : hi) = mid;
    }
    return (k + (lo + hi) / 2) * m.bendCycle;
}

bool repeats(const Motion& m) { return m.repeatSize > 0 && m.repeatTimes > 1; }

/// Where travel `d` has taken the playhead through the clip's content (before wrapping): with a repeat, each
/// segment is travelled `repeatTimes` times before the next.
double contentAt(const Motion& m, double d, double* segmentEnd = nullptr) {
    if (!repeats(m)) {
        if (segmentEnd)
            *segmentEnd = INFINITY;
        return d;
    }
    double r = std::floor(d / m.repeatSize);
    if (segmentEnd)
        *segmentEnd = (r + 1) * m.repeatSize;
    return std::floor(r / m.repeatTimes) * m.repeatSize + (d - r * m.repeatSize);
}

/// The unwrapped place along the region: one region length per wrap (or per ping-pong half).
double unwrapped(const Motion& m, double content) {
    double s = floorMod(m.start, m.regionLength);
    return m.direction == Direction::Reverse ? content + (m.regionLength - s) : content + s;
}

/// A clip position from the place within one wrap (`q`, 0..length) and which wrap it is.
double positionOf(const Motion& m, double wrap, double q) {
    bool backward = m.direction == Direction::Reverse ||
                    (m.direction == Direction::PingPong && floorMod(wrap, 2) >= 1);
    return backward ? m.regionStart + m.regionLength - q : m.regionStart + q;
}

// ---- Drawn paths ----

/// The path's place (0..1) at `c` ticks into its cycle, inside the segment that starts at point `i` (-1: before the
/// first point). At a point's own time the segment before it is meant, so a hold's jump comes after it.
double pathValue(const std::vector<AutoPoint>& path, int i, double c) {
    if (i < 0)
        return path.front().value;
    if (i + 1 >= int(path.size()))
        return path.back().value;
    const AutoPoint& a = path[size_t(i)];
    const AutoPoint& b = path[size_t(i) + 1];
    if (b.t <= a.t)
        return b.value;
    double x = std::clamp((c - double(a.t)) / double(b.t - a.t), 0.0, 1.0);
    return double(a.value) + (double(b.value) - double(a.value)) * curveShape(a.curve, a.tension, x);
}

/// The segment `c` falls in: the last point at or before it, or -1 before the first.
int pathSegment(const std::vector<AutoPoint>& path, double c) {
    auto it = std::upper_bound(path.begin(), path.end(), c, [](double v, const AutoPoint& p) { return v < double(p.t); });
    return int(it - path.begin()) - 1;
}

double drawnPosition(const Motion& m, double value) {
    return m.regionStart + std::clamp(value, 0.0, 1.0) * m.regionLength;
}

/// Sweeps of a drawn path over the playhead's own time [oa, ob): one per stretch between points, cycle ends and
/// jumps.
int sweepDrawn(const Motion& m, double oa, double ob, double restartBase, Sweep* out, int max) {
    const std::vector<AutoPoint>& path = *m.path;
    int count = 0;
    double o = oa;
    double lastTo = NAN, lastDir = 0;
    while (o < ob && count < max) {
        double k = std::floor(o / m.pathCycle);
        double c = o - k * m.pathCycle;
        int i = pathSegment(path, c);
        double next = i + 1 < int(path.size()) ? double(path[size_t(i) + 1].t) : m.pathCycle;
        next = std::min(next, m.pathCycle);
        if (next <= c)
            next = m.pathCycle;
        double end = std::min(ob, k * m.pathCycle + next);
        double v0 = pathValue(path, i, c);
        double v1 = pathValue(path, i, end - k * m.pathCycle);
        double from = drawnPosition(m, v0), to = drawnPosition(m, v1);
        if (to != from) {
            Sweep& s = out[count++];
            s.from = from;
            s.to = to;
            double dir = to > from ? 1 : -1;
            // Turning back where the last stretch ended: the note there was just played.
            s.includeFrom = !(from == lastTo && dir != lastDir);
            s.travelFrom = 0;
            s.restartBase = restartBase;
            s.ownFrom = o;
            s.ownTo = end;
            lastDir = dir;
        }
        lastTo = to;
        o = end;
    }
    return count;
}

bool ordered(const Motion& m) {
    return m.order != Order::InOrder && m.orderStep > 0 && m.orderStep < m.regionLength;
}

/// How far the step holding `pos` moves when its region is read in order `m.order` on pass `pass`.
double orderShift(const Motion& m, double pos, double pass) {
    if (!ordered(m))
        return 0;
    int count = int(std::ceil(m.regionLength / m.orderStep - 1e-9));
    int k = std::clamp(int(std::floor((pos - m.regionStart) / m.orderStep)), 0, count - 1);
    int to = orderedStep(m.order, k, count, int64_t(pass), m.seed);
    // The last step can be shorter; a full step moved there would run past the region's end.
    double shift = double(to - k) * m.orderStep;
    double rel = pos - m.regionStart + shift;
    return rel >= m.regionLength ? shift - (rel - m.regionLength + 1e-6) : shift;
}

int sweepTravel(const Motion& m, double da, double db, double restartBase, Sweep* out, int max) {
    int count = 0;
    double d = da;
    double length = m.regionLength;
    while (d < db && count < max) {
        double segmentEnd;
        double content = contentAt(m, d, &segmentEnd);
        double x = unwrapped(m, content);
        double wrap = std::floor(x / length);
        double q = x - wrap * length;
        double end = std::min({db, segmentEnd, d + (length - q)});
        double from = positionOf(m, wrap, q);
        if (ordered(m)) {
            // Each step is read on its own: a sweep ends at a step's edge, and moves to where the order puts it.
            double rel = from - m.regionStart;
            double into = std::fmod(rel, m.orderStep);
            bool forward = positionOf(m, wrap, q + 1e-3) > from;
            double room = forward ? m.orderStep - into : (into <= 1e-9 ? m.orderStep : into);
            end = std::min(end, d + room);
        }
        Sweep& s = out[count++];
        s.from = from;
        s.to = positionOf(m, wrap, q + (end - d));
        double shift = orderShift(m, (s.from + s.to) / 2, wrap);
        s.from += shift;
        s.to += shift;
        bool atSegmentStart = repeats(m) && d == std::floor(d / m.repeatSize) * m.repeatSize;
        s.includeFrom = !(m.direction == Direction::PingPong && q == 0 && x > 0 && !atSegmentStart);
        s.travelFrom = d;
        s.restartBase = restartBase;
        d = end;
    }
    return count;
}

/// A length as a fraction of a whole note (four quarters), or in bars when `bars` is set.
struct Length {
    const char* name;
    double wholes;
    bool bars = false;
};

double ticksOf(const Length& l, TimeSig ts) {
    return l.bars ? l.wholes * double(ticksPerBar(ts)) : l.wholes * 4 * double(kPpq);
}

template <size_t N>
std::vector<std::string> namesOf(const Length (&list)[N]) {
    std::vector<std::string> out;
    for (const Length& l : list)
        out.push_back(l.name);
    return out;
}

template <size_t N>
double ticksAt(const Length (&list)[N], int index, TimeSig ts) {
    return ticksOf(list[size_t(std::clamp(index, 0, int(N) - 1))], ts);
}

const Length kRepeats[] = {{"Off", 0},       {"1/16", 1.0 / 16}, {"1/8", 1.0 / 8},
                           {"1/4", 1.0 / 4}, {"1/2", 1.0 / 2},   {"1 bar", 1, true}};
const Length kCycles[] = {{"1/16", 1.0 / 16}, {"1/8", 1.0 / 8}, {"1/4", 1.0 / 4},  {"1/2", 1.0 / 2},
                          {"1 bar", 1, true}, {"2 bars", 2, true}, {"4 bars", 4, true}};
const Length kPathCycles[] = {{"1 bar", 1, true}, {"2 bars", 2, true}, {"4 bars", 4, true}, {"8 bars", 8, true},
                              {"16 bars", 16, true}};
const Length kOrderSteps[] = {{"1/16", 1.0 / 16}, {"1/8", 1.0 / 8}, {"1/4", 1.0 / 4}, {"1/2", 1.0 / 2},
                              {"1 bar", 1, true}};
const Length kFixedLengths[] = {{"1/64", 1.0 / 64}, {"1/32", 1.0 / 32}, {"1/16", 1.0 / 16}, {"1/8", 1.0 / 8},
                                {"1/4", 1.0 / 4},   {"1/2", 1.0 / 2},   {"1 bar", 1, true}};

struct Rate {
    const char* name;
    double value;
};
const Rate kRates[] = {{"1/16x", 1.0 / 16}, {"1/8x", 1.0 / 8}, {"1/4x", 0.25}, {"1/3x", 1.0 / 3},
                       {"1/2x", 0.5},       {"2/3x", 2.0 / 3}, {"3/4x", 0.75}, {"1x", 1},
                       {"4/3x", 4.0 / 3},   {"3/2x", 1.5},     {"2x", 2},      {"3x", 3},
                       {"4x", 4},           {"8x", 8},         {"16x", 16}};

} // namespace

const std::vector<std::string>& rateNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const Rate& r : kRates)
            out.push_back(r.name);
        return out;
    }();
    return names;
}

double playheadRate(int index) {
    return kRates[size_t(std::clamp(index, 0, int(std::size(kRates)) - 1))].value;
}

const std::vector<std::string>& repeatNames() {
    static const std::vector<std::string> names = namesOf(kRepeats);
    return names;
}
double repeatTicks(int index, TimeSig ts) { return ticksAt(kRepeats, index, ts); }

const std::vector<std::string>& cycleNames() {
    static const std::vector<std::string> names = namesOf(kCycles);
    return names;
}
double cycleTicks(int index, TimeSig ts) { return ticksAt(kCycles, index, ts); }

const std::vector<std::string>& fixedLengthNames() {
    static const std::vector<std::string> names = namesOf(kFixedLengths);
    return names;
}
double fixedLengthTicks(int index, TimeSig ts) { return ticksAt(kFixedLengths, index, ts); }

const std::vector<std::string>& orderStepNames() {
    static const std::vector<std::string> names = namesOf(kOrderSteps);
    return names;
}
double orderStepTicks(int index, TimeSig ts) { return ticksAt(kOrderSteps, index, ts); }

int orderedStep(Order order, int k, int count, int64_t pass, uint32_t seed) {
    if (count <= 1)
        return 0;
    k = ((k % count) + count) % count;
    switch (order) {
    case Order::OutsideIn: // first, last, second, second-last…
        return k % 2 == 0 ? k / 2 : count - 1 - k / 2;
    case Order::InsideOut: // the same, from the middle outwards
        return orderedStep(Order::OutsideIn, count - 1 - k, count, pass, seed);
    case Order::OddEven: { // every other step, then the ones between
        int half = (count + 1) / 2;
        return k < half ? 2 * k : 2 * (k - half) + 1;
    }
    case Order::Shuffle: {
        // An affine shuffle: a multiplier that shares no factor with the count visits every step once, and needs
        // no table, so it runs on the audio thread. Each pass picks its own from the seed.
        uint32_t h = seed * 2654435761u ^ uint32_t(pass) * 40503u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        static const int kPrimes[] = {3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41};
        int a = 1;
        for (int i = 0; i < 12; i++) {
            int p = kPrimes[(size_t(h) + size_t(i)) % 12];
            if (count % p != 0) {
                a = p;
                break;
            }
        }
        return int((int64_t(a) * k + int64_t(h % uint32_t(count))) % count);
    }
    case Order::InOrder:
    default:
        return k;
    }
}

const std::vector<std::string>& pathCycleNames() {
    static const std::vector<std::string> names = namesOf(kPathCycles);
    return names;
}
double pathCycleTicks(int index, TimeSig ts) { return ticksAt(kPathCycles, index, ts); }

double warp(Bend shape, double amount, double f) {
    f = std::clamp(f, 0.0, 1.0);
    amount = std::clamp(amount, 0.0, 1.0);
    double p = 1 + 3 * amount;
    switch (shape) {
    case Bend::EaseIn:
        return std::pow(f, p);
    case Bend::EaseOut:
        return 1 - std::pow(1 - f, p);
    case Bend::EaseInOut:
        return f < 0.5 ? 0.5 * std::pow(2 * f, p) : 1 - 0.5 * std::pow(2 - 2 * f, p);
    case Bend::Swing: {
        // The first half of the cycle takes longer, the second catches up.
        double k = 0.5 + 0.25 * amount;
        return f < k ? 0.5 * f / k : 0.5 + 0.5 * (f - k) / (1 - k);
    }
    case Bend::Stairs:
        // Four slow-fast steps per cycle; the slope never goes below zero.
        return f - amount * std::sin(8 * kPi * f) / (8 * kPi);
    case Bend::Straight:
    default:
        return f;
    }
}

double positionAt(const Motion& m, double elapsed) {
    if (m.regionLength <= 0)
        return m.regionStart;
    if (m.drawn()) {
        double local = m.restartEvery > 0 ? floorMod(elapsed, m.restartEvery) : elapsed;
        double c = floorMod(m.rate * std::max(0.0, local), m.pathCycle);
        return drawnPosition(m, pathValue(*m.path, pathSegment(*m.path, c), c));
    }
    if (m.direction == Direction::Freeze)
        return m.regionStart + floorMod(m.start, m.regionLength);
    double local = m.restartEvery > 0 ? floorMod(elapsed, m.restartEvery) : elapsed;
    double x = unwrapped(m, contentAt(m, bent(m, m.rate * std::max(0.0, local))));
    double wrap = std::floor(x / m.regionLength);
    double pos = positionOf(m, wrap, x - wrap * m.regionLength);
    return pos + orderShift(m, pos, wrap);
}

int sweeps(const Motion& m, double a, double b, Sweep* out, int max) {
    if (b <= a || m.regionLength <= 0 || m.rate <= 0 || m.direction == Direction::Freeze)
        return 0;
    int count = 0;
    double e = std::max(0.0, a);
    while (e < b && count < max) {
        double base = m.restartEvery > 0 ? std::floor(e / m.restartEvery) * m.restartEvery : 0;
        double pieceEnd = m.restartEvery > 0 ? std::min(b, base + m.restartEvery) : b;
        if (m.drawn()) {
            count += sweepDrawn(m, m.rate * (e - base), m.rate * (pieceEnd - base), base, out + count, max - count);
            e = pieceEnd;
            continue;
        }
        double da = bent(m, m.rate * (e - base));
        double db = bent(m, m.rate * (pieceEnd - base));
        count += sweepTravel(m, da, db, base, out + count, max - count);
        e = pieceEnd;
    }
    return count;
}

double timeAt(const Motion& m, const Sweep& s, double pos) {
    if (m.drawn()) {
        // Within one stretch the place moves one way only, so halving finds when it reaches `pos`.
        double k = std::floor(s.ownFrom / m.pathCycle);
        int i = pathSegment(*m.path, s.ownFrom - k * m.pathCycle);
        double lo = s.ownFrom, hi = s.ownTo;
        bool rising = s.to > s.from;
        for (int n = 0; n < 48; n++) {
            double mid = (lo + hi) / 2;
            double p = drawnPosition(m, pathValue(*m.path, i, mid - k * m.pathCycle));
            ((rising ? p < pos : p > pos) ? lo : hi) = mid;
        }
        return s.restartBase + (lo + hi) / 2 / m.rate;
    }
    double travel = s.travelFrom + std::abs(pos - s.from);
    return s.restartBase + unbent(m, travel) / m.rate;
}

void curvedKnobs(const float* knobs, int count, const std::vector<PlayheadCurve>& curves, double elapsed, float* out) {
    std::copy(knobs, knobs + count, out);
    for (const PlayheadCurve& c : curves) {
        if (c.param >= count || c.points.empty() || c.length <= 0)
            continue;
        size_t cursor = 0;
        double t = elapsed - std::floor(elapsed / double(c.length)) * double(c.length);
        out[c.param] = std::clamp(evaluateAt(c.points, t, cursor, out[c.param]), 0.f, 1.f);
    }
}

NoteShape noteShapeFrom(const float* knobs) {
    auto shown = [&](int k) { return devices::display(devices::playheadInfo().params[size_t(k)], knobs[k]); };
    NoteShape s;
    s.octave = int(std::lround(shown(knob::kOctave)));
    s.keySteps = int(std::lround(shown(knob::kKeySteps)));
    s.pick = NotePick(std::lround(shown(knob::kNotes)));
    s.invert = shown(knob::kInvert) > 0.5f;
    s.velocity = shown(knob::kVelocity) / 100;
    return s;
}

int shapedPitch(const NoteShape& shape, int pitch, int low, int high, const Scale& key) {
    if (shape.invert) {
        // Mirrored by key steps, so the mirror stays in the key: the lowest note becomes the highest.
        int fromLow = key.indexOf(pitch) - key.indexOf(low);
        int span = key.indexOf(high) - key.indexOf(low);
        pitch = key.stepBy(low, span - fromLow);
    }
    pitch += 12 * shape.octave;
    if (shape.keySteps != 0)
        pitch = key.stepBy(pitch, shape.keySteps);
    return std::clamp(pitch, 0, 127);
}

bool wantsDrawnPath(const float* knobs) {
    return std::lround(devices::display(devices::playheadInfo().params[knob::kPath], knobs[knob::kPath])) == 1;
}

std::vector<AutoPoint> pathFromSettings(const Motion& m, double cycle, double step) {
    std::vector<AutoPoint> points;
    if (m.regionLength <= 0 || step <= 0)
        return points;
    auto valueAt = [&](double own) { return float((positionAt(m, own / m.rate) - m.regionStart) / m.regionLength); };
    float last = valueAt(0), slope = 0;
    points.push_back({0, last, CurveType::Linear, 0});
    for (double t = step; t < cycle; t += step) {
        float v = valueAt(t);
        if (std::abs(v - last) > 0.5f) {
            // A wrap: carry on to just before the jump at the same speed, hold there, then jump.
            AutoPoint before{Tick(t) - 1, std::clamp(last + slope * float((step - 1) / step), 0.f, 1.f),
                             CurveType::Hold, 0};
            if (before.t > points.back().t)
                points.push_back(before);
        } else {
            slope = v - last;
        }
        points.push_back({Tick(t), v, CurveType::Linear, 0});
        last = v;
    }
    // Points on a straight line between their neighbours add nothing to edit: keep only the corners.
    std::vector<AutoPoint> corners;
    for (size_t i = 0; i < points.size(); i++) {
        if (i > 0 && i + 1 < points.size() && points[i - 1].curve == CurveType::Linear &&
            points[i].curve == CurveType::Linear && !corners.empty()) {
            const AutoPoint& a = corners.back();
            const AutoPoint& b = points[i + 1];
            double x = double(points[i].t - a.t) / double(b.t - a.t);
            if (std::abs(a.value + (b.value - a.value) * x - points[i].value) < 1e-4)
                continue;
        }
        corners.push_back(points[i]);
    }
    return corners;
}

Motion motionFrom(const float* knobs, const float* deviceKnobs, double clipStart, double clipLength, TimeSig ts) {
    auto shown = [&](int k) { return devices::display(devices::playheadInfo().params[size_t(k)], knobs[k]); };
    auto index = [&](int k) { return int(std::lround(shown(k))); };
    double step = double(kPpq) / 4;
    double bar = double(ticksPerBar(ts));

    Motion m;
    m.rate = playheadRate(index(knob::kRate));
    m.direction = Direction(index(knob::kDirection));
    double regionStart = std::clamp(shown(knob::kRegionStart) * step, 0.0, std::max(0.0, clipLength - 1));
    double regionLength = shown(knob::kRegionLength) * step;
    if (regionLength <= 0 || regionStart + regionLength > clipLength)
        regionLength = clipLength - regionStart;
    m.regionStart = clipStart + regionStart;
    m.regionLength = std::max(0.0, regionLength);
    m.start = (shown(knob::kStart) + shown(knob::kOffset)) * step;
    m.repeatSize = repeatTicks(index(knob::kRepeatSize), ts);
    m.repeatTimes = index(knob::kRepeatTimes);
    m.bend = Bend(index(knob::kBendShape));
    m.bendAmount = shown(knob::kBendAmount) / 100;
    m.bendCycle = cycleTicks(index(knob::kBendCycle), ts);
    m.pathCycle = pathCycleTicks(index(knob::kPathCycle), ts);
    m.order = Order(index(knob::kOrder));
    m.orderStep = orderStepTicks(index(knob::kOrderStep), ts);
    if (deviceKnobs) {
        const devices::TypeInfo& info = devices::deviceInfo(DeviceType::Playheads);
        auto device = [&](int k) { return devices::display(info.params[size_t(k)], deviceKnobs[k]); };
        if (Restart(std::lround(device(knob::kRestart))) == Restart::EveryBars)
            m.restartEvery = std::round(device(knob::kRestartBars)) * bar;
    }
    return m;
}

double noteLength(const float* knobs, const Motion& m, double written, TimeSig ts) {
    const devices::ParamInfo& mode = devices::playheadInfo().params[knob::kLength];
    double longest = 4 * double(ticksPerBar(ts)); // a frozen or crawling playhead never holds a note forever
    switch (LengthMode(std::lround(devices::display(mode, knobs[knob::kLength])))) {
    case LengthMode::FollowsPlayhead:
        return std::min(longest, written / std::max(m.rate, 1e-3));
    case LengthMode::Fixed: {
        const devices::ParamInfo& fixed = devices::playheadInfo().params[knob::kFixedLength];
        return fixedLengthTicks(int(std::lround(devices::display(fixed, knobs[knob::kFixedLength]))), ts);
    }
    case LengthMode::AsWritten:
    default:
        return std::min(longest, written);
    }
}

} // namespace tessera::playheads
