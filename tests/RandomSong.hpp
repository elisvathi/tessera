#pragma once

#include "../src/model/DeviceTypes.hpp"
#include "../src/model/Song.hpp"

#include <random>

namespace tessera::testing {

/// A song with every field set to random but valid values, for round-trip and symmetry tests.
class RandomSong {
  public:
    explicit RandomSong(uint32_t seed) : rng(seed) {}

    Song make() {
        Song s;
        s.timeSig = {int(range(1, 12)), 1 << int(range(1, 4))};
        s.swing = unit();
        s.scale = {int8_t(range(0, 11)), pick({"major", "dorian", "custom-1"})};
        s.customScales.push_back({"custom-1", "Mine", uint16_t(range(1, 0xFFF))});
        s.loopStart = range(0, 4) * kPpq;
        s.loopEnd = s.loopStart + range(1, 16) * kPpq;
        s.seed = uint32_t(range(1, 1 << 30));
        s.launchQuantize = LaunchQuantize(range(0, 5));
        s.scenes.resize(size_t(range(1, 6)));
        for (Scene& sc : s.scenes) {
            sc.name = pick({"", "Intro", "Drop – ünïcode"});
            sc.color = color();
            if (coin())
                sc.tempo = double(range(60, 180)) + 0.5;
        }

        int clipCount = int(range(1, 5));
        std::vector<ClipId> clipIds;
        for (int i = 0; i < clipCount; i++) {
            Clip c = clip(s);
            clipIds.push_back(c.id);
            s.clips.emplace(c.id, std::move(c));
        }

        int trackCount = int(range(1, 6));
        for (int i = 0; i < trackCount; i++)
            s.tracks.push_back(track(s, clipIds));

        TrackGroup g;
        g.id = s.newId<GroupId>();
        g.name = "Group";
        g.color = color();
        g.tracks.push_back(s.tracks.front().id);
        s.groups.push_back(g);

        for (int i = 0; i < 3; i++) {
            Marker m;
            m.id = s.newId<MarkerId>();
            m.position = range(0, 64) * kPpq;
            m.name = "M" + std::to_string(i);
            m.color = color();
            s.markers.push_back(m);
        }
        chains(s);
        normalize(s);
        return s;
    }

    Note note(Song& s) {
        Note n;
        n.id = s.newId<NoteId>();
        n.start = range(0, 8 * kPpq);
        n.length = range(1, 2 * kPpq);
        n.pitch = int8_t(range(0, 127));
        n.velocity = unit();
        n.releaseVelocity = unit();
        n.microOffset = unit() - 0.5f;
        n.muted = coin();
        n.colorTag = uint8_t(range(0, 255));
        n.channel = uint8_t(range(1, kMaxNoteChannel));
        if (coin()) {
            n.ops.chance = unit();
            n.ops.repeats = int8_t(range(-16, 16));
            n.ops.repeatCurve = unit() * 2 - 1;
            n.ops.occurrence = Occurrence(range(0, 6));
            n.ops.recurrenceLength = uint8_t(range(1, 16));
            n.ops.recurrenceMask = uint16_t(range(0, 0xFFFF));
            n.ops.timingSpread = unit() * 0.5f;
        }
        if (coin())
            n.pitchExpr = {{0, unit() * 48 - 24, 0}, {1, unit(), unit() * 2 - 1}};
        if (coin())
            n.timbreExpr = {{0.5f, unit(), 0}};
        return n;
    }

  private:
    Clip clip(Song& s) {
        Clip c;
        c.id = s.newId<ClipId>();
        c.name = pick({"", "Bass", "Lead \"quoted\""});
        c.color = color();
        c.length = range(1, 8) * kPpq;
        c.loopStart = range(0, 2) * kPpq;
        c.loopEnd = c.loopStart + range(1, 8) * kPpq;
        c.loopMode = coin() ? LoopMode::Loop : LoopMode::Off;
        int notes = int(range(0, 40));
        for (int i = 0; i < notes; i++)
            c.notes.push_back(note(s));
        if (coin())
            c.automation.push_back(lane(s));
        if (coin())
            c.scaleOverride = ScaleRef{int8_t(range(0, 11)), "dorian"};
        if (coin())
            c.launchQuantize = LaunchQuantize(range(0, 5));
        return c;
    }

    Track track(Song& s, const std::vector<ClipId>& clips) {
        Track t = makeTrack(s, coin() ? TrackType::Instrument : TrackType::Drum, "T" + std::to_string(range(1, 99)));
        t.polyphony = int(range(kAutoVoices, 16));
        t.mute = coin();
        t.solo = coin();
        t.armed = coin();
        t.monitor = coin();
        t.recordInput = RecordInput(range(0, 2));
        t.midiChannel = int8_t(range(0, 16));
        t.gateLengthScale = unit();
        t.transpose = int8_t(range(-24, 24));
        if (t.type == TrackType::Drum) {
            for (int i = 0; i < 4; i++) {
                DrumLane l;
                l.name = "Lane " + std::to_string(i);
                l.pitch = int8_t(36 + i);
                l.outputIndex = i;
                l.mute = coin();
                l.gateLengthMs = unit() * 50;
                l.chokeGroup = int(range(0, 2));
                l.loopLength = range(0, 2) * kPpq;
                l.stepLength = kPpq / 4 * range(1, 2);
                l.color = coin() ? 0 : color();
                t.drumLanes.push_back(l);
            }
        }
        for (ClipSlot& slot : t.slots)
            if (coin())
                slot.clip = clips[size_t(range(0, int64_t(clips.size()) - 1))];
        for (int i = 0; i < 3; i++) {
            ArrangerClip a;
            a.id = s.newId<ArrangementId>();
            a.clip = clips[size_t(range(0, int64_t(clips.size()) - 1))];
            a.position = range(0, 32) * kPpq;
            a.length = range(1, 8) * kPpq;
            a.offset = range(0, 2) * kPpq;
            a.muted = coin();
            a.transpose = int8_t(range(-12, 12));
            a.velocityScale = unit() * 2;
            a.stretch = 0.5f + unit();
            t.arrangement.push_back(a);
        }
        if (coin())
            t.automation.push_back(lane(s));
        return t;
    }

