#include "CommandAction.hpp"

#include "TesseraModule.hpp"

namespace tessera {

namespace {

TesseraModule* findTessera(int64_t moduleId) { return dynamic_cast<TesseraModule*>(APP->engine->getModule(moduleId)); }

} // namespace

void CommandAction::undo() {
    if (TesseraModule* m = findTessera(moduleId)) {
        m->revertCommand(*command);
        m->editor().selection = selectionBefore;
    }
}

void CommandAction::redo() {
    if (TesseraModule* m = findTessera(moduleId)) {
        m->reapplyCommand(*command);
        m->editor().selection = selectionAfter;
    }
}

} // namespace tessera
