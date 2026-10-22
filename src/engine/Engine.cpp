#include "Engine.hpp"

#include "../model/Recording.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tessera {

namespace {

/// True when a multiple of `period` lies in [from, to).
bool crossesMultiple(double from, double to, double period) { return std::ceil(from / period) * period < to; }

double positiveMod(double x, double m) {
    double r = std::fmod(x, m);
    return r < 0 ? r + m : r;
}

} // namespace

Engine::Engine()
    : chains(std::make_unique<std::array<ChainRuntime, kMaxTracks>>()),
      knobValues(std::make_unique<float[]>(size_t(kMaxKnobs))),
      modStates(std::make_unique<std::array<mods::State, kMaxModulatorSlots>>()),
      liveKnobs(new std::atomic<float>[size_t(kMaxKnobs)]()), liveBases(new std::atomic<float>[size_t(kMaxKnobs)]()),
      liveModulators(new std::atomic<float>[size_t(kMaxModulatorSlots)]()) {
    indexOfSlot.fill(-1);
    setSampleRate(sampleRate);
}

/// Where chain notes leave: the engine's voices.
class Engine::ChainSink : public chain::Output {
  public:
    ChainSink(Engine& engine, const TrackPlayback& track, TrackState& state)
        : engine(engine), track(track), state(state) {}
    void on(const chain::ChainNote& note) override { engine.sound(track, state, note); }
    void end(uint32_t id) override { engine.endNote(track, state, id); }

  private:
    Engine& engine;
    const TrackPlayback& track;
    TrackState& state;
};

void Engine::setSampleRate(double sr) {
    sampleRate = sr;
    setTempo(bpm);
    retriggerGap = int(std::ceil(sampleRate * 0.001)); // >= 1 ms so envelopes retrigger
    for (TrackState& t : tracks) {
        t.voices.setRetriggerGap(retriggerGap);
        t.drums.setRetriggerGap(retriggerGap);
        t.slewMs.fill(-1); // slew coefficients depend on the sample rate: work them out again
    }
}

void Engine::setTempo(double newBpm) {
    bpm = std::clamp(newBpm, kMinBpm, kMaxBpm);
    ticksPerSample = bpm * kPpq / 60.0 / sampleRate;
}

void Engine::setSnapshot(const Snapshot* s) {
    wakeAll();
    frameEpoch++;
    activeMask = 0;
    clearUnused = 2;
    // Carry each track's voices over by track ID, so editing while playing never cuts sounding notes.
    // Voices know their own end tick, so nothing can hang. Tracks that left the snapshot are dropped,
    // which silences them: their bus slot is no longer written.
    previous = tracks;
    size_t previousCount = trackCount;
    snapshot = s;
    trackCount = s ? std::min(s->tracks.size(), tracks.size()) : 0;
    slotUsed.fill(false);
    indexOfSlot.fill(-1);
    for (size_t i = 0; i < trackCount; i++)
        if (s->tracks[i]->busSlot >= 0 && s->tracks[i]->busSlot < kMaxTracks)
            indexOfSlot[size_t(s->tracks[i]->busSlot)] = int(i);

    for (size_t i = 0; i < trackCount; i++) {
        const TrackPlayback& tp = *s->tracks[i];
        TrackState& state = tracks[i];
        state = TrackState();
        for (size_t j = 0; j < previousCount; j++) {
            if (previous[j].id == tp.id) {
                state = previous[j];
                break;
            }
        }
        state.id = tp.id;
        // Extras live in the old snapshot, which is about to be freed: drop them, then find sounding notes' curves
        // again in the new one.
        std::array<uint64_t, kMaxVoices> clips = state.voiceClip, notes = state.voiceNote;
        clearPending(state);
        state.voiceClip = clips;
        state.voiceNote = notes;
        reattachCurves(tp, state);
        state.voices.setRetriggerGap(retriggerGap);
        state.drums.setRetriggerGap(retriggerGap);
        state.voices.setPolyphony(tp.polyphony);
        for (int v = state.voices.polyphony(); v < kMaxVoices; v++)
            if (state.audition[size_t(v)] >= 0)
                endAudition(state, v);
        state.chainInfo = i < s->chains.size() ? &s->chains[i] : nullptr;
        state.autoChannels = std::max(tp.autoChannels, state.chainInfo ? state.chainInfo->autoChannels : 0);
        // Lanes may have come or gone: live controls find their channels again when they next move.
        state.liveAutoMask = 0;
        state.liveCcOutput.fill(-1);
        state.liveCcCount = 0;
        resolveSlots(tp, state);
        if (tp.busSlot >= 0 && tp.busSlot < kMaxTracks)
            slotUsed[tp.busSlot] = true;
        state.busSlot = tp.busSlot >= 0 && tp.busSlot < kMaxTracks ? tp.busSlot : -1;
    }
    listening.fill(false);
    for (size_t i = 0; i < trackCount; i++) {
        const TrackPlayback& tp = *s->tracks[i];
        if ((tp.listen != ListenSource::TrackAfter && tp.listen != ListenSource::TrackBefore) || tp.busSlot < 0 ||
            tp.busSlot >= kMaxTracks)
            continue;
        // Follow the sources; reaching this track again is a loop.
        bool loops = false;
        TrackId at = tp.listenTrack;
        for (size_t step = 0; step <= trackCount && !loops; step++) {
            if (at == tp.id) {
                loops = true;
                break;
            }
            const TrackPlayback* next = nullptr;
            for (size_t j = 0; j < trackCount; j++)
                if (s->tracks[j]->id == at)
                    next = s->tracks[j].get();
            if (!next || (next->listen != ListenSource::TrackAfter && next->listen != ListenSource::TrackBefore))
                break;
            at = next->listenTrack;
        }
        listening[size_t(tp.busSlot)] = !loops;
    }
    // A silent track still runs its clips while an audible track listens to it, through a few listeners in a row.
    feeds.fill(false);
    for (int pass = 0; pass < kMaxForwardDepth; pass++)
        for (size_t i = 0; i < trackCount; i++) {
            const TrackPlayback& tp = *s->tracks[i];
            if (!listens(tp) || !runs(tp))
                continue;
            for (size_t j = 0; j < trackCount; j++) {
                const TrackPlayback& from = *s->tracks[j];
                if (from.id == tp.listenTrack && j != i && from.busSlot >= 0 && from.busSlot < kMaxTracks)
                    feeds[size_t(from.busSlot)] = true;
            }
        }
    for (size_t i = trackCount; i < tracks.size(); i++)
        tracks[i].chain = -1;
    // Tracks moved to new indices: wakeAll() above brought every voice clock to now (new tracks start there).
    clockOf.fill(clockAt);
    assignChains();
    for (size_t i = 0; i < trackCount; i++)
        if (!s->tracks[i]->audible)
            releaseAll(tracks[i]);
    knobsDirty = true;
    seekAll();
}

void Engine::assignChains() {
    // Runtimes stay with their track (by ID), so notes in flight carry over an edit.
    std::array<bool, kMaxTracks> taken{};
    for (size_t i = 0; i < trackCount; i++) {
        TrackState& state = tracks[i];
        state.chain = -1;
        for (size_t r = 0; r < chains->size(); r++) {
            if ((*chains)[r].owner == state.id) {
                state.chain = int(r);
                taken[r] = true;
                break;
            }
        }
    }
    for (size_t i = 0; i < trackCount; i++) {
        TrackState& state = tracks[i];
        if (state.chain < 0) {
            for (size_t r = 0; r < chains->size(); r++) {
                if (taken[r])
                    continue;
                taken[r] = true;
                ChainRuntime& fresh = (*chains)[r];
                fresh.owner = state.id;
                fresh.structure = state.chainInfo ? state.chainInfo->structure : 0;
                fresh.chain.clear();
                state.chain = int(r);
                break;
            }
        }
        if (state.chain < 0)
            continue;
        ChainRuntime& rt = (*chains)[size_t(state.chain)];
        // Expression curves live in the old snapshot, which is about to be freed.
        rt.chain.dropExtras();
        uint64_t structure = state.chainInfo ? state.chainInfo->structure : 0;
        if (rt.structure != structure) {
            // Devices came or went: what they held is dropped, and notes waiting for them to end end now.
            rt.structure = structure;
            rt.chain.clear();
            releaseOpenVoices(state);
        }
    }
    for (size_t r = 0; r < chains->size(); r++)
        if (!taken[r])
            (*chains)[r].owner = TrackId();
}

void Engine::releaseOpenVoices(TrackState& state) {
    for (int v = 0; v < state.voices.polyphony(); v++) {
        const VoiceAllocator::Voice& voice = state.voices.voice(v);
        if (voice.held && !std::isfinite(voice.endTick) && state.audition[size_t(v)] < 0)
            state.voices.release(v);
    }
}

int Engine::chainOverflows() const {
    int n = 0;
    for (const ChainRuntime& rt : *chains)
        n += rt.chain.overflowCount();
    return n;
}

void Engine::resolveSlots(const TrackPlayback& tp, TrackState& state) {
    // Slots are found again by clip, so inserting a scene or moving a clip doesn't change what plays.
    auto find = [&tp](int slot, ClipId clip) {
        if (tp.slot(slot) && tp.slotClips[size_t(slot)] == clip)
            return slot;
        for (size_t j = 0; j < tp.slots.size(); j++)
            if (tp.slots[j] && tp.slotClips[j] == clip)
                return int(j);
        return kNoSlot;
    };
    if (state.playing != kNoSlot) {
        state.playing = find(state.playing, state.playingClip);
        if (state.playing == kNoSlot) {
            state.playingClip = ClipId();
            releaseClipVoices(state);
        }
    }
    if (state.queued >= 0) {
        state.queued = find(state.queued, state.queuedClip);
        if (state.queued == kNoSlot)
            state.queued = kNothingQueued;
    }
}

