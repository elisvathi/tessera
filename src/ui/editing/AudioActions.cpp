#include "AudioActions.hpp"

#include "../../module/TesseraModule.hpp"
#include "../../engine/AudioSnapshotBuilder.hpp"
#include "../EditorContext.hpp"
#include "../views/AudioClipDraw.hpp"
#include "../widgets/MenuTextField.hpp"

#include <app/CableWidget.hpp>
#include <app/RackWidget.hpp>
#include <app/Scene.hpp>
#include <history.hpp>
#include <osdialog.h>

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

void appendAudioClipMenu(rack::ui::Menu* menu, EditorContext& ctx, ClipId clipId) {
    TesseraModule* m = ctx.module();
    const Clip* clip = m ? m->song().findClip(clipId) : nullptr;
    if (!clip || !clip->audio)
        return;
    EditorContext* c = &ctx;
    int uses = m->song().clipUses(clipId);
    std::string all = uses > 1 ? " (all " + std::to_string(uses) + " copies)" : "";
    auto edit = [c, clipId](const std::string& name, std::function<void(AudioContent&)> change) {
        TesseraModule* mod = c->module();
        mod->perform(editAudio(mod->song(), clipId, change, name));
    };
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Audio" + all));

    static const char* const kFollow[] = {"Stretch: keeps its pitch", "Repitch: faster and higher, like tape",
                                          "Off: its own speed and pitch"};
    menu->addChild(rack::createSubmenuItem("Follow tempo", "", [c, clipId, edit](rack::ui::Menu* sub) {
        for (int i = 0; i < 3; i++) {
            const Clip* cl = c->module()->song().findClip(clipId);
            bool on = cl && cl->audio && int(cl->audio->follow) == i;
            sub->addChild(rack::createCheckMenuItem(kFollow[i], "", [on] { return on; }, [edit, i] {
                edit("Follow tempo", [i](AudioContent& a) { a.follow = TempoFollow(i); });
            }));
        }
    }));
    static const SoundType kSounds[] = {SoundType::Drums, SoundType::Melody, SoundType::Voice, SoundType::FullMix};
    static const char* const kSoundNames[] = {"Drums: keeps attacks sharp", "Melody: smooth, for one instrument",
                                              "Voice: keeps a voice's character", "Full mix: a balance"};
    menu->addChild(rack::createSubmenuItem("Sound type", "", [c, clipId, edit](rack::ui::Menu* sub) {
        for (int i = 0; i < 4; i++) {
            const Clip* cl = c->module()->song().findClip(clipId);
            bool on = cl && cl->audio && cl->audio->sound == kSounds[i];
            SoundType s = kSounds[i];
            sub->addChild(rack::createCheckMenuItem(kSoundNames[i], "", [on] { return on; }, [edit, s] {
                edit("Sound type", [s](AudioContent& a) { a.sound = s; });
            }));
        }
    }));

    double tempo = clip->audio->sourceTempo;
    auto setTempo = [c, clipId](double bpm) {
        TesseraModule* mod = c->module();
        mod->perform(setClipTempo(mod->song(), clipId, bpm));
    };
    char label[48];
    snprintf(label, sizeof label, "%.4g BPM", tempo);
    menu->addChild(rack::createSubmenuItem("Clip tempo", label, [tempo, setTempo, m, clip](rack::ui::Menu* sub) {
        const AudioSource* src = m->song().sourceOf(*clip);
        AudioStore* store = m->audioStore();
        double guess = src && store ? store->status(src->id).guess.bpm : 0;
        auto item = [&](double bpm, const std::string& why) {
            char t[48];
            snprintf(t, sizeof t, "%.4g BPM", bpm);
            sub->addChild(rack::createCheckMenuItem(t, why, [tempo, bpm] { return std::abs(tempo - bpm) < 1e-6; },
                                                    [setTempo, bpm] { setTempo(bpm); }));
        };
        if (guess > 0)
            item(guess, "the guess");
        item(tempo * 2, "\u00d72");
        item(tempo / 2, "\u00f72");
        sub->addChild(new rack::ui::MenuSeparator);
        sub->addChild(rack::createMenuLabel("Type a tempo (Enter)"));
        char now[24];
        snprintf(now, sizeof now, "%.4g", tempo);
        sub->addChild(new MenuTextField(now, [setTempo](const std::string& text) {
            double bpm = std::atof(text.c_str());
            if (bpm > 0)
                setTempo(bpm);
        }));
    }));
    if (clip->audio->gainDb != 0)
        menu->addChild(rack::createMenuItem("Gain back to 0 dB", audioDraw::dbText(clip->audio->gainDb), [edit] {
            edit("Clip gain 0 dB", [](AudioContent& a) { a.gainDb = 0; });
        }));
    // A missing file: point at it, and it is copied into the patch.
    if (const AudioSource* src = m->song().sourceOf(*clip))
        if (m->audioStore() && m->audioStore()->status(src->id).state == SourceState::Missing) {
            AudioSourceId id = src->id;
            menu->addChild(rack::createMenuItem("Find file…", src->name, [c, id] { findFile(*c, id); }));
        }
    RenderKey key;
    double songTempo = m->tempo() > 0 ? m->tempo() : 120;
    if (m->audioStore() && wantsRender(*clip->audio, songTempo, key) &&
        m->audioStore()->renderStatus(key).state == RenderState::Failed)
        menu->addChild(rack::createMenuItem("Try again", "make the stretched version",
                                            [m, key] { m->audioStore()->retry(key); }));
}

