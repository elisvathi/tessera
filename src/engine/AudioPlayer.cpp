#include "AudioPlayer.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint64_t kSlotKey = 1ull << 63;

/// The key of a launched clip: which slot, and which clip is in it.
uint64_t slotKey(int slot, uint64_t clip) { return kSlotKey | (uint64_t(slot) << 40) | (clip & ((1ull << 40) - 1)); }

/// x modulo m, always in [0, m).
double positiveMod(double x, double m) {
    double r = std::fmod(x, m);
    return r < 0 ? r + m : r;
}

} // namespace

float fadeShape(float x, float curve, bool crossfade) {
    x = std::clamp(x, 0.f, 1.f);
    // A positive curve rises fast and then levels off; a negative one starts slowly.
    float y = curve == 0 ? x : std::pow(x, std::exp2(-2 * std::clamp(curve, -1.f, 1.f)));
    return crossfade ? float(std::sin(double(y) * kPi / 2)) : y;
}

void AudioPlayer::hold(uint64_t track, float volumeGain, float pan, uint8_t parts) {
    for (Hold& h : holds) {
        uint64_t id = h.track.load(std::memory_order_relaxed);
        if (id == track || id == 0) {
            h.volume.store(volumeGain, std::memory_order_relaxed);
            h.pan.store(pan, std::memory_order_relaxed);
            h.parts.store(parts, std::memory_order_relaxed);
            h.track.store(track, std::memory_order_release);
            return;
        }
    }
}

void AudioPlayer::holdClips(uint64_t track, float gain) {
    for (Hold& h : holds) {
        uint64_t id = h.track.load(std::memory_order_relaxed);
        if (id == track || id == 0) {
            h.clips.store(gain, std::memory_order_relaxed);
            h.parts.store(id == track ? uint8_t(h.parts.load(std::memory_order_relaxed) | kHoldClips) : kHoldClips,
                          std::memory_order_relaxed);
            h.track.store(track, std::memory_order_release);
            return;
        }
    }
}

void AudioPlayer::holdSend(uint64_t track, uint64_t to, float gain) {
    for (Hold& h : holds) {
        uint64_t id = h.track.load(std::memory_order_relaxed);
        if (id == track || id == 0) {
            h.send.store(gain, std::memory_order_relaxed);
            h.sendTo.store(to, std::memory_order_relaxed);
            h.parts.store(kHoldSend, std::memory_order_relaxed);
            h.track.store(track, std::memory_order_release);
            return;
        }
    }
}

void AudioPlayer::armTap(int n, const TapSpec& spec) {
    Tap& t = taps[size_t(n)];
    t.state.store(int(TapState::Idle), std::memory_order_release);
    t.track.store(spec.track.value, std::memory_order_relaxed);
    t.kind.store(int(spec.kind), std::memory_order_relaxed);
    t.from.store(spec.from, std::memory_order_relaxed);
    t.to.store(spec.to, std::memory_order_relaxed);
    t.whileRecording.store(spec.whileRecording, std::memory_order_relaxed);
    t.startWithin.store(spec.startWithin, std::memory_order_relaxed);
    t.firstTick.store(-1, std::memory_order_relaxed);
    t.ring.store(spec.ring, std::memory_order_relaxed);
    t.state.store(int(TapState::Armed), std::memory_order_release);
    anyTap.store(true, std::memory_order_release);
}

void AudioPlayer::disarmTap(int n) {
    taps[size_t(n)].state.store(int(TapState::Idle), std::memory_order_release);
    taps[size_t(n)].ring.store(nullptr, std::memory_order_release);
}

void AudioPlayer::runTaps(const Engine& engine, double song, float masterL, float masterR) {
    bool playing = engine.isPlaying();
    bool recording = engine.recording() && !engine.countingIn();
    bool jumpedBack = song < tapLastSong - 1e-6;
    tapLastSong = song;
    bool any = false;
    for (Tap& t : taps) {
        int state = t.state.load(std::memory_order_acquire);
        if (state != int(TapState::Armed) && state != int(TapState::Running))
            continue;
        any = true;
        bool rec = t.whileRecording.load(std::memory_order_relaxed);
        double from = t.from.load(std::memory_order_relaxed), to = t.to.load(std::memory_order_relaxed);
        if (state == int(TapState::Armed)) {
            double within = t.startWithin.load(std::memory_order_relaxed);
            if (!playing || (rec && !recording) || (from >= 0 && song < from) ||
                (from >= 0 && within >= 0 && song > from + within))
                continue;
            t.firstTick.store(song, std::memory_order_relaxed);
            t.state.store(int(TapState::Running), std::memory_order_release);
        } else if (!playing || (rec && !recording) || (to >= 0 && song >= to) || jumpedBack) {
            t.state.store(int(TapState::Done), std::memory_order_release);
            continue;
        }
        float l = 0, r = 0;
        auto kind = TapKind(t.kind.load(std::memory_order_relaxed));
        if (kind == TapKind::Master) {
            l = masterL;
            r = masterR;
        } else if (t.run >= 0) {
            const TrackRun& run = runs[size_t(t.run)];
            l = kind == TapKind::In ? run.inL : kind == TapKind::Sound ? run.soundL : run.stemL;
            r = kind == TapKind::In ? run.inR : kind == TapKind::Sound ? run.soundR : run.stemR;
        }
        t.peak = std::max(t.peak, std::max(std::abs(l), std::abs(r)));
        if (FrameRing* ring = t.ring.load(std::memory_order_acquire))
            ring->push(l, r);
    }
    if (!any)
        anyTap.store(false, std::memory_order_relaxed);
}

