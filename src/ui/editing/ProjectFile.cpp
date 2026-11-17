#include "ProjectFile.hpp"
#include "ExportAudio.hpp"
#include "ImportMidi.hpp"

#include "../../audio/ProjectBundle.hpp"
#include "../../model/DawImport.hpp"
#include "../../model/Serialize.hpp"
#include "../../model/Zip.hpp"
#include "../../module/TesseraModule.hpp"
#include "../ModuleUndo.hpp"
#include "../interaction/Shortcuts.hpp"
#include "AudioActions.hpp"
#include "ExportMidi.hpp"

#include <osdialog.h>

#include <fstream>
#include <iterator>
#include <memory>

namespace tessera::projectFile {

namespace {

constexpr const char* kFilters = "Tessera project:tessera";
constexpr const char* kStillBusy = "Still working on the last file. Try again in a moment.";

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

/// The folder where a project saved before projects held their audio keeps it: "Song.tessera" has "Song audio".
std::string audioFolderOf(const std::string& projectPath) {
    return rack::system::join(rack::system::getDirectory(projectPath),
                              rack::system::getStem(projectPath) + " audio");
}

/// A progress report for a file job, sent only when the percentage changes.
std::function<void(float)> percentTicker(TesseraModule* m, const std::string& text) {
    auto shown = std::make_shared<int>(-1);
    return [m, text, shown](float progress) {
        int percent = int(progress * 100);
        if (percent != *shown) {
            *shown = percent;
            m->fileMessage = text + " " + std::to_string(percent) + "%";
        }
    };
}

/// Shared ownership of a JSON value that a file job needs after this call returns.
std::shared_ptr<json_t> holdJson(json_t* value) { return std::shared_ptr<json_t>(value, json_decref); }

} // namespace

void open(TesseraModule* m, Say say) {
    if (m->fileJobRunning()) {
        say(kStillBusy, true);
        return;
    }
    std::string path = pick(OSDIALOG_OPEN, m->projectPath);
    if (path.empty())
        return;
    std::string name = rack::system::getFilename(path);
    bool oneFile = bundle::isBundle(path);
    json_error_t error;
    json_t* loaded;
    if (oneFile) {
        std::string text;
        loaded = bundle::readProject(path, text) ? json_loads(text.c_str(), 0, &error) : nullptr;
        if (!loaded) {
            say(name + " is damaged, so Roll couldn't open it", true);
            return;
        }
    } else {
        loaded = json_load_file(path.c_str(), 0, &error);
        if (!loaded) {
            say("Couldn't read " + name + ": " + error.text, true);
            return;
        }
    }
    std::shared_ptr<json_t> project = holdJson(loaded);
    json_t* withSong = m->withProject(project.get(), path);
    if (!withSong) {
        say(name + " isn't a Tessera project", true);
        return;
    }
    std::shared_ptr<json_t> merged = holdJson(withSong);
    auto apply = [m, project, merged] {
        withModuleUndo(m, "open project", [&] {
            m->dataFromJson(merged.get());
            m->applyProjectTransport(project.get());
        });
    };

    std::vector<bundle::Wanted> wanted;
    if (oneFile) {
        int schema = int(json_integer_value(json_object_get(merged.get(), "schema")));
        Song incoming = songFromJson(json_object_get(merged.get(), "song"), schema > 0 ? schema : Song::kSchemaVersion);
        for (const AudioSource& s : incoming.audioSources)
            wanted.push_back({s.file, s.contentHash});
    }
    if (wanted.empty()) {
        apply();
        // A project from before projects held their audio: its files sit next to it, in "<name> audio".
        if (!oneFile && hasAudio(m->song())) {
            importFiles(m->song(), audioFolderOf(path), m->audioStorage());
            if (AudioStore* store = m->audioStore())
                store->retryMissing();
        }
        say("Opened " + name, false);
        return;
    }
    // The audio comes out first, so the song arrives with its files in place and never shows them as missing.
    std::string storage = m->audioStorage();
    m->startFileJob(
        [path, wanted, storage](std::atomic<float>& progress) {
            bundle::extract(path, wanted, storage, &progress);
            return true;
        },
        percentTicker(m, "Opening " + name + "…"),
        [m, apply, name](bool) {
            apply();
            if (AudioStore* store = m->audioStore())
                store->retryMissing();
            m->fileMessage = "Opened " + name;
        });
}

void save(TesseraModule* m, bool as, Say say) {
    if (m->fileJobRunning()) {
        say(kStillBusy, true);
        return;
    }
    std::string path = m->projectPath;
    if (as || path.empty()) {
        std::string suggested = path.empty() ? rack::system::join(rack::asset::user(""), defaultName(m)) : path;
        path = pick(OSDIALOG_SAVE, suggested);
        if (path.empty())
            return;
    }
    std::string name = rack::system::getFilename(path);
    json_t* project = m->projectToJson();
    char* dumped = json_dumps(project, JSON_INDENT(1) | JSON_REAL_PRECISION(9));
    json_decref(project);
    if (!dumped) {
        say("Couldn't write " + path, true);
        return;
    }
    std::string text = dumped;
    std::free(dumped);

    // A song without audio stays a plain text file, exactly as before.
    if (m->song().audioSources.empty()) {
        if (!bundle::writeText(path, text)) {
            say("Couldn't write " + path, true);
            return;
        }
        m->projectPath = path;
        say("Saved " + name, false);
        return;
    }

    std::string storage = m->audioStorage();
    std::vector<bundle::AudioEntry> audio;
    int missing = 0;
    for (const AudioSource& s : m->song().audioSources) {
        std::string file = rack::system::join(storage, s.file);
        if (rack::system::exists(file))
            audio.push_back({s.file, file});
        else
            missing++;
    }
    if (missing > 0) {
        std::string question = std::to_string(missing) + (missing == 1 ? " audio file is" : " audio files are") +
                               " missing and won't be in the project.\n\nSave anyway?";
        if (osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK_CANCEL, question.c_str()) != 1)
            return;
    }
    bool oldFolder = rack::system::isDirectory(audioFolderOf(path));
    std::string folderName = rack::system::getFilename(audioFolderOf(path));
    m->startFileJob(
        [path, text, audio](std::atomic<float>& progress) { return bundle::write(path, text, audio, &progress); },
        percentTicker(m, "Saving " + name + "…"),
        [m, path, name, oldFolder, folderName](bool ok) {
            if (!ok) {
                m->fileMessage.clear();
                std::string problem = "Couldn't save " + name + ". The disk may be full. Any older copy is unchanged.";
                osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, problem.c_str());
                return;
            }
            m->projectPath = path;
            m->fileMessage = "Saved " + name + " with its audio";
            if (oldFolder)
                m->fileMessage += ". The folder '" + folderName + "' is no longer needed";
        });
}

