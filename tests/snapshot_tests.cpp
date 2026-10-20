#include "doctest.h"

#include "../src/engine/Engine.hpp"
#include "../src/engine/SnapshotExchange.hpp"
#include "../src/module/PasteRemaps.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <thread>

using namespace tessera;

TEST_CASE("the snapshot builder only rebuilds tracks an edit touched") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    BusSlots slots;
    slots.sync(song);
    SnapshotBuilder builder;

    auto first = builder.build(song, slots, ChangeSet::all());
    CHECK(builder.rebuiltCount() == 4);

    auto same = builder.build(song, slots, ChangeSet());
    CHECK(builder.rebuiltCount() == 0);
    CHECK(same->tracks[2].get() == first->tracks[2].get()); // shared, not copied

    ClipId demo = *song.tracks[0].slots[0].clip;
    CommandPtr move = moveNotes(song, demo, {song.findClip(demo)->notes[0].id}, kPpq, 0);
    move->apply(song);
    auto edited = builder.build(song, slots, move->changes());
    CHECK(builder.rebuiltCount() == 1);
    CHECK(edited->tracks[0].get() != first->tracks[0].get());
    CHECK(edited->tracks[1].get() == first->tracks[1].get());

    CommandPtr mute = setTrackField(song, song.tracks[1].id, &Track::mute, true, "Mute");
    mute->apply(song);
    builder.build(song, slots, mute->changes());
    CHECK(builder.rebuiltCount() == 1);

    // Solo changes whether every other track is heard, so everything rebuilds.
    CommandPtr solo = setTrackField(song, song.tracks[2].id, &Track::solo, true, "Solo");
    solo->apply(song);
    auto soloed = builder.build(song, slots, solo->changes());
    CHECK(builder.rebuiltCount() == 4);
    CHECK_FALSE(soloed->tracks[0]->audible);
    CHECK(soloed->tracks[2]->audible);
}

TEST_CASE("soloing a second track keeps the first heard, and unsoloing one keeps the other") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    BusSlots slots;
    slots.sync(song);
    SnapshotBuilder builder;
    builder.build(song, slots, ChangeSet::all());

    auto soloTrack = [&](size_t i, bool on) {
        CommandPtr c = setTrackField(song, song.tracks[i].id, &Track::solo, on, "Solo");
        c->apply(song);
        return builder.build(song, slots, c->changes());
    };
    soloTrack(0, true);
    auto both = soloTrack(2, true); // nothing else changes, so only track 3 is rebuilt
    CHECK(builder.rebuiltCount() == 1);
    CHECK(both->tracks[0]->audible);
    CHECK_FALSE(both->tracks[1]->audible);
    CHECK(both->tracks[2]->audible);

    auto one = soloTrack(0, false);
    CHECK_FALSE(one->tracks[0]->audible);
    CHECK(one->tracks[2]->audible);
}

TEST_CASE("a track that switches to another clip is rebuilt even if neither clip changed") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    BusSlots slots;
    slots.sync(song);
    SnapshotBuilder builder;
    builder.build(song, slots, ChangeSet::all());

    song.tracks[1].slots[0].clip = song.tracks[0].slots[0].clip;
    auto snap = builder.build(song, slots, ChangeSet());
    CHECK(builder.rebuiltCount() == 1);
    CHECK(snap->tracks[1]->slot(0) != nullptr);
}

TEST_CASE("the exchange frees only snapshots the engine has moved past") {
    SnapshotExchange exchange;
    auto make = [] { return std::make_unique<Snapshot>(); };

    exchange.publish(make());
    const Snapshot* a = exchange.poll();
    REQUIRE(a != nullptr);
    CHECK(exchange.poll() == nullptr); // nothing new

    exchange.publish(make());
    exchange.publish(make());
    CHECK(exchange.liveCount() == 3); // the engine still holds the first

    const Snapshot* c = exchange.poll();
    CHECK(c == exchange.latest());
    CHECK(c->generation == 3);
    exchange.collect();
    CHECK(exchange.liveCount() == 1); // the second was never picked up and is unreachable now
}