bool AudioPlayer::heldSend(uint64_t track, uint64_t to, float& gain) const {
    for (const Hold& h : holds)
        if (h.track.load(std::memory_order_acquire) == track && (h.parts.load(std::memory_order_relaxed) & kHoldSend) &&
            h.sendTo.load(std::memory_order_relaxed) == to) {
            gain = h.send.load(std::memory_order_relaxed);
            return true;
        }
    return false;
}

bool AudioPlayer::held(uint64_t track, bool pan, float& value) const {
    for (const Hold& h : holds)
        if (h.track.load(std::memory_order_acquire) == track) {
            if (!(h.parts.load(std::memory_order_relaxed) & (pan ? kHoldPan : kHoldVolume)))
                return false;
            value = (pan ? h.pan : h.volume).load(std::memory_order_relaxed);
            return true;
        }
    return false;
}

void AudioPlayer::releaseHolds() {
    for (Hold& h : holds)
        h.track.store(0, std::memory_order_release);
}

void AudioPlayer::setSnapshot(const AudioSnapshot* next) {
    if (next == snapshot)
        return;
    // Each track keeps its voices and its ramping gains, found again by ID: tracks may have moved.
    static_assert(kMaxTracks * 2 <= 256, "ghost runs fit");
    size_t oldCount = snapshot ? snapshot->tracks.size() + ghosts : ghosts;
    std::array<bool, kMaxTracks> taken{};
    size_t newCount = next ? std::min<size_t>(next->tracks.size(), kMaxTracks) : 0;
    for (size_t j = 0; j < newCount; j++) {
        spare[j] = TrackRun{};
        TrackId id = next->tracks[j].id;
        for (size_t k = 0; k < std::min<size_t>(oldCount, kMaxTracks); k++)
            if (!taken[k] && runIds[k] == id) {
                spare[j] = runs[k];
                taken[k] = true;
                break;
            }
        spare[j].cursor = 0;
        runIds[j] = id;
    }
    // Tracks that went away keep fading out where nothing else needs the room.
    size_t ghostCount = 0;
    for (size_t k = 0; k < std::min<size_t>(oldCount, kMaxTracks); k++) {
        if (taken[k] || newCount + ghostCount >= size_t(kMaxTracks))
            continue;
        bool anyVoice = false;
        for (const Voice& v : runs[k].voices)
            anyVoice |= v.on;
        if (!anyVoice)
            continue;
        spare[newCount + ghostCount] = runs[k];
        runIds[newCount + ghostCount] = TrackId();
        ghostCount++;
    }
    for (size_t j = 0; j < newCount + ghostCount; j++) {
        TrackRun& run = spare[j];
        for (Voice& v : run.voices) {
            if (!v.on)
                continue;
            // A buffer the new snapshot doesn't hold may be freed at any moment.
            if (!next || !next->holds(v.buffer)) {
                v = Voice{};
                continue;
            }
            int found = -1;
            for (size_t c = 0; c < next->clips.size(); c++)
                if (next->clips[c].clip == v.clipId) {
                    found = int(c);
                    break;
                }
            v.clip = found;
            if (found < 0 || j >= newCount)
                retire(v, float(kSeamFadeSeconds * sampleRate));
        }
        if (j >= newCount) {
            run.stepL = -run.gainL / kBlock;
            run.stepR = -run.gainR / kBlock;
        }
    }
    std::swap(runs, spare);
    ghosts = ghostCount;
    snapshot = next;
}

void AudioPlayer::retire(Voice& v, float samples) {
    if (!v.on || v.leaving)
        return;
    v.leaving = true;
    v.advanced = false;
    v.key = 0;
    v.fadeStep = -v.fade / std::max(1.f, samples);
}

