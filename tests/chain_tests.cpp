#include "doctest.h"

#include "../src/engine/Engine.hpp"
#include "../src/model/AutomationEdits.hpp"
#include "../src/model/DeviceTypes.hpp"
#include "../src/model/Devices.hpp"
#include "../src/model/Modulators.hpp"

#include <algorithm>
#include <set>
#include <vector>

using namespace tessera;
using chain::ChainNote;

namespace {

/// Knob values for a device or modulator from display values ({index, value}), the rest at defaults.
template <typename Type>
std::vector<float> knobsOf(Type type, std::initializer_list<std::pair<int, float>> values) {
    std::vector<float> out;
    const devices::TypeInfo& info = [&]() -> const devices::TypeInfo& {
        if constexpr (std::is_same_v<Type, DeviceType>)
            return devices::deviceInfo(type);
        else
            return devices::modulatorInfo(type);
    }();
    for (const devices::ParamInfo& p : info.params)
        out.push_back(devices::defaultNormalized(p));
    for (const auto& [k, v] : values)
        out[size_t(k)] = devices::normalize(info.params[size_t(k)], v);
    return out;
}

struct Collect : chain::Output {
    std::vector<ChainNote> notes;
    std::vector<std::pair<uint32_t, double>> ends;
    double now = 0;
    void on(const ChainNote& n) override { notes.push_back(n); }
    void end(uint32_t id) override { ends.push_back({id, now}); }
};

/// A chain of devices with their knobs, run like the engine runs it.
struct Rig {
    std::vector<std::vector<float>> knobs;
    std::vector<chain::DeviceSlot> slots;
    Scale songKey{0, 0xAB5}; // C major
    std::vector<Scale> clipKeys;
    chain::Chain chain;
    Collect out;
    bool drum = false;
    uint32_t seed = 1;

    void add(DeviceType type, std::initializer_list<std::pair<int, float>> values = {}) {
        knobs.push_back(knobsOf(type, values));
        slots.push_back({type, true, false, 100 + slots.size(), nullptr});
    }
    void set(size_t device, int knob, float value) {
        knobs[device][size_t(knob)] = devices::normalize(*devices::paramInfo(slots[device].type, knob), value);
    }
    chain::Context context() {
        for (size_t i = 0; i < slots.size(); i++)
            slots[i].knobs = knobs[i].data();
        chain::Context c;
        c.devices = slots.data();
        c.count = int(slots.size());
        c.drum = drum;
        c.songKey = &songKey;
        c.clipKeys = &clipKeys;
        c.seed = seed;
        return c;
    }
    ChainNote note(double start, double length, int pitch, float velocity = 0.8f) {
        ChainNote n;
        n.start = n.origin = start;
        n.end = start + length;
        n.pitch = int8_t(pitch);
        n.velocity = velocity;
        n.id = chain.newId();
        return n;
    }
    void push(const ChainNote& n) { chain.push(n); }
    void run(double until) {
        out.now = until;
        chain::Context c = context();
        chain.run(until, c, out);
    }
    /// Runs one tick at a time, so ends are seen at the tick they are due.
    void runTicks(double from, double to) {
        for (double t = from + 1; t <= to; t++)
            run(t);
    }
};

int pitchThrough(Rig& rig, int pitch) {
    rig.out.notes.clear();
    rig.push(rig.note(0, 100, pitch));
    rig.run(1);
    return rig.out.notes.empty() ? -1 : rig.out.notes.back().pitch;
}

} // namespace

// ---- Devices --------------------------------------------------------------------------------------------------

TEST_CASE("Transpose moves by semitones, octaves and steps in the key") {
    Rig rig;
    rig.add(DeviceType::Transpose, {{chain::knob::kSemitones, 3}, {chain::knob::kOctaves, -1}});
    CHECK(pitchThrough(rig, 60) == 51);

    rig.set(0, chain::knob::kSemitones, 0);
    rig.set(0, chain::knob::kOctaves, 0);
    rig.set(0, chain::knob::kKeySteps, 2);
    CHECK(pitchThrough(rig, 60) == 64); // C -> E in C major
    CHECK(pitchThrough(rig, 64) == 67); // E -> G

    SUBCASE("a clip's own key wins") {
        rig.clipKeys.push_back(Scale(0, 0x5AD)); // C minor: C D Eb F G Ab Bb
        ChainNote n = rig.note(10, 100, 60);
        n.key = 1;
        rig.push(n);
        rig.run(11);
        REQUIRE(!rig.out.notes.empty());
        CHECK(rig.out.notes.back().pitch == 63);
    }
    SUBCASE("drum tracks keep their lanes unless asked") {
        rig.drum = true;
        CHECK(pitchThrough(rig, 36) == 36);
        rig.slots[0].affectDrums = true;
        CHECK(pitchThrough(rig, 36) != 36);
    }
    SUBCASE("pitches stay in MIDI range") {
        rig.set(0, chain::knob::kKeySteps, 0);
        rig.set(0, chain::knob::kOctaves, 4);
        CHECK(pitchThrough(rig, 120) == 127);
    }
}

TEST_CASE("Scale snaps notes into a key, or drops them") {
    Rig rig;
    rig.add(DeviceType::Scale, {{chain::knob::kKeyFrom, float(chain::KeyFrom::Song)}});
    CHECK(pitchThrough(rig, 60) == 60);
    CHECK(pitchThrough(rig, 61) == 60); // nearest, ties go down
    rig.set(0, chain::knob::kOffKey, float(chain::OffKey::Up));
    CHECK(pitchThrough(rig, 61) == 62);
    rig.set(0, chain::knob::kOffKey, float(chain::OffKey::Down));
    CHECK(pitchThrough(rig, 63) == 62);
    rig.set(0, chain::knob::kOffKey, float(chain::OffKey::Drop));
    CHECK(pitchThrough(rig, 61) == -1);
    CHECK(pitchThrough(rig, 62) == 62);

    SUBCASE("a fixed key") {
        int minor = 0;
        for (size_t i = 0; i < builtinScales().size(); i++)
            if (std::string(builtinScales()[i].id) == "minor")
                minor = int(i);
        rig.set(0, chain::knob::kKeyFrom, float(chain::KeyFrom::Fixed));
        rig.set(0, chain::knob::kRoot, 2); // D minor: D E F G A Bb C
        rig.set(0, chain::knob::kScale, float(minor));
        rig.set(0, chain::knob::kOffKey, float(chain::OffKey::Up));
        CHECK(pitchThrough(rig, 66) == 67); // F# -> G
        CHECK(pitchThrough(rig, 71) == 72); // B -> C
    }
    SUBCASE("no key, no change") {
        rig.songKey = Scale();
        CHECK(pitchThrough(rig, 61) == 61);
    }
}

