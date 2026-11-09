#include "doctest.h"

#include "../src/model/ArrangementEdits.hpp"
#include "../src/model/AudioEdits.hpp"
#include "../src/model/AudioRegions.hpp"
#include "../src/model/LauncherCommands.hpp"
#include "../src/model/Serialize.hpp"

using namespace tessera;

namespace {

constexpr double kRate = 48000;
constexpr double kFramesPerTick = 25; // 120 BPM at 48 kHz, 960 ticks a beat

/// A song with one audio clip of a 10-second file, 4 seconds (7680 ticks) of it from the file's start, on the timeline.
struct Parts {
    Song song;
    TrackId track;
    ClipId clip;
    int64_t frames = int64_t(10 * kRate);
    regions::NewId newId = [this] { return AudioRegionId(song.nextId++); };
    NextId next = [this] { return song.nextId++; };

    Parts() {
        song = makeDefaultSong();
        ImportedAudio file;
        file.source.name = "test.wav";
        file.source.sampleRate = kRate;
        file.source.channels = 1;
        file.source.frames = frames;
        file.tempo = 120;
        AudioDropTarget target;
        target.index = int(song.tracks.size());
        ImportResult made;
        importAudio(song, {file}, target, next, &made)->apply(song);
        track = made.tracks[0];
        clip = made.clips[0];
        Clip& c = *song.findClip(clip);
        c.length = 7680;
        c.loopEnd = 7680;
        song.findTrack(track)->arrangement[0].length = 7680;
    }
    const Clip& theClip() const { return *song.findClip(clip); }
    const AudioContent& audio() const { return *theClip().audio; }
    /// The clip cut at each of `at` and stored; returns its parts.
    regions::List cutAt(std::vector<Tick> at) {
        regions::List list = regions::of(theClip(), kRate);
        for (AudioRegion& r : list)
            if (!r.id.valid())
                r.id = newId();
        for (Tick t : at)
            list = regions::cut(list, audio(), t, kRate, newId);
        regions::setRegions(song, clip, list, "Cut", next)->apply(song);
        return audio().regions;
    }
};

} // namespace

TEST_CASE("an unedited clip has one part over the whole clip") {
    Parts p;
    regions::List list = regions::of(p.theClip(), kRate);
    REQUIRE(list.size() == 1);
    CHECK(list[0].start == 0);
    CHECK(list[0].length == 7680);
    CHECK(list[0].sourceFrame == 0);
    CHECK_FALSE(list[0].id.valid());
    CHECK(p.audio().regions.empty());
}

TEST_CASE("a cut makes two parts that play on from each other") {
    Parts p;
    regions::List list = p.cutAt({1920});
    REQUIRE(list.size() == 2);
    CHECK(list[0].length == 1920);
    CHECK(list[1].start == 1920);
    CHECK(list[1].length == 5760);
    CHECK(list[1].sourceFrame == 1920 * 25);
    CHECK(list[0].id != list[1].id);
    SUBCASE("a cut right at a part's edge does nothing") {
        CHECK(regions::cut(list, p.audio(), 1930, kRate, p.newId).size() == 3);
        CHECK(regions::cut(list, p.audio(), 1925, kRate, p.newId).size() == 2);
        CHECK(regions::cut(list, p.audio(), 1920, kRate, p.newId).size() == 2);
    }
}

TEST_CASE("a backwards part keeps the right audio when it is cut") {
    Parts p;
    Clip& c = *p.song.findClip(p.clip);
    c.audio->reverse = true;
    regions::List list = p.cutAt({1920});
    CHECK_FALSE(p.audio().reverse);
    REQUIRE(list.size() == 2);
    CHECK(list[0].reverse);
    // The clip plays frame 192000 first, going down: the left part plays the file's upper stretch.
    CHECK(regions::frameAt(p.audio(), list[0], 0, kRate) == doctest::Approx(7680 * kFramesPerTick));
    CHECK(regions::frameAt(p.audio(), list[0], 1920, kRate) == doctest::Approx(5760 * kFramesPerTick));
    CHECK(regions::frameAt(p.audio(), list[1], 1920, kRate) == doctest::Approx(5760 * kFramesPerTick));
    CHECK(regions::frameAt(p.audio(), list[1], 7680, kRate) == doctest::Approx(0));
}

