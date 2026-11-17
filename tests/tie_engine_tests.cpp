#include "doctest.h"

#include "../src/engine/Engine.hpp"
#include "../src/engine/VoiceAllocator.hpp"

#include <algorithm>
#include <memory>

using namespace tessera;

namespace {

// 93.75 BPM at 48 kHz is exactly 1/32 tick per sample, so expected sample numbers are exact.
constexpr double kSampleRate = 48000;
constexpr double kBpm = 93.75;
constexpr int64_t kSamplesPerTick = 32;

struct Spec {
    Tick start, length;
    int pitch;
    bool tied = false;
    Tick before = 0, after = 0;
};

/// A song of one track (`voices` voices) whose first launcher slot holds a looping clip of `notes`.
Song songOf(const std::vector<Spec>& notes, Tick loop, int voices = 1) {
    Song song;
    Track track;
    track.id = song.newId<TrackId>();
    track.polyphony = voices;
    track.slots.resize(1);
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = loop;
    for (const Spec& s : notes) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = s.start;
        n.length = s.length;
        n.pitch = int8_t(s.pitch);
        n.tied = s.tied;
        n.glideBefore = s.before;
        n.glideAfter = s.after;
        clip.notes.push_back(n);
    }
    std::sort(clip.notes.begin(), clip.notes.end(), noteOrder);
    track.slots[0].clip = clip.id;
    song.clips.emplace(clip.id, clip);
    song.tracks.push_back(track);
    return song;
}

/// Drives an engine one sample at a time.
struct Rig {
    Song song;
    BusSlots slots;
    std::unique_ptr<Snapshot> snapshot;
    Engine engine;
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    int64_t sample = -1;

    /// Builds the song's snapshot and launches its first scene.
    explicit Rig(Song s) : song(std::move(s)) {
        slots.sync(song);
        snapshot = buildSnapshot(song, slots);
        engine.setSampleRate(kSampleRate);
        engine.setTempo(kBpm);
        engine.setSnapshot(snapshot.get());
        engine.launch({LaunchEvent::Kind::Scene, 0, 0, true});
    }
    /// Processes one sample.
    void step() {
        engine.process(frames.data(), events);
        sample++;
    }
    /// Processes samples until `target` is the last one done.
    void runTo(int64_t target) {
        while (sample < target)
            step();
    }
    /// The track's frame on the bus.
    const TrackFrame& frame() const { return frames[slots.slotOf(song.tracks[0].id)]; }
    /// Whether voice `v`'s gate is high.
    bool gate(int v = 0) const { return frame().gate[v] > 5.f; }
    /// Voice `v`'s V/OCT in semitones above C4.
    float semis(int v = 0) const { return frame().voct[v] * 12.f; }
    /// Whether the gate of voice `v` stays high on every sample up to `until`.
    bool highUntil(int64_t until, int v = 0) {
        bool high = true;
        while (sample < until) {
            step();
            high = high && gate(v);
        }
        return high;
    }
};

} // namespace

TEST_CASE("engine: a tied note keeps the gate high and moves the pitch at the joint") {
    Rig r(songOf({{0, 240, 60}, {240, 240, 64, true}}, 4 * kPpq));
    int64_t joint = 240 * kSamplesPerTick;
    r.runTo(joint - 1);
    CHECK(r.semis() == doctest::Approx(0));
    CHECK(r.highUntil(joint + 100)); // no gap, unlike the retrigger of untied notes
    CHECK(r.semis() == doctest::Approx(4));
    // The tied note ends as it should.
    r.runTo(480 * kSamplesPerTick + 2);
    CHECK_FALSE(r.gate());
}

TEST_CASE("engine: untied back-to-back notes still retrigger") {
    Rig r(songOf({{0, 240, 60}, {240, 240, 64}}, 4 * kPpq));
    r.runTo(240 * kSamplesPerTick);
    CHECK_FALSE(r.gate());
}

TEST_CASE("engine: the glide runs across the joint, from inside the first note into the second") {
    Rig r(songOf({{0, 240, 60}, {240, 240, 64, true, 60, 60}}, 4 * kPpq));
    r.runTo(180 * kSamplesPerTick - 1);
    CHECK(r.semis() == doctest::Approx(0));
    r.runTo(210 * kSamplesPerTick);
    CHECK(r.semis() == doctest::Approx(1).epsilon(0.02)); // a quarter of the way
    r.runTo(240 * kSamplesPerTick);
    CHECK(r.semis() == doctest::Approx(2).epsilon(0.02)); // halfway at the joint
    CHECK(r.gate());
    r.runTo(270 * kSamplesPerTick);
    CHECK(r.semis() == doctest::Approx(3).epsilon(0.02));
    r.runTo(300 * kSamplesPerTick + 1);
    CHECK(r.semis() == doctest::Approx(4));
}

