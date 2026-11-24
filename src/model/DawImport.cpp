#include "DawImport.hpp"

#include "AutomationEdits.hpp"
#include "Recording.hpp"
#include "Ties.hpp"
#include "TrackColors.hpp"
#include "XmlRead.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <set>

namespace tessera::dawImport {

namespace {

using xml::Node;

/// "#a2eabf" as 0xA2EABF; 0 when it isn't a colour.
uint32_t colorOf(const Node& n) {
    const std::string* c = n.attr("color");
    if (!c || c->size() != 7 || (*c)[0] != '#')
        return 0;
    return uint32_t(std::strtoul(c->c_str() + 1, nullptr, 16)) & 0xFFFFFF;
}

std::string nameOf(const Node& n, const std::string& fallback) {
    const std::string* s = n.attr("name");
    return s && !s->empty() ? *s : fallback;
}

/// One audio file a clip plays, as the project describes it.
struct AudioPart {
    std::string path;
    bool external = false;
    int channels = 2;
    double sampleRate = 48000;
    double seconds = 0;
    bool repitch = false;
    bool warped = false;
    std::vector<std::pair<double, double>> warps; // (content beats, file seconds), in the clip's own time
};

/// A lane that a clip or track holds: a MIDI control (CC, bend, pressure) or a mixer setting.
struct PendingLane {
    MidiControl control;
    LaneDrive drive = LaneDrive::Auto;
    TrackId sendTo;
    std::vector<AutoPoint> points;
};

/// What a clip holds once its nested timelines are read: notes and lanes in its own ticks, and an audio file.
struct Content {
    std::vector<Note> notes;
    std::vector<PendingLane> lanes;
    std::optional<AudioPart> audio;
    double audioOffset = 0; // beats: where the audio's own time 0 sits in the clip
    int audioParts = 0;
};

/// What a parameter ID belongs to, from the project's structure.
struct Param {
    enum class Kind { Volume, Pan, Mute, Send, Tempo, GroupVolume, GroupPan } kind;
    TrackId track; // its track (invalid: the master)
    TrackId sendTo;
    double min = 0, max = 1;
    std::string unit;
};

class Reader {
  public:
    Reader(Imported& out, uint64_t firstId) : out(out), song(out.song) { song.nextId = std::max<uint64_t>(1, firstId); }

    bool read(const Node& project, std::string* error) {
        if (project.name != "Project") {
            if (error)
                *error = "not a DAWproject";
            return false;
        }
        if (const Node* transport = project.child("Transport")) {
            if (const Node* t = transport->child("Tempo")) {
                tempo = std::clamp(t->number("value", 120), 20.0, 999.0);
                remember(t, Param::Kind::Tempo, TrackId());
            }
            if (const Node* ts = transport->child("TimeSignature")) {
                song.timeSig.num = int(ts->number("numerator", 4));
                song.timeSig.den = int(ts->number("denominator", 4));
            }
        }
        out.tempo = tempo;
        indexIds(project);
        collectTargets(project);
        if (const Node* structure = project.child("Structure"))
            for (const Node& t : structure->children)
                if (t.name == "Track")
                    track(t, GroupId(), 0);
        resolveSends();
        if (const Node* arrangement = project.child("Arrangement"))
            readArrangement(*arrangement);
        if (const Node* scenes = project.child("Scenes"))
            readScenes(*scenes);
        if (song.tracks.empty()) {
            if (error)
                *error = "it has no tracks Tessera can use";
            return false;
        }
        finish();
        return true;
    }

  private:
    static constexpr int kMaxReferenceDepth = 8;

    /// Remembers every element that has an `id`, so a clip that plays another clip's content by `reference` finds it.
    void indexIds(const Node& n) {
        if (const std::string* id = n.attr("id"))
            byId.emplace(*id, &n);
        for (const Node& child : n.children)
            indexIds(child);
    }

    // ---- Structure ----------------------------------------------------------------------------------------------

    /// Notes every parameter that some automation targets, so a send at -inf that moves isn't dropped.
    void collectTargets(const Node& n) {
        if (n.name == "Target")
            if (const std::string* p = n.attr("parameter"))
                targeted.insert(*p);
        for (const Node& c : n.children)
            collectTargets(c);
    }

    void remember(const Node* param, Param::Kind kind, TrackId track, TrackId sendTo = TrackId()) {
        if (!param)
            return;
        const std::string* id = param->attr("id");
        if (!id)
            return;
        Param p{kind, track, sendTo, param->number("min", 0), param->number("max", 1), ""};
        if (const std::string* u = param->attr("unit"))
            p.unit = *u;
        params[*id] = p;
    }

