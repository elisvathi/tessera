#pragma once

#include "AudioBus.hpp"
#include "AudioPlayback.hpp"
#include "FrameRing.hpp"
#include "Engine.hpp"
#include "Varispeed.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace tessera {

/// Plays audio clips and mixes audio tracks into the master, one sample at a time, following the note engine's
/// transport: the timeline position in Arranger mode, launched slots otherwise (docs/design/AUDIO_MIXER.md §5.2,
/// §8.4). Real-time safe: every array is sized when it is made; process() never allocates, locks or frees.
///
/// No clicks: every clip edge has a 2 ms fade, a jump in a clip (a loop seam, a seek, a render switch) crossfades,
/// a stop fades out, and fader, pan, mute and solo changes ramp over 32 samples.
class AudioPlayer {
  public:
    static constexpr int kVoicesPerTrack = 6;
    static constexpr int kBlock = 32;
    static constexpr double kEdgeFadeSeconds = 0.002;
    static constexpr double kSeamFadeSeconds = 0.005;
    static constexpr double kRenderFadeSeconds = 0.010;
    /// How far a render's tempo may be from the song's before the clip plays the quick version instead (§5.2).
    static constexpr double kRenderTolerance = 0.01;

    AudioPlayer() = default;

    void setSampleRate(double rate) { sampleRate = rate > 0 ? rate : 48000; }

    /// The snapshot to play, which must stay alive until it is replaced and the engine moved on. Real-time safe.
    void setSnapshot(const AudioSnapshot* next);
    const AudioSnapshot* current() const { return snapshot; }

    /// The rack's side of one sample (§8.3, §8.4): the return bus to read output modules' IN jacks from, and the
    /// output bus's audio array to write each track's OUT to. Either may be null.
    struct Rack {
        const ReturnBus* returns = nullptr;
        AudioOutFrame* out = nullptr;
        int64_t frame = 0;
        float rollInL = 0, rollInR = 0; // Roll's own IN jacks, in volts
    };

    /// Plays one sample: reads the engine's transport after its process(), plays the clips, mixes. `left` and
    /// `right` get the master in volts (10 V = 0 dB), ready for MAIN. With `rack`, each track's sound before its
    /// fader goes to OUT and what came back on IN is mixed in.
    void process(const Engine& engine, float& left, float& right, const Rack& rack);
    /// The same without the rack: no OUT, no returns (tests, and the player on its own).
    void process(const Engine& engine, float& left, float& right);

    /// Whether anything can sound: there are tracks that make sound (or returns to the master), or voices still
    /// fading out. Tessera skips process() otherwise, so a song without audio costs what it did before audio existed.
    bool active() const {
        return (snapshot && (!snapshot->tracks.empty() || !snapshot->masterReturns.empty())) || sounding;
    }

    // ---- For the UI (any thread) ----------------------------------------------------------------------------

    /// A track's (by its index in the snapshot) or the master's level in volts, peaks falling 20 dB a second.
    float level(int track, int side) const {
        return track >= 0 && track < kMaxTracks ? meters[size_t(track)][size_t(side & 1)].load(std::memory_order_relaxed)
                                                : 0.f;
    }
    float masterLevel(int side) const { return masterMeter[size_t(side & 1)].load(std::memory_order_relaxed); }
    /// The highest master peak (volts) since the last clearClip(), for the clip light.
    float masterClipPeak() const { return clipPeak.load(std::memory_order_relaxed); }
    void clearClip() { clipPeak.store(0, std::memory_order_relaxed); }

    /// Which control a hold is for: only that one wins over its lane, and only that one records (§7.3).
    static constexpr uint8_t kHoldVolume = 1, kHoldPan = 2, kHoldClips = 4, kHoldSend = 8;
    /// Holds a fader or pan (`parts`) at these values while the user drags it, before the edit is committed (§8.4:
    /// a drag never rebuilds the snapshot). `track` is the TrackId value; the master's ID holds the master's volume.
    void hold(uint64_t track, float volumeGain, float pan, uint8_t parts);
    /// Holds an audio track's clips (or, for the master's ID, its mix) at `gain` while a source Level drag runs.
    void holdClips(uint64_t track, float gain);
    /// Holds the send from `track` to `to` at `gain` while a drag moves it (Show sends, a send knob).
    void holdSend(uint64_t track, uint64_t to, float gain);
    /// The send from `track` to `to` a drag holds now, as a gain, for recording it into its lane. UI thread.
    bool heldSend(uint64_t track, uint64_t to, float& gain) const;
    /// Whether a Send lane moves track `track`'s (its index in the snapshot) send `k` now, and to what (dB).
    bool sendAutomated(int track, int k, float& db) const {
        if (track < 0 || track >= kMaxTracks || k < 0 || k >= kMaxSendsPerTrack ||
            !(sendAutoFlags[size_t(track)].load(std::memory_order_relaxed) & (1u << k)))
            return false;
        db = sendAutoDb[size_t(track)][size_t(k)].load(std::memory_order_relaxed);
        return true;
    }
    /// What track `track`'s send `k` carries now (volts, a peak falling 20 dB a second), for Show sends' meters.
    float sendLevel(int track, int k) const {
        return track >= 0 && track < kMaxTracks && k >= 0 && k < kMaxSendsPerTrack
                   ? sendMeters[size_t(track)][size_t(k)].load(std::memory_order_relaxed)
                   : 0.f;
    }
    // ---- Capture (§8.6): recordings, bounces and exports copy a signal into a ring, sample by sample.

