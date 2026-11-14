#include "DeviceTags.hpp"

#include "../../model/Chord.hpp"
#include "../../model/DeviceTypes.hpp"
#include "../../model/Devices.hpp"
#include "../../model/Playheads.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace tessera::tags {

namespace {

using F = Family;
namespace ph = playheads::knob;
namespace dk = chain::knob;

const char* const kMinus = "−";
const char* const kDirections[] = {"Forward", "Backward", "Ping-pong", "Freeze"};
const char* const kOrders[] = {"Outside in", "Inside out", "Odd then even", "Shuffle"};
const char* const kFlats[] = {"C", "D♭", "D", "E♭", "E", "F", "G♭", "G", "A♭", "A", "B♭", "B"};
const char* const kSharps[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kIntervals[] = {"the same note", "a semitone",     "a whole tone",    "a minor third",
                                  "a major third", "a fourth",       "a tritone",       "a fifth",
                                  "a minor sixth", "a major sixth",  "a minor seventh", "a major seventh"};
// The same fixed "random" numbers as the pictures, so a caption names what its picture draws.
constexpr float kRnd[] = {0.13f, 0.71f, 0.42f, 0.88f, 0.27f, 0.63f, 0.05f, 0.94f, 0.51f, 0.36f,
                          0.79f, 0.18f, 0.67f, 0.31f, 0.84f, 0.46f, 0.22f, 0.58f, 0.09f, 0.75f};
constexpr double kSixteenth = double(kPpq) / 4;

std::vector<TileSpec> buildTiles(DeviceType type) {
    switch (type) {
    case DeviceType::Playheads:
        return {{196, {{ph::kDirection, F::Direction}, {ph::kRate, F::Rate}}},
                {170, {{ph::kTimeOffset, F::Length}}},
                {196, {{ph::kRepeatSize, F::Length}, {ph::kRepeatTimes, F::Number, "×"}, {ph::kCurve, F::Curve}}}};
    case DeviceType::Transpose:
        return {{240, {{dk::kSemitones, F::Number}, {dk::kOctaves, F::Number}, {dk::kKeySteps, F::Number}}}};
    case DeviceType::Scale:
        return {{300,
                 {{dk::kKeyFrom, F::Plain, "Key"},
                  {dk::kRoot, F::Plain},
                  {dk::kScale, F::Plain},
                  {dk::kOffKey, F::Plain, "Off key"}}}};
    case DeviceType::Chord:
        return {{330,
                 {{dk::kChord, F::Plain},
                  {dk::kInversion, F::Number, "Inv"},
                  {dk::kVoicing, F::Plain},
                  {dk::kStrum, F::Number, "Strum"},
                  {dk::kInKey, F::Toggle}}}};
    case DeviceType::NoteLength:
        return {{220,
                 {{dk::kMode, F::Plain},
                  {dk::kScaleBy, F::Number, "", 5},
                  {dk::kLength, F::Length},
                  {dk::kOverlap, F::Number, "Overlap"}}}};
    case DeviceType::Arpeggiator:
        return {{250, {{dk::kPattern, F::Plain}, {dk::kArpOctaves, F::Number, "Oct"}, {dk::kHold, F::Toggle}}},
                {280,
                 {{dk::kRate, F::Length},
                  {dk::kGate, F::Number, "Gate", 5},
                  {dk::kSwing, F::Number, "Swing"},
                  {dk::kArpVelocity, F::Plain}}}};
    case DeviceType::NoteRepeat:
        return {{240,
                 {{dk::kRepeatRate, F::Length},
                  {dk::kRepeatGate, F::Number, "Gate", 5},
                  {dk::kRepeatDecay, F::Number, "Decay"}}}};
    case DeviceType::Echo:
        return {{340,
                 {{dk::kTime, F::Length},
                  {dk::kRepeats, F::Number, "×"},
                  {dk::kDecay, F::Number, "Decay"},
                  {dk::kEchoPitch, F::Number, "Pitch"},
                  {dk::kEchoGate, F::Number, "Gate", 5}}}};
    case DeviceType::Velocity:
        return {{300,
                 {{dk::kCurve, F::Number, "Curve"},
                  {dk::kMin, F::Number, "Min"},
                  {dk::kMax, F::Number, "Max"},
                  {dk::kRandom, F::Number, "Random"}}}};
    case DeviceType::Humanize:
        return {{280,
                 {{dk::kHumanizeTiming, F::Number, "Timing"},
                  {dk::kHumanizeVelocity, F::Number, "Velocity"},
                  {dk::kHumanizeLength, F::Number, "Length"}}}};
    case DeviceType::Random:
        return {{260,
                 {{dk::kRandomPitch, F::Number, "Pitch"}, {dk::kRandomInKey, F::Toggle}, {dk::kDrop, F::Number, "Drop"}}}};
    case DeviceType::Groove:
        return {{210, {{dk::kGrooveSwing, F::Number, "Swing"}, {dk::kGrooveGrid, F::Plain, "Grid"}}}};
    case DeviceType::NoteFilter:
        return {{310,
                 {{dk::kLowest, F::Number, "From"},
                  {dk::kHighest, F::Number, "To"},
                  {dk::kMinVelocity, F::Number, "Vel ≥"},
                  {dk::kMaxVelocity, F::Number, "Vel ≤"}}},
                {230, {{dk::kEvery, F::Number, "Every"}, {dk::kOffset, F::Number, "From"}, {dk::kChannel, F::Channels, "Ch"}}}};
    case DeviceType::Motion:
        return {{260,
                 {{dk::kMotionPitch, F::Number, "Pitch"},
                  {dk::kMotionTime, F::Number, "Time"},
                  {dk::kMotionInKey, F::Toggle}}},
                {230, {{dk::kMotionLength, F::Number, "Length", 10}, {dk::kMotionVelocity, F::Number, "Velocity"}}}};
    }
    return {};
}

const devices::ParamInfo* infoOf(DeviceType type, int param) {
    return type == DeviceType::Playheads ? devices::playheadParamInfo(param) : devices::paramInfo(type, param);
}

std::string lower(std::string s) {
    for (char& ch : s)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

/// Replaces every `from` in `s` with `to`.
std::string replaced(std::string s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
    return s;
}

bool endsWith(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

/// "12", "-3", "2.5" as a number, or nullopt.
std::optional<double> number(const std::string& s) {
    if (s.empty())
        return std::nullopt;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size())
        return std::nullopt;
    return v;
}

/// "3/16" or "1.5" as a number, or nullopt.
std::optional<double> ratio(const std::string& s) {
    size_t slash = s.find('/');
    if (slash == std::string::npos)
        return number(s);
    auto a = number(s.substr(0, slash)), b = number(s.substr(slash + 1));
    if (!a || !b || *b == 0)
        return std::nullopt;
    return *a / *b;
}

/// A label's value as a number: "1/16 T" in wholes, "3/2x", "50%", "+3 st", "−1/8"; nullopt for names.
std::optional<double> labelValue(std::string s) {
    s = replaced(s, kMinus, "-");
    double sign = 1;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
        sign = s[0] == '-' ? -1 : 1;
        s = s.substr(1);
    }
    double mark = 1;
    if (endsWith(s, " T") || endsWith(s, " D")) {
        mark = s.back() == 'T' ? 2.0 / 3 : 1.5;
        s = s.substr(0, s.size() - 2);
    }
    for (const char* unit : {"x", "%", " st", " oct", " steps", " bars", " bar"})
        if (endsWith(s, unit)) {
            s = s.substr(0, s.size() - std::string(unit).size());
            break;
        }
    auto v = ratio(s);
    if (!v)
        return std::nullopt;
    return sign * *v * mark;
}

/// What was typed, as a number in the same units as labelValue: "1/8t", "-3/16", "1.5", "2 bars".
std::optional<double> queryValue(std::string s) {
    s = replaced(lower(s), " ", "");
    s = replaced(replaced(s, "×", "x"), kMinus, "-");
    double sign = 1;
    if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
        sign = s[0] == '-' ? -1 : 1;
        s = s.substr(1);
    }
    for (const char* unit : {"steps", "oct", "st", "x", "%"})
        if (endsWith(s, unit)) {
            s = s.substr(0, s.size() - std::string(unit).size());
            break;
        }
    double mark = 1;
    for (const char* m : {"triplet", "trip", "dotted", "dot", "t", "d", "."}) {
        std::string ms = m;
        if (endsWith(s, ms) && s.size() > ms.size() && std::isdigit(static_cast<unsigned char>(s[s.size() - ms.size() - 1]))) {
            mark = ms[0] == 't' ? 2.0 / 3 : 1.5;
            s = s.substr(0, s.size() - ms.size());
            break;
        }
    }
    for (const char* bar : {"bars", "bar", "b"})
        if (endsWith(s, bar)) {
            s = s.substr(0, s.size() - std::string(bar).size());
            break;
        }
    auto v = ratio(s);
    if (!v)
        return std::nullopt;
    return sign * *v * mark;
}

std::string capitalized(std::string s) {
    if (!s.empty())
        s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

/// A distance in semitones, in words: "a fifth", "an octave and a major third".
std::string span(int t) {
    int a = std::abs(t), o = a / 12, r = a % 12;
    std::string oct = o == 1 ? "an octave" : o == 2 ? "two octaves" : std::to_string(o) + " octaves";
    return o == 0 ? kIntervals[r] : r == 0 ? oct : oct + " and " + kIntervals[r];
}

/// A length in sixteenths as a note value: "1/8", "3/16", "1 bar", "2 bars".
std::string lengthText(double sixteenths) {
    long n = std::lround(sixteenths * 12);
    if (n <= 0)
        return "0";
    if (n % 192 == 0)
        return std::to_string(n / 192) + (n == 192 ? " bar" : " bars");
    long k = std::gcd(n, 192L);
    return std::to_string(n / k) + "/" + std::to_string(192 / k);
}

std::string number0(double v) {
    char text[32];
    snprintf(text, sizeof(text), "%g", std::round(v * 10) / 10);
    return text;
}

} // namespace

int arpStep(int pattern, int n, int k) {
    using P = chain::ArpPattern;
    auto tri = [&](int i, bool full) {
        int per = full ? 2 * n : std::max(1, 2 * n - 2), j = i % per;
        return j < n ? j : (full ? 2 * n - 1 - j : 2 * n - 2 - j);
    };
    std::vector<int> conv;
    for (int a = 0, b = n - 1; a <= b; a++, b--) {
        conv.push_back(a);
        if (a != b)
            conv.push_back(b);
    }
    switch (P(pattern)) {
    case P::Down:
        return n - 1 - k % n;
    case P::UpDown:
        return tri(k, false);
    case P::UpDownFull:
        return tri(k, true);
    case P::DownUp:
        return n - 1 - tri(k, false);
    case P::DownUpFull:
        return n - 1 - tri(k, true);
    case P::Converge:
        return conv[size_t(k % n)];
    case P::Diverge:
        return conv[size_t(n - 1 - k % n)];
    case P::AsPlayed: {
        static const int kPlayed[] = {1, 0, 2, 1, 0, 2};
        return kPlayed[k % 6] % n;
    }
    case P::Random:
        return std::min(n - 1, int(kRnd[(k * 7) % 20] * float(n)));
    case P::Walk: {
        int p = 0;
        for (int i = 0; i < k; i++) {
            float r = kRnd[i % 20];
            p = (p + (r < 0.5f ? 1 : r < 0.75f ? 0 : n - 1)) % n;
        }
        return p;
    }
    case P::Shuffled: {
        std::vector<int> order(static_cast<size_t>(n));
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [](int u, int w) { return kRnd[u % 20] < kRnd[w % 20]; });
        return order[size_t(k % n)];
    }
    case P::Pattern:
        return kRnd[k % 16] < 0.4f ? 0 : std::min(n - 1, int(kRnd[(k + 5) % 20] * float(n)));
    default:
        return k % n;
    }
}

