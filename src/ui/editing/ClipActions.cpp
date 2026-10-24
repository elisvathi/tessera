#include "ClipActions.hpp"
#include "ImportMidi.hpp"

#include "ExportMidi.hpp"
#include "TrackActions.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/ArrangementEdits.hpp"
#include "../../model/DrumKit.hpp"
#include "../../model/LauncherCommands.hpp"
#include "../../model/Serialize.hpp"
#include "../../model/TrackColors.hpp"
#include "../../module/TesseraModule.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../widgets/ColorMenuItem.hpp"
#include "../widgets/MenuTextField.hpp"

#include <osdialog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace tessera::clipActions {

namespace {

const char* const kQuantizeLabels[] = {"None", "1/16", "1/4", "1 bar", "2 bars", "4 bars"};
constexpr LaunchQuantize kQuantizeChoices[] = {LaunchQuantize::Off,     LaunchQuantize::Sixteenth,
                                               LaunchQuantize::Quarter, LaunchQuantize::Bar,
                                               LaunchQuantize::TwoBars, LaunchQuantize::FourBars};
constexpr const char* kClipFileExtension = ".tessera-clip";

const Clip* clipAt(const Song& song, TrackId track, int slot) {
    const Track* t = song.findTrack(track);
    if (!t || slot < 0 || slot >= int(t->slots.size()) || !t->slots[size_t(slot)].clip)
        return nullptr;
    return song.findClip(*t->slots[size_t(slot)].clip);
}

/// The clip now in a slot, selected after an edit put it there.
void selectSlot(EditorContext& ctx, TrackId track, int slot) {
    if (const Clip* c = clipAt(ctx.module()->song(), track, slot))
        select(ctx, track, c->id);
}

std::string sceneLabel(const Song& song, int scene) {
    const Scene& s = song.scenes[size_t(scene)];
    return s.name.empty() ? "Scene " + std::to_string(scene + 1) : s.name;
}

/// File names keep letters, digits, spaces, dashes and underscores.
std::string fileSafe(const std::string& name) {
    std::string out;
    for (char ch : name)
        out += (std::isalnum((unsigned char)ch) || ch == ' ' || ch == '-' || ch == '_') ? ch : '_';
    return out.empty() ? "clip" : out;
}

std::string pickFile(osdialog_file_action action, const std::string& name) {
    osdialog_filters* filters = osdialog_filters_parse("Tessera clip:tessera-clip");
    char* path = osdialog_file(action, nullptr, name.empty() ? nullptr : name.c_str(), filters);
    osdialog_filters_free(filters);
    std::string result = path ? path : "";
    std::free(path);
    return result;
}

void performScene(EditorContext& ctx, int scene, Scene after, const std::string& name) {
    TesseraModule* m = ctx.module();
    m->perform(setScene(m->song(), scene, std::move(after), name));
}

} // namespace

int slotOf(const Track& track, ClipId clip) {
    if (!clip.valid())
        return -1;
    for (size_t i = 0; i < track.slots.size(); i++)
        if (track.slots[i].clip == clip)
            return int(i);
    return -1;
}

ClipId shownClip(EditorContext& ctx, const Track& track) {
    TesseraModule* m = ctx.module();
    if (!m)
        return ClipId();
    const Song& song = m->song();
    ClipId selected = ctx.state().selectedClip;
    bool onTrack =
        slotOf(track, selected) >= 0 || std::any_of(track.arrangement.begin(), track.arrangement.end(),
                                                    [&](const ArrangerClip& a) { return a.clip == selected; });
    if (onTrack && song.findClip(selected))
        return selected;
    int playing = m->launcherStatus(track.id).playing;
    if (const Clip* c = clipAt(song, track.id, playing))
        return c->id;
    // A track whose clips are on the timeline: the one under the playhead, else the earliest one. They come
    // first while the arranger is showing, so stepping through tracks there opens what the timeline holds.
    auto timelineClip = [&]() -> ClipId {
        if (track.arrangement.empty())
            return ClipId();
        if (const ArrangerClip* a = instanceAt(track, Tick(m->playheadTicks())))
            return a->clip;
        return std::min_element(track.arrangement.begin(), track.arrangement.end(),
                                [](const ArrangerClip& x, const ArrangerClip& y) { return x.position < y.position; })
            ->clip;
    };
    if (m->playMode() == PlayMode::Arranger)
        if (ClipId id = timelineClip(); song.findClip(id))
            return id;
    if (const Clip* first = song.activeClip(track))
        return first->id;
    ClipId id = timelineClip();
    return song.findClip(id) ? id : ClipId();
}

