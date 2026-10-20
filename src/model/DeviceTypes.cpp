#include "DeviceTypes.hpp"

#include "Chord.hpp"
#include "Scale.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tessera::devices {

namespace {

using K = ParamInfo::Kind;

ParamInfo number(const char* name, float min, float max, float def, const char* unit) {
    return {name, K::Number, min, max, def, unit, {}};
}
ParamInfo integer(const char* name, float min, float max, float def, const char* unit = "") {
    return {name, K::Integer, min, max, def, unit, {}};
}
ParamInfo exponential(const char* name, float min, float max, float def, const char* unit) {
    return {name, K::Exponential, min, max, def, unit, {}};
}
ParamInfo choice(const char* name, std::vector<std::string> choices, int def) {
    return {name, K::Choice, 0, float(choices.size() - 1), float(def), "", std::move(choices)};
}
ParamInfo toggle(const char* name, bool def) { return {name, K::Toggle, 0, 1, def ? 1.f : 0.f, "", {}}; }

const std::vector<std::string> kRateNames = {"8 bars", "4 bars", "2 bars", "1 bar", "1/2",   "1/4",
                                             "1/8",    "1/16",   "1/32",   "1/64",  "1/2 T", "1/4 T",
                                             "1/8 T",  "1/16 T", "1/4 D",  "1/8 D", "1/16 D"};
constexpr int kRateQuarter = 5, kRateEighth = 6, kRateSixteenth = 7, kRateBar = 3;

std::vector<std::string> rates() { return kRateNames; }

std::vector<std::string> noteNames() { return {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}; }

std::vector<std::string> scaleNames() {
    std::vector<std::string> out;
    for (const ScaleDef& s : builtinScales())
        out.push_back(s.name);
    return out;
}

std::vector<std::string> chordNames() {
    std::vector<std::string> out;
    for (const ChordShapeInfo& c : chordShapes())
        out.push_back(c.name);
    return out;
}

std::vector<TypeInfo> buildDevices() {
    std::vector<TypeInfo> t(kDeviceTypeCount);
    t[size_t(DeviceType::Transpose)] = {"Transpose",
                                        {integer("Semitones", -48, 48, 0, "st"), integer("Octaves", -4, 4, 0, "oct"),
                                         integer("Key steps", -14, 14, 0, "steps")}};
    t[size_t(DeviceType::Scale)] = {"Scale",
                                    {choice("Key from", {"Clip", "Song", "Fixed"}, 0), choice("Root", noteNames(), 0),
                                     choice("Scale", scaleNames(), 0),
                                     choice("Notes off key", {"Nearest", "Up", "Down", "Drop"}, 0)}};
    t[size_t(DeviceType::Velocity)] = {"Velocity",
                                       {number("Curve", -100, 100, 0, "%"), number("Min", 0, 100, 0, "%"),
                                        number("Max", 0, 100, 100, "%"), number("Random", 0, 100, 0, "%")}};
    t[size_t(DeviceType::NoteLength)] = {
        "Note Length",
        {choice("Mode", {"Scale", "Fixed", "Legato"}, 0), exponential("Scale", 5, 400, 100, "%"),
         choice("Length", rates(), kRateSixteenth), number("Overlap", 0, 100, 0, "% of 1/16")}};
    t[size_t(DeviceType::Chord)] = {"Chord",
                                    {choice("Chord", chordNames(), 0), toggle("In key", true),
                                     integer("Inversion", -3, 3, 0), choice("Voicing", {"Close", "Open", "Drop 2"}, 0),
                                     number("Strum", 0, 100, 0, "% of 1/16")}};
    t[size_t(DeviceType::Arpeggiator)] = {
        "Arpeggiator",
        {choice("Pattern", {"Up", "Down", "Up-down", "Down-up", "Random", "As played", "Chord"}, 0),
         choice("Rate", rates(), kRateSixteenth), number("Gate", 5, 200, 80, "%"), integer("Octaves", 1, 4, 1),
         number("Swing", 0, 100, 0, "%"), choice("Velocity", {"As played", "Fixed", "Accent"}, 0),
         toggle("Hold", false)}};
    t[size_t(DeviceType::Echo)] = {"Echo",
                                   {choice("Time", rates(), kRateEighth), integer("Repeats", 1, 16, 3),
                                    number("Decay", 0, 100, 30, "%"), integer("Pitch", -12, 12, 0, "st"),
                                    number("Gate", 5, 100, 50, "%")}};
    t[size_t(DeviceType::Random)] = {
        "Random", {integer("Pitch", 0, 12, 0, "steps"), toggle("In key", true), number("Drop", 0, 100, 0, "%")}};
    t[size_t(DeviceType::Humanize)] = {"Humanize",
                                       {number("Timing", 0, 50, 0, "% of 1/16"), number("Velocity", 0, 100, 0, "%"),
                                        number("Length", 0, 100, 0, "%")}};
    t[size_t(DeviceType::NoteFilter)] = {
        "Note Filter",
        {integer("Lowest", 0, 127, 0), integer("Highest", 0, 127, 127), number("Min velocity", 0, 100, 0, "%"),
         number("Max velocity", 0, 100, 100, "%"), integer("Every", 1, 16, 1), integer("Offset", 0, 15, 0),
         choice("Channel",
                {"Any", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"}, 0)}};
    auto about = [&t](DeviceType type, const char* group, const char* description, const char* keywords) {
        TypeInfo& info = t[size_t(type)];
        info.group = group;
        info.description = description;
        info.keywords = keywords;
    };
    about(DeviceType::Transpose, "Pitch", "moves notes up or down, also within the key",
          "shift octave pitch key semitone");
    about(DeviceType::Scale, "Pitch", "keeps every note in a key", "key quantize mode snap");
    about(DeviceType::Chord, "Pitch", "turns each note into a chord", "harmony triad stack voicing");
    about(DeviceType::Arpeggiator, "Rhythm", "plays held notes one at a time", "arp pattern sequence");
    about(DeviceType::Echo, "Rhythm", "repeats notes, fading out", "delay repeat feedback");
    about(DeviceType::NoteLength, "Rhythm", "shorter, longer or legato notes", "gate legato duration staccato");
    about(DeviceType::Velocity, "Feel", "softer, harder, or within a range", "dynamics accent compress loudness");
    about(DeviceType::Humanize, "Feel", "loosens timing, velocity and length a little on every pass",
          "humanize groove loose feel timing velocity length");
    about(DeviceType::Random, "Feel", "changes pitches and drops notes on every pass", "variation chance random drop");
    about(DeviceType::NoteFilter, "Feel", "lets only some notes through", "filter range every skip thin channel part");
    return t;
}

std::vector<TypeInfo> buildModulators() {
    std::vector<TypeInfo> t(kModulatorTypeCount);
    t[size_t(ModulatorType::Lfo)] = {
        "LFO",
        {choice("Shape", {"Sine", "Triangle", "Saw up", "Saw down", "Square", "Sample & hold", "Smooth random"}, 0),
         toggle("Sync", true), exponential("Rate", 0.01f, 50, 1, "Hz"), choice("Synced rate", rates(), kRateBar),
         number("Phase", 0, 360, 0, "°"), toggle("Bipolar", true),
         choice("Restart", {"Free", "Each bar", "Each note"}, 0)}};
    t[size_t(ModulatorType::Envelope)] = {
        "Envelope",
        {exponential("Attack", 1, 10000, 5, "ms"), exponential("Decay", 1, 10000, 200, "ms"),
         number("Sustain", 0, 100, 60, "%"), exponential("Release", 1, 10000, 300, "ms")}};
    t[size_t(ModulatorType::Steps)] = {"Steps",
                                       {integer("Steps", 1, kMaxSteps, 8), choice("Rate", rates(), kRateSixteenth),
                                        number("Smooth", 0, 100, 0, "%"), toggle("Bipolar", false)}};
    t[size_t(ModulatorType::Random)] = {
        "Random", {choice("Rate", rates(), kRateQuarter), number("Smooth", 0, 100, 0, "%"), toggle("Bipolar", false)}};
    t[size_t(ModulatorType::Ramp)] = {"Ramp",
                                      {integer("Length", 1, 64, 8, "bars"), choice("Direction", {"Rise", "Fall"}, 0),
                                       choice("Then", {"Hold", "Loop"}, 0), number("Curve", -100, 100, 0, "%")}};
    t[size_t(ModulatorType::Macro)] = {"Macro", {number("Value", 0, 100, 0, "%")}};
    t[size_t(ModulatorType::Note)] = {"Note",
                                      {choice("Follows", {"Velocity", "Pitch", "Release", "Timbre", "Pressure"}, 0),
                                       exponential("Smooth", 1, 2000, 1, "ms")}};
    auto about = [&t](ModulatorType type, const char* group, const char* description, const char* keywords) {
        TypeInfo& info = t[size_t(type)];
        info.group = group;
        info.description = description;
        info.keywords = keywords;
    };
    about(ModulatorType::Lfo, "Motion", "a wave that keeps moving: sine, saw, square…", "oscillator wobble sweep");
    about(ModulatorType::Steps, "Motion", "a pattern of values you draw", "sequence step pattern");
    about(ModulatorType::Random, "Motion", "a new random value at a steady rate", "noise sample hold chaos");
    about(ModulatorType::Ramp, "Motion", "rises or falls over several bars: builds, fades", "build riser fade");
    about(ModulatorType::Envelope, "Notes", "rises and falls with each note", "adsr attack decay");
    about(ModulatorType::Note, "Notes", "follows the last note's velocity, pitch…", "key track velocity follow");
    about(ModulatorType::Macro, "Hand", "one knob that turns many", "control knob master");
    return t;
}

const std::vector<TypeInfo>& deviceTable() {
    static const std::vector<TypeInfo> table = buildDevices();
    return table;
}

const std::vector<TypeInfo>& modulatorTable() {
    static const std::vector<TypeInfo> table = buildModulators();
    return table;
}

} // namespace

const TypeInfo& deviceInfo(DeviceType type) {
    return deviceTable()[size_t(std::clamp(int(type), 0, kDeviceTypeCount - 1))];
}

const TypeInfo& modulatorInfo(ModulatorType type) {
    return modulatorTable()[size_t(std::clamp(int(type), 0, kModulatorTypeCount - 1))];
}

const ParamInfo* paramInfo(DeviceType type, int param) {
    const TypeInfo& t = deviceInfo(type);
    return param >= 0 && param < int(t.params.size()) ? &t.params[size_t(param)] : nullptr;
}

const ParamInfo* paramInfo(ModulatorType type, int param) {
    const TypeInfo& t = modulatorInfo(type);
    return param >= 0 && param < int(t.params.size()) ? &t.params[size_t(param)] : nullptr;
}

float display(const ParamInfo& p, float n) {
    n = std::clamp(n, 0.f, 1.f);
    switch (p.kind) {
    case K::Integer:
    case K::Choice:
        return std::round(p.min + n * (p.max - p.min));
    case K::Toggle:
        return n >= 0.5f ? 1.f : 0.f;
    case K::Exponential:
        return p.min * std::pow(p.max / p.min, n);
    case K::Number:
    default:
        return p.min + n * (p.max - p.min);
    }
}

float normalize(const ParamInfo& p, float v) {
    if (p.max <= p.min)
        return 0;
    float n;
    switch (p.kind) {
    case K::Toggle:
        return v >= 0.5f ? 1.f : 0.f;
    case K::Exponential:
        n = std::log(std::max(v, p.min) / p.min) / std::log(p.max / p.min);
        break;
    case K::Integer:
    case K::Choice:
        n = (std::round(v) - p.min) / (p.max - p.min);
        break;
    case K::Number:
    default:
        n = (v - p.min) / (p.max - p.min);
        break;
    }
    return std::clamp(n, 0.f, 1.f);
}

float defaultNormalized(const ParamInfo& p) { return normalize(p, p.def); }

std::string format(const ParamInfo& p, float n) {
    float v = display(p, n);
    char text[64];
    switch (p.kind) {
    case K::Choice: {
        size_t i = size_t(std::clamp<int>(int(v), 0, int(p.choices.size()) - 1));
        return p.choices.empty() ? "" : p.choices[i];
    }
    case K::Toggle:
        return v > 0.5f ? "On" : "Off";
    case K::Integer:
        snprintf(text, sizeof(text), p.min < 0 ? "%+.0f%s%s" : "%.0f%s%s", double(v), p.unit[0] ? " " : "", p.unit);
        return text;
    case K::Exponential:
        snprintf(text, sizeof(text), v < 10 ? "%.2f %s" : "%.0f %s", double(v), p.unit);
        return text;
    case K::Number:
    default: {
        bool percent = p.unit[0] == '%';
        snprintf(text, sizeof(text), p.min < 0 ? "%+.0f%s%s" : "%.0f%s%s", double(v), percent ? "" : " ", p.unit);
        return text;
    }
    }
}

Device makeDevice(DeviceType type, DeviceId id) {
    Device d;
    d.id = id;
    d.type = type;
    d.name = deviceInfo(type).name;
    for (const ParamInfo& p : deviceInfo(type).params)
        d.params.push_back(defaultNormalized(p));
    return d;
}

Modulator makeModulator(ModulatorType type, ModulatorId id) {
    Modulator m;
    m.id = id;
    m.type = type;
    m.name = modulatorInfo(type).name;
    for (const ParamInfo& p : modulatorInfo(type).params)
        m.params.push_back(defaultNormalized(p));
    if (type == ModulatorType::Steps)
        m.steps = {1, 0.25f, 0.75f, 0.5f, 1, 0, 0.6f, 0.3f};
    return m;
}

const std::vector<std::string>& rateNames() { return kRateNames; }

Tick rateTicks(int index, TimeSig ts) {
    static const double kQuarters[] = {0,      0,       0,       0,       2,       1,   0.5,  0.25, 0.125,
                                       0.0625, 4.0 / 3, 2.0 / 3, 1.0 / 3, 1.0 / 6, 1.5, 0.75, 0.375};
    index = std::clamp(index, 0, int(kRateNames.size()) - 1);
    if (index <= kRateBar) {
        static const int kBars[] = {8, 4, 2, 1};
        return ticksPerBar(ts) * kBars[index];
    }
    return std::max<Tick>(1, Tick(std::llround(kQuarters[index] * double(kPpq))));
}

} // namespace tessera::devices