namespace {

const std::string& choiceName(DeviceType type, int param, int index) {
    static const std::string none;
    const devices::ParamInfo* info = infoOf(type, param);
    if (!info || info->choices.empty())
        return none;
    return info->choices[size_t(std::clamp(index, 0, int(info->choices.size()) - 1))];
}

uint16_t majorMask() {
    for (const ScaleDef& d : builtinScales())
        if (std::string(d.id) == "major")
            return d.mask;
    return kChromaticMask;
}

} // namespace

std::vector<int> chordOnC(const Values& v) {
    const std::vector<ChordShapeInfo>& shapes = chordShapes();
    ChordStamp stamp;
    stamp.shape = shapes[size_t(std::clamp(v.i(dk::kChord), 0, int(shapes.size()) - 1))].shape;
    stamp.inversion = v.i(dk::kInversion);
    stamp.voicing = ChordVoicing(std::clamp(v.i(dk::kVoicing), 0, 2));
    // The same call the Chord device makes, in C major.
    std::vector<int> pitches = chordPitches(60, stamp, Scale(0, majorMask()), v.i(dk::kInKey) != 0);
    return pitches;
}

int Values::i(int k) const { return int(std::lround((*this)[k])); }

const std::vector<TileSpec>& tilesOf(DeviceType type) {
    static const std::vector<std::vector<TileSpec>> all = [] {
        std::vector<std::vector<TileSpec>> out;
        for (int t = 0; t < kDeviceTypeCount; t++)
            out.push_back(buildTiles(DeviceType(t)));
        return out;
    }();
    return all[size_t(std::clamp(int(type), 0, kDeviceTypeCount - 1))];
}