void AudioPlayer::startBlock(const Engine& engine) {
    bool anyHold = false;
    for (const Hold& h : holds)
        anyHold |= h.track.load(std::memory_order_relaxed) != 0;
    auto held = [&](uint64_t id, float& volume, float& pan, float& clips) {
        if (!anyHold)
            return;
        for (const Hold& h : holds)
            if (h.track.load(std::memory_order_acquire) == id) {
                uint8_t parts = h.parts.load(std::memory_order_relaxed);
                if (parts & kHoldVolume)
                    volume = h.volume.load(std::memory_order_relaxed);
                if (parts & kHoldPan)
                    pan = h.pan.load(std::memory_order_relaxed);
                if (parts & kHoldClips)
                    clips = h.clips.load(std::memory_order_relaxed);
                return;
            }
    };
    size_t count = snapshot ? std::min<size_t>(snapshot->tracks.size(), kMaxTracks) : 0;
    for (size_t i = 0; i < count; i++) {
        const AudioTrackPlay& t = snapshot->tracks[i];
        float volume = t.volume, pan = t.pan;
        // A fader or pan lane playing now moves the strip; a control held in a drag wins over it (§7.3).
        TrackRun& found = runs[i];
        size_t engineTracks = engine.trackCountNow();
        if (found.engineIndex < 0 || size_t(found.engineIndex) >= engineTracks ||
            engine.trackAt(size_t(found.engineIndex)) != t.id) {
            found.engineIndex = -1;
            for (size_t k = 0; k < engineTracks; k++)
                if (engine.trackAt(k) == t.id) {
                    found.engineIndex = int(k);
                    break;
                }
        }
        uint8_t automated = 0;
        if (found.engineIndex >= 0) {
            float value;
            if (engine.mixLane(size_t(found.engineIndex), false, value)) {
                volume = dbToGain(faderDbOfPos(value));
                automated |= 1;
                autoVolume[i].store(faderDbOfPos(value), std::memory_order_relaxed);
            }
            if (engine.mixLane(size_t(found.engineIndex), true, value)) {
                pan = std::clamp(value * 2 - 1, -1.f, 1.f);
                automated |= 2;
                autoPan[i].store(pan, std::memory_order_relaxed);
            }
        }
        autoFlags[i].store(automated, std::memory_order_relaxed);
        float clips = t.clipsGain;
        held(t.id.value, volume, pan, clips);
        float l = 0, r = 0;
        balanceGains(pan, l, r);
        l *= volume;
        r *= volume;
        TrackRun& run = runs[i];
        float own = t.own ? 1.f : 0.f;
        float heard = t.audible ? 1.f : 0.f;
        float heardOwn = t.audible && t.own ? 1.f : 0.f;
        // A drag on one send holds it where the hand is.
        uint64_t heldTo = 0;
        float heldSend = 0;
        if (anyHold)
            for (const Hold& h : holds)
                if (h.track.load(std::memory_order_acquire) == t.id.value &&
                    (h.parts.load(std::memory_order_relaxed) & kHoldSend)) {
                    heldTo = h.sendTo.load(std::memory_order_relaxed);
                    heldSend = h.send.load(std::memory_order_relaxed);
                }
        std::array<float, kMaxSendsPerTrack> sendTarget{};
        size_t sendCount = std::min<size_t>(t.sends.size(), kMaxSendsPerTrack);
        // A Send lane runs along the song and moves its send; a drag on the send wins (§7.3).
        uint32_t sendAuto = 0;
        for (size_t k = 0; k < sendCount; k++) {
            float g = t.sends[k].gain;
            if (!t.sends[k].lane.empty()) {
                float db = faderDbOfPos(evaluateAt(t.sends[k].lane, engine.position(), run.sendCursor[k], 0));
                g = dbToGain(db);
                sendAutoDb[i][k].store(db, std::memory_order_relaxed);
                sendAuto |= 1u << k;
            }
            sendTarget[k] = !t.sending ? 0.f : t.sends[k].toId == heldTo ? heldSend : g;
        }
        sendAutoFlags[i].store(sendAuto, std::memory_order_relaxed);
        if (run.fresh) {
            run.gainL = l;
            run.gainR = r;
            run.outGain = own;
            run.heardGain = heard;
            run.heardOwnGain = heardOwn;
            run.clipsGain = clips;
            run.sendGain = sendTarget;
            run.fresh = false;
        }
        run.stepL = (l - run.gainL) / kBlock;
        run.stepR = (r - run.gainR) / kBlock;
        run.outStep = (own - run.outGain) / kBlock;
        run.heardStep = (heard - run.heardGain) / kBlock;
        run.heardOwnStep = (heardOwn - run.heardOwnGain) / kBlock;
        run.clipsStep = (clips - run.clipsGain) / kBlock;
        for (size_t k = 0; k < size_t(kMaxSendsPerTrack); k++)
            run.sendStep[k] = (sendTarget[k] - run.sendGain[k]) / kBlock;
    }
    // Each tap finds the run it reads; its level is the last block's peak.
    if (anyTap.load(std::memory_order_acquire))
        for (Tap& t : taps) {
            t.level.store(t.peak, std::memory_order_relaxed);
            t.peak = 0;
            t.run = -1;
            uint64_t id = t.track.load(std::memory_order_relaxed);
            for (size_t i = 0; i < count; i++)
                if (snapshot->tracks[i].id.value == id)
                    t.run = int(i);
        }
    // Tracks deleted while they sounded fade out at the gains they had: nothing ramps them further.
    for (size_t i = count; i < count + ghosts && i < size_t(kMaxTracks); i++) {
        TrackRun& run = runs[i];
        run.stepL = run.stepR = run.outStep = run.heardStep = run.heardOwnStep = run.clipsStep = 0;
        run.sendStep = {};
    }
    // Group strips ramp like tracks; a drag on one holds it (by the group's ID).
    size_t groupCount = snapshot ? std::min<size_t>(snapshot->groups.size(), kMaxTracks) : 0;
    for (size_t g = 0; g < groupCount; g++) {
        const GroupPlay& gp = snapshot->groups[g];
        GroupRun& run = groupRuns[g];
        if (run.id != gp.id) {
            run = GroupRun{};
            run.id = gp.id;
        }
        float volume = gp.volume, pan = gp.pan, unused = 1;
        held(gp.id, volume, pan, unused);
        float l = 0, r = 0;
        if (gp.audible) {
            balanceGains(pan, l, r);
            l *= volume;
            r *= volume;
        }
        if (run.fresh) {
            run.gainL = l;
            run.gainR = r;
            run.fresh = false;
        }
        run.stepL = (l - run.gainL) / kBlock;
        run.stepR = (r - run.gainR) / kBlock;
    }
    float master = snapshot ? snapshot->masterVolume : 0;
    float masterPan = 0;
    // The master's Volume lane runs along the song, like a track's (§7.3); a drag of its fader wins.
    bool masterLane = snapshot && !snapshot->masterLane.empty();
    if (masterLane) {
        float db = faderDbOfPos(evaluateAt(snapshot->masterLane, engine.position(), masterLaneCursor, 0));
        master = dbToGain(db);
        masterAutoDb.store(db, std::memory_order_relaxed);
    }
    masterAuto.store(masterLane, std::memory_order_relaxed);
    float mix = snapshot ? snapshot->masterMixGain : 1.f;
    if (snapshot)
        held(snapshot->masterId, master, masterPan, mix);
    if (masterFresh && snapshot) {
        masterGain = master;
        mixGain = mix;
        masterFresh = false;
    }
    masterStep = (master - masterGain) / kBlock;
    mixStep = (mix - mixGain) / kBlock;
}