    /// A Volume element's level in dB, by its unit (linear gain, as most write it, or decibels).
    static float volumeDb(const Node* v) {
        if (!v)
            return 0;
        const std::string* unit = v->attr("unit");
        double value = v->number("value", 1);
        if (unit && *unit == "decibel")
            return std::clamp(float(value), kSilentDb, kMaxFaderDb);
        if (unit && *unit == "normalized")
            value *= v->number("max", 1);
        return std::clamp(gainToDb(float(value)), kSilentDb, kMaxFaderDb);
    }
    /// A Pan element as -1..1.
    static float panOf(const Node* p) {
        if (!p)
            return 0;
        double lo = p->number("min", 0), hi = p->number("max", 1);
        double v = p->number("value", (lo + hi) / 2);
        return hi > lo ? std::clamp(float((v - lo) / (hi - lo) * 2 - 1), -1.f, 1.f) : 0.f;
    }

    void devicesOf(const Node* channel, const std::string& trackName) {
        const Node* devices = channel ? channel->child("Devices") : nullptr;
        if (!devices)
            return;
        for (const Node& d : devices->children) {
            std::string name = d.attr("deviceName") ? *d.attr("deviceName") : nameOf(d, d.name);
            if (d.flag("enabled", true))
                plugins.push_back(trackName + ": " + name);
        }
    }

    void track(const Node& n, GroupId group, int depth) {
        const Node* channel = n.child("Channel");
        std::string role = channel && channel->attr("role") ? *channel->attr("role") : "regular";
        std::string name = nameOf(n, "Track");
        bool hasChildren = std::any_of(n.children.begin(), n.children.end(), [](const Node& c) { return c.name == "Track"; });
        if (hasChildren) {
            GroupId id = group;
            if (depth == 0) {
                TrackGroup g;
                g.id = song.newId<GroupId>();
                g.name = name;
                g.color = colorOf(n);
                if (channel) {
                    g.channel.volumeDb = volumeDb(channel->child("Volume"));
                    g.channel.pan = panOf(channel->child("Pan"));
                    const Node* mute = channel->child("Mute");
                    g.mute = mute && mute->flag("value");
                    remember(channel->child("Volume"), Param::Kind::GroupVolume, TrackId());
                    remember(channel->child("Pan"), Param::Kind::GroupPan, TrackId());
                }
                song.groups.push_back(g);
                id = g.id;
            } else {
                nestedGroups++;
            }
            devicesOf(channel, name);
            for (const Node& c : n.children)
                if (c.name == "Track")
                    track(c, id, depth + 1);
            return;
        }
        if (role == "master") {
            if (channel) {
                song.master.volumeDb = volumeDb(channel->child("Volume"));
                if (const std::string* id = channel->attr("id"))
                    masterChannel = *id;
                remember(channel->child("Volume"), Param::Kind::Volume, TrackId());
            }
            devicesOf(channel, "Master");
            return;
        }
        if (int(song.tracks.size()) >= kMaxTracks) {
            droppedTracks++;
            return;
        }
        const std::string* content = n.attr("contentType");
        bool notes = !content || content->find("notes") != std::string::npos;
        bool audio = content && content->find("audio") != std::string::npos;
        Track t;
        t.id = song.newId<TrackId>();
        t.name = name;
        t.type = audio && !notes ? TrackType::Audio : TrackType::Instrument;
        t.color = colorOf(n);
        if (!t.color)
            t.color = trackColorForIndex(int(song.tracks.size()));
        if (channel) {
            t.channel.volumeDb = volumeDb(channel->child("Volume"));
            t.channel.pan = panOf(channel->child("Pan"));
            const Node* mute = channel->child("Mute");
            t.mute = mute && mute->flag("value");
            t.solo = channel->flag("solo");
            if (const std::string* id = channel->attr("id"))
                channels[*id] = t.id;
            remember(channel->child("Volume"), Param::Kind::Volume, t.id);
            remember(channel->child("Pan"), Param::Kind::Pan, t.id);
            remember(mute, Param::Kind::Mute, t.id);
            if (const Node* sends = channel->child("Sends"))
                for (const Node& s : sends->children)
                    if (s.name == "Send")
                        pendingSends.push_back({t.id, &s});
        }
        if (const std::string* id = n.attr("id"))
            tracks[*id] = t.id;
        devicesOf(channel, name);
        if (group.valid())
            for (TrackGroup& g : song.groups)
                if (g.id == group)
                    g.tracks.push_back(t.id);
        song.tracks.push_back(t);
    }