int ValueList::nearest(float value) const {
    int best = 0;
    float distance = INFINITY;
    for (int k = 0; k < size(); k++) {
        float d = std::fabs(values[size_t(k)] - value);
        if (d < distance) {
            distance = d;
            best = k;
        }
    }
    return best;
}

double sixteenthsOf(DeviceType type, int param, float value) {
    int index = int(std::lround(value));
    TimeSig ts;
    if (type == DeviceType::Playheads) {
        if (param == ph::kRepeatSize)
            return playheads::repeatTicks(index, ts) / kSixteenth;
        if (param == ph::kTimeOffset)
            return playheads::offsetTicks(index, ts) / kSixteenth;
        if (param == ph::kOrderStep)
            return playheads::orderStepTicks(index, ts) / kSixteenth;
        if (param == ph::kFixedLength)
            return playheads::fixedLengthTicks(index, ts) / kSixteenth;
        return 0;
    }
    return double(devices::rateTicks(index, ts)) / kSixteenth;
}

double rateOf(float value) { return playheads::playheadRate(int(std::lround(value))); }

ValueList directionList() {
    ValueList list;
    list.family = F::Direction;
    for (int k = 0; k < kDirectionValues; k++) {
        list.values.push_back(float(k));
        list.labels.push_back(k < 4 ? kDirections[k] : kOrders[k - 4]);
    }
    return list;
}