void AudioPlayer::process(const Engine& engine, float& left, float& right) { process(engine, left, right, Rack()); }

void AudioPlayer::process(const Engine& engine, float& left, float& right, const Rack& rack) {
    left = right = 0;
    if (blockPos == 0)
        startBlock(engine);

    bool playing = engine.isPlaying();
    double song = engine.position(), play = engine.playPosition();
    double dPlay = play - lastPlay, dSong = song - lastSong;
    tempo = engine.tempo() > 0 ? engine.tempo() : 120;
    double nominal = tempo * double(kPpq) / (60.0 * sampleRate);
    jumped = playing && (!wasPlaying || std::abs(dSong - dPlay) > 1e-6);
    ticksPerSample = playing && wasPlaying && dPlay > 0 && dPlay < 64 * nominal ? dPlay : nominal;

    size_t count = snapshot ? std::min<size_t>(snapshot->tracks.size(), kMaxTracks) : 0;
    // Inside a block that started steady, a track whose clips play on unchanged needs no new positions: its voices
    // simply step on. Anything unexpected (a jump, a stop, a tempo change, a launch) drops it back to the full work.
    if (blockPos == 0) {
        blockTempo = tempo;
        blockSteady = playing && !jumped;
    }
    bool steady = blockPos != 0 && blockSteady && playing && !jumped && tempo == blockTempo;
    if (blockPos != 0 && !steady)
        blockSteady = false;
    bool any = false;
    float masterL = 0, masterR = 0;
    for (size_t i = 0; i < count + ghosts; i++) {
        TrackRun& run = runs[i];
        bool quick = steady && run.steady &&
                     (run.slot < 0 || (run.engineIndex >= 0 && engine.playingSlot(size_t(run.engineIndex)) == run.slot));
        if (quick) {
            for (Voice& v : run.voices)
                if (v.on)
                    advance(v);
        } else {
            run.steady = false;
            std::array<Wanted, 2> want;
            int n = 0;
            if (i < count && playing) {
                n = wanted(engine, snapshot->tracks[i], run, want, song, engineElapsed(engine, run), false);
                if (blockPos == 0)
                    run.steady = steadyAhead(engine, snapshot->tracks[i], run, want, n, song);
            }
            follow(run, want, n);
        }

        float sl = 0, sr = 0;
        for (Voice& v : run.voices) {
            if (!v.on)
                continue;
            any = true;
            // Leaving voices keep their own pace; the others were placed for this sample by follow().
            if (!quick && v.leaving && !v.advanced)
                advance(v);
            v.advanced = false;
            float g = v.gain * v.fade;
            const AudioBuffer& b = *v.buffer;
            float xl, xr;
            if (v.exact) {
                bool inside = v.frame >= 0 && v.frame < b.frames;
                xl = inside ? b.channel(0)[v.frame] : 0.f;
                xr = inside ? b.channel(1)[v.frame] : 0.f;
            } else {
                xl = sinc.read(b.channel(0), b.frames, v.pos);
                xr = b.channels > 1 ? sinc.read(b.channel(1), b.frames, v.pos) : xl;
            }
            sl += xl * g;
            sr += xr * g;
            if (!quick && v.fadeStep != 0) {
                v.fade = std::clamp(v.fade + v.fadeStep, 0.f, 1.f);
                if (v.fade >= 1)
                    v.fadeStep = 0;
                else if (v.leaving && v.fade <= 0)
                    v = Voice{};
            }
        }
        if (i < count) {
            const AudioTrackPlay& tp = snapshot->tracks[i];
            run.clipsGain += run.clipsStep;
            sl *= run.clipsGain;
            sr *= run.clipsGain;
            // The track's own sources join its clips; OUT carries them before the fader; what comes back on IN joins
            // after OUT, so an effect loop never feeds back (§2, §8.4).
            float srcL = 0, srcR = 0;
            if (rack.returns)
                for (int slot : tp.sources) {
                    float rl, rr;
                    if (rack.returns->read(rack.frame, slot, tp.id, rl, rr)) {
                        srcL += rl;
                        srcR += rr;
                    }
                }
            sl += srcL;
            sr += srcR;
            // A muted (or soloed-away) track's own sound stops, in OUT too; what other tracks send to it still
            // goes out on OUT, so an effect's tail is ready on unmute (§4.5).
            run.outGain += run.outStep;
            run.heardGain += run.heardStep;
            run.heardOwnGain += run.heardOwnStep;
            float ownL = sl, ownR = sr;
            sl *= run.outGain;
            sr *= run.outGain;
            float outL = sl + sendBus[i][0], outR = sr + sendBus[i][1];
            sendBus[i] = {0.f, 0.f};
            run.outPeak = std::max(run.outPeak, std::max(std::abs(outL), std::abs(outR)));
            if (rack.out && tp.busSlot >= 0 && tp.busSlot < kMaxTracks)
                rack.out[tp.busSlot] = {tp.id.value, outL, outR};
            float retL = 0, retR = 0;
            if (rack.returns)
                for (int slot : tp.returns) {
                    float rl, rr;
                    if (rack.returns->read(rack.frame, slot, tp.id, rl, rr)) {
                        retL += rl;
                        retR += rr;
                    }
                }
            if (tp.hearRollIn) {
                retL += rack.rollInL;
                retR += rack.rollInR;
            }
            sl += retL;
            sr += retR;
            run.inL = tp.rollIn ? rack.rollInL : srcL + retL;
            run.inR = tp.rollIn ? rack.rollInR : srcR + retR;
            run.soundL = sl;
            run.soundR = sr;
            run.gainL += run.stepL;
            run.gainR += run.stepR;
            float postL = sl * run.gainL, postR = sr * run.gainR;
            // Sends go to tracks later in the order, so they reach those tracks' OUT this same sample.
            size_t sendCount = std::min<size_t>(tp.sends.size(), kMaxSendsPerTrack);
            for (size_t k = 0; k < sendCount; k++) {
                const SendPlay& s = tp.sends[k];
                run.sendGain[k] += run.sendStep[k];
                if (s.to < 0 || s.to >= int(kMaxTracks))
                    continue;
                float g = run.sendGain[k];
                float xl = (s.preFader ? sl : postL) * g, xr = (s.preFader ? sr : postR) * g;
                sendBus[size_t(s.to)][0] += xl;
                sendBus[size_t(s.to)][1] += xr;
                run.sendPeak[k] = std::max(run.sendPeak[k], std::max(std::abs(xl), std::abs(xr)));
            }
            sl = (ownL * run.heardOwnGain + retL * run.heardGain) * run.gainL;
            sr = (ownR * run.heardOwnGain + retR * run.heardGain) * run.gainR;
        } else {
            run.gainL += run.stepL;
            run.gainR += run.stepR;
            sl *= run.gainL * run.heardOwnGain;
            sr *= run.gainR * run.heardOwnGain;
        }
        // Delay compensation: this track waits for the latest one (by its bus slot, which never moves).
        if (i < count) {
            const AudioTrackPlay& tp = snapshot->tracks[i];
            float* mem = delays.load(std::memory_order_acquire);
            if (mem && tp.busSlot >= 0 && tp.busSlot < kMaxTracks) {
                float* line = mem + size_t(tp.busSlot) * 2 * kMaxDelay;
                line[delayPos * 2] = sl;
                line[delayPos * 2 + 1] = sr;
                if (tp.delay > 0) {
                    size_t r = (delayPos + size_t(kMaxDelay) - size_t(tp.delay)) % size_t(kMaxDelay);
                    sl = line[r * 2];
                    sr = line[r * 2 + 1];
                }
            }
        }
        run.stemL = sl;
        run.stemR = sr;
        run.peakL = std::max(run.peakL, std::abs(sl));
        run.peakR = std::max(run.peakR, std::abs(sr));
        int group = i < count ? snapshot->tracks[i].group : -1;
        if (group >= 0 && group < int(kMaxTracks)) {
            groupRuns[size_t(group)].busL += sl;
            groupRuns[size_t(group)].busR += sr;
        } else {
            masterL += sl;
            masterR += sr;
        }
    }
    // Each group strip: its tracks' sum through its fader and pan, into the master.
    size_t groupCount = snapshot ? std::min<size_t>(snapshot->groups.size(), kMaxTracks) : 0;
    for (size_t g = 0; g < groupCount; g++) {
        GroupRun& gr = groupRuns[g];
        gr.gainL += gr.stepL;
        gr.gainR += gr.stepR;
        float gl = gr.busL * gr.gainL, grr = gr.busR * gr.gainR;
        gr.busL = gr.busR = 0;
        gr.peakL = std::max(gr.peakL, std::abs(gl));
        gr.peakR = std::max(gr.peakR, std::abs(grr));
        masterL += gl;
        masterR += grr;
    }
    delayPos = (delayPos + 1) % size_t(kMaxDelay);
    mixGain += mixStep;
    masterL *= mixGain;
    masterR *= mixGain;
    if (snapshot) {
        TrackId master(snapshot->masterId);
        if (rack.out)
            rack.out[kMaxTracks] = {master.value, masterL, masterR};
        if (rack.returns)
            for (int slot : snapshot->masterReturns) {
                float rl, rr;
                if (rack.returns->read(rack.frame, slot, master, rl, rr)) {
                    masterL += rl;
                    masterR += rr;
                }
            }
    }
    masterGain += masterStep;
    left = masterL * masterGain;
    right = masterR * masterGain;
    if (anyTap.load(std::memory_order_relaxed))
        runTaps(engine, song, left, right);
    if (measuring.load(std::memory_order_acquire)) {
        uint64_t id = measureTrack.load(std::memory_order_relaxed);
        bool arrived = false;
        for (size_t i = 0; i < count; i++)
            if (snapshot->tracks[i].id.value == id)
                arrived = std::max(std::abs(runs[i].inL), std::abs(runs[i].inR)) > 1.f;
        if (arrived || ++measureCount > int64_t(sampleRate)) {
            measureResult.store(arrived ? measureCount : -1, std::memory_order_release);
            measuring.store(false, std::memory_order_relaxed);
        }
    }
    masterPeakL = std::max(masterPeakL, std::abs(left));
    masterPeakR = std::max(masterPeakR, std::abs(right));
    sounding = any;

    lastSong = song;
    lastPlay = play;
    wasPlaying = playing;
    if (++blockPos == kBlock) {
        blockPos = 0;
        // Meters fall 20 dB a second.
        float fall = float(std::pow(10.0, -20.0 / 20.0 * kBlock / sampleRate));
        for (size_t i = 0; i < count; i++) {
            TrackRun& run = runs[i];
            std::array<std::atomic<float>, 2>& m = meters[i];
            m[0].store(std::max(run.peakL, m[0].load(std::memory_order_relaxed) * fall), std::memory_order_relaxed);
            m[1].store(std::max(run.peakR, m[1].load(std::memory_order_relaxed) * fall), std::memory_order_relaxed);
            outMeters[i].store(std::max(run.outPeak, outMeters[i].load(std::memory_order_relaxed) * fall),
                               std::memory_order_relaxed);
            for (size_t k = 0; k < size_t(kMaxSendsPerTrack); k++) {
                sendMeters[i][k].store(std::max(run.sendPeak[k], sendMeters[i][k].load(std::memory_order_relaxed) * fall),
                                       std::memory_order_relaxed);
                run.sendPeak[k] = 0;
            }
            run.peakL = run.peakR = 0;
            run.outPeak = 0;
        }
        for (size_t g = 0; g < groupCount; g++) {
            GroupRun& gr = groupRuns[g];
            groupMeters[g][0].store(std::max(gr.peakL, groupMeters[g][0].load(std::memory_order_relaxed) * fall),
                                    std::memory_order_relaxed);
            groupMeters[g][1].store(std::max(gr.peakR, groupMeters[g][1].load(std::memory_order_relaxed) * fall),
                                    std::memory_order_relaxed);
            gr.peakL = gr.peakR = 0;
        }
        masterMeter[0].store(std::max(masterPeakL, masterMeter[0].load(std::memory_order_relaxed) * fall),
                             std::memory_order_relaxed);
        masterMeter[1].store(std::max(masterPeakR, masterMeter[1].load(std::memory_order_relaxed) * fall),
                             std::memory_order_relaxed);
        float peak = std::max(masterPeakL, masterPeakR);
        if (peak > clipPeak.load(std::memory_order_relaxed))
            clipPeak.store(peak, std::memory_order_relaxed);
        masterPeakL = masterPeakR = 0;
        // Tracks deleted while they sounded go once they are silent.
        while (ghosts > 0) {
            bool on = false;
            for (const Voice& v : runs[count + ghosts - 1].voices)
                on |= v.on;
            if (on)
                break;
            runs[count + ghosts - 1] = TrackRun{};
            ghosts--;
        }
    }
}