TEST_CASE("Velocity curves, scales into a range and randomizes repeatably") {
    Rig rig;
    rig.add(DeviceType::Velocity, {{chain::knob::kMin, 20}, {chain::knob::kMax, 80}});
    auto through = [&](float v, double origin = 0, int64_t pass = 0) {
        rig.out.notes.clear();
        ChainNote n = rig.note(origin, 10, 60, v);
        n.pass = pass;
        rig.push(n);
        rig.run(origin + 1);
        return rig.out.notes.back().velocity;
    };
    CHECK(through(0.5f) == doctest::Approx(0.5f));
    CHECK(through(1) == doctest::Approx(0.8f));
    CHECK(through(0) == doctest::Approx(0.2f));

    rig.set(0, chain::knob::kMin, 0);
    rig.set(0, chain::knob::kMax, 100);
    rig.set(0, chain::knob::kCurve, 100);
    CHECK(through(0.125f) == doctest::Approx(0.5f)); // lifts soft notes
    rig.set(0, chain::knob::kCurve, -100);
    CHECK(through(0.5f) == doctest::Approx(0.125f));

    rig.set(0, chain::knob::kCurve, 0);
    rig.set(0, chain::knob::kRandom, 30);
    float a = through(0.5f, 0, 0);
    CHECK(a == through(0.5f, 0, 0)); // the same note on the same pass
    CHECK(std::abs(a - 0.5f) <= 0.3f + 1e-6f);
    bool varies = false;
    for (int pass = 1; pass < 8; pass++)
        varies |= through(0.5f, 0, pass) != a;
    CHECK(varies);
    rig.seed = 2;
    bool seedMatters = false;
    for (int pass = 0; pass < 8; pass++) {
        rig.seed = 1;
        float one = through(0.5f, 0, pass);
        rig.seed = 2;
        seedMatters |= through(0.5f, 0, pass) != one;
    }
    CHECK(seedMatters);
}

TEST_CASE("Note Length scales, fixes or joins note lengths") {
    Rig rig;
    rig.add(DeviceType::NoteLength,
            {{chain::knob::kMode, float(chain::LengthMode::Scale)}, {chain::knob::kScaleBy, 50}});
    rig.push(rig.note(0, 480, 60));
    rig.run(1);
    REQUIRE(rig.out.notes.size() == 1);
    CHECK(rig.out.notes[0].end == doctest::Approx(240));

    rig.set(0, chain::knob::kMode, float(chain::LengthMode::Fixed));
    rig.set(0, chain::knob::kLength, 5); // 1/4
    rig.push(rig.note(10, 100, 60));
    rig.run(11);
    CHECK(rig.out.notes.back().end == doctest::Approx(10 + kPpq));

    SUBCASE("open notes are scaled when they end") {
        rig.set(0, chain::knob::kMode, float(chain::LengthMode::Scale));
        rig.set(0, chain::knob::kScaleBy, 200);
        ChainNote n = rig.note(100, 0, 60);
        n.end = INFINITY;
        rig.push(n);
        rig.run(101);
        CHECK(rig.out.notes.back().open());
        rig.chain.end(n.id, 150, rig.out);
        rig.runTicks(101, 199);
        CHECK(rig.out.ends.empty());
        rig.run(201);
        REQUIRE(rig.out.ends.size() == 1);
        CHECK(rig.out.ends[0].first == n.id);
    }
    SUBCASE("a fixed length ignores the key release") {
        ChainNote n = rig.note(100, 0, 60);
        n.end = INFINITY;
        rig.push(n);
        rig.run(101);
        CHECK(rig.out.notes.back().end == doctest::Approx(100 + kPpq));
        rig.chain.end(n.id, 120, rig.out);
        rig.run(1000);
        CHECK(rig.out.ends.empty());
    }
    SUBCASE("turning the mode while a note is held can't strand it") {
        rig.set(0, chain::knob::kMode, float(chain::LengthMode::Scale));
        rig.set(0, chain::knob::kScaleBy, 100);
        ChainNote n = rig.note(100, 0, 60);
        n.end = INFINITY;
        rig.push(n);
        rig.run(101);
        rig.set(0, chain::knob::kMode, float(chain::LengthMode::Fixed));
        rig.chain.end(n.id, 150, rig.out);
        rig.run(151);
        CHECK(rig.out.ends.size() == 1);
    }
}

TEST_CASE("Note Length legato holds each note until the next one") {
    Rig rig;
    rig.add(DeviceType::NoteLength, {{chain::knob::kMode, float(chain::LengthMode::Legato)}});
    ChainNote a = rig.note(0, 100, 60);
    ChainNote b = rig.note(480, 100, 62);
    ChainNote c1 = rig.note(960, 100, 64);
    ChainNote c2 = rig.note(960, 100, 67);
    for (const ChainNote& n : {a, b, c1, c2})
        rig.push(n);
    rig.run(1);
    REQUIRE(rig.out.notes.size() == 1);
    CHECK(rig.out.notes[0].open());
    rig.run(481);
    REQUIRE(rig.out.ends.size() == 1);
    CHECK(rig.out.ends[0].first == a.id);
    rig.run(961);
    // A chord's notes hold together; only the earlier note ends.
    REQUIRE(rig.out.ends.size() == 2);
    CHECK(rig.out.ends[1].first == b.id);

    SUBCASE("the last notes still end, 4 bars on") {
        rig.run(960 + 4 * ticksPerBar(TimeSig{}) - 1);
        CHECK(rig.out.ends.size() == 2);
        rig.run(960 + 4 * ticksPerBar(TimeSig{}) + 1);
        CHECK(rig.out.ends.size() == 4);
    }
    SUBCASE("overlap ends notes a little after the next starts") {
        rig.set(0, chain::knob::kOverlap, 50);
        ChainNote d = rig.note(1440, 100, 65);
        rig.push(d);
        rig.run(1441);
        CHECK(rig.out.ends.size() == 2);
        rig.run(1440 + 121);
        CHECK(rig.out.ends.size() == 4);
    }
}

TEST_CASE("device order matters and bypassed devices pass notes through") {
    Rig rig;
    rig.add(DeviceType::Transpose, {{chain::knob::kSemitones, 1}});
    rig.add(DeviceType::Scale, {{chain::knob::kKeyFrom, float(chain::KeyFrom::Song)}});
    CHECK(pitchThrough(rig, 60) == 60); // 61, then snapped back down

    std::swap(rig.knobs[0], rig.knobs[1]);
    std::swap(rig.slots[0], rig.slots[1]);
    CHECK(pitchThrough(rig, 60) == 61); // in key, then moved out of it

    rig.slots[1].on = false;
    CHECK(pitchThrough(rig, 60) == 60);
}

TEST_CASE("a full chain drops new notes but never loses an end") {
    Rig rig;
    rig.add(DeviceType::Velocity);
    for (int i = 0; i < chain::kMaxMessages + 10; i++)
        rig.push(rig.note(1000 + i, 10, 60));
    CHECK(rig.chain.overflowCount() > 0);
    for (int i = 0; i < 40; i++)
        rig.chain.end(uint32_t(5000 + i), 2000, rig.out);
    // Ends that didn't fit went out at once; the rest come when due.
    rig.run(3000);
    CHECK(rig.out.ends.size() == 40);
}

// ---- Modulators -----------------------------------------------------------------------------------------------