void Engine::seek(double tick) {
    wakeAll();
    // A take can't jump on the timeline: what comes after the jump is a take of its own.
    bool wasRecording = recordingOn;
    if (wasRecording)
        stopRecording();
    songTick = std::max(0.0, tick);
    windowStart = songTick;
    // Notes of the old position stop; the arrangement picks up from the new one.
    for (size_t i = 0; i < trackCount; i++)
        if (followsArrangement(tracks[i]))
            releaseClipVoices(tracks[i]);
    seekAll();
    if (wasRecording)
        startRecording();
}

void Engine::setPlayMode(PlayMode m) {
    // The module sets the mode on every sample, so only a change may wake the tracks.
    if (m == mode)
        return;
    wakeAll();
    mode = m;
    for (size_t i = 0; i < trackCount; i++) {
        if (m == PlayMode::Arranger)
            backToArrangement(*snapshot->tracks[i], tracks[i]);
        else if (!tracks[i].overridden)
            releaseClipVoices(tracks[i]); // the arrangement goes quiet; launched clips play on
    }
}

void Engine::backToArrangement(const TrackPlayback& tp, TrackState& state) {
    if (state.overridden || state.playing != kNoSlot)
        releaseClipVoices(state);
    if (state.playing != kNoSlot)
        pushCaptureStop(tp, state);
    state.playing = kNoSlot;
    state.playingClip = ClipId();
    state.queued = kNothingQueued;
    state.overridden = false;
    seekTrack(tp, state);
}

void Engine::setCapture(CaptureQueue* queue, bool on) {
    captureQueue = queue;
    if (on && !capturing) {
        capturing = true;
        // Clips already playing are recorded from here.
        for (size_t i = 0; i < trackCount; i++)
            if (tracks[i].playing != kNoSlot)
                pushCapture(*snapshot->tracks[i], tracks[i], playTick);
    }
    capturing = on;
}

double Engine::songTickAt(double at) const {
    double t;
    if (at >= playTick)
        t = songTick + (at - playTick);
    else if (spanCount == 2 && at >= windowPlay + (spans[0][1] - spans[0][0]))
        t = spans[1][0] + (at - windowPlay - (spans[0][1] - spans[0][0]));
    else
        t = spans[0][0] + (at - windowPlay);
    if (loopOn && snapshot && snapshot->loopEnd > snapshot->loopStart && t >= double(snapshot->loopEnd))
        t = double(snapshot->loopStart) + (t - double(snapshot->loopEnd));
    return t;
}

double Engine::clipTimeAt(const TrackPlayback& tp, const TrackState& state, double at) const {
    const ClipPlayback* clip = tp.slot(state.playing);
    if (!clip)
        return 0;
    double a = at - state.launchTick;
    if (clip->looping)
        return double(clip->loopStart) + positiveMod(a, double(clip->loopLength()));
    return double(clip->loopStart) + a;
}

void Engine::pushCapture(const TrackPlayback& tp, const TrackState& state, double at) {
    if (!capturing || !captureQueue)
        return;
    CaptureEvent e;
    e.track = tp.id.value;
    e.songTick = songTickAt(at);
    if (state.playing >= 0) {
        e.clip = tp.slotClips[size_t(state.playing)].value;
        e.clipTick = clipTimeAt(tp, state, at);
    }
    captureQueue->push(e);
}

void Engine::pushCaptureStop(const TrackPlayback& tp, const TrackState& state) {
    if (!capturing || !captureQueue)
        return;
    CaptureEvent e;
    e.track = tp.id.value;
    e.songTick = songTick;
    captureQueue->push(e);
}

void Engine::releaseAll(TrackState& state) {
    if (state.chain >= 0)
        (*chains)[size_t(state.chain)].chain.clear();
    clearPending(state);
    state.voices.releaseAll();
    state.drums.releaseAll();
    state.audition.fill(-1);
    state.auditions = 0;
}

void Engine::endAudition(TrackState& state, int voice) {
    state.audition[size_t(voice)] = -1;
    state.auditions--;
}

void Engine::advanceAuditions(TrackState& state) {
    for (int v = 0; v < kMaxVoices; v++) {
        int32_t& left = state.audition[size_t(v)];
        if (left > 0 && --left == 0) {
            state.voices.release(v);
            endAudition(state, v);
        }
    }
}

void Engine::audition(const AuditionEvent& event) {
    wakeAll();
    for (size_t i = 0; i < trackCount; i++) {
        TrackState& state = tracks[i];
        if (state.id.value != event.track)
            continue;
        const TrackPlayback& tp = *snapshot->tracks[i];
        if (tp.drum) {
            // A drum lane plays its trigger or gate by itself; there is nothing to release.
            if (event.on)
                hitDrum(tp, state, event.pitch, event.velocity);
            return;
        }
        if (event.on) {
            int v = state.voices.noteOn(event.pitch, event.velocity, 0.5f, std::numeric_limits<double>::infinity(),
                                        event.id);
            clearLiveVoice(state, v);
            if (state.audition[size_t(v)] < 0)
                state.auditions++;
            state.audition[size_t(v)] = event.seconds > 0 ? std::max(1, int(event.seconds * sampleRate)) : 0;
        } else {
            for (int v = 0; v < state.voices.polyphony(); v++) {
                if (state.audition[size_t(v)] >= 0 && state.voices.voice(v).pitch == event.pitch) {
                    state.voices.release(v);
                    endAudition(state, v);
                }
            }
        }
        return;
    }
}

// ---- Recording ----------------------------------------------------------------------------------------------

void Engine::toggleRecord(int countInBars) {
    if (recordingOn || countInLength > 0) {
        stopRecording();
        countInLength = 0;
        return;
    }
    if (playing) {
        startRecording();
        return;
    }
    if (countInBars > 0) {
        TimeSig ts = snapshot ? snapshot->timeSig : TimeSig();
        countInLength = double(countInBars * ticksPerBar(ts));
        countInPos = 0;
        return;
    }
    play();
    startRecording();
}

void Engine::startRecording() {
    recordingOn = true;
    RecordEvent e;
    e.kind = RecordEvent::Kind::PunchIn;
    pushRecord(e);
}

void Engine::stopRecording() {
    if (!recordingOn)
        return;
    RecordEvent e;
    e.kind = RecordEvent::Kind::PunchOut;
    pushRecord(e);
    recordingOn = false;
}

void Engine::pushRecord(RecordEvent e) {
    if (!recordQueue)
        return;
    e.recording = recordingOn;
    e.playing = playing;
    e.playTick = playTick;
    e.songTick = songTick;
    e.freeTick = freeTick;
    recordQueue->push(e);
}

bool Engine::takesInput(const TrackPlayback& track, LiveSource source, int channel) {
    // Armed tracks take the input to record it; a track listening to the external inputs takes it always.
    if (!track.armed && track.listen != ListenSource::External)
        return false;
    if (!track.armed && source == LiveSource::Keyboard)
        return false; // the computer keyboard plays the armed tracks only
    // Playing the computer keyboard is meant for the armed tracks, whatever their input is set to.
    if (source == LiveSource::Keyboard)
        return true;
    switch (track.input) {
    case RecordInput::Cv:
        return source == LiveSource::Cv;
    case RecordInput::Midi:
        return source == LiveSource::Midi && (track.midiChannel == 0 || track.midiChannel == channel + 1);
    case RecordInput::Any:
    default:
        return source == LiveSource::Cv || track.midiChannel == 0 || track.midiChannel == channel + 1;
    }
}

void Engine::liveNote(const LiveNote& note) {
    wakeAll();
    if (!snapshot || note.pitch < 0)
        return;
    size_t src = size_t(note.source);
    size_t ch = note.channel & 15;
    size_t in = size_t(note.pitch) & 127;
    int part = note.partChannel >= 0 ? note.partChannel & 15 : int(ch);
    uint64_t& routed = liveTracks[src][ch][in];
    if (note.on) {
        if (routed != 0) // the same key again without a release: end the old note first
            liveNote(LiveNote{note.source, note.channel, note.pitch, 0, false, note.partChannel});
        livePitch[src][ch][in] = int8_t(liveToKey ? snapshot->key.snap(note.pitch) : note.pitch);
        for (size_t i = 0; i < trackCount; i++) {
            const TrackPlayback& tp = *snapshot->tracks[i];
            if (tp.busSlot >= 0 && takesInput(tp, note.source, part))
                routed |= uint64_t(1) << tp.busSlot;
        }
    }
    int8_t pitch = livePitch[src][ch][in];

    // The channel a recorded or forwarded note keeps: MIDI's own; CV's poly channels are voices, not parts.
    uint8_t noteChannel = note.source == LiveSource::Midi ? uint8_t(std::clamp(part + 1, 1, 16)) : 1;
    RecordEvent e;
    e.pitch = pitch;
    e.velocity = note.velocity;
    e.channel = noteChannel;
    e.on = note.on;
    pushRecord(e); // the capture buffer's copy
    for (size_t i = 0; i < trackCount && routed != 0; i++) {
        const TrackPlayback& tp = *snapshot->tracks[i];
        if (tp.busSlot < 0 || !(routed >> tp.busSlot & 1))
            continue;
        TrackState& state = tracks[i];
        // Drum tracks play the incoming note as is: it picks the lane.
        int8_t played = tp.drum ? note.pitch : pitch;
        uint32_t id = liveId(src, ch, in);
        // Armed, monitoring decides; a track that listens without being armed always plays what comes in.
        bool hear = hearsInput(tp);
        bool listened = tp.busSlot >= 0 && tp.busSlot < kMaxTracks && hasListeners(tp);
        if (hear) {
            if (note.on && (chainActive(state) || listened)) {
                chain::ChainNote n;
                n.start = n.origin = freeTick;
                n.pitch = played;
                n.velocity = note.velocity;
                n.channel = noteChannel;
                n.id = id;
                deliver(tp, state, n);
            } else if (note.on) {
                audition({tp.id.value, played, note.velocity, 0, true, id});
            } else {
                // Whichever way the key went in (the chain may have come or gone since), it ends both ways.
                if (state.chainInfo)
                    for (int slot : state.chainInfo->modulatorSlots)
                        mods::releaseNote((*modStates)[size_t(slot)]);
                endInput(tp, state, id);
                audition({tp.id.value, played, note.velocity, 0, false});
            }
        }
        if (!recordingOn || !tp.armed)
            continue;
        RecordEvent r = e;
        r.pitch = tp.drum ? note.pitch : pitch;
        pushTrackRecord(tp, state, r);
    }
    if (!note.on)
        routed = 0;
}

