#include "Snapshot.hpp"

#include "../model/Arrangement.hpp"
#include "../model/DeviceLookup.hpp"
#include "../model/Devices.hpp"
#include "../model/DrumKit.hpp"
#include "../model/Swing.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>

namespace tessera {

void BusSlots::sync(const Song& song) {
    for (TrackId& id : owner)
        if (id.valid() && !song.findTrack(id))
            id = TrackId();

    for (const Track& track : song.tracks) {
        if (slotOf(track.id) >= 0)
            continue;
        auto freeSlot = std::find(owner.begin(), owner.end(), TrackId());
        assert(freeSlot != owner.end() && "more tracks than kMaxTracks");
        if (freeSlot != owner.end())
            *freeSlot = track.id;
    }
}

int BusSlots::slotOf(TrackId id) const {
    for (size_t i = 0; i < owner.size(); i++)
        if (owner[i] == id)
            return int(i);
    return -1;
}

namespace {

/// Drum lanes keep their pitches: transposing a drum track would move notes onto other lanes.
int transposeOf(const Track& track) { return track.type == TrackType::Drum ? 0 : track.transpose; }

NoteEvent noteEvent(const Note& note, const Track& track, Tick end) {
    Tick length = std::max<Tick>(1, Tick(note.length * track.gateLengthScale));
    NoteEvent e;
    e.start = note.start;
    e.end = std::min(note.start + length, end);
    e.pitch = int8_t(std::clamp(note.pitch + transposeOf(track), 0, 127));
    e.velocity = note.velocity;
    e.releaseVelocity = note.releaseVelocity;
    e.channel = note.channel;
    return e;
}

std::vector<LanePlayback> flattenLanes(const std::vector<AutomationEnvelope>& lanes) {
    std::vector<LanePlayback> out;
    for (const AutomationEnvelope& l : lanes) {
        if (l.mute || l.paramTarget.valid())
            continue;
        LanePlayback p;
        p.output = std::clamp(l.outputIndex, 0, kMaxAutomationLanes - 1);
        p.rangeMin = l.rangeMin;
        p.rangeMax = l.rangeMax;
        p.rest = restValue(l);
        p.slewMs = l.slewMs;
        p.combine = l.combine;
        p.points = l.points;
        out.push_back(std::move(p));
    }
    return out;
}

bool hasExtras(const Note& n) {
    return !(n.ops == NoteOperators()) || n.microOffset != 0 || !n.pitchExpr.empty() || !n.timbreExpr.empty() ||
           !n.pressureExpr.empty();
}

/// Gives an event the extras of its note: an index into `extras`, and its start moved by the note's micro-timing
/// and early by the room timing spread needs. Inside a loop of [loopStart, loopStart + length) the moved start
/// wraps around (a note pushed before the loop start plays at the end of the previous pass).
/// The song's swing on one note (DESIGN.md §7), kept inside `limit` (a loop end).
void swingEvent(NoteEvent& e, float swing, Tick limit) {
    if (swing <= 0)
        return;
    Tick start = Tick(std::llround(swung(double(e.start), swing)));
    Tick end = Tick(std::llround(swung(double(e.end), swing)));
    e.start = std::min(start, limit - 1);
    e.end = std::max(e.start + 1, std::min(end, limit));
}

void attachExtras(NoteEvent& e, const Note& note, std::vector<NoteExtras>& extras, uint64_t clip, Tick loopStart,
                  Tick length, bool looping, double stretch = 1) {
    if (!hasExtras(note) || extras.size() >= kNoExtras)
        return;
    double spread = operators::spreadTicks(note);
    Tick shift = Tick(std::llround(operators::microTicks(note) - spread));
    e.extras = uint16_t(extras.size());
    extras.push_back({note, clip, spread, stretch});
    if (shift == 0)
        return;
    Tick duration = e.end - e.start;
    e.start += shift;
    if (looping && length > 0) {
        Tick r = ((e.start - loopStart) % length + length) % length;
        e.start = loopStart + r;
        e.end = std::min(e.start + duration, loopStart + length);
    } else {
        e.start = std::max<Tick>(0, e.start);
        e.end = e.start + duration;
    }
    e.end = std::max(e.end, e.start + 1);
}

void sortEvents(std::vector<NoteEvent>& events) {
    std::stable_sort(events.begin(), events.end(),
                     [](const NoteEvent& a, const NoteEvent& b) { return a.start < b.start; });
}

std::vector<KnobLanePlayback> flattenKnobLanes(const std::vector<AutomationEnvelope>& lanes) {
    std::vector<KnobLanePlayback> out;
    for (const AutomationEnvelope& l : lanes) {
        if (l.mute || !l.paramTarget.valid() || int(out.size()) >= kMaxKnobLanes)
            continue;
        KnobLanePlayback p;
        p.target = l.paramTarget;
        p.rest = restValue(l);
        p.combine = l.combine;
        p.points = l.points;
        out.push_back(std::move(p));
    }
    return out;
}

int channelsOf(const std::vector<LanePlayback>& lanes) {
    int n = 0;
    for (const LanePlayback& l : lanes)
        n = std::max(n, l.output + 1);
    return n;
}

ClipPlayback flattenClip(const Clip& clip, const Track& track, float swing, uint8_t key) {
    ClipPlayback out;
    out.looping = clip.loopMode == LoopMode::Loop;
    out.loopStart = out.looping ? clip.loopStart : 0;
    out.loopEnd = out.looping ? clip.loopEnd : clip.contentEnd();

    // Drum lanes with their own loop length get their own list (polymeter).
    std::vector<int> ownLoopPitches;
    if (track.type == TrackType::Drum) {
        for (const DrumLane& lane : track.drumLanes) {
            if (!hasOwnLoop(lane, clip))
                continue;
            LaneRegion region = laneRegion(lane, clip);
            LaneLoop loop;
            loop.start = region.start;
            loop.length = region.length;
            for (const Note& note : clip.notes)
                if (!note.muted && note.pitch == lane.pitch && note.start >= region.start &&
                    note.start < region.start + region.length) {
                    loop.notes.push_back(noteEvent(note, track, region.start + region.length));
                    swingEvent(loop.notes.back(), swing, region.start + region.length);
                    attachExtras(loop.notes.back(), note, out.extras, clip.id.value, region.start, region.length, true);
                }
            sortEvents(loop.notes);
            out.laneLoops.push_back(std::move(loop));
            ownLoopPitches.push_back(lane.pitch);
        }
    }

    out.lanes = flattenLanes(clip.automation);
    out.knobLanes = flattenKnobLanes(clip.automation);
    for (const Note& note : clip.notes) {
        if (note.muted || note.start < out.loopStart || note.start >= out.loopEnd)
            continue;
        if (std::find(ownLoopPitches.begin(), ownLoopPitches.end(), note.pitch) != ownLoopPitches.end())
            continue;
        out.notes.push_back(noteEvent(note, track, out.loopEnd));
        swingEvent(out.notes.back(), swing, out.loopEnd);
        attachExtras(out.notes.back(), note, out.extras, clip.id.value, out.loopStart, out.loopLength(), out.looping);
    }
    // Clip notes are already sorted by start; stable keeps chords in pitch order.
    sortEvents(out.notes);
    for (NoteEvent& e : out.notes)
        e.key = key;
    for (LaneLoop& loop : out.laneLoops)
        for (NoteEvent& e : loop.notes)
            e.key = key;
    return out;
}

void fillDrumLanes(TrackPlayback& tp, const Track& track) {
    tp.laneOfPitch.fill(-1);
    if (track.type != TrackType::Drum)
        return;
    tp.drum = true;
    bool anySolo =
        std::any_of(track.drumLanes.begin(), track.drumLanes.end(), [](const DrumLane& l) { return l.solo; });
    for (size_t i = 0; i < track.drumLanes.size(); i++) {
        const DrumLane& lane = track.drumLanes[i];
        DrumLanePlayback p;
        p.pitch = lane.pitch;
        p.output = std::clamp(lane.outputIndex, 0, kMaxDrumLanes - 1);
        p.gateMs = lane.gateLengthMs;
        p.choke = lane.chokeGroup;
        p.audible = anySolo ? lane.solo : !lane.mute;
        tp.drumLanes.push_back(p);
        if (tp.laneOfPitch[size_t(lane.pitch)] < 0)
            tp.laneOfPitch[size_t(lane.pitch)] = int8_t(i);
        tp.drumChannels = std::max(tp.drumChannels, p.output + 1);
    }
}

std::vector<ClipId> slotClipsOf(const Track& track) {
    std::vector<ClipId> out;
    out.reserve(track.slots.size());
    for (const ClipSlot& slot : track.slots)
        out.push_back(slot.clip ? *slot.clip : ClipId());
    return out;
}

std::shared_ptr<const TrackPlayback> buildTrack(const Song& song, const Track& track, const BusSlots& slots) {
    auto tp = std::make_shared<TrackPlayback>();
    tp->id = track.id;
    tp->busSlot = slots.slotOf(track.id);
    tp->polyphony = song.voicesOf(track);
    tp->audible = song.isAudible(track);
    tp->armed = track.armed;
    tp->monitor = track.monitor;
    tp->input = track.recordInput;
    tp->midiChannel = track.midiChannel;
    tp->listen = track.listen;
    tp->listenTrack = track.listenTrack;
    tp->slotClips = slotClipsOf(track);
    fillDrumLanes(*tp, track);
    // Clips with their own key get an entry in the track's key list, for devices that work in a key.
    std::map<ClipId, uint8_t> keyOf;
    auto keyIndex = [&](const Clip& clip) -> uint8_t {
        if (!clip.scaleOverride)
            return 0;
        auto it = keyOf.find(clip.id);
        if (it != keyOf.end())
            return it->second;
        if (tp->keys.size() >= 255)
            return 0;
        tp->keys.push_back(resolveScale(*clip.scaleOverride, song.customScales));
        return keyOf[clip.id] = uint8_t(tp->keys.size());
    };
    // A clip in several slots is flattened once and shared.
    std::map<ClipId, std::shared_ptr<const ClipPlayback>> built;
    for (ClipId id : tp->slotClips) {
        std::shared_ptr<const ClipPlayback> playback;
        if (const Clip* clip = id.valid() ? song.findClip(id) : nullptr) {
            auto it = built.find(id);
            if (it != built.end()) {
                playback = it->second;
            } else {
                auto flat = std::make_shared<ClipPlayback>(flattenClip(*clip, track, song.swing, keyIndex(*clip)));
                if (clip->launchQuantize)
                    flat->launchQuantize = launchQuantizeTicks(*clip->launchQuantize, song.timeSig);
                if (flat->loopLength() > 0)
                    playback = flat;
                built[id] = playback;
            }
        }
        tp->slots.push_back(playback);
    }

    tp->lanes = flattenLanes(track.automation);
    tp->knobLanes = flattenKnobLanes(track.automation);
    tp->autoChannels = channelsOf(tp->lanes);
    for (const auto& slot : tp->slots)
        if (slot)
            tp->autoChannels = std::max(tp->autoChannels, channelsOf(slot->lanes));
    for (const ArrangerClip& a : track.arrangement) {
        const Clip* clip = song.findClip(a.clip);
        if (!clip || a.muted || clip->automation.empty())
            continue;
        auto it = built.find(a.clip);
        std::shared_ptr<const ClipPlayback> playback =
            it != built.end() && it->second
                ? it->second
                : std::make_shared<ClipPlayback>(flattenClip(*clip, track, song.swing, keyIndex(*clip)));
        built[a.clip] = playback;
        if (playback->lanes.empty() && playback->knobLanes.empty())
            continue;
        ClipSpan span = clipSpan(*clip);
        InstanceLanes inst;
        inst.position = a.position;
        inst.end = instanceEnd(a);
        inst.offset = double(normalizeOffset(*clip, a.offset));
        inst.stretch = a.stretch > 0 ? double(a.stretch) : 1.0;
        inst.looping = span.looping;
        inst.spanStart = span.start;
        inst.spanEnd = span.end;
        inst.clip = playback;
        tp->arrangementLanes.push_back(inst);
        tp->autoChannels = std::max(tp->autoChannels, channelsOf(playback->lanes));
    }

    std::vector<TimedNote> timed;
    for (const ArrangerClip& a : track.arrangement) {
        if (std::find(tp->arrangementClips.begin(), tp->arrangementClips.end(), a.clip) == tp->arrangementClips.end())
            tp->arrangementClips.push_back(a.clip);
        const Clip* clip = song.findClip(a.clip);
        if (!clip || a.muted)
            continue;
        size_t first = timed.size();
        uint8_t key = keyIndex(*clip);
        renderInstance(*clip, a, timed, transposeOf(track), track.gateLengthScale,
                       track.type == TrackType::Drum ? &track.drumLanes : nullptr);
        tp->arrangement.reserve(timed.size());
        for (size_t i = first; i < timed.size(); i++) {
            const TimedNote& n = timed[i];
            NoteEvent e{n.start, n.end, n.pitch, n.velocity, n.releaseVelocity};
            e.pass = n.pass;
            e.key = key;
            e.channel = n.source ? n.source->channel : 1;
            swingEvent(e, song.swing, instanceEnd(a));
            attachExtras(e, *n.source, tp->arrangementExtras, clip->id.value, 0, 0, false,
                         std::clamp(double(a.stretch), double(kMinStretch), double(kMaxStretch)));
            tp->arrangement.push_back(e);
        }
    }
    sortEvents(tp->arrangement);
    return tp;
}

/// Devices that pass notes through and their order, switch and drum setting: what notes in flight depend on.
uint64_t chainStructure(const Track& track) {
    uint64_t h = 1469598103934665603ull;
    auto add = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (const Device& d : track.devices) {
        add(d.id.value);
        add(uint64_t(d.type));
        add(uint64_t(d.on) << 1 | uint64_t(d.affectDrumLanes));
    }
    return h;
}

/// Knob tables, chains and modulators for the engine (docs/DEVICES.md §5).
void buildModulation(const Song& song, Snapshot& snap, const ModulatorSlots& slots) {
    auto addOwner = [&snap](ParamRef::Kind kind, uint64_t id, const std::vector<float>& params) {
        int first = int(snap.knobs.size());
        int n = std::min<int>(int(params.size()), kMaxDeviceParams);
        snap.knobs.insert(snap.knobs.end(), params.begin(), params.begin() + n);
        snap.owners.push_back({kind, id, first, n});
        return first;
    };

    snap.chains.resize(song.tracks.size());
    for (size_t i = 0; i < song.tracks.size(); i++) {
        const Track& track = song.tracks[i];
        ChainPlayback& chain = snap.chains[i];
        for (const Device& d : track.devices) {
            DevicePlayback dp;
            dp.type = d.type;
            dp.on = d.on;
            dp.affectDrums = d.affectDrumLanes;
            dp.id = d.id.value;
            dp.knobs = addOwner(ParamRef::Kind::Device, d.id.value, d.params);
            chain.devices.push_back(dp);
            chain.active |= d.on;
        }
        chain.structure = chainStructure(track);
    }

    forEachModulator(song, [&](const Modulator& m, const ModulatorPlace& place) {
        int slot = slots.slotOf(m.id);
        if (slot < 0)
            return;
        ModulatorPlayback mp;
        mp.type = m.type;
        mp.id = m.id.value;
        mp.slot = slot;
        mp.knobs = addOwner(ParamRef::Kind::Modulator, m.id.value, m.params);
        mp.track = place.level == ModLevel::Song ? -1 : song.trackIndex(place.track);
        mp.stepCount = int(std::min<size_t>(m.steps.size(), kMaxSteps));
        std::copy(m.steps.begin(), m.steps.begin() + mp.stepCount, mp.steps.begin());
        snap.modulators.push_back(mp);
        if (mp.track >= 0)
            snap.chains[size_t(mp.track)].modulatorSlots.push_back(slot);
    });
    assert(snap.knobs.size() <= size_t(kMaxKnobs));
    std::sort(snap.owners.begin(), snap.owners.end(),
              [](const KnobOwner& a, const KnobOwner& b) { return a.kind != b.kind ? a.kind < b.kind : a.id < b.id; });

    forEachModulator(song, [&](const Modulator& m, const ModulatorPlace&) {
        int slot = slots.slotOf(m.id);
        if (slot < 0)
            return;
        for (const Modulation& r : m.routes) {
            ModSource source{slot, r.amount};
            if (r.kind == Modulation::Kind::Param) {
                int knob = snap.knobIndex(r.target);
                if (knob < 0)
                    continue;
                auto it = std::find_if(snap.modulated.begin(), snap.modulated.end(),
                                       [knob](const KnobModulation& k) { return k.knob == knob; });
                if (it == snap.modulated.end()) {
                    snap.modulated.push_back({});
                    it = snap.modulated.end() - 1;
                    it->knob = knob;
                }
                if (it->count < kMaxModulationsPerKnob)
                    it->sources[size_t(it->count++)] = source;
            } else if (r.kind == Modulation::Kind::AutoChannel) {
                int t = song.trackIndex(r.track);
                if (t < 0 || r.channel < 0 || r.channel >= kMaxAutomationLanes)
                    continue;
                ChainPlayback& chain = snap.chains[size_t(t)];
                chain.autoRoutes.push_back({r.channel, source});
                chain.autoChannels = std::max(chain.autoChannels, r.channel + 1);
            }
            // Rack knobs are turned by the module, which maps them itself.
        }
    });

    auto laneKnobs = [&snap](const std::vector<AutomationEnvelope>& lanes) {
        for (const AutomationEnvelope& l : lanes)
            if (l.paramTarget.valid() && !l.mute)
                if (int k = snap.knobIndex(l.paramTarget); k >= 0)
                    snap.laneKnobs.push_back(k);
    };
    for (const Track& t : song.tracks)
        laneKnobs(t.automation);
    for (const auto& [id, clip] : song.clips)
        laneKnobs(clip.automation);
    std::sort(snap.laneKnobs.begin(), snap.laneKnobs.end());
    snap.laneKnobs.erase(std::unique(snap.laneKnobs.begin(), snap.laneKnobs.end()), snap.laneKnobs.end());
}

template <typename IdT>
bool contains(const std::vector<IdT>& ids, IdT id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

} // namespace

void ModulatorSlots::sync(const Song& song) {
    std::unordered_map<uint64_t, int> kept;
    forEachModulator(song, [&](const Modulator& m, const ModulatorPlace&) {
        auto it = slots.find(m.id.value);
        if (it != slots.end())
            kept[m.id.value] = it->second;
    });
    for (const auto& [id, slot] : slots)
        if (!kept.count(id))
            freeSlots.push_back(slot);
    slots = std::move(kept);
    forEachModulator(song, [&](const Modulator& m, const ModulatorPlace&) {
        if (slots.count(m.id.value))
            return;
        int slot;
        if (!freeSlots.empty()) {
            slot = freeSlots.back();
            freeSlots.pop_back();
        } else if (used < kMaxModulatorSlots) {
            slot = used++;
        } else {
            return;
        }
        slots[m.id.value] = slot;
    });
}

int ModulatorSlots::slotOf(ModulatorId id) const {
    auto it = slots.find(id.value);
    return it != slots.end() ? it->second : -1;
}

int Snapshot::knobIndex(const ParamRef& ref) const {
    auto it = std::lower_bound(owners.begin(), owners.end(), ref, [](const KnobOwner& o, const ParamRef& r) {
        return o.kind != r.kind ? o.kind < r.kind : o.id < r.owner;
    });
    if (it == owners.end() || it->kind != ref.kind || it->id != ref.owner || ref.param >= it->count)
        return -1;
    return it->knobs + ref.param;
}

double InstanceLanes::clipTime(double tick) const {
    double rel = tick - double(position);
    if (rel < 0 || tick >= double(end) || spanEnd <= spanStart)
        return NAN;
    double u = offset + rel / stretch;
    if (u < double(spanEnd))
        return u;
    if (!looping)
        return NAN;
    return double(spanStart) + std::fmod(u - double(spanEnd), double(spanEnd - spanStart));
}

std::unique_ptr<Snapshot> SnapshotBuilder::build(const Song& song, const BusSlots& slots, const ChangeSet& changes) {
    // Soloing one track changes whether every other track is heard.
    bool anySolo = song.anySolo();
    bool everything = changes.everything || anySolo != lastAnySolo;
    lastAnySolo = anySolo;

    auto snap = std::make_unique<Snapshot>();
    snap->timeSig = song.timeSig;
    snap->loopStart = song.loopStart;
    snap->loopEnd = song.loopEnd;
    snap->launchQuantize = launchQuantizeTicks(song.launchQuantize, song.timeSig);
    snap->key = resolveScale(song.scale, song.customScales);
    snap->seed = song.seed;
    for (const Scene& scene : song.scenes)
        snap->sceneTempos.push_back(scene.tempo);
    snap->tracks.reserve(song.tracks.size());

    std::unordered_map<TrackId, std::shared_ptr<const TrackPlayback>> next;
    rebuilt = 0;
    for (const Track& track : song.tracks) {
        // A cached track is stale when it was edited, a slot now holds another clip, or one of its clips changed.
        auto cached = cache.find(track.id);
        bool reuse = !everything && cached != cache.end() && !contains(changes.tracks, track.id) &&
                     cached->second->slotClips == slotClipsOf(track);
        if (reuse)
            for (const std::vector<ClipId>* used : {&cached->second->slotClips, &cached->second->arrangementClips})
                for (ClipId id : *used)
                    if (id.valid() && contains(changes.clips, id))
                        reuse = false;
        std::shared_ptr<const TrackPlayback> tp = reuse ? cached->second : buildTrack(song, track, slots);
        if (!reuse)
            rebuilt++;
        next[track.id] = tp;
        snap->tracks.push_back(std::move(tp));
    }
    cache = std::move(next);

    chain::warmUp();
    modSlots.sync(song);
    buildModulation(song, *snap, modSlots);
    return snap;
}

std::unique_ptr<Snapshot> buildSnapshot(const Song& song, const BusSlots& slots) {
    SnapshotBuilder builder;
    return builder.build(song, slots, ChangeSet::all());
}

} // namespace tessera
