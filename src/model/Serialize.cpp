#include "Serialize.hpp"

#include "Devices.hpp"
#include "Json.hpp"

#include <algorithm>
#include <limits>

namespace tessera {

using namespace json;

namespace {

// Enum names are saved as strings, so reordering an enum never breaks old patches.
template <typename E, size_t N>
const char* enumName(E value, const char* const (&names)[N]) {
    size_t i = size_t(value);
    return i < N ? names[i] : names[0];
}

template <typename E, size_t N>
E enumFromName(const json_t* j, const char* const (&names)[N], E fallback) {
    std::string s = asString(j, "");
    for (size_t i = 0; i < N; i++)
        if (s == names[i])
            return E(i);
    return fallback;
}

const char* const kTrackTypes[] = {"instrument", "drum"};
const char* const kRecordInputs[] = {"any", "cv", "midi"};
const char* const kListenSources[] = {"none", "external", "trackAfter", "trackBefore"};
const char* const kAutoSources[] = {"knob", "cv"};
const char* const kAutoRecordModes[] = {"touch", "latch", "write"};
const char* const kLoopModes[] = {"off", "loop"};
const char* const kOccurrences[] = {"always", "first", "notFirst", "prevPlayed", "prevNotPlayed", "fill", "notFill"};
const char* const kCombineModes[] = {"override", "modulate"};
const char* const kLaunchQuantize[] = {"off", "1/16", "1/4", "1bar", "2bars", "4bars"};
const char* const kDeviceTypes[] = {"transpose",   "scale", "velocity", "noteLength", "chord",
                                    "arpeggiator", "echo",  "random",   "noteFilter", "humanize"};
const char* const kModulatorTypes[] = {"lfo", "envelope", "steps", "random", "ramp", "macro", "note"};
const char* const kParamOwners[] = {"device", "modulator"};
const char* const kModulationKinds[] = {"param", "auto", "knob"};

constexpr int kNoteMuted = 1;
constexpr int kNoteColorShift = 8;

/// Narrows a loaded integer by saturating, so an out-of-range value like pitch 200 becomes the type's maximum
/// (and normalize() then clamps it), instead of wrapping around to a negative number.
template <typename T>
T saturate(int64_t v) {
    return T(std::clamp<int64_t>(v, std::numeric_limits<T>::min(), std::numeric_limits<T>::max()));
}

template <typename IdT>
IdT readId(const json_t* j) {
    return IdT(uint64_t(std::max<int64_t>(0, asInt(j, 0))));
}

template <typename IdT>
json_t* idJson(IdT id) {
    return json_integer(json_int_t(id.value));
}

// ---- Scales -------------------------------------------------------------------------------------------------

json_t* scaleToJson(const ScaleRef& s) {
    json_t* j = json_object();
    setInt(j, "root", s.root);
    setString(j, "scale", s.scale);
    return j;
}

ScaleRef scaleFromJson(const json_t* j) {
    ScaleRef s;
    s.root = saturate<int8_t>(getInt(j, "root", s.root));
    s.scale = getString(j, "scale", s.scale);
    return s;
}

// ---- Automation ---------------------------------------------------------------------------------------------

json_t* paramRefToJson(const ParamRef& r) {
    return json_pack("[sIi]", enumName(r.kind, kParamOwners), json_int_t(r.owner), int(r.param));
}

ParamRef paramRefFromJson(const json_t* j) {
    ParamRef r;
    if (!json_is_array(j))
        return r;
    r.kind = enumFromName(json_array_get(j, 0), kParamOwners, r.kind);
    r.owner = uint64_t(std::max<int64_t>(0, intAt(j, 1, 0)));
    r.param = saturate<uint16_t>(intAt(j, 2, 0));
    return r;
}

json_t* automationToJson(const AutomationEnvelope& a) {
    json_t* j = json_object();
    set(j, "id", idJson(a.id));
    setString(j, "name", a.name);
    setInt(j, "color", a.color);
    setInt(j, "output", a.outputIndex);
    set(j, "range", json_pack("[ff]", double(a.rangeMin), double(a.rangeMax)));
    setBool(j, "bipolar", a.bipolar);
    setBool(j, "mute", a.mute);
    setNumber(j, "slew", a.slewMs);
    setString(j, "combine", enumName(a.combine, kCombineModes));
    setBool(j, "armed", a.armed);
    setString(j, "source", enumName(a.source, kAutoSources));
    setString(j, "recordMode", enumName(a.recordMode, kAutoRecordModes));

    json_t* targets = json_array();
    for (const ParamTarget& t : a.targets)
        json_array_append_new(
            targets, json_pack("[IIff]", json_int_t(t.moduleId), json_int_t(t.paramId), double(t.min), double(t.max)));
    set(j, "targets", targets);
    if (a.paramTarget.valid())
        set(j, "paramTarget", paramRefToJson(a.paramTarget));

    // Points as [t, value, curve, tension]. Curve is an index: CurveType must only ever be appended to.
    json_t* points = json_array();
    for (const AutoPoint& p : a.points)
        json_array_append_new(points,
                              json_pack("[Ifif]", json_int_t(p.t), double(p.value), int(p.curve), double(p.tension)));
    set(j, "points", points);
    return j;
}

AutomationEnvelope automationFromJson(const json_t* j) {
    AutomationEnvelope a;
    a.id = readId<AutomationId>(get(j, "id"));
    a.name = getString(j, "name", a.name);
    a.color = saturate<uint32_t>(getInt(j, "color", a.color));
    a.outputIndex = saturate<int>(getInt(j, "output", a.outputIndex));
    const json_t* range = get(j, "range");
    a.rangeMin = float(numberAt(range, 0, a.rangeMin));
    a.rangeMax = float(numberAt(range, 1, a.rangeMax));
    a.bipolar = getBool(j, "bipolar", a.bipolar);
    a.mute = getBool(j, "mute", a.mute);
    a.slewMs = getFloat(j, "slew", a.slewMs);
    a.combine = enumFromName(get(j, "combine"), kCombineModes, a.combine);
    a.armed = getBool(j, "armed", a.armed);
    a.source = enumFromName(get(j, "source"), kAutoSources, a.source);
    a.recordMode = enumFromName(get(j, "recordMode"), kAutoRecordModes, a.recordMode);
    forEach(get(j, "targets"), [&](const json_t* t) {
        ParamTarget pt;
        pt.moduleId = intAt(t, 0, pt.moduleId);
        pt.paramId = saturate<int>(intAt(t, 1, pt.paramId));
        pt.min = float(numberAt(t, 2, pt.min));
        pt.max = float(numberAt(t, 3, pt.max));
        a.targets.push_back(pt);
    });
    a.paramTarget = paramRefFromJson(get(j, "paramTarget"));
    forEach(get(j, "points"), [&](const json_t* p) {
        if (json_array_size(p) < 2)
            return;
        AutoPoint ap;
        ap.t = intAt(p, 0, 0);
        ap.value = float(numberAt(p, 1, 0));
        int64_t curve = intAt(p, 2, int64_t(ap.curve));
        ap.curve = curve >= 0 && curve <= int64_t(CurveType::SCurve) ? CurveType(curve) : CurveType::Linear;
        ap.tension = float(numberAt(p, 3, 0));
        a.points.push_back(ap);
    });
    return a;
}

json_t* automationListToJson(const std::vector<AutomationEnvelope>& lanes) {
    json_t* arr = json_array();
    for (const AutomationEnvelope& a : lanes)
        json_array_append_new(arr, automationToJson(a));
    return arr;
}

std::vector<AutomationEnvelope> automationListFromJson(const json_t* arr) {
    std::vector<AutomationEnvelope> out;
    forEach(arr, [&](const json_t* a) { out.push_back(automationFromJson(a)); });
    return out;
}

// ---- Notes --------------------------------------------------------------------------------------------------

json_t* exprToJson(const std::vector<ExprPoint>& points) {
    json_t* arr = json_array();
    for (const ExprPoint& p : points)
        json_array_append_new(arr, json_pack("[fff]", double(p.pos), double(p.value), double(p.tension)));
    return arr;
}

std::vector<ExprPoint> exprFromJson(const json_t* arr) {
    std::vector<ExprPoint> out;
    forEach(arr, [&](const json_t* p) {
        out.push_back({float(numberAt(p, 0, 0)), float(numberAt(p, 1, 0)), float(numberAt(p, 2, 0))});
    });
    return out;
}

/// Only operators that differ from the defaults, or nullptr when all are default.
json_t* operatorsToJson(const NoteOperators& ops) {
    const NoteOperators d;
    if (ops == d)
        return nullptr;
    json_t* j = json_object();
    if (ops.chance != d.chance)
        setNumber(j, "chance", ops.chance);
    if (ops.velocitySpread != d.velocitySpread)
        setNumber(j, "velSpread", ops.velocitySpread);
    if (ops.repeats != d.repeats)
        setInt(j, "repeats", ops.repeats);
    if (ops.repeatCurve != d.repeatCurve)
        setNumber(j, "repeatCurve", ops.repeatCurve);
    if (ops.repeatVelEnd != d.repeatVelEnd)
        setNumber(j, "repeatVelEnd", ops.repeatVelEnd);
    if (ops.repeatGate != d.repeatGate)
        setNumber(j, "repeatGate", ops.repeatGate);
    if (ops.occurrence != d.occurrence)
        setString(j, "occurrence", enumName(ops.occurrence, kOccurrences));
    if (ops.recurrenceLength != d.recurrenceLength)
        setInt(j, "recurrenceLength", ops.recurrenceLength);
    if (ops.recurrenceMask != d.recurrenceMask)
        setInt(j, "recurrenceMask", ops.recurrenceMask);
    if (ops.timingSpread != d.timingSpread)
        setNumber(j, "timingSpread", ops.timingSpread);
    return j;
}

NoteOperators operatorsFromJson(const json_t* j) {
    NoteOperators ops;
    ops.chance = getFloat(j, "chance", ops.chance);
    ops.velocitySpread = getFloat(j, "velSpread", ops.velocitySpread);
    ops.repeats = saturate<int8_t>(getInt(j, "repeats", ops.repeats));
    ops.repeatCurve = getFloat(j, "repeatCurve", ops.repeatCurve);
    ops.repeatVelEnd = getFloat(j, "repeatVelEnd", ops.repeatVelEnd);
    ops.repeatGate = getFloat(j, "repeatGate", ops.repeatGate);
    ops.occurrence = enumFromName(get(j, "occurrence"), kOccurrences, ops.occurrence);
    ops.recurrenceLength = saturate<uint8_t>(getInt(j, "recurrenceLength", ops.recurrenceLength));
    ops.recurrenceMask = saturate<uint16_t>(getInt(j, "recurrenceMask", ops.recurrenceMask));
    ops.timingSpread = getFloat(j, "timingSpread", ops.timingSpread);
    return ops;
}

json_t* noteToJson(const Note& n) {
    int flags = (n.muted ? kNoteMuted : 0) | (int(n.colorTag) << kNoteColorShift);
    json_t* arr = json_pack("[IIifffiI]", json_int_t(n.start), json_int_t(n.length), int(n.pitch), double(n.velocity),
                            double(n.releaseVelocity), double(n.microOffset), flags, json_int_t(n.id.value));

    json_t* extras = json_object();
    if (json_t* ops = operatorsToJson(n.ops))
        set(extras, "ops", ops);
    if (n.channel != Note().channel)
        setInt(extras, "ch", n.channel);
    if (!n.pitchExpr.empty())
        set(extras, "pitch", exprToJson(n.pitchExpr));
    if (!n.timbreExpr.empty())
        set(extras, "timbre", exprToJson(n.timbreExpr));
    if (!n.pressureExpr.empty())
        set(extras, "pressure", exprToJson(n.pressureExpr));
    if (json_object_size(extras) > 0)
        json_array_append_new(arr, extras);
    else
        json_decref(extras);
    return arr;
}

/// Returns false for an entry too short to be a note.
bool noteFromJson(const json_t* arr, Note& n) {
    if (!json_is_array(arr) || json_array_size(arr) < 3)
        return false;
    n.start = intAt(arr, 0, n.start);
    n.length = intAt(arr, 1, n.length);
    n.pitch = saturate<int8_t>(intAt(arr, 2, n.pitch));
    n.velocity = float(numberAt(arr, 3, n.velocity));
    n.releaseVelocity = float(numberAt(arr, 4, n.releaseVelocity));
    n.microOffset = float(numberAt(arr, 5, n.microOffset));
    int64_t flags = intAt(arr, 6, 0);
    n.muted = flags & kNoteMuted;
    n.colorTag = uint8_t((flags >> kNoteColorShift) & 0xFF);
    n.id = readId<NoteId>(json_array_get(arr, 7));

    const json_t* extras = json_array_get(arr, 8);
    if (json_is_object(extras)) {
        n.ops = operatorsFromJson(get(extras, "ops"));
        n.channel = saturate<uint8_t>(getInt(extras, "ch", n.channel));
        n.pitchExpr = exprFromJson(get(extras, "pitch"));
        n.timbreExpr = exprFromJson(get(extras, "timbre"));
        n.pressureExpr = exprFromJson(get(extras, "pressure"));
    }
    return true;
}

// ---- Tracks -------------------------------------------------------------------------------------------------

json_t* drumLaneToJson(const DrumLane& l) {
    json_t* j = json_object();
    setString(j, "name", l.name);
    setInt(j, "pitch", l.pitch);
    setInt(j, "output", l.outputIndex);
    setBool(j, "mute", l.mute);
    setBool(j, "solo", l.solo);
    setNumber(j, "gateMs", l.gateLengthMs);
    setInt(j, "choke", l.chokeGroup);
    setInt(j, "loop", l.loopLength);
    setInt(j, "step", l.stepLength);
    setInt(j, "color", l.color);
    return j;
}

DrumLane drumLaneFromJson(const json_t* j) {
    DrumLane l;
    l.name = getString(j, "name", l.name);
    l.pitch = saturate<int8_t>(getInt(j, "pitch", l.pitch));
    l.outputIndex = saturate<int>(getInt(j, "output", l.outputIndex));
    l.mute = getBool(j, "mute", l.mute);
    l.solo = getBool(j, "solo", l.solo);
    l.gateLengthMs = getFloat(j, "gateMs", l.gateLengthMs);
    l.chokeGroup = saturate<int>(getInt(j, "choke", l.chokeGroup));
    l.loopLength = getInt(j, "loop", l.loopLength);
    l.stepLength = getInt(j, "step", l.stepLength);
    l.color = uint32_t(getInt(j, "color", l.color));
    return l;
}

json_t* arrangerClipToJson(const ArrangerClip& a) {
    json_t* j = json_object();
    set(j, "id", idJson(a.id));
    set(j, "clip", idJson(a.clip));
    setInt(j, "pos", a.position);
    setInt(j, "len", a.length);
    setInt(j, "offset", a.offset);
    setBool(j, "muted", a.muted);
    setInt(j, "transpose", a.transpose);
    setNumber(j, "velScale", a.velocityScale);
    setNumber(j, "stretch", a.stretch);
    return j;
}

ArrangerClip arrangerClipFromJson(const json_t* j) {
    ArrangerClip a;
    a.id = readId<ArrangementId>(get(j, "id"));
    a.clip = readId<ClipId>(get(j, "clip"));
    a.position = getInt(j, "pos", a.position);
    a.length = getInt(j, "len", a.length);
    a.offset = getInt(j, "offset", a.offset);
    a.muted = getBool(j, "muted", a.muted);
    a.transpose = saturate<int8_t>(getInt(j, "transpose", a.transpose));
    a.velocityScale = getFloat(j, "velScale", a.velocityScale);
    a.stretch = getFloat(j, "stretch", a.stretch);
    return a;
}

// ---- Devices and modulators ---------------------------------------------------------------------------------

json_t* floatsToJson(const std::vector<float>& values) {
    json_t* arr = json_array();
    for (float v : values)
        json_array_append_new(arr, json_real(double(v)));
    return arr;
}

std::vector<float> floatsFromJson(const json_t* arr) {
    std::vector<float> out;
    forEach(arr, [&](const json_t* v) { out.push_back(float(asNumber(v, 0))); });
    return out;
}

json_t* modulationToJson(const Modulation& m) {
    json_t* j = json_object();
    setString(j, "kind", enumName(m.kind, kModulationKinds));
    setNumber(j, "amount", m.amount);
    switch (m.kind) {
    case Modulation::Kind::Param:
        set(j, "target", paramRefToJson(m.target));
        break;
    case Modulation::Kind::AutoChannel:
        set(j, "track", idJson(m.track));
        setInt(j, "channel", m.channel);
        break;
    case Modulation::Kind::RackKnob:
        set(j, "knob",
            json_pack("[IIff]", json_int_t(m.knob.moduleId), json_int_t(m.knob.paramId), double(m.knob.min),
                      double(m.knob.max)));
        break;
    }
    return j;
}

Modulation modulationFromJson(const json_t* j) {
    Modulation m;
    m.kind = enumFromName(get(j, "kind"), kModulationKinds, m.kind);
    m.amount = getFloat(j, "amount", m.amount);
    m.target = paramRefFromJson(get(j, "target"));
    m.track = readId<TrackId>(get(j, "track"));
    m.channel = saturate<int8_t>(getInt(j, "channel", m.channel));
    const json_t* knob = get(j, "knob");
    m.knob.moduleId = intAt(knob, 0, m.knob.moduleId);
    m.knob.paramId = saturate<int>(intAt(knob, 1, m.knob.paramId));
    m.knob.min = float(numberAt(knob, 2, m.knob.min));
    m.knob.max = float(numberAt(knob, 3, m.knob.max));
    return m;
}

json_t* modulatorToJson(const Modulator& m) {
    json_t* j = json_object();
    set(j, "id", idJson(m.id));
    setString(j, "type", enumName(m.type, kModulatorTypes));
    setString(j, "name", m.name);
    set(j, "params", floatsToJson(m.params));
    if (!m.steps.empty())
        set(j, "steps", floatsToJson(m.steps));
    json_t* routes = json_array();
    for (const Modulation& r : m.routes)
        json_array_append_new(routes, modulationToJson(r));
    set(j, "routes", routes);
    return j;
}

Modulator modulatorFromJson(const json_t* j) {
    Modulator m;
    m.id = readId<ModulatorId>(get(j, "id"));
    m.type = enumFromName(get(j, "type"), kModulatorTypes, m.type);
    m.name = getString(j, "name", m.name);
    m.params = floatsFromJson(get(j, "params"));
    m.steps = floatsFromJson(get(j, "steps"));
    forEach(get(j, "routes"), [&](const json_t* r) { m.routes.push_back(modulationFromJson(r)); });
    return m;
}

json_t* modulatorListToJson(const std::vector<Modulator>& mods) {
    json_t* arr = json_array();
    for (const Modulator& m : mods)
        json_array_append_new(arr, modulatorToJson(m));
    return arr;
}

std::vector<Modulator> modulatorListFromJson(const json_t* arr) {
    std::vector<Modulator> out;
    forEach(arr, [&](const json_t* m) { out.push_back(modulatorFromJson(m)); });
    return out;
}

json_t* deviceToJson(const Device& d) {
    json_t* j = json_object();
    set(j, "id", idJson(d.id));
    setString(j, "type", enumName(d.type, kDeviceTypes));
    setString(j, "name", d.name);
    setBool(j, "on", d.on);
    if (d.affectDrumLanes)
        setBool(j, "drumLanes", true);
    set(j, "params", floatsToJson(d.params));
    if (!d.modulators.empty())
        set(j, "modulators", modulatorListToJson(d.modulators));
    return j;
}

Device deviceFromJson(const json_t* j) {
    Device d;
    d.id = readId<DeviceId>(get(j, "id"));
    d.type = enumFromName(get(j, "type"), kDeviceTypes, d.type);
    d.name = getString(j, "name", d.name);
    d.on = getBool(j, "on", d.on);
    d.affectDrumLanes = getBool(j, "drumLanes", d.affectDrumLanes);
    d.params = floatsFromJson(get(j, "params"));
    d.modulators = modulatorListFromJson(get(j, "modulators"));
    return d;
}

json_t* trackToJson(const Track& t) {
    json_t* j = json_object();
    set(j, "id", idJson(t.id));
    setString(j, "name", t.name);
    setInt(j, "color", t.color);
    setString(j, "type", enumName(t.type, kTrackTypes));
    setInt(j, "polyphony", t.polyphony);
    setBool(j, "mute", t.mute);
    setBool(j, "solo", t.solo);
    setBool(j, "armed", t.armed);
    setBool(j, "monitor", t.monitor);
    setString(j, "recordInput", enumName(t.recordInput, kRecordInputs));
    setInt(j, "midiChannel", t.midiChannel);
    if (t.listen != ListenSource::None) {
        setString(j, "listen", enumName(t.listen, kListenSources));
        if (t.listenTrack.valid())
            set(j, "listenTrack", idJson(t.listenTrack));
    }
    setNumber(j, "gateScale", t.gateLengthScale);
    setInt(j, "transpose", t.transpose);

    json_t* lanes = json_array();
    for (const DrumLane& l : t.drumLanes)
        json_array_append_new(lanes, drumLaneToJson(l));
    set(j, "drumLanes", lanes);

    json_t* slots = json_array();
    for (const ClipSlot& s : t.slots)
        json_array_append_new(slots, s.clip ? idJson(*s.clip) : json_null());
    set(j, "slots", slots);

    json_t* arrangement = json_array();
    for (const ArrangerClip& a : t.arrangement)
        json_array_append_new(arrangement, arrangerClipToJson(a));
    set(j, "arrangement", arrangement);

    set(j, "automation", automationListToJson(t.automation));

    if (!t.devices.empty()) {
        json_t* devices = json_array();
        for (const Device& d : t.devices)
            json_array_append_new(devices, deviceToJson(d));
        set(j, "devices", devices);
    }
    if (!t.modulators.empty())
        set(j, "modulators", modulatorListToJson(t.modulators));
    return j;
}

Track trackFromJson(const json_t* j) {
    Track t;
    t.id = readId<TrackId>(get(j, "id"));
    t.name = getString(j, "name", t.name);
    t.color = saturate<uint32_t>(getInt(j, "color", t.color));
    t.type = enumFromName(get(j, "type"), kTrackTypes, t.type);
    t.polyphony = saturate<int>(getInt(j, "polyphony", t.polyphony));
    t.mute = getBool(j, "mute", t.mute);
    t.solo = getBool(j, "solo", t.solo);
    t.armed = getBool(j, "armed", t.armed);
    t.monitor = getBool(j, "monitor", t.monitor);
    t.recordInput = enumFromName(get(j, "recordInput"), kRecordInputs, t.recordInput);
    t.midiChannel = saturate<int8_t>(getInt(j, "midiChannel", t.midiChannel));
    t.listen = enumFromName(get(j, "listen"), kListenSources, t.listen);
    t.listenTrack = readId<TrackId>(get(j, "listenTrack"));
    t.gateLengthScale = getFloat(j, "gateScale", t.gateLengthScale);
    t.transpose = saturate<int8_t>(getInt(j, "transpose", t.transpose));
    forEach(get(j, "drumLanes"), [&](const json_t* l) { t.drumLanes.push_back(drumLaneFromJson(l)); });
    forEach(get(j, "slots"), [&](const json_t* s) {
        ClipSlot slot;
        if (json_is_integer(s))
            slot.clip = readId<ClipId>(s);
        t.slots.push_back(slot);
    });
    forEach(get(j, "arrangement"), [&](const json_t* a) { t.arrangement.push_back(arrangerClipFromJson(a)); });
    t.automation = automationListFromJson(get(j, "automation"));
    forEach(get(j, "devices"), [&](const json_t* d) { t.devices.push_back(deviceFromJson(d)); });
    t.modulators = modulatorListFromJson(get(j, "modulators"));
    return t;
}

} // namespace

// ---- Clips --------------------------------------------------------------------------------------------------

json_t* clipToJson(const Clip& c) {
    json_t* j = json_object();
    set(j, "id", idJson(c.id));
    setString(j, "name", c.name);
    setInt(j, "color", c.color);
    setInt(j, "length", c.length);
    set(j, "loop", json_pack("[II]", json_int_t(c.loopStart), json_int_t(c.loopEnd)));
    setString(j, "loopMode", enumName(c.loopMode, kLoopModes));
    json_t* notes = json_array();
    for (const Note& n : c.notes)
        json_array_append_new(notes, noteToJson(n));
    set(j, "notes", notes);
    set(j, "automation", automationListToJson(c.automation));
    if (c.scaleOverride)
        set(j, "scale", scaleToJson(*c.scaleOverride));
    if (c.launchQuantize)
        setString(j, "launchQuantize", enumName(*c.launchQuantize, kLaunchQuantize));
    return j;
}

Clip clipFromJson(const json_t* j) {
    Clip c;
    c.id = readId<ClipId>(get(j, "id"));
    c.name = getString(j, "name", c.name);
    c.color = saturate<uint32_t>(getInt(j, "color", c.color));
    c.length = getInt(j, "length", c.length);
    const json_t* loop = get(j, "loop");
    c.loopStart = intAt(loop, 0, c.loopStart);
    c.loopEnd = intAt(loop, 1, c.length);
    c.loopMode = enumFromName(get(j, "loopMode"), kLoopModes, c.loopMode);
    forEach(get(j, "notes"), [&](const json_t* nj) {
        Note n;
        if (noteFromJson(nj, n))
            c.notes.push_back(n);
    });
    c.automation = automationListFromJson(get(j, "automation"));
    if (json_is_object(get(j, "scale")))
        c.scaleOverride = scaleFromJson(get(j, "scale"));
    if (json_is_string(get(j, "launchQuantize")))
        c.launchQuantize = enumFromName(get(j, "launchQuantize"), kLaunchQuantize, LaunchQuantize::Bar);
    return c;
}

// ---- Song ---------------------------------------------------------------------------------------------------

json_t* songToJson(const Song& s) {
    json_t* j = json_object();
    set(j, "timeSig", json_pack("[ii]", s.timeSig.num, s.timeSig.den));
    setNumber(j, "swing", s.swing);
    set(j, "scale", scaleToJson(s.scale));
    json_t* custom = json_array();
    for (const CustomScale& c : s.customScales) {
        json_t* cj = json_object();
        setString(cj, "id", c.id);
        setString(cj, "name", c.name);
        setInt(cj, "mask", c.mask);
        json_array_append_new(custom, cj);
    }
    set(j, "customScales", custom);
    set(j, "loop", json_pack("[II]", json_int_t(s.loopStart), json_int_t(s.loopEnd)));
    setInt(j, "seed", s.seed);
    setString(j, "launchQuantize", enumName(s.launchQuantize, kLaunchQuantize));
    setInt(j, "nextId", json_int_t(s.nextId));

    json_t* tracks = json_array();
    for (const Track& t : s.tracks)
        json_array_append_new(tracks, trackToJson(t));
    set(j, "tracks", tracks);

    json_t* groups = json_array();
    for (const TrackGroup& g : s.groups) {
        json_t* gj = json_object();
        set(gj, "id", idJson(g.id));
        setString(gj, "name", g.name);
        setInt(gj, "color", g.color);
        json_t* members = json_array();
        for (TrackId id : g.tracks)
            json_array_append_new(members, idJson(id));
        set(gj, "tracks", members);
        json_array_append_new(groups, gj);
    }
    set(j, "groups", groups);

    json_t* scenes = json_array();
    for (const Scene& sc : s.scenes) {
        json_t* sj = json_object();
        setString(sj, "name", sc.name);
        setInt(sj, "color", sc.color);
        if (sc.tempo)
            setNumber(sj, "tempo", *sc.tempo);
        json_array_append_new(scenes, sj);
    }
    set(j, "scenes", scenes);

    json_t* clips = json_array();
    for (const auto& [id, clip] : s.clips)
        json_array_append_new(clips, clipToJson(clip));
    set(j, "clips", clips);

    json_t* markers = json_array();
    for (const Marker& m : s.markers) {
        json_t* mj = json_object();
        set(mj, "id", idJson(m.id));
        setInt(mj, "pos", m.position);
        setString(mj, "name", m.name);
        setInt(mj, "color", m.color);
        json_array_append_new(markers, mj);
    }
    set(j, "markers", markers);
    if (!s.modulators.empty())
        set(j, "modulators", modulatorListToJson(s.modulators));
    return j;
}

json_t* clipFileToJson(const Clip& clip) {
    json_t* j = json_object();
    setString(j, "format", "tessera-clip");
    setInt(j, "schema", Song::kSchemaVersion);
    set(j, "clip", clipToJson(clip));
    return j;
}

std::optional<Clip> clipFileFromJson(const json_t* fileJ) {
    if (getString(fileJ, "format", "") != "tessera-clip" || !json_is_object(get(fileJ, "clip")))
        return std::nullopt;
    // Normalizing a one-clip song repairs the clip the same way a loaded patch is repaired.
    Song carrier;
    Clip clip = clipFromJson(get(fileJ, "clip"));
    if (!clip.id.valid())
        clip.id = ClipId(1);
    carrier.clips.emplace(clip.id, clip);
    normalize(carrier);
    return carrier.clips.begin()->second;
}

bool migrateSong(json_t* songJ, int fromVersion) {
    // Each schema bump adds a step here that rewrites the previous version.
    if (fromVersion < 2) {
        // Version 1 had no automatic voice count and saved its default of 8 on every track, which sent 8
        // channels however few notes played. Those tracks now follow their clips.
        json_t* tracks = json_object_get(songJ, "tracks");
        size_t i;
        json_t* t;
        json_array_foreach(tracks, i, t) {
            json_t* poly = json_object_get(t, "polyphony");
            if (json_is_integer(poly) && json_integer_value(poly) == 8)
                json_object_set_new(t, "polyphony", json_integer(kAutoVoices));
        }
    }
    if (fromVersion < 3) {
        // Before version 3 the key couldn't be chosen; every song saved the C major default without anyone
        // picking it. Showing that as a key would dim rows and flag notes nobody asked about.
        json_object_del(songJ, "scale");
    }
    if (fromVersion < 4) {
        // Monitoring did nothing before recording existed, so the saved "off" was never a choice; armed tracks
        // now play what comes in by default.
        json_t* tracks = json_object_get(songJ, "tracks");
        size_t i;
        json_t* t;
        json_array_foreach(tracks, i, t) json_object_set_new(t, "monitor", json_true());
    }
    // Version 5 added device chains and modulators. Older songs have none, and every new field loads as empty,
    // so there is nothing to rewrite.
    return fromVersion <= Song::kSchemaVersion;
}

namespace {

/// Before 13 Oct 2026 Random also had velocity (knob 3) and timing (knob 4). Those moved to the Humanize device: a
/// Random that used them gets a Humanize right after it with the same values, and what pointed at those knobs
/// (automation lanes, modulations) points at Humanize's.
void splitLegacyRandom(Song& s) {
    constexpr size_t kOldVelocity = 3, kOldTiming = 4;
    std::vector<std::pair<uint64_t, uint64_t>> moved; // Random's ID -> its Humanize's ID
    for (Track& t : s.tracks) {
        for (size_t i = 0; i < t.devices.size(); i++) {
            Device& d = t.devices[i];
            if (d.type != DeviceType::Random || d.params.size() <= kOldVelocity)
                continue;
            float velocity = d.params[kOldVelocity];
            float timing = d.params.size() > kOldTiming ? d.params[kOldTiming] : 0;
            d.params.resize(kOldVelocity);
            if ((velocity <= 0 && timing <= 0) || int(t.devices.size()) >= kMaxDevices)
                continue;
            Device h;
            h.id = s.newId<DeviceId>();
            h.type = DeviceType::Humanize;
            h.name = "Humanize";
            h.on = d.on;
            h.params = {timing, velocity, 0}; // the same ranges as before, so the same normalized values
            moved.push_back({d.id.value, h.id.value});
            t.devices.insert(t.devices.begin() + long(i) + 1, h);
            i++;
        }
    }
    if (moved.empty())
        return;
    auto retarget = [&](ParamRef& r) {
        if (r.kind != ParamRef::Kind::Device || (r.param != kOldVelocity && r.param != kOldTiming))
            return;
        for (auto [from, to] : moved)
            if (r.owner == from) {
                r.owner = to;
                r.param =
                    uint16_t(r.param == kOldVelocity ? chain::knob::kHumanizeVelocity : chain::knob::kHumanizeTiming);
            }
    };
    auto routes = [&](std::vector<Modulator>& list) {
        for (Modulator& m : list)
            for (Modulation& r : m.routes)
                if (r.kind == Modulation::Kind::Param)
                    retarget(r.target);
    };
    routes(s.modulators);
    for (Track& t : s.tracks) {
        routes(t.modulators);
        for (Device& d : t.devices)
            routes(d.modulators);
        for (AutomationEnvelope& lane : t.automation)
            retarget(lane.paramTarget);
    }
    for (auto& [id, clip] : s.clips)
        for (AutomationEnvelope& lane : clip.automation)
            retarget(lane.paramTarget);
}

} // namespace

Song songFromJson(const json_t* songJ, int schemaVersion) {
    Song s;
    if (!json_is_object(songJ)) {
        normalize(s);
        return s;
    }
    Ref migrated(json_deep_copy(songJ));
    migrateSong(migrated.get(), schemaVersion);
    const json_t* j = migrated.get();

    const json_t* ts = get(j, "timeSig");
    s.timeSig.num = saturate<int>(intAt(ts, 0, s.timeSig.num));
    s.timeSig.den = saturate<int>(intAt(ts, 1, s.timeSig.den));
    s.swing = getFloat(j, "swing", s.swing);
    if (json_is_object(get(j, "scale")))
        s.scale = scaleFromJson(get(j, "scale"));
    forEach(get(j, "customScales"), [&](const json_t* cj) {
        CustomScale c;
        c.id = getString(cj, "id", c.id);
        c.name = getString(cj, "name", c.name);
        c.mask = uint16_t(getInt(cj, "mask", c.mask) & 0xFFF);
        s.customScales.push_back(c);
    });
    const json_t* loop = get(j, "loop");
    s.loopStart = intAt(loop, 0, s.loopStart);
    s.loopEnd = intAt(loop, 1, s.loopEnd);
    s.seed = saturate<uint32_t>(getInt(j, "seed", s.seed));
    s.launchQuantize = enumFromName(get(j, "launchQuantize"), kLaunchQuantize, s.launchQuantize);
    s.nextId = uint64_t(std::max<int64_t>(1, getInt(j, "nextId", 1)));

    forEach(get(j, "clips"), [&](const json_t* cj) {
        Clip c = clipFromJson(cj);
        if (c.id.valid() && !s.clips.count(c.id))
            s.clips.emplace(c.id, std::move(c));
    });
    forEach(get(j, "tracks"), [&](const json_t* tj) { s.tracks.push_back(trackFromJson(tj)); });
    forEach(get(j, "groups"), [&](const json_t* gj) {
        TrackGroup g;
        g.id = readId<GroupId>(get(gj, "id"));
        g.name = getString(gj, "name", g.name);
        g.color = saturate<uint32_t>(getInt(gj, "color", g.color));
        forEach(get(gj, "tracks"), [&](const json_t* id) { g.tracks.push_back(readId<TrackId>(id)); });
        s.groups.push_back(g);
    });
    forEach(get(j, "scenes"), [&](const json_t* sj) {
        Scene sc;
        sc.name = getString(sj, "name", sc.name);
        sc.color = saturate<uint32_t>(getInt(sj, "color", sc.color));
        if (json_is_number(get(sj, "tempo")))
            sc.tempo = getNumber(sj, "tempo", 120);
        s.scenes.push_back(sc);
    });
    forEach(get(j, "markers"), [&](const json_t* mj) {
        Marker m;
        m.id = readId<MarkerId>(get(mj, "id"));
        m.position = getInt(mj, "pos", m.position);
        m.name = getString(mj, "name", m.name);
        m.color = saturate<uint32_t>(getInt(mj, "color", m.color));
        s.markers.push_back(m);
    });
    s.modulators = modulatorListFromJson(get(j, "modulators"));

    // Before normalize, which trims each device's knobs to its type's list and would drop the old values.
    splitLegacyRandom(s);
    normalize(s);
    return s;
}

} // namespace tessera
