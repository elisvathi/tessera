#pragma once

#include "../model/Commands.hpp"
#include "../ui/EditorState.hpp"

#include <rack.hpp>

namespace tessera {

/// Puts a song Command on Rack's global undo stack, so Ctrl/Cmd+Z works for Tessera edits like any other.
/// It finds its Tessera by module ID each time, because undoing a module delete re-creates the module.
/// It also restores the note selection from before or after the edit (EDITOR_UX §2).
class CommandAction : public rack::history::ModuleAction {
  public:
    CommandAction(int64_t tesseraId, CommandPtr command, NoteSelection before, NoteSelection after)
        : command(std::move(command)), selectionBefore(std::move(before)), selectionAfter(std::move(after)) {
        moduleId = tesseraId;
        name = this->command->name();
    }

    void undo() override;
    void redo() override;

  private:
    CommandPtr command;
    NoteSelection selectionBefore;
    NoteSelection selectionAfter;
};

} // namespace tessera