float AudioPlayer::envelopeOf(const AudioInstancePlay& a, double songTick, double ticksPerClip, float seam) const {
    double start = a.position, end = a.position + a.length * ticksPerClip;
    double in = songTick - start, out = end - songTick;
    double secondsPerSongTick = 60.0 / (tempo * double(kPpq));
    float g = 1;
    double inLen = a.overlapIn > 0 ? a.overlapIn : double(a.fadeIn.length) * ticksPerClip;
    double outLen = a.overlapOut > 0 ? a.overlapOut : double(a.fadeOut.length) * ticksPerClip;
    if (inLen > 0 && in < inLen)
        g *= fadeShape(float(in / inLen), a.fadeIn.curve, a.overlapIn > 0);
    if (outLen > 0 && out < outLen)
        g *= fadeShape(float(out / outLen), a.fadeOut.curve, a.overlapOut > 0);
    // The silent safety fades, so no edge ever clicks. Where an edge and a loop seam's dip fall together, the
    // lower of them counts, not both: two 2 ms fades on top of each other would be twice as steep.
    double safety = std::min({1.0, in * secondsPerSongTick / kEdgeFadeSeconds, out * secondsPerSongTick / kEdgeFadeSeconds,
                              double(seam)});
    return g * float(safety);
}

float AudioPlayer::seamEnvelope(const AudioClipPlay& c, double u, double ticksPerClip, bool wrapped) const {
    if (!c.looping || !c.seamDip)
        return 1;
    double secondsPerClipTick = 60.0 / (tempo * double(kPpq)) * ticksPerClip;
    float g = float(std::min(1.0, (c.loopEnd - u) * secondsPerClipTick / kEdgeFadeSeconds));
    if (wrapped)
        g *= float(std::min(1.0, (u - c.loopStart) * secondsPerClipTick / kEdgeFadeSeconds));
    return g;
}

