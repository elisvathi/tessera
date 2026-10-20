#include "EditorHost.hpp"

#include "../module/TesseraModule.hpp"
#include "Theme.hpp"
#include "editing/DeviceActions.hpp"
#include "editing/ExportMidi.hpp"
#include "editing/RecordActions.hpp"
#include "editing/TrackActions.hpp"
#include "interaction/Cursors.hpp"
#include "interaction/InteractiveView.hpp"
#include "views/Arranger.hpp"
#include "views/EditorPane.hpp"
#include "widgets/Draw.hpp"

namespace tessera {

namespace {

/// The menu showing on top of Rack, if any. Rack's module browser is a menu overlay that stays in the scene while
/// hidden (and plugins such as Leviathan's Deepcache replace it with their own), so only visible overlays count
/// and the browser never does: deleting it crashes Rack.
rack::ui::MenuOverlay* openMenu() {
    for (auto it = APP->scene->children.rbegin(); it != APP->scene->children.rend(); ++it) {
        auto* overlay = dynamic_cast<rack::ui::MenuOverlay*>(*it);
        if (overlay && overlay->isVisible() && *it != APP->scene->browser)
            return overlay;
    }
    return nullptr;
}

bool isModifierKey(int key) {
    switch (key) {
    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT:
    case GLFW_KEY_LEFT_ALT:
    case GLFW_KEY_RIGHT_ALT:
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL:
    case GLFW_KEY_LEFT_SUPER:
    case GLFW_KEY_RIGHT_SUPER:
        return true;
    default:
        return false;
    }
}

} // namespace

bool EditorHost::handleKey(const rack::widget::Widget::KeyBaseEvent& e) {
    // While a text field has the keyboard (renaming a track), keys are text, not shortcuts.
    if (dynamic_cast<rack::ui::TextField*>(APP->event->getSelectedWidget()))
        return false;
    // The editor has the keyboard even while one of its dropdowns is open: Esc closes the dropdown, not the editor.
    if (e.key == GLFW_KEY_ESCAPE && e.action == GLFW_PRESS) {
        if (rack::ui::MenuOverlay* menu = openMenu()) {
            menu->requestDelete();
            return true;
        }
    }
    // Any key closes the help.
    if (ctx.showHelp) {
        if (e.action == GLFW_PRESS)
            ctx.showHelp = false;
        return true;
    }
    ctx.mods = Modifiers::fromGlfw(e.mods);
    if (isModifierKey(e.key)) {
        // Shift / Alt / Ctrl pressed or released mid-drag change the gesture at once (EDITOR_UX §2).
        if (ctx.dragging)
            ctx.dragging->refreshGesture();
        return true;
    }
    int mods = ctx.mods.bits();
    if (keyboard.handleKey(ctx, e))
        return true;

    // Space: tap toggles play, hold + drag pans. Z: tap zooms to the selection, hold is the zoom tool.
    // A release only counts when this key's own press started the hold: releasing Z after Ctrl+Z (with Ctrl
    // already up) must not zoom.
    if (e.key == GLFW_KEY_SPACE) {
        // A held key repeats, but Space's actions (play/stop, play from here) must run once per press.
        if (e.action == GLFW_REPEAT)
            return true;
        if (e.action == GLFW_PRESS && mods == 0) {
            ctx.spaceHeld = true;
            spaceUsed = false;
            return true;
        }
        if (e.action == GLFW_RELEASE && ctx.spaceHeld) {
            ctx.spaceHeld = false;
            if (!spaceUsed)
                runAction(Action::PlayStop);
            return true;
        }
    }
    if (e.key == GLFW_KEY_F1) {
        // Fill while held (EDITOR_UX §7).
        if (TesseraModule* m = ctx.module(); m && (e.action == GLFW_PRESS || e.action == GLFW_RELEASE))
            m->setEditorFill(e.action == GLFW_PRESS);
        return true;
    }
    if (e.keyName == "z") {
        if (e.action == GLFW_REPEAT && ctx.zoomToolHeld)
            return true; // the zoom tool is held, not pressed again
        if (e.action == GLFW_PRESS && mods == 0) {
            ctx.zoomToolHeld = true;
            ctx.zoomToolUsed = false;
            return true;
        }
        if (e.action == GLFW_RELEASE && ctx.zoomToolHeld) {
            ctx.zoomToolHeld = false;
            if (!ctx.zoomToolUsed)
                runAction(Action::ZoomToSelection);
            return true;
        }
    }

    if (e.action != GLFW_PRESS && e.action != GLFW_REPEAT)
        return true;
    // Keys act on the pane under the mouse, in both editors (Trello, 14 Oct): with focus left on the last
    // clicked pane, Ctrl+A then Ctrl+D over the notes selected and copied every track.
    if (EditorPane* hovered = paneUnderMouse())
        ctx.view().focus = hovered->pane();
    Scope scope = ctx.view().focus == Pane::Top && arrangerPane() ? Scope::TopPane : Scope::DetailPane;
    std::optional<Action> action = findAction({e.key, e.keyName, mods}, scope);
    if (!action)
        return false;
    runAction(*action);
    return true;
}

EditorPane* EditorHost::pickUnderMouse(std::initializer_list<EditorPane*> panes) {
    rack::math::Vec mouse = APP->scene->getMousePos();
    for (EditorPane* pane : panes) {
        bool showing = true;
        for (rack::widget::Widget* w = pane; w && showing; w = w->parent)
            showing = w->visible;
        if (!showing)
            continue;
        rack::math::Vec p = mouse.minus(pane->getAbsoluteOffset({})).div(pane->getAbsoluteZoom());
        if (rack::math::Rect(rack::math::Vec(), pane->box.size).contains(p))
            return pane;
    }
    return nullptr;
}

void EditorHost::claimKeyboard(rack::widget::Widget* root, bool always) {
    rack::widget::Widget* selected = APP->event->getSelectedWidget();
    // A text field keeps the keys while it is being typed in (renaming a track), and so does anything in an open
    // menu (a transform's settings panel takes Enter and Esc).
    if (selected == root || dynamic_cast<rack::ui::TextField*>(selected) ||
        (selected && selected->getAncestorOfType<rack::ui::MenuOverlay>()))
        return;
    if (always || (selected && selected->isDescendantOf(root)))
        APP->event->setSelectedWidget(root);
}

void EditorHost::trackHolds() {
    if (ctx.spaceHeld && ctx.dragging)
        spaceUsed = true;
}

void EditorHost::updateCursor(bool active, std::optional<CursorShape> override) {
    if (active || ctx.dragging) {
        applyCursor(override.value_or(ctx.cursor));
        cursorApplied = true;
    } else if (cursorApplied) {
        resetCursor();
        cursorApplied = false;
    }
}

void EditorHost::runAction(Action action) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    EditorState& state = ctx.state();
    ViewState& view = ctx.view();
    switch (action) {
    case Action::PlayStop:
        m->requestTransport(kRequestToggle);
        return;
    case Action::Record:
        m->toggleRecord();
        return;
    case Action::CaptureMidi:
        recordActions::capture(ctx);
        return;
    case Action::PlayFromMouse:
        if (ArrangerView* arranger = arrangerPane()) {
            double tick = arranger->tickUnderMouse();
            if (std::isfinite(tick))
                m->requestSeek(std::max(0.0, tick));
        }
        m->requestTransport(kRequestPlay);
        return;
    case Action::ToggleLoop:
        song.toggleLoop();
        return;
    case Action::ToggleFollow:
        state.follow = FollowMode((int(state.follow) + 1) % 3);
        return;
    case Action::Undo:
        APP->history->undo();
        return;
    case Action::Redo:
        APP->history->redo();
        return;
    case Action::SwitchPane:
        if (!arrangerPane())
            return;
        view.focus = view.focus == Pane::Top ? Pane::Detail : Pane::Top;
        if (view.maximized)
            view.maximized = view.focus;
        return;
    case Action::WidenClips:
        if (!arrangerPane())
            return;
        view.clipsWide = !view.clipsWide;
        if (view.maximized == Pane::Detail)
            view.maximized.reset();
        return;
    case Action::MaximizePane:
        if (!arrangerPane())
            return;
        if (view.maximized)
            view.maximized.reset();
        else
            view.maximized = view.focus;
        return;
    case Action::ToggleInspector:
        view.inspectorOpen = !view.inspectorOpen;
        return;
    case Action::ToggleDevices:
        deviceActions::setStripOpen(ctx, !deviceActions::stripOpen(ctx));
        return;
    case Action::ToolPointer:
        state.tool = Tool::Pointer;
        return;
    case Action::ToolDraw:
        state.tool = Tool::Draw;
        return;
    case Action::ToolPaint:
        state.tool = Tool::Paint;
        return;
    case Action::ToolErase:
        state.tool = Tool::Erase;
        return;
    case Action::ToolSlice:
        state.tool = Tool::Slice;
        return;
    case Action::ToolMute:
        state.tool = Tool::Mute;
        return;
    case Action::ToolVelocity:
        state.tool = Tool::Velocity;
        return;
    case Action::ToggleAudition:
        state.audition = !state.audition;
        return;
    case Action::GridNarrower:
        state.snap = narrower(state.snap);
        return;
    case Action::GridWider:
        state.snap = wider(state.snap);
        return;
    case Action::GridTriplet:
        state.snap = toggledTriplet(state.snap);
        return;
    case Action::SnapToggle:
        state.snap = toggledSnap(state.snap);
        return;
    case Action::TransformBox:
        state.transformBox = !state.transformBox;
        return;
    case Action::ToolRange:
        state.rangeTool = !state.rangeTool;
        return;
    case Action::ScaleFold:
        state.scaleFold = !state.scaleFold;
        return;
    case Action::NextTrack:
        ctx.selectTrackOffset(+1);
        return;
    case Action::PreviousTrack:
        ctx.selectTrackOffset(-1);
        return;
    case Action::MuteTracks:
        if (state.selectedTrack.valid())
            trackActions::toggleMute(ctx, state.selectedTrack);
        return;
    case Action::SoloTracks:
        if (state.selectedTrack.valid())
            trackActions::toggleSolo(ctx, state.selectedTrack, true);
        return;
    case Action::AddInstrumentTrack:
        trackActions::add(ctx, TrackType::Instrument);
        return;
    case Action::AddDrumTrack:
        trackActions::add(ctx, TrackType::Drum);
        return;
    case Action::GroupTracks:
        trackActions::groupSelected(ctx);
        return;
    case Action::AddMarker:
        if (ArrangerView* arranger = arrangerPane())
            arranger->handleAction(action);
        else
            song.addMarker(Tick(snapTick(m->playheadTicks(), ticksPerBeat(m->song().timeSig))));
        return;
    case Action::Help:
        ctx.showHelp = true;
        return;
    case Action::ExportMidi:
        exportMidi::openSong(ctx);
        return;
    case Action::KeyboardPlay:
        state.keyboardPlay = !state.keyboardPlay;
        if (!state.keyboardPlay)
            keyboard.releaseAll(ctx);
        return;
    case Action::StepInput:
        state.stepInput = !state.stepInput;
        if (state.stepInput)
            view.focus = Pane::Detail;
        return;
    case Action::Close:
        // First Esc cancels a drag, then leaves step input or keyboard play, then disarms a shape stamp, then
        // clears the selections (notes, clips, range, points); after that the surface decides (the overlay closes).
        if (ctx.dragging)
            ctx.dragging->cancelGesture();
        else if (ctx.mappingModulator || ctx.openModulator) {
            ctx.mappingModulator = 0;
            ctx.openModulator = 0;
        } else if (state.stepInput || state.keyboardPlay) {
            state.stepInput = false;
            state.keyboardPlay = false;
            keyboard.releaseAll(ctx);
        } else if (state.stampShape) {
            state.stampShape.reset();
        } else if (!state.selection.empty() || !state.clipSelection.empty() || !state.range.empty() ||
                   !state.autoSelection.empty()) {
            state.selection = NoteSelection{};
            state.clipSelection.clear();
            state.range = ArrangerRange{};
            state.autoSelection = AutoSelection{};
        } else {
            escape();
        }
        return;
    default:
        // View actions go to the focused pane. Anything not built yet is quietly ignored.
        if (EditorPane* pane = focusedPane())
            pane->handleAction(action);
        return;
    }
}

