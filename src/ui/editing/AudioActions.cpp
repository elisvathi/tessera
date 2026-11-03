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
    RenderKey key;
    double songTempo = m->tempo() > 0 ? m->tempo() : 120;
    if (m->audioStore() && wantsRender(*clip->audio, songTempo, key) &&
        m->audioStore()->renderStatus(key).state == RenderState::Failed)
        menu->addChild(rack::createMenuItem("Try again", "make the stretched version",
                                            [m, key] { m->audioStore()->retry(key); }));
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

void dropAudio(EditorContext& ctx, const std::vector<std::string>& paths, const AudioDropTarget& target,
               const std::string& note) {
    TesseraModule* m = ctx.module();
    if (!m)
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