TEST_CASE("trims bring back hidden audio and stop at the file's ends") {
    Parts p;
    regions::List list = p.cutAt({1920, 3840});
    AudioRegionId middle = list[1].id;
    SUBCASE("shorter from the left") {
        regions::List out = regions::trimStart(list, middle, 2400, p.audio(), kRate, p.frames);
        CHECK(out[1].start == 2400);
        CHECK(out[1].length == 1440);
        CHECK(out[1].sourceFrame == 2400 * 25);
    }
    SUBCASE("longer to the left, into the audio before it") {
        regions::List out = regions::trimStart(list, middle, 1440, p.audio(), kRate, p.frames);
        CHECK(out[1].sourceFrame == 1440 * 25);
    }
    SUBCASE("the file's start stops it") {
        regions::List out = regions::trimStart(list, list[0].id, -500, p.audio(), kRate, p.frames);
        CHECK(out[0].start == 0);
        CHECK(out[0].sourceFrame == 0);
    }
    SUBCASE("the file's end stops it") {
        regions::List out = regions::trimEnd(list, list[2].id, 999999, p.audio(), kRate, p.frames);
        CHECK(out[2].end() == 19200);
    }
    SUBCASE("a part keeps a little length") {
        regions::List out = regions::trimStart(list, middle, 3840, p.audio(), kRate, p.frames);
        CHECK(out[1].length == regions::kMinLength);
    }
}

TEST_CASE("a slip changes what a part plays, not where it sits") {
    Parts p;
    regions::List list = p.cutAt({1920});
    regions::List out = regions::slip(list, list[1].id, 1000, p.audio(), kRate, p.frames);
    CHECK(out[1].sourceFrame == 1920 * 25 + 1000);
    CHECK(out[1].start == 1920);
    regions::List early = regions::slip(list, list[0].id, -5000, p.audio(), kRate, p.frames);
    CHECK(early[0].sourceFrame == 0);
}

TEST_CASE("moves stay inside the clip") {
    Parts p;
    regions::List list = p.cutAt({1920, 3840});
    regions::Ids mid{list[1].id};
    CHECK(regions::move(list, mid, 100, 7680)[1].start == 2020);
    CHECK(regions::move(list, mid, -99999, 7680)[0].start == 0);
    CHECK(regions::move(list, mid, 99999, 7680)[2].end() == 7680);
}

TEST_CASE("the overlap rule inside a clip matches the timeline's") {
    Parts p;
    regions::List list = p.cutAt({1920, 3840});
    regions::Ids last{list[2].id};
    SUBCASE("a part dropped over a whole other part removes it") {
        regions::List over = list;
        over[2].start = 1800;
        over[2].length = 2200;
        regions::List out = regions::settle(over, last, p.audio(), kRate, p.newId);
        CHECK(out.size() == 2);
    }
    SUBCASE("a part dropped inside another splits it, reaching a little in") {
        regions::List inside = {list[0], list[2]};
        inside[0].length = 7680;
        inside[1].start = 2000;
        inside[1].length = 1000;
        regions::List out = regions::settle(inside, last, p.audio(), kRate, p.newId);
        REQUIRE(out.size() == 3);
        CHECK(out[0].end() > 2000);
        CHECK(out[2].start < 3000);
        CHECK(out[2].sourceFrame == out[2].start * 25);
    }
    SUBCASE("a plain overlap stays: it is a crossfade") {
        regions::List over = list;
        over[1].length = 2100;
        regions::Ids changed{list[1].id};
        CHECK(regions::settle(over, changed, p.audio(), kRate, p.newId).size() == 3);
    }
}

TEST_CASE("an edit reaches linked copies, and an independent copy keeps its own") {
    Parts p;
    ArrangerClip first = p.song.findTrack(p.track)->arrangement[0];
    ArrangerClip linked = first;
    linked.id = ArrangementId(p.song.nextId++);
    linked.position = 20000;
    p.song.findTrack(p.track)->arrangement.push_back(linked);

    Clip copy = copyClip(p.theClip(), p.next);
    ClipId own = copy.id;
    AddClip(std::move(copy)).apply(p.song);

    regions::List list = p.cutAt({1920});
    CHECK(p.song.findClip(linked.clip)->audio->regions.size() == 2);
    CHECK(p.song.findClip(own)->audio->regions.empty());

    Clip second = copyClip(p.theClip(), p.next);
    ClipId other = second.id;
    AddClip(std::move(second)).apply(p.song);
    regions::setRegions(p.song, other, {list[0]}, "Keep one", p.next)->apply(p.song);
    CHECK(p.song.findClip(other)->audio->regions.size() == 1);
    CHECK(p.audio().regions.size() == 2);
}