TEST_CASE("a synced LFO follows the song position") {
    using namespace mods;
    std::vector<float> k = knobsOf(ModulatorType::Lfo, {{knob::kSync, 1}, {knob::kSyncedRate, 3}, {knob::kBipolar, 1}});
    State state;
    Clock clock;
    auto at = [&](double tick) {
        clock.songTick = tick;
        return evaluate(ModulatorType::Lfo, 7, k.data(), state, clock);
    };
    double bar = double(ticksPerBar(TimeSig{}));
    CHECK(at(0) == doctest::Approx(0).epsilon(1e-6));
    CHECK(at(bar / 4) == doctest::Approx(1));
    CHECK(at(bar / 2) == doctest::Approx(0).scale(1));
    CHECK(at(3 * bar / 4) == doctest::Approx(-1));
    CHECK(bipolar(ModulatorType::Lfo, k.data()));

    k[knob::kBipolar] = 0;
    CHECK(at(bar / 4) == doctest::Approx(1));
    CHECK(at(3 * bar / 4) == doctest::Approx(0));
    CHECK(at(0) == doctest::Approx(0.5));

    k = knobsOf(ModulatorType::Lfo, {{knob::kSync, 1}, {knob::kSyncedRate, 3}, {knob::kPhase, 90}});
    CHECK(at(0) == doctest::Approx(1));

    SUBCASE("restart each bar") {
        k = knobsOf(ModulatorType::Lfo, {{knob::kShape, float(Shape::SawUp)},
                                         {knob::kSync, 1},
                                         {knob::kSyncedRate, 2}, // 2 bars
                                         {knob::kRestart, float(Restart::EachBar)}});
        CHECK(at(bar * 1.25) == doctest::Approx(at(bar * 0.25)));
        CHECK(at(bar * 0.25) == doctest::Approx(-0.75));
    }
    SUBCASE("restart each note") {
        k = knobsOf(ModulatorType::Lfo, {{knob::kShape, float(Shape::SawUp)},
                                         {knob::kSync, 1},
                                         {knob::kSyncedRate, 3},
                                         {knob::kRestart, float(Restart::EachNote)}});
        trigger(state, 100, 50, NoteValues(), clock);
        CHECK(at(100) == doctest::Approx(-1));
    }
}

TEST_CASE("a free LFO runs in hertz, and its random shapes repeat by seed") {
    using namespace mods;
    std::vector<float> k = knobsOf(ModulatorType::Lfo, {{knob::kSync, 0}, {knob::kRate, 1}});
    State state;
    Clock clock;
    clock.seconds = 0.25;
    std::vector<float> outs;
    for (int i = 0; i < 5; i++)
        outs.push_back(evaluate(ModulatorType::Lfo, 7, k.data(), state, clock));
    CHECK(outs[0] == doctest::Approx(0).scale(1));
    CHECK(outs[1] == doctest::Approx(1));
    CHECK(outs[3] == doctest::Approx(-1));
    CHECK(outs[4] == doctest::Approx(0).scale(1));

    std::vector<float> sh = knobsOf(
        ModulatorType::Lfo, {{knob::kShape, float(Shape::SampleHold)}, {knob::kSync, 1}, {knob::kSyncedRate, 7}});
    auto cycles = [&](uint32_t seed) {
        State st;
        Clock c;
        c.seed = seed;
        std::vector<float> v;
        for (int i = 0; i < 8; i++) {
            c.songTick = i * kPpq / 4 + 10;
            v.push_back(evaluate(ModulatorType::Lfo, 7, sh.data(), st, c));
        }
        return v;
    };
    CHECK(cycles(1) == cycles(1));
    CHECK(cycles(1) != cycles(2));
    std::vector<float> v = cycles(1);
    CHECK(std::adjacent_find(v.begin(), v.end(), std::not_equal_to<float>()) != v.end());

    std::vector<float> macro = knobsOf(ModulatorType::Macro, {{knob::kValue, 30}});
    CHECK(evaluate(ModulatorType::Macro, 8, macro.data(), state, clock) == doctest::Approx(0.3f));
}

// ---- The engine -----------------------------------------------------------------------------------------------

namespace {

constexpr double kSampleRate = 48000;
constexpr double kBpm = 93.75; // 1/32 tick per sample, so sample numbers are exact
constexpr int64_t kSamplesPerTick = 32;

struct Spec {
    Tick start, length;
    int pitch;
    float velocity = 0.8f;
};

Song songWith(const std::vector<Spec>& notes, Tick loopLength = 4 * kPpq, int polyphony = 2) {
    Song song;
    Track track;
    track.id = song.newId<TrackId>();
    track.polyphony = polyphony;
    track.slots.resize(1);
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = loopLength;
    for (const Spec& s : notes) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = s.start;
        n.length = s.length;
        n.pitch = int8_t(s.pitch);
        n.velocity = s.velocity;
        clip.notes.push_back(n);
    }
    track.slots[0].clip = clip.id;
    song.clips.emplace(clip.id, clip);
    song.tracks.push_back(track);
    song.scenes.resize(1);
    return song;
}

Device& addDevice(Song& song, DeviceType type, std::initializer_list<std::pair<int, float>> values = {}) {
    Device d = devices::makeDevice(type, song.newId<DeviceId>());
    d.params = knobsOf(type, values);
    song.tracks[0].devices.push_back(d);
    return song.tracks[0].devices.back();
}

struct Harness {
    Song song;
    BusSlots slots;
    SnapshotBuilder builder;
    std::unique_ptr<Snapshot> snapshot, old;
    Engine engine;
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    int64_t sample = -1;

    explicit Harness(Song s, bool start = true) : song(std::move(s)) {
        slots.sync(song);
        snapshot = builder.build(song, slots, ChangeSet::all());
        engine.setSampleRate(kSampleRate);
        engine.setTempo(kBpm);
        engine.setSnapshot(snapshot.get());
        if (start)
            engine.launch({LaunchEvent::Kind::Scene, 0, 0, true});
    }
    /// Publishes the song again after an edit, as the module does.
    void republish() {
        slots.sync(song);
        old = std::move(snapshot);
        snapshot = builder.build(song, slots, ChangeSet::all());
        engine.setSnapshot(snapshot.get());
    }
    void step() {
        engine.process(frames.data(), events);
        sample++;
    }
    void runTo(int64_t target) {
        while (sample < target)
            step();
    }
    const TrackFrame& frame() const { return frames[size_t(slots.slotOf(song.tracks[0].id))]; }
    bool gate(int v = 0) const { return frame().gate[v] > 5.f; }
    int pitch(int v = 0) const { return int(std::lround(frame().voct[v] * 12)) + 60; }
    bool anyGate() const {
        for (int v = 0; v < frame().channels; v++)
            if (gate(v))
                return true;
        return false;
    }
};

} // namespace

TEST_CASE("engine: notes go through the chain on the same samples") {
    Song plain = songWith({{0, 240, 60}, {480, 240, 62}});
    Song song = plain;
    addDevice(song, DeviceType::Transpose, {{chain::knob::kSemitones, 7}});
    Harness h(song);

    h.step();
    CHECK(h.gate());
    CHECK(h.pitch() == 67);
    h.runTo(240 * kSamplesPerTick - 1);
    CHECK(h.gate());
    h.step();
    CHECK_FALSE(h.gate());
    h.runTo(480 * kSamplesPerTick - 1);
    CHECK_FALSE(h.anyGate());
    h.step();
    CHECK(h.anyGate());

    SUBCASE("a bypassed device changes nothing") {
        song.tracks[0].devices[0].on = false;
        Harness b(song);
        b.step();
        CHECK(b.pitch() == 60);
    }
}

TEST_CASE("engine: Note Length ends notes on exact samples") {
    Song song = songWith({{0, 480, 60}});
    addDevice(song, DeviceType::NoteLength,
              {{chain::knob::kMode, float(chain::LengthMode::Fixed)}, {chain::knob::kLength, 7}}); // 1/16
    Harness h(song);
    h.runTo(240 * kSamplesPerTick - 1);
    CHECK(h.gate());
    h.step();
    CHECK_FALSE(h.gate());
}