void importDawProject(TesseraModule* m, Say say) {
    if (m->fileJobRunning()) {
        say(kStillBusy, true);
        return;
    }
    osdialog_filters* filters = osdialog_filters_parse("DAWproject:dawproject");
    char* chosen = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters);
    osdialog_filters_free(filters);
    if (!chosen)
        return;
    std::string path = chosen;
    std::free(chosen);
    std::string name = rack::system::getFilename(path);
    // Unpacking may take a while: on the worker, into a folder beside the patch's audio. The song changes on the UI
    // thread once the files are there.
    struct Job {
        dawImport::Imported imported;
        std::map<uint64_t, std::string> files;
        std::string error;
        std::string folder;
    };
    auto job = std::make_shared<Job>();
    job->folder = rack::system::join(m->audioStorage(), "dawproject-import");
    uint64_t firstId = m->song().nextId;
    std::string projectFolder = rack::system::getDirectory(path);
    bool started = m->startFileJob(
        [job, path, firstId, projectFolder](std::atomic<float>& progress) {
            std::ifstream in(path, std::ios::binary);
            std::string archive((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::vector<zip::Entry> entries = zip::list(archive);
            auto find = [&](const std::string& n) -> const zip::Entry* {
                for (const zip::Entry& e : entries)
                    if (e.name == n)
                        return &e;
                return nullptr;
            };
            std::string project, meta;
            const zip::Entry* p = find("project.xml");
            if (!p || !zip::extract(archive, *p, project)) {
                job->error = entries.empty() ? "isn't a DAWproject file" : "is damaged";
                return false;
            }
            if (const zip::Entry* e = find("metadata.xml"))
                zip::extract(archive, *e, meta);
            if (!dawImport::readProject(project, meta, firstId, job->imported, &job->error))
                return false;
            rack::system::createDirectories(job->folder);
            const std::vector<dawImport::AudioFile>& audio = job->imported.audio;
            for (size_t i = 0; i < audio.size(); i++) {
                progress = float(i) / float(std::max<size_t>(1, audio.size()));
                const dawImport::AudioFile& f = audio[i];
                if (f.external) {
                    std::string onDisk = rack::system::isFile(f.path) ? f.path : rack::system::join(projectFolder, f.path);
                    if (rack::system::isFile(onDisk))
                        job->files[f.source.value] = onDisk;
                    continue;
                }
                const zip::Entry* e = find(f.path);
                std::string data;
                if (!e || !zip::extract(archive, *e, data))
                    continue;
                // A folder each, so two files of one name don't meet and the patch keeps the file's own name.
                std::string dir = rack::system::join(job->folder, std::to_string(i));
                rack::system::createDirectories(dir);
                std::string out = rack::system::join(dir, rack::system::getFilename(f.path));
                std::ofstream file(out, std::ios::binary);
                file.write(data.data(), std::streamsize(data.size()));
                if (file.good())
                    job->files[f.source.value] = out;
            }
            return true;
        },
        percentTicker(m, "Importing " + name + "…"),
        [m, job, name](bool ok) {
            if (!ok) {
                m->fileMessage = name + " " + (job->error.empty() ? "couldn't be read" : job->error);
                return;
            }
            m->applyDawProject(job->imported, job->files);
            rack::system::removeRecursively(job->folder); // the files are in the patch now
            const Song& song = m->song();
            m->fileMessage = "Imported " + name + ": " + std::to_string(song.tracks.size()) +
                             (song.tracks.size() == 1 ? " track" : " tracks") + "  ·  undo brings the old song back";
            if (!job->imported.leftOut.empty()) {
                std::string text = "Imported " + name + ". What didn't come across:\n";
                for (const std::string& line : job->imported.leftOut)
                    text += "\n• " + line;
                osdialog_message(OSDIALOG_INFO, OSDIALOG_OK, text.c_str());
            }
        });
    if (!started)
        say(kStillBusy, true);
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
    menu->addChild(rack::createMenuItem("Import DAWproject…", "replaces the song", [m, say] {
        if (confirmReplace(m, "Import a DAWproject?"))
            importDawProject(m, say);
    }));
    menu->addChild(rack::createMenuItem("Import audio…", shortcutLabel(Action::ImportAudio), [editor] {
        if (EditorContext* c = editor())
            audioActions::importDialog(*c);
    }));
    menu->addChild(rack::createMenuItem("Audio files…", "", [editor] {
        if (EditorContext* c = editor())
            audioActions::openAudioFiles(*c);
    }, m->song().audioSources.empty()));
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