void Engine::liveValue(const LiveValue& lv) {
    wakeAll();
    if (!snapshot)
        return;
    size_t src = size_t(lv.source);
    size_t ch = lv.channel & 15;
    int part = lv.partChannel >= 0 ? lv.partChannel & 15 : int(ch);
    RecordEvent e;
    e.channel = uint8_t(part + 1);
    e.value = lv.value;
    if (!lv.perNote) {
        e.kind = RecordEvent::Kind::ChannelValue;
        e.control = lv.control;
        bool bend = lv.control.kind == MidiControl::Kind::PitchBend;
        // Lanes keep 0..1; pitch bend comes in as -1..1 around the middle.
        e.value = bend ? (lv.value + 1) / 2 : lv.value;
        pushRecord(e); // the capture buffer's copy
        for (size_t i = 0; i < trackCount; i++) {
            const TrackPlayback& tp = *snapshot->tracks[i];
            if (tp.busSlot < 0 || !takesInput(tp, lv.source, part))
                continue;
            TrackState& state = tracks[i];
            auto volts = [&](const TrackPlayback::LiveControl& c) { return c.low + e.value * (c.high - c.low); };
            switch (lv.control.kind) {
            case MidiControl::Kind::PitchBend:
                state.liveChannelBend = volts(tp.bendLive);
                break;
            case MidiControl::Kind::Pressure:
                state.liveChannelPress = e.value * 10.f;
                break;
            case MidiControl::Kind::Cc: {
                size_t cc = lv.control.cc & 127;
                TrackPlayback::LiveControl c = tp.ccLive[cc];
                if (c.output < 0 && newControlLanes && !isSetupControl(lv.control)) {
                    if (state.liveCcOutput[cc] < 0 && state.liveCcCount < tp.freeAutoCount)
                        state.liveCcOutput[cc] = tp.freeAuto[size_t(state.liveCcCount++)];
                    c.output = state.liveCcOutput[cc];
                }
                if (c.output >= 0) {
                    state.liveAuto[size_t(c.output)] = volts(c);
                    state.liveAutoMask |= uint8_t(1u << c.output);
                }
                break;
            }
            case MidiControl::Kind::None:
                break;
            }
            state.autoChanges++;
            if (recordingOn && tp.armed)
                pushTrackRecord(tp, state, e);
        }
        return;
    }
    e.kind = RecordEvent::Kind::NoteValue;
    e.curve = lv.curve;
    size_t first = lv.pitch < 0 ? 0 : size_t(lv.pitch & 127);
    size_t last = lv.pitch < 0 ? 127 : first;
    for (size_t in = first; in <= last; in++) {
        uint64_t routed = liveTracks[src][ch][in];
        if (routed == 0)
            continue;
        uint32_t id = liveId(src, ch, in);
        e.pitch = livePitch[src][ch][in];
        pushRecord(e); // the capture buffer's copy
        for (size_t i = 0; i < trackCount; i++) {
            const TrackPlayback& tp = *snapshot->tracks[i];
            if (tp.busSlot < 0 || !(routed >> tp.busSlot & 1) || tp.drum)
                continue;
            TrackState& state = tracks[i];
            int v = state.voices.find(id);
            if (v >= 0) {
                std::array<float, kMaxVoices>& target = lv.curve == NoteCurve::Pitch    ? state.liveBend
                                                        : lv.curve == NoteCurve::Timbre ? state.liveTimbre
                                                                                        : state.livePress;
                target[size_t(v)] = lv.value;
                state.autoChanges++;
            }
            if (recordingOn && tp.armed)
                pushTrackRecord(tp, state, e);
        }
    }
}

void Engine::pushTrackRecord(const TrackPlayback& tp, const TrackState& state, RecordEvent e) {
    e.track = tp.id.value;
    if (tp.slot(state.playing)) {
        e.slot = state.playing;
        e.clipStart = state.launchTick;
        e.clipTick = clipTimeAt(tp, state, playTick);
    }
    pushRecord(e);
}

void Engine::stop() {
    wakeAll();
    countInLength = 0;
    if (playing)
        for (size_t i = 0; i < trackCount; i++)
            if (tracks[i].playing != kNoSlot)
                pushCaptureStop(*snapshot->tracks[i], tracks[i]);
    playing = false;
    // After `playing` drops, so the recorder knows the take ended with the transport (its clip shouldn't play on).
    stopRecording();
    applyQueued();
    for (TrackState& t : tracks)
        releaseAll(t);
}

void Engine::reset() {
    wakeAll();
    bool wasRecording = recordingOn;
    if (wasRecording)
        stopRecording();
    songTick = (loopOn && snapshot) ? double(snapshot->loopStart) : 0.0;
    playTick = 0;
    applyQueued();
    for (TrackState& t : tracks) {
        t.launchTick = 0;
        releaseAll(t);
    }
    seekAll();
    resetPending = true;
    if (wasRecording)
        startRecording();
}

void Engine::applyQueued() {
    for (size_t i = 0; i < trackCount; i++)
        if (tracks[i].queued != kNothingQueued)
            switchTo(*snapshot->tracks[i], tracks[i], tracks[i].queued, playTick);
    if (pendingTempo > 0)
        pendingTempoAt = playTick;
}

void Engine::releaseClipVoices(TrackState& state) {
    // Live notes in the chain lose what the devices held for them; their key release still ends them.
    if (state.chain >= 0)
        (*chains)[size_t(state.chain)].chain.clear();
    state.pendingCount = 0;
    state.drums.releaseAll();
    for (int v = 0; v < state.voices.polyphony(); v++)
        if (state.audition[size_t(v)] < 0)
            state.voices.release(v);
}

void Engine::launch(const LaunchEvent& e) {
    wakeAll();
    if (!snapshot)
        return;
    bool startsSomething = false;
    for (size_t i = 0; i < trackCount; i++) {
        const TrackPlayback& tp = *snapshot->tracks[i];
        TrackState& state = tracks[i];
        switch (e.kind) {
        case LaunchEvent::Kind::Slot:
            if (state.id.value == e.track && (std::isfinite(e.at) || std::isfinite(e.clipTime))) {
                // In time with an earlier start: pick up the clip where it would be by now. Or from a chosen point.
                if (const ClipPlayback* clip = tp.slot(e.index)) {
                    double at = std::isfinite(e.clipTime) ? playTick - (e.clipTime - double(clip->loopStart)) : e.at;
                    releaseClipVoices(state);
                    switchTo(tp, state, e.index, at);
                    seekTrack(tp, state);
                    startsSomething |= std::isfinite(e.clipTime) && e.start; // a seek with play starts
                }
            } else if (state.id.value == e.track) {
                queueLaunch(tp, state, e.index, e.immediate);
                startsSomething |= tp.slot(e.index) != nullptr;
            }
            break;
        case LaunchEvent::Kind::StopTrack:
            if (state.id.value == e.track)
                queueLaunch(tp, state, kNoSlot, e.immediate);
            break;
        case LaunchEvent::Kind::Scene:
            queueLaunch(tp, state, e.index, e.immediate);
            startsSomething |= tp.slot(e.index) != nullptr;
            break;
        case LaunchEvent::Kind::StopAll:
            // Only launched clips: tracks playing the arrangement carry on.
            if (state.playing != kNoSlot)
                queueLaunch(tp, state, kNoSlot, e.immediate);
            break;
        case LaunchEvent::Kind::BackToArrangement:
            if (mode == PlayMode::Arranger)
                backToArrangement(tp, state);
            break;
        case LaunchEvent::Kind::Reset:
            backToArrangement(tp, state);
            break;
        }
    }
    if (e.kind == LaunchEvent::Kind::Scene && e.index >= 0 && e.index < int(snapshot->sceneTempos.size())) {
        if (const std::optional<double>& bpm = snapshot->sceneTempos[size_t(e.index)]) {
            pendingTempo = *bpm;
            pendingTempoAt = playTick;
            // The tempo changes with the scene's clips, so it waits for the same quantize line.
            double q = double(snapshot->launchQuantize);
            if (playing && !e.immediate && q > 0)
                pendingTempoAt = playTick + (std::ceil(songTick / q) * q - songTick);
        }
    }
    if (e.start && startsSomething && !playing)
        play();
}