    /// Sends, once every track (and so every channel a send goes to) is known.
    void resolveSends() {
        for (const auto& [from, node] : pendingSends) {
            const std::string* dest = node->attr("destination");
            auto to = dest ? channels.find(*dest) : channels.end();
            Track* t = song.findTrack(from);
            if (!t || to == channels.end() || to->second == from)
                continue;
            const Node* volume = node->child("Volume");
            float db = volumeDb(volume);
            const std::string* volumeId = volume ? volume->attr("id") : nullptr;
            bool moves = volumeId && targeted.count(*volumeId);
            if (db <= kSilentDb && !moves)
                continue; // a send that sends nothing (some programs list one to every effect track)
            Send s;
            s.to = to->second;
            s.db = db;
            const std::string* type = node->attr("type");
            s.preFader = type && *type == "pre";
            if (t->type == TrackType::Audio)
                s.from = SendFrom::clips();
            t->sends.push_back(s);
            remember(volume, Param::Kind::Send, from, s.to);
        }
    }

    // ---- Time ---------------------------------------------------------------------------------------------------

    /// Whether a timeline counts in seconds: its own timeUnit, else the one it inherits.
    static bool inSeconds(const Node& n, bool inherited, const char* key = "timeUnit") {
        const std::string* u = n.attr(key);
        return u ? *u == "seconds" : inherited;
    }
    double beatsOf(double v, bool seconds) const { return seconds ? v * tempo / 60 : v; }
    static Tick ticksOf(double beats) { return Tick(std::llround(beats * double(kPpq))); }

    // ---- Lanes of values ----------------------------------------------------------------------------------------

    std::vector<AutoPoint> pointsOf(const Node& p, bool seconds, double offset, const std::function<float(double)>& map) {
        std::vector<AutoPoint> out;
        for (const Node& c : p.children) {
            if (c.name != "RealPoint" && c.name != "IntegerPoint" && c.name != "BoolPoint")
                continue;
            AutoPoint pt;
            pt.t = std::max<Tick>(0, ticksOf(offset + beatsOf(c.number("time"), seconds)));
            double v = c.name == "BoolPoint" ? (c.flag("value") ? 1 : 0) : c.number("value");
            pt.value = std::clamp(map(v), 0.f, 1.f);
            const std::string* interp = c.attr("interpolation");
            pt.curve = c.name != "RealPoint" || !interp || *interp == "hold" ? CurveType::Hold : CurveType::Linear;
            out.push_back(pt);
        }
        std::stable_sort(out.begin(), out.end(), [](const AutoPoint& a, const AutoPoint& b) { return a.t < b.t; });
        return out;
    }

    /// A Points timeline: a MIDI control's or a mixer setting's lane, or something Tessera has no place for.
    std::optional<PendingLane> lane(const Node& p, bool seconds, double offset, bool inClip) {
        const Node* target = p.child("Target");
        if (!target)
            return std::nullopt;
        seconds = inSeconds(p, seconds);
        auto unitRange = [&](double v) { return float(v); };
        if (const std::string* e = target->attr("expression")) {
            PendingLane l;
            if (*e == "channelController") {
                l.control = {MidiControl::Kind::Cc, uint8_t(std::clamp(int(target->number("controller")), 0, 127))};
            } else if (*e == "pitchBend") {
                l.control = {MidiControl::Kind::PitchBend};
            } else if (*e == "channelPressure") {
                l.control = {MidiControl::Kind::Pressure};
            } else {
                otherExpressions++;
                return std::nullopt;
            }
            l.points = pointsOf(p, seconds, offset, unitRange);
            return l;
        }
        const std::string* id = target->attr("parameter");
        auto found = id ? params.find(*id) : params.end();
        if (found == params.end()) {
            pluginLanes++;
            return std::nullopt;
        }
        const Param& param = found->second;
        PendingLane l;
        switch (param.kind) {
        case Param::Kind::Volume:
        case Param::Kind::Send: {
            l.drive = param.kind == Param::Kind::Send ? LaneDrive::Send : LaneDrive::Volume;
            l.sendTo = param.sendTo;
            std::string unit = p.attr("unit") ? *p.attr("unit") : param.unit;
            double max = param.max;
            l.points = pointsOf(p, seconds, offset, [unit, max](double v) {
                float db = unit == "decibel" ? float(v) : gainToDb(float(unit == "normalized" ? v * max : v));
                return faderPosOfDb(std::clamp(db, kSilentDb, kMaxFaderDb));
            });
            break;
        }
        case Param::Kind::Pan: {
            l.drive = LaneDrive::Pan;
            double lo = param.min, hi = param.max;
            l.points = pointsOf(p, seconds, offset, [lo, hi](double v) { return hi > lo ? float((v - lo) / (hi - lo)) : 0.5f; });
            break;
        }
        case Param::Kind::Tempo: {
            std::set<double> values;
            for (const Node& c : p.children)
                if (c.name == "RealPoint")
                    values.insert(c.number("value"));
            tempoChanges |= values.size() > 1;
            return std::nullopt;
        }
        default:
            otherMixLanes++;
            return std::nullopt;
        }
        if (inClip) {
            otherMixLanes++; // mixer automation inside a clip has no place: Tessera's mixer lanes are the track's
            return std::nullopt;
        }
        // The lane belongs to the parameter's track (or the master), wherever it was written.
        laneOwner = param.track;
        return l;
    }

