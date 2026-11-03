#include "AudioSnapshotBuilder.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

namespace {

/// Rounds a length factor so the same tempo always asks for the same render.
double roundFactor(double f) { return std::round(f * 1e6) / 1e6; }

} // namespace

bool wantsRender(const AudioContent& clip, double tempo, RenderKey& key) {
    if (clip.follow != TempoFollow::Stretch || !(tempo > 0))
        return false;
    key.source = clip.source;
    key.factor = roundFactor(clip.sourceTempo / tempo);
    key.semitones = clip.semitones + double(clip.cents) / 100;
    key.sound = clip.sound;
    return key.factor != 1 || key.semitones != 0;
}

AudioBuild buildAudioSnapshot(const Song& song, AudioBuffers& buffers, double engineRate, double tempo,
                              const std::vector<std::shared_ptr<const AudioBuffer>>& keep) {
    AudioBuild out;
    out.snapshot = std::make_unique<AudioSnapshot>();
    AudioSnapshot& snap = *out.snapshot;
    snap.masterVolume = dbToGain(song.master.volumeDb);
    snap.masterId = song.master.id.value;
    snap.buffers = keep;
    auto hold = [&](const std::shared_ptr<const AudioBuffer>& b) {
        if (b && std::find(snap.buffers.begin(), snap.buffers.end(), b) == snap.buffers.end())
            snap.buffers.push_back(b);
    };

    // One entry per audio clip any audio track uses.
    std::vector<ClipId> clipIds;
    auto clipIndex = [&](ClipId id) -> int {
        auto it = std::find(clipIds.begin(), clipIds.end(), id);
        if (it != clipIds.end())
            return int(it - clipIds.begin());
        const Clip* clip = song.findClip(id);
        if (!clip || !clip->audio)
            return -1;
        const AudioContent& a = *clip->audio;
        AudioClipPlay c;
        c.clip = id.value;
        c.clipTempo = a.sourceTempo;
        c.follow = a.follow;
        c.looping = clip->loopMode == LoopMode::Loop && clip->loopEnd > clip->loopStart;
        c.loopStart = double(clip->loopStart);
        c.loopEnd = double(clip->loopEnd);
        c.length = double(clip->length);
        if (const AudioSource* src = song.findSource(a.source)) {
            c.gain = dbToGain(a.gainDb) * src->volts;
            std::shared_ptr<const AudioBuffer> prepared = buffers.prepared(a.source);
            hold(prepared);
            c.prepared = prepared.get();
            // Positions are in the buffer's own frames: right after a rate change it may still be at the old rate.
            double bufferRate = prepared && prepared->rate > 0 ? prepared->rate : engineRate;
            c.framesPerTick = engineFrames(framesPerTick(a, src->sampleRate), src->sampleRate, bufferRate);
            c.startFrame = engineFrames(double(a.sourceStart), src->sampleRate, bufferRate);
            double frames = prepared ? double(prepared->frames)
                                     : engineFrames(double(src->frames), src->sampleRate, bufferRate);
            double after = frames - (c.startFrame + c.loopEnd * c.framesPerTick);
            c.seamDip = after < 0.006 * engineRate;
            RenderKey key;
            if (wantsRender(a, tempo, key)) {
                RenderKey found;
                std::shared_ptr<const AudioBuffer> render = buffers.render(key, found);
                double renderRate = render && render->rate > 0 ? render->rate : engineRate;
                if (render && found.factor > 0 && renderRate == bufferRate) {
                    hold(render);
                    c.render = render.get();
                    c.renderTempo = a.sourceTempo / found.factor;
                }
                if (!render || !(found == key))
                    out.wanted.push_back(key);
            }
        } else {
            c.gain = 0;
        }
        clipIds.push_back(id);
        snap.clips.push_back(c);
        return int(snap.clips.size()) - 1;
    };

    for (const Track& t : song.tracks) {
        if (t.type != TrackType::Audio)
            continue;
        AudioTrackPlay tp;
        tp.id = t.id;
        tp.volume = dbToGain(t.channel.volumeDb);
        tp.pan = t.channel.pan;
        tp.audible = song.isAudible(t);
        for (const ClipSlot& s : t.slots)
            tp.slots.push_back(s.clip ? clipIndex(*s.clip) : -1);
        for (const ArrangerClip& a : t.arrangement) {
            if (a.muted)
                continue;
            int ci = clipIndex(a.clip);
            if (ci < 0)
                continue;
            AudioInstancePlay ip;
            ip.id = a.id.value;
            ip.clip = ci;
            ip.position = double(a.position);
            ip.length = double(a.length);
            ip.offset = double(a.offset);
            ip.fadeIn = a.fadeIn;
            ip.fadeOut = a.fadeOut;
            tp.instances.push_back(ip);
        }
        std::stable_sort(tp.instances.begin(), tp.instances.end(),
                         [](const AudioInstancePlay& x, const AudioInstancePlay& y) { return x.position < y.position; });
        // Where neighbours overlap, the overlap is their crossfade (§4.3).
        auto endOf = [&](const AudioInstancePlay& x) {
            return x.position + x.length * snap.clips[size_t(x.clip)].songTicksPerTick(tempo);
        };
        for (size_t i = 0; i + 1 < tp.instances.size(); i++) {
            AudioInstancePlay& a = tp.instances[i];
            AudioInstancePlay& b = tp.instances[i + 1];
            double overlap = std::min(endOf(a), endOf(b)) - b.position;
            if (overlap > 0) {
                a.overlapOut = std::max(a.overlapOut, overlap);
                b.overlapIn = std::max(b.overlapIn, overlap);
            }
        }
        snap.tracks.push_back(std::move(tp));
    }
    return out;
}

} // namespace tessera
