#include "Scale.hpp"

#include <algorithm>

namespace tessera {

namespace {

constexpr uint16_t maskOf(std::initializer_list<int> semitones) {
    uint16_t m = 0;
    for (int s : semitones)
        m = uint16_t(m | (1 << s));
    return m;
}

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int floorMod(int a, int b) { return a - floorDiv(a, b) * b; }

} // namespace

const std::vector<ScaleDef>& builtinScales() {
    static const std::vector<ScaleDef> scales = {
        {"major", "Major", maskOf({0, 2, 4, 5, 7, 9, 11})},
        {"minor", "Minor", maskOf({0, 2, 3, 5, 7, 8, 10})},
        {"harmonic-minor", "Harmonic Minor", maskOf({0, 2, 3, 5, 7, 8, 11})},
        {"melodic-minor", "Melodic Minor", maskOf({0, 2, 3, 5, 7, 9, 11})},
        {"dorian", "Dorian", maskOf({0, 2, 3, 5, 7, 9, 10})},
        {"phrygian", "Phrygian", maskOf({0, 1, 3, 5, 7, 8, 10})},
        {"lydian", "Lydian", maskOf({0, 2, 4, 6, 7, 9, 11})},
        {"mixolydian", "Mixolydian", maskOf({0, 2, 4, 5, 7, 9, 10})},
        {"locrian", "Locrian", maskOf({0, 1, 3, 5, 6, 8, 10})},
        {"major-pentatonic", "Major Pentatonic", maskOf({0, 2, 4, 7, 9})},
        {"minor-pentatonic", "Minor Pentatonic", maskOf({0, 3, 5, 7, 10})},
        {"blues", "Blues", maskOf({0, 3, 5, 6, 7, 10})},
        {"whole-tone", "Whole Tone", maskOf({0, 2, 4, 6, 8, 10})},
        {"hungarian-minor", "Hungarian Minor", maskOf({0, 2, 3, 6, 7, 8, 11})},
        {"phrygian-dominant", "Phrygian Dominant", maskOf({0, 1, 4, 5, 7, 8, 10})},
        {"japanese-in", "Japanese (In)", maskOf({0, 1, 5, 7, 8})},
        {"hirajoshi", "Hirajoshi", maskOf({0, 2, 3, 7, 8})},
        {"double-harmonic", "Double Harmonic", maskOf({0, 1, 4, 5, 7, 8, 11})},
        {"bebop-dominant", "Bebop Dominant", maskOf({0, 2, 4, 5, 7, 9, 10, 11})},
        {"chromatic", "Chromatic", kChromaticMask},
    };
    return scales;
}

Scale::Scale(int root, uint16_t mask) : rootPc(floorMod(root, 12)), bits(uint16_t((mask & kChromaticMask) | 1)) {
    offsets.clear();
    for (int s = 0; s < 12; s++)
        if (bits & (1 << s))
            offsets.push_back(s);
}

bool Scale::inScale(int pitch) const { return bits & (1 << floorMod(pitch - rootPc, 12)); }

bool Scale::isRoot(int pitch) const { return floorMod(pitch - rootPc, 12) == 0; }

int Scale::octaveOf(int pitch) const { return floorDiv(pitch - rootPc, 12); }

int Scale::degreeOf(int pitch) const {
    int semis = floorMod(pitch - rootPc, 12);
    auto it = std::upper_bound(offsets.begin(), offsets.end(), semis);
    return int(it - offsets.begin()) - 1; // offsets[0] is 0, so never below 0
}

int Scale::pitchOf(int degree, int octave) const {
    int n = size();
    return rootPc + 12 * (octave + floorDiv(degree, n)) + offsets[size_t(floorMod(degree, n))];
}

int Scale::indexOf(int pitch) const { return octaveOf(pitch) * size() + degreeOf(pitch); }

int Scale::keepInRange(int pitch) const {
    // The lowest and highest scale notes inside 0..127. The root's pitch class is always in, so both exist.
    if (pitch > 127)
        return pitchAtIndex(indexOf(127));
    if (pitch < 0) {
        int i = indexOf(0);
        return inScale(0) ? 0 : pitchAtIndex(i + 1);
    }
    return pitch;
}

int Scale::snap(int pitch, int direction) const {
    if (inScale(pitch))
        return keepInRange(pitch);
    int below = pitchAtIndex(indexOf(pitch));
    int above = pitchAtIndex(indexOf(pitch) + 1);
    int result;
    if (direction > 0)
        result = above;
    else if (direction < 0)
        result = below;
    else
        result = pitch - below <= above - pitch ? below : above;
    return keepInRange(result);
}

int Scale::stepBy(int pitch, int steps) const {
    if (steps == 0)
        return snap(pitch);
    // Off the scale, indexOf points at the note below, which is already one step down.
    if (!inScale(pitch) && steps < 0)
        steps++;
    return keepInRange(pitchAtIndex(indexOf(pitch) + steps));
}

Scale resolveScale(const ScaleRef& key, const std::vector<CustomScale>& custom) {
    for (const ScaleDef& d : builtinScales())
        if (key.scale == d.id)
            return Scale(key.root, d.mask);
    for (const CustomScale& c : custom)
        if (key.scale == c.id)
            return Scale(key.root, c.mask);
    return Scale(key.root, kChromaticMask);
}

std::string scaleName(const std::string& id, const std::vector<CustomScale>& custom) {
    for (const ScaleDef& d : builtinScales())
        if (id == d.id)
            return d.name;
    for (const CustomScale& c : custom)
        if (id == c.id)
            return c.name;
    return "Chromatic";
}

namespace {
Accidentals spelling = Accidentals::Sharps;
} // namespace

Accidentals accidentals() { return spelling; }

void setAccidentals(Accidentals a) { spelling = a; }

const char* pitchClassName(int pc) {
    static const char* const sharps[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* const flats[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    return (spelling == Accidentals::Flats ? flats : sharps)[floorMod(pc, 12)];
}

std::string rootName(int root) { return pitchClassName(root); }

bool knownScale(const std::string& id, const std::vector<CustomScale>& custom) {
    return std::any_of(builtinScales().begin(), builtinScales().end(), [&](const ScaleDef& d) { return id == d.id; }) ||
           std::any_of(custom.begin(), custom.end(), [&](const CustomScale& c) { return id == c.id; });
}

std::string keyName(const ScaleRef& key, const std::vector<CustomScale>& custom) {
    if (key.scale == "chromatic" || !knownScale(key.scale, custom))
        return "Chromatic";
    return rootName(key.root) + " " + scaleName(key.scale, custom);
}

int moveToKey(int pitch, const Scale& from, const Scale& to) {
    int rootShift = floorMod(to.root() - from.root() + 6, 12) - 6; // -6..5: the short way round
    int moved;
    if (from.size() == to.size()) {
        int index = from.indexOf(pitch);
        int chromatic = pitch - from.pitchAtIndex(index);
        // pitchAtIndex counts octaves from each scale's own root; this lines the new root up with the old
        // root moved by rootShift.
        int octaveFix = from.root() + rootShift - to.root();
        moved = to.pitchAtIndex(index) + chromatic + octaveFix;
    } else {
        moved = to.snap(pitch + rootShift);
    }
    while (moved > 127)
        moved -= 12;
    while (moved < 0)
        moved += 12;
    return moved;
}

std::string newCustomScaleId(const std::vector<CustomScale>& custom) {
    for (int n = 1;; n++) {
        std::string id = "custom-" + std::to_string(n);
        if (std::none_of(custom.begin(), custom.end(), [&](const CustomScale& c) { return c.id == id; }))
            return id;
    }
}

} // namespace tessera