double AudioPlayer::engineElapsed(const Engine& engine, const TrackRun& run) const {
    return run.engineIndex >= 0 && size_t(run.engineIndex) < engine.trackCountNow()
               ? engine.clipElapsed(size_t(run.engineIndex))
               : NAN;
}

bool AudioPlayer::steadyAhead(const Engine& engine, const AudioTrackPlay& track, TrackRun& run,
                              const std::array<Wanted, 2>& now, int n, double song) {
    if (n == 0)
        return false;
    int on = 0;
    for (const Voice& v : run.voices) {
        if (!v.on)
            continue;
        if (v.leaving || v.fade < 1 || v.fadeStep != 0)
            return false;
        on++;
    }
    if (on != n)
        return false;
    // What plays at the block's last sample, if the transport runs on as it does: the same clips, at full gain,
    // exactly as far on as the steps between. Fades and seams sit next to an edge or a wrap, so a block with the same
    // keys at full gain at both ends and no wrap has none inside.
    double ahead = double(kBlock - 1);
    std::array<Wanted, 2> later;
    double elapsed = engineElapsed(engine, run);
    int m = wanted(engine, track, run, later, song + ahead * ticksPerSample,
                   std::isfinite(elapsed) ? elapsed + ahead * ticksPerSample : elapsed, true);
    if (m != n)
        return false;
    for (int k = 0; k < n; k++) {
        const Wanted& a = now[size_t(k)];
        const Wanted& b = later[size_t(k)];
        if (a.key != b.key || a.envelope < 1 || b.envelope < 1 || std::abs(b.tick - (a.tick + ahead * a.tickRate)) > 1e-3)
            return false;
    }
    bool launcher = !(engine.playMode() == PlayMode::Arranger && !engine.overridden(size_t(run.engineIndex)));
    run.slot = launcher ? engine.playingSlot(size_t(run.engineIndex)) : -1;
    // Each voice's step is already set for this tempo; exact ones step one frame.
    return true;
}