    /// Adds a lane to `lanes` (a track's or clip's) on the track `owner`: CC lanes share an AUTO channel per CC.
    void addLane(std::vector<AutomationEnvelope>& lanes, TrackId owner, const PendingLane& p) {
        if (p.points.empty())
            return;
        const Track* t = song.findTrack(owner);
        uint32_t color = t ? t->color : 0;
        AutomationEnvelope l;
        if (p.control.valid()) {
            int output = 0;
            if (p.control.kind == MidiControl::Kind::Cc) {
                std::map<int, int>& used = ccOutputs[owner.value];
                auto it = used.find(p.control.cc);
                if (it == used.end()) {
                    if (int(used.size()) >= kMaxAutomationLanes) {
                        ccDropped++;
                        return;
                    }
                    it = used.emplace(p.control.cc, int(used.size())).first;
                }
                output = it->second;
            }
            l = makeMidiLane(song.newId<AutomationId>(), p.control, output, color);
        } else {
            l = makeLane(song.newId<AutomationId>(), 0, color);
            l.drive = p.drive;
            l.sendTo = p.sendTo;
            l.rangeMin = 0;
            l.rangeMax = 1;
            l.name = p.drive == LaneDrive::Pan ? "Pan" : p.drive == LaneDrive::Send ? "Send" : "Volume";
        }
        l.points = p.points;
        // One lane per target: a second for the same target adds its points.
        for (AutomationEnvelope& e : lanes)
            if (e.midi == l.midi && e.drive == l.drive && e.sendTo == l.sendTo && e.paramTarget == l.paramTarget &&
                (l.drive != LaneDrive::Auto || l.midi.valid())) {
                e.points.insert(e.points.end(), l.points.begin(), l.points.end());
                std::stable_sort(e.points.begin(), e.points.end(), [](const AutoPoint& a, const AutoPoint& b) { return a.t < b.t; });
                return;
            }
        lanes.push_back(l);
    }

    // ---- Notes --------------------------------------------------------------------------------------------------

    /// One note's own curve (pitch in semitones, timbre and pressure 0..1) from a Points inside it.
    void noteCurve(Note& note, const Node& p, bool seconds, double lengthBeats) {
        const Node* target = p.child("Target");
        const std::string* e = target ? target->attr("expression") : nullptr;
        std::vector<ExprPoint>* curve = nullptr;
        float lo = 0, hi = 1;
        if (e && *e == "transpose") {
            curve = &note.pitchExpr;
            lo = -48;
            hi = 48;
        } else if (e && *e == "timbre") {
            curve = &note.timbreExpr;
        } else if (e && *e == "pressure") {
            curve = &note.pressureExpr;
        } else {
            otherNoteCurves++;
            return;
        }
        seconds = inSeconds(p, seconds);
        for (const Node& c : p.children) {
            if (c.name != "RealPoint")
                continue;
            float pos = lengthBeats > 0 ? float(std::clamp(beatsOf(c.number("time"), seconds) / lengthBeats, 0.0, 1.0)) : 0.f;
            float value = std::clamp(float(c.number("value")), lo, hi);
            const std::string* interp = c.attr("interpolation");
            bool hold = !interp || *interp == "hold";
            if (hold && !curve->empty() && curve->back().pos < pos)
                curve->push_back({std::max(curve->back().pos, pos - 1e-4f), curve->back().value, 0});
            curve->push_back({pos, value, 0});
        }
        std::stable_sort(curve->begin(), curve->end(), [](const ExprPoint& a, const ExprPoint& b) { return a.pos < b.pos; });
    }

    void notesOf(const Node& notes, bool seconds, double offset, double from, double to, Content& c) {
        seconds = inSeconds(notes, seconds);
        for (const Node& n : notes.children) {
            if (n.name != "Note")
                continue;
            double start = offset + beatsOf(n.number("time"), seconds);
            if (start < from - 1e-9 || start >= to - 1e-9)
                continue;
            double length = beatsOf(n.number("duration"), seconds);
            Note note;
            note.id = song.newId<NoteId>();
            note.start = std::max<Tick>(0, ticksOf(start));
            note.length = std::max<Tick>(1, ticksOf(length));
            note.pitch = int8_t(std::clamp(int(n.number("key", 60)), 0, 127));
            note.velocity = std::clamp(float(n.number("vel", 0.8)), 0.f, 1.f);
            note.releaseVelocity = std::clamp(float(n.number("rel", 0.5)), 0.f, 1.f);
            note.channel = uint8_t(std::clamp(int(n.number("channel", 0)) + 1, 1, kMaxNoteChannel));
            for (const Node& child : n.children) {
                if (child.name == "Points")
                    noteCurve(note, child, seconds, length);
                else if (child.name == "Lanes")
                    for (const Node& p : child.children)
                        if (p.name == "Points")
                            noteCurve(note, p, inSeconds(child, seconds), length);
            }
            c.notes.push_back(note);
        }
    }

