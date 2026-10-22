#include "Recorder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tessera {

namespace {

double positiveMod(double x, double m) {
    double r = std::fmod(x, m);
    return r < 0 ? r + m : r;
}

Tick floorTo(double tick, Tick step) { return Tick(std::floor(tick / double(step))) * step; }
Tick ceilTo(double tick, Tick step) { return Tick(std::ceil(tick / double(step))) * step; }

} // namespace

TakeNote Recorder::finish(const Open& o, int pitch, double playTick, float release) {
    TakeNote n{o.start, openLength(o, playTick), int8_t(pitch), o.velocity, release, o.channel};
    double length = double(n.length);
    n.pitchExpr = noteCurve(NoteCurve::Pitch, o.pitch, o.playStart, length);
    n.timbreExpr = noteCurve(NoteCurve::Timbre, o.timbre, o.playStart, length);
    n.pressureExpr = noteCurve(NoteCurve::Pressure, o.pressure, o.playStart, length);
    return n;
}

std::vector<TakeLane> Recorder::takeLanes(const Session& s) {
    std::vector<TakeLane> out;
    for (const Session::LaneRecording& r : s.lanes) {
        for (const autoEdits::Points& run : r.take.written()) {
            std::vector<double> x;
            std::vector<float> y;
            for (const AutoPoint& p : run) {
                x.push_back(double(p.t));
                y.push_back(p.value);
            }
            TakeLane lane;
            lane.source = r.source;
            for (size_t i : thinValues(x, y, AutomationTake::kThinTolerance, run.size()))
                lane.points.push_back(run[i]);
            out.push_back(std::move(lane));
        }
    }
    for (const auto& [key, samples] : s.controls) {
        TakeLane lane;
        lane.control = controlOfKey(key);
        lane.points = lanePoints(lane.control, samples);
        if (!lane.points.empty())
            out.push_back(std::move(lane));
    }
    return out;
}

void Recorder::filterControls(TrackId track, std::vector<TakeLane>& lanes, std::vector<int>& reported) {
    const Song& song = host.song();
    const Track* t = song.findTrack(track);
    if (!t)
        return;
    std::vector<MidiControl> noLane, full;
    std::vector<AutomationEnvelope> pending; // the new lanes so far, so each takes its own channel
    auto report = [&](std::vector<MidiControl>& to, MidiControl c) {
        int key = controlKey(c);
        if (std::find(reported.begin(), reported.end(), key) == reported.end()) {
            reported.push_back(key);
            to.push_back(c);
        }
    };
    auto drop = [&](const TakeLane& l) {
        if (l.control.kind != MidiControl::Kind::Cc || l.source || midiLaneOf(song, *t, l.control))
            return false;
        if (isSetupControl(l.control))
            return true;
        if (!settings.newControlLanes) {
            report(noLane, l.control);
            return true;
        }
        // One value all through is a setting sent once (a sound change, a fader's position), not a move.
        bool moves = std::any_of(l.points.begin(), l.points.end(),
                                 [&](const AutoPoint& p) { return p.value != l.points.front().value; });
        if (!moves)
            return true;
        int output = midiLaneOutput(song, *t, l.control, pending);
        if (output < 0) {
            report(full, l.control);
            return true;
        }
        AutomationEnvelope lane;
        lane.outputIndex = output;
        lane.midi = l.control;
        pending.push_back(lane);
        return false;
    };
    lanes.erase(std::remove_if(lanes.begin(), lanes.end(), drop), lanes.end());
    if (!noLane.empty())
        host.controlsNotRecorded(track, noLane, false);
    if (!full.empty())
        host.controlsNotRecorded(track, full, true);
}

Recorder::Session* Recorder::find(TrackId track) {
    for (Session& s : active)
        if (s.track == track)
            return &s;
    return nullptr;
}

int Recorder::emptySlot(const Track& track) {
    int n = int(track.slots.size());
    int scene = std::clamp(host.targetScene(), 0, std::max(0, n - 1));
    for (int k = 0; k < n; k++) {
        int i = (scene + k) % n;
        if (!track.slots[size_t(i)].clip)
            return i;
    }
    return -1;
}

