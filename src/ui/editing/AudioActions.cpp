#include "AudioActions.hpp"

#include "../../module/TesseraModule.hpp"

#include <app/CableWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <history.hpp>

namespace tessera::audioActions {

namespace {

/// The first of Rack's own Audio modules in the patch, or null.
rack::engine::Module* findAudioModule() {
    for (int64_t id : APP->engine->getModuleIds()) {
        rack::engine::Module* m = APP->engine->getModule(id);
        if (m && m->model && m->model->plugin && m->model->plugin->slug == "Core" &&
            m->model->slug.rfind("AudioInterface", 0) == 0)
            return m;
    }
    return nullptr;
}

/// Whether input `id` of `m` has a cable.
bool inputUsed(rack::engine::Module* m, int id) { return id < int(m->inputs.size()) && m->inputs[size_t(id)].isConnected(); }

/// Adds a cable from `out` of `from` to `in` of `to`, with its widget, and records it in `undo`.
void addCable(rack::engine::Module* from, int out, rack::engine::Module* to, int in, rack::history::ComplexAction* undo) {
    auto* cable = new rack::engine::Cable;
    cable->outputModule = from;
    cable->outputId = out;
    cable->inputModule = to;
    cable->inputId = in;
    APP->engine->addCable(cable);
    auto* cw = new rack::app::CableWidget;
    cw->setCable(cable);
    cw->color = APP->scene->rack->getNextCableColor();
    APP->scene->rack->addCable(cw);
    auto* h = new rack::history::CableAdd;
    h->setCable(cw);
    undo->push(h);
}

} // namespace

bool audioModuleInPatch() { return findAudioModule() != nullptr; }

std::string connectMainToAudio(TesseraModule* module) {
    rack::engine::Module* audio = findAudioModule();
    if (!module || !audio)
        return "Add an Audio module to the patch first: MAIN goes to its inputs 1 and 2";
    if (inputUsed(audio, 0) || inputUsed(audio, 1))
        return "The Audio module's inputs 1 and 2 are in use";
    auto* undo = new rack::history::ComplexAction;
    undo->name = "connect MAIN to Audio";
    addCable(module, TesseraModule::MAIN_L_OUTPUT, audio, 0, undo);
    addCable(module, TesseraModule::MAIN_R_OUTPUT, audio, 1, undo);
    APP->history->push(undo);
    return "MAIN is connected to the Audio module";
}

} // namespace tessera::audioActions
