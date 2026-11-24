#include "TrackOutModule.hpp"

#include "TesseraModule.hpp"

#include <iterator>

namespace tessera {

namespace {

/// A jack's tooltip that says when it does nothing for the bound track, and why (§8.1).
struct TrackOutPortInfo : rack::engine::PortInfo {
    /// What the jack is for: notes, OUT or IN.
    enum class Role { Notes, Auto, Out, In } role = Role::Notes;

    std::string getDescription() override {
        auto* m = static_cast<TrackOutModule*>(module);
        BoundKind kind = m ? m->boundKind() : BoundKind::None;
        std::string text = PortInfo::getDescription();
        std::string why;
        if (role == Role::Notes && (kind == BoundKind::Audio || kind == BoundKind::Master))
            why = kind == BoundKind::Audio ? "Nothing comes out here for an audio track"
                                           : "Nothing comes out here for the master";
        else if ((role == Role::Out || role == Role::In) && m && m->binding().followSelected)
            why = "Off while Track Out follows the track selected in the editor";
        return why.empty() ? text : why + ". " + text;
    }
};

} // namespace

TrackOutModule::TrackOutModule() : OutputModuleBase(std::nullopt, true) {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    using Role = TrackOutPortInfo::Role;
    auto out = [this](int id, const char* name, const char* help, Role role) {
        auto* info = configOutput<TrackOutPortInfo>(id, name);
        info->role = role;
        if (help)
            info->description = help;
    };
    out(VOCT_OUTPUT, "Pitch (1V/oct)", nullptr, Role::Notes);
    out(GATE_OUTPUT, "Gate", nullptr, Role::Notes);
    out(VEL_OUTPUT, "Velocity", "0-10 V", Role::Notes);
    out(REL_OUTPUT, "Release velocity", "0-10 V, held after the gate falls", Role::Notes);
    out(TIMBRE_OUTPUT, "Timbre", "Per-note timbre expression, 0-10 V", Role::Notes);
    out(PRESS_OUTPUT, "Pressure", "Per-note pressure expression, 0-10 V", Role::Notes);
    out(AUTO_OUTPUT, "Automation", "One channel per automation lane", Role::Auto);
    const char* outHelp = "The track's sound up to this Track Out, for an effect: patch it into the effect and the "
                          "effect's output back into IN. What comes back then replaces the sound, blended by Mix in "
                          "the mixer. 10 V is full scale (0 dB)";
    out(OUT_L_OUTPUT, "OUT left", outHelp, Role::Out);
    out(OUT_R_OUTPUT, "OUT right", outHelp, Role::Out);
    const char* inHelp = "Audio into the track, before its fader. With nothing on OUT it is added at 0 dB; with OUT "
                         "patched it is the effect coming back. A poly cable is summed; with one side patched, both "
                         "sides hear it";
    for (int id : {IN_L_INPUT, IN_R_INPUT}) {
        auto* info = configInput<TrackOutPortInfo>(id, id == IN_L_INPUT ? "IN left" : "IN right");
        info->role = Role::In;
        info->description = inHelp;
    }
    configLight(IN_LIGHT, "IN level")->description = "Green while audio comes back on IN, red above 0 dB";
    AudioPorts ports;
    ports.outL = OUT_L_OUTPUT;
    ports.outR = OUT_R_OUTPUT;
    ports.inL = IN_L_INPUT;
    ports.inR = IN_R_INPUT;
    ports.inLight = IN_LIGHT;
    setAudioPorts(ports);
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
    if (channel == 0 && !frame.byChannel && frame.channels <= kMaxVoices) {
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
    // Without playheads, one channel: every voice keeps its place, the other channels' gates stay low.
    bool masked = channel != 0 && !frame.byChannel;
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