Recorder::Session* Recorder::start(const RecordEvent& e) {
    const Song& song = host.song();
    const Track* t = song.findTrack(TrackId(e.track));
    if (!t)
        return nullptr;
    Session s;
    s.track = t->id;
    const ClipSlot* playing = e.slot >= 0 && e.slot < int(t->slots.size()) ? &t->slots[size_t(e.slot)] : nullptr;
    const Clip* clip = playing && playing->clip ? song.findClip(*playing->clip) : nullptr;
    if (clip) {
        s.target = Target::Overdub;
        s.slot = e.slot;
        s.clip = clip->id;
        s.clipStart = e.clipStart;
        ClipSpan span = clipSpan(*clip);
        s.loopLength = span.looping ? span.length() : 0;
        s.passFrom = double(span.start);
        s.pass = passOf(s, e.playTick);
    } else if (host.playMode() == PlayMode::Arranger) {
        s.target = Target::Arranger;
        s.passFrom = punchSong;
    } else {
        s.target = Target::NewClip;
        s.slot = emptySlot(*t);
        if (s.slot < 0)
            return nullptr; // no empty slot to record into
        Tick bar = ticksPerBar(song.timeSig);
        // The clip starts at the bar recording started in, so notes keep their place in the bar.
        s.clipStart = punchPlay - positiveMod(punchSong, double(bar));
        s.fixedLength = Tick(settings.clipBars) * bar;
    }
    active.push_back(std::move(s));
    return &active.back();
}

Tick Recorder::targetTick(const Session& s, const RecordEvent& e) const {
    switch (s.target) {
    case Target::Arranger:
        return Tick(std::llround(e.songTick));
    case Target::Overdub:
        // Clip ticks from the loop start, not yet wrapped; recordIntoClip wraps them into the loop.
        return Tick(s.passFrom) + Tick(std::llround(e.playTick - s.clipStart));
    case Target::NewClip:
    default:
        return Tick(std::llround(e.playTick - s.clipStart));
    }
}

int64_t Recorder::passOf(const Session& s, double playTick) const {
    if (s.loopLength <= 0)
        return 0;
    return int64_t(std::floor((playTick - s.clipStart) / double(s.loopLength)));
}

void Recorder::closeOpen(Session& s, double playTick) {
    for (const auto& [pitch, o] : s.open)
        s.done.push_back(finish(o, pitch, playTick, 0.5f));
    s.open.clear();
}

void Recorder::quantize(ClipId clip, const edits::IdSet& ids) { quantize({{clip, ids}}); }

void Recorder::quantize(const std::vector<std::pair<ClipId, edits::IdSet>>& clips) {
    if (settings.quantize <= 0)
        return;
    auto out = std::make_unique<CompoundCommand>("Quantize recording");
    for (const auto& [clip, ids] : clips)
        out->add(quantizeTake(host.song(), clip, ids, settings.quantize, settings.strength));
    if (!out->empty())
        host.perform(std::move(out));
}

