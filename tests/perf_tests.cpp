#include "doctest.h"

#include "../src/engine/Engine.hpp"
#include "../src/model/DeviceTypes.hpp"

#include <algorithm>
#include <chrono>

using namespace tessera;

namespace {

/// `tracks` tracks, each looping a bar of 4-note chords on every 1/16, with automation, all launched; with
/// `playheads`, a Playheads device reads each clip with that many playheads.
double secondOfAudio(int tracks, bool busy = true, bool automation = true, int playheads = 0) {
    Song song;
    for (int t = 0; t < tracks; t++) {
        Track track;
        track.id = song.newId<TrackId>();
        track.polyphony = 8;
        track.slots.resize(1);
        Clip clip;
        clip.id = song.newId<ClipId>();
        clip.length = clip.loopEnd = 4 * kPpq;
        for (int step = 0; step < (busy ? 16 : 0); step++)
            for (int v = 0; v < 4; v++) {
                Note n;
                n.id = song.newId<NoteId>();
                n.start = step * kPpq / 4;
                n.length = kPpq / 8;
                n.pitch = int8_t(48 + v * 4 + step % 5);
                n.ops.chance = (step % 3 == 0) ? 0.7f : 1.f;
                clip.notes.push_back(n);
            }
        std::sort(clip.notes.begin(), clip.notes.end(), noteOrder);
        AutomationEnvelope lane;
        lane.id = song.newId<AutomationId>();
        lane.outputIndex = 0;
        lane.points = {{0, 0, CurveType::Linear, 0}, {4 * kPpq, 1, CurveType::Linear, 0}};
        if (automation)
            clip.automation.push_back(lane);
        track.slots[0].clip = clip.id;
        if (playheads > 0) {
            Device d = devices::makeDevice(DeviceType::Playheads, song.newId<DeviceId>());
            for (int i = 0; i < playheads; i++)
                d.playheads.push_back(devices::makePlayhead(song.newId<PlayheadId>(), i));
            track.devices.push_back(d);
        }
        song.clips.emplace(clip.id, clip);
        song.tracks.push_back(track);
    }
    BusSlots slots;
    slots.sync(song);
    auto snapshot = buildSnapshot(song, slots);
    Engine engine;
    engine.setSampleRate(48000);
    engine.setTempo(140);
    engine.setSnapshot(snapshot.get());
    engine.launch({LaunchEvent::Kind::Scene, 0, 0, true});
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    auto start = std::chrono::steady_clock::now();
    for (int s = 0; s < 48000; s++) {
        engine.setPlayMode(PlayMode::Launcher); // the module sets the mode on every sample
        engine.process(frames.data(), events);
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return seconds * 100; // share of one core, in percent, for one second of audio
}

/// The best of three runs: other work on the machine only ever makes a run slower.
double bestOf3(int tracks, bool busy = true, bool automation = true, int playheads = 0) {
    double best = secondOfAudio(tracks, busy, automation, playheads);
    for (int i = 0; i < 2; i++)
        best = std::min(best, secondOfAudio(tracks, busy, automation, playheads));
    return best;
}

} // namespace

TEST_CASE("performance: the engine stays inside its CPU budget (DESIGN.md §18)") {
    double busy16 = bestOf3(16);
    double busy64 = bestOf3(64);
    double idle64 = bestOf3(64, false, false);
    double notesOnly64 = bestOf3(64, true, false);
    double heads16 = bestOf3(16, true, true, 4);
    double heads16x16 = bestOf3(16, true, true, 16);
    MESSAGE("16 busy tracks with 4 playheads each: " << heads16 << "%, with 16 each: " << heads16x16 << "%");
    MESSAGE("16 busy tracks: " << busy16 << "% of a core, 64: " << busy64 << "%; 64 without automation: " << notesOnly64
                               << "%, 64 idle: " << idle64 << "%");
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
    CHECK(busy16 < 1);
    CHECK(busy64 < 3);
    CHECK(heads16 < 8); // four playheads read four times the notes: about four busy tracks' worth, no more
#else
    CHECK(busy64 < 400); // unoptimized with sanitizers: only a guard against something quadratic
#endif
}
