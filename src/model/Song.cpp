#include "Song.hpp"

#include "Chord.hpp"
#include "DeviceLookup.hpp"
#include "DeviceTypes.hpp"
#include "Devices.hpp"

#include "Arrangement.hpp"

#include "DrumKit.hpp"
#include "TrackColors.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <set>

namespace tessera {

Track* Song::findTrack(TrackId id) {
    for (Track& t : tracks)
        if (t.id == id)
            return &t;
    return nullptr;
}

const Track* Song::findTrack(TrackId id) const { return const_cast<Song*>(this)->findTrack(id); }

const TrackGroup* Song::findGroup(GroupId id) const {
    for (const TrackGroup& g : groups)
        if (g.id == id)
            return &g;
    return nullptr;
}

int Song::trackIndex(TrackId id) const {
    for (size_t i = 0; i < tracks.size(); i++)
        if (tracks[i].id == id)
            return int(i);
    return -1;
}

Clip* Song::findClip(ClipId id) {
    auto it = clips.find(id);
    return it == clips.end() ? nullptr : &it->second;
}

const Clip* Song::findClip(ClipId id) const { return const_cast<Song*>(this)->findClip(id); }

const Clip* Song::activeClip(const Track& track) const {
    for (const ClipSlot& slot : track.slots)
        if (slot.clip)
            return findClip(*slot.clip);
    return nullptr;
}

int Song::clipUses(ClipId clip) const {
    int uses = 0;
    for (const Track& t : tracks) {
        for (const ClipSlot& s : t.slots)
            uses += s.clip == clip;
        for (const ArrangerClip& a : t.arrangement)
            uses += a.clip == clip;
    }
    return uses;
}

namespace {

/// How many voices the track's devices need for `voices` notes at once: chords multiply them, echoes and longer
/// notes overlap them, an arpeggiator plays one step at a time (all of them at once in its Chord pattern).
int voicesThroughChain(const Track& track, int voices) {
    for (const Device& d : track.devices) {
        if (!d.on)
            continue;
        auto shown = [&d](int k) {
            const devices::ParamInfo* p = devices::paramInfo(d.type, k);
            return p && size_t(k) < d.params.size() ? devices::display(*p, d.params[size_t(k)]) : 0.f;
        };
        switch (d.type) {
        case DeviceType::Chord: {
            const std::vector<ChordShapeInfo>& shapes = chordShapes();
            int shape = std::clamp(int(shown(chain::knob::kChord)), 0, int(shapes.size()) - 1);
            voices *= std::max(1, chordSize(shapes[size_t(shape)].shape));
            break;
        }
        case DeviceType::Arpeggiator:
            if (chain::ArpPattern(int(shown(chain::knob::kPattern))) == chain::ArpPattern::Chord)
                voices *= std::max(1, int(shown(chain::knob::kArpOctaves)));
            else
                voices = shown(chain::knob::kGate) > 100 ? 2 : 1;
            break;
        case DeviceType::Echo:
        case DeviceType::NoteLength:
            voices *= 2;
            break;
        default:
            break;
        }
        voices = std::min(voices, kMaxVoices);
    }
    return voices;
}

} // namespace

int Song::voicesOf(const Track& track) const {
    if (track.polyphony != kAutoVoices)
        return std::clamp(track.polyphony, 1, kMaxVoices);
    int most = 1;
    auto add = [&](ClipId id) {
        if (const Clip* clip = findClip(id))
            most = std::max(most, clip->maxSimultaneousNotes());
    };
    for (const ClipSlot& s : track.slots)
        if (s.clip)
            add(*s.clip);
    // Stacked clips on the timeline play together: their voices add up while they overlap.
    std::vector<std::pair<Tick, int>> changes;
    for (const ArrangerClip& a : track.arrangement)
        if (const Clip* clip = findClip(a.clip)) {
            int n = clip->maxSimultaneousNotes();
            changes.push_back({a.position, n});
            changes.push_back({a.position + a.length, -n});
        }
    std::sort(changes.begin(), changes.end()); // ends (negative) before starts at the same tick
    int sounding = 0;
    for (const auto& [tick, n] : changes) {
        sounding += n;
        most = std::max(most, sounding);
    }
    return std::clamp(voicesThroughChain(track, std::min(most, kMaxVoices)), 1, kMaxVoices);
}

bool Song::anySolo() const {
    return std::any_of(tracks.begin(), tracks.end(), [](const Track& t) { return t.solo; });
}

bool Song::hasNotes(const Track& track) const {
    auto filled = [this](ClipId id) {
        const Clip* c = findClip(id);
        return c && !c->notes.empty();
    };
    for (const ClipSlot& s : track.slots)
        if (s.clip && filled(*s.clip))
            return true;
    for (const ArrangerClip& a : track.arrangement)
        if (filled(a.clip))
            return true;
    return false;
}

bool Song::isAudible(const Track& track) const {
    if (track.mute)
        return false;
    return !anySolo() || track.solo;
}

namespace {

template <typename T>
void clampValue(T& v, T lo, T hi) {
    v = std::clamp(v, lo, hi);
}

bool validDenominator(int den) { return den == 1 || den == 2 || den == 4 || den == 8 || den == 16 || den == 32; }

/// Hands out fresh IDs for missing or repeated ones. Collects every ID first so new ones never collide.
class IdRepair {
  public:
    explicit IdRepair(Song& song) : song(song) {}

