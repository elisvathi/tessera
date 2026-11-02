#pragma once

#include "Automation.hpp"
#include "Device.hpp"
#include "Scale.hpp"
#include "Types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/// How playheads move through a clip (docs/design/PLAYHEADS.md §4–5). Pure and allocation free: the engine runs
/// it on the audio thread, MIDI export and the editor's drawing run the same code.
///
/// Three kinds of time meet here:
/// - elapsed: song ticks since the clip started (what the caller has);
/// - travel: how far the playhead has moved, in clip ticks (elapsed times the rate, then bent by the bend shape,
///   whose cycle is counted in the playhead's own time);
/// - position: where in the clip it is, after region wraps, repeats, ping-pong turns and the start.
namespace tessera::playheads {

/// Setting positions in a playhead's parameter list (DeviceTypes.cpp). Append-only, like the list.
namespace knob {
enum Playhead {
    kRate,
    kDirection,
    kStart,
    kRegionStart,
    kRegionLength,
    kRepeatSize,
    kRepeatTimes,
    kBendShape,
    kBendAmount,
    kBendCycle,
    kOffset,
    kLength,
    kFixedLength,
    kPath,      // From settings, or Drawn (the playhead's path points)
    kPathCycle, // how long a drawn path lasts before it repeats
    kOctave,
    kKeySteps,  // moves its notes by steps of the clip's key
    kNotes,     // All, or only the lowest or highest of notes that start together
    kInvert,    // mirrors its notes' pitches inside the clip's range
    kVelocity,  // added to its notes' velocity
    kOrder,     // reads its segment's steps in order, outside in, inside out, odd then even, or shuffled
    kOrderStep, // the size of those steps
    kOnJump     // Retrigger: a jump cuts the note playing and plays again the notes it lands inside; or Let ring
};
/// The Playheads device's own knobs.
enum Device { kRestart, kRestartBars, kSameNote };
} // namespace knob

enum class Direction : uint8_t { Forward, Reverse, PingPong, Freeze };
enum class Bend : uint8_t { Straight, EaseIn, EaseOut, EaseInOut, Swing, Stairs };
enum class LengthMode : uint8_t { AsWritten, FollowsPlayhead, Fixed };
enum class Restart : uint8_t { ClipStart, EveryBars };
enum class Order : uint8_t { InOrder, OutsideIn, InsideOut, OddEven, Shuffle };
enum class NotePick : uint8_t { All, Lowest, Highest };

constexpr int kMaxPlayheads = 16;
constexpr int kMaxRegionSteps = 256;   // how far Region start and Region length reach, in sixteenths
constexpr int kMaxPlayheadParams = 32; // settings per playhead, room to grow (the list is append-only)
constexpr int kMaxSweeps = 16; // per call; a window that would need more is cut short (calls are one sample long)

/// A playhead's settings in ticks, ready to move.
struct Motion {
    double rate = 1;
    Direction direction = Direction::Forward;
    double regionStart = 0;  // clip ticks
    double regionLength = 0; // clip ticks; 0 = nothing to read
    double start = 0;        // from the region's start (offset included), any value: it wraps
    double repeatSize = 0;   // 0 = no repeat
    int repeatTimes = 1;
    Bend bend = Bend::Straight;
    double bendAmount = 0;   // 0..1
    double bendCycle = 0;    // the playhead's own ticks (elapsed times rate); 0 = no bend
    double restartEvery = 0; // elapsed ticks; 0 = only when the clip starts
    /// A drawn path, when the Path knob says Drawn and the playhead has points (set by the caller, which owns them):
    /// over each cycle of the playhead's own time it goes where the points say; rate still applies, the other
    /// movement settings don't.
    const std::vector<AutoPoint>* path = nullptr;
    double pathCycle = 0;
    /// The order steps of `orderStep` ticks are read in; each pass of a shuffle has its own order, from `seed`.
    Order order = Order::InOrder;
    double orderStep = 0;
    uint32_t seed = 1;
    bool drawn() const { return path && !path->empty() && pathCycle > 0; }
};

/// One stretch of continuous movement: the playhead goes from `from` to `to` (clip ticks) without a jump.
/// `to < from` means backwards. A sweep never contains its end; it contains its start unless it begins at a
/// ping-pong turn (the note there was just played on the way in).
struct Sweep {
    double from = 0, to = 0;
    bool includeFrom = true;
    // For finding when a position is reached: travel at `from`, and the elapsed time the travel is counted from.
    double travelFrom = 0;
    double restartBase = 0;
    // A drawn path's sweep: the playhead's own time it covers, for finding when it reaches a place.
    double ownFrom = 0, ownTo = 0;
    // It starts somewhere the playhead wasn't just before: a repeat, a wrap, an order's step, a restart, a drawn
    // path's step. A ping-pong turn starts where the last sweep ended, so it isn't one.
    bool jumped = false;
};

/// Where the playhead is after `elapsed` ticks, for drawing.
double positionAt(const Motion& m, double elapsed);

/// The sweeps the playhead makes between elapsed `a` and `b` (a <= b), in order. Returns how many were written.
/// `before` is where the playhead was just before `a`, if the caller knows (for marking a jump at `a`); NaN works it
/// out, infinity means "don't mark the first one".
int sweeps(const Motion& m, double a, double b, Sweep* out, int max = kMaxSweeps, double before = NAN);

/// The elapsed time at which a sweep reaches `pos` (which must lie in it).
double timeAt(const Motion& m, const Sweep& s, double pos);

/// The elapsed time of the playhead's first jump after `from`, looking no further than `horizon`; infinity when
/// there is none in that time. A Retrigger playhead cuts the note it plays there.
double nextJump(const Motion& m, double from, double horizon);

/// Whether a playhead set to Retrigger plays again the notes it jumps into (the On a jump setting).
bool retriggers(const float* knobs);

/// Calls `hit(note, rest)` for every note the sweep lands inside when it jumps: one that started before where it
/// lands and is still going there (going backward, one that ends after it). `rest` is how much of the note is left
/// to play from there, in the playhead's direction. Notes sorted by start; they need `start` and `end`.
template <typename Note, typename Hit>
void landedIn(const Sweep& s, const std::vector<Note>& notes, Hit&& hit) {
    bool backward = s.to < s.from;
    for (const Note& n : notes) {
        if (double(n.start) >= s.from)
            break;
        if (double(n.end) > s.from)
            hit(n, backward ? s.from - double(n.start) : double(n.end) - s.from);
    }
}

/// Calls `hit(note, pos)` for every note the sweep crosses into: going forward at its start, going backward at its
/// end. `notes` are sorted by start; `byEnd` lists their indices sorted by end. Notes need `start` and `end`.
template <typename Note, typename Hit>
void crossed(const Sweep& s, const std::vector<Note>& notes, const std::vector<uint32_t>& byEnd, Hit&& hit) {
    if (s.to > s.from) {
        auto begin = std::lower_bound(notes.begin(), notes.end(), s.from,
                                      [](const Note& n, double t) { return double(n.start) < t; });
        for (auto it = begin; it != notes.end() && double(it->start) < s.to; ++it)
            if (s.includeFrom || double(it->start) > s.from)
                hit(*it, double(it->start));
    } else if (s.to < s.from) {
        // Backwards: ends in (to, from], latest first.
        auto first = std::upper_bound(byEnd.begin(), byEnd.end(), s.from,
                                      [&](double t, uint32_t i) { return t < double(notes[i].end); });
        for (auto it = first; it != byEnd.begin();) {
            --it;
            double end = double(notes[*it].end);
            if (end <= s.to)
                break;
            if (s.includeFrom || end < s.from)
                hit(notes[*it], end);
        }
    }
}

/// Like `crossed`, for notes that repeat every `period` ticks inside a longer loop (a clip's pattern inside its lane
/// cycle, a drum lane with its own length): clip position p reads them at base + (p - origin) mod period. `hit`
/// gets each note with the position in the sweep where the playhead meets it.
template <typename Note, typename Hit>
void crossedRepeating(const Sweep& s, double origin, double base, double period, const std::vector<Note>& notes,
                      const std::vector<uint32_t>& byEnd, Hit&& hit) {
    if (period <= 0 || s.to == s.from)
        return;
    double u0 = s.from - origin, u1 = s.to - origin;
    bool first = true;
    if (u1 > u0) {
        for (double k = std::floor(u0 / period); k * period < u1; k++) {
            double a = std::max(u0, k * period), b = std::min(u1, (k + 1) * period);
            if (b <= a)
                continue;
            Sweep piece = s;
            piece.from = base + (a - k * period);
            piece.to = base + (b - k * period);
            piece.includeFrom = first ? s.includeFrom : true;
            first = false;
            crossed(piece, notes, byEnd, [&](const Note& n, double p) { hit(n, origin + k * period + (p - base)); });
        }
    } else {
        for (double k = std::ceil(u0 / period) - 1; (k + 1) * period > u1; k--) {
            double a = std::min(u0, (k + 1) * period), b = std::max(u1, k * period);
            if (a <= b)
                continue;
            Sweep piece = s;
            piece.from = base + (a - k * period);
            piece.to = base + (b - k * period);
            piece.includeFrom = first ? s.includeFrom : true;
            first = false;
            crossed(piece, notes, byEnd, [&](const Note& n, double p) { hit(n, origin + k * period + (p - base)); });
        }
    }
}

/// Like `crossed`, for notes moved `shift` ticks later: the sweep meets a note at its written place plus `shift`.
template <typename Note, typename Hit>
void crossedShifted(const Sweep& s, double shift, const std::vector<Note>& notes, const std::vector<uint32_t>& byEnd,
                    Hit&& hit) {
    Sweep moved = s;
    moved.from -= shift;
    moved.to -= shift;
    crossed(moved, notes, byEnd, [&](const Note& n, double p) { hit(n, p + shift); });
}

/// The note order `crossed` needs for going backwards.
template <typename Note>
std::vector<uint32_t> orderByEnd(const std::vector<Note>& notes) {
    std::vector<uint32_t> order(notes.size());
    for (size_t i = 0; i < order.size(); i++)
        order[i] = uint32_t(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](uint32_t a, uint32_t b) { return double(notes[a].end) < double(notes[b].end); });
    return order;
}

