#include "Render.hpp"

#include "Arrangement.hpp"
#include "Automation.hpp"
#include "ChainRender.hpp"
#include "DeviceLookup.hpp"
#include "DeviceTypes.hpp"
#include "Devices.hpp"
#include "DrumKit.hpp"
#include "Json.hpp"
#include "Modulators.hpp"
#include "NoteOperators.hpp"
#include "Playheads.hpp"
#include "Scale.hpp"
#include "Swing.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace tessera::render {

using namespace tessera::json;

namespace {

const char* const kTimingNames[] = {"written", "played"};
const char* const kOperatorNames[] = {"written", "rendered"};
const char* const kAutomationNames[] = {"off", "cc", "cc14"};
const char* const kExpressionNames[] = {"off", "mpe", "flatten"};
const char* const kRangeNames[] = {"song", "loop", "selection"};
const char* const kSplitNames[] = {"one", "track", "clip"};
const char* const kDeviceNames[] = {"applied", "bypassed"};

template <typename E, size_t N>
E named(const json_t* j, const char* key, const char* const (&names)[N], E fallback) {
    std::string s = getString(j, key, "");
    for (size_t i = 0; i < N; i++)
        if (s == names[i])
            return E(i);
    return fallback;
}

constexpr int kTimbreCc = 74;
constexpr int kMasterChannel = 0; // MPE lower zone: channel 1 is the master, 2..16 the members
constexpr int kMembers = 15;

/// A note as exported, before the range and the expression handling.
struct Played {
    double start, end;
    int pitch;
    float velocity, release;
    const Note* source; // for expression curves; null when there is none
    int64_t pass = 0;   // for the devices' randomness: which time through its clip, and where it was written
    double origin = 0;
    uint8_t key = 0; // 0 = the song's key, k = the clip key k - 1
    uint8_t channel = 1;
    uint8_t lane = 0; // the playhead that read it (1..16), for its branch
};

/// The channel a clip note plays on; 1 for notes made by the export itself.
uint8_t noteChannel(const Note* note) { return note ? note->channel : 1; }

Tick songEnd(const Song& song) {
    Tick end = 0;
    for (const Track& t : song.tracks)
        for (const ArrangerClip& a : t.arrangement)
            end = std::max(end, instanceEnd(a));
    return end;
}

/// A note on its way to the file: where it came from, before timing and operators.
struct Item {
    TimedNote note;
    ClipId clip;
    uint8_t key = 0;     // 0 = the song's key, k = the clip key k - 1
    uint8_t channel = 0; // 0 = the clip note's own
    uint8_t lane = 0;    // the playhead that read it (1..16)
};

/// A playhead's settings at `songTick` as the engine plays them: its knobs plus the modulators that reach them and
/// move with the song alone (synced waves and curves, steps, random, ramps, macros). Free-running ones and those that
/// follow notes are left out: a file has no wall clock or live notes to follow.
void playheadKnobsAt(const Song& song, const Track& track, const Device& device, const Playhead& p, double songTick,
                     float* out) {
    std::copy(p.params.begin(), p.params.end(), out);
    mods::Clock clock;
    clock.songTick = songTick;
    clock.timeSig = song.timeSig;
    clock.seed = song.seed;
    auto add = [&](const std::vector<Modulator>& list) {
        for (const Modulator& m : list) {
            const float* k = m.params.data();
            auto shown = [&](int i) {
                return devices::display(devices::modulatorInfo(m.type).params[size_t(i)], k[i]);
            };
            bool songOnly =
                m.type == ModulatorType::Curve ||
                (m.type == ModulatorType::Lfo && shown(mods::knob::kSync) > 0.5f &&
                 shown(mods::knob::kRestart) < 1.5f) ||
                ((m.type == ModulatorType::Steps || m.type == ModulatorType::Ramp) && !mods::perPass(m.type, k)) ||
                m.type == ModulatorType::Random || m.type == ModulatorType::Macro;
            if (!songOnly)
                continue;
            mods::State state;
            mods::Drawn drawn{m.points.data(), int(m.points.size()), m.length};
            float v = mods::evaluate(m.type, m.id.value, k, state, clock, m.steps.data(), int(m.steps.size()), &drawn);
            for (const Modulation& r : m.routes)
                if (r.kind == Modulation::Kind::Param && r.target.kind == ParamRef::Kind::Playhead &&
                    r.target.owner == p.id.value && r.target.param < p.params.size())
                    out[r.target.param] += r.amount * v;
        }
    };
    add(song.modulators);
    add(track.modulators);
    add(device.modulators);
    for (size_t i = 0; i < p.params.size(); i++)
        out[i] = std::clamp(out[i], 0.f, 1.f);
}

/// How a Motion device before the playheads moves its notes on `pass` of the clip: its knobs, with the "Per pass"
/// modulators routed to them stepped to that pass, as the engine reads them. With `songTick`, Curves that count in
/// time add their value at that moment too.
struct MovedBy {
    int steps = 0;
    bool inKey = true;
    double delay = 0;
    double lengthScale = 1;
    float velocity = 0;
};

MovedBy movedAt(const Song& song, const Track& track, const Device& motion, int64_t pass,
                double songTick = std::numeric_limits<double>::quiet_NaN()) {
    std::vector<float> knobs = motion.params;
    auto add = [&](const std::vector<Modulator>& list) {
        for (const Modulator& m : list) {
            bool passes = mods::perPass(m.type, m.params.data());
            bool timed = !passes && m.type == ModulatorType::Curve && std::isfinite(songTick);
            if (!passes && !timed)
                continue;
            mods::Drawn drawn{m.points.data(), int(m.points.size()), m.length};
            float value = 0;
            if (passes) {
                value = mods::passValue(m.type, m.id.value, m.params.data(), pass, song.timeSig, m.steps.data(),
                                        int(m.steps.size()), &drawn);
            } else {
                mods::State state;
                mods::Clock clock;
                clock.songTick = songTick;
                clock.timeSig = song.timeSig;
                value = mods::evaluate(m.type, m.id.value, m.params.data(), state, clock, nullptr, 0, &drawn);
            }
            for (const Modulation& r : m.routes)
                if (r.kind == Modulation::Kind::Param && r.target.kind == ParamRef::Kind::Device &&
                    r.target.owner == motion.id.value && r.target.param < knobs.size())
                    knobs[r.target.param] += r.amount * value;
        }
    };
    add(song.modulators);
    add(track.modulators);
    for (const Device& d : track.devices)
        add(d.modulators);
    auto value = [&](int k) {
        return devices::display(devices::motionInfo().params[size_t(k)], std::clamp(knobs[size_t(k)], 0.f, 1.f));
    };
    MovedBy out;
    out.steps = int(std::lround(value(chain::knob::kMotionPitch)));
    out.inKey = value(chain::knob::kMotionInKey) > 0.5f;
    out.delay = double(value(chain::knob::kMotionTime)) * double(kPpq / 4);
    out.lengthScale = 1 + double(value(chain::knob::kMotionLength)) / 100;
    out.velocity = value(chain::knob::kMotionVelocity) / 100;
    return out;
}

/// The Motion devices before a track's Playheads device (switched on): the playheads read their notes moved.
std::vector<const Device*> readMotions(const Track& track) {
    std::vector<const Device*> out;
    for (const Device& d : track.devices) {
        if (d.type == DeviceType::Playheads)
            return out;
        if (d.type == DeviceType::Motion && d.on)
            out.push_back(&d);
    }
    return {};
}

/// The items with the chosen timing and operators applied, sorted by start. Operators run exactly as the engine runs
/// them: the same seed, clip and pass give the same result, and "previous note" restarts with each pass.
std::vector<Played> applyOperators(const Song& song, const Track& track, const Options& o, std::vector<Item>& items) {
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.note.start < b.note.start; });
    std::vector<Played> out;
    int lastPass = -1;
    bool previousPlayed = true;
    Tick beat = ticksPerBeat(song.timeSig);
    for (const Item& item : items) {
        const TimedNote& n = item.note;
        const Note* src = n.source;
        uint8_t channel = item.channel > 0 ? item.channel : noteChannel(src);
        double start = double(n.start), end = double(n.end);
        if (o.timing == Timing::Played) {
            double micro = src ? operators::microTicks(*src) : 0;
            // With devices bypassed a Groove device plays nothing, so the song's swing stands in for it.
            float swing = o.devices == DeviceMode::Applied ? songSwingFor(song, track) : song.swing;
            start = swung(start, swing, song.swingStep) + micro;
            end = swung(end, swing, song.swingStep) + micro;
        }
        int pitch = n.pitch;
        float velocity = n.velocity;
        if (o.operators == OperatorMode::Written || !src) {
            out.push_back({start, end, pitch, velocity, n.releaseVelocity, src, n.pass, double(n.start), item.key,
                           channel, item.lane});
            continue;
        }
        if (n.pass != lastPass) {
            lastPass = n.pass;
            previousPlayed = true;
        }
        operators::PassContext ctx;
        ctx.seed = song.seed;
        ctx.clip = item.clip.value;
        ctx.pass = n.pass;
        ctx.previousPlayed = previousPlayed;
        bool plays = operators::plays(*src, ctx);
        previousPlayed = plays;
        if (!plays)
            continue;
        double offset = operators::timingOffset(*src, ctx);
        float spread = operators::velocity(*src, ctx) - src->velocity;
        int count = operators::hitCount(*src, beat);
        double length = end - start;
        double scale = src->length > 0 ? length / double(src->length) : 1;
        for (int i = 0; i < count; i++) {
            operators::Hit h = operators::hit(*src, i, count);
            double hitStart = start + offset + h.start * scale;
            double hitEnd = count > 1 ? hitStart + h.length * scale : hitStart + length;
            float shape = src->velocity > 0 ? h.velocity / src->velocity : 1;
            float v = std::clamp(velocity * shape + spread, 0.f, 1.f);
            out.push_back({std::max(0.0, hitStart), hitEnd, pitch, v, n.releaseVelocity, src, n.pass,
                           double(n.start) + i, item.key, channel, item.lane});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Played& a, const Played& b) { return a.start < b.start; });
    return out;
}

