#include "Recorder.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

namespace {

double positiveMod(double x, double m) {
    double r = std::fmod(x, m);
    return r < 0 ? r + m : r;
}

Tick floorTo(double tick, Tick step) { return Tick(std::floor(tick / double(step))) * step; }
Tick ceilTo(double tick, Tick step) { return Tick(std::ceil(tick / double(step))) * step; }

} // namespace

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
        s.done.push_back(TakeNote{o.start, openLength(o, playTick), int8_t(pitch), o.velocity, 0.5f, o.channel});
    s.open.clear();
}

void Recorder::quantize(ClipId clip, const edits::IdSet& ids) {
    if (settings.quantize > 0)
        host.perform(quantizeTake(host.song(), clip, ids, settings.quantize, settings.strength));
}

void Recorder::commit(Session& s, double endPlay, double endSong, bool playing) {
    if (s.done.empty())
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
            for (const TakeNote& n : s.done)
                end = std::max(end, n.start + 1);
            length = wholeBars(end, song.timeSig);
        }
        CommandPtr command = recordNewClip(song, s.track, s.slot, length, s.done, nextId, &clip, &ids);
        s.done.clear();
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
        CommandPtr command = recordIntoClip(song, s.clip, s.done, nextId, &ids);
        s.done.clear();
        if (!command)
            return;
        host.perform(std::move(command));
        quantize(s.clip, ids);
        return;
    }
    case Target::Arranger: {
        double first = s.passFrom, last = endSong;
        for (const TakeNote& n : s.done) {
            first = std::min(first, double(n.start));
            last = std::max(last, double(n.start + 1));
        }
        Tick from = std::max<Tick>(0, floorTo(first, bar));
        Tick to = std::max(from + bar, ceilTo(last, bar));
        CommandPtr command =
            recordToArrangement(song, s.track, from, to, s.done, settings.arranger, nextId, &clip, &ids);
        s.done.clear();
        if (!command)
            return;
        host.perform(std::move(command));
        quantize(clip, ids);
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
    const Open& o = it->second;
    s->done.push_back(TakeNote{o.start, openLength(o, e.playTick), e.pitch, o.velocity, e.velocity, o.channel});
    s->open.erase(it);
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
    std::vector<TakeNote> take;
    Tick last = 1;
    for (size_t i = first; i < notes.size(); i++) {
        const Captured& c = notes[i];
        Tick start = Tick(std::llround((onTimeline ? c.playStart : c.freeStart) - base));
        take.push_back(TakeNote{std::max<Tick>(0, start), Tick(std::llround(c.length)), c.pitch, c.velocity, c.release,
                                c.channel});
        last = std::max(last, take.back().start + take.back().length);
    }
    ClipId clip;
    edits::IdSet ids;
    CommandPtr command = recordNewClip(
        song, track, slot, wholeBars(last, song.timeSig), take, [this] { return host.nextId(); }, &clip, &ids,
        "Capture " + countNotes(take.size()));
    if (!command)
        return ClipId();
    host.perform(std::move(command));
    quantize(clip, ids);
    buffer.clear();
    return clip;
}

} // namespace tessera