void Recorder::commit(Session& s, double endPlay, double endSong, bool playing) {
    Take take(std::move(s.done));
    take.lanes = takeLanes(s);
    filterControls(s.track, take.lanes, s.reported);
    s.done.clear();
    s.controls.clear();
    for (Session::LaneRecording& r : s.lanes)
        r.take = AutomationTake(r.source.recordMode);
    if (take.empty())
        return; // nothing finished: no empty clips or steps
    const Song& song = host.song();
    Tick bar = ticksPerBar(song.timeSig);
    NextId nextId = [this] { return host.nextId(); };
    ClipId clip;
    edits::IdSet ids;
    switch (s.target) {
    case Target::NewClip: {
        Tick length = s.fixedLength;
        if (length == 0) {
            Tick end = Tick(std::llround(endPlay - s.clipStart));
            for (const TakeNote& n : take.notes)
                end = std::max(end, n.start + 1);
            length = wholeBars(end, song.timeSig);
        }
        CommandPtr command = recordNewClip(song, s.track, s.slot, length, take, nextId, &clip, &ids);
        if (!command)
            return;
        host.perform(std::move(command));
        quantize(clip, ids);
        if (playing)
            host.launchAt(s.track, s.slot, s.clipStart);
        // From here on it is a loop overdub of the new clip.
        s.target = Target::Overdub;
        s.clip = clip;
        s.loopLength = length;
        s.passFrom = 0;
        s.pass = passOf(s, endPlay);
        return;
    }
    case Target::Overdub: {
        CommandPtr command = recordIntoClip(song, s.clip, take, nextId, &ids);
        if (!command)
            return;
        host.perform(std::move(command));
        quantize(s.clip, ids);
        return;
    }
    case Target::Arranger: {
        // Knob and AUTO REC moves on the timeline stay on the track's lanes: song-wide, like a mix.
        std::vector<TakeLane> trackLanes;
        const Track* track = song.findTrack(s.track);
        auto onTrack = [&](const TakeLane& l) {
            return l.source && track &&
                   std::any_of(track->automation.begin(), track->automation.end(),
                               [&](const AutomationEnvelope& a) { return a.id == l.source->id; });
        };
        for (const TakeLane& l : take.lanes)
            if (onTrack(l))
                trackLanes.push_back(l);
        take.lanes.erase(std::remove_if(take.lanes.begin(), take.lanes.end(), onTrack), take.lanes.end());
        auto out = std::make_unique<CompoundCommand>(take.notes.empty() ? "Record"
                                                                        : "Record " + countNotes(take.notes.size()));
        std::vector<std::pair<ClipId, edits::IdSet>> merged;
        ArrangerRecordMode mode = settings.arranger;
        if (mode == ArrangerRecordMode::Merge) {
            // What was played where no clip plays still makes a clip, as Stack would.
            Take rest;
            out->add(mergeIntoArrangement(song, s.track, take, nextId, rest, &merged));
            take = std::move(rest);
            mode = ArrangerRecordMode::Stack;
        }
        if (!take.empty()) {
            // After a merge only what fell outside the clips is left: its clip covers just that.
            bool rest = settings.arranger == ArrangerRecordMode::Merge;
            double first = rest ? INFINITY : s.passFrom, last = rest ? -INFINITY : endSong;
            for (const TakeNote& n : take.notes) {
                first = std::min(first, double(n.start));
                last = std::max(last, double(n.start + 1));
            }
            for (const TakeLane& l : take.lanes)
                for (const AutoPoint& p : l.points) {
                    first = std::min(first, double(p.t));
                    last = std::max(last, double(p.t + 1));
                }
            Tick from = std::max<Tick>(0, floorTo(first, bar));
            Tick to = std::max(from + bar, ceilTo(last, bar));
            out->add(recordToArrangement(song, s.track, from, to, take, mode, nextId, &clip, &ids));
        }
        out->add(recordTrackLanes(song, s.track, trackLanes));
        if (out->empty())
            return;
        host.perform(std::move(out));
        if (clip.valid())
            merged.push_back({clip, ids});
        quantize(merged);
        return;
    }
    }
}

void Recorder::handle(const RecordEvent& e) {
    switch (e.kind) {
    case RecordEvent::Kind::PunchIn:
        punched = true;
        punchPlay = e.playTick;
        punchSong = e.songTick;
        active.clear();
        return;
    case RecordEvent::Kind::PunchOut:
        for (Session& s : active) {
            closeOpen(s, e.playTick);
            commit(s, e.playTick, e.songTick, e.playing);
        }
        active.clear();
        punched = false;
        return;
    case RecordEvent::Kind::LoopWrap: {
        double loopEnd = double(host.song().loopEnd);
        for (Session& s : active) {
            if (s.target != Target::Arranger)
                continue;
            commit(s, e.playTick, loopEnd, true);
            s.passFrom = e.songTick;
        }
        return;
    }
    case RecordEvent::Kind::NoteValue:
    case RecordEvent::Kind::ChannelValue:
        handleValue(e);
        return;
    case RecordEvent::Kind::LaneValue:
        handleLaneValue(e);
        return;
    case RecordEvent::Kind::Note:
        break;
    }

    if (e.track == 0) {
        if (!e.recording)
            bufferNote(e);
        return;
    }
    if (!punched)
        return;
    Session* s = find(TrackId(e.track));
    if (!s) {
        if (!e.on)
            return;
        s = start(e);
        if (!s)
            return;
    }
    if (e.on) {
        s->open[e.pitch] = Open{e.playTick, targetTick(*s, e), e.velocity, e.channel};
        return;
    }
    auto it = s->open.find(e.pitch);
    if (it == s->open.end())
        return;
    s->done.push_back(finish(it->second, e.pitch, e.playTick, e.velocity));
    s->open.erase(it);
}

