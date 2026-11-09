#include "AudioSnapshotBuilder.hpp"

#include "AudioPlayer.hpp"

#include "../model/AudioRegions.hpp"
#include "../model/Sends.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <tuple>

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
    key.reverse = clip.reverse;
    return key.factor != 1 || key.semitones != 0 || (!clip.warps.empty() && !clip.reverse);
}

AudioBuild buildAudioSnapshot(const Song& song, AudioBuffers& buffers, double engineRate, double tempo,
                              const std::vector<std::shared_ptr<const AudioBuffer>>& keep, const MixPlan& plan) {
    AudioBuild out;
    out.snapshot = std::make_unique<AudioSnapshot>();
    AudioSnapshot& snap = *out.snapshot;
    snap.masterVolume = dbToGain(song.master.volumeDb);
    snap.masterMixGain = song.master.mix.use ? dbToGain(song.master.mix.db) : 0.f;
    snap.masterId = song.master.id.value;
    for (const AutomationEnvelope& l : song.master.automation)
        if (l.drive == LaneDrive::Volume && !l.mute && !l.points.empty())
            snap.masterLane = l.points;
    for (const MixPlan::Return& r : plan.returns)
        if (r.track == song.master.id)
            snap.masterReturns.push_back(r.slot);
    snap.buffers = keep;
    auto hold = [&](const std::shared_ptr<const AudioBuffer>& b) {
        if (b && std::find(snap.buffers.begin(), snap.buffers.end(), b) == snap.buffers.end())
            snap.buffers.push_back(b);
    };

    // One entry per audio clip, take and direction that plays: a copy picking another take plays that take's file,
    // and a part played backwards reads the reversed buffer.
    std::vector<std::tuple<ClipId, int, bool>> clipIds;
    std::function<int(ClipId, int, bool)> clipIndex = [&](ClipId id, int take, bool backwards) -> int {
        const Clip* clip = song.findClip(id);
        if (!clip || !clip->audio)
            return -1;
        if (take >= int(clip->audio->takes.size()))
            take = -1;
        // Hear (§4): this clip plays one take whole for now, whatever its parts say.
        bool heard = plan.hearClip == id && !backwards && take < 0;
        if (heard)
            take = plan.hearTake;
        auto it = std::find(clipIds.begin(), clipIds.end(), std::make_tuple(id, take, backwards));
        if (it != clipIds.end())
            return int(it - clipIds.begin());
        AudioContent a = takeContent(*clip->audio, take);
        if (backwards) {
            a.reverse = true;
            a.warps.clear();
        }
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
            std::shared_ptr<const AudioBuffer> prepared = buffers.prepared(a.source, a.reverse);
            hold(prepared);
            c.prepared = prepared.get();
            // Positions are in the buffer's own frames: right after a rate change it may still be at the old rate.
            double bufferRate = prepared && prepared->rate > 0 ? prepared->rate : engineRate;
            c.framesPerTick = engineFrames(framesPerTick(a, src->sampleRate), src->sampleRate, bufferRate);
            c.startFrame = engineFrames(double(a.sourceStart), src->sampleRate, bufferRate);
            double frames = prepared ? double(prepared->frames)
                                     : engineFrames(double(src->frames), src->sampleRate, bufferRate);
            // A reversed clip plays its window of the reversed buffer forwards: the window's end comes first.
            if (a.reverse)
                c.startFrame = frames - (c.startFrame + double(clip->length) * c.framesPerTick);
            double after = frames - (c.startFrame + c.loopEnd * c.framesPerTick);
            c.seamDip = after < 0.006 * engineRate;
            if (!a.warps.empty() && !a.reverse)
                for (const WarpMarker& m : a.warps)
                    c.warps.push_back({m.tick, engineFrames(double(m.frame), src->sampleRate, bufferRate)});
            RenderKey key;
            if (wantsRender(a, tempo, key)) {
                key.warps = c.warps;
                if (!c.warps.empty()) {
                    key.startFrame = c.startFrame;
                    key.framesPerTick = c.framesPerTick;
                }
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
        clipIds.push_back({id, take, backwards});
        snap.clips.push_back(c);
        int index = int(snap.clips.size()) - 1;
        if (backwards || take >= 0 || heard || clip->audio->regions.empty())
            return index;
        const AudioSource* src = song.findSource(a.source);
        if (!src)
            return index;
        regions::Files files = regions::filesOf(song, *clip->audio);
        // Each part reads either this entry or its backwards twin, moved to the place in the file it plays.
        std::vector<AudioRegionPlay> parts;
        double ticksScale = snap.clips[size_t(index)].songTicksPerTick(tempo);
        for (const AudioRegion& r : clip->audio->regions) {
            if (r.muted)
                continue;
            AudioRegionPlay p;
            p.span.id = r.id.value;
            p.span.position = double(r.start);
            p.span.length = double(r.length);
            p.span.fadeIn = r.fadeIn;
            p.span.fadeOut = r.fadeOut;
            p.gain = dbToGain(r.gainDb);
            p.clip = clipIndex(id, r.take, r.reverse);
            if (p.clip < 0)
                continue;
            if (r.reverse) {
                const AudioClipPlay& twin = snap.clips[size_t(p.clip)];
                double perSourceTick = framesPerTick(*clip->audio, files.rateOf(r.take));
                double scale = perSourceTick > 0 ? twin.framesPerTick / perSourceTick : 1;
                double frames = twin.prepared ? double(twin.prepared->frames) : double(files.framesOf(r.take)) * scale;
                double firstFrame = frames - (double(r.sourceFrame) * scale + double(r.length) * twin.framesPerTick);
                p.shift = twin.framesPerTick > 0 ? (firstFrame - twin.startFrame) / twin.framesPerTick - double(r.start) : 0;
            } else {
                p.shift = regions::shift(*clip->audio, r, files);
            }
            // A plain cut changes nothing you can hear, so a part that simply plays on from the last one joins it.
            if (!parts.empty()) {
                AudioRegionPlay& last = parts.back();
                constexpr double kSameAudioTicks = 0.1; // the rounding of a frame
                if (last.span.position + last.span.length == p.span.position && last.clip == p.clip &&
                    std::abs(last.shift - p.shift) < kSameAudioTicks && last.gain == p.gain &&
                    last.span.fadeOut.length == 0 && p.span.fadeIn.length == 0) {
                    last.span.length += p.span.length;
                    last.span.fadeOut = p.span.fadeOut;
                    continue;
                }
            }
            parts.push_back(p);
        }
        // Where neighbours overlap, the overlap is their crossfade, as on the timeline. Song ticks, as the envelope wants.
        for (size_t i = 0; i + 1 < parts.size(); i++) {
            AudioInstancePlay& x = parts[i].span;
            AudioInstancePlay& y = parts[i + 1].span;
            double overlap = x.position + x.length - y.position;
            if (overlap > 0) {
                x.overlapOut = overlap * ticksScale;
                y.overlapIn = overlap * ticksScale;
            }
        }
        snap.clips[size_t(index)].regions = std::move(parts);
        return index;
    };

    // Who is heard (§4.5): mute silences a track and its sends; solo follows the sends, so a soloed track's effect
    // tracks stay on (carrying only its sends) and the tracks feeding a soloed effect keep feeding it.
    bool anySolo = song.anySolo();
    auto soloed = [&](const Track& t) { return t.solo && !t.mute; };
    auto downstream = [&](const Track& t) {
        for (const Track& s : song.tracks)
            if (soloed(s) && s.id != t.id && sendsReach(song, s.id, t.id))
                return true;
        return false;
    };
    auto upstream = [&](const Track& t) {
        for (const Track& s : song.tracks)
            if (soloed(s) && s.id != t.id && sendsReach(song, t.id, s.id))
                return true;
        return false;
    };
    std::vector<TrackId> order = sendOrder(song);
    for (TrackId id : order) {
        const Track& t = *song.findTrack(id);
        if (!makesSound(t, plan) && sendersTo(song, t.id).empty())
            continue;
        AudioTrackPlay tp;
        tp.id = t.id;
        for (const MixPlan::Return& r : plan.returns)
            if (r.track == t.id)
                (r.source ? tp.sources : tp.returns).push_back(r.slot);
        for (const auto& [track, slot] : plan.busSlots)
            if (track == t.id)
                tp.busSlot = slot;
        tp.volume = dbToGain(t.channel.volumeDb);
        bool othersSoloed = !t.clips.solo &&
                            std::find(plan.inputSolo.begin(), plan.inputSolo.end(), t.id) != plan.inputSolo.end();
        tp.clipsGain = t.clips.use && !othersSoloed ? dbToGain(t.clips.db) : 0.f;
        tp.clipsPan = t.clips.pan;
        tp.pan = t.channel.pan;
        tp.audible = tp.own = tp.sending = !t.mute;
        tp.rollIn = t.type == TrackType::Audio && t.recordInput == RecordInput::RollIn;
        tp.hearRollIn = tp.rollIn && t.armed && t.monitor;
        if (!t.mute && anySolo && !soloed(t)) {
            bool down = downstream(t), up = upstream(t);
            tp.own = up && !down;
            tp.audible = down;
            tp.sending = down || up;
        }
        for (const ClipSlot& s : t.slots)
            tp.slots.push_back(s.clip ? clipIndex(*s.clip, -1, false) : -1);
        for (const ArrangerClip& a : t.arrangement) {
            if (a.muted)
                continue;
            int ci = clipIndex(a.clip, -1, false);
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
    // Group strips: a group with a track that makes sound gets one, and its tracks go through it.
    for (const TrackGroup& g : song.groups) {
        int index = -1;
        for (AudioTrackPlay& tp : snap.tracks)
            if (std::find(g.tracks.begin(), g.tracks.end(), tp.id) != g.tracks.end()) {
                if (index < 0 && snap.groups.size() < size_t(kMaxTracks)) {
                    snap.groups.push_back({g.id.value, dbToGain(g.channel.volumeDb), g.channel.pan, !g.mute});
                    index = int(snap.groups.size()) - 1;
                }
                tp.group = index;
            }
    }
    // Delay compensation: every track waits as long as the latest one is late, less its own lateness.
    int latest = 0;
    for (const AudioTrackPlay& tp : snap.tracks)
        if (const Track* t = song.findTrack(tp.id))
            latest = std::max(latest, t->latencySamples);
    for (AudioTrackPlay& tp : snap.tracks)
        if (const Track* t = song.findTrack(tp.id))
            tp.delay = std::min(latest - t->latencySamples, AudioPlayer::kMaxDelay - 1);
    // Each send by the index of its receiving track, which comes later in the order.
    for (size_t i = 0; i < snap.tracks.size(); i++) {
        const Track* t = song.findTrack(snap.tracks[i].id);
        for (const Send& send : t->sends)
            for (size_t j = i + 1; j < snap.tracks.size(); j++)
                if (snap.tracks[j].id == send.to && snap.tracks[i].sends.size() < size_t(kMaxSendsPerTrack)) {
                    SendPlay play{int(j), dbToGain(send.db), send.preFader, send.to.value, {}};
                    for (const AutomationEnvelope& l : t->automation)
                        if (l.drive == LaneDrive::Send && l.sendTo == send.to && !l.mute && !l.points.empty())
                            play.lane = l.points;
                    snap.tracks[i].sends.push_back(std::move(play));
                }
    }
    return out;
}

} // namespace tessera