    /// What a tap copies: what comes in on a track's IN (recording), its sound before the fader (bounce), its sound
    /// after the fader as heard (a stem), or the master after its fader (export).
    enum class TapKind : uint8_t { In, Sound, Stem, Master };
    static constexpr int kMaxTaps = 24;
    /// A tap waits until the song plays (and records, with `whileRecording`) from song tick `from`, then copies
    /// until `to`, the transport stops, recording ends, or the song jumps back (a loop wrap): then it is done.
    struct TapSpec {
        TrackId track;
        TapKind kind = TapKind::In;
        double from = -1, to = -1; // song ticks; -1: at once, open-ended
        bool whileRecording = false;
        // With `from`: start only within this many ticks after it, so a tap armed before a jump to `from` never
        // starts at the old position. Negative: any time after `from`.
        double startWithin = -1;
        FrameRing* ring = nullptr; // owned by the caller, alive until disarmTap() and a block more
    };
    enum class TapState : int { Idle, Armed, Running, Done };
    /// UI thread.
    void armTap(int n, const TapSpec& spec);
    void disarmTap(int n);
    TapState tapState(int n) const { return TapState(taps[size_t(n)].state.load(std::memory_order_acquire)); }
    /// The song tick of the first frame a tap copied.
    double tapStart(int n) const { return taps[size_t(n)].firstTick.load(std::memory_order_relaxed); }
    /// The level of a running tap's signal now (volts, the last block's peak), for "until silent".
    float tapLevel(int n) const { return taps[size_t(n)].level.load(std::memory_order_relaxed); }

    /// A group strip's (its index in the snapshot) level in volts, peaks falling 20 dB a second.
    float groupLevel(int group, int side) const {
        return group >= 0 && group < kMaxTracks ? groupMeters[size_t(group)][size_t(side & 1)].load(std::memory_order_relaxed)
                                                : 0.f;
    }

    /// The longest delay compensation can add, in samples (about 170 ms at 48 kHz).
    static constexpr int kMaxDelay = 8192;
    /// Makes room for delay compensation, once: call on the UI thread before a snapshot with delays plays.
    void ensureDelays() {
        if (delays.load(std::memory_order_acquire))
            return;
        delayMemory.reset(new float[size_t(kMaxTracks) * 2 * kMaxDelay]());
        delays.store(delayMemory.get(), std::memory_order_release);
    }

    /// Measures the recording offset (§8.6): counts samples from now until track `track`'s IN goes above 1 V.
    /// The caller fires the click at the same sample. UI thread.
    void startMeasure(uint64_t track) {
        measureTrack.store(track, std::memory_order_relaxed);
        measureResult.store(-2, std::memory_order_relaxed);
        measureCount = 0;
        measuring.store(true, std::memory_order_release);
    }
    /// The samples it took, -1 when nothing came back within a second, -2 while still waiting.
    int64_t measured() const { return measureResult.load(std::memory_order_acquire); }

    /// What leaves a track's OUT now (its index in the snapshot), in volts, a peak falling 20 dB a second (§7.6).
    float outLevel(int track) const {
        return track >= 0 && track < kMaxTracks ? outMeters[size_t(track)].load(std::memory_order_relaxed) : 0.f;
    }
    void releaseHolds();
    /// The fader (`pan` false, as a gain) or pan a drag holds on `track` now, for recording it into its lane. UI thread.
    bool held(uint64_t track, bool pan, float& value) const;

    /// Whether a fader (`pan` false) or pan lane moves track `track` (its index in the snapshot) now, and to what:
    /// dB for the fader, -1..1 for pan. The mixer and the headers show it and ring the control (§7.3).
    bool automated(int track, bool pan, float& value) const {
        if (track < 0 || track >= kMaxTracks ||
            !(autoFlags[size_t(track)].load(std::memory_order_relaxed) & (pan ? 2 : 1)))
            return false;
        value = (pan ? autoPan : autoVolume)[size_t(track)].load(std::memory_order_relaxed);
        return true;
    }
    /// Whether the master's Volume lane moves its fader now, and to what (dB).
    bool masterAutomated(float& db) const {
        if (!masterAuto.load(std::memory_order_relaxed))
            return false;
        db = masterAutoDb.load(std::memory_order_relaxed);
        return true;
    }

