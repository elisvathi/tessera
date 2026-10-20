#pragma once

#include "NoteEdits.hpp"
#include "Scale.hpp"

#include <cstdint>

/// Note transforms (EDITOR_UX §4.8): pure functions from a clip's notes to new notes. Each changes only the notes
/// in `ids` (some add notes, and the ones that replace notes, like arpeggiate, remove them). The result is sorted.
namespace tessera::transforms {

using edits::IdSet;
using edits::IdSource;
using edits::NoteList;

// ---- Timing ----

enum class Ends { Start, End, Both };

struct QuantizeOptions {
    Tick grid = kPpq / 4;
    float strength = 1; // 0..1: how far notes move toward the grid
    float swing = 0;    // 0..1: every second grid step moves later, up to half a step
    Ends ends = Ends::Start;
    Tick range = 0; // notes closer to the grid than this stay where they are (Q-range)
};
NoteList quantize(const NoteList& notes, const IdSet& ids, const QuantizeOptions& o);

struct HumanizeOptions {
    Tick timing = 20;   // starts move up to ± this
    float velocity = 0; // ± this (0..1)
    float length = 0;   // lengths change by up to ± this share
    uint32_t seed = 1;
};
NoteList humanize(const NoteList& notes, const IdSet& ids, const HumanizeOptions& o);

/// Each note lasts until the next one starts (or the next of the same pitch), plus `overlap` (may be negative).
NoteList legato(const NoteList& notes, const IdSet& ids, bool samePitch, Tick overlap);

enum class StrumDirection { Up, Down, Alternate };
/// Notes that start together are spread out, `step` apart (low to high for Up). Ends stay put. A positive curve
/// makes the gaps grow, a negative one shrink.
NoteList strum(const NoteList& notes, const IdSet& ids, StrumDirection direction, Tick step, float curve);

/// A short grace note `offset` before each note, at `velocity` times its velocity.
NoteList flam(const NoteList& notes, const IdSet& ids, Tick offset, float velocity, const IdSource& newId);

/// Splits each note into `parts` equal parts, or at every `grid` line when grid > 0. Velocity ramps from the
/// note's to `velocityEnd` times it.
NoteList chop(const NoteList& notes, const IdSet& ids, int parts, Tick grid, float velocityEnd, const IdSource& newId);

/// Joins notes of the same pitch that touch or overlap into one.
NoteList glue(const NoteList& notes, const IdSet& ids);

// ---- Pitch ----

enum class ArpPattern { Up, Down, UpDown, Random, AsPlayed };
struct ArpOptions {
    ArpPattern pattern = ArpPattern::Up;
    Tick rate = kPpq / 4;
    float gate = 0.8f; // share of a step each note lasts
    int octaves = 1;
    uint32_t seed = 1;
};
/// The chosen notes become an arpeggio: every `rate`, one of the notes held at that moment (EDITOR_UX §4.8).
NoteList arpeggiate(const NoteList& notes, const IdSet& ids, const ArpOptions& o, const IdSource& newId);

/// Fills the gap between each note and the next with passing notes in the key, one per `rate`.
NoteList connect(const NoteList& notes, const IdSet& ids, const Scale& scale, Tick rate, const IdSource& newId);

/// Mirrors the notes in time within their span.
NoteList reverse(const NoteList& notes, const IdSet& ids);
/// Plays the pitches in reverse order on the same rhythm.
NoteList retrograde(const NoteList& notes, const IdSet& ids);
/// Mirrors pitches around `pivot`, in key steps (so the result stays in the key).
NoteList invert(const NoteList& notes, const IdSet& ids, int pivot, const Scale& scale);

/// Moves by semitones, or by key steps when `inKey`.
NoteList transpose(const NoteList& notes, const IdSet& ids, int amount, bool inKey, const Scale& scale);

// ---- Values ----

enum class ScaleMode { Percent, Set };
/// Percent: value times `amount`. Set: every value becomes `amount`.
NoteList scaleVelocity(const NoteList& notes, const IdSet& ids, ScaleMode mode, float amount);
NoteList scaleLength(const NoteList& notes, const IdSet& ids, ScaleMode mode, double amount);

enum class Shape { RampUp, RampDown, Sine, Triangle, Random };
enum class ShapeMode { Multiply, Add, Replace };
/// Shapes velocities over the selection's time span (velocity shaper): a shape repeated `cycles`
/// times, `depth` deep, multiplied with, added to, or replacing each velocity.
NoteList shapeVelocity(const NoteList& notes, const IdSet& ids, Shape shape, ShapeMode mode, float depth, float cycles,
                       uint32_t seed);

struct RandomizeOptions {
    int pitch = 0;      // ± semitones (key steps when the scale isn't chromatic)
    float velocity = 0; // ± (0..1)
    Tick timing = 0;    // ± ticks
    float length = 0;   // ± share
    uint32_t seed = 1;
};
NoteList randomize(const NoteList& notes, const IdSet& ids, const RandomizeOptions& o, const Scale& scale);

// ---- Creating and selecting ----

/// Adds a Euclidean rhythm at `pitch`: `steps` steps of `stepLength` from `start`, `hits` of them played.
NoteList euclidean(const NoteList& notes, int hits, int steps, int rotation, int pitch, Tick start, Tick stepLength,
                   float velocity, const IdSource& newId, IdSet* added = nullptr);

enum class SelectBy { SamePitch, VelocityRange, OffBeats, OnBeats, EveryNth, Muted, WithOperators };
struct SelectOptions {
    SelectBy by = SelectBy::SamePitch;
    int pitch = 60;            // SamePitch
    float low = 0, high = 1;   // VelocityRange
    Tick grid = kPpq;          // On/OffBeats: positions on (or off) this grid
    int every = 2, offset = 0; // EveryNth, in time order
};
/// The notes among `ids` (all notes when empty) that match.
IdSet select(const NoteList& notes, const IdSet& ids, const SelectOptions& o);

} // namespace tessera::transforms
