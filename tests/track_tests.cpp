#include "doctest.h"

#include "../src/engine/Snapshot.hpp"
#include "../src/model/TrackCommands.hpp"
#include "../src/ui/interaction/TrackRows.hpp"
#include "RandomSong.hpp"

using namespace tessera;

namespace {

NextId idsFrom(Song& song) {
    return [&song] { return song.nextId++; };
}

std::vector<std::string> names(const Song& song) {
    std::vector<std::string> out;
    for (const Track& t : song.tracks)
        out.push_back(t.name);
    return out;
}

/// apply, revert (exactly back, apart from IDs handed out), re-apply gives the same song.
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

Song fourTracks() {
    Song s = makeDefaultSong();
    addDemoClip(s);
    for (int i = 0; i < 4; i++)
        s.tracks[size_t(i)].name = std::string(1, char('A' + i));
    return s;
}

} // namespace

TEST_CASE("adding tracks: placed where asked, capped at 64") {
    Song song = fourTracks();
    CommandPtr add = addTrack(song, TrackType::Instrument, 1, idsFrom(song));
    REQUIRE(add);
    checkSymmetric(song, *add);
    CHECK(song.tracks.size() == 5);
    CHECK(song.tracks[1].slots.size() == song.scenes.size());
    CHECK(song.tracks[1].name == "Track 4"); // the fourth instrument track

    while (song.tracks.size() < size_t(kMaxTracks))
        addTrack(song, TrackType::Drum, 0, idsFrom(song))->apply(song);
    CHECK(addTrack(song, TrackType::Drum, 0, idsFrom(song)) == nullptr);
}

TEST_CASE("deleting tracks: their own clips go, shared clips stay, undo restores everything") {
    Song song = fourTracks();
    ClipId demo = *song.tracks[0].slots[0].clip;
    CommandPtr remove = removeTracks(song, {song.tracks[0].id});
    REQUIRE(remove);
    checkSymmetric(song, *remove);
    CHECK(song.findClip(demo) == nullptr);
    remove->revert(song);
    CHECK(song.findClip(demo) != nullptr);

    // Share the clip with another track: deleting track 0 now keeps it.
    song.tracks[1].slots[0].clip = demo;
    removeTracks(song, {song.tracks[0].id})->apply(song);
    CHECK(song.findClip(demo) != nullptr);
}

TEST_CASE("the last track can't be deleted") {
    Song song = fourTracks();
    std::vector<TrackId> all;
    for (const Track& t : song.tracks)
        all.push_back(t.id);
    CHECK(removeTracks(song, all) == nullptr);
    CommandPtr three = removeTracks(song, {all[0], all[1], all[2]});
    REQUIRE(three);
    three->apply(song);
    CHECK(song.tracks.size() == 1);
}

TEST_CASE("duplicating tracks copies their clips as independent clips, right after the original") {
    Song song = fourTracks();
    CommandPtr dup = duplicateTracks(song, {song.tracks[0].id, song.tracks[2].id}, idsFrom(song));
    REQUIRE(dup);
    checkSymmetric(song, *dup);
    CHECK(names(song) == std::vector<std::string>{"A", "A copy", "B", "C", "C copy", "D"});
    ClipId original = *song.tracks[0].slots[0].clip;
    ClipId copy = *song.tracks[1].slots[0].clip;
    CHECK(original != copy);
    CHECK(song.findClip(copy)->notes.size() == song.findClip(original)->notes.size());
    CHECK(song.findClip(copy)->notes[0].id != song.findClip(original)->notes[0].id);
}

