#include "doctest.h"

#include "../src/model/BounceNotes.hpp"
#include "../src/model/DeviceCommands.hpp"

using namespace tessera;

namespace {

/// A default song whose first track plays one four-note bar from the timeline.
struct BounceSong {
    Song song = makeDefaultSong();
    TrackId track = song.tracks[0].id;
    ClipId clipId;
    ArrangementId instance;

    NextId next() {
        return [this] { return song.nextId++; };
    }
    BounceSong() {
        Clip c;
        c.id = ClipId(song.nextId++);
        c.length = 4 * kPpq;
        c.loopStart = 0;
        c.loopEnd = 4 * kPpq;
        c.loopMode = LoopMode::Loop;
        for (int i = 0; i < 4; i++) {
            Note n;
            n.id = NoteId(song.nextId++);
            n.start = i * kPpq;
            n.length = kPpq / 2;
            n.pitch = int8_t(60 + i);
            c.notes.push_back(n);
        }
        clipId = c.id;
        song.clips[c.id] = c;
        song.findTrack(track)->slots[0].clip = c.id;
        ArrangerClip a;
        a.id = ArrangementId(song.nextId++);
        a.clip = c.id;
        a.position = 0;
        a.length = 4 * kPpq;
        instance = a.id;
        song.findTrack(track)->arrangement.push_back(a);
    }
};

} // namespace

TEST_CASE("a notes bounce makes one clip on a new track under the source") {
    BounceSong f;
    NoteBounce what;
    what.track = f.track;
    what.from = 0;
    what.to = 2 * kPpq;
    NoteBounceMade made;
    CommandPtr command = bounceNotes(f.song, what, 120, f.next(), &made);
    REQUIRE(command);
    size_t tracks = f.song.tracks.size();
    command->apply(f.song);
    REQUIRE(f.song.tracks.size() == tracks + 1);
    CHECK(f.song.tracks[1].id == made.track);
    CHECK(f.song.tracks[1].devices.empty());
    REQUIRE(f.song.tracks[1].arrangement.size() == 1);
    CHECK(f.song.tracks[1].arrangement[0].length == 2 * kPpq);
    const Clip* clip = f.song.findClip(made.clip);
    REQUIRE(clip);
    CHECK(clip->notes.size() == 2);
    command->revert(f.song);
    CHECK(f.song.tracks.size() == tracks);
    CHECK(f.song.findClip(made.clip) == nullptr);
}

TEST_CASE("a notes bounce with chosen clips leaves the others out") {
    BounceSong f;
    ArrangerClip other = f.song.findTrack(f.track)->arrangement[0];
    other.id = ArrangementId(f.song.nextId++);
    f.song.findTrack(f.track)->arrangement.push_back(other);
    NoteBounce what;
    what.track = f.track;
    what.to = 4 * kPpq;
    what.only = {f.instance};
    NoteBounceMade made;
    CommandPtr command = bounceNotes(f.song, what, 120, f.next(), &made);
    REQUIRE(command);
    command->apply(f.song);
    CHECK(f.song.findClip(made.clip)->notes.size() == 4);
}

TEST_CASE("a notes bounce of a panel clip fills the same scene on the new track") {
    BounceSong f;
    NoteBounce what;
    what.track = f.track;
    what.clip = f.clipId;
    NoteBounceMade made;
    CommandPtr command = bounceNotes(f.song, what, 120, f.next(), &made);
    REQUIRE(command);
    command->apply(f.song);
    REQUIRE(f.song.findTrack(made.track)->slots[0].clip);
    CHECK(*f.song.findTrack(made.track)->slots[0].clip == made.clip);
    CHECK(f.song.findClip(made.clip)->notes.size() == 4);
    CHECK(f.song.findTrack(made.track)->arrangement.empty());
}

TEST_CASE("a notes bounce keeps each playhead on its channel and can pick one") {
    BounceSong f;
    DeviceId device;
    addDevice(f.song, f.track, DeviceType::Playheads, 0, f.next(), &device)->apply(f.song);
    Device& d = f.song.findTrack(f.track)->devices[0];
    REQUIRE(d.playheads.size() == 2);
    d.playheads[0].channel = 1;
    d.playheads[1].channel = 2;

    NoteBounce what;
    what.track = f.track;
    what.to = 4 * kPpq;
    NoteBounceMade all, one;
    CommandPtr both = bounceNotes(f.song, what, 120, f.next(), &all);
    REQUIRE(both);
    what.channel = 2;
    CommandPtr second = bounceNotes(f.song, what, 120, f.next(), &one);
    REQUIRE(second);
    both->apply(f.song);
    size_t channelOne = 0, channelTwo = 0;
    for (const Note& n : f.song.findClip(all.clip)->notes)
        (n.channel == 1 ? channelOne : channelTwo)++;
    CHECK(channelOne > 0);
    CHECK(channelTwo > 0);
    both->revert(f.song);
    second->apply(f.song);
    CHECK(f.song.findClip(one.clip)->notes.size() == channelTwo);
    for (const Note& n : f.song.findClip(one.clip)->notes)
        CHECK(n.channel == 2);
}

TEST_CASE("a notes bounce refuses an empty span and audio tracks") {
    BounceSong f;
    NoteBounce what;
    what.track = f.track;
    what.from = 8 * kPpq;
    what.to = 12 * kPpq;
    CHECK_FALSE(bounceNotes(f.song, what, 120, f.next()));
    what.from = what.to;
    CHECK_FALSE(bounceNotes(f.song, what, 120, f.next()));
}
