#include "doctest.h"

#include "../src/model/Json.hpp"
#include "../src/model/LauncherCommands.hpp"
#include "../src/model/Serialize.hpp"

using namespace tessera;

namespace {

NextId idsFrom(Song& song) {
    return [&song] { return song.nextId++; };
}

void checkSymmetric(Song& song, Command& c) {
    Song original = song;
    c.apply(song);
    Song applied = song;
    CHECK(applied != original);
    c.revert(song);
    Song expected = original;
    expected.nextId = song.nextId;
    REQUIRE(song == expected);
    c.apply(song);
    CHECK(song == applied);
}

} // namespace

TEST_CASE("scenes: insert adds a slot on every track, remove takes its clips with it") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    CommandPtr insert = insertScene(song, 0);
    REQUIRE(insert);
    checkSymmetric(song, *insert);
    CHECK(song.scenes.size() == kDefaultScenes + 1);
    for (const Track& t : song.tracks)
        CHECK(t.slots.size() == song.scenes.size());
    // The demo clip moved down to scene 2.
    ClipId demo = *song.tracks[0].slots[1].clip;

    CommandPtr remove = removeScene(song, 1);
    REQUIRE(remove);
    checkSymmetric(song, *remove);
    CHECK(song.findClip(demo) == nullptr);
    remove->revert(song);
    CHECK(song.findClip(demo) != nullptr);
    CHECK(song.tracks[0].slots[1].clip == demo);

    Scene changed = song.scenes[0];
    changed.name = "Verse";
    changed.tempo = 128;
    CommandPtr set = setScene(song, 0, changed, "Rename scene");
    REQUIRE(set);
    checkSymmetric(song, *set);
}

TEST_CASE("scenes: never fewer than one, never more than 64") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    while (song.scenes.size() > 1)
        removeScene(song, 0)->apply(song);
    CHECK(removeScene(song, 0) == nullptr);
    while (song.scenes.size() < size_t(kMaxScenes))
        insertScene(song, 0)->apply(song);
    CHECK(insertScene(song, 0) == nullptr);
}

TEST_CASE("slots: move, copy and delete clips") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    TrackId a = song.tracks[0].id, b = song.tracks[1].id;
    ClipId demo = *song.tracks[0].slots[0].clip;

    CommandPtr move = moveSlotClip(song, a, 0, b, 2);
    REQUIRE(move);
    checkSymmetric(song, *move);
    CHECK_FALSE(song.tracks[0].slots[0].clip.has_value());
    CHECK(song.tracks[1].slots[2].clip == demo);

    CommandPtr copy = copySlotClip(song, b, 2, b, 3, idsFrom(song));
    REQUIRE(copy);
    checkSymmetric(song, *copy);
    ClipId copied = *song.tracks[1].slots[3].clip;
    CHECK(copied != demo);
    CHECK(song.findClip(copied)->notes.size() == song.findClip(demo)->notes.size());

    // Moving onto an occupied slot replaces (and drops) the clip that was there.
    CommandPtr over = moveSlotClip(song, b, 3, b, 2);
    REQUIRE(over);
    checkSymmetric(song, *over);
    CHECK(song.tracks[1].slots[2].clip == copied);
    CHECK(song.findClip(demo) == nullptr);

    CommandPtr del = deleteSlotClip(song, b, 2);
    REQUIRE(del);
    checkSymmetric(song, *del);
    CHECK(song.findClip(copied) == nullptr);
    CHECK(deleteSlotClip(song, b, 2) == nullptr);
}

TEST_CASE("a clip in two slots stays in the pool until its last slot is cleared") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    TrackId a = song.tracks[0].id;
    ClipId demo = *song.tracks[0].slots[0].clip;
    song.tracks[0].slots[1].clip = demo;
    deleteSlotClip(song, a, 0)->apply(song);
    CHECK(song.findClip(demo) != nullptr);
    deleteSlotClip(song, a, 1)->apply(song);
    CHECK(song.findClip(demo) == nullptr);
}