TEST_CASE("moving tracks keeps their order and lands before the target") {
    Song song = fourTracks();
    std::vector<TrackId> ids = {song.tracks[0].id, song.tracks[1].id};
    CommandPtr move = moveTracks(song, ids, 4, std::nullopt); // to the end
    REQUIRE(move);
    checkSymmetric(song, *move);
    CHECK(names(song) == std::vector<std::string>{"C", "D", "A", "B"});

    CommandPtr back = moveTracks(song, {song.tracks[3].id}, 0, std::nullopt);
    back->apply(song);
    CHECK(names(song) == std::vector<std::string>{"B", "C", "D", "A"});

    CHECK(moveTracks(song, {song.tracks[0].id}, 0, std::nullopt) == nullptr); // nowhere to go
}

TEST_CASE("grouping gathers tracks, moving into and out of groups, ungrouping") {
    Song song = fourTracks();
    GroupId g = song.newId<GroupId>();
    CommandPtr group = groupTracks(song, {song.tracks[0].id, song.tracks[2].id}, g, "Keys", 0xFF0000);
    REQUIRE(group);
    checkSymmetric(song, *group);
    CHECK(names(song) == std::vector<std::string>{"A", "C", "B", "D"});
    REQUIRE(song.groups.size() == 1);
    CHECK(song.groups[0].tracks == std::vector<TrackId>{song.tracks[0].id, song.tracks[1].id});

    // Rows: header, A, C (indented), B, D. Folded: header, B, D.
    auto rows = buildTrackRows(song, {});
    REQUIRE(rows.size() == 5);
    CHECK(rows[0].kind == TrackRow::Kind::Group);
    CHECK(rows[1].inGroup);
    CHECK_FALSE(rows[3].inGroup);
    CHECK(buildTrackRows(song, {g.value}).size() == 3);

    // Open automation: the track's lanes, then an add-lane row; inside a group they stay in it.
    AutomationEnvelope lane;
    lane.id = song.newId<AutomationId>();
    song.tracks[0].automation.push_back(lane);
    auto open = buildTrackRows(song, {}, {song.tracks[0].id.value});
    REQUIRE(open.size() == 7);
    CHECK(open[2].kind == TrackRow::Kind::Lane);
    CHECK(open[2].lane == lane.id);
    CHECK(open[2].inGroup);
    CHECK(open[3].kind == TrackRow::Kind::AddLane);
    CHECK(open[3].track == song.tracks[0].id);
    song.tracks[0].automation.clear();

    // Drop D onto the group (at its end): it joins.
    CommandPtr join = moveTracks(song, {song.tracks[3].id}, 2, g);
    REQUIRE(join);
    join->apply(song);
    CHECK(names(song) == std::vector<std::string>{"A", "C", "D", "B"});
    CHECK(song.groups[0].tracks.size() == 3);

    CommandPtr leave = moveTracks(song, {song.tracks[0].id}, 4, std::nullopt);
    leave->apply(song);
    CHECK(song.groups[0].tracks.size() == 2);
    CHECK(groupOf(song, song.tracks.back().id) == nullptr);

    CommandPtr un = ungroup(song, g);
    REQUIRE(un);
    checkSymmetric(song, *un);
    CHECK(song.groups.empty());
}

TEST_CASE("deleting a grouped track and undoing puts it back in its group") {
    Song song = fourTracks();
    GroupId g = song.newId<GroupId>();
    groupTracks(song, {song.tracks[1].id, song.tracks[2].id}, g, "G", 0)->apply(song);
    TrackId b = song.tracks[1].id;
    CommandPtr remove = removeTracks(song, {b});
    checkSymmetric(song, *remove); // leaves the delete applied
    remove->revert(song);
    CHECK(song.groups[0].tracks.front() == b);
}

TEST_CASE("a deleted track loses its output slot; undo brings the same track back to the bus") {
    Song song = fourTracks();
    BusSlots slots;
    slots.sync(song);
    TrackId first = song.tracks[0].id;
    CHECK(slots.slotOf(first) >= 0);

    CommandPtr remove = removeTracks(song, {first});
    remove->apply(song);
    slots.sync(song);
    CHECK(slots.slotOf(first) == -1); // output modules bound to it show "Track missing"

    remove->revert(song);
    slots.sync(song);
    CHECK(song.findTrack(first) != nullptr); // same ID: bindings find it again
    CHECK(slots.slotOf(first) >= 0);
}

