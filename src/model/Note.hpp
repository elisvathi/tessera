#pragma once

#include "Types.hpp"

#include <vector>

namespace tessera {

enum class Occurrence : uint8_t { Always, First, NotFirst, PrevPlayed, PrevNotPlayed, Fill, NotFill };

/// Per-note playback operators (EDITOR_UX §4.5). The engine evaluates them on each pass;
/// the written note never changes.
struct NoteOperators {
    float chance = 1;         // 0..1
    float velocitySpread = 0; // 0..1
    int8_t repeats = 0;       // -16..16, negative = repeats per beat, 0 = off
    float repeatCurve = 0;    // -1..1
    float repeatVelEnd = 1;   // 0..1
    float repeatGate = 0.5f;  // 0..1
    Occurrence occurrence = Occurrence::Always;
    uint8_t recurrenceLength = 1; // 1..16 passes
    uint16_t recurrenceMask = 0xFFFF;
    float timingSpread = 0; // 0..0.5 of a 1/16

    bool operator==(const NoteOperators& o) const {
        return chance == o.chance && velocitySpread == o.velocitySpread && repeats == o.repeats &&
               repeatCurve == o.repeatCurve && repeatVelEnd == o.repeatVelEnd && repeatGate == o.repeatGate &&
               occurrence == o.occurrence && recurrenceLength == o.recurrenceLength &&
               recurrenceMask == o.recurrenceMask && timingSpread == o.timingSpread;
    }
};

struct ExprPoint {
    float pos; // 0..1 across the note
    float value;
    float tension;

    bool operator==(const ExprPoint& o) const { return pos == o.pos && value == o.value && tension == o.tension; }
};

struct Note {
    NoteId id;
    Tick start = 0;
    Tick length = kPpq / 4; // >= 1
    int8_t pitch = 60;      // MIDI note number, 0..127
    float velocity = 0.8f;  // 0..1
    float releaseVelocity = 0.5f;
    float microOffset = 0; // fraction of a 1/16, -0.5..0.5, applied at playback
    bool muted = false;
    uint8_t colorTag = 0; // 0 = inherit from clip
    uint8_t channel = 1;  // 1..16, like MIDI: lets one clip hold several parts that devices and export tell apart
    NoteOperators ops;
    // Per-note expression curves; empty means flat. Pitch is in semitones (±48, MPE's default range), the others 0..1.
    std::vector<ExprPoint> pitchExpr, timbreExpr, pressureExpr;

    Tick end() const { return start + length; }

    bool operator==(const Note& o) const {
        return id == o.id && start == o.start && length == o.length && pitch == o.pitch && velocity == o.velocity &&
               releaseVelocity == o.releaseVelocity && microOffset == o.microOffset && muted == o.muted &&
               colorTag == o.colorTag && channel == o.channel && ops == o.ops && pitchExpr == o.pitchExpr &&
               timbreExpr == o.timbreExpr && pressureExpr == o.pressureExpr;
    }
};

/// The order notes are kept in inside a clip: by start, then pitch. The ID breaks ties, so the order is total
/// and undo always restores exactly the same list.
inline bool noteOrder(const Note& a, const Note& b) {
    if (a.start != b.start)
        return a.start < b.start;
    if (a.pitch != b.pitch)
        return a.pitch < b.pitch;
    return a.id < b.id;
}

constexpr int kMaxNoteChannel = 16;

/// MIDI pitch to 1V/oct, with C4 (60) at 0 V.
inline float pitchToVolts(int pitch) { return (pitch - 60) / 12.f; }

} // namespace tessera