TEST_CASE("engine: legato holds until the next note, and nothing hangs") {
    Song song = songWith({{0, 100, 60}, {480, 100, 62}}, 4 * kPpq, 1);
    addDevice(song, DeviceType::NoteLength, {{chain::knob::kMode, float(chain::LengthMode::Legato)}});

    SUBCASE("held to the next note") {
        Harness h(song);
        h.runTo(480 * kSamplesPerTick - 1);
        CHECK(h.gate());
        CHECK(h.pitch() == 60);
        h.step();
        CHECK(h.pitch() == 62);
    }
    SUBCASE("stop releases everything") {
        Harness h(song);
        h.runTo(200 * kSamplesPerTick);
        CHECK(h.gate());
        h.engine.stop();
        h.step();
        CHECK_FALSE(h.anyGate());
    }
    SUBCASE("removing the device while a note is held ends it") {
        Harness h(song);
        h.runTo(200 * kSamplesPerTick);
        CHECK(h.gate());
        h.song.tracks[0].devices.clear();
        h.republish();
        h.step();
        CHECK_FALSE(h.gate());
    }
    SUBCASE("a knob edit leaves held notes alone") {
        Harness h(song);
        h.runTo(200 * kSamplesPerTick);
        h.song.tracks[0].devices[0].params[chain::knob::kOverlap] = 0.5f;
        h.republish();
        h.step();
        CHECK(h.gate());
    }
}

TEST_CASE("engine: an LFO on a knob moves the notes") {
    // Four notes, one per beat; a one-bar sine on Transpose: up at beat 2, down at beat 4.
    Song song = songWith({{0, 100, 60}, {960, 100, 60}, {1920, 100, 60}, {2880, 100, 60}}, 4 * kPpq, 1);
    Device& t = addDevice(song, DeviceType::Transpose);
    Modulator lfo = devices::makeModulator(ModulatorType::Lfo, song.newId<ModulatorId>());
    lfo.params = knobsOf(ModulatorType::Lfo, {{mods::knob::kSync, 1}, {mods::knob::kSyncedRate, 3}});
    Modulation route;
    route.target = {ParamRef::Kind::Device, t.id.value, chain::knob::kSemitones};
    route.amount = 0.125f; // 12 of the knob's 96 semitones
    lfo.routes.push_back(route);
    song.modulators.push_back(lfo);
    Harness h(song);

    int knob = h.snapshot->knobIndex(route.target);
    REQUIRE(knob >= 0);
    std::vector<int> pitches;
    float atPeak = 0;
    bool was = false;
    for (int64_t s = 0; s < 3840 * kSamplesPerTick; s++) {
        h.step();
        if (h.gate() && !was)
            pitches.push_back(h.pitch());
        was = h.gate();
        if (s == 960 * kSamplesPerTick)
            atPeak = h.engine.liveKnob(knob);
    }
    CHECK(pitches == std::vector<int>{60, 72, 60, 48});
    CHECK(atPeak == doctest::Approx(0.625f).epsilon(0.001));
}

TEST_CASE("engine: modulators drive AUTO outputs") {
    Song song = songWith({});
    Modulator macro = devices::makeModulator(ModulatorType::Macro, song.newId<ModulatorId>());
    macro.params = knobsOf(ModulatorType::Macro, {{mods::knob::kValue, 50}});
    Modulation out;
    out.kind = Modulation::Kind::AutoChannel;
    out.track = song.tracks[0].id;
    out.channel = 2;
    out.amount = 1;
    macro.routes.push_back(out);
    song.tracks[0].modulators.push_back(macro);
    Harness h(song);
    h.runTo(100);
    CHECK(h.frame().autoChannels == 3);
    CHECK(h.frame().automation[2] == doctest::Approx(5.f));
    CHECK(h.engine.automationValue(0, 2) == doctest::Approx(0.5f));
}

TEST_CASE("engine: a knob held by the user plays before the edit is committed") {
    Song song = songWith({{0, 100, 60}, {480, 100, 60}}, 4 * kPpq, 1);
    Device& t = addDevice(song, DeviceType::Transpose);
    ParamRef ref{ParamRef::Kind::Device, t.id.value, chain::knob::kSemitones};
    Harness h(song);
    h.step();
    CHECK(h.pitch() == 60);
    h.engine.setKnobOverride(ref, devices::normalize(*devices::paramInfo(DeviceType::Transpose, 0), 5));
    h.runTo(480 * kSamplesPerTick);
    CHECK(h.pitch() == 65);
    h.engine.clearKnobOverride();
    h.runTo(h.sample + 64);
    CHECK(h.engine.liveKnob(h.snapshot->knobIndex(ref)) ==
          doctest::Approx(devices::normalize(*devices::paramInfo(DeviceType::Transpose, 0), 5)));
}

TEST_CASE("engine: live notes go through the chain, also while stopped") {
    Song song = songWith({}, 4 * kPpq, 1);
    song.tracks[0].armed = true;
    song.tracks[0].monitor = true;
    addDevice(song, DeviceType::Transpose, {{chain::knob::kSemitones, 12}});
    Harness h(song, false);
    h.step();
    CHECK_FALSE(h.anyGate());

    h.engine.liveNote({LiveSource::Midi, 0, 60, 0.9f, true});
    h.step();
    CHECK(h.gate());
    CHECK(h.pitch() == 72);
    h.runTo(h.sample + 1000);
    CHECK(h.gate());
    h.engine.liveNote({LiveSource::Midi, 0, 60, 0, false});
    h.step();
    CHECK_FALSE(h.anyGate());

    SUBCASE("a fixed length ends a held key on time, transport stopped") {
        addDevice(h.song, DeviceType::NoteLength,
                  {{chain::knob::kMode, float(chain::LengthMode::Fixed)}, {chain::knob::kLength, 7}});
        h.republish();
        h.runTo(h.sample + 100); // past the last note's retrigger gap
        h.engine.liveNote({LiveSource::Midi, 0, 60, 0.9f, true});
        int64_t from = h.sample;
        h.step();
        CHECK(h.gate());
        h.runTo(from + 240 * kSamplesPerTick - 1);
        CHECK(h.gate());
        h.runTo(from + 240 * kSamplesPerTick + 2);
        CHECK_FALSE(h.gate());
        h.engine.liveNote({LiveSource::Midi, 0, 60, 0, false});
        h.step();
        CHECK_FALSE(h.anyGate());
    }
}

TEST_CASE("engine: device randomness repeats with the same seed") {
    Song song = songWith({{0, 100, 60}, {240, 100, 60}, {480, 100, 60}, {720, 100, 60}}, kPpq, 1);
    addDevice(song, DeviceType::Velocity, {{chain::knob::kRandom, 50}});
    auto velocities = [](const Song& s) {
        Harness h(s);
        std::vector<float> v;
        bool was = false;
        for (int64_t i = 0; i < 2 * kPpq * kSamplesPerTick; i++) {
            h.step();
            if (h.gate() && !was)
                v.push_back(h.frame().vel[0]);
            was = h.gate();
        }
        return v;
    };
    std::vector<float> a = velocities(song);
    CHECK(a.size() == 8);
    CHECK(a == velocities(song));
    // The second pass of the clip differs from the first.
    CHECK(std::vector<float>(a.begin(), a.begin() + 4) != std::vector<float>(a.begin() + 4, a.end()));
    song.seed = 99;
    CHECK(a != velocities(song));
}