void Recorder::handleValue(const RecordEvent& e) {
    if (e.track == 0) {
        if (!e.recording)
            bufferValue(e);
        return;
    }
    if (!punched)
        return;
    Session* s = find(TrackId(e.track));
    if (e.kind == RecordEvent::Kind::NoteValue) {
        // Only a held note has curves to write into.
        if (!s || !s->open.count(e.pitch))
            return;
        Open& o = s->open[e.pitch];
        Samples& to = e.curve == NoteCurve::Pitch ? o.pitch : e.curve == NoteCurve::Timbre ? o.timbre : o.pressure;
        to.push_back({e.playTick, e.value});
        return;
    }
    if (!s && !(s = start(e)))
        return;
    int key = controlKey(e.control);
    auto it = std::find_if(s->controls.begin(), s->controls.end(), [key](const auto& c) { return c.first == key; });
    if (it == s->controls.end())
        it = s->controls.insert(s->controls.end(), {key, Samples{}});
    it->second.push_back({double(targetTick(*s, e)), e.value});
}

const AutomationEnvelope* Recorder::findLane(const Track& track, AutomationId id) const {
    for (const AutomationEnvelope& l : track.automation)
        if (l.id == id)
            return &l;
    for (const ClipSlot& slot : track.slots)
        if (slot.clip)
            if (const Clip* c = host.song().findClip(*slot.clip))
                for (const AutomationEnvelope& l : c->automation)
                    if (l.id == id)
                        return &l;
    return nullptr;
}

void Recorder::handleLaneValue(const RecordEvent& e) {
    if (!punched || e.track == 0)
        return;
    const Track* track = host.song().findTrack(TrackId(e.track));
    const AutomationEnvelope* source = track ? findLane(*track, AutomationId(e.lane)) : nullptr;
    if (!source)
        return;
    Session* s = find(TrackId(e.track));
    if (!s && !(s = start(e)))
        return;
    auto it = std::find_if(s->lanes.begin(), s->lanes.end(),
                           [&](const Session::LaneRecording& r) { return r.source.id == source->id; });
    if (it == s->lanes.end())
        it = s->lanes.insert(s->lanes.end(), {*source, AutomationTake(source->recordMode)});
    it->take.sample(targetTick(*s, e), e.value, e.seconds);
}

bool Recorder::laneWriting(AutomationId lane) const {
    for (const Session& s : active)
        for (const Session::LaneRecording& r : s.lanes)
            if (r.source.id == lane && r.take.writing())
                return true;
    return false;
}

std::vector<std::vector<AutoPoint>> Recorder::wrapRuns(const Session& s, const std::vector<AutoPoint>& points) {
    std::vector<std::vector<AutoPoint>> out(1);
    Tick last = std::numeric_limits<Tick>::min();
    Tick start = Tick(s.passFrom);
    for (AutoPoint p : points) {
        if (s.loopLength > 0)
            p.t = start + ((p.t - start) % s.loopLength + s.loopLength) % s.loopLength;
        if (p.t < last)
            out.emplace_back();
        out.back().push_back(p);
        last = p.t;
    }
    return out;
}

