#include "ProjectFile.hpp"
#include "ExportAudio.hpp"
#include "ImportMidi.hpp"

#include "../../module/TesseraModule.hpp"
#include "../ModuleUndo.hpp"
#include "../interaction/Shortcuts.hpp"
#include "AudioActions.hpp"
#include "ExportMidi.hpp"

#include <osdialog.h>

namespace tessera::projectFile {

namespace {

constexpr const char* kFilters = "Tessera project:tessera";

/// The path the user picks, ending in ".tessera"; empty when cancelled.
std::string pick(osdialog_file_action action, const std::string& suggested) {
    osdialog_filters* filters = osdialog_filters_parse(kFilters);
    std::string dir = suggested.empty() ? "" : rack::system::getDirectory(suggested);
    std::string name = suggested.empty() ? "" : rack::system::getFilename(suggested);
    char* chosen =
        osdialog_file(action, dir.empty() ? nullptr : dir.c_str(), name.empty() ? nullptr : name.c_str(), filters);
    osdialog_filters_free(filters);
    std::string path = chosen ? chosen : "";
    std::free(chosen);
    if (action == OSDIALOG_SAVE && !path.empty() && rack::system::getExtension(path) != ".tessera")
        path += ".tessera";
    return path;
}

std::string defaultName(TesseraModule* m) {
    std::string name = m->instanceName().empty() ? m->model->name : m->instanceName();
    for (char& ch : name)
        if (!std::isalnum((unsigned char)ch) && ch != ' ' && ch != '-' && ch != '_')
            ch = '_';
    return name + ".tessera";
}

/// The folder beside a project file that holds its audio: "Song.tessera" keeps it in "Song audio".
std::string audioFolderOf(const std::string& projectPath) {
    return rack::system::join(rack::system::getDirectory(projectPath),
                              rack::system::getStem(projectPath) + " audio");
}

} // namespace

void open(TesseraModule* m, Say say) {
    std::string path = pick(OSDIALOG_OPEN, m->projectPath);
    if (path.empty())
        return;
    json_error_t error;
    json_t* project = json_load_file(path.c_str(), 0, &error);
    if (!project) {
        say("Couldn't read " + rack::system::getFilename(path) + ": " + error.text, true);
        return;
    }
    json_t* merged = m->withProject(project, path);
    if (!merged) {
        json_decref(project);
        say(rack::system::getFilename(path) + " isn't a Tessera project", true);
        return;
    }
    withModuleUndo(m, "open project", [&] {
        m->dataFromJson(merged);
        m->applyProjectTransport(project);
    });
    // Its audio files sit next to it, in "<name> audio".
    if (hasAudio(m->song())) {
        importFiles(m->song(), audioFolderOf(path), m->audioStorage());
        if (AudioStore* store = m->audioStore())
            store->retryMissing();
    }
    json_decref(merged);
    json_decref(project);
    say("Opened " + rack::system::getFilename(path), false);
}

void save(TesseraModule* m, bool as, Say say) {
    std::string path = m->projectPath;
    if (as || path.empty()) {
        std::string suggested = path.empty() ? rack::system::join(rack::asset::user(""), defaultName(m)) : path;
        path = pick(OSDIALOG_SAVE, suggested);
        if (path.empty())
            return;
    }
    json_t* project = m->projectToJson();
    int failed = json_dump_file(project, path.c_str(), JSON_INDENT(1) | JSON_REAL_PRECISION(9));
    json_decref(project);
    if (failed) {
        say("Couldn't write " + path, true);
        return;
    }
    m->projectPath = path;
    if (hasAudio(m->song()))
        exportFiles(m->song(), m->audioStorage(), audioFolderOf(path));
    say("Saved " + rack::system::getFilename(path), false);
}

void newSong(TesseraModule* m, Say say) {
    withModuleUndo(m, "new song", [&] {
        // Through the engine, which pauses the audio thread while the module resets (Rack's Initialize does this).
        APP->engine->resetModule(m);
        m->projectPath.clear();
    });
    say("New song  ·  undo brings the old one back", false);
}

/// Whether to go on replacing the song: at once for the untouched default, else after an OK in a dialog, as Rack
/// asks before it replaces a patch.
bool confirmReplace(TesseraModule* m, const char* question) {
    if (m->song() == makeDefaultSong())
        return true;
    std::string text = std::string(question) + "\n\nThe current song is replaced. Undo (Ctrl+Z) brings it back.";
    return osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK_CANCEL, text.c_str()) == 1;
}

void appendFileMenu(rack::ui::Menu* menu, TesseraModule* m, std::function<EditorContext*()> editor, Say say) {
    menu->addChild(rack::createMenuItem("New song", "", [m, say] {
        if (confirmReplace(m, "Start a new song?"))
            newSong(m, say);
    }));
    menu->addChild(rack::createMenuItem("Open project…", "", [m, say] {
        if (confirmReplace(m, "Open a project?"))
            open(m, say);
    }));
    std::string file = m->projectPath.empty() ? "" : rack::system::getFilename(m->projectPath);
    menu->addChild(rack::createMenuItem("Save project", file, [m, say] { save(m, false, say); }));
    menu->addChild(rack::createMenuItem("Save project as…", "", [m, say] { save(m, true, say); }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(
        rack::createMenuItem("Import MIDI file as tracks…", "", [m, say] { importMidi::asTracks(m, say); }));
    menu->addChild(rack::createMenuItem("Import audio…", shortcutLabel(Action::ImportAudio), [editor] {
        if (EditorContext* c = editor())
            audioActions::importDialog(*c);
    }));
    menu->addChild(rack::createMenuItem("Remove unused audio", "", [editor] {
        if (EditorContext* c = editor())
            audioActions::removeUnused(*c);
    }, unusedSources(m->song()).empty()));
    menu->addChild(rack::createMenuItem("Export MIDI…", shortcutLabel(Action::ExportMidi), [editor] {
        if (EditorContext* c = editor())
            exportMidi::openSong(*c);
    }));
    menu->addChild(rack::createMenuItem("Export audio…", shortcutLabel(Action::ExportAudio), [editor] {
        if (EditorContext* c = editor())
            exportAudio::openDialog(*c);
    }, !m->makesSound()));
    menu->addChild(rack::createMenuItem("Export DAWproject…", "For other DAWs", [editor] {
        if (EditorContext* c = editor())
            exportMidi::openDawProject(*c);
    }));
}

} // namespace tessera::projectFile
