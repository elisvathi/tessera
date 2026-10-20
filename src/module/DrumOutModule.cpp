#include "DrumOutModule.hpp"

namespace tessera {

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
    lightDivider.setDivision(kLightDivision);
}

void DrumOutModule::writeFrame(const TrackFrame& frame) {
    for (int i = 0; i < kTrigJacks; i++) {
        float gate = i < frame.channels ? frame.gate[i] : 0.f;
        outputs[TRIG_OUTPUT + i].setVoltage(gate);
        outputs[VEL_OUTPUT + i].setVoltage(i < frame.channels ? frame.vel[i] : 0.f);
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
    outputs[TRIG_POLY_OUTPUT].writeVoltages(frame.gate);
    outputs[VEL_POLY_OUTPUT].setChannels(frame.channels);
    outputs[VEL_POLY_OUTPUT].writeVoltages(frame.vel);
    outputs[AUTO_OUTPUT].setChannels(frame.autoChannels);
    outputs[AUTO_OUTPUT].writeVoltages(frame.automation);
}

void DrumOutModule::silenceGates() {
    for (int i = 0; i < kTrigJacks; i++)
        outputs[TRIG_OUTPUT + i].setVoltage(0.f);
    rack::engine::Output& poly = outputs[TRIG_POLY_OUTPUT];
    for (int c = 0; c < poly.getChannels(); c++)
        poly.setVoltage(0.f, c);
}

} // namespace tessera