ValueList valueList(DeviceType type, const TagSpec& spec) {
    if (spec.family == F::Direction)
        return directionList();
    ValueList list;
    list.family = spec.family;
    const devices::ParamInfo* info = infoOf(type, spec.param);
    if (!info)
        return list;
    using K = devices::ParamInfo::Kind;
    if (info->kind == K::Toggle) {
        list.values = {0, 1};
        list.labels = {"Off", "On"};
        list.def = info->def >= 0.5f ? 1 : 0;
        return list;
    }
    if (info->kind == K::Choice) {
        std::vector<int> order(info->choices.size());
        std::iota(order.begin(), order.end(), 0);
        // A device's lengths are stored long first, then triplets and dotted ones; dragging goes short to long.
        if (spec.family == F::Length && type != DeviceType::Playheads)
            std::stable_sort(order.begin(), order.end(),
                             [](int a, int b) { return devices::rateTicks(a, {}) < devices::rateTicks(b, {}); });
        for (int k : order) {
            list.values.push_back(float(k));
            list.labels.push_back(replaced(info->choices[size_t(k)], "-", kMinus));
            if (k == int(std::lround(info->def)))
                list.def = list.size() - 1;
            if (info->choices[size_t(k)] == "0")
                list.zero = list.size() - 1;
        }
        // Ping-pong keeps its hyphen.
        if (spec.family == F::Plain || spec.family == F::Curve)
            for (int k = 0; k < list.size(); k++)
                list.labels[size_t(k)] = info->choices[size_t(order[size_t(k)])];
        return list;
    }
    // Numbers, in steps of `step` display units.
    bool pitches = type == DeviceType::NoteFilter && (spec.param == dk::kLowest || spec.param == dk::kHighest);
    std::string unit = info->unit;
    if (unit.rfind("%", 0) == 0)
        unit = "%";
    float step = spec.step > 0 ? spec.step : 1;
    float first = std::ceil(info->min / step - 1e-4f) * step;
    // Counted, not stepped until past the end: the plugin builds with unsafe float maths.
    int count = int(std::floor((info->max - first) / step + 1e-4f)) + 1;
    for (int k = 0; k < std::clamp(count, 1, 2000); k++) {
        float value = std::round((first + float(k) * step) * 1000) / 1000;
        list.values.push_back(value);
        std::string text;
        if (pitches) {
            int p = int(std::lround(value));
            text = std::string(pitchClassName(p % 12)) + std::to_string(p / 12 - 1);
        } else {
            char digits[32];
            snprintf(digits, sizeof(digits), "%g", double(std::fabs(value)));
            text = (value < 0 ? std::string(kMinus) : info->min < 0 && value > 0 ? "+" : "") + digits;
            if (!unit.empty())
                text += unit == "%" ? unit : " " + unit;
        }
        list.labels.push_back(text);
    }
    list.def = list.nearest(info->def);
    return list;
}

