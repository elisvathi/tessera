#include "Render.hpp"

#include "Arrangement.hpp"
#include "Automation.hpp"
#include "ChainRender.hpp"
#include "Json.hpp"
#include "NoteOperators.hpp"
#include "Scale.hpp"
#include "Swing.hpp"

#include <algorithm>
#include <cmath>
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

/// Every note `t` plays on the timeline, with the chosen timing and operators applied. Clips with their own key
/// add it to `keys`.
std::vector<Played> playedNotes(const Song& song, const Track& t, const Options& o, std::vector<Scale>& keys) {
    struct Item {
        TimedNote note;
        ClipId clip;
    };
    std::map<ClipId, uint8_t> keyOf;
    auto keyIndex = [&](const Clip& clip) -> uint8_t {
        if (!clip.scaleOverride || keys.size() >= 255)
            return 0;
        auto it = keyOf.find(clip.id);
        if (it != keyOf.end())
            return it->second;
        keys.push_back(resolveScale(*clip.scaleOverride, song.customScales));
        return keyOf[clip.id] = uint8_t(keys.size());
    };
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
        for (const TimedNote& n : notes)
            items.push_back({n, a.clip});
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.note.start < b.note.start; });

    std::vector<Played> out;
    // Operators run exactly as the engine runs them: the same seed, clip and pass give the same result, and
    // "previous note" restarts with each pass.
    int lastPass = -1;
    bool previousPlayed = true;
    Tick beat = ticksPerBeat(song.timeSig);
    for (const Item& item : items) {
        const TimedNote& n = item.note;
        const Note* src = n.source;
        double start = double(n.start), end = double(n.end);
        if (o.timing == Timing::Played) {
            double micro = src ? operators::microTicks(*src) : 0;
            start = swung(start, song.swing) + micro;
            end = swung(end, song.swing) + micro;
        }
        const Clip* clip = song.findClip(item.clip);
        uint8_t key = clip ? keyIndex(*clip) : 0;
        if (o.operators == OperatorMode::Written || !src) {
            out.push_back({start, end, n.pitch, n.velocity, n.releaseVelocity, src, n.pass, double(n.start), key,
                           noteChannel(src)});
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
            float v = std::clamp(n.velocity * shape + spread, 0.f, 1.f);
            out.push_back({std::max(0.0, hitStart), hitEnd, n.pitch, v, n.releaseVelocity, src, n.pass,
                           double(n.start) + i, key, noteChannel(src)});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Played& a, const Played& b) { return a.start < b.start; });
    return out;
}

/// The notes a track's device chain plays from `notes` (docs/DEVICES.md §6), with the same code as playback.
std::vector<Played> throughChain(const Song& song, const Track& t, const std::vector<Played>& notes, double until,
                                 double tempo, const std::vector<Scale>& keys) {
    std::vector<chainrender::Note> in;
    in.reserve(notes.size());
    for (const Played& p : notes)
        in.push_back({p.start, p.end, p.pitch, p.velocity, p.release, p.source, p.pass, p.origin, p.key, p.channel});
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
        std::vector<Played> played = playedNotes(song, t, o, keys);
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
    std::vector<Played> played = playedNotes(song, *t, o, keys);
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

} // namespace tessera::render