    void see(uint64_t value) { song.nextId = std::max(song.nextId, value + 1); }

    void claim(uint64_t value) { used.insert(value); }

    template <typename IdT>
    void fix(IdT& id) {
        if (!id.valid() || !used.insert(id.value).second)
            id = song.newId<IdT>();
    }

  private:
    Song& song;
    std::set<uint64_t> used;
};

void normalizeAutomation(std::vector<AutomationEnvelope>& lanes, IdRepair& ids) {
    if (lanes.size() > size_t(kMaxPlayedLanes + kMaxKnobLanes))
        lanes.resize(kMaxPlayedLanes + kMaxKnobLanes);
    for (AutomationEnvelope& lane : lanes) {
        ids.fix(lane.id);
        clampValue(lane.outputIndex, 0, kMaxAutomationLanes - 1);
        clampValue(lane.recInput, -1, kMaxAutomationLanes - 1);
        clampValue(lane.slewMs, 0.f, 100.f);
        if (lane.targets.size() > 4)
            lane.targets.resize(4);
        for (ParamTarget& t : lane.targets) {
            clampValue(t.min, 0.f, 1.f);
            clampValue(t.max, 0.f, 1.f);
        }
        for (AutoPoint& p : lane.points) {
            clampValue(p.value, 0.f, 1.f);
            clampValue(p.tension, -1.f, 1.f);
        }
        std::stable_sort(lane.points.begin(), lane.points.end(),
                         [](const AutoPoint& a, const AutoPoint& b) { return a.t < b.t; });
    }
}

void normalizeClip(Clip& clip, TimeSig ts, IdRepair& ids) {
    if (clip.length < 1)
        clip.length = ticksPerBar(ts);
    clip.loopStart = std::max<Tick>(0, clip.loopStart);
    if (clip.loopEnd <= clip.loopStart)
        clip.loopEnd = clip.loopStart + clip.length;
    for (Note& n : clip.notes) {
        ids.fix(n.id);
        n.start = std::max<Tick>(0, n.start);
        n.length = std::max<Tick>(1, n.length);
        clampValue<int8_t>(n.pitch, 0, 127);
        clampValue(n.velocity, 0.f, 1.f);
        clampValue(n.releaseVelocity, 0.f, 1.f);
        clampValue(n.microOffset, -0.5f, 0.5f);
        clampValue<uint8_t>(n.channel, 1, kMaxNoteChannel);
        clampValue(n.ops.chance, 0.f, 1.f);
        clampValue<int8_t>(n.ops.repeats, -16, 16);
        clampValue<uint8_t>(n.ops.recurrenceLength, 1, 16);
        clampValue(n.ops.timingSpread, 0.f, 0.5f);
    }
    clip.sortNotes();
    normalizeAutomation(clip.automation, ids);
    if (clip.scaleOverride)
        clampValue<int8_t>(clip.scaleOverride->root, 0, 11);
}

void normalizeParams(std::vector<float>& params, const std::vector<devices::ParamInfo>& table) {
    // Patches from before a knob existed get its default; values from a broken file are clamped.
    for (size_t i = params.size(); i < table.size(); i++)
        params.push_back(devices::defaultNormalized(table[i]));
    params.resize(table.size());
    for (float& v : params)
        v = std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f;
}

void normalizeModulators(std::vector<Modulator>& mods, size_t limit, IdRepair& ids) {
    if (mods.size() > limit)
        mods.resize(limit);
    for (Modulator& m : mods) {
        ids.fix(m.id);
        m.type = ModulatorType(std::clamp(int(m.type), 0, kModulatorTypeCount - 1));
        normalizeParams(m.params, devices::modulatorInfo(m.type).params);
        if (m.steps.size() > size_t(kMaxSteps))
            m.steps.resize(kMaxSteps);
        for (float& v : m.steps)
            v = std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f;
        if (m.routes.size() > size_t(kMaxRoutes))
            m.routes.resize(kMaxRoutes);
        for (Modulation& r : m.routes)
            r.amount = std::isfinite(r.amount) ? std::clamp(r.amount, -1.f, 1.f) : 0.f;
    }
}

void normalizeDevices(Track& track, IdRepair& ids) {
    if (track.devices.size() > size_t(kMaxDevices))
        track.devices.resize(kMaxDevices);
    for (Device& d : track.devices) {
        ids.fix(d.id);
        d.type = DeviceType(std::clamp(int(d.type), 0, kDeviceTypeCount - 1));
        normalizeParams(d.params, devices::deviceInfo(d.type).params);
        normalizeModulators(d.modulators, kMaxDeviceModulators, ids);
    }
    normalizeModulators(track.modulators, kMaxTrackModulators, ids);
}

/// A knob takes at most kMaxModulationsPerKnob routes; later ones (in song order) go.
void limitModulationsPerKnob(Song& song) {
    std::vector<std::pair<ParamRef, int>> counts;
    forEachModulator(song, [&](Modulator& m, const ModulatorPlace&) {
        std::vector<Modulation> kept;
        for (const Modulation& r : m.routes) {
            if (r.kind != Modulation::Kind::Param) {
                kept.push_back(r);
                continue;
            }
            auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == r.target; });
            if (it == counts.end())
                it = counts.insert(counts.end(), {r.target, 0});
            if (++it->second <= kMaxModulationsPerKnob)
                kept.push_back(r);
        }
        m.routes = std::move(kept);
    });
}