/// The key index of a clip with its own key (added to `keys` the first time), or 0 for the song's.
uint8_t keyIndexOf(const Song& song, const Clip& clip, std::vector<Scale>& keys, std::map<ClipId, uint8_t>& keyOf) {
    if (!clip.scaleOverride || keys.size() >= 255)
        return 0;
    auto it = keyOf.find(clip.id);
    if (it != keyOf.end())
        return it->second;
    keys.push_back(resolveScale(*clip.scaleOverride, song.customScales));
    return keyOf[clip.id] = uint8_t(keys.size());
}

/// Every note `t` plays on the timeline, with the chosen timing and operators applied. Clips with their own key
/// add it to `keys`.
std::vector<Played> playedNotes(const Song& song, const Track& t, const Options& o, std::vector<Scale>& keys) {
    std::map<ClipId, uint8_t> keyOf;
    std::vector<Item> items;
    for (const ArrangerClip& a : t.arrangement) {
        if (a.muted && !o.includeMuted)
            continue;
        const Clip* clip = song.findClip(a.clip);
        if (!clip)
            continue;
        std::vector<TimedNote> notes;
        renderInstance(*clip, a, notes, t.transpose, t.gateLengthScale,
                       t.type == TrackType::Drum ? &t.drumLanes : nullptr);
        uint8_t key = keyIndexOf(song, *clip, keys, keyOf);
        for (const TimedNote& n : notes)
            items.push_back({n, a.clip, key});
    }
    return applyOperators(song, t, o, items);
}

