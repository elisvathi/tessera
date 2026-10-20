#pragma once

#include "../EditorContext.hpp"

#include <rack.hpp>

#include <functional>

namespace tessera {

class TesseraModule;

/// Project files (.tessera) and the File menu (docs/DECISIONS.md §39): open and save the song with this Tessera's
/// settings and editor layout, and the exports. The editor's File button and the module's right-click menu show
/// the same items.
namespace projectFile {

/// Tells the user how it went; `problem` when it didn't work.
using Say = std::function<void(const std::string& text, bool problem)>;

/// Starts over: the song a new Tessera starts with, the knobs at their defaults, no project file. One undo step.
void newSong(TesseraModule* m, Say say);
/// Asks for a .tessera file and opens it in place of the song, as one undo step.
void open(TesseraModule* m, Say say);
/// Writes the project to its file, or asks where first (`as`, or when it has none yet).
void save(TesseraModule* m, bool as, Say say);

/// New song, Open, Save, Save as, Import MIDI file as tracks, Export MIDI, Export DAWproject. `editor` gives the editor the export dialogs open in
/// (opening it when needed).
void appendFileMenu(rack::ui::Menu* menu, TesseraModule* m, std::function<EditorContext*()> editor, Say say);

} // namespace projectFile
} // namespace tessera