void select(EditorContext& ctx, TrackId track, ClipId clip) {
    EditorState& state = ctx.state();
    state.selectedTrack = track;
    state.selectedTracks = {track};
    state.selectedClip = clip;
}

void editTogether(EditorContext& ctx, const std::vector<ClipId>& clips, TrackId track, ClipId open) {
    std::vector<ClipId> unique;
    for (ClipId id : clips)
        if (std::find(unique.begin(), unique.end(), id) == unique.end())
            unique.push_back(id);
    ctx.state().layered = unique.size() > 1 ? unique : std::vector<ClipId>();
    select(ctx, track, open);
    ctx.view().focus = Pane::Detail;
}

TrackId trackOf(const Song& song, ClipId clip) {
    for (const Track& t : song.tracks) {
        for (const ClipSlot& s : t.slots)
            if (s.clip == clip)
                return t.id;
        for (const ArrangerClip& a : t.arrangement)
            if (a.clip == clip)
                return t.id;
    }
    return TrackId();
}

void create(EditorContext& ctx, TrackId track, int slot) {
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    if (clipAt(song, track, slot))
        return;
    m->perform(createClip(song, track, slot, ticksPerBar(song.timeSig), m->newId<ClipId>()));
    selectSlot(ctx, track, slot);
    // Trello 14 Oct: on a fresh song, drawing a clip and pressing Play played nothing until its slot was launched.
    // A track that plays nothing takes its new clip, the way a song starts on each track's first clip.
    LauncherStatus status = m->launcherStatus(track);
    if (m->playMode() == PlayMode::Launcher && status.playing == kNoSlot && status.queued == kNothingQueued)
        m->launch({LaunchEvent::Kind::Slot, track.value, slot, false, false});
}

void createInFirstEmpty(EditorContext& ctx, TrackId track) {
    TesseraModule* m = ctx.module();
    const Track* t = m->song().findTrack(track);
    if (!t)
        return;
    for (size_t i = 0; i < t->slots.size(); i++) {
        if (!t->slots[i].clip) {
            create(ctx, track, int(i));
            return;
        }
    }
    int at = int(t->slots.size());
    insertScene(ctx, at);
    create(ctx, track, at);
}

void duplicate(EditorContext& ctx, TrackId track, int slot) {
    TesseraModule* m = ctx.module();
    m->perform(duplicateSlotClip(m->song(), track, slot, m->ids()));
    selectSlot(ctx, track, slot + 1);
}

void remove(EditorContext& ctx, TrackId track, int slot) {
    TesseraModule* m = ctx.module();
    m->perform(deleteSlotClip(m->song(), track, slot));
}

void move(EditorContext& ctx, TrackId fromTrack, int fromSlot, TrackId toTrack, int toSlot, bool copy) {
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    m->perform(copy ? copySlotClip(song, fromTrack, fromSlot, toTrack, toSlot, m->ids())
                    : moveSlotClip(song, fromTrack, fromSlot, toTrack, toSlot));
    selectSlot(ctx, toTrack, toSlot);
}

void rename(EditorContext& ctx, ClipId clip, const std::string& name) {
    TesseraModule* m = ctx.module();
    m->perform(setClipField(m->song(), clip, &Clip::name, name, "Rename clip"));
}

void appendScaleMenu(rack::ui::Menu* menu, EditorContext& ctx, ClipId clip) {
    EditorContext* c = &ctx;
    auto scale = [c, clip](double factor) {
        if (TesseraModule* m = c->module())
            m->perform(arrange::scaleClip(m->song(), clip, factor));
    };
    menu->addChild(rack::createSubmenuItem("Length", "", [scale](rack::ui::Menu* sub) {
        sub->addChild(rack::createMenuItem("\u00d72 longer", "half speed", [scale] { scale(2); }));
        sub->addChild(rack::createMenuItem("\u00d7\u00bd shorter", "double speed", [scale] { scale(0.5); }));
        sub->addChild(new rack::ui::MenuSeparator);
        sub->addChild(rack::createMenuLabel("Custom length, % (Enter)"));
        sub->addChild(new MenuTextField("150", [scale](const std::string& text) {
            char* end = nullptr;
            double percent = std::strtod(text.c_str(), &end);
            if (end != text.c_str() && percent >= 1 && percent <= 1600)
                scale(percent / 100);
        }));
    }));
}