void Engine::queueLaunch(const TrackPlayback& tp, TrackState& state, int slot, bool immediate) {
    const ClipPlayback* clip = tp.slot(slot);
    if (!clip)
        slot = kNoSlot;
    // Stopping a silent track does nothing; stopping one that plays the arrangement takes it over.
    if (slot == kNoSlot && state.playing == kNoSlot && !followsArrangement(state)) {
        state.queued = kNothingQueued;
        return;
    }
    double q = double(clip && clip->launchQuantize >= 0 ? clip->launchQuantize : snapshot->launchQuantize);
    double wait = q > 0 ? std::ceil(songTick / q) * q - songTick : 0;
    if (!playing || immediate || wait <= 0) {
        switchTo(tp, state, slot, playTick);
        return;
    }
    state.queued = slot;
    state.queuedClip = slot >= 0 ? tp.slotClips[size_t(slot)] : ClipId();
    state.queuedAt = playTick + wait;
}

void Engine::switchTo(const TrackPlayback& tp, TrackState& state, int slot, double at) {
    releaseClipVoices(state);
    state.playing = tp.slot(slot) ? slot : kNoSlot;
    state.playingClip = state.playing >= 0 ? tp.slotClips[size_t(slot)] : ClipId();
    state.queued = kNothingQueued;
    state.launchTick = at;
    state.cursor = 0;
    state.overridden = true;
    pushCapture(tp, state, at);
}

double Engine::clipPosition(size_t i) const {
    const TrackState& state = tracks[i];
    const ClipPlayback* clip = i < trackCount ? snapshot->tracks[i]->slot(state.playing) : nullptr;
    if (!clip)
        return NAN;
    double a = playTick - state.launchTick;
    if (clip->looping)
        return double(clip->loopStart) + positiveMod(a, double(clip->loopLength()));
    return a < double(clip->loopLength()) ? double(clip->loopStart) + a : NAN;
}

void Engine::process(TrackFrame* frames, TransportEvents& events) {
    events = TransportEvents();
    events.reset = resetPending;
    resetPending = false;
    double step = stepOverride >= 0 ? stepOverride : ticksPerSample;
    stepOverride = -1;
    freeTick += step;

    if (countInLength > 0) {
        // The count-in clicks its own bars, then the song starts recording where it stands.
        TimeSig ts = snapshot ? snapshot->timeSig : TimeSig();
        double prev = countInPos;
        countInPos += step;
        events.bar |= crossesMultiple(prev, countInPos, double(ticksPerBar(ts)));
        events.beat |= crossesMultiple(prev, countInPos, double(ticksPerBeat(ts)));
        if (countInPos >= countInLength) {
            countInLength = 0;
            play();
            startRecording();
        }
    }

    // Modulation runs at control rate, before the notes of this sample read the knobs it sets.
    uint64_t sample = sampleCount++;
    clockAt = sample;
    bool controlTick = (sample & (kControlBlock - 1)) == 0 || controlNow;
    controlNow = false;
    if (controlTick) {
        advanceKnobLanes();
        advanceModulation();
    }

    if (playing) {
        double prevPlay = playTick;
        windowPlay = prevPlay;
        playTick += step;
        advanceSong(step, events);
        // The sample the song loop wraps on wakes every track: that is where a track's place in the timeline jumps
        // back to the loop start. A track asleep through it (its wake time rounds a hair late, as play and song
        // ticks drift apart over a long session) would skip the notes at the loop start for a whole pass.
        bool wrapped = spanCount == 2;
        if (wrapped && playTick > windowPlay + (spans[0][1] - spans[0][0])) {
            // The loop starts over: notes still sounding on the arrangement stop (notes started inside the loop
            // already end at its end; this catches the rest). When the wrap falls exactly on this sample's end, they
            // still sound until then. Every track stops them before any plays the loop start: done track by track, a
            // listening track scheduled after its source would cut the loop start notes the source just handed it.
            for (size_t i = 0; i < trackCount; i++) {
                if (!followsArrangement(tracks[i]) || !runs(*snapshot->tracks[i]))
                    continue;
                catchUp(i, sample);
                releaseClipVoices(tracks[i]);
            }
        }
        for (size_t i = 0; i < trackCount; i++) {
            if (!wrapped && playTick < wakeAt[i])
                continue; // most tracks have nothing due on most samples
            catchUp(i, sample);
            scheduleTrackNow(*snapshot->tracks[i], tracks[i], prevPlay, playTick);
            wakeAt[i] = nextWake(*snapshot->tracks[i], tracks[i], playTick);
            idleUntil[i] = 0; // its outputs may have changed
        }
        if (events.loop && capturing)
            captureLoopWrap();
        if (events.loop && recordingOn) {
            RecordEvent e;
            e.kind = RecordEvent::Kind::LoopWrap;
            pushRecord(e);
        }
    } else {
        // Stopped: live notes still go through the chains, and the notes they made still end.
        for (size_t i = 0; i < trackCount; i++) {
            if (!controlTick && sample < idleUntil[i])
                continue; // no chain message or note end is due (idleUntilControl)
            catchUp(i, sample);
            TrackState& state = tracks[i];
            if (state.chain >= 0 && (*chains)[size_t(state.chain)].chain.nextDue() < freeTick)
                runChain(*snapshot->tracks[i], state, freeTick);
            if (state.voices.earliestEnd() < freeTick)
                state.voices.releaseEndedBefore(freeTick);
        }
    }
    if (pendingTempo > 0 && pendingTempoAt <= playTick) {
        events.tempo = pendingTempo;
        setTempo(pendingTempo);
        pendingTempo = 0;
    }
    events.running = playing;
    // Judged at the start of the sample's window, the same rule note-ons follow, so clock edges and notes line up.
    events.clock = playing && positiveMod(windowStart, double(clockDivision)) < clockDivision * 0.5;

    if (clearUnused > 0) {
        clearUnused--;
        for (int s = 0; s < kMaxTracks; s++) {
            if (!slotUsed[s]) {
                frames[s].trackId = 0;
                frames[s].channels = 0;
                frames[s].autoChannels = 0;
            }
        }
    }
    if (frames != buffers[0] && frames != buffers[1])
        buffers[buffers[0] ? 1 : 0] = frames; // a bus seen for the first time
    size_t parity = frames == buffers[1] ? 1 : 0;

    // Automation runs at control rate too: CV curves don't need every sample, and it is the costliest part.
    for (size_t i = 0; i < trackCount; i++) {
        if (!controlTick && sample < idleUntil[i])
            continue; // its outputs can't change before the next control tick
        catchUp(i, sample);
        TrackState& state = tracks[i];
        // The track's playback data is only looked at when there is work: a control tick or a changed frame.
        if (controlTick)
            advanceAutomation(*snapshot->tracks[i], state);
        if (state.busSlot >= 0) {
            uint64_t version = state.outputVersion(frameEpoch);
            if (state.curvedVoices > 0 || state.written[parity] != version) {
                writeFrame(*snapshot->tracks[i], state, frames[state.busSlot]);
                state.written[parity] = version;
            }
        }
        // The activity lights only need control rate too.
        if (controlTick) {
            uint64_t bit = uint64_t(1) << i;
            activeMask = (state.voices.anyGate() || state.drums.anyGate()) ? activeMask | bit : activeMask & ~bit;
        }
        if (state.auditions > 0)
            advanceAuditions(state);
        state.voices.advance();
        state.drums.advance();
        clockOf[i] = sample + 1;
        idleUntil[i] = idleUntilControl(state, sample) ? (sample | (kControlBlock - 1)) + 1 : 0;
    }
    clockAt = sample + 1; // events until the next process() happen at the next sample
}

bool Engine::idleUntilControl(const TrackState& state, uint64_t sample) const {
    uint64_t next = (sample | (kControlBlock - 1)) + 1;
    if (state.curvedVoices > 0 || state.auditions > 0 || state.drums.counting() ||
        !state.voices.quietFor(int64_t(next - sample)))
        return false; // something counts down or moves on every sample
    // Both bus buffers must hold the current outputs, or the skipped samples would publish an older one.
    uint64_t version = state.outputVersion(frameEpoch);
    if (state.busSlot >= 0 && (state.written[0] != version || state.written[1] != version))
        return false;
    // Playing, scheduling wakes the track for anything due. Stopped, nothing does: only idle without work in flight.
    if (!playing && (state.voices.anyEnding() || (state.chain >= 0 && !(*chains)[size_t(state.chain)].chain.idle())))
        return false;
    return true;
}

void Engine::advanceSong(double dt, TransportEvents& events) {
    double prev = songTick;
    double cur = prev + dt;

    // At most two spans: before and after a loop wrap.
    spans[0][0] = prev;
    spans[0][1] = cur;
    spans[1][0] = spans[1][1] = 0;
    spanCount = 1;
    if (loopOn && snapshot) {
        double loopStart = double(snapshot->loopStart);
        double loopEnd = double(snapshot->loopEnd);
        if (loopEnd > loopStart && prev < loopEnd && cur >= loopEnd) {
            cur = loopStart + (cur - loopEnd);
            spans[0][1] = loopEnd;
            spans[1][0] = loopStart;
            spans[1][1] = cur;
            spanCount = 2;
            events.loop = true;
        }
    }
    songTick = cur;
    // After a wrap the window continues from the loop start, unless that part of the window is still empty.
    windowStart = (spanCount == 2 && spans[1][1] > spans[1][0]) ? spans[1][0] : spans[0][0];

    TimeSig ts = snapshot ? snapshot->timeSig : TimeSig();
    for (int i = 0; i < spanCount; i++) {
        events.bar |= crossesMultiple(spans[i][0], spans[i][1], double(ticksPerBar(ts)));
        events.beat |= crossesMultiple(spans[i][0], spans[i][1], double(ticksPerBeat(ts)));
    }
}

