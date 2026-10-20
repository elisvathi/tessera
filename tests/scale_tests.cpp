#include "doctest.h"

#include "../src/model/Chord.hpp"
#include "../src/model/KeyEdits.hpp"
#include "../src/model/NoteEdits.hpp"
#include "../src/model/Scale.hpp"

#include <algorithm>
#include <set>

using namespace tessera;

namespace {

Scale named(int root, const char* id) { return resolveScale(ScaleRef{int8_t(root), id}, {}); }

} // namespace

TEST_CASE("built-in scales have unique ids, the root, and the sizes their names promise") {
    std::set<std::string> ids;
    for (const ScaleDef& d : builtinScales()) {
        CHECK(ids.insert(d.id).second);
        CHECK((d.mask & 1));
        CHECK(d.mask <= kChromaticMask);
    }
    CHECK(ids.size() == 20);
    CHECK(named(0, "major").size() == 7);
    CHECK(named(0, "minor-pentatonic").size() == 5);
    CHECK(named(0, "blues").size() == 6);
    CHECK(named(0, "bebop-dominant").size() == 8);
    CHECK(named(0, "chromatic").chromatic());
}

TEST_CASE("inScale and isRoot follow the root across octaves") {
    Scale d = named(2, "major"); // D E F# G A B C#
    for (int octave = 0; octave < 10; octave++) {
        int base = 2 + 12 * octave;
        CHECK(d.isRoot(base));
        CHECK(d.inScale(base + 4));       // F#
        CHECK_FALSE(d.inScale(base + 3)); // F
        CHECK(d.inScale(base + 11));      // C# above
        CHECK_FALSE(d.inScale(base + 10));
    }
    CHECK(d.inScale(1)); // C# below the first D
}

TEST_CASE("snap goes to the nearest scale note, ties down, or one way when asked") {
    Scale c = named(0, "major");
    CHECK(c.snap(60) == 60);
    CHECK(c.snap(61) == 60);    // C#: tie between C and D goes down
    CHECK(c.snap(61, 1) == 62); // up
    CHECK(c.snap(63, -1) == 62);
    CHECK(c.snap(63, 1) == 64);
    Scale penta = named(0, "minor-pentatonic"); // C Eb F G Bb
    CHECK(penta.snap(61) == 60);
    CHECK(penta.snap(62) == 63);    // D is 2 from C, 1 from Eb
    CHECK(penta.snap(71) == 70);    // B: a tie between Bb and C above
    CHECK(penta.snap(71, 1) == 72); // across the octave
}

TEST_CASE("snap and stepBy stay inside 0..127") {
    Scale c = named(0, "major");
    CHECK(c.snap(127) == 127);        // G9 is in C major
    Scale csharp = named(1, "major"); // C# D# F F# G# A# C: 127 (G) is out
    CHECK(csharp.snap(127, 1) == 126);
    CHECK(c.stepBy(127, 3) == 127);
    CHECK(c.stepBy(0, -3) == 0);
    Scale b = named(11, "major"); // B C# D# E F# G# A#: 0 (C) is out
    CHECK(b.snap(0, -1) == 1);
}

TEST_CASE("stepBy moves by scale notes and wraps octaves") {
    Scale c = named(0, "major");
    CHECK(c.stepBy(60, 1) == 62);
    CHECK(c.stepBy(64, 1) == 65);
    CHECK(c.stepBy(71, 1) == 72);  // B to C crosses the octave
    CHECK(c.stepBy(60, -1) == 59); // C to B below
    CHECK(c.stepBy(60, 7) == 72);
    CHECK(c.stepBy(60, -7) == 48);
    CHECK(c.stepBy(60, 9) == 76);
    // Off the scale, the first step lands on the scale note in that direction.
    CHECK(c.stepBy(61, 1) == 62);
    CHECK(c.stepBy(61, -1) == 60);
    CHECK(c.stepBy(61, 2) == 64);
    CHECK(c.stepBy(61, -2) == 59);
    Scale penta = named(9, "minor-pentatonic"); // A C D E G
    CHECK(penta.stepBy(57, 1) == 60);
    CHECK(penta.stepBy(57, 5) == 69);
    CHECK(penta.stepBy(57, -1) == 55);
}

