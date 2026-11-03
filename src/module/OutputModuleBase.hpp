#pragma once

#include "../model/Track.hpp"
#include "AudioHandoff.hpp"
#include "OutputBus.hpp"

#include <rack.hpp>

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
    TrackId boundTrack() const { return bindingData.track; }

  protected:
    /// Audio jacks a subclass has (§8.1): OUT L/R carry the track's sound before its fader, IN L/R bring audio back
    /// into it, and the IN light shows what arrives (green, red above 0 dB). -1 where it has none.
    struct AudioPorts {
        int outL = -1, outR = -1, inL = -1, inR = -1, inLight = -1;
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

    AudioHandoff<const OutputBus> bus;
    std::atomic<int> busSlot{-1};
    std::atomic<uint64_t> boundTrackId{0};
    std::atomic<int> audioSlot{-1};         // where OUT reads on the output bus: the track's slot, or the master's
    std::atomic<int> returnSlot{-1};        // where IN writes on the return bus
    std::atomic<bool> audioOn{false};       // OUT and IN work: bound, and not following the editor's selection
    std::shared_ptr<OutputBus> returnOwner; // the bus whose return slot this module holds (UI thread)
    float inPeak = 0;
};

} // namespace tessera