ValueText splitLabel(const std::string& label) {
    ValueText out;
    std::string s = label;
    if (s.rfind(kMinus, 0) == 0) {
        out.sign = kMinus;
        s = s.substr(std::string(kMinus).size());
    } else if (!s.empty() && s[0] == '+') {
        out.sign = "+";
        s = s.substr(1);
    }
    if (endsWith(s, " T") || endsWith(s, " D")) {
        out.mark = s.substr(s.size() - 1);
        s = s.substr(0, s.size() - 2);
    }
    for (const char* unit : {"x", "%", " st", " oct", " steps", " bars", " bar"})
        if (endsWith(s, unit) && s.size() > std::string(unit).size() &&
            std::isdigit(static_cast<unsigned char>(s[s.size() - std::string(unit).size() - 1]))) {
            std::string u = trim(unit);
            out.unit = u == "x" ? "×" : u;
            s = s.substr(0, s.size() - std::string(unit).size());
            break;
        }
    size_t slash = s.find('/');
    if (slash != std::string::npos && slash > 0 && slash + 1 < s.size() &&
        std::all_of(s.begin(), s.end(), [](char ch) { return ch == '/' || std::isdigit(static_cast<unsigned char>(ch)); })) {
        out.num = s.substr(0, slash);
        out.den = s.substr(slash + 1);
    } else {
        out.text = s;
    }
    // A name that only looked like a number with a unit ("1 bar" keeps its word).
    if (out.unit == "bar" || out.unit == "bars") {
        out.text += " " + out.unit;
        out.unit.clear();
    }
    return out;
}

bool dimmed(DeviceType type, int param, const Values& v) {
    if (type == DeviceType::Playheads)
        return (param == ph::kRepeatTimes || param == ph::kCurve) && v.i(ph::kRepeatSize) == 0;
    if (type == DeviceType::Scale)
        return (param == dk::kRoot || param == dk::kScale) && v.i(dk::kKeyFrom) != int(chain::KeyFrom::Fixed);
    return false;
}

bool hidden(DeviceType type, int param, const Values& v) {
    if (type != DeviceType::NoteLength)
        return false;
    auto mode = chain::LengthMode(v.i(dk::kMode));
    return (param == dk::kScaleBy && mode != chain::LengthMode::Scale) ||
           (param == dk::kLength && mode != chain::LengthMode::Fixed) ||
           (param == dk::kOverlap && mode != chain::LengthMode::Legato);
}

