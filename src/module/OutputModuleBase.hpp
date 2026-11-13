#pragma once

#include "../model/AudioSource.hpp"
#include "../model/Track.hpp"
#include "AudioHandoff.hpp"
#include "OutputBus.hpp"

#include <rack.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace tessera {

class TesseraModule;

/// Which Tessera and track an output module follows. Saved in the output module's own JSON.
struct OutputBinding {
    int64_t sourceId = -1;
    std::string sourceName; // fallback when module IDs change (patch import, paste)
    TrackId track;
    /// Outputs whatever track is selected in the source's editor, to audition one voice across tracks.
    bool followSelected = false;
};

enum class BindingStatus { NoSource, TrackMissing, Bound };

/// What an output module is bound to, for its display, its dim jacks and their tooltips (docs/design/AUDIO_MIXER.md
/// §8.1). None while it has no source or its track is missing.
enum class BoundKind { None, Instrument, Drum, Audio, Master };

/// Shared base for Track Out, Drum Out and Auto Out: binding, registry membership and safe frame reading.
///
/// The binding resolves on the UI thread (refreshBinding, called from the widget's step). process() only
/// reads atomics and the bus. Subclasses just copy a TrackFrame to their ports.
class OutputModuleBase : public rack::engine::Module {
  public:
    /// `acceptedType` empty: any track (Auto Out, Track Out). `master`: the master can be bound too (Track Out).
    explicit OutputModuleBase(std::optional<TrackType> acceptedType, bool master = false)
        : accepted(acceptedType), acceptsMaster(master) {}
    ~OutputModuleBase() override;

    void process(const ProcessArgs& args) final;
    void processBypass(const ProcessArgs& args) final;
    void onAdd(const AddEvent& e) override;
    void onRemove(const RemoveEvent& e) override;
    json_t* dataToJson() override;
    void dataFromJson(json_t* root) override;

    // UI thread
    void refreshBinding();
    void bind(const TesseraModule& source, TrackId track);
    void setFollowSelected(bool on) { bindingData.followSelected = on; }
    /// The channel the module plays: 0 for all, 1..16 for only the notes on that channel (docs/design/PLAYHEADS.md
    /// §2.2). Only Track Out offers it.
    int part() const { return partValue.load(std::memory_order_relaxed); }
    void setPart(int channel) { partValue.store(std::clamp(channel, 0, 16), std::memory_order_relaxed); }
    /// Moves to the next (+1) or previous (-1) track this module can output, wrapping around.
    void stepTrack(int delta);
    const OutputBinding& binding() const { return bindingData; }
    TesseraModule* source() const;
    BindingStatus status() const;
    bool accepts(TrackType type) const { return !accepted || *accepted == type; }
    bool canBindMaster() const { return acceptsMaster; }
    /// Tracks of the accepted type in display order, then the master when it can be bound.
    std::vector<TrackId> candidateTracks(const TesseraModule& source) const;
    /// What it is bound to now (UI thread).
    BoundKind boundKind() const;
    /// The return bus slot its IN jacks write to while audio comes back on them, for Tessera's mix plan; -1 when
    /// nothing comes back (no IN, no cable, following the editor, or not bound). UI thread.
    int activeReturn() const;
    /// The return bus slot its source inputs (Drum Out's lane INs) write to while any has a cable; -1 otherwise.
    int activeSource() const;
    /// Its return bus slot while it has a place in its track's chain: a cable on IN or on OUT; -1 otherwise. UI thread.
    int chainSlot() const;
    /// Whether it is an effect loop of its track (DECISIONS §153): bound, with a cable on OUT. What comes back on IN
    /// then replaces the sound by its Mix instead of adding to it. UI thread.
    bool isLoop() const;
    /// How much of an effect loop is heard, 0..1 (1 = only what comes back), saved with the module. UI thread; the
    /// caller records the undo step.
    float mixAmount() const { return mix; }
    void setMix(float amount);
    /// Plays a Mix at once while a drag moves it, before it is committed; nullopt ends it.
    void holdMix(std::optional<float> amount);
    /// The Mix to show: what a drag holds, else the saved one.
    float shownMix() const { return heldMix ? *heldMix : mix; }
    /// An effect loop's round trip through the rack in samples (3 plus one per module on the way), found by Tessera
    /// from the cables. UI thread.
    int loopDelay() const { return loopDelaySamples.load(std::memory_order_relaxed); }
    void setLoopDelay(int samples) { loopDelaySamples.store(std::clamp(samples, 0, 63), std::memory_order_relaxed); }
    /// Whether input `id` is one of its IN jacks, or output `id` one of its OUT jacks.
    bool isInJack(int id) const { return audioPorts.inL >= 0 && (id == audioPorts.inL || id == audioPorts.inR); }
    bool isOutJack(int id) const { return audioPorts.outL >= 0 && (id == audioPorts.outL || id == audioPorts.outR); }
    /// Its place in its track's chain, saved with the module; -1 until the user orders the chain (then rack order).
    int chainOrder() const { return chainPos; }
    void setChainOrder(int pos) { chainPos = pos; }
    /// True once, for a module loaded from a patch made before effect loops had a Mix, so Tessera can set the Mix
    /// that keeps the patch sounding as it did (DECISIONS §153).
    bool takeLegacyMix() {
        bool was = legacyMix;
        legacyMix = false;
        return was;
    }
    /// Its source inputs (Drum Out's lane INs): how many, whether one has a cable, and its level after its own fader
    /// and pan (volts, a peak falling 20 dB a second), for the mixer's lane strips. UI thread.
    int sourceCount() const { return audioPorts.sourceCount; }
    bool sourcePatched(int k) const;
    float sourceLevel(int k) const;
    static constexpr int kMaxSources = 8;
    TrackId boundTrack() const { return bindingData.track; }
    /// What it brings in as a source of its track (§7.8): Level and Use, saved with the module. Applies to every
    /// audio input it has (IN, and a Drum Out's lane INs). UI thread; the caller records the undo step.
    SourceLevel sourceSetting() const { return setting; }
    void setSourceSetting(SourceLevel level);
    /// Plays a Level at once while the sources list drags it, before the drag is committed; nullopt ends it.
    void holdSourceLevel(std::optional<float> db) { heldLevelDb = db; }
    /// Plays a pan at once while a mixer strip drags it, before the drag is committed; nullopt ends it.
    void holdSourcePan(std::optional<float> pan);
    /// False while another input of its track is soloed. UI thread.
    void setHeardBySolo(bool heard);
    /// What it brings in now, after its Level (volts, a peak falling 20 dB a second), for the sources list.
    float inputLevel() const { return inputMeter.load(std::memory_order_relaxed); }
    /// "Counted once": its IN takes the same cable as another source of the same track, so it adds nothing. Set by
    /// the Tessera it follows. UI thread.
    void setCountedOnce(bool on) { countedOnce = on; }
    bool isCountedOnce() const { return countedOnce; }
    /// Whether its OUT has a cable, so what a track gets sent can reach an effect (§7.6 "Sends go nowhere").
    bool outPatched() const {
        return audioPorts.outL >= 0 &&
               (outputs[size_t(audioPorts.outL)].channels > 0 || outputs[size_t(audioPorts.outR)].channels > 0);
    }
    /// Makes its screen flash for a moment, so the user finds it in the rack (Show in the rack). UI thread.
    void flash() { flashUntil = rack::system::getTime() + 1.2; }
    /// How strongly its screen flashes now, 0 when it does not. UI thread.
    float flashAmount() const;

