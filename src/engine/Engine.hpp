#pragma once

#include "../model/Devices.hpp"
#include "../model/MidiReader.hpp"
#include "../model/Modulators.hpp"
#include "DrumVoices.hpp"
#include "Snapshot.hpp"
#include "SpscQueue.hpp"
#include "TrackFrame.hpp"
#include "VoiceAllocator.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace tessera {

/// What the transport did during one sample. Levels describe the state after the sample's events;
/// the rest are one-sample flags.
struct TransportEvents {
    bool running = false; // level
    bool clock = false;   // level, 50% duty at the clock division
    bool reset = false;
    bool bar = false;
    bool beat = false;
    bool loop = false; // the song loop wrapped
    double tempo = 0;  // a launched scene changed the tempo to this; 0 = no change
};

/// Launcher: tracks play their launched clips; the arrangement is silent.
/// Arranger: tracks play the arrangement, except tracks the launcher has taken over: launching or
/// stopping a clip on a track takes it over until "back to arrangement".
enum class PlayMode : uint8_t { Launcher, Arranger };

/// Launcher slot values: a track with no clip playing, and a track with nothing queued.
constexpr int kNoSlot = -1;
constexpr int kNothingQueued = -2;

/// A clip-launcher action (DESIGN.md §7 "Launcher mode", §10).
struct LaunchEvent {
    /// Reset: every track drops its launcher state, in either mode. Loading a patch sends it first, so launches
    /// still queued from before the load (the default song's) can't take tracks off the arrangement.
    enum class Kind : uint8_t { Slot, StopTrack, Scene, StopAll, BackToArrangement, Reset };
    Kind kind = Kind::Slot;
    uint64_t track = 0;     // TrackId value, for Slot and StopTrack
    int index = 0;          // the slot (Slot) or the scene (Scene)
    bool immediate = false; // ignore launch quantize
    bool start = true;      // launching a clip or scene while stopped starts the transport
    /// Slot: start at once as if launched at this play tick (a freshly recorded clip carries on in time).
    double at = NAN;
    /// Slot: start at once from this point of the clip (seeking in the piano roll). Worked out on the audio thread,
    /// so it lands on the sample the event is handled on.
    double clipTime = NAN;
};

/// A launcher clip starting or stopping on a track while capture is on, for "record to arrangement".
/// `clip` is 0 when the track stopped.
struct CaptureEvent {
    uint64_t track = 0;
    uint64_t clip = 0;
    double songTick = 0; // where on the timeline
    double clipTick = 0; // where in the clip it was at that moment
};
using CaptureQueue = SpscQueue<CaptureEvent, 1024>;

/// Where a live note came from. Keyboard is the computer keyboard in the editor (EDITOR_UX §4.10).
enum class LiveSource : uint8_t { Cv, Midi, Keyboard };

/// A note arriving at the record inputs.
struct LiveNote {
    LiveSource source = LiveSource::Cv;
    uint8_t channel = 0; // poly channel (CV) or MIDI channel, 0..15
    int8_t pitch = 60;
    float velocity = 0.8f;
    bool on = true;
    // The channel tracks filter by and recordings keep; -1 = `channel`. MPE gives each note its own channel, so
    // its notes report the zone's master channel here.
    int8_t partChannel = -1;
};

/// A value from the record inputs besides notes (docs/design/EXPRESSION_INPUT.md): one held note's pressure,
/// timbre or pitch, or a value the whole channel shares (pitch bend, channel pressure, a CC).
struct LiveValue {
    LiveSource source = LiveSource::Midi;
    uint8_t channel = 0;     // 0..15
    int8_t partChannel = -1; // as in LiveNote
    bool perNote = true;
    int8_t pitch = -1; // per note: the key as it came in; -1 = every key held on the channel
    NoteCurve curve = NoteCurve::Pressure;
    MidiControl control; // per channel
    float value = 0;     // per note: semitones for Pitch, else 0..1; per channel: -1..1 for pitch bend, else 0..1
};

