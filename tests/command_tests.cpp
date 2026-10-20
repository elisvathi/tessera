#include "doctest.h"

#include "../src/model/Commands.hpp"
#include "RandomSong.hpp"

using namespace tessera;

namespace {

/// Applies, checks the song changed, reverts, checks it is back exactly; then redo gives the same result.
void checkSymmetric(Song& song, Command& command) {
    const Song original = song;
    command.apply(song);
    const Song applied = song;
    CHECK(applied != original);
    command.revert(song);
    REQUIRE(song == original);
    command.apply(song);
    CHECK(song == applied);
}

struct Fixture {
    Song song = testing::RandomSong(7).make();
    ClipId clip;
    std::vector<NoteId> ids;

    Fixture() {
        for (auto& [id, c] : song.clips) {
            if (c.notes.size() >= 4) {
                clip = id;
                break;
            }
        }
        REQUIRE(clip.valid());
        const Clip& c = *song.findClip(clip);
        for (size_t i = 0; i < c.notes.size(); i += 2)
            ids.push_back(c.notes[i].id);
    }
};

} // namespace

TEST_CASE("note commands are reversible") {
    Fixture f;

    SUBCASE("add") {
        std::vector<Note> notes = {testing::RandomSong(1).note(f.song), testing::RandomSong(2).note(f.song)};
        CommandPtr c = addNotes(f.song, f.clip, notes);
        REQUIRE(c);
        CHECK(c->name() == "Add 2 notes");
        checkSymmetric(f.song, *c);
    }
    SUBCASE("delete") {
        CommandPtr c = deleteNotes(f.song, f.clip, f.ids);
        REQUIRE(c);
        checkSymmetric(f.song, *c);
        CHECK(f.song.findClip(f.clip)->findNote(f.ids[0]) == nullptr);
    }
    SUBCASE("move") {
        CommandPtr c = moveNotes(f.song, f.clip, f.ids, kPpq, 3);
        REQUIRE(c);
        CHECK(c->name() == "Move " + countNotes(f.ids.size()));
        checkSymmetric(f.song, *c);
    }
    SUBCASE("resize") {
        CommandPtr c = resizeNotes(f.song, f.clip, f.ids, 10, kPpq);
        REQUIRE(c);
        checkSymmetric(f.song, *c);
    }
    SUBCASE("velocity") {
        CommandPtr c = setVelocity(f.song, f.clip, f.ids, 0.123f);
        REQUIRE(c);
        checkSymmetric(f.song, *c);
        CHECK(f.song.findClip(f.clip)->findNote(f.ids[0])->velocity == 0.123f);
    }
}

TEST_CASE("note commands keep the clip sorted and clamp to valid values") {
    Song song;
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    song.clips.emplace(clip.id, clip);

    Note a;
    a.start = 2 * kPpq;
    a.pitch = 126;
    Note b;
    b.start = 0;
    b.pitch = 60;
    CommandPtr add = addNotes(song, clip.id, {a, b});
    add->apply(song);
    const Clip& c = *song.findClip(clip.id);
    REQUIRE(c.notes.size() == 2);
    CHECK(c.notes[0].start == 0);

    NoteId high = c.notes[1].id;
    CommandPtr move = moveNotes(song, clip.id, {high}, -10 * kPpq, 5);
    move->apply(song);
    const Note* moved = song.findClip(clip.id)->findNote(high);
    CHECK(moved->start == 0);
    CHECK(moved->pitch == 127);

    CommandPtr shrink = resizeNotes(song, clip.id, {high}, 0, -100 * kPpq);
    shrink->apply(song);
    CHECK(song.findClip(clip.id)->findNote(high)->length == 1);
}

TEST_CASE("edits that change nothing record nothing") {
    Fixture f;
    CHECK(moveNotes(f.song, f.clip, f.ids, 0, 0) == nullptr);
    CHECK(deleteNotes(f.song, f.clip, {}) == nullptr);
    CHECK(addNotes(f.song, f.clip, {}) == nullptr);
    const Track& t = f.song.tracks[0];
    CHECK(setTrackField(f.song, t.id, &Track::mute, t.mute, "same") == nullptr);
    CHECK(replaceNotes("same", f.clip, f.song.findClip(f.clip)->notes, f.song.findClip(f.clip)->notes) == nullptr);
}

