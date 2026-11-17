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

TEST_CASE("a drum lane, the clips or an input sends on its own, beside the track's own send, and saves") {
    Song song = fourTracks();
    TrackId drums = idAt(song, 3), lead = idAt(song, 2), bass = idAt(song, 0);
    REQUIRE(song.tracks[3].type == TrackType::Drum);
    addSend(song, drums, lead)->apply(song);
    CommandPtr snare = addSend(song, drums, lead, -6, SendFrom::ofLane(1));
    REQUIRE(snare);
    snare->apply(song);
    CHECK(addSend(song, drums, lead, -6, SendFrom::ofLane(1)) == nullptr); // already sends there
    CHECK(sendersTo(song, lead) == std::vector<TrackId>{drums});         // a track counts once
    CHECK(sendsInto(song, lead).size() == 2);
    CHECK(findSend(song, drums, lead, SendFrom::ofLane(1))->db == -6);
    CHECK(findSend(song, drums, lead)->db == kSilentDb);
    // A part's send can't close a loop either: Lead can't send back to Drums.
    CHECK(addSend(song, lead, drums) == nullptr);
    Send moved = *findSend(song, drums, lead, SendFrom::ofLane(1));
    moved.db = -3;
    setSend(song, drums, moved, "Send -3 dB")->apply(song);
    CHECK(findSend(song, drums, lead, SendFrom::ofLane(1))->db == -3);
    CHECK(findSend(song, drums, lead)->db == kSilentDb);
    addSend(song, bass, lead, -9, SendFrom::input(42))->apply(song);
    // Saved and loaded back with where each send comes from.
    json::Ref j(songToJson(song));
    Song back = songFromJson(j.get(), Song::kSchemaVersion);
    CHECK(back.tracks[3].sends == song.tracks[3].sends);
    REQUIRE(back.tracks[0].sends.size() == 1);
    CHECK(back.tracks[0].sends[0].from == SendFrom::input(42));
    CommandPtr gone = removeSend(song, drums, lead, SendFrom::ofLane(1));
    REQUIRE(gone);
    gone->apply(song);
    CHECK(findSend(song, drums, lead, SendFrom::ofLane(1)) == nullptr);
    CHECK(findSend(song, drums, lead) != nullptr);
    CHECK(removeSend(song, drums, lead, SendFrom::ofLane(1)) == nullptr);
}

TEST_CASE("normalize drops a part's send the track can't have") {
    Song song = fourTracks();
    TrackId lead = idAt(song, 2);
    song.tracks[0].sends = {{lead, 0, false, SendFrom::ofLane(2)}, {lead, 0, false, SendFrom::clips()},
                            {lead, 0, false, SendFrom::input(7)}};
    song.tracks[3].sends = {{lead, 0, false, SendFrom::ofLane(9)}, {lead, 0, false, SendFrom::ofLane(2)}};
    normalize(song);
    REQUIRE(song.tracks[0].sends.size() == 1); // an instrument track has no lanes and no clips
    CHECK(song.tracks[0].sends[0].from == SendFrom::input(7));
    REQUIRE(song.tracks[3].sends.size() == 1); // lane 9 doesn't exist
    CHECK(song.tracks[3].sends[0].from == SendFrom::ofLane(2));
}
