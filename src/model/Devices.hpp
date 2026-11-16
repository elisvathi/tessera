#pragma once

#include "Chord.hpp"
#include "Device.hpp"
#include "Scale.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/// Running notes through a track's device chain (docs/DEVICES.md §2, §5). Pure and allocation free, so the
/// engine runs it on the audio thread and MIDI export runs the very same code.
///
/// Notes travel as whole notes (start and end known). A note whose end isn't known yet (a key held down, a
/// legato note waiting for the next one) is "open": its end is infinite and an End message for its ID follows.
namespace tessera::chain {

/// Knob positions in each device's parameter list (DeviceTypes.cpp). Append-only, like the list; the one exception,
/// Random's velocity and timing (13 Oct 2026, now Humanize), is migrated when a song loads (Serialize.cpp).
namespace knob {
enum Transpose { kSemitones, kOctaves, kKeySteps };
enum ScaleDevice { kKeyFrom, kRoot, kScale, kOffKey };
enum Velocity { kCurve, kMin, kMax, kRandom };
enum NoteLength { kMode, kScaleBy, kLength, kOverlap };
enum ChordDevice { kChord, kInKey, kInversion, kVoicing, kStrum };
/// After the first seven: the Pattern line's settings. Its four value rows (velocity, gate, octave, move) each
/// have a length and one setting per step, so every step can be automated on its own.
enum Arpeggiator {
    kPattern,
    kRate,
    kGate,
    kArpOctaves,
    kSwing,
    kArpVelocity,
    kHold,
    kArpMoveUnit, // what one step of the Move row is: a semitone, a step of the key or a note of the chord (ArpMove)
    kArpVelSteps,
    kArpGateSteps,
    kArpOctSteps,
    kArpMoveSteps,
    kArpStepVel,                       // 16 steps: the held note's velocity scaled, 0..100%
    kArpStepGate = kArpStepVel + 16,   // 16 steps: how long each note is, 5..200% of a step
    kArpStepOct = kArpStepGate + 16,   // 16 steps: octaves, -3..+3
    kArpStepMove = kArpStepOct + 16,   // 16 steps: how far each step's notes move, -12..+12 (in kArpMoveUnit)
    kArpRowShift = kArpStepMove + 16,  // 4 rows (Vel, Gate, Oct, Move): added to every step of the row
    kArpParamCount = kArpRowShift + 4  // 80
};
enum Echo { kTime, kRepeats, kDecay, kEchoPitch, kEchoGate };
enum NoteRepeat { kRepeatRate, kRepeatGate, kRepeatDecay };
enum Random { kRandomPitch, kRandomInKey, kDrop };
enum Humanize { kHumanizeTiming, kHumanizeVelocity, kHumanizeLength };
enum Groove { kGrooveSwing, kGrooveGrid };
enum NoteFilter { kLowest, kHighest, kMinVelocity, kMaxVelocity, kEvery, kOffset, kChannel /* a set: Kind::Mask */ };
enum Motion { kMotionPitch, kMotionTime, kMotionLength, kMotionVelocity, kMotionInKey };
} // namespace knob

/// The Pattern line's value rows, top to bottom: each one's length setting and its first step setting.
enum class ArpLane : uint8_t { Velocity, Gate, Octave, Move };
/// What one step of the Move row is.
enum class ArpMove : uint8_t { Semitones, Key, Chord };
constexpr int kArpLaneCount = 4;
constexpr int arpLaneSteps(ArpLane l) { return knob::kArpVelSteps + int(l); }
constexpr int arpLaneFirst(ArpLane l) { return knob::kArpStepVel + 16 * int(l); }
/// The setting added to every step of a row: one number a modulator, lane or curve moves the whole row with.
constexpr int arpLaneShift(ArpLane l) { return knob::kArpRowShift + int(l); }

enum class KeyFrom : uint8_t { Clip, Song, Fixed };
enum class OffKey : uint8_t { Nearest, Up, Down, Drop };
enum class LengthMode : uint8_t { Scale, Fixed, Legato };
/// The Arpeggiator's patterns, in the order of its Pattern list (saved songs before schema 9 had fewer).
enum class ArpPattern : uint8_t {
    Up,
    Down,
    UpDown,     // the top and bottom notes once at each turn
    UpDownFull, // the top and bottom notes twice at each turn
    DownUp,
    DownUpFull,
    Converge, // lowest, highest, second lowest, second highest… meeting in the middle
    Diverge,  // the middle first, then out, the lower of each pair first
    AsPlayed,
    Random,
    Walk,     // mostly on to the next note, sometimes the same or back one; one path for each set of held notes
    Shuffled, // each note once per round, in an order of its own for each set of held notes
    Pattern,  // a 16-step line made from the held notes, the lowest note twice as often; one per set of held notes
    Chord
};
enum class ArpVelocity : uint8_t { AsPlayed, Fixed, Accent };

constexpr uint32_t kLiveNote = 0x80000000u; // IDs of notes played live carry this bit
constexpr int kMaxMessages = 128;
constexpr int kMaxOpenNotes = 32;
constexpr int kMaxHeld = 16; // notes an arpeggiator holds at once

struct ChainNote {
    double start = 0;
    double end = INFINITY; // infinite while open
    int8_t pitch = 60;
    float velocity = 0.8f;
    float release = 0.5f;
    uint32_t id = 0;
    uint8_t key = 0;              // the key it was written in: 0 = the song's, k = the context's clip key k - 1
    uint8_t channel = 1;          // the note's channel, 1..16
    uint8_t lane = 0;             // the playhead that read it (1..16), whose branch it goes through; 0 = none
    const void* extras = nullptr; // the engine's expression curves, carried along untouched
    double origin = 0;            // where it was written (clip or song ticks), so randomness repeats
    int64_t pass = 0;
    uint64_t motion = 0; // the Motion device the written note moves with; 0 = none

