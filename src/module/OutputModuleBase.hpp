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

/// Shared base for Track Out, Drum Out and Auto Out: binding, registry membership and safe frame reading.
///
/// The binding resolves on the UI thread (refreshBinding, called from the widget's step). process() only
/// reads atomics and the bus. Subclasses just copy a TrackFrame to their ports.
class OutputModuleBase : public rack::engine::Module {
  public:
    /// `acceptedType` empty: any track (Auto Out).
    explicit OutputModuleBase(std::optional<TrackType> acceptedType) : accepted(acceptedType) {}
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
    /// Tracks of the accepted type in display order.
    std::vector<TrackId> candidateTracks(const TesseraModule& source) const;

  protected:
    // Audio thread
    virtual void writeFrame(const TrackFrame& frame) = 0;
    /// Hold every output except gates, which go low.
    virtual void silenceGates() = 0;

  private:
    const std::optional<TrackType> accepted;
    OutputBinding bindingData;
    bool needsDefaultBinding = true;
    // Set when the module arrives from JSON (patch load, paste, duplicate), so the first refresh can check
    // whether its Tessera was pasted alongside it.
    bool checkPaste = false;
    double addedFrameTime = 0;

    AudioHandoff<const OutputBus> bus;
    std::atomic<int> busSlot{-1};
    std::atomic<uint64_t> boundTrack{0};
};

} // namespace tessera