/// What the recorder on the UI thread hears from the engine (DESIGN.md §5 "Recording").
struct RecordEvent {
    enum class Kind : uint8_t {
        Note,         // a live note on or off, for one armed track, or (track 0) for the capture buffer
        PunchIn,      // recording started
        PunchOut,     // recording stopped
        LoopWrap,     // the song loop wrapped while recording
        NoteValue,    // a held note's pressure, timbre or pitch moved (curve, value), for one armed track
        ChannelValue, // a control the channel shares moved (control, value 0..1), for one armed track
    };
    Kind kind = Kind::Note;
    uint64_t track = 0; // TrackId value; 0 = not routed to a track
    int8_t pitch = 60;
    float velocity = 0.8f;
    uint8_t channel = 1; // the MIDI channel it came in on, 1..16 (1 for CV and the computer keyboard)
    bool on = true;
    bool recording = false; // the transport was recording at that moment
    bool playing = false;
    double playTick = 0;
    double songTick = 0;
    double freeTick = 0;                   // a clock that runs even while stopped, for the capture buffer
    int slot = kNoSlot;                    // the launcher clip the track was playing
    double clipTick = NAN;                 // where in that clip
    double clipStart = NAN;                // the play tick that clip started at
    NoteCurve curve = NoteCurve::Pressure; // NoteValue
    MidiControl control;                   // ChannelValue
    float value = 0;                       // NoteValue: semitones for Pitch, else 0..1; ChannelValue: 0..1
};
using RecordQueue = SpscQueue<RecordEvent, 4096>;

/// A note played from the editor (a keyboard click, a freshly drawn note) on a track's outputs.
struct AuditionEvent {
    uint64_t track = 0; // TrackId value
    int8_t pitch = 60;
    float velocity = 0.8f;
    float seconds = 0; // > 0: release by itself after this long; 0: hold until the matching note-off
    bool on = true;
    uint32_t id = 0; // a live key's note ID, so its pressure and bend find the voice it plays
};

/// The audio-thread sequencer: transport, clip scheduling and voice allocation for every track.
/// It has no Rack dependency, so tests drive it one sample at a time.
///
/// Time model: `songTick` is the position shown to the user and wraps inside the loop region.
/// `playTick` only ever moves forward from the last reset; clips loop relative to it.
constexpr int kMaxPendingHits = 64;

class Engine {
  public:
    static constexpr double kMinBpm = 20;
    static constexpr double kMaxBpm = 300;

    Engine();

    void setSampleRate(double sampleRate);
    void setTempo(double bpm);
    /// External clock: the song moves this many ticks on the next sample instead of following the tempo.
    void setStep(double ticks) { stepOverride = ticks; }
    void setLoopEnabled(bool on) {
        // Set on every sample: only a change moves the loop wrap that arrangement tracks sleep until.
        if (on != loopOn)
            wakeAll();
        loopOn = on;
    }
    void setClockDivision(Tick ticks) { clockDivision = ticks > 0 ? ticks : 1; }
    /// Switching to Arranger brings every track back to the arrangement.
    void setPlayMode(PlayMode mode);
    PlayMode playMode() const { return mode; }
    /// While on, clip starts and stops go to `queue` (which must outlive the engine's use of it).
    void setCapture(CaptureQueue* queue, bool on);

    /// The snapshot must stay alive until it is replaced. Real-time safe: no allocation.
    /// Sounding notes carry over for tracks that are still there and still audible.
    void setSnapshot(const Snapshot* snapshot);

    void play() {
        playing = true;
        wakeAll();
    }
    void stop();
    /// Back to the loop start when looping, otherwise to the song start. Clips restart from their beginning.
    void reset();

    /// Moves the song position (the arranger playhead). Clips keep their own phase.
    void seek(double songTick);

    bool isPlaying() const { return playing; }
    double tempo() const { return bpm; }
    double position() const { return songTick; }
    /// Ticks played since the last reset; clips loop relative to this.
    double playPosition() const { return playTick; }
    /// Bit i is set while track i (display order) has any gate high.
    uint64_t activeTracks() const { return activeMask; }

