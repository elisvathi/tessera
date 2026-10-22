#include "Recording.hpp"

#include "AutomationEdits.hpp"
#include "Json.hpp"
#include "LauncherCommands.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>

namespace tessera {

using namespace json;

json_t* RecordSettings::toJson() const {
    json_t* j = json_object();
    setInt(j, "countInBars", countInBars);
    setInt(j, "clipBars", clipBars);
    setString(j, "arranger",
              arranger == ArrangerRecordMode::Replace ? "replace"
              : arranger == ArrangerRecordMode::Stack ? "stack"
                                                      : "merge");
    setInt(j, "quantize", quantize);
    setNumber(j, "strength", strength);
    setBool(j, "toKey", toKey);
    setBool(j, "mpe", mpe);
    setBool(j, "newControlLanes", newControlLanes);
    return j;
}

RecordSettings RecordSettings::fromJson(const json_t* j) {
    RecordSettings s;
    if (!json_is_object(j))
        return s;
    s.countInBars = int(std::clamp<int64_t>(getInt(j, "countInBars", s.countInBars), 0, kMaxCountInBars));
    s.clipBars = int(std::clamp<int64_t>(getInt(j, "clipBars", s.clipBars), 0, 64));
    // "overdub" is what Stack was called before Merge came.
    std::string arranger = getString(j, "arranger", "merge");
    s.arranger = arranger == "replace"                          ? ArrangerRecordMode::Replace
                 : arranger == "stack" || arranger == "overdub" ? ArrangerRecordMode::Stack
                                                                : ArrangerRecordMode::Merge;
    s.quantize = std::clamp<Tick>(getInt(j, "quantize", s.quantize), 0, 4 * kPpq);
    s.strength = std::clamp(getFloat(j, "strength", s.strength), 0.f, 1.f);
    s.toKey = getBool(j, "toKey", s.toKey);
    s.mpe = getBool(j, "mpe", s.mpe);
    s.newControlLanes = getBool(j, "newControlLanes", s.newControlLanes);
    return s;
}

Tick wholeBars(Tick length, TimeSig ts) {
    Tick bar = ticksPerBar(ts);
    return std::max<Tick>(1, (length + bar - 1) / bar) * bar;
}

namespace {

Tick positiveMod(Tick a, Tick b) { return ((a % b) + b) % b; }

Note toNote(const TakeNote& t, Tick start, const NextId& nextId) {
    Note n;
    n.id = NoteId(nextId());
    n.start = start;
    n.length = std::max<Tick>(1, t.length);
    n.pitch = int8_t(std::clamp<int>(t.pitch, 0, 127));
    n.velocity = std::clamp(t.velocity, 0.f, 1.f);
    n.releaseVelocity = std::clamp(t.release, 0.f, 1.f);
    n.channel = uint8_t(std::clamp<int>(t.channel, 1, kMaxNoteChannel));
    n.pitchExpr = t.pitchExpr;
    n.timbreExpr = t.timbreExpr;
    n.pressureExpr = t.pressureExpr;
    return n;
}

/// Every clip a track plays: its launcher clips and the clips on its timeline.
std::vector<const Clip*> clipsOf(const Song& song, const Track& track) {
    std::vector<const Clip*> out;
    auto add = [&](ClipId id) {
        const Clip* c = song.findClip(id);
        if (c && std::find(out.begin(), out.end(), c) == out.end())
            out.push_back(c);
    };
    for (const ClipSlot& slot : track.slots)
        if (slot.clip)
            add(*slot.clip);
    for (const ArrangerClip& a : track.arrangement)
        add(a.clip);
    return out;
}

/// The track that plays a clip, from a launcher slot or its timeline; null when none does.
const Track* trackOf(const Song& song, ClipId clip) {
    for (const Track& t : song.tracks) {
        for (const ClipSlot& slot : t.slots)
            if (slot.clip && *slot.clip == clip)
                return &t;
        for (const ArrangerClip& a : t.arrangement)
            if (a.clip == clip)
                return &t;
    }
    return nullptr;
}

/// `lane` with the take's points over the stretch they cover replacing what was there. In a loop of `loopLength`
/// from `loopStart` the points wrap into it; a take that crosses the loop end replaces both stretches.
void writePoints(AutomationEnvelope& lane, const std::vector<AutoPoint>& points, Tick loopStart, Tick loopLength) {
    std::vector<std::vector<AutoPoint>> runs(1);
    Tick last = std::numeric_limits<Tick>::min();
    for (AutoPoint p : points) {
        if (loopLength > 0)
            p.t = loopStart + positiveMod(p.t - loopStart, loopLength);
        p.t = std::max<Tick>(0, p.t);
        if (p.t < last)
            runs.emplace_back();
        runs.back().push_back(p);
        last = p.t;
    }
    for (const std::vector<AutoPoint>& run : runs) {
        if (run.empty())
            continue;
        Tick from = run.front().t, to = run.back().t;
        auto& pts = lane.points;
        pts.erase(std::remove_if(pts.begin(), pts.end(), [&](const AutoPoint& p) { return p.t >= from && p.t <= to; }),
                  pts.end());
        pts.insert(pts.end(), run.begin(), run.end());
    }
    std::stable_sort(lane.points.begin(), lane.points.end(),
                     [](const AutoPoint& a, const AutoPoint& b) { return a.t < b.t; });
}

/// A clip lane made for a track lane's take: the same output, range and name, but no points, knobs or MIDI link
/// (the knobs stay mapped on the track lane).
AutomationEnvelope clipLaneFor(const AutomationEnvelope& source, AutomationId id) {
    AutomationEnvelope lane = source;
    lane.id = id;
    lane.points.clear();
    lane.targets.clear();
    lane.midi = MidiControl();
    lane.noRecord = false;
    return lane;
}

/// The take's lanes written into `lanes` (a clip's): into the lane that already records the control or plays the
/// source's target, or a new one. A new CC lane copies a lane the track already has for that CC (its channel,
/// range and name), else takes a free AUTO channel; with none free the CC is dropped. A lane marked "don't record"
/// keeps its points.
void mergeTakeLanes(const Song& song, const Track& track, std::vector<AutomationEnvelope>& lanes,
                    const std::vector<TakeLane>& take, Tick loopStart, Tick loopLength, const NextId& nextId) {
    for (const TakeLane& tl : take) {
        if ((!tl.control.valid() && !tl.source) || tl.points.empty())
            continue;
        std::vector<AutomationEnvelope>::iterator it;
        if (tl.source) {
            it = std::find_if(lanes.begin(), lanes.end(), [&](const AutomationEnvelope& l) {
                return !l.midi.valid() && sameTarget(l, *tl.source);
            });
            if (it == lanes.end()) {
                lanes.push_back(clipLaneFor(*tl.source, AutomationId(nextId())));
                it = lanes.end() - 1;
            }
        } else {
            it = std::find_if(lanes.begin(), lanes.end(),
                              [&](const AutomationEnvelope& l) { return l.midi == tl.control; });
            if (it == lanes.end()) {
                if (tl.control.kind == MidiControl::Kind::Cc) {
                    if (const AutomationEnvelope* linked = midiLaneOf(song, track, tl.control)) {
                        lanes.push_back(clipLaneFor(*linked, AutomationId(nextId())));
                        lanes.back().midi = tl.control;
                    } else {
                        int output = midiLaneOutput(song, track, tl.control, lanes);
                        if (output < 0)
                            continue;
                        lanes.push_back(makeMidiLane(AutomationId(nextId()), tl.control, output, track.color));
                    }
                } else {
                    lanes.push_back(makeMidiLane(AutomationId(nextId()), tl.control, 0, track.color));
                }
                it = lanes.end() - 1;
            }
        }
        if (!it->noRecord)
            writePoints(*it, tl.points, loopStart, loopLength);
    }
}

} // namespace

CommandPtr recordTrackLanes(const Song& song, TrackId trackId, const std::vector<TakeLane>& take) {
    const Track* track = song.findTrack(trackId);
    if (!track)
        return nullptr;
    auto out = std::make_unique<CompoundCommand>("Record");
    LaneOwner owner = LaneOwner::ofTrack(trackId);
    for (const TakeLane& tl : take) {
        if (!tl.source || tl.points.empty())
            continue;
        for (size_t i = 0; i < track->automation.size(); i++) {
            const AutomationEnvelope& lane = track->automation[i];
            if (lane.id != tl.source->id || lane.noRecord)
                continue;
            AutomationEnvelope next = lane;
            writePoints(next, tl.points, 0, 0);
            out->add(std::make_unique<EditLane>("Record " + lane.name, owner, int(i), lane, next));
        }
    }
    if (out->empty())
        return nullptr;
    return out;
}

namespace {

/// Whether a track lane has a knob or AUTO REC source (MIDI lanes and device knob lanes aside).
bool hasRackSource(const AutomationEnvelope& l) { return l.source == AutoSource::Cv || !l.targets.empty(); }

} // namespace

LaneRecordState laneRecordState(const Song& song, LaneOwner owner, const AutomationEnvelope& lane, int autoRecInputs,
                                const std::function<bool(const ParamTarget&)>& knobExists, std::string* why) {
    const Track* track = owner.track.valid() ? song.findTrack(owner.track) : trackOf(song, owner.clip);
    if (!track || !track->armed)
        return LaneRecordState::Off;
    if (lane.noRecord)
        return LaneRecordState::Kept;
    if (lane.midi.valid() || lane.paramTarget.valid())
        return LaneRecordState::Records;
    // A clip lane records what its track's lane on the same output reads.
    const AutomationEnvelope* source = &lane;
    if (owner.clip.valid()) {
        source = nullptr;
        for (const AutomationEnvelope& l : track->automation)
            if (!l.noRecord && !l.midi.valid() && hasRackSource(l) && sameTarget(l, lane))
                source = &l;
        if (!source)
            return LaneRecordState::Off;
    }
    if (source->source == AutoSource::Cv) {
        int channel = source->recInput >= 0 ? source->recInput : source->outputIndex;
        if (channel < autoRecInputs)
            return LaneRecordState::Records;
        if (why)
            *why = "Nothing patched to AUTO REC channel " + std::to_string(channel + 1);
        return LaneRecordState::Missing;
    }
    if (source->targets.empty())
        return LaneRecordState::Off; // a lane you draw: nothing to record from
    if (knobExists && !knobExists(source->targets.front())) {
        if (why)
            *why = "Its knob's module is gone";
        return LaneRecordState::Missing;
    }
    return LaneRecordState::Records;
}

std::string laneSourceLabel(const AutomationEnvelope& lane) {
    switch (lane.midi.kind) {
    case MidiControl::Kind::PitchBend:
        return "bend";
    case MidiControl::Kind::Pressure:
        return "pressure";
    case MidiControl::Kind::Cc:
        return "CC " + std::to_string(lane.midi.cc);
    case MidiControl::Kind::None:
        break;
    }
    if (lane.paramTarget.valid())
        return "device knob";
    if (lane.source == AutoSource::Cv)
        return "AUTO REC " + std::to_string((lane.recInput >= 0 ? lane.recInput : lane.outputIndex) + 1);
    return lane.targets.empty() ? "" : "knob";
}

std::string laneOutputLabel(const AutomationEnvelope& lane) {
    if (lane.paramTarget.valid())
        return "knob";
    switch (lane.drive) {
    case LaneDrive::Pitch:
        return "V/OCT";
    case LaneDrive::Pressure:
        return "PRESS";
    case LaneDrive::Auto:
    default:
        return "AUTO " + std::to_string(lane.outputIndex + 1);
    }
}

std::string midiControlName(MidiControl control) {
    switch (control.kind) {
    case MidiControl::Kind::PitchBend:
        return "Pitch bend";
    case MidiControl::Kind::Pressure:
        return "Pressure";
    case MidiControl::Kind::Cc:
        break;
    case MidiControl::Kind::None:
    default:
        return "";
    }
    std::string number = "CC " + std::to_string(control.cc);
    switch (control.cc) {
    case 1:
        return "Mod wheel (" + number + ")";
    case 2:
        return "Breath (" + number + ")";
    case 4:
        return "Foot (" + number + ")";
    case 7:
        return "Volume (" + number + ")";
    case 11:
        return "Expression (" + number + ")";
    case 64:
        return "Sustain (" + number + ")";
    case 74:
        return "Timbre (" + number + ")";
    default:
        return number;
    }
}

AutomationEnvelope makeMidiLane(AutomationId id, MidiControl control, int output, uint32_t color) {
    AutomationEnvelope lane = makeLane(id, output, color);
    lane.name = midiControlName(control);
    lane.midi = control;
    if (control.kind == MidiControl::Kind::PitchBend) {
        lane.drive = LaneDrive::Pitch;
        lane.bipolar = true;
        lane.rangeMin = -2 / 12.f;
        lane.rangeMax = 2 / 12.f;
    } else if (control.kind == MidiControl::Kind::Pressure) {
        lane.drive = LaneDrive::Pressure;
    }
    return lane;
}

const AutomationEnvelope* midiLaneOf(const Song& song, const Track& track, MidiControl control) {
    auto in = [&](const std::vector<AutomationEnvelope>& lanes) -> const AutomationEnvelope* {
        auto it =
            std::find_if(lanes.begin(), lanes.end(), [&](const AutomationEnvelope& l) { return l.midi == control; });
        return it == lanes.end() ? nullptr : &*it;
    };
    if (const AutomationEnvelope* l = in(track.automation))
        return l;
    for (const Clip* c : clipsOf(song, track))
        if (const AutomationEnvelope* l = in(c->automation))
            return l;
    return nullptr;
}

bool isSetupControl(MidiControl control) {
    return control.kind == MidiControl::Kind::Cc && (control.cc == 0 || control.cc == 32 || control.cc >= 120);
}

int midiLaneOutput(const Song& song, const Track& track, MidiControl control,
                   const std::vector<AutomationEnvelope>& pending) {
    std::vector<const std::vector<AutomationEnvelope>*> all{&track.automation, &pending};
    for (const Clip* c : clipsOf(song, track))
        all.push_back(&c->automation);
    for (const auto* lanes : all)
        for (const AutomationEnvelope& l : *lanes)
            if (drivesOutput(l) && l.midi == control)
                return l.outputIndex;
    for (int o = 0; o < kMaxAutomationLanes; o++) {
        bool used = false;
        for (const auto* lanes : all)
            for (const AutomationEnvelope& l : *lanes)
                used |= drivesOutput(l) && l.outputIndex == o;
        if (!used)
            return o;
    }
    return -1;
}

namespace {

constexpr float kCurveTolerance = 0.01f; // of a curve's 0..1 range
constexpr float kBendTolerance = 0.05f;  // semitones
constexpr size_t kMaxCurvePoints = 64;   // per note curve
constexpr size_t kMaxLanePoints = 4096;  // per lane and take

/// Sustain, portamento, sostenuto, soft pedal, legato, hold 2: on or off, so their lanes step.
bool isSwitch(MidiControl c) { return c.kind == MidiControl::Kind::Cc && c.cc >= 64 && c.cc <= 69; }

/// A note's curve from what its key played, across the note (0..1 of `length` from `start`). A curve that starts
/// late begins at `from` (pressure and bend are 0 before the key moved them); `from` NAN holds the first value.
std::vector<ExprPoint> curveOf(const TimedValues& samples, double start, double length, float tolerance, float from) {
    if (samples.empty())
        return {};
    std::vector<double> x;
    std::vector<float> y;
    if (!std::isnan(from) && samples.front().first > start) {
        x.push_back(start);
        y.push_back(from);
    }
    for (const auto& [t, v] : samples) {
        x.push_back(std::max(t, start));
        y.push_back(v);
    }
    std::vector<ExprPoint> out;
    for (size_t i : thinValues(x, y, tolerance, kMaxCurvePoints))
        out.push_back({float(std::clamp((x[i] - start) / length, 0.0, 1.0)), y[i], 0});
    // Pressure or bend that stayed at rest says nothing. (A steady timbre does: it is where the finger sat.)
    if (!std::isnan(from) && std::all_of(out.begin(), out.end(), [&](const ExprPoint& p) { return p.value == from; }))
        return {};
    return out;
}

} // namespace

std::vector<ExprPoint> noteCurve(NoteCurve curve, const TimedValues& values, double start, double length) {
    switch (curve) {
    case NoteCurve::Pitch:
        return curveOf(values, start, length, kBendTolerance, 0);
    case NoteCurve::Timbre:
        return curveOf(values, start, length, kCurveTolerance, NAN);
    case NoteCurve::Pressure:
    default:
        return curveOf(values, start, length, kCurveTolerance, 0);
    }
}

std::vector<AutoPoint> lanePoints(MidiControl control, const TimedValues& values) {
    std::vector<AutoPoint> out;
    if (isSwitch(control)) {
        float last = -1;
        for (const auto& [t, v] : values) {
            float on = v >= 0.5f ? 1.f : 0.f;
            if (on != last)
                out.push_back({Tick(std::llround(t)), on, CurveType::Hold, 0});
            last = on;
        }
        return out;
    }
    std::vector<double> x;
    std::vector<float> y;
    for (const auto& [t, v] : values) {
        x.push_back(t);
        y.push_back(v);
    }
    for (size_t i : thinValues(x, y, kCurveTolerance, kMaxLanePoints))
        out.push_back({Tick(std::llround(x[i])), y[i], CurveType::Linear, 0});
    return out;
}

std::vector<size_t> thinValues(const std::vector<double>& ticks, const std::vector<float>& values, float tolerance,
                               size_t maxPoints) {
    size_t n = std::min(ticks.size(), values.size());
    if (n <= 2) {
        std::vector<size_t> all(n);
        for (size_t i = 0; i < n; i++)
            all[i] = i;
        return all;
    }
    maxPoints = std::max<size_t>(2, maxPoints);
    // Keep the point furthest off the line between two kept ones, until every point is close enough. Too many
    // points left: loosen the tolerance and try again.
    for (float tol = tolerance;; tol *= 2) {
        std::vector<bool> keep(n, false);
        keep[0] = keep[n - 1] = true;
        std::vector<std::pair<size_t, size_t>> spans{{0, n - 1}};
        while (!spans.empty()) {
            auto [a, b] = spans.back();
            spans.pop_back();
            float worst = 0;
            size_t at = a;
            for (size_t i = a + 1; i < b; i++) {
                double span = ticks[b] - ticks[a];
                double x = span > 0 ? (ticks[i] - ticks[a]) / span : 0;
                float line = values[a] + float(x) * (values[b] - values[a]);
                float off = std::fabs(values[i] - line);
                if (off > worst) {
                    worst = off;
                    at = i;
                }
            }
            if (worst > tol) {
                keep[at] = true;
                spans.push_back({a, at});
                spans.push_back({at, b});
            }
        }
        std::vector<size_t> out;
        for (size_t i = 0; i < n; i++)
            if (keep[i])
                out.push_back(i);
        if (out.size() <= maxPoints || tol > 1)
            return out;
    }
}

namespace {

/// The undo name of a take: "Record 5 notes", or "Record" when it holds only lane values.
std::string takeName(const Take& take) {
    return take.notes.empty() ? "Record" : "Record " + countNotes(take.notes.size());
}

} // namespace

CommandPtr recordNewClip(const Song& song, TrackId track, int slot, Tick length, const Take& take, const NextId& nextId,
                         ClipId* clipId, edits::IdSet* noteIds, const std::string& name) {
    const Track* t = song.findTrack(track);
    if (!t || slot < 0 || slot >= int(t->slots.size()) || t->slots[size_t(slot)].clip || length < 1)
        return nullptr;
    Clip clip;
    clip.id = ClipId(nextId());
    clip.name = newClipName(song);
    clip.color = t->color;
    clip.length = length;
    clip.loopEnd = length;
    for (const TakeNote& tn : take.notes) {
        clip.notes.push_back(toNote(tn, positiveMod(tn.start, length), nextId));
        if (noteIds)
            noteIds->insert(clip.notes.back().id);
    }
    clip.sortNotes();
    mergeTakeLanes(song, *t, clip.automation, take.lanes, 0, length, nextId);
    if (clipId)
        *clipId = clip.id;
    auto out = std::make_unique<CompoundCommand>(name.empty() ? takeName(take) : name);
    out->add(std::make_unique<AddClipToSlot>(track, slot, std::move(clip)));
    return out;
}

CommandPtr recordIntoClip(const Song& song, ClipId clipId, const Take& take, const NextId& nextId,
                          edits::IdSet* noteIds) {
    const Clip* clip = song.findClip(clipId);
    if (!clip || take.empty())
        return nullptr;
    ClipSpan span = clipSpan(*clip);
    std::vector<Note> added;
    for (const TakeNote& tn : take.notes) {
        Tick start = span.looping && span.length() > 0 ? span.start + positiveMod(tn.start - span.start, span.length())
                                                       : std::max<Tick>(0, tn.start);
        added.push_back(toNote(tn, start, nextId));
        if (noteIds)
            noteIds->insert(added.back().id);
    }
    auto out = std::make_unique<CompoundCommand>(takeName(take));
    if (!added.empty())
        out->add(std::make_unique<EditNotes>(takeName(take), clipId, std::vector<Note>{}, std::move(added)));
    if (const Track* track = trackOf(song, clipId)) {
        std::vector<AutomationEnvelope> lanes = clip->automation;
        mergeTakeLanes(song, *track, lanes, take.lanes, span.looping ? span.start : 0, span.looping ? span.length() : 0,
                       nextId);
        LaneOwner owner = LaneOwner::ofClip(clipId);
        for (size_t i = 0; i < lanes.size(); i++) {
            std::optional<AutomationEnvelope> before;
            if (i < clip->automation.size())
                before = clip->automation[i];
            if (!before || !(*before == lanes[i]))
                out->add(std::make_unique<EditLane>("Record", owner, int(i), before, lanes[i]));
        }
    }
    if (out->empty())
        return nullptr;
    return out;
}

CommandPtr recordToArrangement(const Song& song, TrackId track, Tick from, Tick to, const Take& take,
                               ArrangerRecordMode mode, const NextId& nextId, ClipId* clipId, edits::IdSet* noteIds) {
    const Track* t = song.findTrack(track);
    if (!t || to <= from || from < 0)
        return nullptr;
    Clip clip;
    clip.id = ClipId(nextId());
    clip.name = newClipName(song);
    clip.color = t->color;
    clip.length = to - from;
    clip.loopEnd = clip.length;

    for (const TakeNote& tn : take.notes) {
        if (tn.start < from || tn.start >= to)
            continue;
        clip.notes.push_back(toNote(tn, tn.start - from, nextId));
        if (noteIds)
            noteIds->insert(clip.notes.back().id);
    }
    clip.sortNotes();
    std::vector<TakeLane> lanes = take.lanes;
    for (TakeLane& l : lanes)
        for (AutoPoint& p : l.points)
            p.t -= from;
    mergeTakeLanes(song, *t, clip.automation, lanes, 0, 0, nextId);
    if (clipId)
        *clipId = clip.id;

    arrange::Placed placed{track, ArrangerClip{}};
    placed.clip.id = ArrangementId(nextId());
    placed.clip.clip = clip.id;
    placed.clip.position = from;
    placed.clip.length = clip.length;
    arrange::Layout before = arrange::layoutOf(song);
    arrange::Layout after = before;
    after.push_back(placed);
    // Stack keeps what was there as it was, under the take; Replace cuts it out.
    if (mode == ArrangerRecordMode::Replace)
        after = arrange::resolveOverlaps(after, {placed.clip.id}, song.clips, [&] { return ArrangementId(nextId()); });

    auto out = std::make_unique<CompoundCommand>(takeName(take));
    out->add(std::make_unique<AddClip>(std::move(clip)));
    out->add(arrange::replaceArrangement(song, "Record", before, after));
    return out;
}

namespace {

/// Where a timeline tick lands for Merge: the clip the top instance of the track plays there, its clip time, and
/// how fast the instance plays it. None where no clip plays.
struct MergeSpot {
    ClipId clip;
    double time;
    double stretch;
};

std::optional<MergeSpot> mergeSpot(const Song& song, const Track& track, double tick) {
    // Stacked instances: the one that starts last is on top, as the engine plays them.
    const ArrangerClip* top = nullptr;
    for (const ArrangerClip& a : track.arrangement)
        if (tick >= double(a.position) && tick < double(instanceEnd(a)) && (!top || a.position >= top->position))
            top = &a;
    const Clip* clip = top ? song.findClip(top->clip) : nullptr;
    if (!clip)
        return std::nullopt;
    ClipSpan span = clipSpan(*clip);
    double stretch = stretchOf(*top);
    double u = double(normalizeOffset(*clip, top->offset)) + (tick - double(top->position)) / stretch;
    // A clip without a loop takes notes past its last one: they lengthen what it plays.
    if (span.looping && span.length() > 0 && u >= double(span.end))
        u = double(span.start) + std::fmod(u - double(span.end), double(span.length()));
    return MergeSpot{clip->id, u, stretch};
}

} // namespace

CommandPtr mergeIntoArrangement(const Song& song, TrackId trackId, const Take& take, const NextId& nextId, Take& rest,
                                std::vector<std::pair<ClipId, edits::IdSet>>* noteIds) {
    rest = Take();
    const Track* track = song.findTrack(trackId);
    if (!track) {
        rest = take;
        return nullptr;
    }
    std::map<uint64_t, Take> into; // by ClipId value
    for (const TakeNote& tn : take.notes) {
        std::optional<MergeSpot> spot = mergeSpot(song, *track, double(tn.start));
        if (!spot) {
            rest.notes.push_back(tn);
            continue;
        }
        TakeNote n = tn;
        n.start = Tick(std::llround(spot->time));
        n.length = std::max<Tick>(1, Tick(std::llround(double(tn.length) / spot->stretch)));
        into[spot->clip.value].notes.push_back(std::move(n));
    }
    for (const TakeLane& tl : take.lanes) {
        std::map<uint64_t, TakeLane> parts;
        TakeLane outside{tl.control, tl.source, {}};
        for (const AutoPoint& p : tl.points) {
            std::optional<MergeSpot> spot = mergeSpot(song, *track, double(p.t));
            if (!spot) {
                outside.points.push_back(p);
                continue;
            }
            auto [it, added] = parts.try_emplace(spot->clip.value, TakeLane{tl.control, tl.source, {}});
            AutoPoint q = p;
            q.t = Tick(std::llround(spot->time));
            it->second.points.push_back(q);
        }
        for (auto& [clip, part] : parts)
            into[clip].lanes.push_back(std::move(part));
        if (!outside.points.empty())
            rest.lanes.push_back(std::move(outside));
    }
    if (into.empty())
        return nullptr;
    auto out = std::make_unique<CompoundCommand>(takeName(take));
    for (const auto& [clip, part] : into) {
        edits::IdSet ids;
        CommandPtr command = recordIntoClip(song, ClipId(clip), part, nextId, &ids);
        if (!command)
            continue;
        out->add(std::move(command));
        if (noteIds && !ids.empty())
            noteIds->push_back({ClipId(clip), std::move(ids)});
    }
    if (out->empty())
        return nullptr;
    return out;
}

edits::NoteList quantizeStarts(const edits::NoteList& notes, const edits::IdSet& ids, Tick grid, float strength) {
    edits::NoteList out = notes;
    if (grid <= 0)
        return out;
    for (Note& n : out) {
        if (!ids.count(n.id))
            continue;
        double target = std::round(double(n.start) / double(grid)) * double(grid);
        n.start = std::max<Tick>(0, Tick(std::llround(double(n.start) + (target - double(n.start)) * strength)));
    }
    std::sort(out.begin(), out.end(), noteOrder);
    return out;
}

CommandPtr quantizeTake(const Song& song, ClipId clipId, const edits::IdSet& ids, Tick grid, float strength) {
    const Clip* clip = song.findClip(clipId);
    if (!clip || grid <= 0 || ids.empty())
        return nullptr;
    return replaceNotes("Quantize recording", clipId, clip->notes, quantizeStarts(clip->notes, ids, grid, strength));
}

} // namespace tessera