/// The choices of the knobs that pick a speed or a length, and what each one means.
const std::vector<std::string>& rateNames();
double playheadRate(int index);
const std::vector<std::string>& repeatNames(); // "Off" first
double repeatTicks(int index, TimeSig ts);
const std::vector<std::string>& cycleNames();
double cycleTicks(int index, TimeSig ts);
const std::vector<std::string>& fixedLengthNames();
const std::vector<std::string>& pathCycleNames();
const std::vector<std::string>& orderStepNames();
double orderStepTicks(int index, TimeSig ts);
/// Which of `count` steps is read `k`-th in `order` (pass `pass` of a shuffle). Every step is read once per pass.
int orderedStep(Order order, int k, int count, int64_t pass, uint32_t seed);
double pathCycleTicks(int index, TimeSig ts);
double fixedLengthTicks(int index, TimeSig ts);

/// The bend shape: maps 0..1 of a cycle onto 0..1, always rising, with both ends fixed.
double warp(Bend shape, double amount, double f);

/// Whether a playhead's Path knob says Drawn.
bool wantsDrawnPath(const float* knobs);
/// The places a playhead's settings take it to over one path cycle, as points every `step` ticks, with a pair of points
/// at each jump: what "Draw this path" starts from.
std::vector<AutoPoint> pathFromSettings(const Motion& m, double cycle, double step);

