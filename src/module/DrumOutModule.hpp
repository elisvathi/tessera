#pragma once

#include "OutputModuleBase.hpp"

namespace tessera {

/// "Drum Out": the outputs of one drum track, placed anywhere in the patch (DESIGN.md §6).
/// Lane outputs 1-8 each have a trigger, velocity, pitch, timbre and pressure jack; all 16 lanes are also on the
/// poly outputs, channel = lane output. Pitch is the hit's tuning away from normal (1 V per octave, 0 V = none);
/// timbre and pressure are 0-10 V. Each follows the hit's value or curve and holds until the lane's next hit.
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
        ENUMS(TIMBRE_OUTPUT, kTrigJacks),
        ENUMS(PRESS_OUTPUT, kTrigJacks),
        PITCH_POLY_OUTPUT,
        ENUMS(PITCH_OUTPUT, kTrigJacks),
        OUTPUTS_LEN
    };
    enum LightId { ENUMS(TRIG_LIGHT, kTrigJacks), LIGHTS_LEN };

    DrumOutModule();

    // Jack descriptions, shared by the per-lane and poly jacks (the widget renames the lane jacks after lanes).
    static const char* const kPitchHelp;
    static const char* const kTimbreHelp;
    static const char* const kPressHelp;

  protected:
    void writeFrame(const TrackFrame& frame) override;
    void silenceGates() override;

  private:
    static constexpr int kLightDivision = 64;
    rack::dsp::ClockDivider lightDivider;
    uint8_t hits = 0; // lanes that fired since the lights were last updated
};

} // namespace tessera
