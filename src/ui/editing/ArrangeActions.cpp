#include "ArrangeActions.hpp"
#include "ImportMidi.hpp"

#include "ExportMidi.hpp"
#include "TrackActions.hpp"

#include "../../model/ArrangementEdits.hpp"
#include "../../module/TesseraModule.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../views/Arranger.hpp"
#include "../views/ArrangerLanes.hpp"
#include "ClipActions.hpp"

#include <cmath>

namespace tessera::arrangeActions {

using arrange::IdSet;
using arrange::Layout;

namespace {

/// Instances copied with Ctrl/Cmd+C: positions relative to the earliest, tracks relative to the topmost.
struct Clipboard {
    std::vector<std::pair<int, ArrangerClip>> items; // (track offset in display order, instance)
    // The clips they play, as they were: cutting a clip's last instance removes the clip, and paste brings it back.
    std::vector<Clip> clips;
};
Clipboard clipboard;

TesseraModule* moduleOf(ArrangerLanes& lanes) { return lanes.context().module(); }

/// Runs `edit` on the committed layout and commits the result under `name`, keeping the selection.
template <typename F>
void editSelected(ArrangerLanes& lanes, const std::string& name, F edit) {
    const Layout& before = lanes.committed();
    IdSet ids = lanes.selection();
    if (ids.empty())
        return;
    Layout after = edit(before, ids);
    lanes.commit(name, before, after, ids);
}

std::vector<TrackId> songOrder(const Song& song) {
    std::vector<TrackId> order;
    for (const Track& t : song.tracks)
        order.push_back(t.id);
    return order;
}

arrange::IdSource idSource(ArrangerLanes& lanes) {
    return [&lanes] { return lanes.newInstanceId(); };
}

} // namespace

void deleteSelected(ArrangerLanes& lanes) {
    size_t n = lanes.selection().size();
    editSelected(lanes, "Delete " + arrange::countClips(n), [](const Layout& before, const IdSet& ids) {
        Layout out;
        for (const arrange::Placed& p : before)
            if (!ids.count(p.clip.id))
                out.push_back(p);
        return out;
    });
    lanes.selection().clear();
}

void duplicateSelected(ArrangerLanes& lanes) {
    const Layout& before = lanes.committed();
    IdSet ids = lanes.selection();
    auto [start, end] = arrange::span(before, ids);
    if (end <= start)
        return;
    IdSet added;
    Layout after =
        arrange::duplicate(before, ids, end - start, songOrder(moduleOf(lanes)->song()), 0, idSource(lanes), &added);
    after = lanes.settle(after, added, idSource(lanes));
    lanes.commit("Duplicate " + arrange::countClips(ids.size()), before, after, added);
}

void split(ArrangerLanes& lanes, Tick at) {
    TesseraModule* m = moduleOf(lanes);
    const Layout& before = lanes.committed();
    IdSet ids = lanes.selection();
    if (ids.empty()) {
        // Nothing selected: the clips under that time on the selected track.
        for (const arrange::Placed& p : before)
            if (p.track == lanes.context().state().selectedTrack)
                ids.insert(p.clip.id);
    }
    IdSet right;
    Layout after = arrange::split(before, ids, at, m->song().clips, idSource(lanes), &right);
    if (right.empty())
        return;
    IdSet select = lanes.selection();
    select.insert(right.begin(), right.end());
    lanes.commit("Split " + arrange::countClips(right.size()), before, after, select);
}

void glueSelected(ArrangerLanes& lanes) {
    TesseraModule* m = moduleOf(lanes);
    IdSet ids = lanes.selection();
    CommandPtr command = arrange::glue(m->song(), ids, m->ids());
    if (!command)
        return;
    IdSet old = arrange::idsOf(lanes.committed());
    m->perform(std::move(command));
    // Select the glued instance: the one that wasn't there before.
    lanes.selection().clear();
    for (const arrange::Placed& p : lanes.committed())
        if (!old.count(p.clip.id))
            lanes.selection().insert(p.clip.id);
}

void makeUniqueSelected(ArrangerLanes& lanes) {
    TesseraModule* m = moduleOf(lanes);
    m->perform(arrange::makeUnique(m->song(), lanes.selection(), m->ids()));
}

void setMuted(ArrangerLanes& lanes, bool muted) {
    editSelected(lanes, muted ? "Mute clips" : "Unmute clips",
                 [muted](const Layout& l, const IdSet& ids) { return arrange::setMuted(l, ids, muted); });
}

void transpose(ArrangerLanes& lanes, int semitones) {
    editSelected(lanes, "Transpose clips",
                 [semitones](const Layout& l, const IdSet& ids) { return arrange::transpose(l, ids, semitones); });
}

void setVelocityScale(ArrangerLanes& lanes, float scale) {
    editSelected(lanes, "Scale clip velocity",
                 [scale](const Layout& l, const IdSet& ids) { return arrange::setVelocityScale(l, ids, scale); });
}

void resetStretch(ArrangerLanes& lanes) {
    editSelected(lanes, "Reset stretch", [](const Layout& l, const IdSet& ids) {
        Layout out = l;
        for (arrange::Placed& p : out) {
            if (!ids.count(p.clip.id) || p.clip.stretch == 1.f)
                continue;
            p.clip.length = std::max<Tick>(1, Tick(std::llround(double(p.clip.length) / double(p.clip.stretch))));
            p.clip.stretch = 1.f;
        }
        return out;
    });
}

void nudge(ArrangerLanes& lanes, Tick ticks) {
    TesseraModule* m = moduleOf(lanes);
    const Layout& before = lanes.committed();
    IdSet ids = lanes.selection();
    Layout after = arrange::move(before, ids, ticks, songOrder(m->song()), 0);
    after = lanes.settle(after, ids, idSource(lanes));
    lanes.commit("Nudge " + arrange::countClips(ids.size()), before, after, ids);
}

void changeLength(ArrangerLanes& lanes, Tick ticks) {
    const Layout& before = lanes.committed();
    IdSet ids = lanes.selection();
    Layout after = arrange::resizeEnd(before, ids, ticks, std::max<Tick>(1, std::abs(ticks)));
    after = lanes.settle(after, ids, idSource(lanes));
    lanes.commit("Resize " + arrange::countClips(ids.size()), before, after, ids);
}

void selectAll(ArrangerLanes& lanes) {
    lanes.selection() = arrange::idsOf(lanes.committed());
    lanes.owner.target = ArrangerView::Target::Clips;
}

void selectNone(ArrangerLanes& lanes) { lanes.selection().clear(); }

void selectInvert(ArrangerLanes& lanes) {
    IdSet inverted;
    for (const arrange::Placed& p : lanes.committed())
        if (!lanes.selection().count(p.clip.id))
            inverted.insert(p.clip.id);
    lanes.selection() = inverted;
}

void copy(ArrangerLanes& lanes) {
    TesseraModule* m = moduleOf(lanes);
    Layout chosen = arrange::pick(lanes.committed(), lanes.selection());
    if (chosen.empty())
        return;
    std::vector<TrackId> order = songOrder(m->song());
    auto [start, end] = arrange::span(chosen, lanes.selection());
    int top = int(order.size());
    for (const arrange::Placed& p : chosen)
        top = std::min(top, int(std::find(order.begin(), order.end(), p.track) - order.begin()));
    clipboard.items.clear();
    clipboard.clips.clear();
    for (const arrange::Placed& p : chosen) {
        ArrangerClip a = p.clip;
        a.position -= start;
        int row = int(std::find(order.begin(), order.end(), p.track) - order.begin());
        clipboard.items.push_back({row - top, a});
        bool kept =
            std::any_of(clipboard.clips.begin(), clipboard.clips.end(), [&](const Clip& c) { return c.id == a.clip; });
        if (const Clip* clip = m->song().findClip(a.clip); clip && !kept)
            clipboard.clips.push_back(*clip);
    }
}

void cut(ArrangerLanes& lanes) {
    copy(lanes);
    deleteSelected(lanes);
}

void paste(ArrangerLanes& lanes, bool atMouse) {
    TesseraModule* m = moduleOf(lanes);
    if (clipboard.items.empty())
        return;
    const Song& song = m->song();
    std::vector<TrackId> order = songOrder(song);
    int base = song.trackIndex(lanes.context().state().selectedTrack);
    if (base < 0)
        base = 0;
    double mouse = lanes.mouseTick();
    Tick at =
        atMouse && std::isfinite(mouse) ? lanes.snapTick(mouse, false, {}) : Tick(std::llround(m->playheadTicks()));
    const Layout& before = lanes.committed();
    Layout after = before;
    IdSet added;
    std::vector<Clip> restored;
    for (const auto& [rowOffset, item] : clipboard.items) {
        int row = base + rowOffset;
        if (row >= int(order.size()))
            continue; // past the last track
        if (!song.findClip(item.clip)) {
            auto kept = std::find_if(clipboard.clips.begin(), clipboard.clips.end(),
                                     [&](const Clip& c) { return c.id == item.clip; });
            if (kept == clipboard.clips.end())
                continue;
            bool once = std::none_of(restored.begin(), restored.end(), [&](const Clip& c) { return c.id == kept->id; });
            if (once)
                restored.push_back(*kept);
        }
        arrange::Placed p{order[size_t(row)], item};
        p.clip.id = lanes.newInstanceId();
        p.clip.position = std::max<Tick>(0, at + item.position);
        after.push_back(p);
        added.insert(p.clip.id);
    }
    after = lanes.settle(after, added, idSource(lanes));
    lanes.commit("Paste " + arrange::countClips(added.size()), before, after, added, std::move(restored));
}

void openInEditor(ArrangerLanes& lanes, ArrangementId id) {
    EditorContext& ctx = lanes.context();
    const arrange::Placed* p = arrange::find(lanes.committed(), id);
    if (!p)
        return;
    ViewState& view = ctx.view();
    bool alreadyOpen = ctx.state().selectedClip == p->clip.clip && view.focus == Pane::Detail;
    lanes.selection() = {id};
    clipActions::select(ctx, p->track, p->clip.clip);
    view.focus = Pane::Detail;
    if (view.maximized == Pane::Top)
        view.maximized.reset();
    else if (alreadyOpen)
        view.maximized = Pane::Detail; // double-click again: the piano roll fills the editor
}

void appendClipMenu(rack::ui::Menu* menu, ArrangerLanes& lanes, ArrangementId clicked, Tick at) {
    TesseraModule* m = moduleOf(lanes);
    const Song& song = m->song();
    const arrange::Placed* placed = arrange::find(lanes.committed(), clicked);
    const Clip* clip = placed ? song.findClip(placed->clip.clip) : nullptr;
    if (!clip)
        return;
    ArrangerLanes* l = &lanes;
    ClipId clipId = clip->id;
    size_t n = lanes.selection().size();
    int uses = song.clipUses(clipId);
    std::string title = n > 1 ? arrange::countClips(n) : (clip->name.empty() ? "Clip" : clip->name);
    if (n == 1 && uses > 1)
        title += "  (linked, " + std::to_string(uses) + " uses)";
    menu->addChild(rack::createMenuLabel(title));

    menu->addChild(
        rack::createMenuItem("Open in piano roll", "double-click", [l, clicked] { openInEditor(*l, clicked); }));
    if (n > 1) {
        std::vector<ClipId> together;
        for (const arrange::Placed& p : lanes.committed())
            if (lanes.selection().count(p.clip.id))
                together.push_back(p.clip.clip);
        TrackId track = placed->track;
        menu->addChild(rack::createMenuItem("Edit together in the piano roll", "", [l, together, track, clipId] {
            clipActions::editTogether(l->context(), together, track, clipId);
        }));
    }
    EditorContext* c = &lanes.context();
    menu->addChild(rack::createMenuItem("Rename", "", [c, clipId] { clipActions::startRename(*c, clipId); }));
    menu->addChild(rack::createMenuItem("Duplicate", shortcutLabel(Action::Duplicate), [l] { duplicateSelected(*l); }));
    {
        std::vector<std::pair<TrackId, ClipId>> clips;
        for (const arrange::Placed& p : lanes.committed())
            if (lanes.selection().count(p.clip.id) || p.clip.id == clicked) {
                std::pair<TrackId, ClipId> one{p.track, p.clip.clip};
                if (std::find(clips.begin(), clips.end(), one) == clips.end())
                    clips.push_back(one);
            }
        menu->addChild(rack::createMenuItem(clips.size() > 1 ? "Export clips as MIDI…" : "Export as MIDI…", "",
                                            [l, clips] { exportMidi::openClips(l->context(), clips); }));
    }
    menu->addChild(rack::createMenuItem("Delete", shortcutLabel(Action::Delete), [l] { deleteSelected(*l); }));
    menu->addChild(rack::createMenuItem("Split here", shortcutLabel(Action::Split), [l, at] { split(*l, at); }));
    menu->addChild(rack::createMenuItem("Glue", shortcutLabel(Action::Glue), [l] { glueSelected(*l); }));
    menu->addChild(rack::createMenuItem(
        "Make unique", shortcutLabel(Action::MakeUnique), [l] { makeUniqueSelected(*l); }, uses <= 1));
    bool muted = placed->clip.muted;
    menu->addChild(rack::createMenuItem(muted ? "Unmute" : "Mute", "", [l, muted] { setMuted(*l, !muted); }));

    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(
        rack::createSubmenuItem("Transpose", std::to_string(placed->clip.transpose), [l](rack::ui::Menu* sub) {
            for (int d : {12, 1, -1, -12})
                sub->addChild(
                    rack::createMenuItem((d > 0 ? "+" : "") + std::to_string(d), "", [l, d] { transpose(*l, d); }));
            sub->addChild(rack::createMenuItem("Reset", "", [l] {
                editSelected(*l, "Reset transpose", [](const Layout& lay, const IdSet& ids) {
                    Layout out = lay;
                    for (arrange::Placed& p : out)
                        if (ids.count(p.clip.id))
                            p.clip.transpose = 0;
                    return out;
                });
            }));
        }));
    menu->addChild(rack::createSubmenuItem(
        "Velocity", std::to_string(int(std::lround(placed->clip.velocityScale * 100))) + "%", [l](rack::ui::Menu* sub) {
            for (int pct : {50, 75, 100, 125, 150})
                sub->addChild(rack::createMenuItem(std::to_string(pct) + "%", "",
                                                   [l, pct] { setVelocityScale(*l, float(pct) / 100.f); }));
        }));
    menu->addChild(rack::createMenuItem("Reset stretch", "", [l] { resetStretch(*l); }, placed->clip.stretch == 1.f));

    // The clip itself (every instance of it changes).
    menu->addChild(new rack::ui::MenuSeparator);
    clipActions::appendScaleMenu(menu, *c, clipId);
    menu->addChild(rack::createBoolMenuItem(
        "Clip loops", "",
        [c, clipId] {
            const Clip* cl = c->module()->song().findClip(clipId);
            return cl && cl->loopMode == LoopMode::Loop;
        },
        [c, clipId](bool on) {
            TesseraModule* mod = c->module();
            mod->perform(setClipField(mod->song(), clipId, &Clip::loopMode, on ? LoopMode::Loop : LoopMode::Off,
                                      on ? "Loop clip" : "Play clip once"));
        }));
    menu->addChild(
        rack::createMenuItem("Save as Tessera clip file…", "", [c, clipId] { clipActions::exportClip(*c, clipId); }));
}

void appendLaneMenu(rack::ui::Menu* menu, ArrangerLanes& lanes, TrackId track, Tick at) {
    TesseraModule* m = moduleOf(lanes);
    const Track* t = m->song().findTrack(track);
    if (!t)
        return;
    ArrangerLanes* l = &lanes;
    menu->addChild(rack::createMenuLabel(t->name));
    menu->addChild(rack::createMenuItem("Create clip here", "double-click", [l, track, at] {
        TesseraModule* mod = l->context().module();
        Tick bar = ticksPerBar(mod->song().timeSig);
        ClipId clip = l->newClipId();
        ArrangementId id = l->newInstanceId();
        mod->perform(arrange::drawClip(
            mod->song(), track, at, bar, clip, id, [l] { return l->newInstanceId(); },
            l->context().state().stackClips));
        l->selection() = {id};
        clipActions::select(l->context(), track, clip);
    }));
    menu->addChild(rack::createMenuItem("Import MIDI file here…", "", [l, track, at] {
        std::string path = importMidi::pickFile(); // the dialog blocks; check the module again after it
        if (path.empty() || !l->context().module())
            return;
        importMidi::chooseClip(l->context(), path, [l, track, at](Clip clip) {
            TesseraModule* mod = l->context().module();
            if (!mod)
                return;
            ClipId id = clip.id;
            ArrangementId instance = l->newInstanceId();
            mod->perform(arrange::placeNewClip(
                mod->song(), track, at, std::move(clip), instance, [l] { return l->newInstanceId(); },
                l->context().state().stackClips, "Import MIDI file"));
            l->selection() = {instance};
            clipActions::select(l->context(), track, id);
        });
    }));
    menu->addChild(rack::createMenuItem(
        "Paste", shortcutLabel(Action::Paste), [l] { paste(*l, false); }, clipboard.items.empty()));
    // The launcher's clips of this track, ready to drop here.
    std::vector<ClipId> slotClips;
    for (const ClipSlot& s : t->slots)
        if (s.clip && std::find(slotClips.begin(), slotClips.end(), *s.clip) == slotClips.end())
            slotClips.push_back(*s.clip);
    if (!slotClips.empty()) {
        menu->addChild(
            rack::createSubmenuItem("Place launcher clip", "", [l, track, at, slotClips](rack::ui::Menu* sub) {
                const Song& song = l->context().module()->song();
                for (ClipId id : slotClips) {
                    const Clip* clip = song.findClip(id);
                    if (!clip)
                        continue;
                    sub->addChild(
                        rack::createMenuItem(clip->name.empty() ? "Clip" : clip->name, "", [l, track, at, id] {
                            TesseraModule* mod = l->context().module();
                            mod->perform(arrange::placeClip(
                                mod->song(), track, id, at, [l] { return l->newInstanceId(); },
                                l->context().state().stackClips));
                        }));
                }
            }));
    }
    // Edit modes, also in the arranger's corner when there is room.
    EditorContext* c = &lanes.context();
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createCheckMenuItem(
        "Ripple (deleting closes the gap)", "", [c] { return c->state().ripple; },
        [c] { c->state().ripple = !c->state().ripple; }));
    menu->addChild(rack::createCheckMenuItem(
        "Automation follows clips", "", [c] { return c->state().automationFollowsClips; },
        [c] { c->state().automationFollowsClips = !c->state().automationFollowsClips; }));
    menu->addChild(
        rack::createMenuItem("Add automation lane", "", [c, track] { trackActions::addAutomationLane(*c, track); }));
}

} // namespace tessera::arrangeActions