TEST_CASE("degreeOf, octaveOf and pitchOf are inverses, including below the root") {
    Scale a = named(9, "minor"); // A B C D E F G
    CHECK(a.degreeOf(57) == 0);
    CHECK(a.degreeOf(60) == 2);
    CHECK(a.degreeOf(61) == 2); // C# is off the scale: the degree below
    CHECK(a.degreeOf(55) == 6); // G below A
    CHECK(a.octaveOf(57) == 4);
    CHECK(a.octaveOf(55) == 3);
    CHECK(a.pitchOf(0, 4) == 57);
    CHECK(a.pitchOf(7, 4) == 69);  // degree 7 wraps to the next octave
    CHECK(a.pitchOf(-1, 4) == 55); // degree -1 wraps down
    for (int p = 0; p < 128; p++) {
        if (!a.inScale(p))
            continue;
        CHECK(a.pitchOf(a.degreeOf(p), a.octaveOf(p)) == p);
        CHECK(a.pitchAtIndex(a.indexOf(p)) == p);
    }
}

TEST_CASE("a chromatic scale steps by semitones") {
    Scale c = named(5, "chromatic");
    for (int p = 0; p < 127; p++) {
        CHECK(c.inScale(p));
        CHECK(c.stepBy(p, 1) == p + 1);
        CHECK(c.snap(p) == p);
    }
}

TEST_CASE("unknown and custom scale ids resolve") {
    std::vector<CustomScale> custom{{"custom-1", "Mine", 0b001010010101}};
    Scale mine = resolveScale(ScaleRef{0, "custom-1"}, custom);
    CHECK(mine.size() == 5);
    CHECK(mine.inScale(64));
    CHECK_FALSE(mine.inScale(65));
    CHECK(resolveScale(ScaleRef{0, "gone"}, custom).chromatic());
    CHECK(scaleName("custom-1", custom) == "Mine");
    CHECK(keyName(ScaleRef{0, "custom-1"}, custom) == "C Mine");
    CHECK(keyName(ScaleRef{9, "minor"}, {}) == "A Minor");
    CHECK(keyName(ScaleRef{3, "chromatic"}, {}) == "Chromatic");
    CHECK(keyName(ScaleRef{2, "custom-9"}, {{"custom-9", "Every note", kChromaticMask}}) == "D Every note");
    CHECK(keyName(ScaleRef{2, "gone"}, {}) == "Chromatic");
    CHECK(newCustomScaleId(custom) == "custom-2");
    // A mask without the root still keeps it.
    CHECK(resolveScale(ScaleRef{0, "custom-1"}, {{"custom-1", "x", 0b100}}).inScale(60));
}

TEST_CASE("moveToKey keeps degrees and moves the root the short way") {
    Scale c = named(0, "major");
    Scale d = named(2, "major");
    CHECK(moveToKey(64, c, d) == 66); // E -> F#
    CHECK(moveToKey(71, c, d) == 73); // B -> C#
    CHECK(moveToKey(61, c, d) == 63); // C# (off) keeps a semitone above its degree
    Scale b = named(11, "major");
    CHECK(moveToKey(60, c, b) == 59); // C -> B below, not above
    CHECK(moveToKey(59, b, c) == 60);
    Scale cm = named(0, "minor");
    CHECK(moveToKey(64, c, cm) == 63); // E -> Eb, same root
    CHECK(moveToKey(69, c, cm) == 68);
    // Different sizes: move with the root, then snap.
    Scale penta = named(0, "major-pentatonic");
    CHECK(moveToKey(65, c, penta) == 64);
    // Stays in range.
    CHECK(moveToKey(127, c, named(5, "major")) <= 127);
    CHECK(moveToKey(0, c, named(7, "major")) >= 0);
}