double Engine::nextWake(const TrackPlayback& track, const TrackState& state, double now) const {
    // A hair early, so float rounding can never delay a note by a sample.
    constexpr double kEarly = 1e-6;
    // Voice ends and the chain's messages are on the engine clock.
    double wake = state.voices.earliestEnd() - clockOffset();
    if (state.chain >= 0)
        wake = std::min(wake, (*chains)[size_t(state.chain)].chain.nextDue() - clockOffset());
    for (int i = 0; i < state.pendingCount; i++)
        wake = std::min(wake, state.pending[size_t(i)].start);
    if (state.queued != kNothingQueued)
        wake = std::min(wake, state.queuedAt);
    if (followsArrangement(state)) {
        // The arrangement is flat and in song ticks, which keep pace with play ticks until the song loop wraps.
        if (runs(track) && state.arrangementCursor < track.arrangement.size())
            wake = std::min(wake, now + (track.arrangement[state.arrangementCursor].start - songTick));
        if (loopOn && snapshot->loopEnd > snapshot->loopStart && songTick < double(snapshot->loopEnd))
            wake = std::min(wake, now + (double(snapshot->loopEnd) - songTick));
        return wake - kEarly;
    }
    const ClipPlayback* clip = track.slot(state.playing);
    if (clip && runs(track)) {
        if (!clip->laneLoops.empty())
            return -INFINITY; // drum lanes with their own loops: every sample
        double length = double(clip->loopLength());
        double a = now - state.launchTick;
        if (!clip->looping) {
            if (state.cursor < clip->notes.size())
                wake = std::min(wake, state.launchTick + (clip->notes[state.cursor].start - double(clip->loopStart)));
            wake = std::min(wake, state.launchTick + length); // the clip ends
        } else if (length > 0) {
            double iter = std::floor(a / length);
            double next =
                state.cursor < clip->notes.size()
                    ? state.launchTick + iter * length + (clip->notes[state.cursor].start - double(clip->loopStart))
                    : state.launchTick + (iter + 1) * length; // the loop wraps (and the cursor restarts)
            wake = std::min(wake, next);
        }
    }
    return wake - kEarly;
}

void Engine::scheduleTrackNow(const TrackPlayback& track, TrackState& state, double prevPlay, double curPlay) {
    state.voices.releaseEndedBefore(curPlay + clockOffset());
    if (state.pendingCount > 0)
        firePending(track, state, curPlay);
    double from = prevPlay;
    if (state.queued != kNothingQueued && state.queuedAt < curPlay) {
        double at = std::max(state.queuedAt, prevPlay);
        playSource(track, state, from, at);
        switchTo(track, state, state.queued, at);
        from = at;
    }
    playSource(track, state, from, curPlay);
    if (state.chain >= 0)
        runChain(track, state, curPlay + clockOffset());
}

void Engine::playSource(const TrackPlayback& track, TrackState& state, double from, double to) {
    if (followsArrangement(state))
        playArrangement(track, state, from, to);
    else
        playClip(track, state, from, to);
}

void Engine::playArrangement(const TrackPlayback& track, TrackState& state, double from, double to) {
    if (!runs(track) || to <= from)
        return;
    // Play ticks map onto this sample's song spans; the second span starts after the loop wrapped.
    double wrapPlay = windowPlay + (spans[0][1] - spans[0][0]);
    if (from < wrapPlay) {
        double end = std::min(to, wrapPlay);
        fireArrangement(track, state, spans[0][0] + (from - windowPlay), spans[0][0] + (end - windowPlay), from);
    }
    // The loop starts over (process() already stopped the notes still sounding).
    if (spanCount == 2 && from <= wrapPlay && to >= wrapPlay)
        state.arrangementCursor = arrangementIndex(track, spans[1][0]);
    if (spanCount == 2 && to > wrapPlay) {
        double start = std::max(from, wrapPlay);
        fireArrangement(track, state, spans[1][0] + (start - wrapPlay), spans[1][0] + (to - wrapPlay), start);
    }
}

void Engine::fireArrangement(const TrackPlayback& track, TrackState& state, double from, double to, double playAtFrom) {
    // Inside the song loop, a note never sounds past the loop end.
    if (replacing(track)) {
        while (state.arrangementCursor < track.arrangement.size() &&
               track.arrangement[state.arrangementCursor].start < to)
            state.arrangementCursor++;
        return;
    }
    bool looping = loopOn && snapshot->loopEnd > snapshot->loopStart;
    double cap = looping && from < double(snapshot->loopEnd) ? double(snapshot->loopEnd) : INFINITY;
    fireList(track, state, track.arrangement, state.arrangementCursor, from, to, playAtFrom, cap, -1,
             &track.arrangementExtras);
}

size_t Engine::arrangementIndex(const TrackPlayback& track, double songPos) {
    auto it = std::lower_bound(track.arrangement.begin(), track.arrangement.end(), songPos,
                               [](const NoteEvent& n, double t) { return n.start < t; });
    return size_t(it - track.arrangement.begin());
}

void Engine::captureLoopWrap() {
    // The timeline starts over: what plays is recorded again from the loop start.
    double wrapPlay = windowPlay + (spans[0][1] - spans[0][0]);
    for (size_t i = 0; i < trackCount; i++) {
        const TrackPlayback& tp = *snapshot->tracks[i];
        TrackState& state = tracks[i];
        if (state.playing == kNoSlot)
            continue;
        CaptureEvent stop;
        stop.track = tp.id.value;
        stop.songTick = spans[0][1];
        captureQueue->push(stop);
        CaptureEvent start = stop;
        start.clip = tp.slotClips[size_t(state.playing)].value;
        start.songTick = spans[1][0];
        start.clipTick = clipTimeAt(tp, state, wrapPlay);
        captureQueue->push(start);
    }
}

void Engine::playClip(const TrackPlayback& track, TrackState& state, double from, double to) {
    const ClipPlayback* playing = track.slot(state.playing);
    if (!runs(track) || !playing || to <= from)
        return;
    const ClipPlayback& clip = *playing;
    double length = double(clip.loopLength());
    double a = from - state.launchTick;
    double b = to - state.launchTick;

    if (!clip.looping) {
        if (a < length)
            fireList(track, state, clip.notes, state.cursor, clip.loopStart + a, clip.loopStart + std::min(b, length),
                     from, INFINITY, 0, &clip.extras);
        if (b >= length) // a one-shot clip stops when it ends; its last notes end by themselves
            state.playing = kNoSlot;
        return;
    }
    playLoop(track, state, clip.notes, state.cursor, clip.loopStart, clip.loopLength(), a, b, from, &clip.extras);
    for (size_t k = 0; k < clip.laneLoops.size() && k < state.laneCursor.size(); k++) {
        const LaneLoop& lane = clip.laneLoops[k];
        playLoop(track, state, lane.notes, state.laneCursor[k], lane.start, lane.length, a, b, from, &clip.extras);
    }
}

void Engine::playLoop(const TrackPlayback& track, TrackState& state, const std::vector<NoteEvent>& notes,
                      size_t& cursor, Tick loopStart, Tick loopLength, double a, double b, double playAtFrom,
                      const std::vector<NoteExtras>* extras) {
    if (loopLength <= 0)
        return;
    // One sample is always shorter than a loop, so the window wraps at most once.
    double length = double(loopLength);
    double iterA = std::floor(a / length);
    double iterB = std::floor(b / length);
    double localA = double(loopStart) + (a - iterA * length);
    if (iterA == iterB) {
        fireList(track, state, notes, cursor, localA, localA + (b - a), playAtFrom, INFINITY, int64_t(iterA), extras);
    } else {
        double loopEnd = double(loopStart) + length;
        fireList(track, state, notes, cursor, localA, loopEnd, playAtFrom, INFINITY, int64_t(iterA), extras);
        cursor = 0;
        double localB = double(loopStart) + (b - iterB * length);
        fireList(track, state, notes, cursor, double(loopStart), localB, playAtFrom + (loopEnd - localA), INFINITY,
                 int64_t(iterB), extras);
    }
}

void Engine::fireList(const TrackPlayback& track, TrackState& state, const std::vector<NoteEvent>& notes,
                      size_t& cursor, double from, double to, double playAtFrom, double capEnd, int64_t pass,
                      const std::vector<NoteExtras>* extras) {
    // The cursor already skips notes fired earlier, so a note is never fired twice even when rounding moves
    // `from` a hair past a note that sat exactly on the previous window's end.
    while (cursor < notes.size() && notes[cursor].start < to) {
        const NoteEvent& n = notes[cursor++];
        double onPlay = playAtFrom + std::max(0.0, n.start - from);
        double end = std::min(double(n.end), capEnd);
        int64_t notePass = pass >= 0 ? pass : std::max<int64_t>(0, n.pass);
        if (notePass != state.lastPass) {
            // "Previous note" is within the pass: the first note of a pass has none.
            state.lastPass = notePass;
            state.previousPlayed = true;
        }
        if (extras && n.extras < extras->size()) {
            startWithOperators(track, state, n, (*extras)[n.extras], onPlay, onPlay + (end - double(n.start)),
                               notePass);
        } else {
            startNote(track, state, n, onPlay, onPlay + (end - double(n.start)), notePass);
            state.previousPlayed = true;
        }
    }
}