TEST_CASE("engine: a short gate length never shortens a note a tie continues") {
    Song song = songOf({{0, 240, 60}, {240, 240, 64, true}, {720, 240, 67}}, 4 * kPpq);
    song.tracks[0].gateLengthScale = 0.5f;
    Rig r(std::move(song));
    CHECK(r.highUntil(240 * kSamplesPerTick + 10));
    // The tied note itself still plays at the gate length.
    r.runTo(360 * kSamplesPerTick + 2);
    CHECK_FALSE(r.gate());
}

TEST_CASE("engine: a tie carries over the clip's own loop, and stop ends it") {
    Rig r(songOf({{0, kPpq, 60, true}, {kPpq, kPpq, 62}}, 2 * kPpq));
    int64_t wrap = 2 * kPpq * kSamplesPerTick;
    r.runTo(wrap - 10);
    CHECK(r.gate());
    CHECK(r.highUntil(wrap + 100));
    CHECK(r.semis() == doctest::Approx(0));
    r.engine.stop();
    r.step();
    CHECK_FALSE(r.gate());
}

TEST_CASE("engine: chords tie note by note to the nearest pitch") {
    Rig r(songOf({{0, 240, 60}, {0, 240, 64}, {0, 240, 67},
                  {240, 240, 62, true}, {240, 240, 65, true}, {240, 240, 69, true}},
                 4 * kPpq, 3));
    r.runTo(240 * kSamplesPerTick - 1);
    float before[3] = {r.semis(0), r.semis(1), r.semis(2)};
    bool high = true;
    while (r.sample < 240 * kSamplesPerTick + 100) {
        r.step();
        high = high && r.gate(0) && r.gate(1) && r.gate(2);
    }
    CHECK(high);
    // C to D, E to F, G to A.
    for (int v = 0; v < 3; v++)
        CHECK(r.semis(v) - before[v] == doctest::Approx(before[v] == 4 ? 1 : 2));
}

TEST_CASE("VoiceAllocator: Reuse puts a pitch back on the voice that played it last") {
    VoiceAllocator v;
    v.setPolyphony(4);
    v.setAllocation(VoiceAllocation::Reuse);
    CHECK(v.noteOn(60, 1, 0.5f, 10, 1) == 0);
    CHECK(v.noteOn(64, 1, 0.5f, 10, 2) == 1);
    v.release(0);
    v.release(1);
    CHECK(v.noteOn(67, 1, 0.5f, 10, 3) == 2);
    CHECK(v.noteOn(64, 1, 0.5f, 10, 4) == 1);
    CHECK(v.noteOn(60, 1, 0.5f, 10, 5) == 0);
    // Still sounding: it starts again on its own voice.
    CHECK(v.noteOn(60, 1, 0.5f, 10, 6) == 0);
}

TEST_CASE("VoiceAllocator: Reset takes the lowest free voice, and a strummed chord goes low to high") {
    VoiceAllocator v;
    v.setPolyphony(4);
    v.setAllocation(VoiceAllocation::Reset);
    CHECK(v.noteOn(60, 1, 0.5f, 10, 1) == 0);
    v.release(0);
    CHECK(v.noteOn(62, 1, 0.5f, 10, 2) == 0); // Rotate would have moved on to voice 1
    v.release(0);
    // Strummed down: G first (two lower notes to come), then E, then C.
    CHECK(v.noteOn(67, 1, 0.5f, 10, 3, 0, 0, 2) == 2);
    CHECK(v.noteOn(64, 1, 0.5f, 10, 4, 0, 0, 1) == 1);
    CHECK(v.noteOn(60, 1, 0.5f, 10, 5, 0, 0, 0) == 0);
}

TEST_CASE("VoiceAllocator: Rotate starts again from the first voice when play starts") {
    VoiceAllocator v;
    v.setPolyphony(4);
    CHECK(v.noteOn(60, 1, 0.5f, 10, 1) == 0);
    CHECK(v.noteOn(62, 1, 0.5f, 10, 2) == 1);
    v.releaseAll();
    v.restartRotation();
    CHECK(v.noteOn(64, 1, 0.5f, 10, 3) == 0);
}

TEST_CASE("VoiceAllocator: a cut voice is never continued by a tie") {
    VoiceAllocator v;
    v.setPolyphony(1);
    int a = v.noteOn(60, 1, 0.5f, 100, 1, 0, 0);
    CHECK(v.continued(64, 0, 50, 1e-6) == a);
    v.cut(a);
    CHECK(v.continued(64, 0, 50, 1e-6) == -1);
}