TEST_CASE("chord stamp builds fixed and diatonic chords with inversions and voicings") {
    Scale c = named(0, "major");
    CHECK(chordPitches(60, {ChordShape::Off}, c).empty());
    CHECK(chordPitches(60, {ChordShape::Major}, c) == std::vector<int>{60, 64, 67});
    CHECK(chordPitches(62, {ChordShape::DiatonicTriad}, c) == std::vector<int>{62, 65, 69});       // D minor
    CHECK(chordPitches(71, {ChordShape::DiatonicTriad}, c) == std::vector<int>{71, 74, 77});       // B dim
    CHECK(chordPitches(67, {ChordShape::DiatonicSeventh}, c) == std::vector<int>{67, 71, 74, 77}); // G7
    CHECK(chordPitches(61, {ChordShape::DiatonicTriad}, c) == std::vector<int>{60, 64, 67});       // snaps the root
    CHECK(chordPitches(60, {ChordShape::Major, 1}, c) == std::vector<int>{64, 67, 72});
    CHECK(chordPitches(60, {ChordShape::Major, 2}, c) == std::vector<int>{67, 72, 76});
    CHECK(chordPitches(60, {ChordShape::Major, 9}, c) == std::vector<int>{67, 72, 76}); // clamped
    CHECK(chordPitches(60, {ChordShape::Major, 0, ChordVoicing::Open}, c) == std::vector<int>{60, 67, 76});
    CHECK(chordPitches(60, {ChordShape::Major7, 0, ChordVoicing::Drop2}, c) == std::vector<int>{55, 60, 64, 71});
    CHECK(chordPitches(122, {ChordShape::Major}, c) == std::vector<int>{122, 126}); // out of range dropped
    // A chromatic key stacks major-scale thirds.
    CHECK(chordPitches(60, {ChordShape::DiatonicTriad}, named(0, "chromatic")) == std::vector<int>{60, 64, 67});
    for (const ChordShapeInfo& s : chordShapes())
        CHECK(chordPitches(60, {s.shape}, c).size() == size_t(chordSize(s.shape)));
}

TEST_CASE("changing the song key moves notes only in clips that follow it, in one undo step") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    song.scale = ScaleRef{0, "major"};
    REQUIRE(!song.clips.empty());
    ClipId melodic = song.clips.begin()->first;
    Clip& clip = song.clips.at(melodic);
    clip.notes.clear();
    Note e;
    e.id = song.newId<NoteId>();
    e.start = 0;
    e.length = kPpq;
    e.pitch = 64;
    clip.notes.push_back(e);

    Clip own = clip;
    own.id = song.newId<ClipId>();
    own.notes[0].id = song.newId<NoteId>();
    own.scaleOverride = ScaleRef{0, "major"};
    song.clips[own.id] = own;

    Song before = song;
    CHECK(keyChangeMovesNotes(song, clipsFollowingSongKey(song), named(0, "major"), named(2, "major")));
    CommandPtr keep = changeSongKey(song, ScaleRef{2, "major"}, false);
    keep->apply(song);
    CHECK(song.scale == ScaleRef{2, "major"});
    CHECK(song.clips.at(melodic).notes[0].pitch == 64);
    keep->revert(song);
    CHECK(song == before);

    CommandPtr move = changeSongKey(song, ScaleRef{2, "major"}, true);
    move->apply(song);
    CHECK(song.clips.at(melodic).notes[0].pitch == 66);
    CHECK(song.clips.at(own.id).notes[0].pitch == 64); // has its own key
    move->revert(song);
    CHECK(song == before);
    CHECK(changeSongKey(song, song.scale, true) == nullptr);
}

TEST_CASE("drum clips never move with the key") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    const Track& drums = song.tracks.back();
    REQUIRE(drums.type == TrackType::Drum);
    ClipId id = song.newId<ClipId>();
    Clip clip;
    clip.id = id;
    clip.length = kPpq * 4;
    Note kick;
    kick.id = song.newId<NoteId>();
    kick.length = 10;
    kick.pitch = 36;
    clip.notes.push_back(kick);
    song.clips[id] = clip;
    song.tracks.back().slots[0].clip = id;
    CHECK(usedByDrumTrack(song, id));
    auto follow = clipsFollowingSongKey(song);
    CHECK(std::find(follow.begin(), follow.end(), id) == follow.end());
    CommandPtr c = changeSongKey(song, ScaleRef{5, "minor"}, true);
    c->apply(song);
    CHECK(song.clips.at(id).notes[0].pitch == 36);
}

