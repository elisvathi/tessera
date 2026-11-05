#pragma once

#include "../engine/VoicePicker.hpp"
#include "OutputModuleBase.hpp"

namespace tessera {

/// "Tessera Track Out": one track's jacks, placed anywhere in the patch. It binds to any track or the master: notes
/// and automation come out of the note jacks; OUT carries the track's sound before its fader (for an effect) and IN
/// brings audio back into the track (docs/design/AUDIO_MIXER.md §8.1). Jacks that do nothing for the bound track are
/// drawn dim.
class TrackOutModule : public OutputModuleBase {
  public:
    enum ParamId { PARAMS_LEN };
    enum InputId { IN_L_INPUT, IN_R_INPUT, INPUTS_LEN };
    // New outputs go last: patches store cables by output ID.
    enum OutputId {
        VOCT_OUTPUT,
        GATE_OUTPUT,
        VEL_OUTPUT,
        REL_OUTPUT,
        TIMBRE_OUTPUT,
        PRESS_OUTPUT,
        AUTO_OUTPUT,
        OUT_L_OUTPUT,
        OUT_R_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId { ENUMS(IN_LIGHT, 2), LIGHTS_LEN }; // green, red

    TrackOutModule();

  protected:
    void writeFrame(const TrackFrame& frame) override;
    void silenceGates() override;

  private:
    // With a channel picked, each voice holds what it last played on that channel while another channel uses it,
    // so its pitch doesn't jump during the release.
    float held[6][kMaxVoices] = {};
    VoicePicker picker; // which of the track's voices go out on which channel, when it has more than 16
};

} // namespace tessera