int AudioPlayer::wanted(const Engine& engine, const AudioTrackPlay& track, TrackRun& run,
                        std::array<Wanted, 2>& out, double t, double elapsed, bool peek) {
    // The engine's index for the track, found again whenever the engine's track list changed.
    size_t engineTracks = engine.trackCountNow();
    if (run.engineIndex < 0 || size_t(run.engineIndex) >= engineTracks ||
        engine.trackAt(size_t(run.engineIndex)) != track.id) {
        run.engineIndex = -1;
        for (size_t i = 0; i < engineTracks; i++)
            if (engine.trackAt(i) == track.id) {
                run.engineIndex = int(i);
                break;
            }
        if (run.engineIndex < 0)
            return 0;
    }
    size_t index = size_t(run.engineIndex);
    const std::vector<AudioClipPlay>& clips = snapshot->clips;
    int n = 0;

    if (engine.playMode() == PlayMode::Arranger && !engine.overridden(index)) {
        const std::vector<AudioInstancePlay>& list = track.instances;
        auto endOf = [&](const AudioInstancePlay& a) {
            return a.position + a.length * clips[size_t(a.clip)].songTicksPerTick(tempo);
        };
        size_t cursor = run.cursor;
        if ((jumped && !peek) || cursor > list.size())
            cursor = 0;
        while (cursor < list.size() && endOf(list[cursor]) <= t)
            cursor++;
        if (!peek)
            run.cursor = cursor;
        for (size_t k = cursor; k < list.size() && list[k].position <= t && n < 2; k++) {
            const AudioInstancePlay& a = list[k];
            const AudioClipPlay& c = clips[size_t(a.clip)];
            double s = c.songTicksPerTick(tempo);
            if (t >= endOf(a))
                continue;
            double u = a.offset + (t - a.position) / s;
            bool wrapped = false;
            if (c.looping && c.loopEnd > c.loopStart) {
                if (u >= c.loopEnd) {
                    u = c.loopStart + positiveMod(u - c.loopStart, c.loopEnd - c.loopStart);
                    wrapped = true;
                }
            } else if (u >= c.length) {
                continue;
            }
            float env = envelopeOf(a, t, s, seamEnvelope(c, u, s, wrapped));
            out[size_t(n++)] = {a.id, a.clip, u, ticksPerSample / s, env};
        }
        return n;
    }

    int slot = engine.playingSlot(index);
    if (slot < 0 || slot >= int(track.slots.size()) || track.slots[size_t(slot)] < 0)
        return 0;
    int ci = track.slots[size_t(slot)];
    const AudioClipPlay& c = clips[size_t(ci)];
    double s = c.songTicksPerTick(tempo);
    if (!std::isfinite(elapsed))
        return 0;
    double u = elapsed / s;
    double span = c.looping ? c.loopEnd - c.loopStart : c.length - c.loopStart;
    if (span <= 0)
        return 0;
    if (c.looping) {
        u = c.loopStart + positiveMod(u, span);
    } else {
        if (u >= span)
            return 0;
        u = c.loopStart + u;
    }
    double secondsPerSongTick = 60.0 / (tempo * double(kPpq));
    double env = std::min(1.0, elapsed * secondsPerSongTick / kEdgeFadeSeconds);
    if (!c.looping)
        env = std::min(env, (span - (u - c.loopStart)) * s * secondsPerSongTick / kEdgeFadeSeconds);
    else
        env = std::min(env, double(seamEnvelope(c, u, s, elapsed / s >= span)));
    out[0] = {slotKey(slot, c.clip), ci, u, ticksPerSample / s, float(env)};
    return 1;
}

