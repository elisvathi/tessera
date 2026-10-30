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
    if (channel == 0 && frame.channels <= kMaxVoices) {
        // The usual case: every voice as it is, copied in one go.
        for (size_t i = 0; i < std::size(routes); i++) {
            outputs[routes[i].output].setChannels(frame.channels);
            outputs[routes[i].output].writeVoltages(routes[i].values);
        }
        outputs[AUTO_OUTPUT].setChannels(frame.autoChannels);
        outputs[AUTO_OUTPUT].writeVoltages(frame.automation);
        return;
    }
    std::array<int8_t, kMaxVoices> voices;
    int count = picker.pick(frame, channel, voices);
    // Up to 16 voices, one channel: every voice keeps its place, the other channels' gates stay low.
    bool masked = channel != 0 && frame.channels <= kMaxVoices;
    for (size_t i = 0; i < std::size(routes); i++) {
        const Route& r = routes[i];
        bool gate = r.output == GATE_OUTPUT;
        outputs[r.output].setChannels(count);
        for (int c = 0; c < count; c++) {
            int v = voices[size_t(c)];
            if (v >= 0 && (!masked || frame.part[v] == channel))
                held[i][c] = r.values[v];
            else if (gate)
                held[i][c] = 0; // nothing sounding here: pitch and the rest hold for a release tail
            outputs[r.output].setVoltage(held[i][c], c);
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