/// What the track's playheads play from its timeline clips (docs/design/PLAYHEADS.md), with the code the engine
/// uses: each timeline clip read from where it begins the clip, slower when stretched.
std::vector<Played> playheadNotes(const Song& song, const Track& t, const Device& device, const Options& o,
                                  std::vector<Scale>& keys) {
    struct Readable {
        double start, end;
        const Note* source;
        int pitch;
        uint8_t role = 3; // lowest and/or highest of the notes starting with it
    };
    std::map<ClipId, uint8_t> keyOf;
    bool once =
        std::lround(devices::display(devices::deviceInfo(DeviceType::Playheads).params[playheads::knob::kSameNote],
                                     device.params[playheads::knob::kSameNote])) == 1;
    bool solo = std::any_of(device.playheads.begin(), device.playheads.end(),
                            [](const Playhead& p) { return p.solo && !p.mute; });
    std::vector<Item> items;
    for (const ArrangerClip& a : t.arrangement) {
        if (a.muted && !o.includeMuted)
            continue;
        const Clip* clip = song.findClip(a.clip);
        if (!clip)
            continue;
        uint8_t key = keyIndexOf(song, *clip, keys, keyOf);
        const std::vector<DrumLane>* drumLanes = t.type == TrackType::Drum ? &t.drumLanes : nullptr;
        // The region is the whole span (a drum clip's lane cycle); the pattern and each lane with its own length
        // repeat inside it, as in the engine.
        ClipSpan span = clipSpan(*clip, drumLanes);
        double playStart = double(span.start), playEnd = double(span.end);
        double patternEnd = playStart + double(span.pattern);
        int transpose = t.type == TrackType::Drum ? 0 : t.transpose + a.transpose;
        struct Group {
            double base, period;
            std::vector<Readable> notes;
            std::vector<uint32_t> byEnd;
            const Device* motion = nullptr; // a Motion device before the playheads: its notes, read moved
            bool repeats = true;            // false: read where written on the canvas (base and period unused)
        };
        std::vector<Group> groups;
        std::vector<int> ownLoopPitches;
        auto readable = [&](const Note& n, double end) {
            return Readable{double(n.start),
                            std::min(double(n.start) + std::max(1.0, double(n.length) * t.gateLengthScale), end), &n,
                            std::clamp(n.pitch + transpose, 0, 127)};
        };
        if (drumLanes)
            for (const DrumLane& lane : *drumLanes) {
                if (!hasOwnLoop(lane, *clip))
                    continue;
                LaneRegion region = laneRegion(lane, *clip);
                Group g{double(region.start), double(region.length), {}, {}};
                for (const Note& n : clip->notes)
                    if (!n.muted && n.pitch == lane.pitch && n.start >= region.start &&
                        n.start < region.start + region.length)
                        g.notes.push_back(readable(n, double(region.start + region.length)));
                groups.push_back(std::move(g));
                ownLoopPitches.push_back(lane.pitch);
            }
        // The piano roll is a canvas: the notes not on a lane of their own are read where they are written, uncut,
        // unless they repeat inside a longer lane cycle.
        Group main{playStart, double(span.pattern), {}, {}};
        main.repeats = span.cycles();
        for (const Note& n : clip->notes)
            if (!n.muted && (!main.repeats || (n.start >= playStart && n.start < patternEnd)) &&
                std::find(ownLoopPitches.begin(), ownLoopPitches.end(), n.pitch) == ownLoopPitches.end())
                main.notes.push_back(readable(n, main.repeats ? patternEnd : std::numeric_limits<double>::max()));
        groups.push_back(std::move(main));
        int lowest = 127, highest = 0;
        for (Group& g : groups) {
            g.byEnd = playheads::orderByEnd(g.notes);
            playheads::markChordRoles(g.notes, [](Readable& n) -> uint8_t& { return n.role; });
            for (const Readable& n : g.notes) {
                lowest = std::min(lowest, n.pitch);
                highest = std::max(highest, n.pitch);
            }
        }
        // Notes of a Motion device before the playheads leave the main list for their own, read shifted by its Time
        // (chord roles stay as the whole clip gives them).
        for (const Device* motion : readMotions(t)) {
            Group moved{groups.back().base, groups.back().period, {}, {}, motion, groups.back().repeats};
            std::vector<Readable>& rest = groups.back().notes;
            for (auto it = rest.begin(); it != rest.end();)
                if (it->source && it->source->motion == motion->id) {
                    moved.notes.push_back(*it);
                    it = rest.erase(it);
                } else {
                    ++it;
                }
            if (moved.notes.empty())
                continue;
            groups.back().byEnd = playheads::orderByEnd(rest);
            moved.byEnd = playheads::orderByEnd(moved.notes);
            groups.insert(groups.end() - 1, std::move(moved));
        }
        Scale scale = clip->scaleOverride ? resolveScale(*clip->scaleOverride, song.customScales)
                                          : resolveScale(song.scale, song.customScales);

        double stretch = a.stretch > 0 ? double(a.stretch) : 1.0;
        double begin = double(normalizeOffset(*clip, a.offset)) - double(clipSpan(*clip).start);
        double length = double(instanceEnd(a) - a.position) / stretch;
        for (size_t index = 0; index < device.playheads.size(); index++) {
            const Playhead& p = device.playheads[index];
            if (p.mute || (solo && !p.solo))
                continue;
            // Small steps, so no window needs more sweeps than one call gives; each step reads the modulators anew.
            constexpr double kStep = double(kPpq) / 8;
            std::vector<float> knobs(p.params.size());
            for (double e = begin; e < begin + length; e += kStep) {
                double to = std::min(begin + length, e + kStep);
                playheadKnobsAt(song, t, device, p, double(a.position) + (e - begin) * stretch, knobs.data());
                playheads::Motion m = playheads::motionFrom(knobs.data(), device.params.data(), playStart,
                                                            playEnd - playStart, song.timeSig);
                if (playheads::wantsDrawnPath(knobs.data()))
                    m.path = &p.path;
                playheads::NoteShape shape = playheads::noteShapeFrom(knobs.data());
                playheads::Sweep sweeps[playheads::kMaxSweeps];
                int count = playheads::sweeps(m, e, to, sweeps);
                for (int i = 0; i < count; i++)
                    for (const Group& g : groups) {
                        MovedBy moved;
                        if (g.motion) // its pass is the clip's
                            moved = movedAt(song, t, *g.motion,
                                            int64_t(std::floor(e / std::max(1.0, playEnd - playStart))));
                        auto onHit = [&](const Readable& n, double pos) {
                            if (!playheads::picked(shape.pick, n.role))
                                return;
                            double elapsed = playheads::timeAt(m, sweeps[i], pos);
                            double at = double(a.position) + (elapsed - begin) * stretch;
                            double len = playheads::noteLength(knobs.data(), m, n.end - n.start, song.timeSig) *
                                         stretch * std::max(0.01, moved.lengthScale);
                            // The pass as the engine counts it: the playhead's own time over its region, its place
                            // mixed in.
                            double own = m.rate * (m.restartEvery > 0 ? std::fmod(elapsed, m.restartEvery) : elapsed);
                            int pass = int(std::floor(own / std::max(1.0, m.regionLength))) * 16 + int(index);
                            Tick start = Tick(std::llround(at));
                            int written = n.pitch;
                            if (moved.steps != 0)
                                written = std::clamp(
                                    moved.inKey ? scale.stepBy(written, moved.steps) : written + moved.steps, 0, 127);
                            int pitch = playheads::shapedPitch(shape, written, lowest, highest, scale);
                            TimedNote note{start,
                                           std::max(start + 1, Tick(std::llround(at + len))),
                                           int8_t(pitch),
                                           std::clamp(n.source->velocity + shape.velocity + moved.velocity, 0.f, 1.f),
                                           n.source->releaseVelocity,
                                           n.source,
                                           pass};
                            items.push_back({note, a.clip, key, p.channel, uint8_t(index + 1)});
                        };
                        if (g.repeats)
                            playheads::crossedRepeating(sweeps[i], playStart, g.base - moved.delay, g.period, g.notes,
                                                        g.byEnd, onHit);
                        else
                            playheads::crossedShifted(sweeps[i], moved.delay, g.notes, g.byEnd, onHit);
                    }
            }
        }
    }
    std::vector<Played> out = applyOperators(song, t, o, items);
    if (once) {
        // Two playheads on one note at the same moment play it once, as in the engine.
        std::vector<Played> kept;
        for (const Played& n : out)
            if (std::none_of(kept.begin(), kept.end(), [&](const Played& k) {
                    return k.pitch == n.pitch && k.channel == n.channel && std::abs(k.start - n.start) < 1e-6;
                }))
                kept.push_back(n);
        out = std::move(kept);
    }
    return out;
}

