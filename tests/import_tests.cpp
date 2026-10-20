#include "doctest.h"

#include "../src/model/MidiFile.hpp"
#include "../src/model/MidiImport.hpp"

using namespace tessera;

namespace {

/// Counts up from 1000, as a song's ID source would.
NextId counter() {
    auto next = std::make_shared<uint64_t>(1000);
    return [next] { return (*next)++; };
}

std::vector<uint8_t> bytesOf(const smf::File& file) { return smf::write(file); }

} // namespace

TEST_CASE("MIDI import reads notes at Tessera's resolution, with their velocities") {
    smf::File file;
    file.ppq = 480;
    smf::Track t;
    t.events.push_back(smf::trackName(0, "Lead"));
    t.events.push_back(smf::noteOn(0, 0, 60, 100));
    t.events.push_back(smf::noteOff(480, 0, 60, 64));
    t.events.push_back(smf::noteOn(480, 0, 62, 127));
    smf::Event off; // a note-on with velocity 0 ends it (smf::noteOn() never writes one, so it is made by hand)
    off.tick = 720;
    off.bytes = {0x90, 62, 0};
    off.order = 1;
    t.events.push_back(off);
    file.tracks.push_back(t);

    midiImport::Imported in;
    REQUIRE(midiImport::read(bytesOf(file), in));
    REQUIRE(in.parts.size() == 1);
    const midiImport::Part& p = in.parts[0];
    CHECK(p.name == "Lead");
    CHECK_FALSE(p.drum);
    REQUIRE(p.notes.size() == 2);
    CHECK(p.notes[0].start == 0);
    CHECK(p.notes[0].length == kPpq);
    CHECK(p.notes[0].pitch == 60);
    CHECK(p.notes[0].velocity == doctest::Approx(100.f / 127.f));
    CHECK(p.notes[0].releaseVelocity == doctest::Approx(64.f / 127.f));
    CHECK(p.notes[1].start == kPpq);
    CHECK(p.notes[1].length == kPpq / 2);
    CHECK(p.notes[1].releaseVelocity == doctest::Approx(0.5f));
}

TEST_CASE("MIDI import splits a single-track file by channel and finds tempo and time signature") {
    smf::File file;
    file.format = 0;
    smf::Track t;
    t.events.push_back(smf::tempo(0, 96));
    t.events.push_back(smf::timeSignature(0, 3, 4));
    t.events.push_back(smf::noteOn(0, 0, 48, 90));
    t.events.push_back(smf::noteOn(0, 9, 36, 110));
    t.events.push_back(smf::noteOff(240, 9, 36, 0));
    t.events.push_back(smf::noteOn(480, 9, 42, 80));
    t.events.push_back(smf::noteOff(600, 9, 42, 0));
    t.events.push_back(smf::noteOff(960, 0, 48, 0));
    file.tracks.push_back(t);

    midiImport::Imported in;
    REQUIRE(midiImport::read(bytesOf(file), in));
    REQUIRE(in.parts.size() == 2);
    CHECK(in.parts[0].channel == 0);
    CHECK_FALSE(in.parts[0].drum);
    CHECK(in.parts[1].channel == 9);
    CHECK(in.parts[1].drum);
    CHECK(in.parts[1].name == "MIDI 2 (ch 10)");
    REQUIRE(in.tempo);
    CHECK(*in.tempo == doctest::Approx(96).epsilon(0.001));
    REQUIRE(in.timeSig);
    CHECK(*in.timeSig == TimeSig{3, 4});
    CHECK(midiImport::noteCount(in) == 3);

    SUBCASE("one clip of everything, in whole bars") {
        Clip c = midiImport::clipOf(in, -1, *in.timeSig, counter(), "Song");
        CHECK(c.notes.size() == 3);
        CHECK(c.length == ticksPerBar(TimeSig{3, 4}));
        CHECK(c.loopEnd == c.length);
        CHECK(c.loopMode == LoopMode::Loop);
    }
    SUBCASE("as tracks: a clip on the timeline and in the first slot, drum lanes for the notes used") {
        Song song = makeDefaultSong();
        Song before = song;
        CommandPtr command = midiImport::importTracks(song, in, counter(), "song.mid");
        REQUIRE(command);
        command->apply(song);
        REQUIRE(song.tracks.size() == before.tracks.size() + 2);
        CHECK(song.timeSig == TimeSig{3, 4});
        const Track& keys = song.tracks[before.tracks.size()];
        const Track& drums = song.tracks[before.tracks.size() + 1];
        CHECK(keys.type == TrackType::Instrument);
        CHECK(drums.type == TrackType::Drum);
        REQUIRE(drums.drumLanes.size() == 2);
        CHECK(drums.drumLanes[0].pitch == 36);
        CHECK(drums.drumLanes[1].pitch == 42);
        REQUIRE(keys.arrangement.size() == 1);
        CHECK(keys.arrangement[0].position == 0);
        REQUIRE(keys.slots.size() == song.scenes.size());
        REQUIRE(keys.slots[0].clip);
        const Clip* clip = song.findClip(*keys.slots[0].clip);
        REQUIRE(clip);
        CHECK(clip->notes.size() == 1);
        CHECK(keys.arrangement[0].clip == clip->id);
        command->revert(song);
        CHECK(song == before);
    }
}

TEST_CASE("MIDI import refuses what it can't read") {
    midiImport::Imported in;
    CHECK_FALSE(midiImport::read({'n', 'o', 'p', 'e'}, in));
    smf::File file;
    file.ppq = 0x8000 | (25 << 8) | 40; // SMPTE timing: frames, not beats
    file.tracks.push_back(smf::Track{});
    CHECK_FALSE(midiImport::read(bytesOf(file), in));

    smf::File empty;
    empty.tracks.push_back(smf::Track{});
    REQUIRE(midiImport::read(bytesOf(empty), in));
    CHECK(in.parts.empty());
    Song song = makeDefaultSong();
    CHECK_FALSE(midiImport::importTracks(song, in, counter(), "empty.mid"));
}