std::vector<std::vector<AutoPoint>> Recorder::liveRuns(LaneOwner owner, const AutomationEnvelope& lane) const {
    std::vector<std::vector<AutoPoint>> out;
    for (const Session& s : active) {
        if (s.target == Target::Arranger && owner.track == s.track) {
            for (const Session::LaneRecording& r : s.lanes)
                if (r.source.id == lane.id)
                    for (const autoEdits::Points& run : r.take.written())
                        out.push_back(run);
        } else if (s.target == Target::Overdub && owner.clip == s.clip) {
            for (const Session::LaneRecording& r : s.lanes)
                if (!lane.midi.valid() && sameTarget(r.source, lane))
                    for (const autoEdits::Points& run : r.take.written())
                        for (auto& wrapped : wrapRuns(s, run))
                            out.push_back(std::move(wrapped));
            if (lane.midi.valid())
                for (const auto& [key, samples] : s.controls)
                    if (key == controlKey(lane.midi)) {
                        std::vector<AutoPoint> run;
                        for (const auto& [t, v] : samples)
                            run.push_back({Tick(std::llround(t)), v, CurveType::Linear, 0});
                        for (auto& wrapped : wrapRuns(s, run))
                            out.push_back(std::move(wrapped));
                    }
        }
    }
    return out;
}

void Recorder::update(double playTick) {
    for (Session& s : active) {
        if (s.target == Target::NewClip && s.fixedLength > 0 && playTick - s.clipStart >= double(s.fixedLength)) {
            commit(s, s.clipStart + double(s.fixedLength), 0, true);
        } else if (s.target == Target::Overdub && s.loopLength > 0) {
            int64_t pass = passOf(s, playTick);
            if (pass != s.pass) {
                commit(s, playTick, 0, true);
                s.pass = pass;
            }
        }
    }
}

// ---- Capture ------------------------------------------------------------------------------------------------

void Recorder::bufferNote(const RecordEvent& e) {
    if (e.on) {
        bufferOpen[e.pitch] =
            Captured{e.freeTick, e.playTick, e.songTick, 0, e.playing, e.pitch, e.velocity, 0.5f, e.channel};
        return;
    }
    auto it = bufferOpen.find(e.pitch);
    if (it == bufferOpen.end())
        return;
    Captured c = it->second;
    bufferOpen.erase(it);
    c.length = std::max(1.0, e.freeTick - c.freeStart);
    c.release = e.velocity;
    // A note held across start or stop has no single timeline to sit on; place it by the free clock.
    c.playing = c.playing && e.playing;
    buffer.push_back(c);
    double keep = double(kBufferBars * ticksPerBar(host.song().timeSig));
    while (!buffer.empty() && buffer.front().freeStart < e.freeTick - keep)
        buffer.pop_front();
}

void Recorder::bufferValue(const RecordEvent& e) {
    if (e.kind == RecordEvent::Kind::NoteValue) {
        auto it = bufferOpen.find(e.pitch);
        if (it == bufferOpen.end())
            return;
        Captured& c = it->second;
        Samples& to = e.curve == NoteCurve::Pitch ? c.pitchCurve : e.curve == NoteCurve::Timbre ? c.timbre : c.pressure;
        to.push_back({e.freeTick, e.value});
        return;
    }
    bufferValues.push_back({e.freeTick, e.playTick, e.playing, controlKey(e.control), e.value});
    // As long as the notes are kept; a control swept nonstop is capped so the buffer stays small.
    constexpr size_t kMaxValues = 1 << 16;
    double keep = double(kBufferBars * ticksPerBar(host.song().timeSig));
    while (!bufferValues.empty() &&
           (bufferValues.front().freeTick < e.freeTick - keep || bufferValues.size() > kMaxValues))
        bufferValues.pop_front();
}