void EditorHost::drawReadout(NVGcontext* vg, rack::math::Vec bounds) {
    drawToast(vg, bounds);
    drawHelp(vg, bounds);
    if (ctx.readout.empty())
        return;
    const theme::Palette& c = theme::colors();
    draw::TextStyle style{theme::fonts::mono(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    float w = draw::textWidth(vg, style, ctx.readout) + 16;
    float h = 22;
    // Offset from the cursor so the pill never covers what is being dragged; keep it inside the surface.
    rack::math::Vec p =
        ctx.readoutPos.plus(rack::math::Vec(theme::metrics::kReadoutOffset, theme::metrics::kReadoutOffset));
    p.x = std::max(4.f, std::min(p.x, bounds.x - w - 4));
    p.y = std::max(4.f, std::min(p.y, bounds.y - h - 4));
    rack::math::Rect r(p, rack::math::Vec(w, h));
    draw::roundedRect(vg, r, h / 2, nvgTransRGBAf(c.bg2, 0.95f));
    draw::roundedRectStroke(vg, r, h / 2, c.line2, 1);
    draw::text(vg, style, p.x + 8, p.y + h / 2, ctx.readout);
}

void EditorHost::drawHelp(NVGcontext* vg, rack::math::Vec bounds) {
    if (!ctx.showHelp)
        return;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, bounds.x, bounds.y);
    nvgFillColor(vg, nvgTransRGBAf(c.bg0, 0.95f));
    nvgFill(vg);

    // Groups in table order, each a heading and "label .... keys" rows, flowed into as many columns as fit.
    struct Row {
        std::string label, keys;
        bool heading;
    };
    std::vector<Row> rows;
    // Each group once, in the order groups first appear in the table.
    std::vector<std::string> groups;
    for (const ActionInfo& info : actionTable())
        if (std::find(groups.begin(), groups.end(), info.group) == groups.end())
            groups.push_back(info.group);
    for (const std::string& group : groups) {
        rows.push_back({group, "", true});
        for (const ActionInfo& info : actionTable()) {
            if (info.chords.empty() || info.group != group)
                continue;
            std::string keys;
            for (const KeyChord& chord : info.chords)
                keys += (keys.empty() ? "" : " / ") + chordLabel(chord) + (chord.hold ? " (hold)" : "");
            rows.push_back({info.label, keys, false});
        }
    }
    static const char* const kMouse[][2] = {
        {"Bypass snap while dragging", "Alt"},
        {"One axis, or fine values", "Shift + drag"},
        {"Duplicate what you drag", "Ctrl/Cmd + drag"},
        {"Add to / toggle in the selection", "Shift / Ctrl/Cmd + click"},
        {"Velocity of a note or clip", "Alt + drag its body"},
        {"Open, or create", "Double-click"},
        {"Cancel a drag", "Esc"},
    };
    rows.push_back({"Mouse", "", true});
    for (const auto& m : kMouse)
        rows.push_back({m[0], m[1], false});

    const float rowH = 16.5f, colW = 300, top = 60, margin = 24;
    int perColumn = std::max(4, int((bounds.y - top - margin) / rowH));
    int columns = int((rows.size() + size_t(perColumn) - 1) / size_t(perColumn));
    float total = float(columns) * colW;
    float left = std::max(margin, (bounds.x - total) / 2);
    draw::TextStyle title{theme::fonts::uiStrong(), 18, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::text(vg, title, left, 32, "Shortcuts");
    draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    draw::text(vg, hint, std::min(bounds.x - margin, left + total - 16), 32, "Press any key or click to close");
    draw::TextStyle heading{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.accent,
                            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme::metrics::kPanelLabelSpacing};
    draw::TextStyle label{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle keys{theme::fonts::mono(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    for (size_t i = 0; i < rows.size(); i++) {
        float x = left + float(i / size_t(perColumn)) * colW;
        float y = top + float(i % size_t(perColumn)) * rowH;
        if (x + colW > bounds.x + colW / 2)
            break; // narrower than all the columns: the rest is cut off rather than overlapping
        const Row& r = rows[i];
        if (r.heading) {
            std::string upper = r.label;
            for (char& ch : upper)
                ch = char(std::toupper(static_cast<unsigned char>(ch)));
            draw::text(vg, heading, x, y + 3, upper);
            continue;
        }
        float keyWidth = std::min(130.f, draw::textWidth(vg, keys, r.keys));
        draw::text(vg, keys, x + colW - 16, y, draw::ellipsize(vg, keys, r.keys, 130));
        draw::text(vg, label, x, y, draw::ellipsize(vg, label, r.label, colW - 28 - keyWidth));
    }
}

void EditorHost::drawToast(NVGcontext* vg, rack::math::Vec bounds) {
    if (ctx.toastText.empty() || EditorContext::now() > ctx.toastUntil)
        return;
    const theme::Palette& c = theme::colors();
    draw::TextStyle style{theme::fonts::ui(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::string text = draw::ellipsize(vg, style, ctx.toastText, bounds.x - 60);
    float w = draw::textWidth(vg, style, text) + 24;
    float h = 28;
    rack::math::Rect r((bounds.x - w) / 2, bounds.y - h - 24, w, h);
    draw::roundedRect(vg, r, h / 2, nvgTransRGBAf(c.bg2, 0.97f));
    draw::roundedRectStroke(vg, r, h / 2, c.play, 1);
    draw::text(vg, style, r.pos.x + 12, r.getCenter().y, text);
}

} // namespace tessera
