#include "doctest.h"

#include "../src/model/Json.hpp"
#include "../src/model/Sends.hpp"
#include "../src/model/Serialize.hpp"
#include "../src/model/TrackCommands.hpp"

using namespace tessera;

namespace {

/// The default song: Bass, Chords, Lead, Drums.
Song fourTracks() { return makeDefaultSong(); }

TrackId idAt(const Song& song, int i) { return song.tracks[size_t(i)].id; }

} // namespace

TEST_CASE("sends never make a loop, and say why one can't be added") {
    Song song = fourTracks();
    TrackId bass = idAt(song, 0), chords = idAt(song, 1), lead = idAt(song, 2);
    CommandPtr a = addSend(song, bass, lead);
    REQUIRE(a);
    a->apply(song);
    CommandPtr b = addSend(song, lead, chords);
    REQUIRE(b);
    b->apply(song);
    CHECK(sendsReach(song, bass, chords));
    CHECK(whyNoSend(song, chords, bass) == "Would feed back into " + song.tracks[1].name);
    CHECK(addSend(song, chords, bass) == nullptr);
    CHECK(addSend(song, bass, bass) == nullptr);
    CHECK(addSend(song, bass, lead) == nullptr); // already sends there
    CHECK(sendersTo(song, lead) == std::vector<TrackId>{bass});
    // Senders come before the tracks they send to.
    std::vector<TrackId> order = sendOrder(song);
    auto at = [&](TrackId id) { return std::find(order.begin(), order.end(), id) - order.begin(); };
    CHECK(order.size() == song.tracks.size());
    CHECK(at(bass) < at(lead));
    CHECK(at(lead) < at(chords));
}

TEST_CASE("a send's level and place are one undo step each, and save with the song") {
    Song song = fourTracks();
    TrackId bass = idAt(song, 0), lead = idAt(song, 2);
    addSend(song, bass, lead)->apply(song);
    CHECK(findSend(song, bass, lead)->db == kSilentDb);
    CommandPtr up = setSend(song, bass, {lead, -12, true}, "Send level");
    REQUIRE(up);
    up->apply(song);
    CHECK(*findSend(song, bass, lead) == Send{lead, -12, true});
    json::Ref j(songToJson(song));
    CHECK(songFromJson(j.get(), Song::kSchemaVersion) == song);
    up->revert(song);
    CHECK(findSend(song, bass, lead)->db == kSilentDb);
    CommandPtr gone = removeSend(song, bass, lead);
    REQUIRE(gone);
    gone->apply(song);
    CHECK(song.tracks[0].sends.empty());
}

TEST_CASE("deleting a track removes the sends to it in the same undo step") {
    Song song = fourTracks();
    TrackId bass = idAt(song, 0), lead = idAt(song, 2);
    addSend(song, bass, lead)->apply(song);
    Song before = song;
    CommandPtr del = removeTracks(song, {lead});
    REQUIRE(del);
    del->apply(song);
    CHECK(song.tracks[0].sends.empty());
    del->revert(song);
    CHECK(song == before);
}

TEST_CASE("normalize drops sends that go nowhere, repeat, or close a loop") {
    Song song = fourTracks();
    TrackId bass = idAt(song, 0), chords = idAt(song, 1), lead = idAt(song, 2);
    song.tracks[0].sends = {{lead, -6, false}, {lead, -3, false}, {bass, 0, false}, {TrackId(99999), 0, false}};
    song.tracks[2].sends = {{chords, 0, false}};
    song.tracks[1].sends = {{bass, 0, false}}; // chords -> bass -> lead -> chords: a loop
    normalize(song);
    CHECK(song.tracks[0].sends.size() == 1);
    CHECK(song.tracks[0].sends[0].db == -6);
    CHECK_FALSE(sendsReach(song, bass, bass));
    CHECK_FALSE(sendsReach(song, lead, lead));
}

TEST_CASE("a Send lane names the track it sends to, and saves") {
    Song song = fourTracks();
    TrackId bass = idAt(song, 0), lead = idAt(song, 2);
    addSend(song, bass, lead)->apply(song);
    AutomationEnvelope lane;
    lane.id = song.newId<AutomationId>();
    lane.drive = LaneDrive::Send;
    lane.sendTo = lead;
    lane.points = {{0, 0.5f}};
    song.tracks[0].automation.push_back(lane);
    json::Ref j(songToJson(song));
    Song back = songFromJson(j.get(), Song::kSchemaVersion);
    REQUIRE(back.tracks[0].automation.size() == 1);
    CHECK(back.tracks[0].automation[0].sendTo == lead);
    CHECK(back.tracks[0].automation[0].drive == LaneDrive::Send);
}