void startRename(EditorContext& ctx, ClipId clip) {
    TesseraModule* m = ctx.module();
    const Clip* c = m ? m->song().findClip(clip) : nullptr;
    if (!c)
        return;
    EditorContext* cx = &ctx;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Clip name"));
    auto* field = new MenuTextField(c->name, [cx, clip](const std::string& name) {
        // The clip may be gone by the time the field closes (an undo meanwhile).
        if (cx->module() && cx->module()->song().findClip(clip))
            rename(*cx, clip, name);
    });
    menu->addChild(field);
    APP->event->setSelectedWidget(field);
}

void exportClip(EditorContext& ctx, ClipId clip) {
    TesseraModule* m = ctx.module();
    const Clip* c = m ? m->song().findClip(clip) : nullptr;
    if (!c)
        return;
    std::string path = pickFile(OSDIALOG_SAVE, fileSafe(c->name) + kClipFileExtension);
    if (path.empty())
        return;
    if (!rack::string::endsWith(path, kClipFileExtension))
        path += kClipFileExtension;
    json_t* fileJ = clipFileToJson(*c);
    if (json_dump_file(fileJ, path.c_str(), JSON_INDENT(2)) != 0)
        WARN("Tessera: could not write clip file %s", path.c_str());
    json_decref(fileJ);
}

void importClip(EditorContext& ctx, TrackId track, int slot) {
    std::string path = pickFile(OSDIALOG_OPEN, "");
    TesseraModule* m = ctx.module(); // the dialog blocks; the module may be gone when it returns
    if (path.empty() || !m)
        return;
    json_error_t error;
    json_t* fileJ = json_load_file(path.c_str(), 0, &error);
    std::optional<Clip> clip = fileJ ? clipFileFromJson(fileJ) : std::nullopt;
    if (fileJ)
        json_decref(fileJ);
    if (!clip) {
        WARN("Tessera: %s is not a Tessera clip file", path.c_str());
        return;
    }
    m->perform(importClipToSlot(m->song(), track, slot, *clip, m->ids()));
    selectSlot(ctx, track, slot);
}

void insertScene(EditorContext& ctx, int index) {
    TesseraModule* m = ctx.module();
    m->perform(tessera::insertScene(m->song(), index));
}

std::string quantizeLabel(LaunchQuantize q) { return kQuantizeLabels[int(q)]; }

namespace {

Tick playedLength(const Clip& clip) { return clip.playEnd() - clip.playStart(); }

/// A clip that doesn't loop, on a drum track with a lane that has its own steps: the lane repeats until the clip's
/// length, so the clip has a length to show and set, not only its last note.
bool lanesRepeat(const Song& song, const Clip& clip) {
    if (clip.loopMode == LoopMode::Loop)
        return false;
    const Track* t = song.findTrack(trackOf(song, clip.id));
    return t && t->type == TrackType::Drum &&
           std::any_of(t->drumLanes.begin(), t->drumLanes.end(),
                       [&](const DrumLane& l) { return hasOwnLoop(l, clip); });
}

} // namespace

Tick shownLength(const Song& song, const Clip& clip) {
    return lanesRepeat(song, clip) ? laneLoopsEnd(clip) : playedLength(clip);
}

std::string lengthLabel(const Song& song, const Clip& clip) {
    Tick length = shownLength(song, clip);
    Tick bar = ticksPerBar(song.timeSig), beat = ticksPerBeat(song.timeSig);
    char text[32];
    if (length % bar == 0)
        snprintf(text, sizeof(text), "%lld %s", (long long)(length / bar), length == bar ? "bar" : "bars");
    else
        snprintf(text, sizeof(text), "%.4g beats", double(length) / double(beat));
    return text;
}

void openLengthMenu(EditorContext& ctx, ClipId clipId) {
    if (ctx.module() && ctx.module()->song().findClip(clipId))
        appendLengthItems(rack::createMenu(), ctx, clipId);
}