    /// Plays or releases an audition note. Works whether or not the transport is running.
    void audition(const AuditionEvent& event);

    // Recording (DESIGN.md §14)

    /// Where live notes and punch events go. Must outlive the engine's use of it.
    void setRecordQueue(RecordQueue* queue) { recordQueue = queue; }
    /// The record button: while stopped it counts in `countInBars` bars (clicking on the beats) and then
    /// plays and records; while playing it punches in at once; while recording or counting in it punches out.
    void toggleRecord(int countInBars);
    bool recording() const { return recordingOn; }
    bool countingIn() const { return countInLength > 0; }
    /// Ticks of count-in still to go.
    double countInRemaining() const { return countInLength > 0 ? countInLength - countInPos : 0; }
    /// The FILL state (panel button or gate) for notes with a Fill / Not fill condition.
    void setFill(bool on) { fill = on; }

    /// Snap live pitches to the song key (monitoring and recording alike).
    void setLiveToKey(bool on) { liveToKey = on; }
    /// A note from the record inputs: armed tracks with a matching input play it (when monitoring) and it is
    /// reported to the recorder, once per armed track and once more for the capture buffer.
    void liveNote(const LiveNote& note);
    /// A value from the record inputs: the voices playing the key (or the tracks taking the channel) follow it
    /// while monitoring, and armed tracks report it to the recorder.
    void liveValue(const LiveValue& value);

    /// Launches or stops clips. While playing, the change waits for the next launch-quantize line of the
    /// song position; while stopped it happens at once. Launching an empty slot stops the track.
    void launch(const LaunchEvent& event);

    // Launcher state per track, by display index, for the UI
    size_t trackCountNow() const { return trackCount; }
    int sceneCount() const { return snapshot ? int(snapshot->sceneTempos.size()) : 0; }
    TrackId trackAt(size_t i) const { return tracks[i].id; }
    int busSlotAt(size_t i) const { return snapshot->tracks[i]->busSlot; }
    int playingSlot(size_t i) const { return tracks[i].playing; }
    /// kNothingQueued, kNoSlot (a stop is queued) or the queued slot.
    int queuedSlot(size_t i) const { return tracks[i].queued; }
    /// In Arranger mode: the launcher has taken this track over, so its arrangement is silent.
    bool overridden(size_t i) const { return mode == PlayMode::Arranger && tracks[i].overridden; }
    /// Where the playing clip is, in clip ticks. NaN when nothing plays or a one-shot clip has ended.
    double clipPosition(size_t i) const;
    /// Clip ticks played since the playing clip started (not wrapped by its loop). NaN when nothing plays.
    double clipElapsed(size_t i) const { return tracks[i].playing != kNoSlot ? playTick - tracks[i].launchTick : NAN; }

    /// A track's automation value (0..1) on an AUTO channel after this sample, for parameter mapping. Includes
    /// what modulators add to the channel.
    float automationValue(size_t i, int output) const { return tracks[i].autoOut[size_t(output)]; }
    /// The same by output-bus slot (stable for a track's lifetime); -1 when no track has that slot.
    float automationValueOfSlot(int slot, int output) const {
        int i = slot >= 0 && slot < kMaxTracks ? indexOfSlot[size_t(slot)] : -1;
        return i >= 0 ? tracks[size_t(i)].autoOut[size_t(output) & 7] : -1.f;
    }

    // Devices and modulators (docs/DEVICES.md §5)