std::vector<TakeLane> Recorder::capturedLanes(double from, double to, double offset) const {
    // Controls that moved during the phrase, each starting where it was when the phrase began.
    std::vector<std::pair<int, Samples>> moved;
    auto samplesOf = [&](int key) -> Samples& {
        auto it = std::find_if(moved.begin(), moved.end(), [key](const auto& m) { return m.first == key; });
        if (it == moved.end())
            it = moved.insert(moved.end(), {key, Samples{}});
        return it->second;
    };
    for (const CapturedValue& v : bufferValues)
        if (v.freeTick >= from && v.freeTick <= to)
            samplesOf(v.control).push_back({std::max(0.0, v.freeTick - from + offset), v.value});
    std::vector<TakeLane> out;
    for (auto& [key, samples] : moved) {
        const CapturedValue* before = nullptr;
        for (const CapturedValue& v : bufferValues)
            if (v.control == key && v.freeTick < from)
                before = &v;
        if (before)
            samples.insert(samples.begin(), {offset, before->value});
        TakeLane lane;
        lane.control = controlOfKey(key);
        lane.points = lanePoints(lane.control, samples);
        if (!lane.points.empty())
            out.push_back(std::move(lane));
    }
    return out;
}

ClipId Recorder::capture(TrackId track) {
    const Song& song = host.song();
    const Track* t = song.findTrack(track);
    if (!t || buffer.empty())
        return ClipId();
    int slot = emptySlot(*t);
    if (slot < 0)
        return ClipId();

    std::vector<Captured> notes(buffer.begin(), buffer.end());
    std::stable_sort(notes.begin(), notes.end(),
                     [](const Captured& a, const Captured& b) { return a.freeStart < b.freeStart; });
    // The last phrase: walk back until a silence of kCaptureGapBars.
    Tick bar = ticksPerBar(song.timeSig);
    double gap = double(kCaptureGapBars * bar);
    std::vector<double> endBefore(notes.size());
    double end = -INFINITY;
    for (size_t i = 0; i < notes.size(); i++) {
        endBefore[i] = end;
        end = std::max(end, notes[i].freeStart + notes[i].length);
    }
    size_t first = notes.size() - 1;
    while (first > 0 && notes[first].freeStart - endBefore[first] < gap)
        first--;

    bool onTimeline =
        std::all_of(notes.begin() + long(first), notes.end(), [](const Captured& c) { return c.playing; });
    double base;
    if (onTimeline) {
        // Played along with the song: keep the notes' place in the bar.
        base = notes[first].playStart - positiveMod(notes[first].songStart, double(bar));
    } else {
        Tick beat = ticksPerBeat(song.timeSig);
        base = std::floor(notes[first].freeStart / double(beat)) * double(beat);
    }
    Take take;
    Tick last = 1;
    double phraseEnd = notes[first].freeStart;
    for (size_t i = first; i < notes.size(); i++) {
        const Captured& c = notes[i];
        Tick start = Tick(std::llround((onTimeline ? c.playStart : c.freeStart) - base));
        TakeNote n{std::max<Tick>(0, start), Tick(std::llround(c.length)), c.pitch, c.velocity, c.release, c.channel};
        n.pitchExpr = noteCurve(NoteCurve::Pitch, c.pitchCurve, c.freeStart, c.length);
        n.timbreExpr = noteCurve(NoteCurve::Timbre, c.timbre, c.freeStart, c.length);
        n.pressureExpr = noteCurve(NoteCurve::Pressure, c.pressure, c.freeStart, c.length);
        take.notes.push_back(std::move(n));
        last = std::max(last, take.notes.back().start + take.notes.back().length);
        phraseEnd = std::max(phraseEnd, c.freeStart + c.length);
    }
    // Controls are placed by the free clock from the first note, which runs with the song while it plays.
    take.lanes = capturedLanes(notes[first].freeStart, phraseEnd, double(take.notes.front().start));
    std::vector<int> reported;
    filterControls(track, take.lanes, reported);
    ClipId clip;
    edits::IdSet ids;
    CommandPtr command = recordNewClip(
        song, track, slot, wholeBars(last, song.timeSig), take, [this] { return host.nextId(); }, &clip, &ids,
        "Capture " + countNotes(take.notes.size()));
    if (!command)
        return ClipId();
    host.perform(std::move(command));
    quantize(clip, ids);
    buffer.clear();
    bufferValues.clear();
    return clip;
}

} // namespace tessera