void normalizeTrack(Track& track, const Song& song, IdRepair& ids) {
    ids.fix(track.id);
    clampValue(track.polyphony, kAutoVoices, kMaxVoices);
    clampValue(track.gateLengthScale, 0.f, 1.f);
    clampValue<int8_t>(track.transpose, -48, 48);
    clampValue<int8_t>(track.midiChannel, 0, 16);

    if (track.drumLanes.size() > size_t(kMaxDrumLanes))
        track.drumLanes.resize(kMaxDrumLanes);
    for (DrumLane& lane : track.drumLanes) {
        clampValue<int8_t>(lane.pitch, 0, 127);
        clampValue(lane.outputIndex, 0, kMaxDrumLanes - 1);
        lane.gateLengthMs = std::max(0.f, lane.gateLengthMs);
        lane.loopLength = std::max<Tick>(0, lane.loopLength);
        clampValue<Tick>(lane.stepLength, kPpq / 16, 4 * kPpq);
        clampValue(lane.chokeGroup, 0, kMaxChokeGroups);
    }

    track.slots.resize(song.scenes.size());
    for (ClipSlot& slot : track.slots)
        if (slot.clip && !song.findClip(*slot.clip))
            slot.clip.reset();

    auto& arr = track.arrangement;
    arr.erase(std::remove_if(arr.begin(), arr.end(), [&](const ArrangerClip& a) { return !song.findClip(a.clip); }),
              arr.end());
    for (ArrangerClip& a : arr) {
        ids.fix(a.id);
        a.position = std::max<Tick>(0, a.position);
        a.length = std::max<Tick>(1, a.length);
        a.offset = std::max<Tick>(0, a.offset);
        a.velocityScale = std::max(0.f, a.velocityScale);
        a.stretch = a.stretch > 0 ? std::clamp(a.stretch, kMinStretch, kMaxStretch) : 1.f;
    }
    std::stable_sort(arr.begin(), arr.end(), [](const ArrangerClip& a, const ArrangerClip& b) {
        return a.position != b.position ? a.position < b.position : a.id.value < b.id.value;
    });

    normalizeAutomation(track.automation, ids);
    normalizeDevices(track, ids);
}

/// Raises nextId above every ID in the song, before any repair hands out new ones.
void seeAllIds(Song& song, IdRepair& ids) {
    for (const auto& [key, clip] : song.clips) {
        ids.see(key.value);
        for (const Note& n : clip.notes)
            ids.see(n.id.value);
        for (const AutomationEnvelope& a : clip.automation)
            ids.see(a.id.value);
    }
    for (const Track& t : song.tracks) {
        ids.see(t.id.value);
        for (const ArrangerClip& a : t.arrangement)
            ids.see(a.id.value);
        for (const AutomationEnvelope& a : t.automation)
            ids.see(a.id.value);
    }
    for (const TrackGroup& g : song.groups)
        ids.see(g.id.value);
    forEachModulator(song, [&](const Modulator& m, const ModulatorPlace&) { ids.see(m.id.value); });
    for (const Track& t : song.tracks)
        for (const Device& d : t.devices)
            ids.see(d.id.value);
    for (const Marker& m : song.markers)
        ids.see(m.id.value);
}

} // namespace

