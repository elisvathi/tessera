#pragma once

#include "OutputModuleBase.hpp"

namespace tessera {

/// "Tessera Drum Out": the outputs of one drum track, placed anywhere in the patch (DESIGN.md §6).
/// Lane outputs 1-8 each have a trigger and a velocity jack; all 16 are on the poly outputs, channel = lane
/// output. Timbre and pressure play each hit's expression curves (0-10 V); pitch bend adds onto the velocity
/// jacks' pitch, as on the trigger poly channels.
class DrumOutModule : public OutputModuleBase {
  public:
    static constexpr int kTrigJacks = 8;

    enum ParamId { PARAMS_LEN };
    enum InputId { INPUTS_LEN };
    // New outputs go last: patches store cables by output ID.
    enum OutputId {
        ENUMS(TRIG_OUTPUT, kTrigJacks),
        TRIG_POLY_OUTPUT,
        VEL_POLY_OUTPUT,
        AUTO_OUTPUT,
        ENUMS(VEL_OUTPUT, kTrigJacks),
        TIMBRE_POLY_OUTPUT,
        PRESS_POLY_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId { ENUMS(TRIG_LIGHT, kTrigJacks), LIGHTS_LEN };

    DrumOutModule();

  protected:
    void writeFrame(const TrackFrame& frame) override;
    void silenceGates() override;

  private:
    static constexpr int kLightDivision = 64;
    rack::dsp::ClockDivider lightDivider;
    uint8_t hits = 0; // lanes that fired since the lights were last updated
};

} // namespace tessera