// ---- The second set of devices --------------------------------------------------------------------------------

namespace {

int shapeIndex(const char* name) {
    const std::vector<ChordShapeInfo>& shapes = chordShapes();
    for (size_t i = 0; i < shapes.size(); i++)
        if (std::string(shapes[i].name) == name)
            return int(i);
    return 0;
}

std::vector<int> pitchesOf(const std::vector<ChainNote>& notes) {
    std::vector<int> out;
    for (const ChainNote& n : notes)
        out.push_back(n.pitch);
    return out;
}

} // namespace

TEST_CASE("Chord turns each note into a chord") {
    Rig rig;
    rig.add(DeviceType::Chord, {{chain::knob::kChord, float(shapeIndex("Major"))}, {chain::knob::kInKey, 0}});
    rig.push(rig.note(0, 480, 60));
    rig.run(1);
    CHECK(pitchesOf(rig.out.notes) == std::vector<int>{60, 64, 67});
    std::set<uint32_t> ids;
    for (const ChainNote& n : rig.out.notes) {
        ids.insert(n.id);
        CHECK(n.end == doctest::Approx(480));
    }
    CHECK(ids.size() == 3);

    SUBCASE("inversion and strum") {
        rig.out.notes.clear();
        rig.set(0, chain::knob::kInversion, 1);
        rig.set(0, chain::knob::kStrum, 50);
        rig.push(rig.note(1000, 480, 60));
        rig.runTicks(1000, 1300);
        REQUIRE(rig.out.notes.size() == 3);
        CHECK(pitchesOf(rig.out.notes) == std::vector<int>{64, 67, 72});
        CHECK(rig.out.notes[1].start == doctest::Approx(1000 + 120));
        CHECK(rig.out.notes[2].start == doctest::Approx(1000 + 240));
    }
    SUBCASE("a held key's chord ends with it") {
        rig.out.notes.clear();
        ChainNote held = rig.note(2000, 0, 62);
        held.end = INFINITY;
        rig.push(held);
        rig.run(2001);
        REQUIRE(rig.out.notes.size() == 3);
        rig.chain.end(held.id, 2100, rig.out);
        rig.run(2101);
        CHECK(rig.out.ends.size() == 3);
    }
    SUBCASE("in the key, a triad follows the key") {
        rig.out.notes.clear();
        rig.set(0, chain::knob::kChord, float(shapeIndex("Triad in key")));
        rig.push(rig.note(3000, 100, 62)); // D in C major: D minor
        rig.run(3001);
        CHECK(pitchesOf(rig.out.notes) == std::vector<int>{62, 65, 69});
    }
}

TEST_CASE("the arpeggiator steps through held notes on the grid") {
    Rig rig;
    rig.add(DeviceType::Arpeggiator, {{chain::knob::kRate, 7}, {chain::knob::kGate, 50}}); // 1/16 = 240 ticks
    for (int p : {67, 60, 64})
        rig.push(rig.note(0, 960, p));
    rig.runTicks(-1, 1200);
    CHECK(pitchesOf(rig.out.notes) == std::vector<int>{60, 64, 67, 60});
    REQUIRE(rig.out.notes.size() == 4);
    for (size_t i = 0; i < 4; i++) {
        CHECK(rig.out.notes[i].start == doctest::Approx(240.0 * double(i)));
        CHECK(rig.out.notes[i].end - rig.out.notes[i].start == doctest::Approx(120));
    }

    SUBCASE("patterns and octaves") {
        auto run = [](float pattern, int octaves) {
            Rig r;
            r.add(DeviceType::Arpeggiator, {{chain::knob::kRate, 7},
                                            {chain::knob::kPattern, pattern},
                                            {chain::knob::kArpOctaves, float(octaves)}});
            for (int p : {60, 64, 67})
                r.push(r.note(0, 240 * 8, p));
            r.runTicks(-1, 240 * 8 - 1);
            return pitchesOf(r.out.notes);
        };
        CHECK(run(float(chain::ArpPattern::Down), 1) == std::vector<int>{67, 64, 60, 67, 64, 60, 67, 64});
        CHECK(run(float(chain::ArpPattern::UpDown), 1) == std::vector<int>{60, 64, 67, 64, 60, 64, 67, 64});
        CHECK(run(float(chain::ArpPattern::Up), 2) == std::vector<int>{60, 64, 67, 72, 76, 79, 60, 64});
        CHECK(run(float(chain::ArpPattern::Chord), 1).size() == 24);
    }
    SUBCASE("off-grid notes wait for the next step; hold keeps playing after release") {
        Rig r;
        r.add(DeviceType::Arpeggiator, {{chain::knob::kRate, 7}, {chain::knob::kHold, 1}});
        r.push(r.note(100, 50, 60));
        r.runTicks(99, 1000);
        REQUIRE(!r.out.notes.empty());
        CHECK(r.out.notes[0].start == doctest::Approx(240));
        CHECK(r.out.notes.size() == 4); // 240, 480, 720, 960: the note was let go at 150
    }
}

TEST_CASE("Echo repeats notes, fading") {
    Rig rig;
    rig.add(
        DeviceType::Echo,
        {{chain::knob::kTime, 6}, {chain::knob::kRepeats, 3}, {chain::knob::kDecay, 50}, {chain::knob::kEchoPitch, 2}});
    rig.push(rig.note(0, 100, 60, 0.8f));
    rig.runTicks(-1, 2000);
    REQUIRE(rig.out.notes.size() == 4);
    for (size_t k = 0; k < 4; k++) {
        CHECK(rig.out.notes[k].start == doctest::Approx(480.0 * double(k)));
        CHECK(rig.out.notes[k].pitch == 60 + 2 * int(k));
        CHECK(rig.out.notes[k].velocity == doctest::Approx(0.8 * std::pow(0.5, double(k))));
    }
}

TEST_CASE("Random varies pitches repeatably and drops notes") {
    Rig rig;
    rig.add(DeviceType::Random, {{chain::knob::kRandomPitch, 2}});
    std::vector<ChainNote> first;
    for (int pass = 0; pass < 2; pass++) {
        rig.out.notes.clear();
        for (int i = 0; i < 8; i++) {
            ChainNote n = rig.note(i * 240, 100, 60);
            rig.push(n);
        }
        rig.runTicks(-1, 2400);
        if (pass == 0)
            first = rig.out.notes;
    }
    REQUIRE(first.size() == 8);
    bool moved = false;
    for (size_t i = 0; i < 8; i++) {
        CHECK(std::abs(first[i].pitch - 60) <= 4);                 // 2 steps in C major
        CHECK(first[i].start == doctest::Approx(double(i) * 240)); // timing is Humanize's now
        moved |= first[i].pitch != 60;
        CHECK(rig.out.notes[i].pitch == first[i].pitch); // the same notes on the same pass
    }
    CHECK(moved);
    rig.set(0, chain::knob::kDrop, 100);
    rig.out.notes.clear();
    rig.push(rig.note(5000, 100, 60));
    rig.run(5001);
    CHECK(rig.out.notes.empty());
}

