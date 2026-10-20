#include "NoteFields.hpp"

#include "ExpressionEdits.hpp"
#include "Scale.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <random>
#include <vector>

namespace tessera::fields {

namespace {

constexpr double kBeat = double(kPpq);

const Info kInfo[] = {
    {"Start", "beats", 0, 1e6, 1.0 / 16, false},
    {"Length", "beats", 1.0 / kBeat, 1e5, 1.0 / 16, false},
    {"Pitch", "", 0, 127, 0.25, true},
    {"Velocity", "%", 0, 100, 0.5, false},
    {"Release", "%", 0, 100, 0.5, false},
    {"Micro-timing", "% of 1/16", -50, 50, 0.5, false},
    {"Chance", "%", 0, 100, 0.5, false},
    {"Velocity spread", "%", 0, 100, 0.5, false},
    {"Repeats", "", -16, 16, 0.1, true},
    {"Repeat curve", "%", -100, 100, 1, false},
    {"Repeat end velocity", "%", 0, 100, 0.5, false},
    {"Repeat gate", "%", 1, 100, 0.5, false},
    {"Timing spread", "% of 1/16", 0, 50, 0.5, false},
    {"Channel", "", 1, kMaxNoteChannel, 0.1, true},
};

double clampTo(Field f, double v) {
    const Info& i = info(f);
    v = std::clamp(v, i.min, i.max);
    return i.integer ? std::round(v) : v;
}

template <typename F>
edits::NoteList each(const edits::NoteList& notes, const edits::IdSet& ids, F change) {
    edits::NoteList out = notes;
    for (Note& n : out)
        if (ids.count(n.id))
            change(n);
    std::sort(out.begin(), out.end(), noteOrder);
    return out;
}

} // namespace

const Info& info(Field f) { return kInfo[std::clamp(int(f), 0, int(Field::Count) - 1)]; }

std::string lowerName(Field f) {
    std::string s = info(f).name;
    for (char& ch : s)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

double get(const Note& n, Field f) {
    switch (f) {
    case Field::Start:
        return double(n.start) / kBeat;
    case Field::Length:
        return double(n.length) / kBeat;
    case Field::Pitch:
        return n.pitch;
    case Field::Velocity:
        return n.velocity * 100.0;
    case Field::Release:
        return n.releaseVelocity * 100.0;
    case Field::Micro:
        return n.microOffset * 100.0;
    case Field::Chance:
        return n.ops.chance * 100.0;
    case Field::VelocitySpread:
        return n.ops.velocitySpread * 100.0;
    case Field::Repeats:
        return n.ops.repeats;
    case Field::RepeatCurve:
        return n.ops.repeatCurve * 100.0;
    case Field::RepeatVelocityEnd:
        return n.ops.repeatVelEnd * 100.0;
    case Field::RepeatGate:
        return n.ops.repeatGate * 100.0;
    case Field::TimingSpread:
        return n.ops.timingSpread * 100.0;
    case Field::Channel:
        return n.channel;
    default:
        return 0;
    }
}

void set(Note& n, Field f, double value) {
    double v = clampTo(f, value);
    switch (f) {
    case Field::Start:
        n.start = std::max<Tick>(0, Tick(std::llround(v * kBeat)));
        break;
    case Field::Length: {
        Tick oldLength = n.length;
        n.length = std::max<Tick>(1, Tick(std::llround(v * kBeat)));
        expr::keepInTime(n, n.start, oldLength);
        break;
    }
    case Field::Pitch:
        n.pitch = int8_t(v);
        break;
    case Field::Velocity:
        n.velocity = float(v / 100);
        break;
    case Field::Release:
        n.releaseVelocity = float(v / 100);
        break;
    case Field::Micro:
        n.microOffset = float(v / 100);
        break;
    case Field::Chance:
        n.ops.chance = float(v / 100);
        break;
    case Field::VelocitySpread:
        n.ops.velocitySpread = float(v / 100);
        break;
    case Field::Repeats:
        n.ops.repeats = int8_t(v);
        break;
    case Field::RepeatCurve:
        n.ops.repeatCurve = float(v / 100);
        break;
    case Field::RepeatVelocityEnd:
        n.ops.repeatVelEnd = float(v / 100);
        break;
    case Field::RepeatGate:
        n.ops.repeatGate = float(v / 100);
        break;
    case Field::TimingSpread:
        n.ops.timingSpread = float(v / 100);
        break;
    case Field::Channel:
        n.channel = uint8_t(v);
        break;
    default:
        break;
    }
}

std::pair<double, double> range(const edits::NoteList& notes, const edits::IdSet& ids, Field f) {
    double lo = INFINITY, hi = -INFINITY;
    for (const Note& n : notes)
        if (ids.count(n.id)) {
            lo = std::min(lo, get(n, f));
            hi = std::max(hi, get(n, f));
        }
    return lo <= hi ? std::make_pair(lo, hi) : std::make_pair(0.0, 0.0);
}

edits::NoteList offset(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double delta) {
    return each(notes, ids, [&](Note& n) { set(n, f, get(n, f) + delta); });
}

edits::NoteList spread(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double factor) {
    auto [lo, hi] = range(notes, ids, f);
    double mid = (lo + hi) / 2;
    return each(notes, ids, [&](Note& n) { set(n, f, mid + (get(n, f) - mid) * std::max(0.0, factor)); });
}

double mean(const edits::NoteList& notes, const edits::IdSet& ids, Field f) {
    double sum = 0;
    int count = 0;
    for (const Note& n : notes)
        if (ids.count(n.id)) {
            sum += get(n, f);
            count++;
        }
    return count > 0 ? sum / count : 0;
}

edits::NoteList spreadAroundMean(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double factor) {
    double m = mean(notes, ids, f);
    return each(notes, ids, [&](Note& n) { set(n, f, m + (get(n, f) - m) * std::max(0.0, factor)); });
}

edits::NoteList chaos(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double amount, uint32_t seed) {
    const Info& in = info(f);
    double reach = std::clamp(amount, 0.0, 1.0) * (in.max - in.min);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> pick(-1, 1);
    // Directions are drawn in note order, so the same seed scatters the same way whatever the set's order.
    edits::NoteList out = notes;
    for (Note& n : out)
        if (ids.count(n.id)) {
            double r = pick(rng);
            set(n, f, get(n, f) + r * reach);
        }
    std::sort(out.begin(), out.end(), noteOrder);
    return out;
}

std::vector<int> histogram(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double low, double high,
                           int buckets) {
    std::vector<int> out(size_t(std::max(1, buckets)), 0);
    double width = high > low ? (high - low) / double(out.size()) : 1;
    for (const Note& n : notes)
        if (ids.count(n.id)) {
            int b = int(std::floor((get(n, f) - low) / width));
            out[size_t(std::clamp(b, 0, int(out.size()) - 1))]++;
        }
    return out;
}

edits::NoteList randomize(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double low, double high,
                          uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> pick(std::min(low, high), std::max(low, high));
    // Draw in note order so the same seed gives the same values whatever the set's order.
    edits::NoteList out = notes;
    for (Note& n : out)
        if (ids.count(n.id))
            set(n, f, pick(rng));
    std::sort(out.begin(), out.end(), noteOrder);
    return out;
}

edits::NoteList ramp(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double first, double last,
                     double curve) {
    std::vector<const Note*> chosen;
    for (const Note& n : notes)
        if (ids.count(n.id))
            chosen.push_back(&n);
    if (chosen.empty())
        return notes;
    Tick t0 = chosen.front()->start, t1 = chosen.front()->start;
    for (const Note* n : chosen) {
        t0 = std::min(t0, n->start);
        t1 = std::max(t1, n->start);
    }
    double power = std::pow(2.0, -std::clamp(curve, -1.0, 1.0) * 2);
    return each(notes, ids, [&](Note& n) {
        double x = t1 > t0 ? double(n.start - t0) / double(t1 - t0) : 0;
        set(n, f, first + (last - first) * std::pow(x, power));
    });
}

edits::NoteList setAll(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double value) {
    return each(notes, ids, [&](Note& n) { set(n, f, value); });
}

namespace {

/// Reads "bar.beat.step" (a step is a 1/16; beat and step may be left off) as a start in beats from the clip's
/// start, like the ruler counts. False when the text isn't a position.
bool parsePosition(const std::string& text, TimeSig ts, double& beats) {
    long parts[3] = {1, 1, 1};
    size_t count = 0, at = 0;
    while (count < 3) {
        size_t dot = text.find('.', at);
        std::string part = text.substr(at, dot == std::string::npos ? std::string::npos : dot - at);
        if (part.empty() || part.size() > 6 ||
            !std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return false;
        parts[count++] = std::stol(part);
        if (dot == std::string::npos)
            break;
        at = dot + 1;
        if (count == 3)
            return false;
    }
    if (parts[0] < 1 || parts[1] < 1 || parts[2] < 1)
        return false;
    Tick tick = (parts[0] - 1) * ticksPerBar(ts) + (parts[1] - 1) * ticksPerBeat(ts) + (parts[2] - 1) * (kPpq / 4);
    beats = double(tick) / kBeat;
    return true;
}

} // namespace

bool applyTyped(const edits::NoteList& notes, const edits::IdSet& ids, Field f, const std::string& raw,
                edits::NoteList& out, TimeSig ts) {
    std::string text;
    for (char ch : raw)
        if (ch != ' ')
            text += ch;
    if (text.empty())
        return false;
    char op = '=';
    if (text[0] == '=' || text[0] == '*' || text[0] == '/' || ((text[0] == '+' || text[0] == '-') && text.size() > 1))
        op = text[0];
    std::string number = op == '=' && text[0] != '=' ? text : text.substr(1);
    if (f == Field::Start && op == '=') {
        double beats;
        if (!parsePosition(number, ts, beats))
            return false;
        out = setAll(notes, ids, f, beats);
        return true;
    }
    bool percent = !number.empty() && number.back() == '%';
    if (percent)
        number.pop_back();
    char* end = nullptr;
    double x = std::strtod(number.c_str(), &end);
    if (number.empty() || end != number.c_str() + number.size())
        return false;
    if ((f == Field::Start || f == Field::Length) && !percent && op != '*' && op != '/')
        x *= double(ticksPerBeat(ts)) / kBeat;
    switch (op) {
    case '+':
        out = each(notes, ids, [&](Note& n) { set(n, f, percent ? get(n, f) * (1 + x / 100) : get(n, f) + x); });
        return true;
    case '-':
        out = each(notes, ids, [&](Note& n) { set(n, f, percent ? get(n, f) * (1 - x / 100) : get(n, f) - x); });
        return true;
    case '*':
        out = each(notes, ids, [&](Note& n) { set(n, f, get(n, f) * x); });
        return true;
    case '/':
        if (x == 0)
            return false;
        out = each(notes, ids, [&](Note& n) { set(n, f, get(n, f) / x); });
        return true;
    default:
        out = setAll(notes, ids, f, x);
        return true;
    }
}

std::string format(Field f, double v, TimeSig ts) {
    char text[48];
    switch (f) {
    case Field::Pitch: {
        int p = int(std::lround(v));
        snprintf(text, sizeof(text), "%s%d", pitchClassName(p), p / 12 - 1);
        return text;
    }
    case Field::Repeats:
        if (v == 0)
            return "Off";
        snprintf(text, sizeof(text), v > 0 ? "%.0f" : "%.0f per beat", std::fabs(v));
        return text;
    case Field::Start: {
        BarBeatTick bbt = toBarBeatTick(std::max<Tick>(0, std::llround(v * kBeat)), ts);
        Tick step = kPpq / 4;
        snprintf(text, sizeof(text), "%lld.%d.%lld", (long long)bbt.bar, bbt.beat, (long long)(bbt.tick / step + 1));
        std::string position = text;
        // Off the 1/16 grid: the leftover ticks, as a fourth number.
        if (bbt.tick % step != 0)
            position += "." + std::to_string(bbt.tick % step);
        return position;
    }
    case Field::Length: {
        Tick ticks = std::llround(v * kBeat), bar = ticksPerBar(ts);
        std::string length;
        if (ticks >= bar)
            length = std::to_string(ticks / bar) + (ticks / bar == 1 ? " bar" : " bars");
        if (ticks % bar != 0 || ticks == 0) {
            snprintf(text, sizeof(text), "%.3g", double(ticks % bar) / double(ticksPerBeat(ts)));
            bool one = std::string(text) == "1";
            length += (length.empty() ? "" : " ") + std::string(text) + (one ? " beat" : " beats");
        }
        return length;
    }
    default:
        snprintf(text, sizeof(text), "%.0f%s", v, info(f).unit[0] == '%' ? "%" : "");
        return text;
    }
}

double defaultValue(Field f) { return get(Note(), f); }

const char* operatorName(Operator op) {
    static const char* const names[] = {"Chance", "Repeats", "Spread", "Only when…", "Every … passes"};
    return names[std::clamp(int(op), 0, int(Operator::Count) - 1)];
}

bool isOn(const Note& n, Operator op) {
    const NoteOperators& o = n.ops;
    const NoteOperators d;
    switch (op) {
    case Operator::Chance:
        return o.chance != d.chance;
    case Operator::Repeats:
        return o.repeats != 0;
    case Operator::Spread:
        return o.velocitySpread != d.velocitySpread || o.timingSpread != d.timingSpread;
    case Operator::Occurrence:
        return o.occurrence != Occurrence::Always;
    case Operator::Recurrence:
        return o.recurrenceLength > 1;
    default:
        return false;
    }
}

edits::NoteList setOperator(const edits::NoteList& notes, const edits::IdSet& ids, Operator op, bool on) {
    const NoteOperators d;
    return each(notes, ids, [&](Note& n) {
        NoteOperators& o = n.ops;
        if (on && isOn(n, op))
            return;
        switch (op) {
        case Operator::Chance:
            o.chance = on ? 0.5f : d.chance;
            break;
        case Operator::Repeats:
            o.repeats = on ? 2 : 0;
            o.repeatCurve = d.repeatCurve;
            o.repeatVelEnd = d.repeatVelEnd;
            o.repeatGate = d.repeatGate;
            break;
        case Operator::Spread:
            o.velocitySpread = on ? 0.2f : d.velocitySpread;
            o.timingSpread = on ? 0.1f : d.timingSpread;
            break;
        case Operator::Occurrence:
            o.occurrence = on ? Occurrence::First : Occurrence::Always;
            break;
        case Operator::Recurrence:
            o.recurrenceLength = on ? 2 : 1;
            o.recurrenceMask = on ? 0b01 : d.recurrenceMask;
            break;
        default:
            break;
        }
    });
}

edits::NoteList setRecurrencePass(const edits::NoteList& notes, const edits::IdSet& ids, int pass, bool on) {
    if (pass < 0 || pass >= 16)
        return notes;
    edits::NoteList out = notes;
    for (Note& n : out)
        if (ids.count(n.id)) {
            if (n.ops.recurrenceLength < pass + 1)
                n = setRecurrenceLength({n}, {n.id}, pass + 1).front();
            n.ops.recurrenceMask =
                uint16_t(on ? n.ops.recurrenceMask | (1u << pass) : n.ops.recurrenceMask & ~(1u << pass));
        }
    return out;
}

edits::NoteList setRecurrenceLength(const edits::NoteList& notes, const edits::IdSet& ids, int length) {
    if (length <= 0)
        return notes;
    length = std::clamp(length, 1, 16);
    return each(notes, ids, [&](Note& n) {
        int old = std::clamp<int>(n.ops.recurrenceLength, 1, 16);
        uint16_t mask = old > 1 ? n.ops.recurrenceMask : 0xFFFF;
        for (int p = old; p < length; p++)
            mask |= uint16_t(1u << p);
        // Passes past the end don't exist: keep them on so a later, longer cycle starts with them playing.
        for (int p = length; p < 16; p++)
            mask |= uint16_t(1u << p);
        n.ops.recurrenceLength = uint8_t(length);
        n.ops.recurrenceMask = length > 1 ? mask : 0xFFFF;
    });
}

} // namespace tessera::fields