void Engine::startWithOperators(const TrackPlayback& track, TrackState& state, const NoteEvent& n, const NoteExtras& ex,
                                double onPlay, double endPlay, int64_t pass) {
    operators::PassContext ctx;
    ctx.seed = snapshot->seed;
    ctx.clip = ex.clip;
    ctx.pass = pass;
    ctx.fill = fill;
    ctx.previousPlayed = state.previousPlayed;
    const Note& note = ex.note;
    bool plays = operators::plays(note, ctx);
    state.previousPlayed = plays;
    if (!plays)
        return;
    // The event already sits `early` ticks before the note; delaying by early plus a spread in ±early lands it
    // anywhere within its timing spread, never before the window that fired it.
    double delay = std::max(0.0, ex.early + operators::timingOffset(note, ctx));
    float spread = operators::velocity(note, ctx) - note.velocity;
    double length = endPlay - onPlay;
    int count = operators::hitCount(note, ticksPerBeat(snapshot->timeSig));
    // Hits are laid out over the note as written, then fitted to the event (gate scale, loop cuts).
    double scale = note.length > 0 ? length / double(note.length) : 1;
    for (int i = 0; i < count; i++) {
        operators::Hit h = operators::hit(note, i, count);
        TrackState::PendingHit hit;
        hit.start = onPlay + delay + h.start * scale;
        hit.end = hit.start + (count > 1 ? h.length * scale : length);
        hit.pitch = n.pitch;
        float shape = note.velocity > 0 ? h.velocity / note.velocity : 1;
        hit.velocity = std::clamp(n.velocity * shape + spread, 0.f, 1.f);
        hit.release = n.releaseVelocity;
        hit.extras = &ex;
        hit.origin = double(n.start) + i; // each repeat varies on its own
        hit.pass = pass;
        hit.key = n.key;
        hit.channel = n.channel;
        if (hit.start < playTick)
            startHit(track, state, hit);
        else if (state.pendingCount < kMaxPendingHits)
            state.pending[size_t(state.pendingCount++)] = hit;
    }
}

void Engine::startHit(const TrackPlayback& track, TrackState& state, const TrackState::PendingHit& hit) {
    double off = clockOffset();
    chain::ChainNote n;
    n.start = hit.start + off;
    n.end = hit.end + off;
    n.pitch = hit.pitch;
    n.velocity = hit.velocity;
    n.release = hit.release;
    n.key = hit.key;
    n.channel = hit.channel;
    n.extras = hit.extras;
    n.origin = hit.origin;
    n.pass = hit.pass;
    deliver(track, state, n);
}

void Engine::firePending(const TrackPlayback& track, TrackState& state, double until) {
    for (int i = 0; i < state.pendingCount;) {
        if (state.pending[size_t(i)].start < until) {
            startHit(track, state, state.pending[size_t(i)]);
            state.pending[size_t(i)] = state.pending[size_t(--state.pendingCount)];
        } else {
            i++;
        }
    }
}

void Engine::reattachCurves(const TrackPlayback& tp, TrackState& state) {
    auto find = [](const std::vector<NoteExtras>& list, uint64_t clip, uint64_t note) -> const NoteExtras* {
        for (const NoteExtras& ex : list)
            if (ex.clip == clip && ex.note.id.value == note)
                return &ex;
        return nullptr;
    };
    for (int v = 0; v < state.voices.polyphony(); v++) {
        uint64_t clip = state.voiceClip[size_t(v)], note = state.voiceNote[size_t(v)];
        state.voiceClip[size_t(v)] = state.voiceNote[size_t(v)] = 0;
        if (!note || !state.voices.voice(v).held)
            continue;
        const NoteExtras* found = find(tp.arrangementExtras, clip, note);
        for (size_t s = 0; !found && s < tp.slots.size(); s++)
            if (tp.slots[s])
                found = find(tp.slots[s]->extras, clip, note);
        bool curves = found && (!found->note.pitchExpr.empty() || !found->note.timbreExpr.empty() ||
                                !found->note.pressureExpr.empty());
        if (!curves)
            continue;
        setVoiceCurves(state, v, found);
        state.voiceLength[size_t(v)] = std::max(1.0, double(found->note.length) * found->stretch);
    }
}

void Engine::clearPending(TrackState& state) {
    state.pendingCount = 0;
    state.voiceExtras.fill(nullptr);
    state.voiceClip.fill(0);
    state.voiceNote.fill(0);
    state.curvedVoices = 0;
}

void Engine::startNote(const TrackPlayback& track, TrackState& state, const NoteEvent& e, double onPlay, double endPlay,
                       int64_t pass) {
    double off = clockOffset();
    chain::ChainNote n;
    n.start = onPlay + off;
    n.end = endPlay + off;
    n.pitch = e.pitch;
    n.velocity = e.velocity;
    n.release = e.releaseVelocity;
    n.key = e.key;
    n.channel = e.channel;
    n.origin = double(e.start);
    n.pass = pass;
    deliver(track, state, n);
}

void Engine::deliver(const TrackPlayback& track, TrackState& state, const chain::ChainNote& note) {
    forwardNote(track, note, ListenSource::TrackBefore);
    triggerModulators(state, note);
    if (!chainActive(state)) {
        sound(track, state, note);
        return;
    }
    chain::ChainNote n = note;
    chain::Chain& c = (*chains)[size_t(state.chain)].chain;
    if (n.id == 0)
        n.id = c.newId();
    c.push(n);
}

void Engine::sound(const TrackPlayback& track, TrackState& state, const chain::ChainNote& n) {
    forwardNote(track, n, ListenSource::TrackAfter);
    if (!track.audible)
        return; // it only feeds the tracks that listen to it
    if (track.drum) {
        hitDrum(track, state, n.pitch, n.velocity);
        return;
    }
    int v = state.voices.noteOn(n.pitch, n.velocity, n.release, n.end, n.id);
    clearLiveVoice(state, v);
    if (n.id & chain::kLiveNote) {
        // Held until its key is released, like an audition: clip events leave it alone.
        if (state.audition[size_t(v)] < 0)
            state.auditions++;
        state.audition[size_t(v)] = 0;
    } else if (state.audition[size_t(v)] >= 0) {
        endAudition(state, v); // a clip note took over the voice
    }
    const NoteExtras* extras = static_cast<const NoteExtras*>(n.extras);
    const Note* note = extras ? &extras->note : nullptr;
    bool curves = note && (!note->pitchExpr.empty() || !note->timbreExpr.empty() || !note->pressureExpr.empty());
    setVoiceCurves(state, v, curves ? extras : nullptr);
    state.voiceStart[size_t(v)] = n.start - clockOffset();
    // Curves span the note as written, not the part that sounds: a note cut short by the song loop, the clip's
    // loop or the gate length keeps its curve's timing, as the editor draws it.
    double length = note ? double(note->length) * extras->stretch : n.open() ? double(kPpq) : double(n.end - n.start);
    state.voiceLength[size_t(v)] = std::max(1.0, length);
}

void Engine::endNote(const TrackPlayback& track, TrackState& state, uint32_t id) {
    forwardEnd(track, id, ListenSource::TrackAfter);
    if (track.drum)
        return;
    int v = state.voices.find(id);
    if (v < 0)
        return; // stolen by a later note, or never started
    state.voices.release(v);
    if (state.audition[size_t(v)] >= 0)
        endAudition(state, v);
}

namespace {

/// A forwarded open note's ID on the listening track: marked, and mixed with the source's bus slot, so it can't be
/// taken for one of the listener's own notes. The live bit stays, so a held key keeps its voice until released.
uint32_t forwardedId(const TrackPlayback& from, uint32_t id) {
    return (id & chain::kLiveNote) | 0x40000000u | (uint32_t(from.busSlot & 63) << 24) | (id & 0x00FFFFFFu);
}

} // namespace

bool Engine::hasListeners(const TrackPlayback& from) const {
    for (size_t i = 0; i < trackCount; i++) {
        const TrackPlayback& tp = *snapshot->tracks[i];
        if (listens(tp) && tp.listenTrack == from.id && &tp != &from)
            return true;
    }
    return false;
}

void Engine::forwardNote(const TrackPlayback& from, const chain::ChainNote& note, ListenSource point) {
    if (!snapshot || forwardDepth >= kMaxForwardDepth)
        return;
    for (size_t i = 0; i < trackCount; i++) {
        const TrackPlayback& tp = *snapshot->tracks[i];
        if (tp.listen != point || tp.listenTrack != from.id || &tp == &from || !listens(tp) || !runs(tp))
            continue;
        chain::ChainNote n = note;
        n.id = note.open() ? forwardedId(from, note.id) : 0;
        wakeTrack(i); // it may be asleep between its own notes
        forwardDepth++;
        deliver(tp, tracks[i], n);
        forwardDepth--;
    }
}

void Engine::forwardEnd(const TrackPlayback& from, uint32_t id, ListenSource point) {
    if (!snapshot || forwardDepth >= kMaxForwardDepth)
        return;
    for (size_t i = 0; i < trackCount; i++) {
        const TrackPlayback& tp = *snapshot->tracks[i];
        if (tp.listen != point || tp.listenTrack != from.id || &tp == &from || !listens(tp))
            continue;
        wakeTrack(i);
        forwardDepth++;
        endInput(tp, tracks[i], forwardedId(from, id));
        forwardDepth--;
    }
}

void Engine::endInput(const TrackPlayback& track, TrackState& state, uint32_t id) {
    forwardEnd(track, id, ListenSource::TrackBefore);
    if (state.chain >= 0) {
        ChainSink sink(*this, track, state);
        (*chains)[size_t(state.chain)].chain.end(id, freeTick, sink);
    } else {
        endNote(track, state, id);
    }
}