void normalize(Song& song) {
    IdRepair ids(song);
    seeAllIds(song, ids);

    clampValue(song.timeSig.num, 1, 32);
    if (!validDenominator(song.timeSig.den))
        song.timeSig.den = 4;
    clampValue(song.swing, 0.f, 1.f);
    clampValue<int8_t>(song.scale.root, 0, 11);
    song.loopStart = std::max<Tick>(0, song.loopStart);
    if (song.loopEnd <= song.loopStart)
        song.loopEnd = song.loopStart + ticksPerBar(song.timeSig);

    for (auto& [key, clip] : song.clips) {
        clip.id = key; // the pool key is the truth, and keys are already unique
        ids.claim(key.value);
        normalizeClip(clip, song.timeSig, ids);
    }

    // Scenes and slots must line up: grow the scene list to the longest slot list.
    size_t scenes = song.scenes.size();
    for (const Track& t : song.tracks)
        scenes = std::max(scenes, t.slots.size());
    scenes = std::clamp<size_t>(scenes, 1, kMaxScenes);
    song.scenes.resize(scenes);

    if (song.tracks.size() > size_t(kMaxTracks))
        song.tracks.resize(kMaxTracks);
    if (song.tracks.empty())
        song.tracks.push_back(makeTrack(song, TrackType::Instrument, "Track 1"));
    for (Track& t : song.tracks)
        normalizeTrack(t, song, ids);

    for (TrackGroup& g : song.groups) {
        ids.fix(g.id);
        g.tracks.erase(
            std::remove_if(g.tracks.begin(), g.tracks.end(), [&](TrackId id) { return !song.findTrack(id); }),
            g.tracks.end());
    }
    normalizeModulators(song.modulators, kMaxSongModulators, ids);
    pruneRoutes(song);
    limitModulationsPerKnob(song);
    for (Marker& m : song.markers) {
        ids.fix(m.id);
        m.position = std::max<Tick>(0, m.position);
    }
    std::stable_sort(song.markers.begin(), song.markers.end(),
                     [](const Marker& a, const Marker& b) { return a.position < b.position; });
}

Track makeTrack(Song& song, TrackType type, const std::string& name) {
    Track t;
    t.id = song.newId<TrackId>();
    t.name = name;
    t.type = type;
    t.color = trackColorForIndex(int(song.tracks.size()));
    t.slots.resize(song.scenes.size());
    if (type == TrackType::Drum)
        t.drumLanes = drumKit(DrumKitPreset::Standard8);
    return t;
}

namespace {

Clip makeDemoClip(Song& song, uint32_t color) {
    constexpr Tick s16 = kPpq / 4;
    struct Step {
        int start, length, pitch;
        float velocity;
    };
    // Two bars in C minor: a bass line and arpeggio over Cm - Ab - G.
    const Step steps[] = {
        {0, 4, 48, 0.95f}, {0, 3, 60, 0.7f},  {0, 3, 63, 0.7f},  {0, 3, 67, 0.7f},  {4, 2, 55, 0.6f},
        {6, 2, 58, 0.55f}, {8, 4, 60, 0.85f}, {12, 2, 63, 0.6f}, {14, 2, 62, 0.5f}, {16, 4, 44, 0.95f},
        {16, 3, 56, 0.7f}, {16, 3, 60, 0.7f}, {16, 3, 63, 0.7f}, {20, 2, 51, 0.6f}, {22, 2, 55, 0.55f},
        {24, 4, 43, 0.9f}, {24, 3, 55, 0.7f}, {24, 3, 59, 0.7f}, {24, 3, 62, 0.7f}, {28, 2, 59, 0.6f},
        {30, 2, 62, 0.5f},
    };

    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.name = "Demo";
    clip.color = color;
    clip.length = 2 * ticksPerBar(song.timeSig);
    clip.loopStart = 0;
    clip.loopEnd = clip.length;
    for (const Step& s : steps) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = s.start * s16;
        n.length = s.length * s16;
        n.pitch = int8_t(s.pitch);
        n.velocity = s.velocity;
        clip.notes.push_back(n);
    }
    clip.sortNotes();
    return clip;
}

} // namespace

Song makeDefaultSong() {
    Song song;
    song.scenes.resize(kDefaultScenes);
    song.tracks.push_back(makeTrack(song, TrackType::Instrument, "Track 1"));
    song.tracks.push_back(makeTrack(song, TrackType::Instrument, "Track 2"));
    song.tracks.push_back(makeTrack(song, TrackType::Instrument, "Track 3"));
    song.tracks.push_back(makeTrack(song, TrackType::Drum, "Drums"));
    return song;
}

void addDemoClip(Song& song) {
    assert(!song.tracks.empty() && !song.tracks.front().slots.empty());
    Track& first = song.tracks.front();
    Clip demo = makeDemoClip(song, first.color);
    first.slots[0].clip = demo.id;
    song.clips.emplace(demo.id, std::move(demo));
}

} // namespace tessera