TEST_CASE("replaceNotes stores only the notes that differ") {
    Fixture f;
    std::vector<Note> before = f.song.findClip(f.clip)->notes;
    std::vector<Note> after = before;
    after[1].velocity = 0.01f;
    after.pop_back();
    CommandPtr c = replaceNotes("Edit", f.clip, before, after);
    REQUIRE(c);
    CHECK(c->changes().clips == std::vector<ClipId>{f.clip});
    checkSymmetric(f.song, *c);
    CHECK(f.song.findClip(f.clip)->notes == after);
}

TEST_CASE("property commands are reversible") {
    Fixture f;
    TrackId track = f.song.tracks[0].id;

    CommandPtr mute = setTrackField(f.song, track, &Track::mute, !f.song.tracks[0].mute, "Mute");
    REQUIRE(mute);
    CHECK(mute->changes().tracks == std::vector<TrackId>{track});
    checkSymmetric(f.song, *mute);

    CommandPtr rename = setTrackField<std::string>(f.song, track, &Track::name, "Bass", "Rename");
    REQUIRE(rename);
    checkSymmetric(f.song, *rename);

    CommandPtr loop = setClipField<Tick>(f.song, f.clip, &Clip::loopEnd, 123, "Set loop end");
    REQUIRE(loop);
    checkSymmetric(f.song, *loop);

    CommandPtr ts = setSongField(f.song, &Song::timeSig, TimeSig{7, 8}, "Set time signature");
    REQUIRE(ts);
    CHECK(ts->changes().everything);
    checkSymmetric(f.song, *ts);
}

TEST_CASE("a compound command is one step that reverts in reverse order") {
    Fixture f;
    auto compound = std::make_unique<CompoundCommand>("Move and mute");
    compound->add(moveNotes(f.song, f.clip, f.ids, kPpq, 0));
    TrackId track = f.song.tracks[0].id;
    compound->add(setTrackField(f.song, track, &Track::solo, !f.song.tracks[0].solo, "Solo"));
    ChangeSet changes = compound->changes();
    CHECK(changes.clips.size() == 1);
    CHECK(changes.tracks.size() == 1);
    checkSymmetric(f.song, *compound);
}

TEST_CASE("a long random edit history undoes back to the start") {
    Song song = testing::RandomSong(99).make();
    const Song original = song;
    testing::RandomSong notes(5);
    std::mt19937 rng(3);
    std::vector<CommandPtr> history;

    for (int step = 0; step < 200; step++) {
        auto it = song.clips.begin();
        std::advance(it, rng() % song.clips.size());
        ClipId clip = it->first;
        std::vector<NoteId> some;
        for (const Note& n : it->second.notes)
            if (rng() % 3 == 0)
                some.push_back(n.id);

        CommandPtr c;
        switch (rng() % 5) {
        case 0:
            c = addNotes(song, clip, {notes.note(song)});
            break;
        case 1:
            c = deleteNotes(song, clip, some);
            break;
        case 2:
            c = moveNotes(song, clip, some, Tick(rng() % 960) - 480, int(rng() % 7) - 3);
            break;
        case 3:
            c = resizeNotes(song, clip, some, 0, Tick(rng() % 480) - 240);
            break;
        case 4:
            c = setTrackField(song, song.tracks[0].id, &Track::mute, !song.tracks[0].mute, "Mute");
            break;
        }
        if (!c)
            continue;
        c->apply(song);
        history.push_back(std::move(c));
    }
    REQUIRE(history.size() > 100);
    for (auto it = history.rbegin(); it != history.rend(); ++it)
        (*it)->revert(song);
    // Undo never hands IDs back, so the ID counter is the one thing allowed to differ.
    Song expected = original;
    expected.nextId = song.nextId;
    CHECK(song == expected);
}

TEST_CASE("creating a clip in an empty slot is reversible") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    TrackId track = song.tracks[1].id;
    CommandPtr c = createClip(song, track, 2, kPpq * 4, song.newId<ClipId>());
    REQUIRE(c);
    checkSymmetric(song, *c);
    const Clip* clip = song.activeClip(*song.findTrack(track));
    REQUIRE(clip != nullptr);
    CHECK(clip->loopEnd == kPpq * 4);
    CHECK(clip->color == song.findTrack(track)->color);
    CHECK(createClip(song, track, 99, kPpq, song.newId<ClipId>()) == nullptr);
}

