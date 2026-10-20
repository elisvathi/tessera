#pragma once

#include "ScaleRef.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace tessera {

/// A built-in scale. The id is saved in patches, so it never changes; the name is for display.
struct ScaleDef {
    const char* id;
    const char* name;
    uint16_t mask; // bit n set = n semitones above the root is in the scale
};

const std::vector<ScaleDef>& builtinScales();

constexpr uint16_t kChromaticMask = 0xFFF;

/// A key ready for pitch questions: a root (0 = C .. 11 = B) and a 12-bit mask. The root is always in.
/// Pitches are MIDI note numbers. Results that would leave 0..127 stay at the nearest scale note inside it.
class Scale {
  public:
    Scale() = default;
    Scale(int root, uint16_t mask);

    int root() const { return rootPc; }
    uint16_t mask() const { return bits; }
    /// Notes per octave (7 for major, 5 for pentatonics, 12 for chromatic).
    int size() const { return int(offsets.size()); }
    bool chromatic() const { return bits == kChromaticMask; }

    bool inScale(int pitch) const;
    bool isRoot(int pitch) const;
    /// The nearest scale note. direction > 0: at or above; < 0: at or below; 0: nearest (ties go down).
    int snap(int pitch, int direction = 0) const;
    /// Moves by scale steps. A pitch outside the scale counts as sitting on the scale note in the move's
    /// direction, so one step up from C# in C major lands on D.
    int stepBy(int pitch, int steps) const;
    /// The degree (0 = root) of the scale note at or below the pitch.
    int degreeOf(int pitch) const;
    /// The octave of the pitch counted from the root: C4 (60) in C is octave 5, B3 (59) in C is octave 4.
    int octaveOf(int pitch) const;
    /// The pitch of a degree in an octave. Degrees past the scale size wrap into the next octaves, negative
    /// degrees into the ones below. May leave 0..127.
    int pitchOf(int degree, int octave) const;
    /// Degree counted from pitch 0's octave, so stepping is adding: indexOf(stepBy(p, n)) == indexOf(p) + n.
    int indexOf(int pitch) const;
    int pitchAtIndex(int index) const { return pitchOf(index, 0); }

    bool operator==(const Scale& o) const { return rootPc == o.rootPc && bits == o.bits; }

  private:
    int keepInRange(int pitch) const;

    int rootPc = 0;
    uint16_t bits = kChromaticMask;
    std::vector<int> offsets{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}; // semitones above the root, ascending
};

/// The scale a key names. An unknown id (a deleted custom scale) gives chromatic, which never constrains.
Scale resolveScale(const ScaleRef& key, const std::vector<CustomScale>& custom);

/// "Minor", "Dorian", or the custom scale's name. Unknown ids give "Chromatic".
std::string scaleName(const std::string& id, const std::vector<CustomScale>& custom);
/// How black keys are named everywhere: "C#" or "Db". A display preference saved with the plugin's settings, not
/// part of the song. UI thread only.
enum class Accidentals { Sharps, Flats };
Accidentals accidentals();
void setAccidentals(Accidentals a);
/// A pitch class's name (0..11) in the chosen spelling: "C#" or "Db".
const char* pitchClassName(int pc);
/// "C", "C#" ... "B" (or "Db" ... with flats).
std::string rootName(int root);
bool knownScale(const std::string& id, const std::vector<CustomScale>& custom);
/// "C Minor"; the chromatic scale (or a deleted custom one) has no root, so just "Chromatic".
std::string keyName(const ScaleRef& key, const std::vector<CustomScale>& custom);

/// Moves a pitch into another key keeping its degree: E in C major becomes F# in D major. The root moves the
/// short way round (B to C goes up a semitone, not down eleven). Notes outside the old scale keep their
/// distance above the degree below them. Between scales of different sizes the degree can't be kept, so the
/// pitch moves with the root and snaps to the new scale.
int moveToKey(int pitch, const Scale& from, const Scale& to);

/// The next free "custom-N" id.
std::string newCustomScaleId(const std::vector<CustomScale>& custom);

} // namespace tessera
