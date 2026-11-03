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

void AudioPlayer::hold(uint64_t track, float volumeGain, float pan) {
    for (Hold& h : holds) {
        uint64_t id = h.track.load(std::memory_order_relaxed);
        if (id == track || id == 0) {
            h.volume.store(volumeGain, std::memory_order_relaxed);
            h.pan.store(pan, std::memory_order_relaxed);
            h.track.store(track, std::memory_order_release);
            return;
        }
    }
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
    auto held = [&](uint64_t id, float& volume, float& pan) {
        if (!anyHold)
            return;
        for (const Hold& h : holds)
            if (h.track.load(std::memory_order_acquire) == id) {
                volume = h.volume.load(std::memory_order_relaxed);
                pan = h.pan.load(std::memory_order_relaxed);
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
        held(t.id.value, volume, pan);
        float l = 0, r = 0;
        if (t.audible) {
            balanceGains(pan, l, r);
            l *= volume;
            r *= volume;
        }
        TrackRun& run = runs[i];
        float out = t.audible ? 1.f : 0.f;
        if (run.fresh) {
            run.gainL = l;
            run.gainR = r;
            run.outGain = out;
            run.fresh = false;
        }
        run.stepL = (l - run.gainL) / kBlock;
        run.stepR = (r - run.gainR) / kBlock;
        run.outStep = (out - run.outGain) / kBlock;
    }
    float master = snapshot ? snapshot->masterVolume : 0;
    float masterPan = 0;
    if (snapshot)
        held(snapshot->masterId, master, masterPan);
    if (masterFresh && snapshot) {
        masterGain = master;
        masterFresh = false;
    }
    masterStep = (master - masterGain) / kBlock;
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
            // OUT carries the track's clips before its fader; what comes back on IN joins them (§2, §8.4).
            // A muted (or soloed-away) track's own clips stop feeding its effect too.
            run.outGain += run.outStep;
            if (rack.out && tp.busSlot >= 0 && tp.busSlot < kMaxTracks)
                rack.out[tp.busSlot] = {tp.id.value, sl * run.outGain, sr * run.outGain};
            if (rack.returns)
                for (int slot : tp.returns) {
                    float rl, rr;
                    if (rack.returns->read(rack.frame, slot, tp.id, rl, rr)) {
                        sl += rl;
                        sr += rr;
                    }
                }
        }
        run.gainL += run.stepL;
        run.gainR += run.stepR;
        sl *= run.gainL;
        sr *= run.gainR;
        run.peakL = std::max(run.peakL, std::abs(sl));
        run.peakR = std::max(run.peakR, std::abs(sr));
        masterL += sl;
        masterR += sr;
    }
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
            run.peakL = run.peakR = 0;
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

void AudioPlayer::place(Voice& v, const Wanted& w) const {
    const AudioClipPlay& c = snapshot->clips[size_t(w.clip)];
    double factor = v.rendered ? c.clipTempo / c.renderTempo : 1.0;
    double target = (c.startFrame + w.tick * c.framesPerTick) * factor;
    v.rate = w.tickRate * c.framesPerTick * factor;
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
    double factor = rendered ? c.clipTempo / c.renderTempo : 1.0;
    double target = (c.startFrame + w.tick * c.framesPerTick) * factor;
    v.rate = w.tickRate * c.framesPerTick * factor;
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
        double factor = rendered ? c.clipTempo / c.renderTempo : 1.0;
        double target = (c.startFrame + w.tick * c.framesPerTick) * factor;
        double rate = w.tickRate * c.framesPerTick * factor;
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