    // ---- Clips --------------------------------------------------------------------------------------------------

    AudioPart audioOf(const Node& audio) {
        AudioPart a;
        if (const Node* file = audio.child("File")) {
            if (const std::string* p = file->attr("path"))
                a.path = *p;
            a.external = file->flag("external");
        }
        a.channels = std::clamp(int(audio.number("channels", 2)), 1, 2);
        a.sampleRate = audio.number("sampleRate", 48000);
        a.seconds = audio.number("duration", 0);
        const std::string* algo = audio.attr("algorithm");
        a.repitch = algo && *algo == "repitch";
        return a;
    }

    /// Reads a clip's content (its nested timelines) into `c`: times in beats from the clip's own start, shifted by
    /// `offset`; notes only where they start inside [from, to).
    void contentOf(const Node& n, bool seconds, double offset, double from, double to, Content& c) {
        std::vector<const Node*> parts;
        for (const Node& child : n.children)
            parts.push_back(&child);
        // A clip can play another clip's content: `reference` names that timeline instead of holding a copy. The
        // depth limit stops a file whose references go round in a circle.
        const std::string* ref = n.attr("reference");
        auto target = ref ? byId.find(*ref) : byId.end();
        if (target != byId.end() && referenceDepth < kMaxReferenceDepth)
            parts.push_back(target->second);
        referenceDepth++;
        for (const Node* part : parts) {
            const Node& child = *part;
            if (child.name == "Notes") {
                notesOf(child, seconds, offset, from, to, c);
            } else if (child.name == "Lanes") {
                contentOf(child, inSeconds(child, seconds), offset, from, to, c);
            } else if (child.name == "Clips") {
                bool s = inSeconds(child, seconds);
                for (const Node& inner : child.children) {
                    if (inner.name != "Clip")
                        continue;
                    if (!inner.flag("enable", true))
                        continue;
                    double time = offset + beatsOf(inner.number("time"), s);
                    double duration = beatsOf(inner.number("duration"), s);
                    double play = beatsOf(inner.number("playStart"), inSeconds(inner, s, "contentTimeUnit"));
                    contentOf(inner, inSeconds(inner, s, "contentTimeUnit"), time - play, std::max(from, time),
                              std::min(to, time + duration), c);
                }
            } else if (child.name == "Points") {
                laneOwner = TrackId();
                if (std::optional<PendingLane> l = lane(child, seconds, offset, true))
                    c.lanes.push_back(*l);
            } else if (child.name == "Warps" || child.name == "Audio") {
                c.audioParts++;
                if (c.audio)
                    continue;
                if (child.name == "Audio") {
                    c.audio = audioOf(child);
                } else if (const Node* audio = child.child("Audio")) {
                    AudioPart a = audioOf(*audio);
                    a.warped = true;
                    bool beats = !inSeconds(child, seconds);
                    bool contentSeconds = inSeconds(child, true, "contentTimeUnit");
                    for (const Node& w : child.children)
                        if (w.name == "Warp") {
                            double t = w.number("time");
                            double ct = w.number("contentTime");
                            a.warps.push_back({beats ? t : t * tempo / 60, contentSeconds ? ct : ct * 60 / tempo});
                        }
                    std::sort(a.warps.begin(), a.warps.end());
                    c.audio = a;
                }
                c.audioOffset = offset;
            }
        }
        referenceDepth--;
    }

    /// The placeholder source for a file, one per path.
    AudioSourceId sourceFor(const AudioPart& a) {
        auto it = sources.find(a.path);
        if (it != sources.end())
            return it->second;
        AudioSource s;
        s.id = song.newId<AudioSourceId>();
        size_t slash = a.path.find_last_of("/\\");
        s.name = slash == std::string::npos ? a.path : a.path.substr(slash + 1);
        s.file = a.path;
        s.channels = s.fileChannels = a.channels;
        s.sampleRate = a.sampleRate;
        s.frames = int64_t(std::llround(a.seconds * a.sampleRate));
        song.audioSources.push_back(s);
        out.audio.push_back({s.id, a.path, a.external, a.sampleRate});
        sources[a.path] = s.id;
        return s.id;
    }

