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
        Sweep& s = out[count++];
        s.from = positionOf(m, wrap, q);
        s.to = positionOf(m, wrap, q + (end - d));
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
    if (m.direction == Direction::Freeze)
        return m.regionStart + floorMod(m.start, m.regionLength);
    double local = m.restartEvery > 0 ? floorMod(elapsed, m.restartEvery) : elapsed;
    double x = unwrapped(m, contentAt(m, m.rate * bent(m, std::max(0.0, local))));
    double wrap = std::floor(x / m.regionLength);
    return positionOf(m, wrap, x - wrap * m.regionLength);
}

int sweeps(const Motion& m, double a, double b, Sweep* out, int max) {
    if (b <= a || m.regionLength <= 0 || m.rate <= 0 || m.direction == Direction::Freeze)
        return 0;
    int count = 0;
    double e = std::max(0.0, a);
    while (e < b && count < max) {
        double base = m.restartEvery > 0 ? std::floor(e / m.restartEvery) * m.restartEvery : 0;
        double pieceEnd = m.restartEvery > 0 ? std::min(b, base + m.restartEvery) : b;
        double da = m.rate * bent(m, e - base);
        double db = m.rate * bent(m, pieceEnd - base);
        count += sweepTravel(m, da, db, base, out + count, max - count);
        e = pieceEnd;
    }
    return count;
}

double timeAt(const Motion& m, const Sweep& s, double pos) {
    double travel = s.travelFrom + std::abs(pos - s.from);
    return s.restartBase + unbent(m, travel / m.rate);
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