void Engine::runChain(const TrackPlayback& track, TrackState& state, double until) {
    if (state.chain < 0 || !state.chainInfo || !snapshot)
        return;
    chain::Chain& c = (*chains)[size_t(state.chain)].chain;
    if (c.idle())
        return;
    const std::vector<DevicePlayback>& devices = state.chainInfo->devices;
    std::array<chain::DeviceSlot, kMaxDevices> slots;
    int count = std::min<int>(int(devices.size()), kMaxDevices);
    for (int k = 0; k < count; k++) {
        const DevicePlayback& d = devices[size_t(k)];
        slots[size_t(k)] = {d.type, d.on, d.affectDrums, d.id, &knobValues[size_t(d.knobs)]};
    }
    chain::Context context;
    context.devices = slots.data();
    context.count = count;
    context.drum = track.drum;
    context.songKey = &snapshot->key;
    context.clipKeys = &track.keys;
    context.timeSig = snapshot->timeSig;
    context.seed = snapshot->seed;
    // Chain time is the engine clock; while playing it maps onto the song, so arpeggiators step on its grid.
    context.songOffset = playing ? songTick - freeTick : 0;
    ChainSink sink(*this, track, state);
    c.run(until, context, sink);
}

void Engine::triggerModulators(const TrackState& state, const chain::ChainNote& note) {
    if (!state.chainInfo || state.chainInfo->modulatorSlots.empty())
        return;
    double at = songTickAt(note.start - clockOffset());
    mods::NoteValues values;
    values.velocity = note.velocity;
    values.pitch = float(note.pitch) / 127.f;
    values.release = note.release;
    if (const auto* extras = static_cast<const NoteExtras*>(note.extras)) {
        values.timbre = expressionAt(extras->note.timbreExpr, 0, 0);
        values.pressure = expressionAt(extras->note.pressureExpr, 0, 0);
    }
    mods::Clock clock;
    clock.ticksPerSecond = bpm * kPpq / 60.0;
    for (int slot : state.chainInfo->modulatorSlots)
        mods::trigger((*modStates)[size_t(slot)], at, note.end - note.start, values, clock);
}

void Engine::advanceModulation() {
    if (!snapshot)
        return;
    const Snapshot& s = *snapshot;
    // Every modulation uses the modulators' outputs from the previous block, so modulators turning each other's
    // settings never loop.
    if (knobsDirty || overrideOn || overrideWasOn || knobLanesOn || knobLanesWereOn || !s.modulated.empty()) {
        std::copy(s.knobs.begin(), s.knobs.end(), knobValues.get());
        // Knob lanes set the base; a knob held by the mouse wins while it is held, and modulation adds on top.
        for (size_t i = 0; i < trackCount; i++)
            for (int j = 0; j < tracks[i].knobSetCount; j++) {
                auto [k, v] = tracks[i].knobSets[size_t(j)];
                knobValues[size_t(k)] = v;
            }
        // Knobs whose lanes are quiet (a clip that isn't playing) show their saved value.
        for (int k : s.laneKnobs) {
            liveBases[size_t(k)].store(knobValues[size_t(k)], std::memory_order_relaxed);
            liveKnobs[size_t(k)].store(knobValues[size_t(k)], std::memory_order_relaxed);
        }
        knobLanesWereOn = knobLanesOn;
        if (overrideOn) {
            int k = s.knobIndex(overrideRef);
            if (k >= 0) {
                knobValues[size_t(k)] = std::clamp(overrideValue, 0.f, 1.f);
                liveKnobs[size_t(k)].store(knobValues[size_t(k)], std::memory_order_relaxed);
            }
        }
        for (const KnobModulation& km : s.modulated) {
            float v = knobValues[size_t(km.knob)];
            for (int j = 0; j < km.count; j++)
                v += km.sources[size_t(j)].amount * (*modStates)[size_t(km.sources[size_t(j)].slot)].out;
            v = std::clamp(v, 0.f, 1.f);
            knobValues[size_t(km.knob)] = v;
            liveKnobs[size_t(km.knob)].store(v, std::memory_order_relaxed);
        }
        knobsDirty = false;
        overrideWasOn = overrideOn;
    }
    if (s.modulators.empty())
        return;
    mods::Clock clock;
    clock.songTick = songTick;
    clock.seconds = double(kControlBlock) / sampleRate;
    clock.timeSig = s.timeSig;
    clock.seed = s.seed;
    clock.ticksPerSecond = bpm * kPpq / 60.0;
    for (const ModulatorPlayback& m : s.modulators) {
        mods::State& st = (*modStates)[size_t(m.slot)];
        if (st.owner != m.id) {
            st = mods::State();
            st.owner = m.id;
        }
        float out = mods::evaluate(m.type, m.id, &knobValues[size_t(m.knobs)], st, clock, m.steps.data(), m.stepCount);
        liveModulators[size_t(m.slot)].store(out, std::memory_order_relaxed);
    }
}

void Engine::hitDrum(const TrackPlayback& track, TrackState& state, int pitch, float velocity) {
    int lane = pitch >= 0 && pitch < 128 ? track.laneOfPitch[size_t(pitch)] : -1;
    if (lane < 0 || lane >= int(track.drumLanes.size()) || !track.drumLanes[size_t(lane)].audible)
        return;
    const DrumLanePlayback& l = track.drumLanes[size_t(lane)];
    if (l.choke > 0)
        for (size_t j = 0; j < track.drumLanes.size(); j++)
            if (int(j) != lane && track.drumLanes[j].choke == l.choke)
                state.drums.cut(int(j));
    int samples = l.gateMs > 0 ? int(std::ceil(l.gateMs * 0.001 * sampleRate)) : retriggerGap; // a 1 ms trigger
    state.drums.hit(lane, velocity, samples);
}

void Engine::seekTrack(const TrackPlayback& track, TrackState& state) {
    state.arrangementCursor = arrangementIndex(track, songTick);
    state.cursor = 0;
    const ClipPlayback* playing = track.slot(state.playing);
    if (!playing)
        return;
    const ClipPlayback& clip = *playing;
    double length = double(clip.loopLength());
    double a = playTick - state.launchTick;
    if (!clip.looping && a >= length) {
        state.cursor = clip.notes.size();
        return;
    }
    auto indexOf = [](const std::vector<NoteEvent>& notes, double local) {
        auto it = std::lower_bound(notes.begin(), notes.end(), local,
                                   [](const NoteEvent& n, double t) { return n.start < t; });
        return size_t(it - notes.begin());
    };
    state.cursor = indexOf(clip.notes, clip.loopStart + (clip.looping ? positiveMod(a, length) : a));
    for (size_t k = 0; k < clip.laneLoops.size() && k < state.laneCursor.size(); k++) {
        const LaneLoop& lane = clip.laneLoops[k];
        if (lane.length > 0)
            state.laneCursor[k] = indexOf(lane.notes, double(lane.start) + positiveMod(a, double(lane.length)));
    }
}

void Engine::seekAll() {
    if (!snapshot)
        return;
    for (size_t i = 0; i < trackCount; i++)
        seekTrack(*snapshot->tracks[i], tracks[i]);
}

int Engine::lanesClips(const TrackPlayback& tp, const TrackState& state, double song, double play,
                       LaneClips& out) const {
    if (const ClipPlayback* clip = tp.slot(state.playing)) {
        double t = clipTimeAt(tp, state, play);
        if (!std::isfinite(t))
            return 0;
        out[0] = {clip, t};
        return 1;
    }
    if (!followsArrangement(state) || replacing(tp))
        return 0;
    // Sorted by position: the clips under the playhead come bottom to top, so a later one (a newer take on top)
    // overrides the lanes it shares with the ones below.
    int n = 0;
    for (const InstanceLanes& inst : tp.arrangementLanes) {
        if (double(inst.position) > song || n == int(out.size()))
            break;
        if (double(inst.end) <= song)
            continue;
        double t = inst.clipTime(song);
        if (std::isfinite(t))
            out[size_t(n++)] = {inst.clip.get(), t};
    }
    return n;
}

void Engine::advanceKnobLanes() {
    knobLanesOn = false;
    if (!snapshot)
        return;
    // Lanes are read for where the song will be at the end of this block, so a note on a lane's point already
    // plays with the point's value (notes read the knobs set at the block's start).
    double ahead = ticksPerSample * kControlBlock;
    double song = songTick + ahead;
    if (loopOn && snapshot->loopEnd > snapshot->loopStart && songTick < double(snapshot->loopEnd) &&
        song >= double(snapshot->loopEnd))
        song = double(snapshot->loopStart) + (song - double(snapshot->loopEnd));
    for (size_t i = 0; i < trackCount; i++) {
        const TrackPlayback& tp = *snapshot->tracks[i];
        TrackState& state = tracks[i];
        // Knob lanes turn their knobs only while the song plays; stopped, the knobs are free to turn by hand.
        state.knobSetCount = 0;
        if (!playing || (tp.knobLanes.empty() && tp.arrangementLanes.empty() && !tp.slot(state.playing)))
            continue;
        auto set = [&](const ParamRef& ref, float value, bool modulate) {
            int k = snapshot->knobIndex(ref);
            if (k < 0)
                return;
            for (int j = 0; j < state.knobSetCount; j++)
                if (state.knobSets[size_t(j)].first == k) {
                    float& v = state.knobSets[size_t(j)].second;
                    v = modulate ? std::clamp(v + value - 0.5f, 0.f, 1.f) : value; // around the track lane's value
                    return;
                }
            if (state.knobSetCount < int(state.knobSets.size()))
                state.knobSets[size_t(state.knobSetCount++)] = {k, value};
        };
        // Track lanes run along the song in Launcher mode too, as AUTO lanes do.
        if (mode == PlayMode::Launcher || followsArrangement(state))
            for (size_t k = 0; k < tp.knobLanes.size() && k < size_t(kMaxKnobLanes); k++) {
                const KnobLanePlayback& lane = tp.knobLanes[k];
                set(lane.target, evaluateAt(lane.points, song, state.trackKnobCursor[k], lane.rest), false);
            }
        LaneClips clips;
        int count = lanesClips(tp, state, song, playTick + ahead, clips);
        for (int i = 0; i < count; i++) {
            auto [clip, clipT] = clips[size_t(i)];
            for (size_t k = 0; k < clip->knobLanes.size() && k < size_t(kMaxKnobLanes); k++) {
                const KnobLanePlayback& lane = clip->knobLanes[k];
                set(lane.target, evaluateAt(lane.points, clipT, state.clipKnobCursor[k], lane.rest),
                    lane.combine == CombineMode::Modulate);
            }
        }
        knobLanesOn |= state.knobSetCount > 0;
    }
}

