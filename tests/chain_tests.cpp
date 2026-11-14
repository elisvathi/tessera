#include "doctest.h"

#include "../src/engine/Engine.hpp"
#include "../src/model/AutomationEdits.hpp"
#include "../src/model/DeviceTypes.hpp"
#include "../src/model/Devices.hpp"
#include "../src/model/Modulators.hpp"
#include "../src/model/Render.hpp"

#include <algorithm>
#include <set>
#include <tuple>
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

/// A playhead setting's saved value for the choice named `name` ("2x", "1/16").
float choiceKnob(int k, const char* name) {
    const devices::ParamInfo& info = devices::playheadInfo().params[size_t(k)];
    int index = playheads::choiceIndex(info.choices, name, -1);
    REQUIRE(index >= 0);
    return devices::normalize(info, float(index));
}

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
    // On the heap: an Engine is over a megabyte, and a test's subcases keep several harnesses in one stack frame.
    std::unique_ptr<Engine> enginePtr = std::make_unique<Engine>();
    Engine& engine = *enginePtr;
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
    route.amount = 0.25f; // 12 of the knob's 48 semitones
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
    CHECK(atPeak == doctest::Approx(0.75f).epsilon(0.001));
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
        // The ends twice at each turn: a round of 6 with three notes.
        CHECK(run(float(chain::ArpPattern::UpDownFull), 1) == std::vector<int>{60, 64, 67, 67, 64, 60, 60, 64});
        CHECK(run(float(chain::ArpPattern::DownUpFull), 1) == std::vector<int>{67, 64, 60, 60, 64, 67, 67, 64});
        // Outside in: C G E; over two octaves the whole run, C G' E E' G C'.
        CHECK(run(float(chain::ArpPattern::Converge), 1) == std::vector<int>{60, 67, 64, 60, 67, 64, 60, 67});
        CHECK(run(float(chain::ArpPattern::Converge), 2) == std::vector<int>{60, 79, 64, 76, 67, 72, 60, 79});
        // Middle out, the lower first: E C G; with six notes G C' E E' C G'.
        CHECK(run(float(chain::ArpPattern::Diverge), 1) == std::vector<int>{64, 60, 67, 64, 60, 67, 64, 60});
        CHECK(run(float(chain::ArpPattern::Diverge), 2) == std::vector<int>{67, 72, 64, 76, 60, 79, 67, 72});
        // Each note once per round, the same order every round and every time.
        std::vector<int> shuffled = run(float(chain::ArpPattern::Shuffled), 1);
        REQUIRE(shuffled.size() == 8);
        CHECK(std::multiset<int>(shuffled.begin(), shuffled.begin() + 3) == std::multiset<int>{60, 64, 67});
        CHECK(std::equal(shuffled.begin(), shuffled.begin() + 5, shuffled.begin() + 3));
        CHECK(run(float(chain::ArpPattern::Shuffled), 1) == shuffled);
        // Walk starts on the lowest note and moves at most one note a step, the same path every time.
        std::vector<int> walk = run(float(chain::ArpPattern::Walk), 2);
        const std::vector<int> run6 = {60, 64, 67, 72, 76, 79};
        REQUIRE(walk.size() == 8);
        CHECK(walk[0] == 60);
        for (size_t i = 1; i < walk.size(); i++) {
            int from = int(std::find(run6.begin(), run6.end(), walk[i - 1]) - run6.begin());
            int to = int(std::find(run6.begin(), run6.end(), walk[i]) - run6.begin());
            int move = (to - from + 6) % 6;
            CHECK((move == 0 || move == 1 || move == 5)); // the same, the next, or back one (wrapping)
        }
        CHECK(run(float(chain::ArpPattern::Walk), 2) == walk);
        CHECK(walk != std::vector<int>{60, 64, 67, 72, 76, 79, 60, 64}); // it wanders, unlike Up
        // Pattern: a fixed line of held notes, the same every time.
        std::vector<int> line = run(float(chain::ArpPattern::Pattern), 1);
        REQUIRE(line.size() == 8);
        CHECK(line == run(float(chain::ArpPattern::Pattern), 1));
        for (int p : line)
            CHECK((p == 60 || p == 64 || p == 67));
        std::vector<int> wide = run(float(chain::ArpPattern::Shuffled), 4);
        CHECK(std::set<int>(wide.begin(), wide.end()).size() == 8); // no repeats inside a round of 12
    }
    SUBCASE("a new chord starts the pattern again; one voice moving inside it doesn't") {
        auto run = [](bool hold, const std::vector<std::tuple<double, double, int>>& notes) {
            Rig r;
            r.add(DeviceType::Arpeggiator, {{chain::knob::kRate, 7}, {chain::knob::kHold, hold ? 1.f : 0.f}});
            for (auto [start, length, pitch] : notes)
                r.push(r.note(start, length, pitch));
            r.runTicks(-1, 1900);
            return pitchesOf(r.out.notes);
        };
        // Held on through a gap: the latched C E G plays on, then D F A starts from its lowest note.
        CHECK(
            run(true, {{0, 900, 60}, {0, 900, 64}, {0, 900, 67}, {1000, 900, 62}, {1000, 900, 65}, {1000, 900, 69}}) ==
            std::vector<int>{60, 64, 67, 60, 64, 62, 65, 69});
        // E moves to F under a held C and G: the pattern carries on.
        CHECK(run(false, {{0, 1920, 60}, {0, 900, 64}, {0, 1920, 67}, {900, 1020, 65}}) ==
              std::vector<int>{60, 64, 67, 60, 65, 67, 60, 65});
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

TEST_CASE("Note Filter keeps a set of channels, or every channel") {
    SUBCASE("channels 1 and 3") {
        Rig rig;
        rig.add(DeviceType::NoteFilter, {{chain::knob::kChannel, 0b101}});
        for (int i = 0; i < 4; i++) {
            ChainNote n = rig.note(i * 10, 5, 60 + i);
            n.channel = uint8_t(i + 1); // 1, 2, 3, 4
            rig.push(n);
        }
        rig.run(100);
        CHECK(pitchesOf(rig.out.notes) == std::vector<int>{60, 62});
    }
    SUBCASE("none picked: any channel") {
        Rig rig;
        rig.add(DeviceType::NoteFilter, {{chain::knob::kChannel, 0}});
        for (int i = 0; i < 4; i++) {
            ChainNote n = rig.note(i * 10, 5, 60 + i);
            n.channel = uint8_t(i * 5 + 1); // 1, 6, 11, 16
            rig.push(n);
        }
        rig.run(100);
        CHECK(pitchesOf(rig.out.notes) == std::vector<int>{60, 61, 62, 63});
    }
}

TEST_CASE("Note Filter keeps one channel") {
    Rig rig;
    rig.add(DeviceType::NoteFilter, {{chain::knob::kChannel, 0b10}}); // only channel 2
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

TEST_CASE("Groove swings its track's off-beats in place of the song's swing") {
    auto firstStartOfSecond = [](float songSwing, float groovePercent) {
        Song song = songWith({{0, 120, 60}, {240, 120, 62}}, 4 * kPpq, kAutoVoices);
        song.swing = songSwing;
        if (groovePercent > 0)
            addDevice(song, DeviceType::Groove, {{chain::knob::kGrooveSwing, groovePercent}});
        Harness h(song);
        for (int64_t s = 0; s < 600 * kSamplesPerTick; s++) {
            h.step();
            if (h.gate() && h.pitch() == 62)
                return double(s) / kSamplesPerTick;
        }
        return -1.0;
    };
    CHECK(firstStartOfSecond(0, 75) == doctest::Approx(360).epsilon(0.01)); // half a 1/16 late
    CHECK(firstStartOfSecond(1, 50) == doctest::Approx(240).epsilon(0.01)); // straight, though the song swings
    CHECK(firstStartOfSecond(1, 0) == doctest::Approx(360).epsilon(0.01));  // no Groove: the song's swing
}

TEST_CASE("engine: song swing plays on the timeline too, and an off-beat 1/8 swings only with the 1/8 grid") {
    auto startOf = [](Tick at, Tick step, bool timeline) {
        Song song = songWith({{0, 120, 60}, {at, 120, 62}}, 4 * kPpq, kAutoVoices);
        song.swing = 1;
        song.swingStep = step;
        if (timeline) {
            ArrangerClip a;
            a.id = song.newId<ArrangementId>();
            a.clip = song.clips.begin()->first;
            a.length = 4 * kPpq;
            song.tracks[0].arrangement.push_back(a);
        }
        Harness h(song, !timeline);
        if (timeline) {
            h.engine.setPlayMode(PlayMode::Arranger);
            h.engine.reset();
            h.engine.play();
        }
        for (int64_t s = 0; s < 1200 * kSamplesPerTick; s++) {
            h.step();
            if (h.gate() && h.pitch() == 62)
                return double(s) / kSamplesPerTick;
        }
        return -1.0;
    };
    CHECK(startOf(240, kPpq / 4, true) == doctest::Approx(360).epsilon(0.01));
    CHECK(startOf(480, kPpq / 4, false) == doctest::Approx(480).epsilon(0.01)); // an off-beat 1/8 doesn't move on 1/16s
    CHECK(startOf(480, kPpq / 2, false) == doctest::Approx(720).epsilon(0.01)); // it does on 1/8s
}

TEST_CASE("engine: playheads read the clip each from its own segment") {
    Song song = songWith({{0, 240, 60}, {960, 240, 64}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    const auto& info = devices::playheadInfo();
    for (int i = 0; i < 2; i++) {
        Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), i);
        if (i == 1) { // a bar long, from beat 2
            p.params[playheads::knob::kRegionStart] = devices::normalize(info.params[playheads::knob::kRegionStart], 4);
            p.params[playheads::knob::kRegionLength] =
                devices::normalize(info.params[playheads::knob::kRegionLength], 16);
        }
        d.playheads.push_back(p);
    }
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    auto sounding = [&h] {
        std::multiset<int> out;
        for (int v = 0; v < h.frame().channels; v++)
            if (h.gate(v))
                out.insert(h.pitch(v));
        return out;
    };

    h.step();
    CHECK(sounding() == std::multiset<int>{60, 64}); // the second playhead starts a beat in, on the 64
    for (int v = 0; v < h.frame().channels; v++)
        if (h.gate(v))
            CHECK(int(h.frame().part[v]) == (h.pitch(v) == 60 ? 1 : 2)); // each playhead's channel, for Track Outs
    h.runTo(960 * kSamplesPerTick + 1);
    CHECK(sounding() == std::multiset<int>{64}); // the first reaches the 64; the second is past the clip's notes

    SUBCASE("bypassed, the clip plays as usual") {
        h.song.tracks[0].devices[0].on = false;
        h.republish();
        h.runTo(4 * 960 * kSamplesPerTick + 1);
        CHECK(sounding() == std::multiset<int>{60});
    }
}

TEST_CASE("engine: a Curve speeds up a playhead's repeats, as MIDI export does") {
    // A note held over the bar, Repeat 1/16, Times 4, Curve Ease in (t cubed): the second hit of the beat comes at the
    // cube root of 1/4 of it, and the first note stops there (Retrigger).
    Song song = songWith({{0, 4 * 960, 60}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    const auto& info = devices::playheadInfo();
    Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
    const std::vector<std::string>& sizes = playheads::repeatNames();
    const std::vector<std::string>& curves = playheads::curveNames();
    p.params[playheads::knob::kRepeatSize] = devices::normalize(
        info.params[playheads::knob::kRepeatSize], float(std::find(sizes.begin(), sizes.end(), "1/16") - sizes.begin()));
    p.params[playheads::knob::kRepeatTimes] = devices::normalize(info.params[playheads::knob::kRepeatTimes], 4);
    p.params[playheads::knob::kCurve] = devices::normalize(
        info.params[playheads::knob::kCurve], float(std::find(curves.begin(), curves.end(), "Ease in") - curves.begin()));
    d.playheads.push_back(p);
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    h.step();
    REQUIRE(h.gate());
    int64_t s = 1;
    while (s < 960 * kSamplesPerTick && h.gate())
        h.step(), s++;
    CHECK(double(s) / kSamplesPerTick == doctest::Approx(960 * std::cbrt(0.25)).epsilon(0.01));
}

TEST_CASE("engine: a segment past the clip's end reads the notes written there, the piano roll is a canvas") {
    // A one-bar loop, and notes written in bar 2, outside it.
    Song song = songWith({{0, 240, 60}, {3840, 240, 67}, {4800, 240, 69}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
    const auto& info = devices::playheadInfo();
    p.params[playheads::knob::kRegionStart] = devices::normalize(info.params[playheads::knob::kRegionStart], 16);
    p.params[playheads::knob::kRegionLength] = devices::normalize(info.params[playheads::knob::kRegionLength], 16);
    d.playheads.push_back(p);
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    auto sounding = [&h] {
        std::multiset<int> out;
        for (int v = 0; v < h.frame().channels; v++)
            if (h.gate(v))
                out.insert(h.pitch(v));
        return out;
    };

    h.step();
    CHECK(sounding() == std::multiset<int>{67});
    h.runTo(960 * kSamplesPerTick + 1);
    CHECK(sounding() == std::multiset<int>{69});
    h.runTo(2 * 960 * kSamplesPerTick + 1);
    CHECK(sounding().empty()); // nothing written there: silence, not the clip again
}

TEST_CASE("engine: a playhead that jumps into a note plays it again, and cuts the one it was playing") {
    // One long note; Repeat 1/16 x4 reads each sixteenth four times, so in beat 2 it keeps jumping back into the note.
    const auto& info = devices::playheadInfo();
    auto run = [&](int onJump, int* most) {
        Song song = songWith({{0, 3840, 60}}, 4 * kPpq, kAutoVoices);
        Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
        Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
        p.params[playheads::knob::kRepeatSize] = choiceKnob(playheads::knob::kRepeatSize, "1/16");
        p.params[playheads::knob::kRepeatTimes] = devices::normalize(info.params[playheads::knob::kRepeatTimes], 4);
        p.params[playheads::knob::kOnJump] = devices::normalize(info.params[playheads::knob::kOnJump], float(onJump));
        d.playheads.push_back(p);
        song.tracks[0].devices.push_back(d);
        Harness h(song);
        // The notes that start in beat 2, counted by gates rising, and the most sounding at once.
        int starts = 0;
        *most = 0;
        std::vector<bool> was(16, false);
        for (int64_t s = 960 * kSamplesPerTick; s < 1920 * kSamplesPerTick; s += 4) {
            h.runTo(s);
            int now = 0;
            for (int v = 0; v < h.frame().channels && v < 16; v++) {
                if (h.gate(v) && !was[size_t(v)] && s > 960 * kSamplesPerTick)
                    starts++;
                was[size_t(v)] = h.gate(v);
                now += h.gate(v) ? 1 : 0;
            }
            *most = std::max(*most, now);
        }
        return starts;
    };
    int most = 0;
    CHECK(run(0, &most) == 4); // Retrigger: one at each sixteenth, including the move to the next sixteenth
    CHECK(most == 1);          // each repeat cuts the one before
    CHECK(run(1, &most) == 0); // Let ring: no note starts where it lands, so nothing new plays
    CHECK(most >= 1);          // the notes from beat 1 still ring
}

TEST_CASE("engine: a chord read by playheads keeps its whole length when one playhead echoes it") {
    // A three-note chord, half a bar long, read by two playheads; the first has an Echo in its own chain (1/8,
    // 3 repeats, gate 50%). Auto voices must hold the chord and every echo, or the echoes take the chord's voices
    // halfway through it.
    Song song = songWith({{0, 1920, 60}, {0, 1920, 64}, {0, 1920, 67}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    for (int i = 0; i < 2; i++) {
        Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), i);
        p.channel = uint8_t(i + 1);
        if (i == 0)
            p.branch.push_back(devices::makeDevice(DeviceType::Echo, song.newId<DeviceId>()));
        d.playheads.push_back(p);
    }
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    h.runTo(int64_t(1800) * kSamplesPerTick);
    int sounding = 0;
    for (int v = 0; v < h.frame().channels; v++)
        sounding += h.gate(v) ? 1 : 0;
    CHECK(sounding == 12); // both playheads' chords (6) and the two echoes still going (6)
}

TEST_CASE("engine: a playhead turned off stops its echoes still to come, and the other playhead plays on") {
    // A short note read by two playheads; the first echoes it (1/8, 3 repeats).
    Song song = songWith({{0, 200, 60}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    for (int i = 0; i < 2; i++) {
        Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), i);
        p.channel = uint8_t(i + 1);
        if (i == 0)
            p.branch.push_back(devices::makeDevice(DeviceType::Echo, song.newId<DeviceId>()));
        d.playheads.push_back(p);
    }
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    auto channelSounds = [&h](int channel) {
        for (int v = 0; v < h.frame().channels; v++)
            if (h.gate(v) && h.frame().part[v] == channel)
                return true;
        return false;
    };
    h.runTo(int64_t(100) * kSamplesPerTick);
    REQUIRE(channelSounds(1));
    REQUIRE(channelSounds(2));
    h.song.tracks[0].devices[0].playheads[0].params[playheads::knob::kOn] = 0;
    h.republish();
    h.step();
    CHECK_FALSE(channelSounds(1));
    CHECK(channelSounds(2));
    // Where the echoes would have come (every 1/8 for 3 repeats), it stays silent.
    bool echoed = false;
    while (h.sample < int64_t(4) * kPpq * kSamplesPerTick - 10) {
        h.step();
        echoed |= channelSounds(1);
    }
    CHECK_FALSE(echoed);
}

TEST_CASE("engine: one playhead running out of voices never takes another playhead's") {
    // Three voices per channel. Playhead 1 echoes its chord, far more than 3 voices; playhead 2 just plays it.
    Song song = songWith({{0, 1920, 60}, {0, 1920, 64}, {0, 1920, 67}}, 4 * kPpq, 3);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    for (int i = 0; i < 2; i++) {
        Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), i);
        p.channel = uint8_t(i + 1);
        if (i == 0)
            p.branch.push_back(devices::makeDevice(DeviceType::Echo, song.newId<DeviceId>()));
        d.playheads.push_back(p);
    }
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    h.step();
    REQUIRE(int(h.frame().channels) == 6);
    h.runTo(int64_t(1800) * kSamplesPerTick);
    int second = 0;
    for (int v = 0; v < h.frame().channels; v++)
        second += h.frame().part[v] == 2 && h.gate(v) ? 1 : 0;
    CHECK(second == 3); // playhead 2's chord, whole, while playhead 1's echoes fight over its own three
}

TEST_CASE("engine: muting or unmuting one playhead leaves the others' sounding notes alone") {
    // Three long notes read by three playheads, each on its own channel.
    Song song = songWith({{0, 3800, 60}, {0, 3800, 64}, {0, 3800, 67}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    for (int i = 0; i < 3; i++) {
        Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), i);
        p.channel = uint8_t(i + 1);
        d.playheads.push_back(p);
    }
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    // What each channel has sounding, by the channel the frame reports for its voices.
    auto sounding = [&h](int channel) {
        int n = 0;
        for (int v = 0; v < h.frame().channels; v++)
            n += h.gate(v) && h.frame().part[v] == channel ? 1 : 0;
        return n;
    };
    h.runTo(int64_t(1000) * kSamplesPerTick);
    REQUIRE(sounding(2) == 3);
    REQUIRE(sounding(3) == 3);

    REQUIRE(sounding(1) == 3);
    auto setOn = [&h](int k, bool on) {
        h.song.tracks[0].devices[0].playheads[size_t(k)].params[playheads::knob::kOn] = on ? 1.f : 0.f;
    };
    setOn(0, false);
    h.republish();
    h.step();
    CHECK(sounding(1) == 0); // its notes stop at once (Trello: "the current gate continues")
    h.runTo(int64_t(1200) * kSamplesPerTick);
    CHECK(sounding(1) == 0);
    CHECK(sounding(2) == 3);
    CHECK(sounding(3) == 3);

    setOn(0, true);
    h.republish();
    h.runTo(int64_t(1400) * kSamplesPerTick);
    CHECK(sounding(2) == 3);
    CHECK(sounding(3) == 3);

    // A playhead added in front shifts every block: the sounding notes move along with their channels.
    Playhead added = devices::makePlayhead(h.song.newId<PlayheadId>(), 3);
    added.channel = 4;
    auto& heads = h.song.tracks[0].devices[0].playheads;
    heads.insert(heads.begin(), added);
    h.republish();
    h.runTo(int64_t(1600) * kSamplesPerTick);
    CHECK(sounding(2) == 3);
    CHECK(sounding(3) == 3);

    // The second playhead off, then one more added in front: the third moves to the place the second had, and the
    // second (off) to the third's. Its notes follow it, so the off one's place stops nothing of the third's.
    setOn(2, false); // the second of the first three, behind the added one
    h.republish();
    h.step();
    CHECK(sounding(2) == 0);
    CHECK(sounding(3) == 3);
    Playhead another = devices::makePlayhead(h.song.newId<PlayheadId>(), 4);
    another.channel = 5;
    heads.insert(heads.begin(), another);
    h.republish();
    h.runTo(int64_t(1800) * kSamplesPerTick);
    CHECK(sounding(2) == 0);
    CHECK(sounding(3) == 3);
}

TEST_CASE("engine: a bass playhead over chords uses one voice; its Track Out sends just that one") {
    // Chords on every beat. Playhead 1 picks the highest note of each (a line); playhead 2 plays the chords.
    Song song = songWith({{0, 900, 60},
                          {0, 900, 64},
                          {0, 900, 67},
                          {960, 900, 62},
                          {960, 900, 65},
                          {960, 900, 69},
                          {1920, 900, 60},
                          {1920, 900, 64},
                          {1920, 900, 67}},
                         4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    for (int i = 0; i < 2; i++) {
        Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), i);
        p.channel = uint8_t(i + 1);
        if (i == 0)
            p.params[playheads::knob::kNotes] =
                devices::normalize(devices::playheadInfo().params[playheads::knob::kNotes], 2); // Highest
        d.playheads.push_back(p);
    }
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    auto used = [&h](int channel) {
        int n = 0;
        for (int v = 0; v < h.frame().channels; v++)
            n += h.frame().part[v] == channel && h.frame().used[v] ? 1 : 0;
        return n;
    };
    h.runTo(int64_t(3000) * kSamplesPerTick);
    REQUIRE(h.frame().byChannel);
    CHECK(used(1) == 1); // a line of single notes: always its first voice
    CHECK(used(2) == 3); // the chords: three
    h.engine.stop();
    h.step();
    CHECK(used(2) == 1); // stopped: each channel back to one until it needs more
}

TEST_CASE("engine: a playhead's line of back-to-back notes stays on one voice, however long the engine ran first") {
    // Each note ends where the next starts. The voice's end is on the engine's clock, which keeps running while
    // stopped, and the next start is on the play clock: the two can round a hair apart.
    Song song =
        songWith({{0, 1920, 55}, {1920, 1920, 55}, {3840, 1920, 57}, {5760, 1920, 59}, {7680, 1920, 52}}, 12 * kPpq, 8);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
    p.channel = 1;
    p.params[playheads::knob::kRate] = choiceKnob(playheads::knob::kRate, "2x");
    p.params[playheads::knob::kRegionLength] =
        devices::normalize(devices::playheadInfo().params[playheads::knob::kRegionLength], 32); // steps
    d.playheads.push_back(p);
    song.tracks[0].devices.push_back(d);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = song.clips.begin()->first;
    a.length = 48 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    song.loopStart = 0;
    song.loopEnd = 48 * kPpq;
    for (int64_t idle : {0, 1000, 44100, 100000, 220500, 333333}) {
        CAPTURE(idle);
        Harness h(song, false);
        h.engine.setSampleRate(44100);
        h.engine.setTempo(120);
        h.engine.setPlayMode(PlayMode::Arranger);
        h.engine.setLoopEnabled(true);
        h.runTo(idle);
        h.engine.reset();
        h.engine.play();
        int second = 0;
        for (int64_t s = 0; s < 44100 * 20; s++) {
            h.step();
            for (int v = 1; v < h.frame().channels; v++)
                second += h.frame().part[v] == 1 && h.gate(v) ? 1 : 0;
        }
        CHECK(second == 0);
    }
}

TEST_CASE("engine: playheads read the timeline's clips too, from each clip's start") {
    Song song = songWith({{0, 240, 60}, {960, 240, 64}}, 4 * kPpq, kAutoVoices);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = song.clips.begin()->first;
    a.position = 4 * kPpq;
    a.length = 4 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
    p.params[playheads::knob::kDirection] =
        devices::normalize(devices::playheadInfo().params[playheads::knob::kDirection], 1); // reverse
    d.playheads.push_back(p);
    song.tracks[0].devices.push_back(d);
    Harness h(song, false);
    h.engine.setPlayMode(PlayMode::Arranger);
    h.engine.reset();
    h.engine.play();

    h.runTo(4 * kPpq * kSamplesPerTick - 2);
    CHECK_FALSE(h.anyGate()); // nothing before the timeline clip
    // Going backward from the clip's start it wraps to the end; the 64 ends 2 beats and 3/4 before it.
    h.runTo((4 * kPpq + 4 * kPpq - 1200) * kSamplesPerTick + 2);
    CHECK(h.gate());
    CHECK(h.pitch() == 64);
}

TEST_CASE("engine: timeline playheads read ahead, and play every note once while the song is being edited") {
    Song song = songWith({{0, 240, 60}, {960, 240, 62}, {1920, 240, 64}, {2880, 240, 65}}, 4 * kPpq, kAutoVoices);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = song.clips.begin()->first;
    a.length = 16 * kPpq; // the one-bar clip looping four times on the timeline
    song.tracks[0].arrangement.push_back(a);
    song.clips.begin()->second.loopMode = LoopMode::Loop;
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    d.playheads.push_back(devices::makePlayhead(song.newId<PlayheadId>(), 0));
    song.tracks[0].devices.push_back(d);
    Harness h(song, false);
    h.engine.setPlayMode(PlayMode::Arranger);
    h.engine.reset();
    h.engine.play();
    // An odd tempo, so reads ahead don't line up with the beats and hold queued notes when an edit comes.
    h.engine.setTempo(kBpm * 1.37);
    int starts = 0;
    bool was = false;
    for (int64_t s = 0; s < int64_t((16 * kPpq - 100) * kSamplesPerTick / 1.37); s++) {
        if (s % 47 == 0)
            h.republish();
        h.step();
        bool now = h.anyGate();
        starts += now && !was;
        was = now;
    }
    CHECK(starts == 16);
}

TEST_CASE("engine: a playhead's branch changes only that playhead's notes") {
    Song song = songWith({{0, 240, 60}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    for (int i = 0; i < 2; i++)
        d.playheads.push_back(devices::makePlayhead(song.newId<PlayheadId>(), i));
    Device up = devices::makeDevice(DeviceType::Transpose, song.newId<DeviceId>());
    up.params = knobsOf(DeviceType::Transpose, {{chain::knob::kSemitones, 12}});
    d.playheads[1].branch.push_back(up);
    song.tracks[0].devices.push_back(d);
    Device fifth = devices::makeDevice(DeviceType::Transpose, song.newId<DeviceId>());
    fifth.params = knobsOf(DeviceType::Transpose, {{chain::knob::kSemitones, 7}});
    song.tracks[0].devices.push_back(fifth); // the shared chain, after the branches
    Harness h(song);
    h.step();
    std::multiset<int> sounding;
    for (int v = 0; v < h.frame().channels; v++)
        if (h.gate(v))
            sounding.insert(h.pitch(v));
    CHECK(sounding == std::multiset<int>{67, 79}); // both playheads on the 60: one up a fifth, one up an octave too
}

TEST_CASE("engine: changing a playhead's rate while it plays changes its speed, without a jump") {
    Song song = songWith({{0, 240, 60}, {960, 240, 62}, {1920, 240, 64}, {2880, 240, 65}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    d.playheads.push_back(devices::makePlayhead(song.newId<PlayheadId>(), 0));
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    h.runTo(1000 * kSamplesPerTick);
    CHECK(h.pitch() == 62);
    // Twice as fast from tick 1000: the 64 (at 1920) comes 460 ticks later; 2x from the start would play the 65 by
    // then.
    float twice = choiceKnob(playheads::knob::kRate, "2x");
    h.song.tracks[0].devices[0].playheads[0].params[playheads::knob::kRate] = twice;
    h.republish();
    h.runTo(1480 * kSamplesPerTick);
    CHECK(h.gate());
    CHECK(h.pitch() == 64);
}

TEST_CASE("engine: note operators still apply to the notes playheads play") {
    Song song = songWith({{0, 240, 60}, {960, 240, 62}}, 4 * kPpq, kAutoVoices);
    Clip& clip = song.clips.begin()->second;
    clip.notes[0].ops.chance = 0;  // never plays
    clip.notes[1].ops.repeats = 2; // twice
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    d.playheads.push_back(devices::makePlayhead(song.newId<PlayheadId>(), 0));
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    h.step();
    CHECK_FALSE(h.anyGate());
    int starts = 0;
    bool was = false;
    for (int64_t s = h.sample; s < 1920 * kSamplesPerTick; s++) {
        h.step();
        bool now = h.anyGate();
        starts += now && !was;
        was = now;
    }
    CHECK(starts == 2);
}

TEST_CASE("engine: a note in a Motion device drifts pass by pass with a Per pass Steps modulator") {
    Song song = songWith({{0, 240, 60}}, 4 * kPpq, kAutoVoices);
    Clip& clip = song.clips.begin()->second;
    Device& group = addDevice(song, DeviceType::Motion, {{chain::knob::kMotionInKey, 0}}); // semitones
    clip.notes[0].motion = group.id;
    Modulator steps = devices::makeModulator(ModulatorType::Steps, song.newId<ModulatorId>());
    steps.params = knobsOf(ModulatorType::Steps, {{mods::knob::kStepCount, 2}, {mods::knob::kStepPerPass, 1}});
    steps.steps = {0, 1};
    Modulation route;
    route.target = {ParamRef::Kind::Device, group.id.value, 0};
    route.amount = 1.f / 48; // one step at full
    steps.routes.push_back(route);
    song.tracks[0].modulators.push_back(steps);
    Harness h(song);
    std::vector<int> pitches;
    for (int pass = 0; pass < 3; pass++) {
        h.runTo(int64_t(pass) * 4 * kPpq * kSamplesPerTick + 10);
        pitches.push_back(h.gate() ? h.pitch() : -1);
    }
    CHECK(pitches == std::vector<int>{60, 61, 60}); // up a semitone on the second pass, back on the third
}

TEST_CASE("engine: one piano roll, two parts: the bass playhead takes the lowest notes two octaves down") {
    Song song = songWith({{0, 240, 60}, {0, 240, 64}, {0, 240, 67}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    for (int i = 0; i < 2; i++)
        d.playheads.push_back(devices::makePlayhead(song.newId<PlayheadId>(), i));
    const devices::TypeInfo& info = devices::playheadInfo();
    auto set = [&](Playhead& p, int k, float v) {
        p.params[size_t(k)] = devices::normalize(info.params[size_t(k)], v);
    };
    set(d.playheads[0], playheads::knob::kNotes, 2);   // the melody: highest notes
    set(d.playheads[1], playheads::knob::kNotes, 1);   // the bass: lowest notes...
    set(d.playheads[1], playheads::knob::kOctave, -2); // ...two octaves down
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    h.step();
    std::multiset<int> sounding;
    for (int v = 0; v < h.frame().channels; v++)
        if (h.gate(v))
            sounding.insert(h.pitch(v));
    CHECK(sounding == std::multiset<int>{36, 67});
}

TEST_CASE("engine: a Curve modulator moves a playhead setting while it plays") {
    Song song = songWith({{0, 240, 60}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
    const devices::ParamInfo& octave = devices::playheadInfo().params[playheads::knob::kOctave];
    d.playheads.push_back(p);
    song.tracks[0].devices.push_back(d);
    // Two bars: the first at the octave as set, the second an octave up. Drawn around the middle (no change), turning
    // the setting all the way: one octave is an eighth of the knob, so a sixteenth above the middle.
    Modulator curve = devices::makeModulator(ModulatorType::Curve, song.newId<ModulatorId>());
    curve.length = curve.segmentEnd = 8 * kPpq;
    float up = 0.5f + (devices::normalize(octave, 1) - devices::normalize(octave, 0)) / 2;
    curve.points = {{0, 0.5f, CurveType::Hold, 0}, {4 * kPpq, up, CurveType::Hold, 0}};
    Modulation route;
    route.target = {ParamRef::Kind::Playhead, p.id.value, uint16_t(playheads::knob::kOctave)};
    route.amount = 1;
    curve.routes.push_back(route);
    song.tracks[0].modulators.push_back(curve);
    Harness h(song);
    std::vector<int> pitches;
    for (int bar = 0; bar < 3; bar++) {
        h.runTo(int64_t(bar) * 4 * kPpq * kSamplesPerTick + 10);
        pitches.push_back(h.gate() ? h.pitch() : -1);
    }
    CHECK(pitches == std::vector<int>{60, 72, 60});
}

TEST_CASE("engine: a curve or a lane on a playhead's On switches it off and on, cutting what it sounds") {
    // A long note from the start and a short one halfway through beat 2. The playhead is off for beat 2.
    Song song = songWith({{0, 3800, 60}, {1440, 100, 64}}, 8 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
    d.playheads.push_back(p);
    song.tracks[0].devices.push_back(d);
    ParamRef on{ParamRef::Kind::Playhead, p.id.value, uint16_t(playheads::knob::kOn)};
    SUBCASE("a curve") {
        Modulator curve = devices::makeModulator(ModulatorType::Curve, song.newId<ModulatorId>());
        curve.length = curve.segmentEnd = 8 * kPpq;
        // The middle is the On as set (on); a whole row down is off.
        curve.points = {{0, 0.5f, CurveType::Hold, 0}, {960, 0, CurveType::Hold, 0}, {1920, 0.5f, CurveType::Hold, 0}};
        Modulation route;
        route.target = on;
        route.amount = 1;
        curve.routes.push_back(route);
        song.tracks[0].modulators.push_back(curve);
    }
    SUBCASE("a knob lane") {
        AutomationEnvelope lane = makeKnobLane(song.newId<AutomationId>(), on, "Playhead · On", 1, true, 0);
        lane.points = {{0, 1, CurveType::Hold, 0}, {960, 0, CurveType::Hold, 0}, {1920, 1, CurveType::Hold, 0}};
        song.tracks[0].automation.push_back(lane);
    }
    Harness h(song);
    auto sounding = [&h] {
        std::vector<int> out;
        for (int v = 0; v < h.frame().channels; v++)
            if (h.gate(v))
                out.push_back(h.pitch(v));
        return out;
    };
    h.runTo(900 * kSamplesPerTick);
    CHECK(sounding() == std::vector<int>{60});
    // Off: the long note stops within a control block, and the one in beat 2 never starts.
    h.runTo(960 * kSamplesPerTick + 64);
    CHECK(sounding().empty());
    h.runTo(1460 * kSamplesPerTick);
    CHECK(sounding().empty());
    // On again: it plays what it reaches next, not the note it cut.
    h.runTo(1940 * kSamplesPerTick);
    CHECK(sounding().empty());
    // Round again (the clip is two bars), it plays from the start.
    h.runTo((8 * kPpq + 20) * kSamplesPerTick);
    CHECK(sounding() == std::vector<int>{60});
}

/// A Curve on a Transpose's octaves: the first bar of its drawing as set, the second an octave up.
Modulator octaveCurve(Song& song, Device& transpose, Tick segmentStart = 0) {
    const devices::ParamInfo& octaves = devices::deviceInfo(DeviceType::Transpose).params[chain::knob::kOctaves];
    Modulator curve = devices::makeModulator(ModulatorType::Curve, song.newId<ModulatorId>());
    curve.length = 8 * kPpq;
    float up = 0.5f + (devices::normalize(octaves, 1) - devices::normalize(octaves, 0)) / 2;
    curve.points = {{0, 0.5f, CurveType::Hold, 0}, {4 * kPpq, up, CurveType::Hold, 0}};
    curve.segmentStart = segmentStart;
    curve.segmentEnd = 8 * kPpq;
    Modulation route;
    route.target = {ParamRef::Kind::Device, transpose.id.value, uint16_t(chain::knob::kOctaves)};
    route.amount = 1;
    curve.routes.push_back(route);
    return curve;
}

TEST_CASE("engine: a Curve counts from when its track's timeline clip starts, not from the song's start") {
    Song song = songWith({{0, 240, 60}}, 4 * kPpq, kAutoVoices);
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = song.clips.begin()->first;
    a.position = 6 * kPpq; // halfway through bar 2: the song's own count would be inside the curve's second bar
    a.length = 12 * kPpq;
    song.tracks[0].arrangement.push_back(a);
    Device& transpose = addDevice(song, DeviceType::Transpose);
    song.tracks[0].modulators.push_back(octaveCurve(song, transpose));
    Harness h(song, false);
    h.engine.setPlayMode(PlayMode::Arranger);
    h.engine.reset();
    h.engine.play();
    std::vector<int> pitches;
    for (int bar = 0; bar < 3; bar++) {
        h.runTo((6 * kPpq + int64_t(bar) * 4 * kPpq) * kSamplesPerTick + 10);
        pitches.push_back(h.gate() ? h.pitch() : -1);
    }
    INFO(pitches[0], " ", pitches[1], " ", pitches[2]);
    CHECK(pitches == std::vector<int>{60, 72, 60});
}

TEST_CASE("engine: a launched clip's Curve plays its segment, from the segment's start, round and round") {
    Song song = songWith({{0, 240, 60}}, 4 * kPpq, kAutoVoices);
    Device& transpose = addDevice(song, DeviceType::Transpose);
    // Only the second bar of the drawing plays: every pass is an octave up.
    song.tracks[0].modulators.push_back(octaveCurve(song, transpose, 4 * kPpq));
    Harness h(song);
    std::vector<int> pitches;
    for (int bar = 0; bar < 3; bar++) {
        h.runTo(int64_t(bar) * 4 * kPpq * kSamplesPerTick + 10);
        pitches.push_back(h.gate() ? h.pitch() : -1);
    }
    INFO(pitches[0], " ", pitches[1], " ", pitches[2]);
    CHECK(pitches == std::vector<int>{72, 72, 72});
}

TEST_CASE("engine: playheads play every note while the song is being edited") {
    Song song = songWith({{0, 240, 60}, {960, 240, 62}, {1920, 240, 64}, {2880, 240, 65}}, 4 * kPpq, kAutoVoices);
    song.clips.begin()->second.notes[1].ops.velocitySpread = 0.01f; // a note with operators (extras)
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    d.playheads.push_back(devices::makePlayhead(song.newId<PlayheadId>(), 0));
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    // An odd tempo, so the playheads' reads ahead don't line up with the beats and hold queued notes.
    h.engine.setTempo(kBpm * 1.37);
    int starts = 0;
    bool was = false;
    for (int64_t s = 0; s < int64_t((4 * 4 * kPpq - 100) * kSamplesPerTick / 1.37); s++) {
        if (s % 47 == 0)
            h.republish(); // edits all the time: the notes read ahead must still play
        h.step();
        bool now = h.anyGate();
        starts += now && !was;
        was = now;
    }
    CHECK(starts == 16);
}

TEST_CASE("engine: playheads play exactly the notes their settings say, whatever the settings") {
    const devices::TypeInfo& info = devices::playheadInfo();
    using namespace playheads::knob;
    int checked = 0;
    for (const char* rate : {"1/2x", "1x", "2x", "4x"})
        for (int direction : {0, 1, 2})
            for (const char* repeat : {"Off", "1/4"})
                for (int order : {0, 1, 4}) {
                    Song song = songWith({{0, 120, 60}, {960, 120, 62}, {1920, 120, 64}, {2880, 120, 65}}, 4 * kPpq,
                                         kAutoVoices);
                    tessera::Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
                    tessera::Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
                    auto set = [&](int k, float v) {
                        p.params[size_t(k)] = devices::normalize(info.params[size_t(k)], v);
                    };
                    p.params[kRate] = choiceKnob(kRate, rate);
                    set(kDirection, float(direction));
                    p.params[kRepeatSize] = choiceKnob(kRepeatSize, repeat);
                    set(kOrder, float(order));
                    p.params[kOrderStep] = choiceKnob(kOrderStep, "1/4");
                    d.playheads.push_back(p);
                    song.tracks[0].devices.push_back(d);

                    // What should play: the pure maths over the same 4 bars.
                    playheads::Motion m =
                        playheads::motionFrom(p.params.data(), d.params.data(), 0, 4 * kPpq, TimeSig());
                    const Clip& clip = song.clips.begin()->second;
                    struct Ev {
                        double start, end;
                    };
                    std::vector<Ev> evs;
                    for (const Note& n : clip.notes)
                        evs.push_back({double(n.start), double(n.end())});
                    std::vector<uint32_t> byEnd = playheads::orderByEnd(evs);
                    int expected = 0;
                    double until = 4 * 4 * kPpq;
                    for (double a = 0; a < until; a += 8) {
                        playheads::Sweep s[playheads::kMaxSweeps];
                        int n = playheads::sweeps(m, a, std::min(until, a + 8), s);
                        for (int i = 0; i < n; i++)
                            playheads::crossed(s[i], evs, byEnd, [&](const Ev&, double) { expected++; });
                    }

                    // What plays: gate rises on any voice.
                    Harness h(song);
                    int starts = 0;
                    std::array<bool, kMaxVoices> was{};
                    for (int64_t smp = 0; smp < int64_t(until) * kSamplesPerTick; smp++) {
                        h.step();
                        for (int v = 0; v < h.frame().channels; v++) {
                            bool now = h.gate(v);
                            starts += now && !was[size_t(v)];
                            was[size_t(v)] = now;
                        }
                    }
                    CAPTURE(rate);
                    CAPTURE(direction);
                    CAPTURE(repeat);
                    CAPTURE(order);
                    CHECK(starts == expected);
                    checked++;
                }
    CHECK(checked == 72);
}

TEST_CASE("engine: a Motion device before the playheads moves the note inside the clip; after them it delays it") {
    // One note on beat 1, pushed a beat later; one playhead at 2x.
    auto firstStart = [](bool before) {
        Song song = songWith({{0, 240, 60}}, 4 * kPpq, kAutoVoices);
        Device heads = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
        Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
        p.params[playheads::knob::kRate] = choiceKnob(playheads::knob::kRate, "2x");
        heads.playheads.push_back(p);
        Device motion = devices::makeDevice(DeviceType::Motion, song.newId<DeviceId>());
        motion.params[chain::knob::kMotionTime] =
            devices::normalize(devices::motionInfo().params[chain::knob::kMotionTime], 4); // 4/16 = a beat
        song.clips.begin()->second.notes[0].motion = motion.id;
        if (before) {
            song.tracks[0].devices.push_back(motion);
            song.tracks[0].devices.push_back(heads);
        } else {
            song.tracks[0].devices.push_back(heads);
            song.tracks[0].devices.push_back(motion);
        }
        Harness h(song);
        for (int64_t s = 0; s < 2 * kPpq * kSamplesPerTick; s++) {
            h.step();
            if (h.gate())
                return double(s) / kSamplesPerTick;
        }
        return -1.0;
    };
    // Before: the note sits on beat 2 of the clip, which a 2x playhead reaches after half a beat.
    CHECK(firstStart(true) == doctest::Approx(kPpq / 2).epsilon(0.01));
    // After: the playhead plays it on beat 1, then it is a beat late.
    CHECK(firstStart(false) == doctest::Approx(kPpq).epsilon(0.01));
}

TEST_CASE("engine: Steps set to Per pass step a Motion knob once a pass") {
    Song song = songWith({{0, 240, 60}}, 4 * kPpq, kAutoVoices);
    Device& motion = addDevice(song, DeviceType::Motion, {{chain::knob::kMotionInKey, 0}}); // semitones
    song.clips.begin()->second.notes[0].motion = motion.id;
    // Three passes: as written, one semitone up, two up. Pitch spans 48 semitones, so a semitone is 1/48 of the knob:
    // bipolar with amount 1, the drawn value moves half that per semitone.
    Modulator steps = devices::makeModulator(ModulatorType::Steps, song.newId<ModulatorId>());
    steps.params = knobsOf(ModulatorType::Steps,
                           {{mods::knob::kStepCount, 3}, {mods::knob::kStepBipolar, 1}, {mods::knob::kStepPerPass, 1}});
    float semitone = 1.f / 96;
    steps.steps = {0.5f, 0.5f + semitone, 0.5f + 2 * semitone};
    Modulation route;
    route.target = {ParamRef::Kind::Device, motion.id.value, uint16_t(chain::knob::kMotionPitch)};
    route.amount = 1;
    steps.routes.push_back(route);
    song.tracks[0].modulators.push_back(steps);
    Harness h(song);
    std::vector<int> pitches;
    for (int pass = 0; pass < 4; pass++) {
        h.runTo(int64_t(pass) * 4 * kPpq * kSamplesPerTick + 10);
        pitches.push_back(h.gate() ? h.pitch() : -1);
    }
    CHECK(pitches == std::vector<int>{60, 61, 62, 60}); // then round again
}

TEST_CASE(
    "the piano roll learns the pitch a Motion's Per pass steps give each note on every pass, as the engine plays it") {
    Song song = songWith({{0, 240, 60}, {960, 240, 64}}, 4 * kPpq, kAutoVoices);
    Device& motion = addDevice(song, DeviceType::Motion, {{chain::knob::kMotionInKey, 0}}); // semitones
    Clip& clip = song.clips.begin()->second;
    clip.notes[0].motion = motion.id;
    Modulator steps = devices::makeModulator(ModulatorType::Steps, song.newId<ModulatorId>());
    steps.params = knobsOf(ModulatorType::Steps,
                           {{mods::knob::kStepCount, 3}, {mods::knob::kStepBipolar, 1}, {mods::knob::kStepPerPass, 1}});
    float semitone = 1.f / 96;
    steps.steps = {0.5f, 0.5f + semitone, 0.5f + 2 * semitone};
    Modulation route;
    route.target = {ParamRef::Kind::Device, motion.id.value, uint16_t(chain::knob::kMotionPitch)};
    route.amount = 1;
    steps.routes.push_back(route);
    song.tracks[0].modulators.push_back(steps);

    std::vector<std::vector<render::MotionMove>> moves = render::motionMoves(song, song.tracks[0], clip);
    REQUIRE(moves.size() == 2);
    std::vector<int> pitches;
    for (const render::MotionMove& m : moves[0])
        pitches.push_back(m.pitch);
    CHECK(pitches == std::vector<int>{60, 61, 62}); // the same three passes the engine plays
    CHECK(moves[1].empty());                        // not in the Motion
}

TEST_CASE("engine: a note's pitch curve keeps pace with the playhead crossing it, at any speed") {
    Song song = songWith({{0, 960, 60}}, 4 * kPpq, kAutoVoices);
    song.clips.begin()->second.notes[0].pitchExpr = {{0, 0, 0}, {1, 12, 0}}; // an octave up across the note
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
    p.params[playheads::knob::kRate] = choiceKnob(playheads::knob::kRate, "1/2x");
    d.playheads.push_back(p); // "1/2x": it takes 1920 ticks to cross the 960-tick note
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    h.runTo(960 * kSamplesPerTick);
    REQUIRE(h.gate());
    CHECK(h.frame().voct[0] * 12 == doctest::Approx(6).epsilon(0.05)); // half way across: half way up
    h.runTo(1900 * kSamplesPerTick);
    CHECK(h.frame().voct[0] * 12 == doctest::Approx(12 * 1900.0 / 1920).epsilon(0.05));
}

TEST_CASE("engine: it tells the editor where each playhead is, as a changed rate moved it") {
    Song song = songWith({{0, 240, 60}}, 4 * kPpq, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    d.playheads.push_back(devices::makePlayhead(song.newId<PlayheadId>(), 0));
    song.tracks[0].devices.push_back(d);
    Harness h(song);
    h.runTo(1000 * kSamplesPerTick);
    CHECK(h.engine.playheadPosition(0, 0) == doctest::Approx(1000).epsilon(0.02));
    h.song.tracks[0].devices[0].playheads[0].params[playheads::knob::kRate] =
        choiceKnob(playheads::knob::kRate, "2x"); // from here on
    h.republish();
    h.runTo(1480 * kSamplesPerTick);
    // 1000 + 2 x 480, not 2 x 1480 as the new rate alone would say.
    CHECK(h.engine.playheadPosition(0, 0) == doctest::Approx(1960).epsilon(0.02));
}

/// The ticks at which notes of `pitch` start on any voice, up to `ticks`.
std::vector<double> startsOf(Harness& h, int pitch, double ticks) {
    std::vector<double> out;
    bool was = false;
    while (double(h.sample + 1) < ticks * kSamplesPerTick) {
        h.step();
        bool now = false;
        for (int v = 0; v < h.frame().channels; v++)
            now = now || (h.gate(v) && h.pitch(v) == pitch);
        if (now && !was)
            out.push_back(double(h.sample) / kSamplesPerTick);
        was = now;
    }
    return out;
}

/// A song with one Playheads device of one playhead whose Offset is `offset` (a choice name such as "+1/8").
Song offsetSong(const std::vector<Spec>& notes, const char* offset, Tick loopLength = 4 * kPpq) {
    Song song = songWith(notes, loopLength, kAutoVoices);
    Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
    p.params[playheads::knob::kTimeOffset] = choiceKnob(playheads::knob::kTimeOffset, offset);
    d.playheads.push_back(p);
    song.tracks[0].devices.push_back(d);
    return song;
}

void checkStarts(const std::vector<double>& got, const std::vector<double>& want) {
    REQUIRE(got.size() == want.size());
    for (size_t i = 0; i < got.size(); i++)
        CHECK(got[i] == doctest::Approx(want[i]).epsilon(0.002));
}

TEST_CASE("engine: a playhead's Offset plays its notes later or earlier, and its line follows") {
    SUBCASE("late") {
        Harness h(offsetSong({{0, 240, 60}, {1920, 240, 64}}, "+1/8"));
        checkStarts(startsOf(h, 60, 4400), {480, 4320});
        Harness line(offsetSong({{0, 240, 60}, {1920, 240, 64}}, "+1/8"));
        line.runTo(1000 * kSamplesPerTick);
        CHECK(line.engine.playheadPosition(0, 0) == doctest::Approx(520).epsilon(0.03)); // trailing by 1/8
    }
    SUBCASE("late, the other note") {
        Harness h(offsetSong({{0, 240, 60}, {1920, 240, 64}}, "+1/8"));
        checkStarts(startsOf(h, 64, 4000), {2400});
    }
    SUBCASE("early: the first pass skips the note that would come before the clip started") {
        Harness h(offsetSong({{0, 240, 60}, {1920, 240, 64}}, "-1/16"));
        checkStarts(startsOf(h, 60, 7600), {3600, 7440});
    }
    SUBCASE("early, the other note") {
        Harness h(offsetSong({{0, 240, 60}, {1920, 240, 64}}, "-1/16"));
        checkStarts(startsOf(h, 64, 4000), {1680});
    }
}

TEST_CASE("engine: an Offset never plays past a clip's end, on the timeline or launched") {
    SUBCASE("a late note past the timeline clip's end is cut") {
        Song song = offsetSong({{0, 240, 60}, {3600, 120, 64}}, "+1/8");
        ArrangerClip a;
        a.id = song.newId<ArrangementId>();
        a.clip = song.clips.begin()->first;
        a.position = 4 * kPpq;
        a.length = 4 * kPpq;
        song.tracks[0].arrangement.push_back(a);
        Harness h(song, false);
        h.engine.setPlayMode(PlayMode::Arranger);
        h.engine.reset();
        h.engine.play();
        checkStarts(startsOf(h, 60, 12 * kPpq), {4 * kPpq + 480});
        Harness again(song, false);
        again.engine.setPlayMode(PlayMode::Arranger);
        again.engine.reset();
        again.engine.play();
        CHECK(startsOf(again, 64, 12 * kPpq).empty());
    }
    SUBCASE("an early note from past the timeline clip's end is not read") {
        Song song = offsetSong({{0, 240, 60}, {3900, 120, 64}}, "-1/8", 8 * kPpq);
        ArrangerClip a;
        a.id = song.newId<ArrangementId>();
        a.clip = song.clips.begin()->first;
        a.position = 4 * kPpq;
        a.length = 4 * kPpq;
        song.tracks[0].arrangement.push_back(a);
        Harness h(song, false);
        h.engine.setPlayMode(PlayMode::Arranger);
        h.engine.reset();
        h.engine.play();
        CHECK(startsOf(h, 64, 12 * kPpq).empty());
    }
    SUBCASE("a launched clip that doesn't loop cuts its late notes at its end") {
        Song song = offsetSong({{0, 240, 60}, {3600, 120, 64}}, "+1/8");
        song.clips.begin()->second.loopMode = LoopMode::Off;
        Harness h(song);
        CHECK(startsOf(h, 64, 6000).empty());
    }
    SUBCASE("a looping launched clip carries its late notes over the loop") {
        Harness h(offsetSong({{0, 240, 60}, {3600, 120, 64}}, "+1/8"));
        checkStarts(startsOf(h, 64, 8000), {4080, 7920});
    }
}

TEST_CASE("engine: a lane moves a playhead's Offset while it plays, and no note plays twice") {
    Song song = offsetSong({{0, 120, 60}, {960, 120, 62}, {1920, 120, 64}, {2880, 120, 65}}, "0");
    ParamRef offset{ParamRef::Kind::Playhead, song.tracks[0].devices[0].playheads[0].id.value,
                    uint16_t(playheads::knob::kTimeOffset)};
    AutomationEnvelope lane = makeKnobLane(song.newId<AutomationId>(), offset, "Playhead · Offset",
                                           choiceKnob(playheads::knob::kTimeOffset, "0"), true, 0);
    // From halfway through beat 2 a quarter late: the playhead waits a beat, then reads on from where it was.
    lane.points = {{0, choiceKnob(playheads::knob::kTimeOffset, "0"), CurveType::Hold, 0},
                   {1440, choiceKnob(playheads::knob::kTimeOffset, "+1/4"), CurveType::Hold, 0}};
    song.tracks[0].automation.push_back(lane);
    Harness h(song);
    std::vector<double> starts;
    bool was[128] = {};
    while (h.sample < 4000 * kSamplesPerTick) {
        h.step();
        for (int pitch : {60, 62, 64, 65}) {
            bool now = false;
            for (int v = 0; v < h.frame().channels; v++)
                now = now || (h.gate(v) && h.pitch(v) == pitch);
            if (now && !was[pitch])
                starts.push_back(double(h.sample) / kSamplesPerTick + pitch * 1e4);
            was[pitch] = now;
        }
    }
    std::sort(starts.begin(), starts.end());
    checkStarts(starts, {60e4 + 0, 62e4 + 960, 64e4 + 2880, 65e4 + 3840});
}

TEST_CASE("the piano roll finds the moves of a Motion inside a playhead's own devices, as of one in the chain") {
    Song song = songWith({{0, 240, 60}, {960, 240, 64}}, 4 * kPpq, kAutoVoices);
    auto motion = [&](float semitones) {
        Device m = devices::makeDevice(DeviceType::Motion, song.newId<DeviceId>());
        m.params[chain::knob::kMotionInKey] = 0;
        m.params[chain::knob::kMotionPitch] =
            devices::normalize(devices::motionInfo().params[chain::knob::kMotionPitch], semitones);
        return m;
    };
    Device inChain = motion(2), inBranch = motion(5);
    Device heads = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
    p.branch.push_back(inBranch);
    heads.playheads.push_back(p);
    song.tracks[0].devices = {inChain, heads};
    Clip& clip = song.clips.begin()->second;
    clip.notes[0].motion = inChain.id;
    clip.notes[1].motion = inBranch.id;
    std::vector<std::vector<render::MotionMove>> moves = render::motionMoves(song, song.tracks[0], clip);
    REQUIRE(moves[0].size() >= 1);
    REQUIRE(moves[1].size() >= 1);
    CHECK(moves[0][0].pitch == 62);
    CHECK(moves[1][0].pitch == 69);
}

TEST_CASE("the piano roll reads a Motion's Curve from when its timeline clip started, as the engine does") {
    Song song = songWith({{0, 240, 60}}, 4 * kPpq, kAutoVoices);
    Device motion = devices::makeDevice(DeviceType::Motion, song.newId<DeviceId>());
    motion.params[chain::knob::kMotionInKey] = 0;
    Device heads = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
    heads.playheads.push_back(devices::makePlayhead(song.newId<PlayheadId>(), 0));
    song.tracks[0].devices = {motion, heads};
    Clip& clip = song.clips.begin()->second;
    clip.notes[0].motion = motion.id;
    // A step up two semitones at bar 2 of the curve; the clip starts on the timeline at bar 2.
    const devices::ParamInfo& pitch = devices::motionInfo().params[chain::knob::kMotionPitch];
    Modulator curve = devices::makeModulator(ModulatorType::Curve, song.newId<ModulatorId>());
    curve.length = 8 * kPpq;
    float up = 0.5f + (devices::normalize(pitch, 2) - devices::normalize(pitch, 0)) / 2;
    curve.points = {{0, 0.5f, CurveType::Hold, 0}, {4 * kPpq, up, CurveType::Hold, 0}};
    curve.segmentEnd = 8 * kPpq;
    Modulation route;
    route.target = {ParamRef::Kind::Device, motion.id.value, uint16_t(chain::knob::kMotionPitch)};
    route.amount = 1;
    curve.routes.push_back(route);
    song.tracks[0].modulators.push_back(curve);

    double clipStart = 4 * kPpq;
    // Bar 1 of the clip: the curve hasn't stepped yet, though the song is past its step.
    CHECK(render::motionMoveAt(song, song.tracks[0], clip, 0, 0, clipStart + kPpq, kPpq).pitch == 60);
    // Bar 2 of the clip: stepped up.
    CHECK(render::motionMoveAt(song, song.tracks[0], clip, 0, 1, clipStart + 5 * kPpq, 5 * kPpq).pitch == 62);
}

namespace {
/// Rising edges of `pitch` on any voice over `samples`.
int countStarts(Harness& h, int pitch, int64_t samples) {
    int starts = 0;
    bool was = false;
    for (int64_t s = 0; s < samples; s++) {
        h.step();
        bool now = false;
        for (int v = 0; v < 48; v++)
            now |= h.frame().gate[v] > 5.f && int(std::lround(h.frame().voct[v] * 12)) + 60 == pitch;
        starts += now && !was;
        was = now;
    }
    return starts;
}
} // namespace

TEST_CASE("engine: playheads play a clip's first note on every pass, at any speed and tempo") {
    for (double tempo : {1.0, 1.37, 0.91, 1.113}) {
        for (const char* rate : {"1x", "1/2x", "3/4x", "4/3x", "2x"}) {
            // Launcher loop.
            Song song = songWith({{0, 120, 60}, {960, 120, 62}}, 4 * kPpq, kAutoVoices);
            song.clips.begin()->second.loopMode = LoopMode::Loop;
            Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
            Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
            p.params[playheads::knob::kRate] = choiceKnob(playheads::knob::kRate, rate);
            d.playheads.push_back(p);
            song.tracks[0].devices.push_back(d);
            Harness h(song);
            h.engine.setTempo(kBpm * tempo);
            double speed = playheads::playheadRate(playheads::choiceIndex(playheads::rateNames(), rate));
            int64_t bars = 12;
            int starts = countStarts(h, 60, int64_t(bars * 4 * kPpq * kSamplesPerTick / tempo) - 50);
            int expected = int(std::ceil(bars * speed - 1e-9));
            if (starts != expected)
                MESSAGE("launcher tempo " << tempo << " rate " << rate << ": " << starts << " of " << expected);
            CHECK(starts == expected);
        }
    }
}

TEST_CASE("engine: timeline playheads play the first note again after the song loop") {
    for (double tempo : {1.0, 1.37, 0.91, 1.113}) {
        Song song = songWith({{0, 120, 60}, {960, 120, 62}}, 4 * kPpq, kAutoVoices);
        song.clips.begin()->second.loopMode = LoopMode::Loop;
        ArrangerClip a;
        a.id = song.newId<ArrangementId>();
        a.clip = song.clips.begin()->first;
        a.length = 8 * kPpq;
        song.tracks[0].arrangement.push_back(a);
        song.loopStart = 0;
        song.loopEnd = 8 * kPpq;
        Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
        d.playheads.push_back(devices::makePlayhead(song.newId<PlayheadId>(), 0));
        song.tracks[0].devices.push_back(d);
        Harness h(song, false);
        h.engine.setPlayMode(PlayMode::Arranger);
        h.engine.setLoopEnabled(true);
        h.engine.reset();
        h.engine.play();
        h.engine.setTempo(kBpm * tempo);
        int starts = countStarts(h, 60, int64_t(3 * 8 * kPpq * kSamplesPerTick / tempo) - 50);
        if (starts != 6)
            MESSAGE("timeline tempo " << tempo << ": " << starts << " of 6");
        CHECK(starts == 6);
    }
}

TEST_CASE("engine: back-to-back notes of one pitch start again each time, across the loop, with or without playheads") {
    for (bool heads : {false, true}) {
        for (const char* rate : {"1x", "1/2x", "2x"}) {
            Song song =
                songWith({{0, 960, 60}, {960, 960, 60}, {1920, 960, 60}, {2880, 960, 60}}, 4 * kPpq, kAutoVoices);
            song.clips.begin()->second.loopMode = LoopMode::Loop;
            if (heads) {
                Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
                Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), 0);
                p.params[playheads::knob::kRate] = choiceKnob(playheads::knob::kRate, rate);
                d.playheads.push_back(p);
                song.tracks[0].devices.push_back(d);
            } else if (std::string(rate) != "1x") {
                continue;
            }
            Harness h(song);
            double speed = playheads::playheadRate(playheads::choiceIndex(playheads::rateNames(), rate));
            int starts = countStarts(h, 60, int64_t(3 * 4 * kPpq * kSamplesPerTick) - 50);
            int expected = int(std::ceil(12 * speed - 1e-9));
            if (starts != expected)
                MESSAGE("heads " << heads << " rate " << rate << ": " << starts << " of " << expected);
            CHECK(starts == expected);
        }
    }
}

TEST_CASE("engine: one to three playheads at any speed start the first note on the first sample") {
    const int rates = int(playheads::rateNames().size());
    for (int r = 0; r < rates; r++) {
        for (int heads = 1; heads <= 3; heads++) {
            Song song = songWith({{0, 120, 60}, {960, 120, 62}}, 4 * kPpq, kAutoVoices);
            Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
            for (int k = 0; k < heads; k++) {
                Playhead p = devices::makePlayhead(song.newId<PlayheadId>(), k);
                p.params[playheads::knob::kRate] = devices::normalize(
                    devices::playheadInfo().params[playheads::knob::kRate], float((r + k * 3) % rates));
                d.playheads.push_back(p);
            }
            song.tracks[0].devices.push_back(d);
            Harness h(song);
            int64_t first = -1;
            for (int64_t s = 0; s < 200 && first < 0; s++) {
                h.step();
                for (int v = 0; v < 48; v++)
                    if (h.frame().gate[v] > 5.f)
                        first = s;
            }
            if (first != 0)
                MESSAGE("rate index " << r << " heads " << heads << ": first gate at sample " << first);
            CHECK(first == 0);
        }
    }
}