  private:
    struct Voice {
        bool on = false;
        uint64_t key = 0; // which instance or launched slot it plays
        int clip = -1;         // index into the snapshot's clips
        uint64_t clipId = 0;   // to find the clip again in a new snapshot
        const AudioBuffer* buffer = nullptr;
        bool rendered = false;
        bool exact = false;   // plays every frame in turn: no interpolation needed
        bool free = false;    // Off clips: keeps its own pace, ignoring tempo changes
        double pos = 0;       // frame in `buffer`
        double rate = 0;      // frames per sample
        int64_t frame = 0;    // exact voices
        bool advanced = false; // a leaving voice already moved on this sample
        double lastTick = 0;  // clip tick, to see seams and jumps
        float fade = 0, fadeStep = 0; // crossfade gain and its change per sample
        bool leaving = false; // fading out for good
        float gain = 0;       // this sample's clip gain and envelope
    };

    /// What should play on a track now: up to two clips (two only where timeline clips overlap).
    struct Wanted {
        uint64_t key = 0;
        int clip = -1;
        double tick = 0;     // clip tick
        double tickRate = 0; // clip ticks per sample
        float envelope = 1;
    };

    struct TrackRun {
        std::array<Voice, kVoicesPerTrack> voices{};
        size_t cursor = 0; // first timeline instance that may play
        int engineIndex = -1;
        float gainL = 0, gainR = 0, stepL = 0, stepR = 0; // fader and pan, ramping
        float outGain = 0, outStep = 0; // OUT: the clips while the track is heard, ramping to 0 when muted (§4.5)
        float clipsGain = 1, clipsStep = 0; // the clips' source Level
        // Into the master: what came back on IN (heard), and its own sound (heard and own). One ramp per signal, so a
        // mute never multiplies two ramps into a click.
        float heardGain = 0, heardStep = 0;
        float heardOwnGain = 0, heardOwnStep = 0;
        std::array<float, kMaxSendsPerTrack> sendGain{}, sendStep{};
        std::array<size_t, kMaxSendsPerTrack> sendCursor{};
        std::array<float, kMaxSendsPerTrack> sendPeak{};
        float outPeak = 0;
        // This sample's signals, for the taps.
        float inL = 0, inR = 0, soundL = 0, soundR = 0, stemL = 0, stemR = 0;
        bool fresh = true; // a track just seen starts at its fader: nothing of it plays yet, so nothing to ramp
        bool steady = false; // its voices step on unchanged for the rest of this block
        int slot = -1;       // the launched slot a steady block plays, or -1 on the timeline
        float peakL = 0, peakR = 0;
    };

    /// What should play on `track` at song tick `t`, with the launched clip `elapsed` ticks in. With `peek` it only
    /// looks (ahead), leaving the track's cursor alone.
    int wanted(const Engine& engine, const AudioTrackPlay& track, TrackRun& run, std::array<Wanted, 2>& out, double t,
               double elapsed, bool peek);
    /// Whether the track's voices can step on unchanged for the rest of the block (see process()).
    bool steadyAhead(const Engine& engine, const AudioTrackPlay& track, TrackRun& run, const std::array<Wanted, 2>& now,
                     int n, double song);
    /// The engine's ticks since the track's launched clip started; NaN when none plays.
    double engineElapsed(const Engine& engine, const TrackRun& run) const;
    void follow(TrackRun& run, const std::array<Wanted, 2>& want, int count);
    /// Where in its buffer a voice of clip `c` plays at `w`'s tick, and how fast it moves: through a straight render,
    /// or the prepared buffer through the clip's warp markers.
    static void positionIn(const AudioClipPlay& c, bool rendered, const Wanted& w, double& target, double& rate);
    void startVoice(TrackRun& run, const Wanted& w, float fadeSeconds);
    static void retire(Voice& v, float samples);
    /// Moves a voice that plays on its own (leaving) one sample on.
    static void advance(Voice& v) {
        if (v.exact)
            v.frame++;
        else
            v.pos += v.rate;
    }
    /// A voice follow() just retired moves on at once, so this sample reads its next frame, not the last one again.
    static void leaveNow(Voice& v) {
        advance(v);
        v.advanced = true;
    }
    void place(Voice& v, const Wanted& w) const;
    /// The dip at a loop seam that has no audio after it to crossfade with: 2 ms down before it, 2 ms up after it.
    float seamEnvelope(const AudioClipPlay& c, double u, double ticksPerClip, bool wrapped) const;
    /// The gain of instance `a` at `songTick` from its fades, crossfades and safety fades; `seam` is its loop seam's
    /// dip.
    float envelopeOf(const AudioInstancePlay& a, double songTick, double ticksPerClip, float seam) const;
    void startBlock(const Engine& engine);

