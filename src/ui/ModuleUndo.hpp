#pragma once

#include <rack.hpp>

#include <string>

namespace tessera {

/// Runs `change` as one undo step named `name`, by recording the module's JSON before and after.
/// Suits module settings (names, bindings, menu options). Song edits use the command layer instead.
template <typename F>
void withModuleUndo(rack::engine::Module* module, const std::string& name, F change) {
    json_t* before = module->toJson();
    change();
    json_t* after = module->toJson();
    if (json_equal(before, after)) {
        json_decref(before);
        json_decref(after);
        return;
    }
    auto* action = new rack::history::ModuleChange;
    action->name = name;
    action->moduleId = module->id;
    action->oldModuleJ = before;
    action->newModuleJ = after;
    APP->history->push(action);
}

} // namespace tessera
