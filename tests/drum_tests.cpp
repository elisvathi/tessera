#include "doctest.h"

#include "../src/model/DrumKit.hpp"
#include "../src/model/Serialize.hpp"

using namespace tessera;

namespace {

std::string pattern(const std::vector<bool>& p) {
    std::string s;
    for (bool b : p)
        s += b ? 'x' : '.';
    return s;
}

struct Ids {
    uint64_t next = 1000;
    edits::IdSource source() {
        return [this] { return NoteId(next++); };
    }
};

Clip barClip() {
    Clip c;
    c.id = ClipId(1);
    c.length = 4 * kPpq;
    c.loopEnd = 4 * kPpq;
    return c;
}

std::string laneSteps(const edits::NoteList& notes, int pitch, LaneRegion r, Tick step) {
    std::vector<bool> on(size_t(r.steps(step)), false);
    for (const Note& n : drums::laneNotes(notes, pitch, r))
        on[size_t((n.start - r.start) / step)] = true;
    return pattern(on);
}

} // namespace

TEST_CASE("Euclidean rhythms spread hits evenly and rotate") {
    CHECK(pattern(euclid(3, 8)) == "x..x..x.");
    CHECK(pattern(euclid(4, 16)) == "x...x...x...x...");
    CHECK(pattern(euclid(5, 8)) == "x.x.xx.x"); // a rotation of the textbook x.xx.xx.; the first step always hits
    CHECK(pattern(euclid(3, 8, 1)) == ".x..x..x");
    CHECK(pattern(euclid(0, 4)) == "....");
    CHECK(pattern(euclid(9, 4)) == "xxxx");
}

TEST_CASE("kits: new drum tracks get the standard kit; hats choke each other") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    const Track& drums = song.tracks.back();
    REQUIRE(drums.drumLanes.size() == 8);
    CHECK(drums.drumLanes[0].name == "Kick");
    CHECK(laneOfPitch(drums.drumLanes, 38) == 1);
    CHECK(laneOfPitch(drums.drumLanes, 99) == -1);
    int ch = laneOfPitch(drums.drumLanes, 42), oh = laneOfPitch(drums.drumLanes, 46);
    CHECK(drums.drumLanes[size_t(ch)].chokeGroup == drums.drumLanes[size_t(oh)].chokeGroup);
    CHECK(drumKit(DrumKitPreset::Gm16).size() == 16);
}

TEST_CASE("a lane's region: the clip loop, or its own loop length") {
    Clip c = barClip();
    c.loopStart = kPpq;
    c.loopEnd = 3 * kPpq;
    DrumLane lane;
    CHECK(laneRegion(lane, c).start == kPpq);
    CHECK(laneRegion(lane, c).length == 2 * kPpq);
    CHECK_FALSE(hasOwnLoop(lane, c));
    lane.loopLength = 3 * kPpq / 4; // 3 sixteenths
    CHECK(laneRegion(lane, c).length == 3 * kPpq / 4);
    CHECK(hasOwnLoop(lane, c));
}

TEST_CASE("pattern tools touch only their lane and region") {
    Ids ids;
    Clip c = barClip();
    LaneRegion r{0, 4 * kPpq};
    Tick s = kPpq / 4;
    edits::NoteList notes;
    notes = drums::applyPattern(notes, 36, r, s, euclid(4, 16), 0.9f, ids.source());
    notes = drums::applyPattern(notes, 38, r, s, euclid(2, 16, 4), 0.9f, ids.source());
    CHECK(laneSteps(notes, 36, r, s) == "x...x...x...x...");
    CHECK(laneSteps(notes, 38, r, s) == "....x.......x...");

    CHECK(laneSteps(drums::rotate(notes, 36, r, s), 36, r, s) == ".x...x...x...x..");
    CHECK(laneSteps(drums::rotate(notes, 36, r, -s), 36, r, s) == "...x...x...x...x");
    CHECK(laneSteps(drums::rotate(notes, 36, r, s), 38, r, s) == "....x.......x..."); // other lane untouched

    edits::NoteList one = drums::applyPattern({}, 36, r, s, euclid(1, 16), 1, ids.source());
    CHECK(laneSteps(drums::reverse(one, 36, r, s), 36, r, s) == "...............x");

    float v = 0.5f;
    edits::NoteList toggled = drums::setStep(notes, 36, s, s, &v, ids.source());
    CHECK(laneSteps(toggled, 36, r, s) == "xx..x...x...x...");
    toggled = drums::setStep(toggled, 36, 0, s, nullptr, ids.source());
    CHECK(laneSteps(toggled, 36, r, s) == ".x..x...x...x...");

    CHECK(drums::laneNotes(drums::clear(notes, 38, r), 38, r).empty());
    CHECK(drums::laneNotes(drums::clear(notes, 38, r), 36, r).size() == 4);

    edits::NoteList copied = drums::laneNotes(notes, 36, r);
    edits::NoteList pasted = drums::paste(notes, 38, r, copied, ids.source());
    CHECK(laneSteps(pasted, 38, r, s) == "x...x...x...x...");
}

TEST_CASE("randomize repeats for the same seed and follows the density") {
    Ids ids;
    LaneRegion r{0, 16 * kPpq};
    Tick s = kPpq / 4;
    auto a = drums::randomize({}, 42, r, s, 0.5f, 0.3f, 0.9f, 7, ids.source());
    auto b = drums::randomize({}, 42, r, s, 0.5f, 0.3f, 0.9f, 7, ids.source());
    auto c = drums::randomize({}, 42, r, s, 0.5f, 0.3f, 0.9f, 8, ids.source());
    CHECK(laneSteps(a, 42, r, s) == laneSteps(b, 42, r, s));
    CHECK(laneSteps(a, 42, r, s) != laneSteps(c, 42, r, s));
    CHECK(a.size() > 16); // about half of 64 steps
    CHECK(a.size() < 48);
    for (const Note& n : a) {
        CHECK(n.velocity >= 0.3f);
        CHECK(n.velocity <= 0.9f);
    }
}

#include "../src/model/Arrangement.hpp"

TEST_CASE("a lane with its own loop repeats on it across the arrangement (polymeter)") {
    Ids ids;
    Clip c = barClip(); // 16 sixteenths
    Tick s = kPpq / 4;
    DrumLane kick, hat;
    kick.pitch = 36;
    hat.pitch = 42;
    hat.loopLength = 3 * s; // 3 steps against 16
    std::vector<DrumLane> lanes{kick, hat};
    c.notes = drums::applyPattern({}, 36, {0, 4 * kPpq}, s, euclid(1, 16), 1, ids.source());
    c.notes = drums::applyPattern(c.notes, 42, {0, 3 * s}, s, euclid(1, 3), 1, ids.source());

    ArrangerClip a;
    a.clip = c.id;
    a.length = 2 * 4 * kPpq; // two bars
    std::vector<TimedNote> out;
    renderInstance(c, a, out, 0, 1, &lanes);
    std::vector<Tick> hats, kicks;
    for (const TimedNote& n : out)
        (n.pitch == 42 ? hats : kicks).push_back(n.start);
    CHECK(kicks == std::vector<Tick>{0, 4 * kPpq});
    REQUIRE(hats.size() == 11); // every 3 steps over 32: 0, 3, ..., 30
    CHECK(hats[5] == 15 * s);
    CHECK(hats[6] == 18 * s); // keeps going across the bar line instead of restarting there
    for (size_t i = 1; i < out.size(); i++)
        CHECK(out[i - 1].start <= out[i].start);
}