/// What a playhead does to the notes it plays (its NOTE settings).
struct NoteShape {
    int octave = 0;
    int keySteps = 0;
    NotePick pick = NotePick::All;
    bool invert = false;
    float velocity = 0; // added, -1..1
};
NoteShape noteShapeFrom(const float* knobs);

/// Note roles among notes that start together, for NotePick: the lowest and the highest (a lone note is both).
constexpr uint8_t kLowestNote = 1, kHighestNote = 2;
inline bool picked(NotePick pick, uint8_t role) {
    return pick == NotePick::All || (pick == NotePick::Lowest && (role & kLowestNote)) ||
           (pick == NotePick::Highest && (role & kHighestNote));
}

/// A note's pitch as the playhead plays it: mirrored inside [low, high] (in the key) when inverting, then moved by
/// octaves and key steps.
int shapedPitch(const NoteShape& shape, int pitch, int low, int high, const Scale& key);

/// Sets each note's role (kLowestNote, kHighestNote) among the notes that start with it. `notes` are sorted by start;
/// `role` reads and writes a note's role.
template <typename Note, typename Role>
void markChordRoles(std::vector<Note>& notes, Role&& role) {
    for (size_t i = 0; i < notes.size();) {
        size_t j = i;
        int lo = notes[i].pitch, hi = notes[i].pitch;
        while (j < notes.size() && notes[j].start == notes[i].start) {
            lo = std::min<int>(lo, notes[j].pitch);
            hi = std::max<int>(hi, notes[j].pitch);
            j++;
        }
        for (size_t k = i; k < j; k++)
            role(notes[k]) =
                uint8_t((notes[k].pitch == lo ? kLowestNote : 0) | (notes[k].pitch == hi ? kHighestNote : 0));
        i = j;
    }
}

/// A playhead's region from its Region start and Region length (in steps): where on the piano roll it starts and how
/// long it is. The piano roll is a canvas, so a region may sit anywhere on it, past the clip's end too. Length 0
/// follows the clip: its loop, from `clipStart`, `clipLength` long.
std::pair<double, double> regionFrom(double startSteps, double lengthSteps, double clipStart, double clipLength);
/// A playhead's settings, from its normalized knobs and the clip's play range [clipStart, clipStart + clipLength).
Motion motionFrom(const float* knobs, const float* deviceKnobs, double clipStart, double clipLength, TimeSig ts);

/// How long a note the playhead plays lasts, in song ticks: as written, stretched by the playhead's speed, or fixed.
/// `travel` is where the playhead's travel is when the note starts (Sweep::travelFrom plus how far into the sweep):
/// with it, a note following a bent playhead lasts until the playhead has really crossed it.
double noteLength(const float* knobs, const Motion& m, double written, TimeSig ts, double travel = NAN);

} // namespace tessera::playheads
