#pragma once

#include "OutputModuleBase.hpp"

namespace tessera {

/// "Tessera Auto Out": a track's automation lanes, one mono jack each, labelled with the lane's name on the panel.
/// Works for instrument and drum tracks. (Track Out and Drum Out also carry the lanes on their poly AUTO jack.)
class AutoOutModule : public OutputModuleBase {
  public:
    static constexpr int kLanes = 8;

    enum ParamId { PARAMS_LEN };
    enum InputId { INPUTS_LEN };
    enum OutputId { ENUMS(LANE_OUTPUT, kLanes), OUTPUTS_LEN };
    enum LightId { LIGHTS_LEN };

    AutoOutModule();

    /// Each lane's latest output in volts, for the panel's value bars (written on the audio thread).
    std::array<std::atomic<float>, kLanes> volts{};

  protected:
    void writeFrame(const TrackFrame& frame) override;
    void silenceGates() override {}
};

} // namespace tessera