    /// Holds one knob at `value` (normalized) while the user drags it, before the edit is committed.
    void setKnobOverride(const ParamRef& ref, float value) {
        overrideRef = ref;
        overrideValue = value;
        overrideOn = true;
    }
    void clearKnobOverride() { overrideOn = false; }
    /// A modulated or held knob's value as it plays (knob table index of the current snapshot). Any thread.
    float liveKnob(int knob) const {
        return knob >= 0 && knob < kMaxKnobs ? liveKnobs[size_t(knob)].load(std::memory_order_relaxed) : 0.f;
    }
    /// What a knob lane last set a knob to, before modulation. Any thread.
    float liveBase(int knob) const {
        return knob >= 0 && knob < kMaxKnobs ? liveBases[size_t(knob)].load(std::memory_order_relaxed) : 0.f;
    }
    /// A modulator's output (by slot): -1..1 when bipolar, else 0..1. Any thread.
    float modulatorOutput(int slot) const {
        return slot >= 0 && slot < kMaxModulatorSlots ? liveModulators[size_t(slot)].load(std::memory_order_relaxed)
                                                      : 0.f;
    }
    /// Notes the device chains had to drop because too many were in flight.
    int chainOverflows() const;

    /// Advances one sample. `frames` is indexed by bus slot and must hold kMaxTracks entries.
    void process(TrackFrame* frames, TransportEvents& events);

  private:
    struct TrackState {
        TrackState() {
            audition.fill(-1);
            slewAlpha.fill(1);
            autoHigh.fill(10); // a channel only modulators drive sends 0..10 V
            liveCcOutput.fill(-1);
        }

        // Output frames: what changed since each bus buffer was last written (DESIGN.md §18).
        uint64_t autoChanges = 0;
        int curvedVoices = 0; // voices playing expression curves change their outputs every sample
        int auditions = 0;    // voices with an audition, so the per-sample countdown is skipped when there are none
        int busSlot = -1;     // the track's frame on the bus (a copy of TrackPlayback::busSlot, kept close at hand)
        std::array<uint64_t, 2> written{~uint64_t(0), ~uint64_t(0)};
        DrumVoices drums; // drum tracks: one trigger or gate per lane
        VoiceAllocator voices;
        TrackId id;
        // Per voice: -1 = clip note or idle, 0 = audition held until note-off, > 0 = audition samples left.
        std::array<int32_t, kMaxVoices> audition;
        size_t cursor = 0; // next note to consider in the playing clip
        int playing = kNoSlot;
        ClipId playingClip; // to find the clip again when slots move (a scene inserted above it)
        int queued = kNothingQueued;
        ClipId queuedClip;
        double queuedAt = 0;     // playTick when the queued change happens
        double launchTick = 0;   // playTick when the playing clip started
        bool overridden = false; // launched or stopped by the launcher since the last "back to arrangement"
        size_t arrangementCursor = 0;
        // Automation per AUTO channel: the smoothed value (0..1), its volts, and where each lane list was.
        // Slots past the AUTO channels are the lanes played into the voices (kPitchLaneSlot, kPressureLaneSlot).
        std::array<float, kMaxPlayedLanes> autoValue{};
        std::array<float, kMaxPlayedLanes> autoOut{}; // with modulators added
        int autoChannels = 0;                         // the lanes' channels and the modulated ones
        std::array<float, kMaxPlayedLanes> autoVolts{};
        std::array<float, kMaxPlayedLanes> autoLow{}, autoHigh{};
        std::array<float, kMaxPlayedLanes> slewMs{}, slewAlpha{};
        std::array<size_t, kMaxPlayedLanes> trackLaneCursor{}, clipLaneCursor{};
        // Knob lanes: where each list was, and what they set this block (knob table index and value).
        std::array<size_t, kMaxKnobLanes> trackKnobCursor{}, clipKnobCursor{};
        std::array<std::pair<int, float>, 2 * kMaxKnobLanes> knobSets{};
        int knobSetCount = 0;
        size_t instanceCursor = 0;