TEST_CASE("track selection helpers") {
    Song song = fourTracks();
    auto range = trackRange(song, song.tracks[3].id, song.tracks[1].id);
    CHECK(range == std::vector<TrackId>{song.tracks[1].id, song.tracks[2].id, song.tracks[3].id});
    auto sel = toggled({song.tracks[0].id}, song.tracks[2].id);
    CHECK(sel.size() == 2);
    CHECK(toggled(sel, song.tracks[0].id) == std::vector<TrackId>{song.tracks[2].id});
}

TEST_CASE("random track edits undo back to the start") {
    Song song = testing::RandomSong(11).make();
    while (song.tracks.size() < 6)
        addTrack(song, TrackType::Instrument, 0, idsFrom(song))->apply(song);
    const Song original = song;
    std::mt19937 rng(5);
    std::vector<CommandPtr> history;
    for (int step = 0; step < 120; step++) {
        auto pick = [&] { return song.tracks[rng() % song.tracks.size()].id; };
        CommandPtr c;
        switch (rng() % 6) {
        case 0:
            c = addTrack(song, TrackType::Instrument, int(rng() % (song.tracks.size() + 1)), idsFrom(song));
            break;
        case 1:
            c = duplicateTracks(song, {pick()}, idsFrom(song));
            break;
        case 2:
            c = removeTracks(song, {pick()});
            break;
        case 3:
            c = moveTracks(song, {pick()}, int(rng() % (song.tracks.size() + 1)), std::nullopt);
            break;
        case 4:
            c = groupTracks(song, {pick(), pick()}, song.newId<GroupId>(), "G", 0);
            break;
        case 5:
            if (!song.groups.empty())
                c = ungroup(song, song.groups[rng() % song.groups.size()].id);
            break;
        }
        if (!c)
            continue;
        c->apply(song);
        history.push_back(std::move(c));
    }
    REQUIRE(history.size() > 60);
    for (auto it = history.rbegin(); it != history.rend(); ++it)
        (*it)->revert(song);
    Song expected = original;
    expected.nextId = song.nextId;
    CHECK(song == expected);
}

TEST_CASE("auto voices follow the most notes a track's clips play at once") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    Track& track = song.tracks[1];
    track.polyphony = kAutoVoices;
    CHECK(song.voicesOf(track) == 1); // no clips yet

    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.length = clip.loopEnd = 4 * kPpq;
    auto note = [&](Tick start, Tick length, int pitch, bool muted = false) {
        Note n;
        n.id = song.newId<NoteId>();
        n.start = start;
        n.length = length;
        n.pitch = int8_t(pitch);
        n.muted = muted;
        clip.notes.push_back(n);
    };
    note(0, kPpq, 60);
    note(0, kPpq, 64);
    note(0, kPpq, 67);
    note(kPpq, kPpq, 60);           // starts as the chord ends: not overlapping
    note(2 * kPpq, kPpq, 50, true); // muted notes don't count
    note(2 * kPpq, kPpq, 52, true);
    note(2 * kPpq, kPpq, 53, true);
    note(2 * kPpq, kPpq, 55, true);
    note(5 * kPpq, kPpq, 40); // after the loop: never plays
    note(5 * kPpq, kPpq, 41);
    note(5 * kPpq, kPpq, 42);
    note(5 * kPpq, kPpq, 43);
    clip.sortNotes();
    CHECK(clip.maxSimultaneousNotes() == 3);

    song.clips.emplace(clip.id, clip);
    track.slots[0].clip = clip.id;
    CHECK(song.voicesOf(track) == 3);

    track.polyphony = 5;
    CHECK(song.voicesOf(track) == 5);
}
