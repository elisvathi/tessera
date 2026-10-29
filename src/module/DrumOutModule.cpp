#include "DrumOutModule.hpp"

namespace tessera {

const char* const DrumOutModule::kPitchHelp =
    "The hit's tuning away from normal, 1 V per octave: 0 V when the hit has no pitch. Holds until the next hit";
const char* const DrumOutModule::kTimbreHelp =
    "The hit's timbre, 0-10 V: 0 V when it has none. Holds until the next hit";
const char* const DrumOutModule::kPressHelp =
    "The hit's pressure, 0-10 V: 0 V when it has none. Holds until the next hit";

DrumOutModule::DrumOutModule() : OutputModuleBase(TrackType::Drum) {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    for (int i = 0; i < kTrigJacks; i++) {
        configOutput(TRIG_OUTPUT + i, "Lane " + std::to_string(i + 1))->description =
            "Trigger (or gate) of the lane on output " + std::to_string(i + 1);
        configOutput(VEL_OUTPUT + i, "Lane " + std::to_string(i + 1) + " velocity")->description =
            "0-10 V, held from one hit to the next";
        configLight(TRIG_LIGHT + i, "Lane " + std::to_string(i + 1));
    }
    configOutput(TRIG_POLY_OUTPUT, "All lanes")->description = "Every lane's trigger, one channel per lane output";
    configOutput(VEL_POLY_OUTPUT, "Velocity")->description = "0-10 V, one channel per lane output";
    configOutput(AUTO_OUTPUT, "Automation")->description = "One channel per automation lane";
    configOutput(PITCH_POLY_OUTPUT, "Pitch")->description = kPitchHelp;
    configOutput(TIMBRE_POLY_OUTPUT, "Timbre")->description = kTimbreHelp;
    configOutput(PRESS_POLY_OUTPUT, "Pressure")->description = kPressHelp;
    for (int i = 0; i < kTrigJacks; i++) {
        std::string lane = "Lane " + std::to_string(i + 1);
        configOutput(PITCH_OUTPUT + i, lane + " pitch")->description = kPitchHelp;
        configOutput(TIMBRE_OUTPUT + i, lane + " timbre")->description = kTimbreHelp;
        configOutput(PRESS_OUTPUT + i, lane + " pressure")->description = kPressHelp;
    }
    lightDivider.setDivision(kLightDivision);
}

void DrumOutModule::writeFrame(const TrackFrame& frame) {
    // With a channel picked, a lane sends only hits on that channel; its velocity, pitch, timbre and pressure hold
    // the last of them.
    int channel = part();
    float gates[kMaxVoices], vels[kMaxVoices], tunes[kMaxVoices], timbres[kMaxVoices], presses[kMaxVoices];
    for (int c = 0; c < frame.channels && c < kMaxVoices; c++) {
        bool ours = channel == 0 || frame.part[c] == channel;
        gates[c] = ours ? frame.gate[c] : 0.f;
        if (ours) {
            heldVel[c] = frame.vel[c];
            heldTune[c] = frame.tune[c];
            heldTimbre[c] = frame.timbre[c];
            heldPress[c] = frame.press[c];
        }
        vels[c] = heldVel[c];
        tunes[c] = heldTune[c];
        timbres[c] = heldTimbre[c];
        presses[c] = heldPress[c];
    }
    for (int i = 0; i < kTrigJacks; i++) {
        float gate = i < frame.channels ? gates[i] : 0.f;
        outputs[TRIG_OUTPUT + i].setVoltage(gate);
        outputs[VEL_OUTPUT + i].setVoltage(i < frame.channels ? vels[i] : 0.f);
        outputs[PITCH_OUTPUT + i].setVoltage(i < frame.channels ? tunes[i] : 0.f);
        outputs[TIMBRE_OUTPUT + i].setVoltage(i < frame.channels ? timbres[i] : 0.f);
        outputs[PRESS_OUTPUT + i].setVoltage(i < frame.channels ? presses[i] : 0.f);
        if (gate > 0)
            hits |= uint8_t(1 << i);
    }
    // Lights at control rate. `hits` remembers a 1 ms trigger between updates, and the light fades slowly so a hit
    // can be seen.
    if (lightDivider.process()) {
        float dt = APP->engine->getSampleTime() * kLightDivision;
        for (int i = 0; i < kTrigJacks; i++)
            lights[TRIG_LIGHT + i].setBrightnessSmooth((hits >> i) & 1 ? 1.f : 0.f, dt);
        hits = 0;
    }
    outputs[TRIG_POLY_OUTPUT].setChannels(frame.channels);
    outputs[TRIG_POLY_OUTPUT].writeVoltages(gates);
    outputs[VEL_POLY_OUTPUT].setChannels(frame.channels);
    outputs[VEL_POLY_OUTPUT].writeVoltages(vels);
    outputs[AUTO_OUTPUT].setChannels(frame.autoChannels);
    outputs[AUTO_OUTPUT].writeVoltages(frame.automation);
    outputs[PITCH_POLY_OUTPUT].setChannels(frame.channels);
    outputs[PITCH_POLY_OUTPUT].writeVoltages(tunes);
    outputs[TIMBRE_POLY_OUTPUT].setChannels(frame.channels);
    outputs[TIMBRE_POLY_OUTPUT].writeVoltages(timbres);
    outputs[PRESS_POLY_OUTPUT].setChannels(frame.channels);
    outputs[PRESS_POLY_OUTPUT].writeVoltages(presses);
}

void DrumOutModule::silenceGates() {
    for (int i = 0; i < kTrigJacks; i++)
        outputs[TRIG_OUTPUT + i].setVoltage(0.f);
    rack::engine::Output& poly = outputs[TRIG_POLY_OUTPUT];
    for (int c = 0; c < poly.getChannels(); c++)
        poly.setVoltage(0.f, c);
}

} // namespace tessera
