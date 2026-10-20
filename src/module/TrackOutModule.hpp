#pragma once

#include "OutputModuleBase.hpp"

namespace tessera {

/// "Tessera Track Out": the outputs of one instrument track, placed anywhere in the patch.
class TrackOutModule : public OutputModuleBase {
  public:
    enum ParamId { PARAMS_LEN };
    enum InputId { INPUTS_LEN };
    enum OutputId {
        VOCT_OUTPUT,
        GATE_OUTPUT,
        VEL_OUTPUT,
        REL_OUTPUT,
        TIMBRE_OUTPUT,
        PRESS_OUTPUT,
        AUTO_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId { LIGHTS_LEN };

    TrackOutModule();

  protected:
    void writeFrame(const TrackFrame& frame) override;
    void silenceGates() override;
};

} // namespace tessera
