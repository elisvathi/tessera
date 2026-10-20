#pragma once

#include "../model/Recording.hpp"
#include "Engine.hpp"

#include <deque>
#include <map>
#include <vector>

namespace tessera {

/// What the recorder needs from its Tessera. The module implements it; tests fake it.
class RecorderHost {
  public:
    virtual ~RecorderHost() = default;
    virtual const Song& song() = 0;
    virtual void perform(CommandPtr command) = 0;
    virtual uint64_t nextId() = 0;
    /// Starts a launcher slot at once, in time with a start at `playTick`, so a new clip carries on looping.
    virtual void launchAt(TrackId track, int slot, double playTick) = 0;
    virtual PlayMode playMode() = 0;
    /// The scene whose slot a new launcher clip goes into when that slot is empty.
    virtual int targetScene() = 0;
};

/// Turns the engine's record events into song edits on the UI thread (DESIGN.md §14). No Rack dependency.
///
/// Where a take goes, per armed track, decided by its first note:
/// - a track playing a launcher clip overdubs it: each loop pass is merged as it ends;
/// - otherwise in Launcher mode: a new clip in an empty slot, starting at the bar recording started. With a
///   fixed length it is committed when full and then keeps looping, taking overdubs; without one it is as long
///   as the recording, rounded up to the bar;
/// - otherwise in Arranger mode: a new clip on the timeline, one per loop pass.
/// Each commit is one undo step; input quantize is a second one, so undo brings back the played timing.
class Recorder {
  public:
    explicit Recorder(RecorderHost& host) : host(host) {}

    RecordSettings settings;

    void handle(const RecordEvent& event);
    /// Each UI frame: commits loop passes that have ended.
    void update(double playTick);
    bool punchedIn() const { return punched; }

    enum class Target { NewClip, Overdub, Arranger };
    struct Open {
        double playStart = 0;
        Tick start = 0; // in the target's ticks
        float velocity = 0;
        uint8_t channel = 1;
    };
    /// One armed track's take in progress.
    struct Session {
        TrackId track;
        Target target = Target::NewClip;
        ClipId clip; // Overdub
        int slot = kNoSlot;
        double clipStart = 0;       // NewClip, Overdub: the play tick of clip tick 0
        Tick loopLength = 0;        // Overdub: 0 for a clip that doesn't loop
        Tick fixedLength = 0;       // NewClip: 0 = until recording stops
        int64_t pass = 0;           // Overdub: the loop pass the pending notes belong to
        double passFrom = 0;        // Arranger: where this pass started on the timeline
        std::vector<TakeNote> done; // finished notes not yet committed, in the target's ticks
        std::map<int, Open> open;   // held notes by pitch
    };
    const std::vector<Session>& sessions() const { return active; }
    /// Where a held note has got to, in its target's ticks, for drawing it live.
    static Tick openLength(const Open& open, double playTick) { return Tick(std::max(1.0, playTick - open.playStart)); }

    // Capture: the last notes played while not recording.

    bool canCapture() const { return !buffer.empty(); }
    /// Turns the last phrase played (notes with no gap of `kCaptureGapBars` or more) into a new clip in an
    /// empty slot of `track`. Returns the clip, or an invalid ID when there was nothing or no room.
    ClipId capture(TrackId track);
    static constexpr int kBufferBars = 64;
    static constexpr int kCaptureGapBars = 4;

  private:
    struct Captured {
        double freeStart, playStart, songStart, length;
        bool playing;
        int8_t pitch;
        float velocity, release;
        uint8_t channel = 1;
    };

    Session* find(TrackId track);
    Session* start(const RecordEvent& event);
    Tick targetTick(const Session& s, const RecordEvent& e) const;
    int64_t passOf(const Session& s, double playTick) const;
    int emptySlot(const Track& track);
    void closeOpen(Session& s, double playTick);
    void commit(Session& s, double endPlay, double endSong, bool final);
    void quantize(ClipId clip, const edits::IdSet& ids);
    void bufferNote(const RecordEvent& event);

    RecorderHost& host;
    bool punched = false;
    double punchPlay = 0, punchSong = 0;
    std::vector<Session> active;

    std::deque<Captured> buffer;
    std::map<int, Captured> bufferOpen;
};

} // namespace tessera