TEST_CASE("a clip key override moves its notes and undoes cleanly") {
    Song song = makeDefaultSong();
    addDemoClip(song);
    song.scale = ScaleRef{0, "major"};
    ClipId id = song.clips.begin()->first;
    Song before = song;
    CommandPtr c = changeClipKey(song, id, ScaleRef{0, "minor"}, true);
    REQUIRE(c);
    c->apply(song);
    REQUIRE(song.clips.at(id).scaleOverride);
    for (const Note& n : song.clips.at(id).notes)
        CHECK(n.pitch == moveToKey(before.clips.at(id).findNote(n.id)->pitch, named(0, "major"), named(0, "minor")));
    CommandPtr back = changeClipKey(song, id, std::nullopt, false);
    back->apply(song);
    CHECK_FALSE(song.clips.at(id).scaleOverride);
    back->revert(song);
    c->revert(song);
    CHECK(song == before);
}

TEST_CASE("custom scales are added, edited and deleted as commands") {
    Song song;
    CustomScale s{newCustomScaleId(song.customScales), "Mine", 0b101};
    CommandPtr add = putCustomScale(song, s);
    add->apply(song);
    REQUIRE(song.customScales.size() == 1);
    s.mask = 0b111;
    CommandPtr edit = putCustomScale(song, s);
    edit->apply(song);
    CHECK(song.customScales[0].mask == 0b111);
    CommandPtr del = deleteCustomScale(song, s.id);
    del->apply(song);
    CHECK(song.customScales.empty());
    del->revert(song);
    edit->revert(song);
    CHECK(song.customScales[0].mask == 0b101);
    CHECK(deleteCustomScale(song, "nope") == nullptr);
}

TEST_CASE("scale note edits: step, fit and move") {
    Scale c = named(0, "major");
    edits::NoteList notes(3);
    int pitches[] = {60, 61, 64};
    for (int i = 0; i < 3; i++) {
        notes[size_t(i)].id = NoteId(uint64_t(i + 1));
        notes[size_t(i)].start = i;
        notes[size_t(i)].length = 1;
        notes[size_t(i)].pitch = int8_t(pitches[i]);
    }
    edits::IdSet all = edits::idsOf(notes);
    auto up = edits::stepInScale(notes, all, c, 1);
    CHECK(up[0].pitch == 62);
    CHECK(up[1].pitch == 62);
    CHECK(up[2].pitch == 65);
    auto fit = edits::fitToScale(notes, all, c);
    CHECK(fit[1].pitch == 60);
    auto moved = edits::moveToKey(notes, all, c, named(2, "major"));
    CHECK(moved[2].pitch == 66);
}

TEST_CASE("in the key, chord shapes stack key steps (as fold shows them)") {
    Scale c = named(0, "major");
    // A major triad on D in C major: D F A, two key steps and two more (D minor), not D F# A.
    CHECK(chordPitches(62, {ChordShape::Major}, c, true) == std::vector<int>{62, 65, 69});
    CHECK(chordPitches(62, {ChordShape::Major}, c, false) == std::vector<int>{62, 66, 69});
    CHECK(chordPitches(60, {ChordShape::Major}, c, true) == std::vector<int>{60, 64, 67});
    CHECK(chordPitches(64, {ChordShape::Major7}, c, true) == std::vector<int>{64, 67, 71, 74}); // E G B D
    CHECK(chordPitches(60, {ChordShape::Sus4}, c, true) == std::vector<int>{60, 65, 67});
    CHECK(chordPitches(60, {ChordShape::Octave}, c, true) == std::vector<int>{60, 72});
    // Every note lands in the key, for every shape and degree.
    Scale a = named(9, "minor");
    for (const ChordShapeInfo& s : chordShapes())
        for (int root = 57; root < 69; root++)
            for (int p : chordPitches(root, {s.shape}, a, true))
                CHECK(a.inScale(p));
    // Without a key it stays the plain shape.
    CHECK(chordPitches(62, {ChordShape::Major}, named(0, "chromatic"), true) == std::vector<int>{62, 66, 69});
}