/// The notes a track plays before its device chain: its clips as written, or what its playheads read from them when
/// devices are applied.
std::vector<Played> sourceNotes(const Song& song, const Track& t, const Options& o, std::vector<Scale>& keys) {
    if (o.devices == DeviceMode::Applied)
        if (const Device* device = activePlayheads(t))
            return playheadNotes(song, t, *device, o, keys);
    return playedNotes(song, t, o, keys);
}

/// The notes a track's device chain plays from `notes` (docs/DEVICES.md §6), with the same code as playback.
std::vector<Played> throughChain(const Song& song, const Track& t, const std::vector<Played>& notes, double until,
                                 double tempo, const std::vector<Scale>& keys) {
    std::vector<chainrender::Note> in;
    in.reserve(notes.size());
    for (const Played& p : notes)
        in.push_back(
            {p.start, p.end, p.pitch, p.velocity, p.release, p.source, p.pass, p.origin, p.key, p.channel, p.lane});
    std::vector<Played> out;
    for (const chainrender::Note& n : chainrender::run(song, t, in, until, tempo, keys))
        out.push_back({n.start, n.end, n.pitch, n.velocity, n.release, n.source, n.pass, n.origin, n.key, n.channel});
    return out;
}

int bend14(float semitones, int range) {
    return std::clamp(int(std::lround(double(semitones) / double(std::max(1, range)) * 8192)), -8192, 8191);
}