    const AudioSnapshot* snapshot = nullptr;
    // Per snapshot track, then tracks deleted while they sounded ("ghosts") fading out after them.
    std::array<TrackRun, kMaxTracks> runs{};
    std::array<TrackRun, kMaxTracks> spare{}; // scratch for setSnapshot, kept here so it never allocates
    std::array<TrackId, kMaxTracks> runIds{};
    size_t ghosts = 0;
    double ticksPerSample = 0;
    double blockTempo = 0;
    bool blockSteady = false;
    Varispeed sinc;
    double sampleRate = 48000;
    double tempo = 120;
    double lastSong = 0, lastPlay = 0;
    bool wasPlaying = false;
    bool jumped = false;
    bool sounding = false;
    int blockPos = 0;
    float masterGain = 0, masterStep = 0;
    float mixGain = 1, mixStep = 0; // the master's mix as a source
    bool masterFresh = true;
    size_t masterLaneCursor = 0;
    std::atomic<bool> masterAuto{false};
    std::atomic<float> masterAutoDb{0};
    float masterPeakL = 0, masterPeakR = 0;

    std::array<std::array<std::atomic<float>, 2>, kMaxTracks> meters{};
    std::array<std::atomic<float>, kMaxTracks> outMeters{};
    struct Tap {
        std::atomic<int> state{0};
        std::atomic<uint64_t> track{0};
        std::atomic<int> kind{0};
        std::atomic<double> from{-1}, to{-1}, firstTick{-1}, startWithin{-1};
        std::atomic<bool> whileRecording{false};
        std::atomic<FrameRing*> ring{nullptr};
        std::atomic<float> level{0};
        int run = -1; // audio thread: the run it reads, found at each block
        float peak = 0;
    };
    std::array<Tap, kMaxTaps> taps{};
    struct GroupRun {
        uint64_t id = 0;
        float gainL = 0, gainR = 0, stepL = 0, stepR = 0;
        float busL = 0, busR = 0, peakL = 0, peakR = 0;
        bool fresh = true;
    };
    std::array<GroupRun, kMaxTracks> groupRuns{};
    std::array<std::array<std::atomic<float>, 2>, kMaxTracks> groupMeters{};
    std::unique_ptr<float[]> delayMemory; // per bus slot, two channels of kMaxDelay; made once, never freed early
    std::atomic<float*> delays{nullptr};
    size_t delayPos = 0;
    std::atomic<bool> measuring{false};
    std::atomic<uint64_t> measureTrack{0};
    std::atomic<int64_t> measureResult{-2};
    int64_t measureCount = 0;
    std::atomic<bool> anyTap{false};
    double tapLastSong = 0;
    /// Audio thread: copies this sample's signals into the running taps, starting and ending them.
    void runTaps(const Engine& engine, double song, float masterL, float masterR);
    std::array<std::atomic<uint32_t>, kMaxTracks> sendAutoFlags{};
    std::array<std::array<std::atomic<float>, kMaxSendsPerTrack>, kMaxTracks> sendMeters{};
    std::array<std::array<std::atomic<float>, kMaxSendsPerTrack>, kMaxTracks> sendAutoDb{};
    std::array<std::array<float, 2>, kMaxTracks> sendBus{}; // what senders put into each track's OUT this sample
    std::array<std::atomic<uint8_t>, kMaxTracks> autoFlags{};
    std::array<std::atomic<float>, kMaxTracks> autoVolume{}, autoPan{};
    std::array<std::atomic<float>, 2> masterMeter{};
    std::atomic<float> clipPeak{0};
    struct Hold {
        std::atomic<uint64_t> track{0};
        std::atomic<float> volume{1}, pan{0}, clips{1}, send{0};
        std::atomic<uint64_t> sendTo{0};
        std::atomic<uint8_t> parts{0};
    };
    std::array<Hold, kMaxTracks + 1> holds{};
};

/// The left and right gains for `pan` (-1..1) on a stereo sound: a constant-power balance, both at 1 in the middle.
inline void balanceGains(float pan, float& left, float& right) {
    constexpr float kQuarterPi = 0.78539816f;
    float theta = (pan + 1) * kQuarterPi;
    left = std::fmin(1.f, std::cos(theta) * 1.41421356f);
    right = std::fmin(1.f, std::sin(theta) * 1.41421356f);
}

/// The shape of a fade from 0 to 1 at `x` (0..1). `curve` bends it (-1..1); 0 is a straight line, or equal power
/// when `crossfade` is set, so two overlapping clips keep their loudness through the overlap.
float fadeShape(float x, float curve, bool crossfade);

} // namespace tessera
