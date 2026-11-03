#include "TrackOutModule.hpp"

#include "TesseraModule.hpp"

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
        else if (role == Role::Out && (kind == BoundKind::Instrument || kind == BoundKind::Drum))
            why = "Nothing comes out here yet for a note track: OUT will carry what other tracks send to it";
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
    const char* outHelp = "The track's sound before its fader, for an effect: patch it out and the effect's output "
                          "back into IN. 10 V is full scale (0 dB)";
    out(OUT_L_OUTPUT, "OUT left", outHelp, Role::Out);
    out(OUT_R_OUTPUT, "OUT right", outHelp, Role::Out);
    const char* inHelp = "Audio mixed into the track, before its fader, at 0 dB. A poly cable is summed; with one "
                         "side patched, both sides hear it";
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
    for (const Route& r : routes) {
        outputs[r.output].setChannels(frame.channels);
        outputs[r.output].writeVoltages(r.values);
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
