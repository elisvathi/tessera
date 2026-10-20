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
enum Arpeggiator { kPattern, kRate, kGate, kArpOctaves, kSwing, kArpVelocity, kHold };
enum Echo { kTime, kRepeats, kDecay, kEchoPitch, kEchoGate };
enum Random { kRandomPitch, kRandomInKey, kDrop };
enum Humanize { kHumanizeTiming, kHumanizeVelocity, kHumanizeLength };
enum NoteFilter { kLowest, kHighest, kMinVelocity, kMaxVelocity, kEvery, kOffset, kChannel };
} // namespace knob

enum class KeyFrom : uint8_t { Clip, Song, Fixed };
enum class OffKey : uint8_t { Nearest, Up, Down, Drop };
enum class LengthMode : uint8_t { Scale, Fixed, Legato };
enum class ArpPattern : uint8_t { Up, Down, UpDown, DownUp, Random, AsPlayed, Chord };
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
    const void* extras = nullptr; // the engine's expression curves, carried along untouched
    double origin = 0;            // where it was written (clip or song ticks), so randomness repeats
    int64_t pass = 0;

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
};

struct Context {
    const DeviceSlot* devices = nullptr;
    int count = 0;
    bool drum = false;
    const Scale* songKey = nullptr;
    const std::vector<Scale>* clipKeys = nullptr;
    TimeSig timeSig;
    uint32_t seed = 1;
    /// Song ticks = chain ticks + songOffset, so arpeggiators step on the song's grid (0 while stopped: they step
    /// on their own clock then).
    double songOffset = 0;

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
    /// Forgets the expression curves notes carry (they belong to a snapshot that is going away).
    void dropExtras() {
        for (int i = 0; i < count; i++)
            messages[size_t(i)].note.extras = nullptr;
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
    void random(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    /// Velocity ±, a later start and a longer or shorter note, a little different on every pass but repeatable.
    void humanize(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
    void noteFilter(const Message& m, const DeviceSlot& d, const Context& c, Output& out);
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