void AudioPlayer::positionIn(const AudioClipPlay& c, bool rendered, const Wanted& w, double& target, double& rate) {
    if (rendered) {
        // A render is straight, warped or not: one line through it.
        double factor = c.clipTempo / c.renderTempo;
        target = (c.startFrame + w.tick * c.framesPerTick) * factor;
        rate = w.tickRate * c.framesPerTick * factor;
        return;
    }
    double perTick = 0;
    target = c.framesAt(w.tick, perTick);
    rate = w.tickRate * perTick;
}

void AudioPlayer::place(Voice& v, const Wanted& w) const {
    const AudioClipPlay& c = snapshot->clips[size_t(w.clip)];
    double target = 0;
    positionIn(c, v.rendered, w, target, v.rate);
    v.gain = c.gain * w.envelope;
    v.lastTick = w.tick;
    if (v.exact)
        v.frame++;
    else
        v.pos = target;
    (void)target;
}

void AudioPlayer::startVoice(TrackRun& run, const Wanted& w, float fadeSeconds) {
    const AudioClipPlay& c = snapshot->clips[size_t(w.clip)];
    bool rendered = c.follow == TempoFollow::Stretch && c.render &&
                    std::abs(c.renderTempo - tempo) <= kRenderTolerance * tempo;
    const AudioBuffer* buffer = rendered ? c.render : c.prepared;
    if (!buffer || buffer->frames <= 0)
        return;
    // The free slot, or else the quietest leaving voice.
    Voice* slot = nullptr;
    for (Voice& v : run.voices)
        if (!v.on) {
            slot = &v;
            break;
        }
    if (!slot)
        for (Voice& v : run.voices)
            if (v.leaving && (!slot || v.fade < slot->fade))
                slot = &v;
    if (!slot)
        return;
    Voice& v = *slot;
    v = Voice{};
    v.on = true;
    v.key = w.key;
    v.clip = w.clip;
    v.clipId = c.clip;
    v.buffer = buffer;
    v.rendered = rendered;
    v.free = c.follow == TempoFollow::Off;
    double target = 0;
    positionIn(c, rendered, w, target, v.rate);
    // An Off clip's step doesn't depend on the song tempo: one frame a sample, unless its buffer is still at another
    // sample rate.
    v.exact = std::abs(v.rate - 1) < 1e-9;
    v.pos = target;
    v.frame = int64_t(std::llround(target));
    v.gain = c.gain * w.envelope;
    v.lastTick = w.tick;
    float samples = float(std::max(1.0, double(fadeSeconds) * sampleRate));
    // A clip starting from silence (its own edge fade at 0) needs no second fade on top.
    bool silentStart = w.envelope < 0.05f && fadeSeconds <= float(kEdgeFadeSeconds);
    v.fade = silentStart ? 1 : 0;
    v.fadeStep = silentStart ? 0 : 1.f / samples;
}

void AudioPlayer::follow(TrackRun& run, const std::array<Wanted, 2>& want, int count) {
    std::array<bool, 2> placed{};
    std::array<float, 2> fadeFor{float(kEdgeFadeSeconds), float(kEdgeFadeSeconds)};
    for (Voice& v : run.voices) {
        if (!v.on || v.leaving)
            continue;
        int match = -1;
        for (int k = 0; k < count; k++)
            if (!placed[size_t(k)] && want[size_t(k)].key == v.key) {
                match = k;
                break;
            }
        if (match < 0) {
            retire(v, float(kSeamFadeSeconds * sampleRate));
            leaveNow(v);
            continue;
        }
        const Wanted& w = want[size_t(match)];
        const AudioClipPlay& c = snapshot->clips[size_t(w.clip)];
        bool rendered = c.follow == TempoFollow::Stretch && c.render &&
                        std::abs(c.renderTempo - tempo) <= kRenderTolerance * tempo;
        const AudioBuffer* buffer = rendered ? c.render : c.prepared;
        if (buffer != v.buffer || w.clip != v.clip) {
            // A render became ready, or the tempo moved off it: crossfade to the other buffer.
            retire(v, float(kRenderFadeSeconds * sampleRate));
            leaveNow(v);
            fadeFor[size_t(match)] = float(kRenderFadeSeconds);
            continue;
        }
        double target = 0, rate = 0;
        positionIn(c, rendered, w, target, rate);
        bool jump;
        if (v.free) {
            // Off clips keep their own pace; only a seam (the clip tick going back) or a jump of the song moves them.
            jump = jumped || w.tick < v.lastTick - 1e-6;
        } else if (v.exact) {
            jump = std::abs(rate - 1) >= 1e-9 || std::abs(double(v.frame + 1) - target) > 1.5;
        } else {
            jump = std::abs(target - (v.pos + v.rate)) > 1.5;
        }
        if (jump) {
            retire(v, float(kSeamFadeSeconds * sampleRate));
            leaveNow(v);
            fadeFor[size_t(match)] = float(kSeamFadeSeconds);
            continue;
        }
        if (v.free) {
            advance(v);
            v.gain = c.gain * w.envelope;
            v.lastTick = w.tick;
        } else {
            place(v, w);
        }
        placed[size_t(match)] = true;
    }
    for (int k = 0; k < count; k++)
        if (!placed[size_t(k)])
            startVoice(run, want[size_t(k)], fadeFor[size_t(k)]);
}

} // namespace tessera