        // Note operators: hits waiting for their time (delays, repeats), and what the last note of the pass did.
        struct PendingHit {
            double start, end; // play ticks
            int8_t pitch;
            float velocity, release;
            const NoteExtras* extras;
            double origin; // where the note was written, for the devices' randomness
            int64_t pass;
            uint8_t key;
            uint8_t channel;
        };
        std::array<PendingHit, kMaxPendingHits> pending{};
        int pendingCount = 0;
        int64_t lastPass = -1;
        bool previousPlayed = true;
        // Per voice: the expression curves of the note it plays, and when that note started and how long it is.
        std::array<const NoteExtras*, kMaxVoices> voiceExtras{};
        std::array<double, kMaxVoices> voiceStart{}, voiceLength{};
        // Which written note a curved voice plays (clip and note ID values), to find its curves again in a new
        // snapshot; 0 = none.
        std::array<uint64_t, kMaxVoices> voiceClip{}, voiceNote{};
        // What the record inputs play now: per voice its key's own bend (semitones), timbre and pressure (0..1),
        // and for the track the values its channel shares, in volts (AUTO channels in `liveAutoMask`).
        std::array<float, kMaxVoices> liveBend{}, liveTimbre{}, livePress{};
        float liveChannelBend = 0, liveChannelPress = 0;
        std::array<float, kMaxAutomationLanes> liveAuto{};
        uint8_t liveAutoMask = 0;
        // CCs with no lane yet, and the free AUTO channel each plays on until its take makes the lane.
        std::array<int8_t, 128> liveCcOutput;
        int liveCcCount = 0;
        std::array<size_t, kMaxDrumLanes> laneCursor{}; // per own-loop lane of the playing clip
        // The device chain: what the snapshot says, and the runtime (an index into `chains`, -1 for none).
        const ChainPlayback* chainInfo = nullptr;
        int chain = -1;
        uint64_t outputVersion(uint64_t epoch) const {
            return voices.changeCount() + drums.changeCount() + autoChanges + epoch;
        }
    };
    /// The note ID a live key plays under, by source, channel and incoming pitch.
    static uint32_t liveId(size_t source, size_t channel, size_t pitch) {
        return chain::kLiveNote | uint32_t(source << 11 | channel << 7 | pitch);
    }
    /// A voice starting a new note forgets what the last live key on it was doing.
    static void clearLiveVoice(TrackState& state, int voice) {
        state.liveBend[size_t(voice)] = state.liveTimbre[size_t(voice)] = state.livePress[size_t(voice)] = 0;
    }
    /// Whether a track plays what comes in at the record inputs: armed with monitoring, or listening to them.
    static bool hearsInput(const TrackPlayback& tp) { return tp.audible && (tp.armed ? tp.monitor : true); }
    /// Whether a track plays the record inputs' values now: it takes the inputs and hears them.
    static bool playsLiveValues(const TrackPlayback& tp) {
        return (tp.armed || tp.listen == ListenSource::External) && hearsInput(tp);
    }
    /// Every track works out what is next on the next sample (after anything that changes playback).
    static void setVoiceCurves(TrackState& state, int voice, const NoteExtras* extras) {
        const NoteExtras*& slot = state.voiceExtras[size_t(voice)];
        state.curvedVoices += (extras != nullptr) - (slot != nullptr);
        slot = extras;
        state.voiceClip[size_t(voice)] = extras ? extras->clip : 0;
        state.voiceNote[size_t(voice)] = extras ? extras->note.id.value : 0;
    }
    /// After a snapshot swap: sounding voices find their notes' curves in the new snapshot (edited or not), so a
    /// long note keeps bending instead of going flat until it starts again.
    static void reattachCurves(const TrackPlayback& tp, TrackState& state);
    void wakeAll() {
        // Something outside the sample loop is about to change tracks: their voices count up to now first.
        for (size_t i = 0; i < trackCount; i++)
            catchUp(i, clockAt);
        wakeAt.fill(-INFINITY);
        idleUntil.fill(0);
        controlNow = true; // and automation catches up at once (a seek, a launch, a new snapshot)
    }
    /// Counts the samples track `i` skipped while idle, so its voices' clock reads `sample`.
    /// One track gets something from outside its own schedule (a note from a track it listens to): its voices
    /// count up to now first, and it is looked at again at once.
    void wakeTrack(size_t i) {
        catchUp(i, clockAt);
        wakeAt[i] = -INFINITY;
        idleUntil[i] = 0;
    }
    void catchUp(size_t i, uint64_t sample) {
        if (clockOf[i] < sample) {
            tracks[i].voices.skip(int64_t(sample - clockOf[i]));
            clockOf[i] = sample;
        }
    }
    /// After track `i`'s work for `sample`: can its outputs stay as they are until the next control tick?
    bool idleUntilControl(const TrackState& state, uint64_t sample) const;
    /// The next play tick at which `track` has something to do.
    double nextWake(const TrackPlayback& track, const TrackState& state, double now) const;