  protected:
    /// Audio jacks a subclass has (§8.1): OUT L/R carry the track's sound before its fader, IN L/R bring audio back
    /// into it, and the IN light shows what arrives (green, red above 0 dB). -1 where it has none.
    struct AudioPorts {
        int outL = -1, outR = -1, inL = -1, inR = -1, inLight = -1;
        // Mono inputs that are the track's own sound (Drum Out's lane INs): mixed into it and into its OUT.
        int sourceFirst = -1, sourceCount = 0;
    };
    void setAudioPorts(AudioPorts ports) { audioPorts = ports; }

  protected:
    // Audio thread
    virtual void writeFrame(const TrackFrame& frame) = 0;
    /// Hold every output except gates, which go low.
    virtual void silenceGates() = 0;

  private:
    void processAudio(const ProcessArgs& args, const OutputBus* b, TrackId track);
    void releaseReturn();

    const std::optional<TrackType> accepted;
    const bool acceptsMaster;
    AudioPorts audioPorts;
    OutputBinding bindingData;
    bool needsDefaultBinding = true;
    // Set when the module arrives from JSON (patch load, paste, duplicate), so the first refresh can check
    // whether its Tessera was pasted alongside it.
    bool checkPaste = false;
    double addedFrameTime = 0;
    double flashUntil = 0;

    AudioHandoff<const OutputBus> bus;
    std::atomic<int> busSlot{-1};
    std::atomic<uint64_t> boundTrackId{0};
    std::atomic<int> partValue{0}; // the channel it plays (part()), 0 for all
    std::atomic<int> audioSlot{-1};         // where OUT reads on the output bus: the track's slot, or the master's
    std::atomic<int> returnSlot{-1};        // where IN writes on the return bus
    std::atomic<int> sourceSlot{-1};        // where the source inputs write
    std::atomic<bool> audioOn{false};       // OUT and IN work: bound, and not following the editor's selection
    std::atomic<bool> trackGone{false};     // its track was deleted: IN is ignored and its light turns amber
    std::shared_ptr<OutputBus> returnOwner; // the bus whose return slot this module holds (UI thread)
    float inPeak = 0;
    // Each source's gains (its level and pan), set on the UI thread, ramped on the audio thread; and its meter.
    std::array<std::atomic<float>, kMaxSources> sourceTargetL{}, sourceTargetR{};
    std::array<float, kMaxSources> sourceGainL{}, sourceGainR{};
    std::array<float, kMaxSources> sourcePeak{};
    std::array<std::atomic<float>, kMaxSources> sourceMeter{};
    int meterCount = 0;
    bool sourcesFresh = true;
    SourceLevel setting;
    std::optional<float> heldLevelDb;
    std::optional<float> heldPan;
    bool heardBySolo = true;
    std::atomic<float> inputPanL{1}, inputPanR{1};
    bool countedOnce = false;
    std::atomic<float> inputTarget{1}; // Level as a gain, 0 when not in use or counted once
    float inputGain = 1;
    float mix = 1; // an effect loop's Mix (isLoop())
    std::optional<float> heldMix;
    bool legacyMix = false;
    int chainPos = -1;
    std::atomic<bool> loopOn{false};
    std::atomic<int> loopDelaySamples{4};
    std::atomic<float> dryTarget{1}; // what stays of the sound before it, 1 unless it is an effect loop
    std::atomic<float> wetTarget{1}; // what is heard of its IN by the Mix, 1 unless it is an effect loop
    float dryGain = 1, wetGain = 1;
    float inputPeak = 0;
    std::atomic<float> inputMeter{0};
};

} // namespace tessera