void appendLengthItems(rack::ui::Menu* menu, EditorContext& ctx, ClipId clipId) {
    TesseraModule* m = ctx.module();
    const Clip* clip = m ? m->song().findClip(clipId) : nullptr;
    if (!clip)
        return;
    EditorContext* c = &ctx;
    Tick bar = ticksPerBar(m->song().timeSig);
    Tick current = shownLength(m->song(), *clip);
    bool looping = clip->loopMode == LoopMode::Loop;
    bool repeats = lanesRepeat(m->song(), *clip);
    menu->addChild(rack::createMenuLabel(looping   ? "Loop length"
                                         : repeats ? "Clip length: lanes with their own steps repeat to here"
                                                   : "Clip length: up to its last note"));
    for (int bars : {1, 2, 4, 8, 16}) {
        if (!looping && !repeats)
            break; // without a loop the notes set the length
        Tick length = bars * bar;
        menu->addChild(rack::createCheckMenuItem(
            std::to_string(bars) + (bars == 1 ? " bar" : " bars"), "", [current, length] { return current == length; },
            [c, clipId, length, looping] {
                if (TesseraModule* m = c->module())
                    m->perform(looping ? setLoopLength(m->song(), clipId, length)
                                       : setClipLength(m->song(), clipId, length));
            }));
    }
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Double", "copies the notes", [c, clipId] {
        if (TesseraModule* m = c->module())
            m->perform(doubleLoop(m->song(), clipId, m->ids()));
    }));
    auto* halve = rack::createMenuItem("Halve", "", [c, clipId, current] {
        if (TesseraModule* m = c->module())
            m->perform(setLoopLength(m->song(), clipId, current / 2));
    });
    halve->disabled = current < 2 || !looping;
    menu->addChild(halve);
}

std::string chipText(EditorContext& ctx, ClipId clip, bool shortText) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(ctx.state().selectedTrack) : nullptr;
    std::string text = t ? t->name : "No track";
    if (shortText)
        return text;
    if (const Clip* cl = m ? m->song().findClip(clip) : nullptr) {
        text += "  ·  " + (cl->name.empty() ? std::string("Clip") : cl->name);
        text += "  ·  " + lengthLabel(m->song(), *cl);
    }
    return text;
}