std::optional<Match> match(const ValueList& list, const std::string& query) {
    std::string q = trim(query);
    if (q.empty() || list.values.empty())
        return std::nullopt;
    std::string ql = lower(q);
    for (int k = 0; k < list.size(); k++)
        if (lower(list.labels[size_t(k)]) == ql)
            return Match{k, true};
    if (list.family == F::Direction) {
        static const std::pair<const char*, int> kAliases[] = {
            {"reverse", 1}, {"back", 1},    {"rev", 1},    {"bounce", 2}, {"pingpong", 2}, {"ping pong", 2},
            {"hold", 3},    {"stop", 3},    {"still", 3},  {"fwd", 0},    {"random", 7},   {"shuffled", 7}};
        for (const auto& [word, index] : kAliases)
            if (ql == word)
                return Match{index, true};
    }
    if (auto typed = queryValue(q)) {
        bool linear = list.zero >= 0 || list.family == F::Number;
        int best = -1;
        double distance = 1e9;
        for (int k = 0; k < list.size(); k++) {
            auto lv = labelValue(list.labels[size_t(k)]);
            if (!lv)
                continue;
            double d = linear ? std::fabs(*lv - *typed)
                       : *lv > 0 && *typed > 0 ? std::fabs(std::log(*lv / *typed))
                                               : 1e8;
            if (d < distance) {
                distance = d;
                best = k;
            }
        }
        if (best >= 0)
            return Match{best, distance < 1e-6};
    }
    if (ql == "none")
        ql = "off";
    for (int k = 0; k < list.size(); k++)
        if (lower(list.labels[size_t(k)]).rfind(ql, 0) == 0)
            return Match{k, true};
    std::vector<std::string> words;
    for (size_t a = 0; a < ql.size();) {
        size_t b = ql.find(' ', a);
        if (b == std::string::npos)
            b = ql.size();
        if (b > a)
            words.push_back(ql.substr(a, b - a));
        a = b + 1;
    }
    for (int k = 0; k < list.size(); k++) {
        std::string label = lower(list.labels[size_t(k)]);
        if (std::all_of(words.begin(), words.end(), [&](const std::string& w) { return label.find(w) != std::string::npos; }))
            return Match{k, true};
    }
    return std::nullopt;
}

std::string channelsText(uint16_t mask) {
    if (mask == 0)
        return "Any";
    if (mask == 0xFFFF)
        return "All";
    std::string out;
    for (int a = 0; a < 16;) {
        if (!(mask & (1u << a))) {
            a++;
            continue;
        }
        int b = a;
        while (b + 1 < 16 && (mask & (1u << (b + 1))))
            b++;
        if (!out.empty())
            out += ", ";
        if (b > a + 1)
            out += std::to_string(a + 1) + "–" + std::to_string(b + 1);
        else if (b == a + 1)
            out += std::to_string(a + 1) + ", " + std::to_string(b + 1);
        else
            out += std::to_string(a + 1);
        a = b + 1;
    }
    return out;
}

std::optional<uint16_t> parseChannels(const std::string& text) {
    std::string q = lower(trim(text));
    q = replaced(q, "–", "-");
    if (q.empty())
        return std::nullopt;
    if (q == "any")
        return uint16_t(0);
    if (q == "all")
        return uint16_t(0xFFFF);
    uint16_t mask = 0;
    for (char& ch : q)
        if (ch == ',')
            ch = ' ';
    size_t a = 0;
    while (a < q.size()) {
        size_t b = q.find(' ', a);
        if (b == std::string::npos)
            b = q.size();
        std::string part = q.substr(a, b - a);
        a = b + 1;
        if (part.empty())
            continue;
        size_t dash = part.find('-');
        auto lo = number(dash == std::string::npos ? part : part.substr(0, dash));
        auto hi = dash == std::string::npos ? lo : number(part.substr(dash + 1));
        if (!lo || !hi || *lo != std::floor(*lo) || *hi != std::floor(*hi) || *lo < 1 || *hi > 16 || *lo > *hi)
            return std::nullopt;
        for (int c = int(*lo); c <= int(*hi); c++)
            mask = uint16_t(mask | (1u << (c - 1)));
    }
    return mask;
}