    void advanceSong(double dt, TransportEvents& events);
    void scheduleTrackNow(const TrackPlayback& track, TrackState& state, double prevPlay, double curPlay);
    void playClip(const TrackPlayback& track, TrackState& state, double from, double to);
    void playSource(const TrackPlayback& track, TrackState& state, double from, double to);
    void playArrangement(const TrackPlayback& track, TrackState& state, double from, double to);
    void fireArrangement(const TrackPlayback& track, TrackState& state, double from, double to, double playAtFrom);
    bool followsArrangement(const TrackState& state) const { return mode == PlayMode::Arranger && !state.overridden; }
    void backToArrangement(const TrackPlayback& track, TrackState& state);
    /// The song tick at play tick `at` in or just after the current sample.
    double songTickAt(double at) const;
    double clipTimeAt(const TrackPlayback& track, const TrackState& state, double at) const;
    void pushCapture(const TrackPlayback& track, const TrackState& state, double at);
    void pushCaptureStop(const TrackPlayback& track, const TrackState& state);
    void captureLoopWrap();
    static size_t arrangementIndex(const TrackPlayback& track, double songPos);
    void queueLaunch(const TrackPlayback& track, TrackState& state, int slot, bool immediate);
    void switchTo(const TrackPlayback& track, TrackState& state, int slot, double at);
    /// Carries out every queued launch now (on stop and reset).
    void applyQueued();
    void releaseClipVoices(TrackState& state);
    void resolveSlots(const TrackPlayback& track, TrackState& state);
    /// Fires the notes of `notes` starting in [from, to) (clip or song ticks), advancing `cursor`. Notes
    /// never sound past `capEnd`.
    void fireList(const TrackPlayback& track, TrackState& state, const std::vector<NoteEvent>& notes, size_t& cursor,
                  double from, double to, double playAtFrom, double capEnd = INFINITY, int64_t pass = -1,
                  const std::vector<NoteExtras>* extras = nullptr);
    /// Plays [a, b) of a loop (ticks since launch) whose notes live in [loopStart, loopStart + length).
    void playLoop(const TrackPlayback& track, TrackState& state, const std::vector<NoteEvent>& notes, size_t& cursor,
                  Tick loopStart, Tick length, double a, double b, double playAtFrom,
                  const std::vector<NoteExtras>* extras);
    void startNote(const TrackPlayback& track, TrackState& state, const NoteEvent& n, double onPlay, double endPlay,
                   int64_t pass);
    /// A note with extras: decides whether it plays on this pass and turns it into hits, now or later.
    void startWithOperators(const TrackPlayback& track, TrackState& state, const NoteEvent& n, const NoteExtras& ex,
                            double onPlay, double endPlay, int64_t pass);
    void startHit(const TrackPlayback& track, TrackState& state, const TrackState::PendingHit& hit);
    void firePending(const TrackPlayback& track, TrackState& state, double until);
    static void clearPending(TrackState& state);
    void hitDrum(const TrackPlayback& track, TrackState& state, int pitch, float velocity);
    void seekTrack(const TrackPlayback& track, TrackState& state);
    void seekAll();
    void writeFrame(const TrackPlayback& track, const TrackState& state, TrackFrame& frame) const;
    /// Track lanes play while the track follows the arrangement; clip lanes while their clip plays. A clip lane
    /// on the same channel as a track lane overrides or modulates it.
    void advanceAutomation(const TrackPlayback& track, TrackState& state);
    static void writeAutomation(const TrackPlayback& track, const TrackState& state, TrackFrame& frame);
    void startRecording();
    void stopRecording();
    void pushRecord(RecordEvent e);
    /// Reports a live event for one armed track, with the launcher clip it is playing (where an overdub lands).
    void pushTrackRecord(const TrackPlayback& tp, const TrackState& state, RecordEvent e);
    static bool takesInput(const TrackPlayback& track, LiveSource source, int channel);
    void writeDrumFrame(const TrackPlayback& track, const TrackState& state, TrackFrame& frame) const;
    void releaseAll(TrackState& state);