void openChipMenu(EditorContext& ctx, TrackId track, ClipId clip) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    EditorContext* c = &ctx;
    rack::ui::Menu* menu = rack::createMenu();
    const Clip* cl = m->song().findClip(clip);
    if (!cl) {
        TrackId selected = ctx.state().selectedTrack;
        menu->addChild(rack::createMenuItem("Create a clip", "double-click", [c, selected] {
            if (c->module() && c->module()->song().findTrack(selected))
                createInFirstEmpty(*c, selected);
        }));
        return;
    }
    ClipId id = cl->id;
    // Which track's notes show: the mouse way to switch, also when the notes are maximized (Ctrl+Up / Ctrl+Down).
    const Track* shown = m->song().findTrack(ctx.state().selectedTrack);
    menu->addChild(rack::createSubmenuItem("Track", shown ? shown->name : "", [c](rack::ui::Menu* sub) {
        TesseraModule* mod = c->module();
        if (!mod)
            return;
        sub->addChild(rack::createMenuItem("Previous track", shortcutLabel(Action::PreviousTrack),
                                           [c] { c->selectTrackOffset(-1); }));
        sub->addChild(
            rack::createMenuItem("Next track", shortcutLabel(Action::NextTrack), [c] { c->selectTrackOffset(+1); }));
        sub->addChild(new rack::ui::MenuSeparator);
        for (const Track& t : mod->song().tracks) {
            TrackId id = t.id;
            sub->addChild(rack::createCheckMenuItem(
                t.name, "", [c, id] { return c->state().selectedTrack == id; },
                [c, id] { trackActions::click(*c, id, false, false); }));
        }
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel(cl->name.empty() ? "Clip" : cl->name));
    menu->addChild(rack::createMenuItem("Rename…", "", [c, id] { startRename(*c, id); }));
    menu->addChild(rack::createBoolMenuItem(
        "Loop", "",
        [c, id] {
            const Clip* x = c->module() ? c->module()->song().findClip(id) : nullptr;
            return x && x->loopMode == LoopMode::Loop;
        },
        [c, id](bool on) {
            if (TesseraModule* mod = c->module())
                mod->perform(setClipField(mod->song(), id, &Clip::loopMode, on ? LoopMode::Loop : LoopMode::Off,
                                          on ? "Loop clip" : "Play clip once"));
        }));
    menu->addChild(new rack::ui::MenuSeparator);
    appendLengthItems(menu, ctx, id);
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Save as Tessera clip file…", "", [c, id] { exportClip(*c, id); }));
    menu->addChild(
        rack::createMenuItem("Export as MIDI…", "", [c, track, id] { exportMidi::openClips(*c, {{track, id}}); }));
}

void appendClipMenu(rack::ui::Menu* menu, EditorContext& ctx, TrackId track, int slot) {
    TesseraModule* m = ctx.module();
    const Clip* clip = clipAt(m->song(), track, slot);
    if (!clip)
        return;
    EditorContext* c = &ctx;
    ClipId id = clip->id;
    menu->addChild(rack::createMenuLabel(clip->name.empty() ? "Clip" : clip->name));
    menu->addChild(rack::createMenuItem("Launch", "", [c, track, slot] { c->module()->launchSlot(track, slot); }));
    std::vector<ClipId> scene;
    for (const Track& t : m->song().tracks)
        if (t.type == TrackType::Instrument)
            if (const Clip* other = clipAt(m->song(), t.id, slot))
                scene.push_back(other->id);
    if (scene.size() > 1)
        menu->addChild(rack::createMenuItem("Edit this scene's clips together", "",
                                            [c, scene, track, id] { editTogether(*c, scene, track, id); }));
    menu->addChild(rack::createMenuItem("Rename", "", [c, id] { startRename(*c, id); }));
    appendScaleMenu(menu, ctx, id);
    menu->addChild(rack::createMenuItem("Place in arrangement at playhead", "", [c, track, id] {
        TesseraModule* mod = c->module();
        Tick bar = ticksPerBar(mod->song().timeSig);
        Tick at = Tick(mod->playheadTicks() / double(bar)) * bar;
        mod->perform(arrange::placeClip(
            mod->song(), track, id, at, [mod] { return mod->newId<ArrangementId>(); }, c->state().stackClips));
    }));
    menu->addChild(rack::createMenuItem("Duplicate", shortcutLabel(Action::Duplicate),
                                        [c, track, slot] { duplicate(*c, track, slot); }));
    menu->addChild(
        rack::createMenuItem("Export as MIDI…", "", [c, track, id] { exportMidi::openClips(*c, {{track, id}}); }));
    menu->addChild(
        rack::createMenuItem("Delete", shortcutLabel(Action::Delete), [c, track, slot] { remove(*c, track, slot); }));

    menu->addChild(rack::createBoolMenuItem(
        "Loop", "",
        [c, id] {
            const Clip* cl = c->module()->song().findClip(id);
            return cl && cl->loopMode == LoopMode::Loop;
        },
        [c, id](bool on) {
            TesseraModule* mod = c->module();
            mod->perform(setClipField(mod->song(), id, &Clip::loopMode, on ? LoopMode::Loop : LoopMode::Off,
                                      on ? "Loop clip" : "Play clip once"));
        }));

    std::string current = clip->launchQuantize ? quantizeLabel(*clip->launchQuantize) : "Song";
    menu->addChild(rack::createSubmenuItem("Launch quantize", current, [c, id](rack::ui::Menu* sub) {
        auto set = [c, id](std::optional<LaunchQuantize> q) {
            TesseraModule* mod = c->module();
            mod->perform(setClipField(mod->song(), id, &Clip::launchQuantize, q, "Set clip launch quantize"));
        };
        auto isSet = [c, id](std::optional<LaunchQuantize> q) {
            const Clip* cl = c->module()->song().findClip(id);
            return cl && cl->launchQuantize == q;
        };
        sub->addChild(rack::createCheckMenuItem(
            "Song setting", "", [isSet] { return isSet(std::nullopt); }, [set] { set(std::nullopt); }));
        for (LaunchQuantize q : kQuantizeChoices)
            sub->addChild(
                rack::createCheckMenuItem(quantizeLabel(q), "", [isSet, q] { return isSet(q); }, [set, q] { set(q); }));
    }));

    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Save as Tessera clip file…", "", [c, id] { exportClip(*c, id); }));
    menu->addChild(
        rack::createMenuItem("Replace with Tessera clip file…", "", [c, track, slot] { importClip(*c, track, slot); }));
}

void appendEmptySlotMenu(rack::ui::Menu* menu, EditorContext& ctx, TrackId track, int slot) {
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuLabel("Empty slot"));
    menu->addChild(rack::createMenuItem("Create clip", "double-click", [c, track, slot] { create(*c, track, slot); }));
    menu->addChild(
        rack::createMenuItem("Load a Tessera clip file…", "", [c, track, slot] { importClip(*c, track, slot); }));
    menu->addChild(
        rack::createMenuItem("Import MIDI file…", "", [c, track, slot] { importMidi::toSlot(*c, track, slot); }));
    menu->addChild(rack::createMenuItem("Stop track", "", [c, track] { c->module()->stopTrack(track); }));
}