    /// The audio a clip plays: its file, where the clip's tick 0 is in it, at what tempo, with warp markers when the
    /// project's warps aren't a straight line.
    AudioContent audioContent(const AudioPart& a, double offset) {
        AudioContent c;
        c.source = sourceFor(a);
        c.follow = !a.warped ? TempoFollow::Off : a.repitch ? TempoFollow::Repitch : TempoFollow::Stretch;
        c.sourceTempo = tempo;
        if (!a.warped || a.warps.size() < 2) {
            // Unwarped: the file plays at its own speed from where its time 0 sits.
            c.sourceStart = std::max<int64_t>(0, std::llround(-offset * 60 / tempo * a.sampleRate));
            return c;
        }
        auto [b0, s0] = a.warps.front();
        auto [b1, s1] = a.warps.back();
        double secondsPerBeat = b1 > b0 ? (s1 - s0) / (b1 - b0) : 60 / tempo;
        bool straight = true;
        for (const auto& [b, s] : a.warps)
            straight = straight && std::abs(s0 + (b - b0) * secondsPerBeat - s) < 1e-4;
        c.sourceTempo = std::clamp(60 / std::max(1e-9, secondsPerBeat), kMinClipTempo, kMaxClipTempo);
        double atZero = s0 + (0 - (b0 + offset)) * secondsPerBeat; // file seconds at the clip's tick 0
        if (straight && atZero >= 0) {
            c.sourceStart = std::llround(atZero * a.sampleRate);
            return c;
        }
        for (const auto& [b, s] : a.warps)
            c.warps.push_back({double(ticksOf(b + offset)), int64_t(std::llround(s * a.sampleRate))});
        return c;
    }

    /// A Tessera clip from a project clip on `owner`: its content, loop and lanes; null when it holds nothing
    /// Tessera can play there.
    std::optional<Clip> clipOf(const Node& n, TrackId owner, bool seconds, double& durationBeats, double& playStart) {
        Track* t = song.findTrack(owner);
        if (!t)
            return std::nullopt;
        bool contentSeconds = inSeconds(n, seconds, "contentTimeUnit");
        durationBeats = beatsOf(n.number("duration"), seconds);
        playStart = beatsOf(n.number("playStart"), contentSeconds);
        Content c;
        contentOf(n, contentSeconds, 0, -1e18, 1e18, c);
        if (c.audioParts > 1)
            audioPartsDropped += c.audioParts - 1;
        bool audio = t->type == TrackType::Audio;
        if (audio && !c.audio) {
            if (!c.notes.empty())
                notesOnAudioTracks++;
            return std::nullopt;
        }
        if (!audio && c.audio && c.notes.empty()) {
            audioOnNoteTracks++;
            return std::nullopt;
        }
        Clip clip;
        clip.id = song.newId<ClipId>();
        clip.name = nameOf(n, "");
        clip.ownColor = colorOf(n);
        double loopStart = beatsOf(n.number("loopStart", -1), contentSeconds);
        double loopEnd = beatsOf(n.number("loopEnd", -1), contentSeconds);
        bool loops = n.attr("loopStart") && n.attr("loopEnd") && loopEnd > loopStart;
        Tick contentEnd = ticksOf(playStart + durationBeats);
        if (audio) {
            clip.audio = audioContent(*c.audio, c.audioOffset);
        } else {
            clip.notes = std::move(c.notes);
            for (const Note& note : clip.notes)
                contentEnd = std::max(contentEnd, note.end());
            std::sort(clip.notes.begin(), clip.notes.end(), noteOrder);
            clip.notes = ties::tieHandovers(clip.notes); // what an export wrote as overlaps comes back tied
            out.notes += int(clip.notes.size());
        }
        if (loops) {
            clip.loopMode = LoopMode::Loop;
            clip.loopStart = std::max<Tick>(0, ticksOf(loopStart));
            clip.loopEnd = ticksOf(loopEnd);
            contentEnd = std::max(contentEnd, clip.loopEnd);
        } else {
            clip.loopMode = LoopMode::Off;
            clip.loopStart = 0;
            clip.loopEnd = std::max<Tick>(1, contentEnd);
        }
        clip.length = std::max<Tick>(1, contentEnd);
        for (const PendingLane& l : c.lanes)
            addLane(clip.automation, owner, l);
        out.clips++;
        return clip;
    }

    /// A Fade length from a clip's fade attribute, in ticks.
    Tick fadeOf(const Node& n, const char* key, bool seconds) {
        return std::max<Tick>(0, ticksOf(beatsOf(n.number(key, 0), inSeconds(n, seconds, "fadeTimeUnit"))));
    }

    void timelineClips(const Node& clips, TrackId owner, bool seconds) {
        seconds = inSeconds(clips, seconds);
        for (const Node& n : clips.children) {
            if (n.name != "Clip")
                continue;
            double duration = 0, playStart = 0;
            std::optional<Clip> clip = clipOf(n, owner, seconds, duration, playStart);
            Track* t = song.findTrack(owner);
            if (!clip || !t)
                continue;
            ArrangerClip a;
            a.id = song.newId<ArrangementId>();
            a.clip = clip->id;
            a.position = std::max<Tick>(0, ticksOf(beatsOf(n.number("time"), seconds)));
            a.length = std::max<Tick>(1, ticksOf(duration));
            a.offset = std::max<Tick>(0, ticksOf(playStart));
            a.muted = !n.flag("enable", true);
            if (clip->audio) {
                a.fadeIn.length = fadeOf(n, "fadeInTime", seconds);
                a.fadeOut.length = fadeOf(n, "fadeOutTime", seconds);
            }
            song.clips.emplace(clip->id, std::move(*clip));
            t->arrangement.push_back(a);
        }
    }