TEST_CASE("Humanize loosens timing (only later), velocity and length, repeatably") {
    Rig rig;
    rig.add(
        DeviceType::Humanize,
        {{chain::knob::kHumanizeTiming, 50}, {chain::knob::kHumanizeVelocity, 30}, {chain::knob::kHumanizeLength, 50}});
    std::vector<ChainNote> first;
    for (int pass = 0; pass < 2; pass++) {
        rig.out.notes.clear();
        for (int i = 0; i < 8; i++)
            rig.push(rig.note(i * 240, 100, 60));
        rig.runTicks(-1, 2400);
        if (pass == 0)
            first = rig.out.notes;
    }
    REQUIRE(first.size() == 8);
    bool velocityMoved = false, lengthMoved = false;
    for (size_t i = 0; i < 8; i++) {
        CHECK(first[i].pitch == 60);
        CHECK(first[i].start >= double(i) * 240);
        CHECK(first[i].start <= double(i) * 240 + 120.001); // 50% of a 1/16
        double length = first[i].end - first[i].start;
        CHECK(length >= 50 - 0.001);
        CHECK(length <= 150 + 0.001);
        velocityMoved |= std::fabs(first[i].velocity - 0.8f) > 0.001f;
        lengthMoved |= std::fabs(length - 100) > 1;
        CHECK(rig.out.notes[i].start == first[i].start); // the same on the same pass
    }
    CHECK(velocityMoved);
    CHECK(lengthMoved);
}

TEST_CASE("Note Filter lets some notes through") {
    Rig rig;
    rig.add(DeviceType::NoteFilter,
            {{chain::knob::kLowest, 60}, {chain::knob::kHighest, 72}, {chain::knob::kEvery, 2}});
    for (int i = 0; i < 6; i++)
        rig.push(rig.note(i * 10, 5, 59 + i * 3)); // 59 (too low), 62, 65, 68, 71, 74 (too high)
    rig.run(100);
    CHECK(pitchesOf(rig.out.notes) == std::vector<int>{62, 68});
}

TEST_CASE("Note Filter keeps one channel, or every channel") {
    Rig rig;
    rig.add(DeviceType::NoteFilter, {{chain::knob::kChannel, 2}}); // the choice after Any: channel 2
    for (int i = 0; i < 4; i++) {
        ChainNote n = rig.note(i * 10, 5, 60 + i);
        n.channel = uint8_t(i % 2 + 1); // 1, 2, 1, 2
        rig.push(n);
    }
    rig.run(100);
    CHECK(pitchesOf(rig.out.notes) == std::vector<int>{61, 63});
    for (const ChainNote& n : rig.out.notes)
        CHECK(n.channel == 2);
}

TEST_CASE("Steps, Random, Ramp, Envelope and Note modulators") {
    using namespace mods;
    Clock clock;
    State state;
    double bar = double(ticksPerBar(TimeSig{}));
    SUBCASE("steps on the grid") {
        std::vector<float> k = knobsOf(ModulatorType::Steps, {{knob::kStepCount, 4}, {knob::kStepRate, 5}}); // 1/4
        float steps[] = {0.1f, 0.9f, 0.3f, 0.7f};
        std::vector<float> got;
        for (int i = 0; i < 6; i++) {
            clock.songTick = i * kPpq + 10;
            got.push_back(evaluate(ModulatorType::Steps, 1, k.data(), state, clock, steps, 4));
        }
        CHECK(got == std::vector<float>{0.1f, 0.9f, 0.3f, 0.7f, 0.1f, 0.9f});
    }
    SUBCASE("random by seed") {
        std::vector<float> k = knobsOf(ModulatorType::Random, {{knob::kRandomRate, 5}});
        clock.songTick = 10;
        float a = evaluate(ModulatorType::Random, 1, k.data(), state, clock);
        CHECK(a == evaluate(ModulatorType::Random, 1, k.data(), state, clock));
        CHECK(a >= 0);
        CHECK(a <= 1);
    }
    SUBCASE("ramp rises over its bars, then holds or loops") {
        std::vector<float> k = knobsOf(ModulatorType::Ramp, {{knob::kBars, 4}});
        clock.songTick = 2 * bar;
        CHECK(evaluate(ModulatorType::Ramp, 1, k.data(), state, clock) == doctest::Approx(0.5f));
        clock.songTick = 6 * bar;
        CHECK(evaluate(ModulatorType::Ramp, 1, k.data(), state, clock) == doctest::Approx(1));
        k = knobsOf(ModulatorType::Ramp, {{knob::kBars, 4}, {knob::kThen, 1}, {knob::kDirection, 1}});
        CHECK(evaluate(ModulatorType::Ramp, 1, k.data(), state, clock) == doctest::Approx(0.5f));
    }
    SUBCASE("envelope rises, holds and falls with the note") {
        std::vector<float> k =
            knobsOf(ModulatorType::Envelope,
                    {{knob::kAttack, 10}, {knob::kDecay, 10}, {knob::kSustain, 50}, {knob::kRelease, 10}});
        clock.seconds = 0.005;
        clock.ticksPerSecond = 1920; // 120 BPM: a 960-tick note lasts half a second
        NoteValues v;
        trigger(state, 0, 960, v, clock);
        std::vector<float> got;
        for (int i = 0; i < 120; i++)
            got.push_back(evaluate(ModulatorType::Envelope, 1, k.data(), state, clock));
        CHECK(got[1] == doctest::Approx(0.5f));    // halfway up after 5 ms
        CHECK(got[2] == doctest::Approx(1.0f));    // the top at 10 ms
        CHECK(got[50] == doctest::Approx(0.5f));   // sustaining
        CHECK(got[101] == doctest::Approx(0.25f)); // half released 5 ms after the note's end
        CHECK(got[110] == doctest::Approx(0));
    }
    SUBCASE("note follows the last note") {
        std::vector<float> k = knobsOf(ModulatorType::Note, {{knob::kFollows, 0}});
        clock.seconds = 0.1;
        NoteValues v;
        v.velocity = 0.7f;
        trigger(state, 0, 100, v, clock);
        CHECK(evaluate(ModulatorType::Note, 1, k.data(), state, clock) == doctest::Approx(0.7f).epsilon(0.01));
    }
}

TEST_CASE("engine: a Chord device sounds the whole chord") {
    Song song = songWith({{0, 480, 60}}, 4 * kPpq, 4);
    addDevice(song, DeviceType::Chord, {{chain::knob::kChord, float(shapeIndex("Minor"))}, {chain::knob::kInKey, 0}});
    Harness h(song);
    h.step();
    std::multiset<int> sounding;
    for (int v = 0; v < 4; v++)
        if (h.gate(v))
            sounding.insert(h.pitch(v));
    CHECK(sounding == std::multiset<int>{60, 63, 67});
}

TEST_CASE("engine: an arpeggiator plays steps on the song's grid") {
    Song song = songWith({{0, 960, 60}, {0, 960, 64}, {0, 960, 67}}, 4 * kPpq, 2);
    addDevice(song, DeviceType::Arpeggiator, {{chain::knob::kRate, 7}, {chain::knob::kGate, 50}});
    Harness h(song);
    std::vector<std::pair<int64_t, int>> starts;
    std::array<bool, 2> was{};
    for (int64_t s = 0; s < 960 * kSamplesPerTick; s++) {
        h.step();
        for (int v = 0; v < 2; v++) {
            if (h.gate(v) && !was[size_t(v)])
                starts.push_back({h.sample, h.pitch(v)});
            was[size_t(v)] = h.gate(v);
        }
    }
    REQUIRE(starts.size() == 4);
    CHECK(starts[0] == std::make_pair(int64_t(0), 60));
    CHECK(starts[1] == std::make_pair(int64_t(240 * kSamplesPerTick), 64));
    CHECK(starts[2] == std::make_pair(int64_t(480 * kSamplesPerTick), 67));
}

