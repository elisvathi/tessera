#pragma once

#include "NoteEdits.hpp"

#include <string>

/// The note properties the inspector edits, and the multi-note edits it offers (EDITOR_UX §4.7): offset, spread,
/// randomize, ramp, set, and typed expressions. Values are in display units (percent, semitones, beats...), so the
/// inspector and typed input agree with what is shown.
namespace tessera::fields {

enum class Field {
    Start,
    Length,
    Pitch,
    Velocity,
    Release,
    Micro,
    Chance,
    VelocitySpread,
    Repeats,
    RepeatCurve,
    RepeatVelocityEnd,
    RepeatGate,
    TimingSpread,
    Channel,
    Count
};

struct Info {
    const char* name;
    const char* unit; // shown after the value
    double min, max;  // display units
    double step;      // smallest change by dragging one pixel
    bool integer;
};
const Info& info(Field f);
/// "velocity", "repeat gate": the name inside a sentence, as in undo names.
std::string lowerName(Field f);

/// A note's value in display units, and setting it (clamped to the field's range).
double get(const Note& note, Field f);
void set(Note& note, Field f, double value);

/// The lowest and highest value among `ids`.
std::pair<double, double> range(const edits::NoteList& notes, const edits::IdSet& ids, Field f);

/// Adds `delta` to every selected note, keeping their differences (each clamped).
edits::NoteList offset(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double delta);
/// Moves values away from (factor > 1) or toward (factor < 1) the middle of their range; 0 makes them equal.
edits::NoteList spread(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double factor);
/// The average value among `ids` (0 when none).
double mean(const edits::NoteList& notes, const edits::IdSet& ids, Field f);
/// Histogram Spread: moves values away from (factor > 1) or toward (factor < 1) their mean; 0 makes them
/// all the mean.
edits::NoteList spreadAroundMean(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double factor);
/// Histogram Chaos: moves each value by a random amount of up to `amount` (0..1) of the field's full range,
/// up or down. The same seed picks the same directions, so dragging further only scatters further.
edits::NoteList chaos(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double amount, uint32_t seed);
/// How many of `ids` fall into each of `buckets` equal steps between `low` and `high` (values outside count in the
/// first or last).
std::vector<int> histogram(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double low, double high,
                           int buckets);
/// Random values between `low` and `high`, the same for the same seed.
edits::NoteList randomize(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double low, double high,
                          uint32_t seed);
/// Values from `first` to `last` in the notes' time order (a curve of 0 is straight, positive bends it up early).
edits::NoteList ramp(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double first, double last,
                     double curve = 0);
edits::NoteList setAll(const edits::NoteList& notes, const edits::IdSet& ids, Field f, double value);

/// A typed edit: "64" or "=64" sets ("=-20" sets a negative value), "+10" / "-10" adds or subtracts, "+5%" /
/// "-5%" changes by that share of each value, "*0.8" / "/2" scales. Start is set by a position as the ruler
/// writes it ("2.1.1", "2.3", "2"); numbers for start and length count beats of `ts`. Returns false (and leaves
/// `out` alone) when the text isn't one of these.
bool applyTyped(const edits::NoteList& notes, const edits::IdSet& ids, Field f, const std::string& text,
                edits::NoteList& out, TimeSig ts = TimeSig());

/// "64%", "C4", "1.4.1" (start, as the ruler writes it), "1 bar 2 beats": a value as the inspector shows it.
std::string format(Field f, double value, TimeSig ts = TimeSig());
/// A field's value on a new note (for double-click to reset).
double defaultValue(Field f);

/// The operators as on/off switches: each is off (the note always plays as written) or on.
enum class Operator { Chance, Repeats, Spread, Occurrence, Recurrence, Count };
const char* operatorName(Operator op);
bool isOn(const Note& note, Operator op);
/// Turns an operator on with a starting value that is heard at once (chance 50%, 2 repeats, every second
/// pass...), or off by putting its fields back to their defaults. Notes that already have it on keep their
/// values when it is turned on.
edits::NoteList setOperator(const edits::NoteList& notes, const edits::IdSet& ids, Operator op, bool on);
/// Recurrence: pass `pass` (0-based) on or off; a pass past the cycle's end makes the cycle that long first.
edits::NoteList setRecurrencePass(const edits::NoteList& notes, const edits::IdSet& ids, int pass, bool on);
/// Recurrence: a cycle of `length` passes. New passes play.
edits::NoteList setRecurrenceLength(const edits::NoteList& notes, const edits::IdSet& ids, int length);

} // namespace tessera::fields