    /// A Lanes of the arrangement: its track's clips and lanes, and nested Lanes.
    void arrangementLanes(const Node& lanes, TrackId owner, bool seconds) {
        seconds = inSeconds(lanes, seconds);
        if (const std::string* t = lanes.attr("track")) {
            auto it = tracks.find(*t);
            owner = it != tracks.end() ? it->second : TrackId();
            if (it == tracks.end() && channels.count(*t))
                owner = channels[*t];
        }
        for (const Node& c : lanes.children) {
            if (c.name == "Lanes") {
                arrangementLanes(c, owner, seconds);
            } else if (c.name == "Clips") {
                if (owner.valid())
                    timelineClips(c, owner, seconds);
            } else if (c.name == "Points") {
                laneOwner = owner;
                std::optional<PendingLane> l = lane(c, seconds, 0, false);
                if (!l)
                    continue;
                if (!laneOwner.valid() && l->drive == LaneDrive::Volume)
                    addLane(song.master.automation, TrackId(), *l);
                else if (Track* t = song.findTrack(laneOwner))
                    addLane(t->automation, t->id, *l);
            } else if (c.name == "Notes" && owner.valid()) {
                // Notes straight on the timeline, outside any clip: one clip that holds them all.
                Content content;
                notesOf(c, seconds, 0, -1e18, 1e18, content);
                Track* t = song.findTrack(owner);
                if (!t || content.notes.empty() || t->type == TrackType::Audio)
                    continue;
                Clip clip;
                clip.id = song.newId<ClipId>();
                std::sort(content.notes.begin(), content.notes.end(), noteOrder);
                Tick end = 1;
                for (const Note& note : content.notes)
                    end = std::max(end, note.end());
                clip.notes = ties::tieHandovers(content.notes);
                clip.length = clip.loopEnd = end;
                clip.loopMode = LoopMode::Off;
                ArrangerClip a;
                a.id = song.newId<ArrangementId>();
                a.clip = clip.id;
                a.length = end;
                out.notes += int(clip.notes.size());
                out.clips++;
                song.clips.emplace(clip.id, std::move(clip));
                t->arrangement.push_back(a);
            }
        }
    }

    void readArrangement(const Node& arrangement) {
        for (const Node& c : arrangement.children) {
            if (c.name == "Lanes") {
                arrangementLanes(c, TrackId(), false);
            } else if (c.name == "Markers") {
                bool seconds = inSeconds(c, false);
                for (const Node& m : c.children)
                    if (m.name == "Marker") {
                        Marker marker;
                        marker.id = song.newId<MarkerId>();
                        marker.position = std::max<Tick>(0, ticksOf(beatsOf(m.number("time"), seconds)));
                        marker.name = nameOf(m, "");
                        marker.color = colorOf(m);
                        song.markers.push_back(marker);
                    }
            } else if (c.name == "TempoAutomation") {
                std::set<double> values;
                for (const Node& p : c.children)
                    if (p.name == "RealPoint")
                        values.insert(p.number("value"));
                tempoChanges |= values.size() > 1;
            } else if (c.name == "TimeSignatureAutomation") {
                int points = 0;
                for (const Node& p : c.children)
                    points += p.name == "TimeSignaturePoint" ? 1 : 0;
                signatureChanges |= points > 1;
            }
        }
        std::sort(song.markers.begin(), song.markers.end(),
                  [](const Marker& a, const Marker& b) { return a.position < b.position; });
    }

    void readScenes(const Node& scenes) {
        for (const Node& s : scenes.children) {
            if (s.name != "Scene")
                continue;
            if (int(song.scenes.size()) >= kMaxScenes) {
                droppedScenes++;
                continue;
            }
            size_t index = song.scenes.size();
            Scene scene;
            scene.name = nameOf(s, "");
            scene.color = colorOf(s);
            song.scenes.push_back(scene);
            for (Track& t : song.tracks)
                t.slots.resize(song.scenes.size());
            std::function<void(const Node&)> slots = [&](const Node& n) {
                for (const Node& c : n.children) {
                    if (c.name == "Lanes") {
                        slots(c);
                        continue;
                    }
                    if (c.name != "ClipSlot")
                        continue;
                    const std::string* tid = c.attr("track");
                    auto it = tid ? tracks.find(*tid) : tracks.end();
                    Track* t = it != tracks.end() ? song.findTrack(it->second) : nullptr;
                    if (!t)
                        continue;
                    t->slots[index].stop = c.flag("hasStop", true);
                    const Node* clipNode = c.child("Clip");
                    if (!clipNode)
                        continue;
                    double duration = 0, playStart = 0;
                    std::optional<Clip> clip = clipOf(*clipNode, t->id, inSeconds(n, false), duration, playStart);
                    if (!clip)
                        continue;
                    if (clip->loopMode == LoopMode::Off)
                        clip->loopEnd = std::max(clip->loopEnd, ticksOf(playStart + duration));
                    t = song.findTrack(it->second);
                    t->slots[index].clip = clip->id;
                    song.clips.emplace(clip->id, std::move(*clip));
                }
            };
            slots(s);
        }
    }