/// Samples a note's curve from `s` to `e` (export ticks) every `step`, sending `send(tick, value)` when it changes.
template <typename Send>
void sampleCurve(const std::vector<ExprPoint>& curve, float rest, Tick s, Tick e, Tick step, bool always, Send send) {
    if (curve.empty() && !always)
        return;
    int last = INT32_MIN;
    for (Tick t = s; t < e; t += std::max<Tick>(1, step)) {
        float pos = e > s ? float(double(t - s) / double(e - s)) : 0.f;
        int v = send(t, expressionAt(curve, pos, rest), last);
        last = v;
    }
}

void emitNotes(smf::Track& out, const std::vector<Played>& notes, int ch, Tick from, Tick to, const Options& o) {
    std::array<Tick, 16> busyUntil{};
    std::array<Tick, 16> lastUsed{};
    if (o.expressions == ExpressionMode::Mpe) {
        // MPE configuration: a lower zone of 15 members, then each member's pitch bend range (RPN 0).
        auto rpn = [&](int c, int lsb, int value) {
            out.events.push_back(smf::controlChange(0, c, 101, 0));
            out.events.push_back(smf::controlChange(0, c, 100, lsb));
            out.events.push_back(smf::controlChange(0, c, 6, value));
            out.events.push_back(smf::controlChange(0, c, 38, 0));
            out.events.push_back(smf::controlChange(0, c, 101, 127));
            out.events.push_back(smf::controlChange(0, c, 100, 127));
        };
        rpn(kMasterChannel, 6, kMembers);
        for (int c = 1; c <= kMembers; c++)
            rpn(c, 0, o.bendRange);
    }
    for (const Played& n : notes) {
        if (n.start < double(from) || n.start >= double(to))
            continue;
        Tick s = Tick(std::llround(n.start)) - from;
        Tick e = std::max(s + 1, Tick(std::llround(std::min(n.end, double(to)))) - from);
        // MPE hands out its own channels; otherwise each note may keep its own.
        int c = o.noteChannels ? std::clamp(int(n.channel), 1, 16) - 1 : ch;
        if (o.expressions == ExpressionMode::Mpe) {
            // A free member channel, the one used longest ago; with none free, the one that frees first.
            int best = -1;
            for (int m = 1; m <= kMembers; m++)
                if (busyUntil[size_t(m)] <= s && (best < 0 || lastUsed[size_t(m)] < lastUsed[size_t(best)]))
                    best = m;
            if (best < 0) {
                best = 1;
                for (int m = 2; m <= kMembers; m++)
                    if (busyUntil[size_t(m)] < busyUntil[size_t(best)])
                        best = m;
            }
            c = best;
            busyUntil[size_t(c)] = e;
            lastUsed[size_t(c)] = s + 1;
        }
        const Note* src = n.source;
        static const std::vector<ExprPoint> none;
        const std::vector<ExprPoint>& pitchCurve = src ? src->pitchExpr : none;
        const std::vector<ExprPoint>& timbreCurve = src ? src->timbreExpr : none;
        const std::vector<ExprPoint>& pressureCurve = src ? src->pressureExpr : none;
        if (o.expressions == ExpressionMode::Mpe) {
            // Every note sets its channel's bend, timbre and pressure first, so nothing carries over from the
            // channel's last note.
            sampleCurve(pitchCurve, 0, s, e, o.ccStep, true, [&](Tick t, float v, int last) {
                int b = bend14(v, o.bendRange);
                if (b != last)
                    out.events.push_back(smf::pitchBend(t, c, b));
                return b;
            });
            sampleCurve(timbreCurve, 0, s, e, o.ccStep, true, [&](Tick t, float v, int last) {
                int x = smf::value7(v);
                if (x != last)
                    out.events.push_back(smf::controlChange(t, c, kTimbreCc, x));
                return x;
            });
            sampleCurve(pressureCurve, 0, s, e, o.ccStep, true, [&](Tick t, float v, int last) {
                int x = smf::value7(v);
                if (x != last)
                    out.events.push_back(smf::channelPressure(t, c, x));
                return x;
            });
        } else if (o.expressions == ExpressionMode::Flatten) {
            sampleCurve(timbreCurve, 0, s, e, o.ccStep, false, [&](Tick t, float v, int last) {
                int x = smf::value7(v);
                if (x != last)
                    out.events.push_back(smf::controlChange(t, c, kTimbreCc, x));
                return x;
            });
            sampleCurve(pressureCurve, 0, s, e, o.ccStep, false, [&](Tick t, float v, int last) {
                int x = smf::value7(v);
                if (x != last)
                    out.events.push_back(smf::polyPressure(t, c, n.pitch, x));
                return x;
            });
        }
        out.events.push_back(smf::noteOn(s, c, n.pitch, smf::velocity7(n.velocity)));
        out.events.push_back(smf::noteOff(e, c, n.pitch, smf::value7(n.release)));
    }
}

/// One AUTO channel's value at a song tick, as the engine combines track and clip lanes; nothing when no lane
/// covers it.
std::optional<float> laneValue(const Song& song, const Track& t, int output, const Options& o, Tick tick) {
    std::optional<float> v;
    for (const AutomationEnvelope& lane : t.automation)
        if (drivesOutput(lane) && lane.outputIndex == output && !lane.mute && !lane.points.empty()) {
            v = evaluate(lane, double(tick));
            break;
        }
    const ArrangerClip* inst = instanceAt(t, tick);
    if (!inst || (inst->muted && !o.includeMuted))
        return v;
    const Clip* clip = song.findClip(inst->clip);
    if (!clip)
        return v;
    for (const AutomationEnvelope& lane : clip->automation) {
        if (!drivesOutput(lane) || lane.outputIndex != output || lane.mute || lane.points.empty())
            continue;
        std::optional<double> ct = clipTimeAt(*clip, *inst, double(tick));
        if (!ct)
            break;
        float cv = evaluate(lane, *ct);
        if (v && lane.combine == CombineMode::Modulate)
            v = std::clamp(*v + cv - 0.5f, 0.f, 1.f);
        else
            v = cv;
        break;
    }
    return v;
}

