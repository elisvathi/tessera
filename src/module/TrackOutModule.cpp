#include "TrackOutModule.hpp"

#include <iterator>

namespace tessera {

TrackOutModule::TrackOutModule() : OutputModuleBase(TrackType::Instrument) {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    configOutput(VOCT_OUTPUT, "Pitch (1V/oct)");
    configOutput(GATE_OUTPUT, "Gate");
    configOutput(VEL_OUTPUT, "Velocity")->description = "0-10 V";
    configOutput(REL_OUTPUT, "Release velocity")->description = "0-10 V, held after the gate falls";
    configOutput(TIMBRE_OUTPUT, "Timbre")->description = "Per-note timbre expression, 0-10 V";
    configOutput(PRESS_OUTPUT, "Pressure")->description = "Per-note pressure expression, 0-10 V";
    configOutput(AUTO_OUTPUT, "Automation")->description = "One channel per automation lane";
}

void TrackOutModule::writeFrame(const TrackFrame& frame) {
    struct Route {
        OutputId output;
        const float* values;
    };
    const Route routes[] = {
        {VOCT_OUTPUT, frame.voct}, {GATE_OUTPUT, frame.gate},     {VEL_OUTPUT, frame.vel},
        {REL_OUTPUT, frame.rel},   {TIMBRE_OUTPUT, frame.timbre}, {PRESS_OUTPUT, frame.press},
    };
    int channel = part();
    for (size_t i = 0; i < std::size(routes); i++) {
        const Route& r = routes[i];
        outputs[r.output].setChannels(frame.channels);
        if (channel == 0) {
            outputs[r.output].writeVoltages(r.values);
            continue;
        }
        for (int v = 0; v < frame.channels; v++) {
            if (frame.part[v] == channel)
                held[i][v] = r.values[v];
            else if (r.output == GATE_OUTPUT)
                held[i][v] = 0;
            outputs[r.output].setVoltage(held[i][v], v);
        }
    }
    outputs[AUTO_OUTPUT].setChannels(frame.autoChannels);
    outputs[AUTO_OUTPUT].writeVoltages(frame.automation);
}

void TrackOutModule::silenceGates() {
    rack::engine::Output& gate = outputs[GATE_OUTPUT];
    for (int c = 0; c < gate.getChannels(); c++)
        gate.setVoltage(0.f, c);
}

} // namespace tessera
