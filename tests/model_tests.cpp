#include "doctest.h"

#include "../src/engine/Snapshot.hpp"
#include "../src/model/Song.hpp"

#include <algorithm>
#include <set>

using namespace tessera;

TEST_CASE("bar.beat.tick positions are 1-based and floor negative ticks") {
    TimeSig ts{4, 4};
    auto bbt = toBarBeatTick(0, ts);
    CHECK(bbt.bar == 1);
    CHECK(bbt.beat == 1);
    CHECK(bbt.tick == 0);

    bbt = toBarBeatTick(ticksPerBar(ts) + kPpq * 2 + 5, ts);
    CHECK(bbt.bar == 2);
    CHECK(bbt.beat == 3);
    CHECK(bbt.tick == 5);

    bbt = toBarBeatTick(-1, ts);
    CHECK(bbt.bar == 0);
    CHECK(bbt.beat == 4);
    CHECK(bbt.tick == kPpq - 1);

    CHECK(ticksPerBar(TimeSig{6, 8}) == 6 * kPpq / 2);
}

TEST_CASE("default song: 3 instrument tracks, 1 drum track, no clips; the demo clip helper") {
    Song song = makeDefaultSong();
    CHECK(song.clips.empty()); // Trello: "Let's not initialize with a clip"
    REQUIRE(song.tracks.size() == 4);
    CHECK(std::count_if(song.tracks.begin(), song.tracks.end(),
                        [](const Track& t) { return t.type == TrackType::Instrument; }) == 3);
    CHECK(song.tracks.back().type == TrackType::Drum);

    std::set<uint64_t> ids;
    for (const Track& t : song.tracks)
        ids.insert(t.id.value);
    CHECK(ids.size() == 4);

    addDemoClip(song);
    REQUIRE(song.tracks[0].slots.at(0).clip.has_value());
    const Clip* clip = song.findClip(*song.tracks[0].slots[0].clip);
    REQUIRE(clip != nullptr);
    CHECK(!clip->notes.empty());
    CHECK(std::is_sorted(clip->notes.begin(), clip->notes.end(), noteOrder));
    for (const Note& n : clip->notes) {
        CHECK(n.length >= 1);
        CHECK(n.end() <= clip->loopEnd);
    }
}

TEST_CASE("mute and solo decide which tracks are heard") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    Track& a = song.tracks[0];
    Track& b = song.tracks[1];
    CHECK(song.isAudible(a));

    a.mute = true;
    CHECK_FALSE(song.isAudible(a));

    a.mute = false;
    b.solo = true;
    CHECK_FALSE(song.isAudible(a));
    CHECK(song.isAudible(b));

    b.mute = true; // mute beats solo
    CHECK_FALSE(song.isAudible(b));
}

TEST_CASE("bus slots stay put when tracks move, and are freed when tracks go") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    BusSlots slots;
    slots.sync(song);

    TrackId first = song.tracks[0].id;
    TrackId last = song.tracks[3].id;
    int firstSlot = slots.slotOf(first);
    int lastSlot = slots.slotOf(last);
    CHECK(firstSlot >= 0);
    CHECK(firstSlot != lastSlot);

    std::swap(song.tracks[0], song.tracks[3]);
    slots.sync(song);
    CHECK(slots.slotOf(first) == firstSlot);
    CHECK(slots.slotOf(last) == lastSlot);

    song.tracks.erase(song.tracks.begin() + 3); // removes `first`
    slots.sync(song);
    CHECK(slots.slotOf(first) == -1);

    Track added;
    added.id = song.newId<TrackId>();
    song.tracks.push_back(added);
    slots.sync(song);
    CHECK(slots.slotOf(added.id) == firstSlot); // the freed slot is reused
}

TEST_CASE("snapshot flattens notes: muted dropped, transpose and gate scale applied, cut at loop end") {
    Song song;
    Track track;
    track.id = song.newId<TrackId>();
    track.transpose = 12;
    track.gateLengthScale = 0.5f;
    track.slots.resize(1);

    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = 4 * kPpq;
    clip.loopStart = 0;
    clip.loopEnd = 2 * kPpq;
    auto addNote = [&](Tick start, Tick length, int pitch, bool muted = false) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = start;
        n.length = length;
        n.pitch = int8_t(pitch);
        n.muted = muted;
        clip.notes.push_back(n);
    };
    addNote(0, kPpq, 60);
    addNote(kPpq / 2, kPpq, 62, true); // muted
    addNote(kPpq, 4 * kPpq, 64);       // runs past the loop end
    addNote(3 * kPpq, kPpq, 65);       // starts after the loop end
    addNote(0, kPpq, 120);             // transposes past 127

    track.slots[0].clip = clip.id;
    song.clips.emplace(clip.id, clip);
    song.tracks.push_back(track);

    BusSlots slots;
    slots.sync(song);
    auto snap = buildSnapshot(song, slots);
    REQUIRE(snap->tracks.size() == 1);
    REQUIRE(snap->tracks[0]->slot(0) != nullptr);
    const auto& notes = snap->tracks[0]->slot(0)->notes;
    REQUIRE(notes.size() == 3);

    CHECK(notes[0].start == 0);
    CHECK(notes[0].pitch == 72);
    CHECK(notes[0].end == kPpq / 2);
    CHECK(notes[1].pitch == 127);
    CHECK(notes[2].start == kPpq);
    CHECK(notes[2].end == 2 * kPpq);
}

TEST_CASE("tracks without a clip, or with an empty loop, have no playback") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    BusSlots slots;
    slots.sync(song);
    auto snap = buildSnapshot(song, slots);
    CHECK(snap->tracks[0]->slot(0) != nullptr);
    CHECK(snap->tracks[1]->slot(0) == nullptr);

    Clip& clip = song.clips.begin()->second;
    clip.loopEnd = clip.loopStart;
    snap = buildSnapshot(song, slots);
    CHECK(snap->tracks[0]->slot(0) == nullptr);
}