    bool open() const { return !std::isfinite(end); }
};

/// A device as the chain runs it: its knobs are normalized values the caller keeps up to date (base plus
/// modulation).
struct DeviceSlot {
    DeviceType type = DeviceType::Transpose;
    bool on = true;
    bool affectDrums = false;
    uint64_t id = 0;
    const float* knobs = nullptr;
    uint8_t lane = 0; // a playhead's branch device (1..16): only that playhead's notes go through it; 0 = all notes
    int32_t variation = 1; // its dice: a fixed number, or kNewEachPlay to play on this run's roll
    const uint8_t* line = nullptr; // an Arpeggiator's own Pattern line (Device::line), and how many steps it has
    uint8_t lineLength = 0;
};

constexpr int kMaxLineSteps = 16;
/// A Pattern line step: which held notes it plays, a bit per row (bit 0 the lowest note, up to kMaxLineNotes rows);
/// 0 is a rest. A row past the notes held wraps around to the lowest, and a note twice in one step plays once, so a
/// full step (kFullStep) plays exactly the chord. Before schema 15 a step was one row number and an octave.
constexpr int kMaxLineNotes = 8;
constexpr uint8_t kFullStep = 0xFF;
inline bool stepHas(uint8_t step, int row) { return row >= 1 && row <= kMaxLineNotes && ((step >> (row - 1)) & 1); }
inline uint8_t stepOf(int row) { return row >= 1 && row <= kMaxLineNotes ? uint8_t(1u << (row - 1)) : 0; }
/// Step `k` of a line the dice writes from `seed`: one of the four lowest rows each step, the lowest twice as often
/// as each other.
uint8_t generatedLineStep(uint64_t seed, int k);
/// A whole line of `length` steps the dice writes from `seed`.
std::vector<uint8_t> generatedLine(uint64_t seed, int length);

struct Context {
    const DeviceSlot* devices = nullptr;
    int count = 0;
    bool drum = false;
    const Scale* songKey = nullptr;
    const std::vector<Scale>* clipKeys = nullptr;
    TimeSig timeSig;
    /// This run's roll: what devices on "New every play" play on. It changes on every play and loop pass.
    uint32_t roll = 0;
    /// Song ticks = chain ticks + songOffset, so arpeggiators step on the song's grid (0 while stopped: they step
    /// on their own clock then).
    double songOffset = 0;
    /// A Motion device's knob as a note on `pass` reads it: with its "Per pass" modulators stepped to that pass.
    /// Unset, the knob as it stands.
    float (*passKnob)(const void* user, const DeviceSlot& device, int knob, int64_t pass) = nullptr;
    const void* passUser = nullptr;

    const Scale& keyOf(uint8_t key) const;
};

/// Where notes leave the chain.
class Output {
  public:
    virtual ~Output() = default;
    virtual void on(const ChainNote& note) = 0;
    /// The open note `id` ends now.
    virtual void end(uint32_t id) = 0;
};

class Chain {
  public:
    /// Forgets every waiting message and every open note.
    void clear();
    /// A note entering the chain at its start.
    void push(const ChainNote& note);
    /// The open note `id`, which entered earlier, ends at `at`. `out` takes the end at once if the chain is full.
    void end(uint32_t id, double at, Output& out);
    /// Runs every message due before `until` through the devices, in time order.
    void run(double until, const Context& context, Output& out);
    /// The time of the earliest waiting message; infinity when none.
    double nextDue() const;
    bool idle() const { return count == 0; }
    /// Forgets the expression curves notes carry (they belong to a snapshot that is going away): in waiting
    /// messages and in the notes an arpeggiator holds, which it plays again on its next steps.
    void dropExtras() {
        for (int i = 0; i < count; i++)
            messages[size_t(i)].note.extras = nullptr;
        for (DeviceState& state : states)
            for (int i = 0; i < state.heldCount; i++)
                state.held[size_t(i)].note.extras = nullptr;
    }
    /// Forgets the notes of one playhead's lane (1..16) that haven't come out yet: waiting starts, echo repeats and
    /// the notes an arpeggiator holds. Ends stay, so nothing waits for a note that never ends.
    void dropLane(uint8_t lane);
    /// The chain's devices changed while it plays (one added, removed, moved, switched on or off): `oldIds` and
    /// `oldOn` are the devices it had, stage by stage, `newIds` and `newOn` the ones it has now. What a device that
    /// stays holds follows it to its new place, so an arpeggiator plays on; waiting notes at a device that left go
    /// on to the next one. A device that left or was switched off hands its held notes on at `now` and ends the
    /// notes only it would have ended (a chord's notes, a legato note), so nothing hangs.
    void restructure(const uint64_t* oldIds, const bool* oldOn, int oldCount, const uint64_t* newIds, const bool* newOn,
                     int newCount, double now, Output& out);
    /// The step an arpeggiator at `stage` played last, counted from where its pattern started, while it runs; -1
    /// when it doesn't (nothing held). The editor lights each Pattern row's step from it.
    int64_t arpStep(int stage) const {
        if (stage < 0 || stage >= kMaxDevices)
            return -1;
        const DeviceState& s = states[size_t(stage)];
        return s.running && s.heldCount > 0 ? s.played : -1;
    }
    /// How many notes the arpeggiator at `stage` holds while it runs; 0 when it doesn't. The editor shows a Pattern
    /// row for each note of the biggest chord it has held.
    int arpHeld(int stage) const {
        if (stage < 0 || stage >= kMaxDevices)
            return 0;
        const DeviceState& s = states[size_t(stage)];
        return s.running ? s.heldCount : 0;
    }
    /// A fresh ID for a note entering the chain (never 0, never with kLiveNote).
    uint32_t newId() {
        ids = (ids + 1) & ~kLiveNote;
        return ids == 0 ? ++ids : ids;
    }
    /// Notes dropped because the chain was full (shown as a warning).
    int overflowCount() const { return overflows; }