void importDialog(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    osdialog_filters* filters = osdialog_filters_parse("Audio:wav,wave,aif,aiff,aifc,flac,mp3");
    char* chosen = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters);
    osdialog_filters_free(filters);
    if (!chosen)
        return;
    std::string path = chosen;
    std::free(chosen);
    const Song& song = m->song();
    AudioDropTarget target;
    target.kind = AudioDropTarget::Kind::NewTracks;
    target.index = int(song.tracks.size());
    target.timeline = m->playMode() == PlayMode::Arranger;
    Tick bar = ticksPerBar(song.timeSig);
    target.at = Tick(m->playheadTicks() / double(bar)) * bar;
    dropAudio(ctx, {path}, target);
}

void removeUnused(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    size_t n = unusedSources(m->song()).size();
    if (n == 0) {
        ctx.toast("Every audio file is in use");
        return;
    }
    m->perform(removeUnusedAudio(m->song()));
    ctx.toast("Removed " + std::to_string(n) + (n == 1 ? " audio file" : " audio files") +
              ". The files go when the patch is next loaded; undo brings them back until then.");
}

std::string dropMessage(const std::vector<std::string>& imported, const std::vector<std::string>& failed,
                        int skipped) {
    std::string text;
    if (imported.size() == 1)
        text = "Imported " + imported[0];
    else if (!imported.empty())
        text = "Imported " + std::to_string(imported.size()) + " files";
    if (!failed.empty()) {
        if (!text.empty())
            text += ". ";
        text += "Couldn't read '" + failed[0] + "'" + (failed.size() > 1 ? " and others" : "") +
                ". Tessera reads WAV, AIFF, FLAC and MP3.";
    }
    if (skipped > 0)
        text += " " + std::to_string(skipped) + (skipped == 1 ? " file didn't" : " files didn't") +
                " fit: no more tracks or slots.";
    return text;
}

void findFile(EditorContext& ctx, AudioSourceId source) {
    TesseraModule* m = ctx.module();
    const AudioSource* src = m ? m->song().findSource(source) : nullptr;
    if (!src)
        return;
    std::string file = src->file;
    osdialog_filters* filters = osdialog_filters_parse("Audio:wav,wave,aif,aiff,aifc,flac,mp3");
    char* chosen = osdialog_file(OSDIALOG_OPEN, nullptr, src->name.c_str(), filters);
    osdialog_filters_free(filters);
    if (!chosen)
        return;
    std::string from = chosen;
    std::free(chosen);
    std::string to = rack::system::join(m->audioStorage(), file);
    rack::system::createDirectories(rack::system::getDirectory(to));
    if (!rack::system::copy(from, to)) {
        ctx.toast("Couldn't copy " + rack::system::getFilename(from) + " into the patch");
        return;
    }
    if (AudioStore* store = m->audioStore())
        store->retryMissing();
    ctx.toast("Found " + rack::system::getFilename(from));
}

std::vector<AudioSourceId> missingSources(TesseraModule& m) {
    std::vector<AudioSourceId> out;
    AudioStore* store = m.audioStore();
    if (!store)
        return out;
    for (const AudioSource& s : m.song().audioSources)
        if (store->status(s.id).state == SourceState::Missing)
            out.push_back(s.id);
    return out;
}

bool confirmSize(const std::vector<std::string>& paths) {
    constexpr double kLongSeconds = 20 * 60;
    constexpr double kManyBytes = 1024.0 * 1024 * 1024;
    double bytes = 0, longest = 0;
    std::string longName;
    for (const std::string& p : paths) {
        AudioFileInfo info;
        if (!probeAudio(p, info) || info.sampleRate <= 0)
            continue;
        double seconds = double(info.frames) / info.sampleRate;
        // Decoded samples are floats at Rack's rate, at most stereo; a stretched render about doubles that.
        bytes += double(info.frames) * std::min(info.channels, 2) * sizeof(float) * 2;
        if (seconds > longest) {
            longest = seconds;
            longName = rack::system::getFilename(p);
        }
    }
    if (longest <= kLongSeconds && bytes <= kManyBytes)
        return true;
    char text[256];
    if (longest > kLongSeconds)
        snprintf(text, sizeof text, "'%s' is %.0f minutes and needs about %.1f GB of memory. Import anyway?",
                 longName.c_str(), longest / 60, bytes / kManyBytes);
    else
        snprintf(text, sizeof text, "These files need about %.1f GB of memory. Import anyway?", bytes / kManyBytes);
    return osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK_CANCEL, text) == 1;
}

void dropAudio(EditorContext& ctx, const std::vector<std::string>& paths, const AudioDropTarget& target,
               const std::string& note) {
    TesseraModule* m = ctx.module();
    if (!m || !confirmSize(paths))
        return;
    TesseraModule::AudioDropResult r = m->importAudioFiles(paths, target);
    if (!r.made.instances.empty()) {
        ctx.state().clipSelection.clear();
        for (ArrangementId id : r.made.instances)
            ctx.state().clipSelection.insert(id);
    }
    if (!r.made.tracks.empty())
        ctx.state().selectedTrack = r.made.tracks.front();
    if (!r.made.clips.empty())
        ctx.state().selectedClip = r.made.clips.front();
    std::string text = dropMessage(r.imported, r.failed, r.made.skipped);
    if (!note.empty() && !r.made.clips.empty())
        text = note + ". " + text;
    if (r.tempoSet) {
        char buf[96];
        snprintf(buf, sizeof buf, "%s · Song set to %.4g BPM", text.c_str(), r.newTempo);
        char keep[32];
        snprintf(keep, sizeof keep, "Keep %.4g BPM", r.oldTempo);
        double old = r.oldTempo;
        ctx.toast(buf, keep, [m, old] { m->setParamWithUndo(TesseraModule::BPM_PARAM, float(old), "keep the tempo"); });
    } else if (!text.empty()) {
        ctx.toast(text);
    }
}

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