TEST_CASE("swapping snapshots while playing keeps sounding notes") {
    // One track, one long note. The edit adds a second track and changes nothing about the first.
    Song song;
    song.scenes.resize(1);
    song.tracks.push_back(makeTrack(song, TrackType::Instrument, "A"));
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    Note n;
    n.id = song.newId<NoteId>();
    n.start = 0;
    n.length = 2 * kPpq;
    clip.notes.push_back(n);
    song.tracks[0].slots[0].clip = clip.id;
    song.clips.emplace(clip.id, clip);

    BusSlots slots;
    slots.sync(song);
    SnapshotBuilder builder;
    auto before = builder.build(song, slots, ChangeSet::all());

    Engine engine;
    engine.setSampleRate(48000);
    engine.setSnapshot(before.get());
    engine.launch({LaunchEvent::Kind::Scene, 0, 0, true});
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    for (int i = 0; i < 1000; i++)
        engine.process(frames.data(), events);
    int slotA = slots.slotOf(song.tracks[0].id);
    REQUIRE(frames[slotA].gate[0] > 5);

    song.tracks.insert(song.tracks.begin(), makeTrack(song, TrackType::Instrument, "New"));
    slots.sync(song);
    auto after = builder.build(song, slots, ChangeSet::all());
    engine.setSnapshot(after.get());
    engine.process(frames.data(), events);
    CHECK(frames[slotA].gate[0] > 5); // track A moved to index 1 and kept its voice

    // Muting through a snapshot swap releases the track's gates at once.
    song.tracks[1].mute = true;
    auto muted = builder.build(song, slots, ChangeSet::track(song.tracks[1].id));
    engine.setSnapshot(muted.get());
    engine.process(frames.data(), events);
    CHECK(frames[slotA].gate[0] == 0);

    // A track removed from the snapshot stops owning its bus slot.
    song.tracks.erase(song.tracks.begin() + 1);
    slots.sync(song);
    auto removed = builder.build(song, slots, ChangeSet::all());
    engine.setSnapshot(removed.get());
    engine.process(frames.data(), events);
    CHECK(frames[slotA].trackId == 0);
}

TEST_CASE("paste records link a pasted output module to the Tessera pasted with it") {
    PasteRemaps remaps;
    remaps.record(10, 42, 100.0);
    CHECK(remaps.find(10, 100.0) == std::optional<int64_t>(42));
    CHECK_FALSE(remaps.find(10, 100.5).has_value()); // a different frame: pasted on its own
    CHECK_FALSE(remaps.find(11, 100.0).has_value());
    CHECK_FALSE(remaps.find(10, NAN).has_value());

    remaps.record(20, 43, 200.0); // old records expire
    CHECK_FALSE(remaps.find(10, 100.0).has_value());
}

TEST_CASE("the exchange never frees a snapshot the engine thread is reading") {
    SnapshotExchange exchange;
    std::atomic<bool> done{false};
    std::atomic<int> bad{0};

    std::thread audio([&] {
        const Snapshot* current = nullptr;
        while (!done.load()) {
            if (const Snapshot* next = exchange.poll())
                current = next;
            // Read through the pointer like the engine does; ASan/TSan flag any use after free.
            if (current && current->loopEnd != current->loopStart + Tick(current->generation))
                bad++;
        }
    });
    for (uint64_t g = 1; g <= 5000; g++) {
        auto s = std::make_unique<Snapshot>();
        s->loopStart = 0;
        s->loopEnd = Tick(g); // published as generation g
        exchange.publish(std::move(s));
    }
    done.store(true);
    audio.join();
    CHECK(bad.load() == 0);
    exchange.poll(); // the engine catches up with the last publish
    exchange.collect();
    CHECK(exchange.liveCount() == 1);
}

TEST_CASE("the song's swing moves off-beat sixteenths later when playing") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    ClipId demo = *song.tracks[0].slots[0].clip;
    Clip& clip = *song.findClip(demo);
    Note n = clip.notes[0];
    n.id = song.newId<NoteId>();
    n.start = kPpq / 4; // the second 1/16
    n.length = kPpq / 8;
    n.pitch = 127;
    clip.notes = {n};
    BusSlots slots;
    slots.sync(song);
    auto straight = buildSnapshot(song, slots);
    CHECK(straight->tracks[0]->slot(0)->notes[0].start == kPpq / 4);
    song.swing = 1;
    auto swungSnap = buildSnapshot(song, slots);
    CHECK(swungSnap->tracks[0]->slot(0)->notes[0].start == kPpq / 4 + kPpq / 8);
    // The model stays as written.
    CHECK(song.findClip(demo)->notes[0].start == kPpq / 4);
}

TEST_CASE("snapshot notes carry their channel, from slots and from the timeline") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    ClipId demo = *song.tracks[0].slots[0].clip;
    for (Note& n : song.clips[demo].notes)
        n.channel = 5;
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = demo;
    a.length = song.clips[demo].length;
    song.tracks[0].arrangement.push_back(a);
    BusSlots slots;
    slots.sync(song);
    SnapshotBuilder builder;
    auto snap = builder.build(song, slots, ChangeSet::all());
    const TrackPlayback& tp = *snap->tracks[0];
    REQUIRE(bool(tp.slots[0]));
    REQUIRE_FALSE(tp.slots[0]->notes.empty());
    REQUIRE_FALSE(tp.arrangement.empty());
    for (const NoteEvent& e : tp.slots[0]->notes)
        CHECK(e.channel == 5);
    for (const NoteEvent& e : tp.arrangement)
        CHECK(e.channel == 5);
}