  private:
    // On and End travel between devices; the others are a device's reminders to itself: Expire (a held note's
    // time is up), Tick (an arpeggiator's next step), Echo (an echo's next repeat).
    enum class Kind : uint8_t { On, End, Expire, Tick, Echo };
    struct Message {
        double time;
        uint32_t seq;
        uint8_t stage;
        Kind kind;
        uint8_t step;   // Echo: which repeat
        ChainNote note; // On and Echo: the note; End and Expire: note.id
    };
    /// What a device does when an open note it saw ends.
    enum class Action : uint8_t { Pass, Rescale, Consume, Legato, Children };
    struct OpenNote {
        uint32_t id;
        double start;
        Action action;
        uint8_t childCount = 0; // Children: the chord notes that end with it
        std::array<uint32_t, kMaxChordNotes> children{};
    };
    /// A note an arpeggiator holds.
    struct Held {
        ChainNote note;
        uint32_t order; // arrival, for "as played"
        bool down;      // still held (with Hold on, released notes stay until new ones come)
    };
    struct DeviceState {
        std::array<OpenNote, kMaxOpenNotes> open;
        int count = 0;
        std::array<Held, kMaxHeld> held;
        int heldCount = 0;
        bool running = false; // an arpeggiator's clock is ticking
        uint64_t steps = 0;   // arpeggiator steps played; Note Filter: notes counted
        uint32_t arrivals = 0;
        uint64_t patternStart = 0; // the arpeggiator step its pattern last started again on
        uint32_t stepArrivals = 0; // `arrivals` at its last step: notes arriving after it are new
        int walk = 0;              // where a Walk pattern is in its list of notes
        int64_t played = -1;       // an arpeggiator's last step since its pattern started; -1 before the first
    };

    void post(int stage, Kind kind, const ChainNote& note, double time, Output& out, uint8_t step = 0);
    void dispatch(const Message& m, const Context& context, Output& out);
    void forward(const Message& m, Output& out);
    void transpose(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    void scale(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    void velocity(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    void noteLength(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    void chord(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    void arpeggiator(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    void echo(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    /// Plays every note again at its Rate for as long as it lasts, so a chord repeats as a chord.
    void noteRepeat(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    void random(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    /// Velocity ±, a later start and a longer or shorter note, a little different on every pass but repeatable.
    void humanize(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    /// Swings this track's notes by where they were written, in place of the song's swing.
    void groove(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    void noteFilter(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    /// Moves the notes that joined this Motion device by its knobs, read for the note's pass (a playhead's own).
    void motion(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    /// An arpeggiator lets go of a note (it ended, or its time is up).
    void release(int stage, uint32_t id, bool hold);
    /// Remembers an open note in the stage's state; when full, the oldest legato note ends to make room.
    void remember(int stage, const OpenNote& note, double now, Output& out);
    /// Finds and removes an open note; false when the stage never saw it.
    bool forget(int stage, uint32_t id, OpenNote* found = nullptr);

    std::array<Message, kMaxMessages> messages;
    int count = 0;
    uint32_t seq = 0;
    uint32_t ids = 0;
    int overflows = 0;
    std::array<DeviceState, kMaxDevices> states;
};

/// A key from the Scale device's own choice (root 0..11, index into builtinScales()). Built once.
const Scale& fixedKey(int root, int scaleIndex);
/// Builds the tables the chain needs, so the audio thread never builds them. Call once from the UI thread.
void warmUp();

} // namespace tessera::chain