    /// Devices and modulators on random tracks, with routes that stay inside each modulator's reach.
    void chains(Song& s) {
        std::vector<std::pair<TrackId, ParamRef>> knobs;
        for (Track& t : s.tracks) {
            int devices = int(range(0, 3));
            for (int i = 0; i < devices; i++) {
                Device d = devices::makeDevice(DeviceType(range(0, kDeviceTypeCount - 1)), s.newId<DeviceId>());
                d.name = pick({"Arp", "Chord – ünïcode", "X"});
                d.on = coin();
                d.affectDrumLanes = coin();
                for (float& p : d.params)
                    p = unit();
                if (coin()) {
                    Modulator m = modulator(s);
                    m.routes.push_back(paramRoute({ParamRef::Kind::Device, d.id.value, 0}));
                    d.modulators.push_back(m);
                }
                knobs.push_back({t.id, {ParamRef::Kind::Device, d.id.value, uint16_t(range(0, 2))}});
                t.devices.push_back(d);
            }
            if (coin()) {
                Modulator m = modulator(s);
                for (const auto& [track, ref] : knobs)
                    if (track == t.id && m.routes.size() < 2)
                        m.routes.push_back(paramRoute(ref));
                Modulation out;
                out.kind = Modulation::Kind::AutoChannel;
                out.track = t.id;
                out.channel = int8_t(range(0, 7));
                out.amount = unit() * 2 - 1;
                m.routes.push_back(out);
                t.modulators.push_back(m);
            }
            if (!knobs.empty() && knobs.back().first == t.id && coin()) {
                AutomationEnvelope a = lane(s);
                a.paramTarget = knobs.back().second;
                t.automation.push_back(a);
            }
        }
        int songMods = int(range(0, 2));
        for (int i = 0; i < songMods; i++) {
            Modulator m = modulator(s);
            if (!knobs.empty())
                m.routes.push_back(paramRoute(knobs[size_t(range(0, int64_t(knobs.size()) - 1))].second));
            Modulation knob;
            knob.kind = Modulation::Kind::RackKnob;
            knob.knob = {int64_t(range(1, 1000)), int(range(0, 20)), unit() * 0.5f, 0.5f + unit() * 0.5f};
            knob.amount = unit() * 2 - 1;
            m.routes.push_back(knob);
            s.modulators.push_back(m);
        }
    }

    Modulator modulator(Song& s) {
        Modulator m = devices::makeModulator(ModulatorType(range(0, kModulatorTypeCount - 1)), s.newId<ModulatorId>());
        m.name = pick({"LFO", "Mod \"q\"", ""});
        for (float& p : m.params)
            p = unit();
        if (m.type == ModulatorType::Steps)
            for (float& v : m.steps)
                v = unit();
        return m;
    }

    Modulation paramRoute(ParamRef target) {
        Modulation r;
        r.target = target;
        r.amount = unit() * 2 - 1;
        return r;
    }

    AutomationEnvelope lane(Song& s) {
        AutomationEnvelope a;
        a.id = s.newId<AutomationId>();
        a.name = "Cutoff";
        a.color = color();
        a.outputIndex = int(range(0, 7));
        a.rangeMin = -5;
        a.rangeMax = 5;
        a.bipolar = coin();
        a.mute = coin();
        a.slewMs = unit() * 100;
        a.combine = coin() ? CombineMode::Override : CombineMode::Modulate;
        a.armed = coin();
        a.source = coin() ? AutoSource::Knob : AutoSource::Cv;
        a.recordMode = AutoRecordMode(range(0, 2));
        a.targets.push_back({int64_t(range(1, 1000)), int(range(0, 20)), unit() * 0.5f, 0.5f + unit() * 0.5f});
        for (int i = 0; i < 5; i++)
            a.points.push_back({range(0, 16) * kPpq, unit(), CurveType(range(0, 3)), unit() * 2 - 1});
        return a;
    }

    int64_t range(int64_t lo, int64_t hi) { return std::uniform_int_distribution<int64_t>(lo, hi)(rng); }
    float unit() { return std::uniform_real_distribution<float>(0, 1)(rng); }
    bool coin() { return range(0, 1) == 1; }
    uint32_t color() { return uint32_t(range(0, 0xFFFFFF)); }
    const char* pick(std::initializer_list<const char*> options) {
        return options.begin()[range(0, int64_t(options.size()) - 1)];
    }

    std::mt19937 rng;
};

} // namespace tessera::testing