void Engine::advanceAutomation(const TrackPlayback& tp, TrackState& state) {
    // Track lanes run along the song in Launcher mode too; in Arranger mode they stop only while the launcher has
    // taken the track over.
    if (state.autoChannels == 0 && !tp.voiceLanes) {
        for (int o : {kPitchLaneSlot, kPressureLaneSlot})
            if (state.autoVolts[size_t(o)] != 0) {
                state.autoVolts[size_t(o)] = 0;
                state.autoChanges++;
            }
        return;
    }
    bool trackLanes = mode == PlayMode::Launcher || followsArrangement(state);
    LaneClips clips;
    int clipCount = lanesClips(tp, state, songTick, playTick, clips);
    std::array<float, kMaxPlayedLanes> target;
    std::array<bool, kMaxPlayedLanes> have{};
    auto take = [&](const LanePlayback& lane, float value) {
        size_t o = size_t(lane.output);
        have[o] = true;
        target[o] = value;
        state.autoLow[o] = lane.rangeMin;
        state.autoHigh[o] = lane.rangeMax;
        if (lane.slewMs != state.slewMs[o]) {
            state.slewMs[o] = lane.slewMs;
            state.slewAlpha[o] = lane.slewMs > 0
                                     ? float(1 - std::exp(-1000.0 * kControlBlock / (double(lane.slewMs) * sampleRate)))
                                     : 1.f;
        }
    };
    if (trackLanes)
        for (size_t k = 0; k < tp.lanes.size() && k < kMaxPlayedLanes; k++)
            take(tp.lanes[k], evaluateAt(tp.lanes[k].points, songTick, state.trackLaneCursor[k], tp.lanes[k].rest));
    // Modulate moves around the track lane's value, whatever stacked clips below did.
    std::array<float, kMaxPlayedLanes> trackTarget = target;
    std::array<bool, kMaxPlayedLanes> haveTrack = have;
    for (int i = 0; i < clipCount; i++) {
        auto [clipLanes, clipT] = clips[size_t(i)];
        for (size_t k = 0; k < clipLanes->lanes.size() && k < kMaxPlayedLanes; k++) {
            const LanePlayback& lane = clipLanes->lanes[k];
            float v = evaluateAt(lane.points, clipT, state.clipLaneCursor[k], lane.rest);
            size_t o = size_t(lane.output);
            if (haveTrack[o] && lane.combine == CombineMode::Modulate)
                target[o] = std::clamp(trackTarget[o] + v - 0.5f, 0.f, 1.f);
            else
                take(lane, v);
        }
    }
    // Modulators add onto the lanes (docs/DEVICES.md §3 "CV out").
    std::array<float, kMaxAutomationLanes> added{};
    std::array<bool, kMaxAutomationLanes> modulated{};
    if (state.chainInfo)
        for (const AutoModulation& r : state.chainInfo->autoRoutes) {
            size_t o = size_t(r.channel) & (kMaxAutomationLanes - 1);
            added[o] += r.source.amount * (*modStates)[size_t(r.source.slot)].out;
            modulated[o] = true;
        }
    // Channels with nothing playing hold their last value.
    for (size_t o = 0; o < size_t(state.autoChannels); o++) {
        if (!have[o] && !modulated[o])
            continue;
        if (have[o])
            state.autoValue[o] += (target[o] - state.autoValue[o]) * state.slewAlpha[o];
        state.autoOut[o] = std::clamp(state.autoValue[o] + added[o], 0.f, 1.f);
        float volts = state.autoLow[o] + state.autoOut[o] * (state.autoHigh[o] - state.autoLow[o]);
        if (volts != state.autoVolts[o])
            state.autoChanges++;
        state.autoVolts[o] = volts;
    }
    // Pitch bend and pressure lanes rest at 0 V when nothing plays them, so a clip that ends leaves no bend behind.
    for (size_t o : {size_t(kPitchLaneSlot), size_t(kPressureLaneSlot)}) {
        float volts = 0;
        if (have[o]) {
            state.autoValue[o] += (target[o] - state.autoValue[o]) * state.slewAlpha[o];
            volts = state.autoLow[o] + state.autoValue[o] * (state.autoHigh[o] - state.autoLow[o]);
        }
        if (volts != state.autoVolts[o])
            state.autoChanges++;
        state.autoVolts[o] = volts;
    }
}

void Engine::writeAutomation(const TrackPlayback& tp, const TrackState& state, TrackFrame& frame) {
    frame.autoChannels = uint8_t(state.autoChannels);
    for (int o = 0; o < state.autoChannels; o++)
        frame.automation[o] = state.autoVolts[size_t(o)];
    // A control moving on an armed track plays over its lane, so you hear what you play.
    if (!playsLiveValues(tp) || state.liveAutoMask == 0)
        return;
    for (int o = 0; o < kMaxAutomationLanes; o++) {
        if (!(state.liveAutoMask >> o & 1))
            continue;
        for (int k = frame.autoChannels; k < o; k++)
            frame.automation[k] = 0;
        frame.autoChannels = uint8_t(std::max<int>(frame.autoChannels, o + 1));
        frame.automation[o] = state.liveAuto[size_t(o)];
    }
}

void Engine::writeFrame(const TrackPlayback& track, const TrackState& state, TrackFrame& frame) const {
    if (track.drum) {
        writeDrumFrame(track, state, frame);
        return;
    }
    const VoiceAllocator& voices = state.voices;
    frame.trackId = track.id.value;
    frame.channels = uint8_t(voices.polyphony());
    writeAutomation(track, state, frame);
    bool live = playsLiveValues(track);
    for (int v = 0; v < voices.polyphony(); v++) {
        const VoiceAllocator::Voice& voice = voices.voice(v);
        frame.voct[v] = pitchToVolts(voice.pitch);
        frame.gate[v] = voices.gate(v) ? 10.f : 0.f;
        frame.vel[v] = voice.velocity * 10.f;
        frame.rel[v] = voice.releaseVelocity * 10.f;
        frame.timbre[v] = 0;
        frame.press[v] = 0;
        if (const NoteExtras* ex = state.voiceExtras[size_t(v)]) {
            // Per-note curves across the note (EDITOR_UX §4.6); after it they hold their last value.
            float pos =
                float(std::clamp((playTick - state.voiceStart[size_t(v)]) / state.voiceLength[size_t(v)], 0.0, 1.0));
            frame.voct[v] += expressionAt(ex->note.pitchExpr, pos, 0) / 12.f;
            frame.timbre[v] = expressionAt(ex->note.timbreExpr, pos, 0) * 10.f;
            frame.press[v] = expressionAt(ex->note.pressureExpr, pos, 0) * 10.f;
        }
        // Lanes played into the voices: pitch bend adds on; pressure shows where it is above the note's own.
        frame.voct[v] += state.autoVolts[size_t(kPitchLaneSlot)];
        frame.press[v] = std::max(frame.press[v], state.autoVolts[size_t(kPressureLaneSlot)]);
        if (live && (voice.id & chain::kLiveNote)) {
            frame.voct[v] += state.liveBend[size_t(v)] / 12.f;
            frame.timbre[v] = state.liveTimbre[size_t(v)] * 10.f;
            frame.press[v] = std::max(frame.press[v], state.livePress[size_t(v)] * 10.f);
        }
        if (live) {
            frame.voct[v] += state.liveChannelBend;
            frame.press[v] = std::max(frame.press[v], state.liveChannelPress);
        }
    }
}

} // namespace tessera

namespace tessera {

void Engine::writeDrumFrame(const TrackPlayback& track, const TrackState& state, TrackFrame& frame) const {
    // One channel per lane output: a gate (or trigger) with the lane's velocity and its note as pitch.
    frame.trackId = track.id.value;
    frame.channels = uint8_t(std::clamp(track.drumChannels, 1, kMaxVoices));
    writeAutomation(track, state, frame);
    for (int c = 0; c < frame.channels; c++) {
        frame.gate[c] = 0;
        frame.rel[c] = 0;
    }
    // Velocity holds after the trigger; where lanes share an output, the one sounding wins.
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < track.drumLanes.size(); i++) {
            int c = track.drumLanes[i].output;
            bool high = state.drums.gate(int(i));
            if (c >= frame.channels || (pass == 1 && !high))
                continue;
            frame.voct[c] = pitchToVolts(track.drumLanes[i].pitch);
            frame.vel[c] = state.drums.velocity(int(i)) * 10.f;
            if (high)
                frame.gate[c] = 10.f;
        }
    }
}

} // namespace tessera
