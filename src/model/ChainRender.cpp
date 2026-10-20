#include "ChainRender.hpp"

#include "Arrangement.hpp"
#include "Devices.hpp"
#include "Modulators.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <unordered_map>

namespace tessera::chainrender {

namespace {

constexpr double kBlock = 16; // ticks between modulation updates (the engine's is about 1.3 at 120 BPM)

/// Collects what leaves the chain; an open note gets its end when its end message comes.
struct Collect : chain::Output {
    std::vector<Note> out;
    std::unordered_map<uint32_t, size_t> open;
    double now = 0;

    void on(const chain::ChainNote& n) override {
        Note o;
        o.start = n.start;
        o.end = n.end;
        o.pitch = n.pitch;
        o.velocity = n.velocity;
        o.release = n.release;
        o.source = static_cast<const tessera::Note*>(n.extras);
        o.pass = n.pass;
        o.origin = n.origin;
        o.key = n.key;
        o.channel = n.channel;
        if (n.open())
            open[n.id] = out.size();
        out.push_back(o);
    }
    void end(uint32_t id) override {
        auto it = open.find(id);
        if (it == open.end())
            return;
        out[it->second].end = std::max(now, out[it->second].start + 1);
        open.erase(it);
    }
};

struct Knobs {
    std::vector<float> base, value;
};

struct Running {
    const Modulator* modulator;
    mods::State state;
    bool followsNotes; // track and device modulators restart with the track's notes
};

} // namespace

std::vector<Note> run(const Song& song, const Track& track, const std::vector<Note>& notes, double until, double tempo,
                      const std::vector<Scale>& keys) {
    if (std::none_of(track.devices.begin(), track.devices.end(), [](const Device& d) { return d.on; }))
        return notes;

    // Every knob and setting the chain or its modulators can read, by owner.
    std::map<std::pair<int, uint64_t>, Knobs> knobs;
    auto own = [&knobs](ParamRef::Kind kind, uint64_t id, const std::vector<float>& params) {
        knobs[{int(kind), id}] = {params, params};
    };
    std::vector<Running> running;
    for (const Modulator& m : song.modulators) {
        own(ParamRef::Kind::Modulator, m.id.value, m.params);
        running.push_back({&m, mods::State(), false});
    }
    for (const Modulator& m : track.modulators) {
        own(ParamRef::Kind::Modulator, m.id.value, m.params);
        running.push_back({&m, mods::State(), true});
    }
    for (const Device& d : track.devices) {
        own(ParamRef::Kind::Device, d.id.value, d.params);
        for (const Modulator& m : d.modulators) {
            own(ParamRef::Kind::Modulator, m.id.value, m.params);
            running.push_back({&m, mods::State(), true});
        }
    }
    std::vector<chain::DeviceSlot> slots;
    for (const Device& d : track.devices)
        slots.push_back({d.type, d.on, d.affectDrumLanes, d.id.value,
                         knobs[{int(ParamRef::Kind::Device), d.id.value}].value.data()});

    Scale songKey = resolveScale(song.scale, song.customScales);
    chain::Context context;
    context.devices = slots.data();
    context.count = int(slots.size());
    context.drum = track.type == TrackType::Drum;
    context.songKey = &songKey;
    context.clipKeys = &keys;
    context.timeSig = song.timeSig;
    context.seed = song.seed;
    context.songOffset = 0; // the chain runs in song ticks here

    mods::Clock clock;
    clock.timeSig = song.timeSig;
    clock.seed = song.seed;
    clock.ticksPerSecond = std::max(1.0, tempo) * double(kPpq) / 60;
    clock.seconds = kBlock / clock.ticksPerSecond;

    auto c = std::make_unique<chain::Chain>();
    Collect sink;
    size_t next = 0;
    double t = notes.empty() ? 0 : std::floor(notes.front().start / kBlock) * kBlock;
    while (t < until) {
        // Knobs: base plus modulation, from the modulators' outputs of the block before (as the engine does).
        for (auto& [owner, k] : knobs)
            k.value = k.base;
        // Knob lanes set the base, as in the engine while the song plays: the track's lanes, then the clip's
        // under the playhead (Override, or Modulate around the track lane's value).
        std::vector<const float*> setByTrack;
        auto laneSets = [&](const AutomationEnvelope& lane, float v, bool modulate) {
            auto it = knobs.find({int(lane.paramTarget.kind), lane.paramTarget.owner});
            if (it == knobs.end() || lane.paramTarget.param >= it->second.value.size())
                return;
            float& value = it->second.value[lane.paramTarget.param];
            bool fromTrack = std::find(setByTrack.begin(), setByTrack.end(), &value) != setByTrack.end();
            value = modulate && fromTrack ? std::clamp(value + v - 0.5f, 0.f, 1.f) : v;
            if (!modulate)
                setByTrack.push_back(&value);
        };
        for (const AutomationEnvelope& lane : track.automation)
            if (lane.paramTarget.valid() && !lane.mute)
                laneSets(lane, evaluate(lane, t), false);
        if (const ArrangerClip* inst = instanceAt(track, Tick(t)); inst && !inst->muted)
            if (const Clip* clip = song.findClip(inst->clip))
                if (std::optional<double> ct = clipTimeAt(*clip, *inst, t))
                    for (const AutomationEnvelope& lane : clip->automation)
                        if (lane.paramTarget.valid() && !lane.mute)
                            laneSets(lane, evaluate(lane, *ct), lane.combine == CombineMode::Modulate);
        for (const Running& r : running)
            for (const Modulation& route : r.modulator->routes) {
                if (route.kind != Modulation::Kind::Param)
                    continue;
                auto it = knobs.find({int(route.target.kind), route.target.owner});
                if (it != knobs.end() && route.target.param < it->second.value.size())
                    it->second.value[route.target.param] += route.amount * r.state.out;
            }
        for (auto& [owner, k] : knobs)
            for (float& v : k.value)
                v = std::clamp(v, 0.f, 1.f);

        while (next < notes.size() && notes[next].start < t + kBlock) {
            const Note& n = notes[next++];
            chain::ChainNote cn;
            cn.start = n.start;
            cn.end = n.end;
            cn.pitch = int8_t(std::clamp(n.pitch, 0, 127));
            cn.velocity = n.velocity;
            cn.release = n.release;
            cn.id = c->newId();
            cn.key = n.key;
            cn.channel = n.channel;
            cn.extras = n.source;
            cn.origin = n.origin;
            cn.pass = n.pass;
            mods::NoteValues values;
            values.velocity = n.velocity;
            values.pitch = float(n.pitch) / 127.f;
            values.release = n.release;
            for (Running& r : running)
                if (r.followsNotes)
                    mods::trigger(r.state, n.start, n.end - n.start, values, clock);
            c->push(cn);
        }
        clock.songTick = t;
        for (Running& r : running) {
            const Modulator& m = *r.modulator;
            mods::evaluate(m.type, m.id.value, knobs[{int(ParamRef::Kind::Modulator), m.id.value}].value.data(),
                           r.state, clock, m.steps.data(), int(m.steps.size()));
        }
        sink.now = t + kBlock;
        c->run(t + kBlock, context, sink);
        t += kBlock;
        if (next >= notes.size() && c->idle())
            break;
    }
    // What still sounds at the end stops there; what would start after it never does.
    for (auto& [id, index] : sink.open)
        sink.out[index].end = std::max(sink.out[index].start + 1, until);
    std::vector<Note> out;
    for (Note& n : sink.out)
        if (n.start < until) {
            n.end = std::min(n.end, std::max(until, n.start + 1));
            out.push_back(n);
        }
    std::stable_sort(out.begin(), out.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

} // namespace tessera::chainrender