TEST_CASE("engine: an arpeggiator's held notes let go of their curves when the snapshot is replaced") {
    Song song = songWith({{0, 960, 60}, {0, 960, 64}}, 4 * kPpq, 2);
    for (Note& n : song.clips.begin()->second.notes)
        n.pressureExpr = {{0, 0.2f, 0}, {1, 0.9f, 0}};
    addDevice(song, DeviceType::Arpeggiator, {{chain::knob::kRate, 7}, {chain::knob::kGate, 50}});
    Harness h(song);
    h.runTo(100 * kSamplesPerTick);
    // An edit while the arpeggiator holds the notes; the old snapshot is freed, as the module does.
    h.republish();
    h.old.reset();
    // Its next steps must not read the freed curves (the address sanitizer catches it), and they still play.
    int steps = 0;
    bool was = h.anyGate();
    while (h.sample < 900 * kSamplesPerTick) {
        h.step();
        steps += h.anyGate() && !was;
        was = h.anyGate();
    }
    CHECK(steps >= 2);
}

TEST_CASE("automatic voices count what the devices add") {
    Song song = songWith({{0, 480, 60}}, 4 * kPpq, kAutoVoices);
    CHECK(song.voicesOf(song.tracks[0]) == 1);
    addDevice(song, DeviceType::Chord, {{chain::knob::kChord, float(shapeIndex("Major 7"))}, {chain::knob::kInKey, 0}});
    CHECK(song.voicesOf(song.tracks[0]) == 4);
    song.tracks[0].devices[0].on = false;
    CHECK(song.voicesOf(song.tracks[0]) == 1);
    song.tracks[0].devices[0].on = true;
    addDevice(song, DeviceType::Arpeggiator);
    CHECK(song.voicesOf(song.tracks[0]) == 1); // one step at a time

    Harness h([] {
        Song s = songWith({{0, 480, 60}}, 4 * kPpq, kAutoVoices);
        addDevice(s, DeviceType::Chord, {{chain::knob::kChord, float(shapeIndex("Minor"))}, {chain::knob::kInKey, 0}});
        return s;
    }());
    h.step();
    CHECK(h.frame().channels == 3);
    int gates = 0;
    for (int v = 0; v < 3; v++)
        gates += h.gate(v);
    CHECK(gates == 3);
}

namespace {

/// A lane on Transpose's semitones that holds each value until the next point.
AutomationEnvelope semitoneLane(Song& song, const Device& d, std::initializer_list<std::pair<Tick, float>> points,
                                CombineMode combine = CombineMode::Override) {
    const devices::ParamInfo& info = *devices::paramInfo(DeviceType::Transpose, chain::knob::kSemitones);
    ParamRef ref{ParamRef::Kind::Device, d.id.value, chain::knob::kSemitones};
    AutomationEnvelope lane =
        makeKnobLane(song.newId<AutomationId>(), ref, "Transpose · Semitones", devices::normalize(info, 0), true, 0);
    lane.points.clear();
    for (auto [t, semitones] : points) {
        AutoPoint p;
        p.t = t;
        p.value = devices::normalize(info, semitones);
        p.curve = CurveType::Hold;
        lane.points.push_back(p);
    }
    lane.combine = combine;
    return lane;
}

std::vector<int> pitchesOver(Harness& h, int64_t samples) {
    std::vector<int> pitches;
    bool was = false;
    for (int64_t s = 0; s < samples; s++) {
        h.step();
        if (h.gate() && !was)
            pitches.push_back(h.pitch());
        was = h.gate();
    }
    return pitches;
}

} // namespace

TEST_CASE("knob lanes stay out of the AUTO channels") {
    Song song = songWith({});
    Device& d = addDevice(song, DeviceType::Transpose);
    Track& t = song.tracks[0];
    t.automation.push_back(semitoneLane(song, d, {{0, 3}}));
    CHECK(freeOutput(t.automation) == 0); // the knob lane takes no channel
    t.automation.push_back(makeLane(song.newId<AutomationId>(), 0, 0));
    CHECK(freeOutput(t.automation) == 1);

    SUBCASE("they count apart from AUTO lanes") {
        for (int i = 1; i < kMaxAutomationLanes; i++)
            t.automation.push_back(makeLane(song.newId<AutomationId>(), i, 0));
        LaneOwner owner = LaneOwner::ofTrack(t.id);
        CHECK(addLane(song, owner, makeLane(song.newId<AutomationId>(), 0, 0)) == nullptr);
        CHECK(addLane(song, owner, semitoneLane(song, d, {{0, 1}})) != nullptr);
    }
    SUBCASE("the snapshot plays them as knob lanes") {
        Harness h(song);
        const TrackPlayback& tp = *h.snapshot->tracks[0];
        CHECK(tp.lanes.size() == 1);
        CHECK(tp.knobLanes.size() == 1);
        CHECK(tp.autoChannels == 1);
        ParamRef ref{ParamRef::Kind::Device, d.id.value, chain::knob::kSemitones};
        CHECK(h.snapshot->laneKnobs == std::vector<int>{h.snapshot->knobIndex(ref)});
        CHECK(lanesTurning(song, ref).size() == 1);
    }
}

TEST_CASE("engine: a knob lane sets the knob while the song plays") {
    // A note on every beat; the lane goes to +7 at beat 3.
    Song song = songWith({{0, 100, 60}, {960, 100, 60}, {1920, 100, 60}, {2880, 100, 60}}, 4 * kPpq, 1);
    Device& d = addDevice(song, DeviceType::Transpose);
    ParamRef ref{ParamRef::Kind::Device, d.id.value, chain::knob::kSemitones};
    const devices::ParamInfo& info = *devices::paramInfo(DeviceType::Transpose, chain::knob::kSemitones);
    song.tracks[0].automation.push_back(semitoneLane(song, d, {{0, 0}, {1920, 7}}));

    SUBCASE("the notes follow the lane") {
        Harness h(song);
        CHECK(pitchesOver(h, 3840 * kSamplesPerTick) == std::vector<int>{60, 60, 67, 67});
    }
    SUBCASE("modulation adds on top of the lane") {
        Modulator macro = devices::makeModulator(ModulatorType::Macro, song.newId<ModulatorId>());
        macro.params = knobsOf(ModulatorType::Macro, {{mods::knob::kValue, 100}});
        Modulation route;
        route.target = ref;
        route.amount = devices::normalize(info, 12) - devices::normalize(info, 0); // +12 st at full
        macro.routes.push_back(route);
        song.modulators.push_back(macro);
        Harness h(song);
        // Modulation uses the modulators' outputs of the block before, so the very first note has none yet.
        CHECK(pitchesOver(h, 3840 * kSamplesPerTick) == std::vector<int>{60, 72, 79, 79});
    }
    SUBCASE("a clip lane overrides the track's, or moves around it") {
        Clip& clip = song.clips.begin()->second;
        SUBCASE("override") {
            clip.automation.push_back(semitoneLane(song, d, {{0, 12}}));
            Harness h(song);
            CHECK(pitchesOver(h, 3840 * kSamplesPerTick) == std::vector<int>{72, 72, 72, 72});
        }
        SUBCASE("modulate") {
            // 0.5 is "no change"; +12 st above the middle adds 12 to the track lane's value.
            AutomationEnvelope lane = semitoneLane(song, d, {{0, 12}}, CombineMode::Modulate);
            lane.points[0].value = 0.5f + devices::normalize(info, 12) - devices::normalize(info, 0);
            clip.automation.push_back(lane);
            Harness h(song);
            CHECK(pitchesOver(h, 3840 * kSamplesPerTick) == std::vector<int>{72, 72, 79, 79});
        }
    }
    SUBCASE("a held knob wins while it is held") {
        Harness h(song);
        h.engine.setKnobOverride(ref, devices::normalize(info, 5));
        CHECK(pitchesOver(h, 3840 * kSamplesPerTick) == std::vector<int>{65, 65, 65, 65});
    }
    SUBCASE("stopped, the knob is its saved value again") {
        song.tracks[0].devices[0].params[chain::knob::kSemitones] = devices::normalize(info, 2);
        Harness h(song);
        h.runTo(2000 * kSamplesPerTick);
        int k = h.snapshot->knobIndex(ref);
        CHECK(h.engine.liveBase(k) == doctest::Approx(devices::normalize(info, 7)));
        h.engine.stop();
        h.runTo(h.sample + 200);
        CHECK(h.engine.liveBase(k) == doctest::Approx(devices::normalize(info, 2)));
    }
    SUBCASE("a muted lane leaves the knob alone") {
        song.tracks[0].automation[0].mute = true;
        Harness h(song);
        CHECK(pitchesOver(h, 3840 * kSamplesPerTick) == std::vector<int>{60, 60, 60, 60});
    }
}