std::string caption(DeviceType type, int tile, const Values& v) {
    switch (type) {
    case DeviceType::Playheads: {
        if (tile == 0) {
            int d = v.i(ph::kDirection), order = v.i(ph::kOrder);
            if (order == 0 && d == int(playheads::Direction::Freeze))
                return "Holds one moment";
            const std::string& rate = choiceName(type, ph::kRate, v.i(ph::kRate));
            std::string r = rate.substr(0, rate.size() - 1); // without its "x"
            size_t slash = r.find('/');
            int a = std::atoi(r.c_str()), b = slash == std::string::npos ? 1 : std::atoi(r.c_str() + slash + 1);
            std::string s = a == b   ? "One pass a bar"
                            : b == 1 ? std::to_string(a) + " passes a bar"
                            : a == 1 ? "A pass takes " + std::to_string(b) + " bars"
                                     : std::to_string(a) + " passes every " + std::to_string(b) + " bars";
            if (order > 0)
                s += ", " + lower(kOrders[std::clamp(order - 1, 0, 3)]);
            else if (d == int(playheads::Direction::PingPong))
                s += ", turning at each end";
            return s;
        }
        if (tile == 1) {
            const std::string& o = choiceName(type, ph::kTimeOffset, v.i(ph::kTimeOffset));
            if (o == "0" || o.empty())
                return "Lands with the others";
            return o[0] == '+' ? "A canon, " + o.substr(1) + " behind" : "Leads the others by " + o.substr(1);
        }
        if (v.i(ph::kRepeatSize) == 0)
            return "Plays straight through";
        return "Fills " + lengthText(sixteenthsOf(type, ph::kRepeatSize, v[ph::kRepeatSize]) * v[ph::kRepeatTimes]) +
               ", then moves on";
    }
    case DeviceType::Transpose: {
        int t = v.i(dk::kSemitones) + 12 * v.i(dk::kOctaves) + int(std::lround(v[dk::kKeySteps] * 12 / 7));
        return t == 0 ? "Stays put" : capitalized(span(t)) + (t > 0 ? " up" : " down");
    }
    case DeviceType::Scale: {
        bool fixed = v.i(dk::kKeyFrom) == int(chain::KeyFrom::Fixed);
        const std::vector<ScaleDef>& scales = builtinScales();
        int root = fixed ? v.i(dk::kRoot) : v.keyRoot;
        uint16_t mask = fixed ? scales[size_t(std::clamp(v.i(dk::kScale), 0, int(scales.size()) - 1))].mask : v.keyMask;
        if (mask == kChromaticMask)
            return fixed ? "Every note fits" : "No key set yet: every note fits";
        std::string s;
        for (int i = 0; i < 12; i++)
            if (mask & (1u << i)) {
                if (!s.empty())
                    s += " ";
                s += i == 0 ? kSharps[root % 12] : kFlats[(root + i) % 12];
            }
        return s;
    }
    case DeviceType::Chord: {
        std::string s = "On C:";
        for (int p : chordOnC(v))
            s += std::string(" ") + kFlats[((p % 12) + 12) % 12];
        return s;
    }
    case DeviceType::NoteLength: {
        auto mode = chain::LengthMode(v.i(dk::kMode));
        if (mode == chain::LengthMode::Fixed)
            return "All " + choiceName(type, dk::kLength, v.i(dk::kLength)) + ", whatever was written";
        if (mode == chain::LengthMode::Legato)
            return v[dk::kOverlap] > 0.5f ? "Notes overlap, so a mono synth glides" : "Every gap closed";
        float s = v[dk::kScaleBy];
        return s < 50 ? "Staccato" : s < 90 ? "Detached" : s <= 110 ? "As written" : s < 200 ? "Held over" : "Smeared into the next";
    }
    case DeviceType::Arpeggiator: {
        if (tile == 0) {
            if (v.i(dk::kPattern) == int(chain::ArpPattern::Chord))
                return "C E G, all together";
            std::vector<std::string> pool;
            for (int o = 0; o < std::max(1, v.i(dk::kArpOctaves)); o++)
                for (const char* n : {"C", "E", "G"})
                    pool.push_back(n + std::string(size_t(o), '\''));
            std::string s = "On C E G:";
            for (int k = 0; k < 8; k++)
                s += " " + pool[size_t(arpStep(v.i(dk::kPattern), int(pool.size()), k))];
            return s + " …";
        }
        double n = 16 / std::max(0.25, sixteenthsOf(type, dk::kRate, v[dk::kRate]));
        std::string s = n >= 1 ? number0(n) + " steps a bar" : "A step every " + std::to_string(std::lround(1 / n)) + " bars";
        if (v[dk::kGate] > 100)
            s += ", tied over";
        else if (v[dk::kGate] < 35)
            s += ", clipped short";
        if (v[dk::kSwing] > 0.5f)
            s += ", swung";
        if (v.i(dk::kArpVelocity) == int(chain::ArpVelocity::Accent))
            s += ", accent on the one";
        return s;
    }
    case DeviceType::NoteRepeat: {
        double n = 16 / std::max(0.25, sixteenthsOf(type, dk::kRepeatRate, v[dk::kRepeatRate]));
        return (n >= 1 ? number0(n) + " hits a bar" : "A hit every " + std::to_string(std::lround(1 / n)) + " bars") +
               (v[dk::kRepeatDecay] > 0.5f ? ", fading" : "");
    }
    case DeviceType::Echo: {
        int p = v.i(dk::kEchoPitch);
        std::string s = "Rings on for " + lengthText(v[dk::kRepeats] * sixteenthsOf(type, dk::kTime, v[dk::kTime]));
        if (p)
            s += std::string(", ") + (p > 0 ? "climbing " : "falling ") + span(p) + " each time";
        return s;
    }
    case DeviceType::Velocity: {
        float c = v[dk::kCurve];
        std::string s = c > 0.5f ? "Quiet notes come forward" : c < -0.5f ? "Loud notes stand out more" : "Same feel, new range";
        return s + (v[dk::kRandom] > 0.5f ? ", never twice the same" : "");
    }
    case DeviceType::Humanize:
        return v[dk::kHumanizeTiming] > 0.5f
                   ? "Never more than " + lengthText(v[dk::kHumanizeTiming] / 100) + " late"
                   : "Right on the grid";
    case DeviceType::Random: {
        float d = v[dk::kDrop];
        return d < 0.5f ? "Never skips a note" : d >= 99.5f ? "Skips every note"
                                                           : "Skips about 1 note in " + std::to_string(std::lround(100 / d));
    }
    case DeviceType::Groove: {
        float s = v[dk::kGrooveSwing];
        return s <= 51 ? "Straight" : s < 60 ? "A light lean" : s < 65 ? "Lazy" : s < 70 ? "Triplet feel" : "Dotted, hard swing";
    }
    case DeviceType::NoteFilter: {
        if (tile == 0) {
            int w = v.i(dk::kHighest) - v.i(dk::kLowest);
            return w < 0 ? "Lets nothing through" : w >= 127 ? "Every pitch gets through" : capitalized(span(w)) + " wide";
        }
        int e = std::max(1, v.i(dk::kEvery));
        int picked = 0;
        for (int c = 0; c < 16; c++)
            picked += (v.channels >> c) & 1;
        if (e == 1)
            return v.channels ? "Only " + std::to_string(picked) + " of the 16 channels" : "Nothing skipped";
        return "Keeps " + std::string(e == 2   ? "half the notes"
                                      : e == 3 ? "a third of the notes"
                                      : e == 4 ? "a quarter of the notes"
                                               : "1 note in " + std::to_string(e));
    }
    case DeviceType::Motion: {
        if (tile == 0) {
            int p = std::abs(v.i(dk::kMotionPitch)), t = v.i(dk::kMotionTime);
            std::string s;
            if (p)
                s = "within " + span(int(std::lround(p * 12.0 / 7)));
            if (t)
                s += (s.empty() ? "" : ", ") + std::string("up to ") + lengthText(t) + " late";
            return s.empty() ? "Stays put" : "Wanders " + s;
        }
        float l = v[dk::kMotionLength], vel = v[dk::kMotionVelocity];
        std::string s;
        if (std::fabs(l) > 0.5f)
            s = l > 0 ? "longer" : "shorter";
        if (std::fabs(vel) > 0.5f)
            s += (s.empty() ? "" : " and ") + std::string(vel > 0 ? "harder" : "softer");
        return s.empty() ? "Length and strength stay" : "Each pass a little " + s;
    }
    }
    return "";
}

} // namespace tessera::tags