    // Device chains run on the engine clock (freeTick), which also runs while stopped, so live notes go through
    // them too. Voices end on the same clock.
    struct ChainRuntime {
        TrackId owner;
        uint64_t structure = 0;
        chain::Chain chain;
    };
    class ChainSink;
    double clockOffset() const { return freeTick - playTick; }
    bool chainActive(const TrackState& state) const {
        return state.chain >= 0 && state.chainInfo && state.chainInfo->active;
    }
    /// A note from a clip or a live key: into the track's chain, or straight to the voices without one.
    void deliver(const TrackPlayback& track, TrackState& state, const chain::ChainNote& note);
    /// A note leaving the chain: a voice (or a drum hit) starts.
    void sound(const TrackPlayback& track, TrackState& state, const chain::ChainNote& note);
    void endNote(const TrackPlayback& track, TrackState& state, uint32_t id);
    /// Whether any track listens to this one (its live notes then go the long way, through deliver()).
    bool hasListeners(const TrackPlayback& from) const;
    /// A copy of a note for the tracks that listen to `from` at this point (after or before its devices).
    void forwardNote(const TrackPlayback& from, const chain::ChainNote& note, ListenSource point);
    /// The end of an open note (a held key, a legato note) for the same listeners.
    void forwardEnd(const TrackPlayback& from, uint32_t id, ListenSource point);
    /// An open note entering a track ends: in its chain, or at its voices without one; its listeners hear it too.
    void endInput(const TrackPlayback& track, TrackState& state, uint32_t id);
    /// Whether a track runs its clips: it is heard, or an audible track listens to it (then it only feeds).
    bool runs(const TrackPlayback& track) const {
        return track.audible || (track.busSlot >= 0 && track.busSlot < kMaxTracks && feeds[size_t(track.busSlot)]);
    }
    void runChain(const TrackPlayback& track, TrackState& state, double until);
    void assignChains();
    /// Voices of notes whose end only the chain would have sent (legato, held notes) end now.
    void releaseOpenVoices(TrackState& state);
    void triggerModulators(const TrackState& state, const chain::ChainNote& note);
    void advanceModulation();
    /// What each track's knob lanes set this block (before modulation reads them).
    void advanceKnobLanes();
    /// The clip whose lanes play on a track now, and its clip time; nullptr when none does.
    const ClipPlayback* lanesClip(const TrackPlayback& tp, TrackState& state, double song, double play, double& clipT);
    static void endAudition(TrackState& state, int voice);
    void advanceAuditions(TrackState& state);

    const Snapshot* snapshot = nullptr;
    size_t trackCount = 0;
    std::array<TrackState, kMaxTracks> tracks;
    std::array<TrackState, kMaxTracks> previous; // scratch for setSnapshot, kept here so it never allocates
    int retriggerGap = 48;
    std::array<bool, kMaxTracks> slotUsed{};
    // The bus is double buffered: which buffer this sample writes, so each gets every change once.
    std::array<const TrackFrame*, 2> buffers{};
    uint64_t frameEpoch = 1;                   // bumps when a new snapshot may change any frame
    int clearUnused = 2;                       // samples left that clear the frames of unused bus slots
    std::array<int, kMaxTracks> indexOfSlot{}; // track index by bus slot, -1 for none
    std::array<bool, kMaxTracks> feeds{};      // by bus slot: an audible track listens to this one
    // By bus slot: the track listens to another track and that doesn't lead back to it. A loop would keep a note
    // circling through the chains' queues, so tracks in a loop listen to nothing (the menu never makes one).
    std::array<bool, kMaxTracks> listening{};
    bool listens(const TrackPlayback& tp) const {
        return tp.busSlot >= 0 && tp.busSlot < kMaxTracks && listening[size_t(tp.busSlot)];
    }
    int forwardDepth = 0; // how deep a note is in listening tracks (stops loops)
    static constexpr int kMaxForwardDepth = 4;