std::vector<std::pair<Tick, float>> samplesOf(const Song& song, const Track& t, int output, const Options& o, Tick from,
                                              Tick to) {
    std::vector<std::pair<Tick, float>> out;
    bool any = false;
    for (const AutomationEnvelope& lane : t.automation)
        any |= drivesOutput(lane) && lane.outputIndex == output && !lane.mute && !lane.points.empty();
    for (const ArrangerClip& a : t.arrangement)
        if (const Clip* clip = song.findClip(a.clip))
            for (const AutomationEnvelope& lane : clip->automation)
                any |= drivesOutput(lane) && lane.outputIndex == output && !lane.mute && !lane.points.empty();
    if (!any)
        return out;
    // Sample on a grid, plus at every track lane point so corners land exactly.
    std::vector<Tick> ticks;
    for (Tick tick = from; tick < to; tick += std::max<Tick>(1, o.ccStep))
        ticks.push_back(tick);
    for (const AutomationEnvelope& lane : t.automation)
        if (drivesOutput(lane) && lane.outputIndex == output)
            for (const AutoPoint& p : lane.points)
                if (p.t >= from && p.t < to)
                    ticks.push_back(p.t);
    std::sort(ticks.begin(), ticks.end());
    ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
    for (Tick tick : ticks) {
        std::optional<float> v = laneValue(song, t, output, o, tick);
        if (v && (out.empty() || out.back().second != *v))
            out.push_back({tick, *v});
    }
    return out;
}

void emitAutomation(smf::Track& out, const Song& song, const Track& t, int ch, Tick from, Tick to, const Options& o) {
    if (o.automation == AutomationMode::Off)
        return;
    for (int output = 0; output < kLanes; output++) {
        int cc = std::clamp(o.laneCc[size_t(output)], 0, 119);
        int last = -1;
        for (const auto& [tick, v] : samplesOf(song, t, output, o, from, to)) {
            if (o.automation == AutomationMode::Cc14 && cc < 32) {
                int x = std::clamp(int(std::lround(double(v) * 16383)), 0, 16383);
                if (x == last)
                    continue;
                out.events.push_back(smf::controlChange(tick - from, ch, cc, x >> 7));
                out.events.push_back(smf::controlChange(tick - from, ch, cc + 32, x & 0x7F));
                last = x;
            } else {
                int x = smf::value7(v);
                if (x == last)
                    continue;
                out.events.push_back(smf::controlChange(tick - from, ch, cc, x));
                last = x;
            }
        }
    }
}

/// A song with only `clip` on its track, once from 0: how a clip exports on its own.
Song soloSong(const Song& song, const Track& t, const Clip& clip) {
    Song solo = song;
    Track one = t;
    ArrangerClip a;
    a.id = ArrangementId(1);
    a.clip = clip.id;
    a.position = 0;
    a.length = std::max<Tick>(1, clipSpan(clip, t.type == TrackType::Drum ? &t.drumLanes : nullptr).end);
    one.arrangement = {a};
    one.automation.clear();
    one.mute = false;
    solo.tracks = {one};
    solo.markers.clear();
    solo.loopStart = solo.loopEnd = 0;
    return solo;
}

smf::Track conductor(const Song& song, const Context& ctx, Tick from, Tick to) {
    smf::Track c;
    c.events.push_back(smf::trackName(0, ctx.title.empty() ? "Tessera" : ctx.title));
    c.events.push_back(smf::tempo(0, ctx.tempo));
    c.events.push_back(smf::timeSignature(0, song.timeSig.num, song.timeSig.den));
    for (const Marker& m : song.markers)
        if (m.position >= from && m.position < to)
            c.events.push_back(smf::marker(m.position - from, m.name.empty() ? "Marker" : m.name));
    if (song.loopEnd > song.loopStart && song.loopStart >= from && song.loopEnd <= to) {
        c.events.push_back(smf::cuePoint(song.loopStart - from, "Loop start"));
        c.events.push_back(smf::cuePoint(song.loopEnd - from, "Loop end"));
    }
    return c;
}

} // namespace

