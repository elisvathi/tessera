#pragma once

#include "Scale.hpp"

#include <vector>

namespace tessera {

/// Chord stamp shapes (EDITOR_UX §4.2). Diatonic shapes stack every other note of the key, so the chord's
/// quality follows the clicked degree (D in C major gives D minor).
enum class ChordShape : int {
    Off,
    DiatonicTriad,
    DiatonicSeventh,
    DiatonicNinth,
    Major,
    Minor,
    Diminished,
    Augmented,
    Sus2,
    Sus4,
    Major7,
    Minor7,
    Dominant7,
    HalfDiminished7,
    Diminished7,
    Add9,
    Power,
    Octave,
};

enum class ChordVoicing : int {
    Close,
    Open,  // the second note from the bottom goes up an octave
    Drop2, // the second note from the top goes down an octave
};

struct ChordStamp {
    ChordShape shape = ChordShape::Off;
    int inversion = 0; // how many bottom notes move up an octave
    ChordVoicing voicing = ChordVoicing::Close;

    bool on() const { return shape != ChordShape::Off; }
    bool operator==(const ChordStamp& o) const {
        return shape == o.shape && inversion == o.inversion && voicing == o.voicing;
    }
};

struct ChordShapeInfo {
    ChordShape shape;
    const char* name;
    bool diatonic;
};

/// Every shape except Off, in menu order.
const std::vector<ChordShapeInfo>& chordShapes();
const char* chordShapeName(ChordShape shape);
/// Notes in the shape before inversions (3 for a triad).
int chordSize(ChordShape shape);

/// The pitches of a chord on `root`, low to high, inside 0..127. Diatonic shapes first snap the root to the
/// key. Empty when the stamp is off.
///
/// With `inKey` (the key shapes editing: scale fold or snap), every shape is built in the key's steps: its
/// intervals are read as steps of the key from its root, so a major triad stacks two steps and two more (thirds
/// of the key) on any degree, as the folded rows show it.
std::vector<int> chordPitches(int root, const ChordStamp& stamp, const Scale& key, bool inKey = false);
/// The same without allocating, for the audio thread: fills `out` (room for `capacity`), returns how many. Call
/// warmUpChords() once first, off the audio thread.
constexpr int kMaxChordNotes = 8;
int chordPitches(int root, const ChordStamp& stamp, const Scale& key, bool inKey, int* out, int capacity);
void warmUpChords();

} // namespace tessera
