#pragma once

#include "OutputModuleBase.hpp"

namespace tessera {

/// "Tessera Drum Out": the outputs of one drum track, placed anywhere in the patch (DESIGN.md §6).
/// Lane outputs 1-8 each have a trigger, velocity, pitch, timbre and pressure jack; all 16 lanes are also on the
/// poly outputs, channel = lane output. Pitch is the hit's tuning away from normal (1 V per octave, 0 V = none);
/// timbre and pressure are 0-10 V. Each follows the hit's value or curve and holds until the lane's next hit.
/// Audio: each lane output has a mono IN for its drum module; the kit's OUT L/R and IN L/R make an effect loop for
/// the whole kit, like Track Out's.
class DrumOutModule : public OutputModuleBase {
  public:
    static constexpr int kTrigJacks = 8;

    enum ParamId { PARAMS_LEN };
    // Each lane's mono sound (its drum module's output), mixed into the drum track and its OUT; the kit IN brings an
    // effect on the whole kit back, or a stereo drum machine (docs/design/AUDIO_MIXER.md §8.1).
    enum InputId { ENUMS(LANE_IN_INPUT, kTrigJacks), KIT_IN_L_INPUT, KIT_IN_R_INPUT, INPUTS_LEN };
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
        KIT_OUT_L_OUTPUT, // the kit's sound before the drum track's fader: every lane IN mixed
        KIT_OUT_R_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId { ENUMS(TRIG_LIGHT, kTrigJacks), ENUMS(KIT_IN_LIGHT, 2), LIGHTS_LEN }; // the IN light: green, red

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