TEST_CASE(".tessera-clip files round-trip and import with fresh IDs") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    const Clip& demo = *song.findClip(*song.tracks[0].slots[0].clip);
    json::Ref file(clipFileToJson(demo));
    std::optional<Clip> loaded = clipFileFromJson(file.get());
    REQUIRE(loaded);
    CHECK(loaded->notes == demo.notes);
    CHECK(loaded->name == demo.name);

    CommandPtr import = importClipToSlot(song, song.tracks[2].id, 4, *loaded, idsFrom(song));
    REQUIRE(import);
    checkSymmetric(song, *import);
    ClipId imported = *song.tracks[2].slots[4].clip;
    CHECK(imported != demo.id);
    CHECK(song.findClip(imported)->notes.size() == demo.notes.size());

    json::Ref notAClip(json_object());
    CHECK_FALSE(clipFileFromJson(notAClip.get()).has_value());
}

TEST_CASE("duplicate puts a copy in the slot below, inserting a scene when that slot is taken") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    TrackId track = song.tracks[0].id;
    ClipId demo = *song.tracks[0].slots[0].clip;
    size_t scenes = song.scenes.size();

    CommandPtr intoEmpty = duplicateSlotClip(song, track, 0, idsFrom(song));
    REQUIRE(intoEmpty);
    checkSymmetric(song, *intoEmpty);
    CHECK(song.scenes.size() == scenes);
    REQUIRE(song.tracks[0].slots[1].clip);
    ClipId copy = *song.tracks[0].slots[1].clip;
    CHECK(copy != demo);
    CHECK(song.findClip(copy)->notes.size() == song.findClip(demo)->notes.size());

    // Slot 1 is now taken: duplicating slot 0 again makes room with a new scene.
    CommandPtr withScene = duplicateSlotClip(song, track, 0, idsFrom(song));
    REQUIRE(withScene);
    checkSymmetric(song, *withScene);
    CHECK(song.scenes.size() == scenes + 1);
    CHECK(song.tracks[0].slots[2].clip == copy);
    CHECK(song.tracks[0].slots[1].clip != copy);
    for (const Track& t : song.tracks)
        CHECK(t.slots.size() == song.scenes.size());

    CHECK(duplicateSlotClip(song, song.tracks[1].id, 0, idsFrom(song)) == nullptr); // empty slot
}

TEST_CASE("a clip's loop can be set longer or shorter, and doubled with its notes") {
    Song song;
    Clip c;
    c.id = song.newId<ClipId>();
    c.length = 4 * kPpq;
    c.loopEnd = 4 * kPpq;
    Note n;
    n.id = song.newId<NoteId>();
    n.start = kPpq;
    n.length = 100;
    n.pitch = 36;
    c.notes.push_back(n);
    song.clips[c.id] = c;
    Song before = song;

    CommandPtr longer = setLoopLength(song, c.id, 8 * kPpq);
    REQUIRE(longer);
    CHECK(longer->name() == "Clip length 2 bars");
    longer->apply(song);
    CHECK(song.findClip(c.id)->loopEnd == 8 * kPpq);
    CHECK(song.findClip(c.id)->length == 8 * kPpq);
    longer->revert(song);
    CHECK(song == before);
    CHECK(setLoopLength(song, c.id, 4 * kPpq) == nullptr);

    CommandPtr twice = doubleLoop(song, c.id, [&] { return song.nextId++; });
    twice->apply(song);
    const Clip* d = song.findClip(c.id);
    CHECK(d->loopEnd == 8 * kPpq);
    REQUIRE(d->notes.size() == 2);
    CHECK(d->notes[1].start == 5 * kPpq);
    CHECK(d->notes[1].id != n.id);
    twice->revert(song);
    song.nextId = before.nextId;
    CHECK(song == before);
}

TEST_CASE("a clip that doesn't loop gets a length of its own, which undoes") {
    Song song;
    Clip c;
    c.id = song.newId<ClipId>();
    c.length = 4 * kPpq;
    c.loopEnd = 4 * kPpq;
    song.clips[c.id] = c;
    CHECK(setClipLength(song, c.id, 16 * kPpq) == nullptr); // it loops: that is the loop length's job
    song.clips[c.id].loopMode = LoopMode::Off;
    Song before = song;
    CommandPtr longer = setClipLength(song, c.id, 16 * kPpq);
    REQUIRE(longer);
    CHECK(longer->name() == "Clip length 4 bars");
    longer->apply(song);
    CHECK(song.findClip(c.id)->length == 16 * kPpq);
    CHECK(song.findClip(c.id)->loopEnd == 4 * kPpq); // the loop it gets when turned on again stays
    longer->revert(song);
    CHECK(song == before);
    CHECK(setClipLength(song, c.id, 4 * kPpq) == nullptr);
}