void appendSceneMenu(rack::ui::Menu* menu, EditorContext& ctx, int scene, std::function<void()> startRename) {
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    if (scene < 0 || scene >= int(song.scenes.size()))
        return;
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuLabel(sceneLabel(song, scene)));
    menu->addChild(rack::createMenuItem("Launch", "", [c, scene] { c->module()->launchScene(scene); }));
    menu->addChild(rack::createMenuItem("Rename", "double-click", std::move(startRename)));

    menu->addChild(rack::createSubmenuItem("Colour", "", [c, scene](rack::ui::Menu* sub) {
        const Song& s = c->module()->song();
        if (scene >= int(s.scenes.size()))
            return;
        Scene current = s.scenes[size_t(scene)];
        auto setColor = [c, scene, current](uint32_t color) {
            Scene after = current;
            after.color = color;
            performScene(*c, scene, after, "Change scene colour");
        };
        sub->addChild(rack::createCheckMenuItem(
            "None", "", [current] { return current.color == 0; }, [setColor] { setColor(0); }));
        for (int i = 0; i < 16; i++) {
            uint32_t color = kTrackColors[i];
            sub->addChild(new ColorMenuItem("Colour " + std::to_string(i + 1), color, current.color == color,
                                            [setColor, color] { setColor(color); }));
        }
    }));

    const Scene& s = song.scenes[size_t(scene)];
    char tempoText[16] = "";
    if (s.tempo)
        snprintf(tempoText, sizeof(tempoText), "%.2f", *s.tempo);
    menu->addChild(rack::createSubmenuItem("Tempo", s.tempo ? tempoText : "none", [c, scene](rack::ui::Menu* sub) {
        const Song& song = c->module()->song();
        if (scene >= int(song.scenes.size()))
            return;
        Scene current = song.scenes[size_t(scene)];
        sub->addChild(rack::createMenuLabel("Tempo when the scene launches (BPM)"));
        char text[16] = "";
        if (current.tempo)
            snprintf(text, sizeof(text), "%.2f", *current.tempo);
        sub->addChild(new MenuTextField(text, [c, scene, current](const std::string& value) {
            Scene after = current;
            char* end = nullptr;
            double bpm = std::strtod(value.c_str(), &end);
            if (value.empty() || end == value.c_str())
                after.tempo.reset();
            else
                after.tempo = std::clamp(bpm, Engine::kMinBpm, Engine::kMaxBpm);
            performScene(*c, scene, after, after.tempo ? "Set scene tempo" : "Clear scene tempo");
        }));
        sub->addChild(rack::createMenuItem("No tempo change", "", [c, scene, current] {
            Scene after = current;
            after.tempo.reset();
            performScene(*c, scene, after, "Clear scene tempo");
        }));
    }));

    menu->addChild(new rack::ui::MenuSeparator);
    bool full = int(song.scenes.size()) >= kMaxScenes;
    menu->addChild(rack::createMenuItem("Insert scene above", "", [c, scene] { insertScene(*c, scene); }, full));
    menu->addChild(rack::createMenuItem("Insert scene below", "", [c, scene] { insertScene(*c, scene + 1); }, full));
    menu->addChild(rack::createMenuItem(
        "Delete scene", "",
        [c, scene] {
            TesseraModule* mod = c->module();
            mod->perform(removeScene(mod->song(), scene));
        },
        song.scenes.size() <= 1));
}

void appendQuantizeMenu(rack::ui::Menu* menu, EditorContext& ctx) {
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuLabel("Launch quantize"));
    for (LaunchQuantize q : kQuantizeChoices) {
        menu->addChild(rack::createCheckMenuItem(
            quantizeLabel(q), "", [c, q] { return c->module() && c->module()->song().launchQuantize == q; },
            [c, q] {
                TesseraModule* m = c->module();
                m->perform(
                    setSongField(m->song(), &Song::launchQuantize, q, "Set launch quantize to " + quantizeLabel(q)));
            }));
    }
}

} // namespace tessera::clipActions