    double sampleRate = 48000;
    double bpm = 120;
    double ticksPerSample = 0;
    double stepOverride = -1;                     // set for one sample by setStep
    static constexpr uint64_t kControlBlock = 32; // samples between automation updates (a power of two)
    uint64_t sampleCount = 0;
    // Per track, packed together: on most samples most tracks have nothing to do, and these are all a sample then
    // reads of them. Other modules run between samples and push the big track states out of the cache (§18).
    // wakeAt: nothing is due before this play tick (no note start or end, hit, or launch), so scheduling skips the
    // track until then; -infinity works it out on the next sample. idleUntil: before this sample the track's
    // outputs can't change on their own, so the output loop skips it. clockOf: the sample its voices count to.
    std::array<double, kMaxTracks> wakeAt{};
    std::array<uint64_t, kMaxTracks> idleUntil{};
    std::array<uint64_t, kMaxTracks> clockOf{};
    uint64_t clockAt = 0; // the sample events happen at: the one being processed, or else the next one
    bool controlNow = true;
    bool loopOn = false;
    Tick clockDivision = kPpq / 4;

    bool playing = false;
    bool resetPending = false;
    double songTick = 0;
    double windowStart = 0; // song tick where the current sample's window begins
    double playTick = 0;
    uint64_t activeMask = 0;
    PlayMode mode = PlayMode::Launcher;
    // The current sample's song spans (two when the loop wraps inside it) and its starting play tick.
    double spans[2][2] = {{0, 0}, {0, 0}};
    int spanCount = 0;
    double windowPlay = 0;
    CaptureQueue* captureQueue = nullptr;
    bool capturing = false;
    double pendingTempo = 0; // a scene's tempo, applied when playTick reaches pendingTempoAt
    double pendingTempoAt = 0;

    RecordQueue* recordQueue = nullptr;
    bool recordingOn = false;
    double countInLength = 0; // > 0 while counting in
    double countInPos = 0;
    double freeTick = 0;
    bool liveToKey = false;
    bool fill = false;
    // Live notes held now, by source, channel and incoming pitch: the bus slots they went to and the pitch
    // they play, so each note-off reaches the same tracks even if arming changed in between.
    std::array<std::array<std::array<uint64_t, 128>, 16>, 3> liveTracks{}; // by LiveSource
    std::array<std::array<std::array<int8_t, 128>, 16>, 3> livePitch{};

    // Devices and modulators. Large tables live on the heap, allocated once.
    std::unique_ptr<std::array<ChainRuntime, kMaxTracks>> chains;
    std::unique_ptr<float[]> knobValues; // knob table with modulation, as the devices read it
    std::unique_ptr<std::array<mods::State, kMaxModulatorSlots>> modStates;
    std::unique_ptr<std::atomic<float>[]> liveKnobs;      // for the UI
    std::unique_ptr<std::atomic<float>[]> liveBases;      // for the UI: knob lane values
    std::unique_ptr<std::atomic<float>[]> liveModulators; // for the UI
    ParamRef overrideRef;
    float overrideValue = 0;
    bool overrideOn = false;
    bool overrideWasOn = false;
    bool knobsDirty = true;
    bool knobLanesOn = false, knobLanesWereOn = false; // some knob lane set a knob this block, the last one
};

} // namespace tessera