TEST_CASE("new clips are numbered across the song, not after their track") {
    Song song = makeDefaultSong();
    CHECK(newClipName(song) == "Clip 1");
    TrackId track = song.tracks[0].id;
    createClip(song, track, 0, kPpq * 4, song.newId<ClipId>())->apply(song);
    CHECK(song.activeClip(*song.findTrack(track))->name == "Clip 1");
    CHECK(newClipName(song) == "Clip 2");

    Clip renamed;
    renamed.id = song.newId<ClipId>();
    renamed.name = "Clip 7";
    song.clips[renamed.id] = renamed;
    Clip other = renamed;
    other.id = song.newId<ClipId>();
    other.name = "Clip 9b";
    song.clips[other.id] = other;
    CHECK(newClipName(song) == "Clip 8");
}

TEST_CASE("editNotes grows a loop that runs to the clip's end to hold notes put past it") {
    const Tick bar = 4 * kPpq;
    Song song;
    Clip c;
    c.id = song.newId<ClipId>();
    c.length = bar;
    c.loopEnd = bar;
    auto note = [&](Tick start, Tick length) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = start;
        n.length = length;
        n.pitch = 60;
        return n;
    };
    c.notes = {note(0, kPpq), note(3 * kPpq, kPpq / 4)};
    song.clips[c.id] = c;
    const Clip original = c;

    // Trello 14 Oct: Ctrl+D on a full one-bar clip put the copies after the loop, where they never played.
    std::vector<Note> doubled = c.notes;
    for (Note n : c.notes) {
        n.id = song.newId<NoteId>();
        n.start += bar;
        doubled.push_back(n);
    }
    CommandPtr grow = editNotes(song, "Duplicate 2 notes", c.id, c.notes, doubled);
    REQUIRE(grow);
    CHECK(grow->name() == "Duplicate 2 notes");
    grow->apply(song);
    CHECK(song.findClip(c.id)->loopEnd == 2 * bar);
    CHECK(song.findClip(c.id)->length == 2 * bar);
    grow->revert(song);
    CHECK(*song.findClip(c.id) == original);

    // A note that starts inside but rings past the end, or hangs just past a bar line, adds no bar.
    std::vector<Note> ringing = c.notes;
    ringing.push_back(note(3 * kPpq, 2 * kPpq));
    CommandPtr ring = editNotes(song, "Draw note", c.id, c.notes, ringing);
    ring->apply(song);
    CHECK(song.findClip(c.id)->loopEnd == bar);
    ring->revert(song);
    std::vector<Note> strummed = c.notes;
    strummed.push_back(note(bar + 3 * kPpq, kPpq + 20));
    CommandPtr strum = editNotes(song, "Draw note", c.id, c.notes, strummed);
    strum->apply(song);
    CHECK(song.findClip(c.id)->loopEnd == 2 * bar);
    strum->revert(song);

    // A note that was already outside stays outside when edited.
    Clip& stored = song.clips[c.id];
    stored.notes.push_back(note(2 * bar, kPpq));
    std::vector<Note> before = stored.notes, moved = stored.notes;
    moved.back().start += kPpq;
    CommandPtr outside = editNotes(song, "Move note", c.id, before, moved);
    outside->apply(song);
    CHECK(song.findClip(c.id)->loopEnd == bar);
    outside->revert(song);
    stored.notes.pop_back();

    // A loop set inside the clip, or a clip without a loop, is left alone.
    stored.length = 2 * bar;
    CommandPtr inner = editNotes(song, "Duplicate 2 notes", c.id, c.notes, doubled);
    inner->apply(song);
    CHECK(song.findClip(c.id)->loopEnd == bar);
    inner->revert(song);
    stored.length = bar;
    stored.loopMode = LoopMode::Off;
    CommandPtr unlooped = editNotes(song, "Duplicate 2 notes", c.id, c.notes, doubled);
    unlooped->apply(song);
    CHECK(song.findClip(c.id)->loopEnd == bar);
}

TEST_CASE("a track has notes once a slot or timeline clip holds one") {
    Song song = makeDefaultSong();
    TrackId track = song.tracks[0].id;
    CHECK_FALSE(song.hasNotes(song.tracks[0]));
    ClipId clip = song.newId<ClipId>();
    createClip(song, track, 0, kPpq * 4, clip)->apply(song);
    CHECK_FALSE(song.hasNotes(song.tracks[0])); // an empty clip plays nothing
    Note n;
    n.id = song.newId<NoteId>();
    n.length = kPpq;
    song.clips[clip].notes.push_back(n);
    CHECK(song.hasNotes(song.tracks[0]));
    CHECK_FALSE(song.hasNotes(song.tracks[1]));
}
