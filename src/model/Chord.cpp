#include "Chord.hpp"

#include <algorithm>

namespace tessera {

namespace {

/// The shape's semitones above its root, into `out` (at most 4). Returns how many.
int intervalsOf(ChordShape shape, int* out) {
    auto put = [out](std::initializer_list<int> list) {
        int n = 0;
        for (int v : list)
            out[n++] = v;
        return n;
    };
    switch (shape) {
    case ChordShape::Major:
        return put({0, 4, 7});
    case ChordShape::Minor:
        return put({0, 3, 7});
    case ChordShape::Diminished:
        return put({0, 3, 6});
    case ChordShape::Augmented:
        return put({0, 4, 8});
    case ChordShape::Sus2:
        return put({0, 2, 7});
    case ChordShape::Sus4:
        return put({0, 5, 7});
    case ChordShape::Major7:
        return put({0, 4, 7, 11});
    case ChordShape::Minor7:
        return put({0, 3, 7, 10});
    case ChordShape::Dominant7:
        return put({0, 4, 7, 10});
    case ChordShape::HalfDiminished7:
        return put({0, 3, 6, 10});
    case ChordShape::Diminished7:
        return put({0, 3, 6, 9});
    case ChordShape::Add9:
        return put({0, 4, 7, 14});
    case ChordShape::Power:
        return put({0, 7});
    case ChordShape::Octave:
        return put({0, 12});
    default:
        return 0;
    }
}

/// Major on each root: the stand-in key for chords in key when there is no key.
const Scale& majorOn(int root) {
    static const std::vector<Scale> keys = [] {
        std::vector<Scale> k;
        for (int r = 0; r < 12; r++)
            k.emplace_back(r, builtinScales().front().mask);
        return k;
    }();
    return keys[size_t(((root % 12) + 12) % 12)];
}

} // namespace

const std::vector<ChordShapeInfo>& chordShapes() {
    static const std::vector<ChordShapeInfo> shapes = {
        {ChordShape::DiatonicTriad, "Triad in key", true},
        {ChordShape::DiatonicSeventh, "7th in key", true},
        {ChordShape::DiatonicNinth, "9th in key", true},
        {ChordShape::Major, "Major", false},
        {ChordShape::Minor, "Minor", false},
        {ChordShape::Diminished, "Diminished", false},
        {ChordShape::Augmented, "Augmented", false},
        {ChordShape::Sus2, "Sus2", false},
        {ChordShape::Sus4, "Sus4", false},
        {ChordShape::Major7, "Major 7", false},
        {ChordShape::Minor7, "Minor 7", false},
        {ChordShape::Dominant7, "Dominant 7", false},
        {ChordShape::HalfDiminished7, "Half-diminished 7", false},
        {ChordShape::Diminished7, "Diminished 7", false},
        {ChordShape::Add9, "Add 9", false},
        {ChordShape::Power, "Power (5th)", false},
        {ChordShape::Octave, "Octave", false},
    };
    return shapes;
}

const char* chordShapeName(ChordShape shape) {
    for (const ChordShapeInfo& s : chordShapes())
        if (s.shape == shape)
            return s.name;
    return "Off";
}

int chordSize(ChordShape shape) {
    switch (shape) {
    case ChordShape::Off:
        return 0;
    case ChordShape::DiatonicTriad:
        return 3;
    case ChordShape::DiatonicSeventh:
        return 4;
    case ChordShape::DiatonicNinth:
        return 5;
    default: {
        int out[8];
        return intervalsOf(shape, out);
    }
    }
}

void warmUpChords() { majorOn(0); }

int chordPitches(int root, const ChordStamp& stamp, const Scale& key, bool inKey, int* out, int capacity) {
    int notes[kMaxChordNotes];
    int count = std::min(chordSize(stamp.shape), kMaxChordNotes);
    if (count == 0 || capacity <= 0)
        return 0;
    bool diatonic = stamp.shape == ChordShape::DiatonicTriad || stamp.shape == ChordShape::DiatonicSeventh ||
                    stamp.shape == ChordShape::DiatonicNinth;
    int iv[8];
    int n = 0;
    if (inKey && !diatonic && !key.chromatic()) {
        // Each interval becomes the number of key steps it spans from the key's root (4 semitones in a major key
        // is 2 steps), then those steps are stacked on the clicked degree.
        int base = key.indexOf(key.root());
        int start = key.indexOf(key.snap(root));
        int k = intervalsOf(stamp.shape, iv);
        for (int i = 0; i < k; i++)
            notes[n++] = key.pitchAtIndex(start + key.indexOf(key.snap(key.root() + iv[i])) - base);
    } else if (diatonic) {
        // Thirds can't be stacked in a chromatic key; major is the least surprising stand-in.
        const Scale& k = key.chromatic() ? majorOn(key.root()) : key;
        int index = k.indexOf(k.snap(root));
        for (int i = 0; i < count; i++)
            notes[n++] = k.pitchAtIndex(index + 2 * i);
    } else {
        int k = intervalsOf(stamp.shape, iv);
        for (int i = 0; i < k; i++)
            notes[n++] = root + iv[i];
    }

    // Inversions: positive moves bottom notes up an octave, negative moves top notes down.
    for (int i = 0; i < std::clamp(stamp.inversion, 0, n - 1); i++) {
        int low = notes[0] + 12;
        std::move(notes + 1, notes + n, notes);
        notes[n - 1] = low;
    }
    for (int i = 0; i < std::clamp(-stamp.inversion, 0, n - 1); i++) {
        int high = notes[n - 1] - 12;
        std::move_backward(notes, notes + n - 1, notes + n);
        notes[0] = high;
    }
    if (n >= 3 && stamp.voicing == ChordVoicing::Open)
        notes[1] += 12;
    else if (n >= 3 && stamp.voicing == ChordVoicing::Drop2)
        notes[n - 2] -= 12;
    std::sort(notes, notes + n);
    int kept = 0;
    for (int i = 0; i < n && kept < capacity; i++) {
        if (notes[i] < 0 || notes[i] > 127 || (kept > 0 && out[kept - 1] == notes[i]))
            continue;
        out[kept++] = notes[i];
    }
    return kept;
}

std::vector<int> chordPitches(int root, const ChordStamp& stamp, const Scale& key, bool inKey) {
    int notes[kMaxChordNotes];
    int n = chordPitches(root, stamp, key, inKey, notes, kMaxChordNotes);
    return std::vector<int>(notes, notes + n);
}

} // namespace tessera