    // ---- The report ---------------------------------------------------------------------------------------------

    void finish() {
        if (!song.master.id.valid())
            song.master.id = song.newId<TrackId>();
        if (song.scenes.empty())
            song.scenes.resize(1);
        for (Track& t : song.tracks)
            t.slots.resize(song.scenes.size());
        normalize(song);
        std::vector<std::string>& l = out.leftOut;
        if (!plugins.empty()) {
            std::string list;
            for (size_t i = 0; i < plugins.size() && i < 12; i++)
                list += (i ? "; " : "") + plugins[i];
            if (plugins.size() > 12)
                list += "; and " + std::to_string(plugins.size() - 12) + " more";
            l.push_back("Instruments and effects (rebuild them in Rack): " + list);
        }
        auto count = [&](int n, const std::string& one, const std::string& many) {
            if (n > 0)
                l.push_back(n == 1 ? one : std::to_string(n) + " " + many);
        };
        count(pluginLanes, "1 lane moving an instrument or effect knob", "lanes moving instrument or effect knobs");
        count(otherMixLanes, "1 mixer lane Tessera has no place for (mute, or inside a clip)",
              "mixer lanes Tessera has no place for (mute, or inside a clip)");
        count(otherExpressions, "1 lane of another MIDI kind (poly pressure, program change)",
              "lanes of other MIDI kinds (poly pressure, program change)");
        count(otherNoteCurves, "1 note curve of gain, pan or formant", "note curves of gain, pan or formant");
        count(ccDropped, "1 controller lane past a track's 8 AUTO channels", "controller lanes past a track's 8 AUTO channels");
        count(droppedTracks, "1 track past the 64-track limit", "tracks past the 64-track limit");
        count(droppedScenes, "1 scene past the 64-scene limit", "scenes past the 64-scene limit");
        count(nestedGroups, "1 group inside a group: its tracks are in the outer group",
              "groups inside groups: their tracks are in the outer group");
        count(audioPartsDropped, "1 extra audio part inside a clip (the first plays)",
              "extra audio parts inside clips (the first plays in each)");
        count(audioOnNoteTracks, "1 audio clip on a note track", "audio clips on note tracks");
        count(notesOnAudioTracks, "1 note clip on an audio track", "note clips on audio tracks");
        char bpm[64];
        snprintf(bpm, sizeof bpm, "Tempo changes: the song plays at %.4g BPM throughout", tempo);
        if (tempoChanges)
            l.push_back(bpm);
        if (signatureChanges)
            l.push_back("Time signature changes: the first one is kept");
    }

    Imported& out;
    Song& song;
    double tempo = 120;
    std::map<std::string, TrackId> tracks, channels;
    std::map<std::string, const Node*> byId;
    int referenceDepth = 0;
    std::map<std::string, Param> params;
    std::set<std::string> targeted;
    std::string masterChannel;
    std::vector<std::pair<TrackId, const Node*>> pendingSends;
    std::map<std::string, AudioSourceId> sources;
    std::map<uint64_t, std::map<int, int>> ccOutputs;
    TrackId laneOwner;
    std::vector<std::string> plugins;
    int pluginLanes = 0, otherMixLanes = 0, otherExpressions = 0, otherNoteCurves = 0, ccDropped = 0;
    int droppedTracks = 0, droppedScenes = 0, nestedGroups = 0, audioPartsDropped = 0, audioOnNoteTracks = 0;
    int notesOnAudioTracks = 0;
    bool tempoChanges = false, signatureChanges = false;
};

} // namespace

bool readProject(const std::string& projectXml, const std::string& metadataXml, uint64_t firstId, Imported& out,
                 std::string* error) {
    out = Imported();
    xml::Node project;
    std::string why;
    if (!xml::parse(projectXml, project, &why)) {
        if (error)
            *error = "project.xml is damaged (" + why + ")";
        return false;
    }
    xml::Node meta;
    if (!metadataXml.empty() && xml::parse(metadataXml, meta) && meta.child("Title"))
        out.title = meta.child("Title")->text;
    Reader reader(out, firstId);
    return reader.read(project, error);
}

} // namespace tessera::dawImport