TEST_CASE("storing parts is one undo step, and a part past the end makes the clip longer") {
    Parts p;
    regions::List list = p.cutAt({1920});
    regions::List longer = regions::trimEnd(list, list[1].id, 9000, p.audio(), kRate, p.frames);
    CommandPtr cmd = regions::setRegions(p.song, p.clip, longer, "Trim", p.next);
    REQUIRE(cmd);
    Song before = p.song;
    cmd->apply(p.song);
    CHECK(p.theClip().length == 9000);
    CHECK(p.theClip().loopEnd == 9000);
    cmd->revert(p.song);
    CHECK(p.song == before);
    CHECK_FALSE(regions::setRegions(p.song, p.clip, list, "Same", p.next));
}

TEST_CASE("Shift+Delete closes the gap and shortens what played the whole clip") {
    Parts p;
    regions::List list = p.cutAt({1920, 3840});
    CommandPtr cmd = regions::removeClosingGaps(p.song, p.clip, {list[1].id}, p.next);
    REQUIRE(cmd);
    Song before = p.song;
    cmd->apply(p.song);
    REQUIRE(p.audio().regions.size() == 2);
    CHECK(p.audio().regions[1].start == 1920);
    CHECK(p.audio().regions[1].sourceFrame == 3840 * 25);
    CHECK(p.theClip().length == 5760);
    CHECK(p.theClip().loopEnd == 5760);
    CHECK(p.song.findTrack(p.track)->arrangement[0].length == 5760);
    cmd->revert(p.song);
    CHECK(p.song == before);
    CHECK_FALSE(regions::removeClosingGaps(p.song, p.clip, {list[0].id, list[1].id, list[2].id}, p.next));
}

TEST_CASE("parts survive saving and loading, and bad ones are cleaned up") {
    Parts p;
    regions::List list = p.cutAt({1920});
    list[1].gainDb = -3;
    list[1].muted = true;
    list[1].reverse = true;
    list[1].fadeIn = {100, 0.5f};
    regions::setRegions(p.song, p.clip, list, "Set", p.next)->apply(p.song);
    json_t* j = clipToJson(p.theClip());
    Clip back = clipFromJson(j);
    json_decref(j);
    CHECK(back.audio->regions == p.audio().regions);

    Clip messy = p.theClip();
    messy.audio->regions[1].start = 7000;
    messy.audio->regions[1].length = 99999;
    messy.audio->regions.push_back(AudioRegion{});
    messy.audio->regions.back().length = 0;
    Song s = p.song;
    *s.findClip(p.clip) = messy;
    normalize(s);
    CHECK(s.findClip(p.clip)->audio->regions.size() == 2);
    CHECK(s.findClip(p.clip)->audio->regions[1].end() == 7680);
}

TEST_CASE("clip-wide edits keep the parts playing what they played") {
    Parts p;
    p.cutAt({1920});
    SUBCASE("a higher clip tempo scales their places, not their audio") {
        setClipTempo(p.song, p.clip, 240)->apply(p.song);
        CHECK(p.audio().regions[1].start == 3840);
        CHECK(p.audio().regions[1].sourceFrame == 1920 * 25);
    }
    SUBCASE("reversing mirrors the parts and turns each round") {
        reverseAudio(p.song, p.clip)->apply(p.song);
        REQUIRE(p.audio().regions.size() == 2);
        CHECK_FALSE(p.audio().reverse);
        CHECK(p.audio().regions[0].start == 0);
        CHECK(p.audio().regions[0].length == 5760);
        CHECK(p.audio().regions[0].reverse);
        reverseAudio(p.song, p.clip)->apply(p.song);
        CHECK_FALSE(p.audio().regions[0].reverse);
        CHECK(p.audio().regions[1].start == 1920);
    }
}

TEST_CASE("hits follow the part that plays them") {
    Parts p;
    regions::List list = p.cutAt({1920});
    list[1].start = 3000;
    list[1].length = 4680;
    regions::setRegions(p.song, p.clip, list, "Move", p.next)->apply(p.song);
    ArrangerClip at = p.song.findTrack(p.track)->arrangement[0];
    // A hit at frame 50000 is 2000 ticks into the file: inside the moved part, 80 ticks after where it begins.
    std::vector<Tick> hits = hitTicks(p.theClip(), at, {50000}, kRate, 120);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0] == 3080);
}