std::vector<int> heardPitches(const Song& song, const Track& track, const Device& device, const Playhead& p,
                              const Clip& clip) {
    struct Readable {
        double start, end;
        int pitch;
        uint8_t role = 0;
        size_t index;
    };
    std::vector<int> heard(clip.notes.size(), -1);
    double playStart = double(clip.playStart()), playEnd = double(clip.playEnd());
    if (playEnd <= playStart)
        return heard;
    std::vector<Readable> notes;
    int lowest = 127, highest = 0;
    for (size_t i = 0; i < clip.notes.size(); i++) {
        const Note& n = clip.notes[i];
        if (n.muted)
            continue;
        notes.push_back({double(n.start), double(n.start + n.length), n.pitch, 0, i});
        lowest = std::min<int>(lowest, n.pitch);
        highest = std::max<int>(highest, n.pitch);
    }
    std::vector<uint32_t> byEnd = playheads::orderByEnd(notes);
    playheads::markChordRoles(notes, [](Readable& n) -> uint8_t& { return n.role; });
    Scale scale = clip.scaleOverride ? resolveScale(*clip.scaleOverride, song.customScales)
                                     : resolveScale(song.scale, song.customScales);
    constexpr double kStep = double(kPpq) / 8;
    std::vector<float> knobs(p.params.size());
    std::vector<const Device*> before = readMotions(track);
    for (double e = 0; e < playEnd - playStart; e += kStep) {
        playheadKnobsAt(song, track, device, p, e, knobs.data());
        playheads::Motion m =
            playheads::motionFrom(knobs.data(), device.params.data(), playStart, playEnd - playStart, song.timeSig);
        if (playheads::wantsDrawnPath(knobs.data()))
            m.path = &p.path;
        playheads::NoteShape shape = playheads::noteShapeFrom(knobs.data());
        playheads::Sweep sweeps[playheads::kMaxSweeps];
        int count = playheads::sweeps(m, e, e + kStep, sweeps);
        for (int i = 0; i < count; i++)
            playheads::crossed(sweeps[i], notes, byEnd, [&](const Readable& n, double) {
                if (heard[n.index] >= 0 || !playheads::picked(shape.pick, n.role))
                    return;
                // A Motion device before the playheads moves the note's pitch first (its first pass).
                int pitch = n.pitch;
                for (const Device* motion : before)
                    if (clip.notes[n.index].motion == motion->id) {
                        MovedBy moved = movedAt(song, track, *motion, 0);
                        if (moved.steps != 0)
                            pitch = std::clamp(moved.inKey ? scale.stepBy(pitch, moved.steps) : pitch + moved.steps, 0,
                                               127);
                    }
                heard[n.index] = playheads::shapedPitch(shape, pitch, lowest, highest, scale);
            });
    }
    return heard;
}

std::pair<Tick, Tick> rangeOf(const Song& song, const Options& o) {
    switch (o.range) {
    case RangeMode::Loop:
        if (song.loopEnd > song.loopStart)
            return {song.loopStart, song.loopEnd};
        break;
    case RangeMode::Selection:
        if (o.to > o.from)
            return {std::max<Tick>(0, o.from), o.to};
        break;
    default:
        break;
    }
    return {0, songEnd(song)};
}

int channelOf(const Song& song, const Options& o, TrackId track) {
    auto it = o.channels.find(track.value);
    if (it != o.channels.end() && it->second >= 1 && it->second <= 16)
        return it->second;
    int n = 0;
    for (const Track& t : song.tracks) {
        if (t.id == track) {
            if (t.type == TrackType::Drum)
                return 10;
            break;
        }
        if (t.type == TrackType::Instrument)
            n++;
    }
    int ch = n % 15 + 1;           // 1..15 in order...
    return ch >= 10 ? ch + 1 : ch; // ...skipping the drum channel
}

smf::File renderSong(const Song& song, const Options& o, const Context& ctx, const std::vector<TrackId>& tracks) {
    auto [from, to] = rangeOf(song, o);
    smf::File file;
    file.ppq = int(kPpq);
    file.tracks.push_back(conductor(song, ctx, from, to));
    for (const Track& t : song.tracks) {
        if (!tracks.empty() && std::find(tracks.begin(), tracks.end(), t.id) == tracks.end())
            continue;
        if (t.mute && !o.includeMuted)
            continue;
        int ch = channelOf(song, o, t.id) - 1;
        smf::Track out;
        out.events.push_back(smf::trackName(0, t.name));
        std::vector<Scale> keys;
        std::vector<Played> played = sourceNotes(song, t, o, keys);
        if (o.devices == DeviceMode::Applied)
            played = throughChain(song, t, played, double(to), ctx.tempo, keys);
        emitNotes(out, played, ch, from, to, o);
        emitAutomation(out, song, t, ch, from, to, o);
        file.tracks.push_back(std::move(out));
    }
    return file;
}

smf::File renderClip(const Song& song, const Options& o, const Context& ctx, TrackId track, ClipId clipId) {
    const Track* t = song.findTrack(track);
    const Clip* clip = song.findClip(clipId);
    if (!t || !clip) {
        smf::File empty;
        empty.tracks.push_back(conductor(song, ctx, 0, 0));
        return empty;
    }
    // The clip on its own timeline: one instance at 0, as long as one pass, with the track's settings.
    Song solo = soloSong(song, *t, *clip);
    Options clipOptions = o;
    clipOptions.range = RangeMode::Song;
    clipOptions.includeMuted = true;
    // The channel the track has in the whole song.
    clipOptions.channels[track.value] = channelOf(song, o, track);
    return renderSong(solo, clipOptions, ctx);
}

std::vector<ExportNote> trackNotes(const Song& song, TrackId track, const Options& o, const Context& ctx) {
    std::vector<ExportNote> out;
    const Track* t = song.findTrack(track);
    if (!t)
        return out;
    std::vector<Scale> keys;
    std::vector<Played> played = sourceNotes(song, *t, o, keys);
    if (o.devices == DeviceMode::Applied)
        played = throughChain(song, *t, played, double(songEnd(song)), ctx.tempo, keys);
    for (const Played& p : played)
        out.push_back({p.start, p.end, p.pitch, p.velocity, p.release, p.channel});
    return out;
}

std::vector<ExportNote> clipNotes(const Song& song, TrackId track, ClipId clipId, const Options& o,
                                  const Context& ctx) {
    const Track* t = song.findTrack(track);
    const Clip* clip = song.findClip(clipId);
    if (!t || !clip)
        return {};
    Song solo = soloSong(song, *t, *clip);
    Options clipOptions = o;
    clipOptions.includeMuted = true;
    return trackNotes(solo, track, clipOptions, ctx);
}