// ---- Tracks listening to other tracks and to the external inputs ---------------------------------------------

namespace {

/// Adds a track that listens to the first one at `point`, with no clips of its own.
void addListener(Song& song, ListenSource point) {
    Track t;
    t.id = song.newId<TrackId>();
    t.polyphony = 1;
    t.slots.resize(1);
    t.listen = point;
    t.listenTrack = song.tracks[0].id;
    song.tracks.push_back(t);
}

const TrackFrame& frameOf(const Harness& h, size_t track) {
    return h.frames[size_t(h.slots.slotOf(h.song.tracks[track].id))];
}

int pitchOf(const TrackFrame& f) { return int(std::lround(f.voct[0] * 12)) + 60; }

} // namespace

TEST_CASE("engine: a track plays another track's notes, after or before its devices") {
    Song song = songWith({{0, 240, 60}}, 4 * kPpq, 1);
    addDevice(song, DeviceType::Transpose, {{chain::knob::kSemitones, 12}});

    SUBCASE("after its devices: what the other track plays") {
        addListener(song, ListenSource::TrackAfter);
        Harness h(song);
        h.runTo(100);
        CHECK(frameOf(h, 1).gate[0] > 5.f);
        CHECK(pitchOf(frameOf(h, 1)) == 72);
        h.runTo(300 * kSamplesPerTick);
        CHECK_FALSE(frameOf(h, 1).gate[0] > 5.f); // and it ends with it
    }
    SUBCASE("before its devices: the clip as written") {
        addListener(song, ListenSource::TrackBefore);
        Harness h(song);
        h.runTo(100);
        CHECK(frameOf(h, 1).gate[0] > 5.f);
        CHECK(pitchOf(frameOf(h, 1)) == 60);
    }
    SUBCASE("a muted track still feeds the track that listens to it") {
        addListener(song, ListenSource::TrackAfter);
        song.tracks[0].mute = true;
        Harness h(song);
        h.runTo(100);
        CHECK_FALSE(frameOf(h, 0).gate[0] > 5.f);
        CHECK(frameOf(h, 1).gate[0] > 5.f);
        CHECK(pitchOf(frameOf(h, 1)) == 72);
    }
    SUBCASE("two tracks listening to each other don't loop forever") {
        addListener(song, ListenSource::TrackAfter);
        song.tracks[0].listen = ListenSource::TrackAfter;
        song.tracks[0].listenTrack = song.tracks[1].id;
        Harness h(song);
        h.runTo(100);
        CHECK(frameOf(h, 0).gate[0] > 5.f); // its own clip still plays; the loop just listens to nothing
        h.runTo(4 * kPpq * kSamplesPerTick);
        CHECK_FALSE(frameOf(h, 1).gate[0] > 5.f);
    }
}

TEST_CASE("engine: a track listening to another plays the song loop's start on every pass") {
    // The listener comes after its source in the track list: on the sample the loop wraps it is scheduled after the
    // source has handed it the loop start note. At the harness tempo the wrap falls on a sample's end, which hid this.
    Song song = songWith({{0, kPpq, 60}, {2 * kPpq, kPpq, 62}}, 4 * kPpq, 1);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = song.clips.begin()->first;
    a.length = 4 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    song.loopStart = 0;
    song.loopEnd = 4 * kPpq;
    for (ListenSource point : {ListenSource::TrackAfter, ListenSource::TrackBefore}) {
        CAPTURE(int(point));
        Song s = song;
        addListener(s, point);
        Harness h(s, false);
        h.engine.setTempo(120);
        h.engine.setPlayMode(PlayMode::Arranger);
        h.engine.setLoopEnabled(true);
        h.engine.reset();
        h.engine.play();
        std::vector<int> notesPerPass(4, 0);
        size_t pass = 0;
        bool was = false;
        while (pass < notesPerPass.size()) {
            h.step();
            pass += h.events.loop;
            bool now = frameOf(h, 1).gate[0] > 5.f;
            if (now && !was && pass < notesPerPass.size())
                notesPerPass[pass]++;
            was = now;
        }
        CHECK(notesPerPass == std::vector<int>{2, 2, 2, 2});
    }
}

TEST_CASE("engine: a track listening to the external inputs plays them without arming") {
    Song song = songWith({}, 4 * kPpq, 1);
    Harness h(song, false);
    h.engine.liveNote({LiveSource::Midi, 0, 60, 0.9f, true});
    h.step();
    CHECK_FALSE(h.gate()); // neither armed nor listening
    h.engine.liveNote({LiveSource::Midi, 0, 60, 0, false});

    h.song.tracks[0].listen = ListenSource::External;
    h.republish();
    h.step();
    h.engine.liveNote({LiveSource::Midi, 0, 62, 0.9f, true});
    h.step();
    CHECK(h.gate());
    CHECK(h.pitch() == 62);
    h.engine.liveNote({LiveSource::Midi, 0, 62, 0, false});
    h.step();
    CHECK_FALSE(h.anyGate());
}

TEST_CASE("engine: a launched clip restarts from a chosen point (seeking in the piano roll)") {
    Song song = songWith({{0, 240, 60}, {1920, 240, 67}}, 4 * kPpq, 1);
    Harness h(song);
    h.runTo(100);
    CHECK(h.pitch() == 60);
    LaunchEvent seek;
    seek.kind = LaunchEvent::Kind::Slot;
    seek.track = h.song.tracks[0].id.value;
    seek.index = 0;
    seek.immediate = true;
    seek.clipTime = 1900;
    h.engine.launch(seek);
    h.step();
    CHECK(h.engine.clipPosition(0) == doctest::Approx(1900).epsilon(0.001));
    CHECK_FALSE(h.gate()); // the note that was playing let go
    h.runTo(h.sample + 25 * kSamplesPerTick);
    CHECK(h.gate());
    CHECK(h.pitch() == 67); // and the clip carries on from there
}