std::vector<std::pair<Tick, float>> laneSamples(const Song& song, TrackId track, int output, const Options& o,
                                                Tick from, Tick to) {
    const Track* t = song.findTrack(track);
    return t ? samplesOf(song, *t, output, o, from, to) : std::vector<std::pair<Tick, float>>();
}

// ---- Options ------------------------------------------------------------------------------------------------

json_t* Options::toJson() const {
    json_t* j = json_object();
    setString(j, "timing", kTimingNames[int(timing)]);
    setString(j, "operators", kOperatorNames[int(operators)]);
    setString(j, "automation", kAutomationNames[int(automation)]);
    setString(j, "expressions", kExpressionNames[int(expressions)]);
    setString(j, "range", kRangeNames[int(range)]);
    setString(j, "split", kSplitNames[int(split)]);
    setString(j, "devices", kDeviceNames[int(devices)]);
    setBool(j, "includeMuted", includeMuted);
    setBool(j, "noteChannels", noteChannels);
    setInt(j, "ccStep", ccStep);
    setInt(j, "bendRange", bendRange);
    json_t* ccs = json_array();
    for (int cc : laneCc)
        json_array_append_new(ccs, json_integer(cc));
    set(j, "laneCc", ccs);
    json_t* chans = json_array();
    for (const auto& [track, ch] : channels)
        json_array_append_new(chans, json_pack("[Ii]", static_cast<json_int_t>(track), ch));
    set(j, "channels", chans);
    return j;
}

Options Options::fromJson(const json_t* j) {
    Options o;
    if (!json_is_object(j))
        return o;
    o.timing = named(j, "timing", kTimingNames, o.timing);
    o.operators = named(j, "operators", kOperatorNames, o.operators);
    o.automation = named(j, "automation", kAutomationNames, o.automation);
    o.expressions = named(j, "expressions", kExpressionNames, o.expressions);
    o.range = named(j, "range", kRangeNames, o.range);
    o.split = named(j, "split", kSplitNames, o.split);
    o.devices = named(j, "devices", kDeviceNames, o.devices);
    o.includeMuted = getBool(j, "includeMuted", o.includeMuted);
    o.noteChannels = getBool(j, "noteChannels", o.noteChannels);
    o.ccStep = std::clamp<Tick>(getInt(j, "ccStep", o.ccStep), 1, kPpq);
    o.bendRange = int(std::clamp<int64_t>(getInt(j, "bendRange", o.bendRange), 1, 96));
    const json_t* ccs = get(j, "laneCc");
    for (size_t i = 0; i < o.laneCc.size(); i++)
        o.laneCc[i] = int(std::clamp<int64_t>(intAt(ccs, i, o.laneCc[i]), 0, 119));
    forEach(get(j, "channels"), [&](const json_t* p) {
        int64_t track = intAt(p, 0, 0);
        int64_t ch = intAt(p, 1, 0);
        if (track > 0 && ch >= 1 && ch <= 16)
            o.channels[uint64_t(track)] = int(ch);
    });
    return o;
}

bool Options::operator==(const Options& o) const {
    return timing == o.timing && operators == o.operators && automation == o.automation &&
           expressions == o.expressions && range == o.range && split == o.split && devices == o.devices &&
           includeMuted == o.includeMuted && noteChannels == o.noteChannels && from == o.from && to == o.to &&
           ccStep == o.ccStep && bendRange == o.bendRange && laneCc == o.laneCc && channels == o.channels;
}

std::vector<std::vector<int>> motionPitches(const Song& song, const Track& track, const Clip& clip) {
    std::vector<std::vector<int>> out(clip.notes.size());
    ClipSpan span = clipSpan(clip);
    double loop = std::max(1.0, double(span.length()));
    Scale scale = clip.scaleOverride ? resolveScale(*clip.scaleOverride, song.customScales)
                                     : resolveScale(song.scale, song.customScales);
    for (const Device& motion : track.devices) {
        if (motion.type != DeviceType::Motion)
            continue;
        // How many passes before its curves line up again: the longest of them, capped so a long cycle stays cheap.
        int passes = 1;
        auto measure = [&](const std::vector<Modulator>& list) {
            for (const Modulator& m : list) {
                bool routed = std::any_of(m.routes.begin(), m.routes.end(), [&](const Modulation& r) {
                    return r.kind == Modulation::Kind::Param && r.target.kind == ParamRef::Kind::Device &&
                           r.target.owner == motion.id.value;
                });
                if (!routed || m.length <= 0)
                    continue;
                if (mods::perPass(m.type, m.params.data()))
                    passes = std::max(passes, int(std::lround(double(m.length) / double(mods::kPassTicks))));
                else if (m.type == ModulatorType::Curve)
                    passes = std::max(passes, int(std::ceil(double(m.length) / loop)));
            }
        };
        measure(song.modulators);
        measure(track.modulators);
        for (const Device& d : track.devices)
            measure(d.modulators);
        passes = std::clamp(passes, 1, kMaxGhostPasses);
        for (size_t i = 0; i < clip.notes.size(); i++) {
            const Note& n = clip.notes[i];
            if (n.muted || n.motion != motion.id)
                continue;
            for (int k = 0; k < passes; k++) {
                MovedBy moved = movedAt(song, track, motion, k, double(k) * loop + double(n.start - span.start));
                int pitch = n.pitch;
                if (moved.steps != 0)
                    pitch = moved.inKey ? scale.stepBy(pitch, moved.steps) : pitch + moved.steps;
                out[i].push_back(std::clamp(pitch, 0, 127));
            }
        }
    }
    return out;
}

} // namespace tessera::render
