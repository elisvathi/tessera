#include "DetailPane.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "DeviceStrip.hpp"
#include "DrumEditor.hpp"
#include "ModulatorPopover.hpp"
#include "PianoRoll.hpp"

namespace tessera {

DetailPane::DetailPane(EditorContext& ctx) : EditorPane(ctx, Pane::Detail) {
    roll = new PianoRollView(ctx);
    drums = new DrumEditorView(ctx);
    addChild(roll);
    addChild(drums);
    strip = new DeviceStrip(ctx);
    addChild(strip); // after the editors: it sits over their top
    popover = new ModulatorPopover(ctx);
    addChild(popover); // last: it floats over the notes
}

void DetailPane::reserveHeader(float px) {
    roll->headerReserve = px;
    drums->headerReserve = px;
}

void DetailPane::onButton(const ButtonEvent& e) {
    // Delete and Ctrl+D act on the device cards only right after a click in the strip.
    if (e.action == GLFW_PRESS && ctx.module())
        stripFocus = strip->visible && strip->box.contains(e.pos);
    EditorPane::onButton(e);
}

bool DetailPane::drumTrackSelected() {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(ctx.state().selectedTrack) : nullptr;
    return t && t->type == TrackType::Drum;
}

EditorPane* DetailPane::current() {
    bool steps = ctx.module() && drumTrackSelected() && ctx.view().drumSteps;
    return steps ? static_cast<EditorPane*>(drums) : roll;
}

NoteTarget* DetailPane::noteTarget() {
    if (current() == drums)
        return drums;
    return roll;
}

bool DetailPane::handleAction(Action action) {
    if (action == Action::ToggleDrumView) {
        if (!drumTrackSelected())
            return false;
        ctx.view().drumSteps = !ctx.view().drumSteps;
        return true;
    }
    if (stripFocus && strip->handleAction(action))
        return true;
    return current()->handleAction(action);
}

void DetailPane::layout() {
    EditorPane* shown = current();
    // The strip never runs past the pane: a module's drawing isn't clipped, it would cover the rack below.
    float stripHeight = ctx.module() ? strip->wantedHeight() : 0;
    float room = std::max(0.f, box.size.y - theme::metrics::kPaneHeader);
    if (stripHeight > room)
        stripHeight = room >= DeviceStrip::kMinOpenHeight ? room : std::min(DeviceStrip::kClosedHeight, room);
    strip->roomBelow = room - stripHeight;
    for (EditorPane* pane : {static_cast<EditorPane*>(roll), static_cast<EditorPane*>(drums)}) {
        pane->visible = pane == shown;
        pane->box = rack::math::Rect(rack::math::Vec(), box.size);
        pane->topInset = stripHeight;
    }
    strip->visible = stripHeight > 0;
    strip->box = rack::math::Rect(0, theme::metrics::kPaneHeader, box.size.x, stripHeight);
    // The popover hangs from the modulator list. In a short pane (the in-panel editor) it moves up to fit, beside
    // the list so the list stays clickable, and scrolls when even the whole pane is too short.
    float want = ctx.module() && deviceActions::stripOpen(ctx) && stripHeight >= DeviceStrip::kMinOpenHeight
                     ? popover->wantedHeight()
                     : 0;
    float top = theme::metrics::kPaneHeader + 2;
    float height = std::min(want, box.size.y - top - 2);
    float below = theme::metrics::kPaneHeader + stripHeight - 6;
    float y = std::max(top, std::min(below, box.size.y - height - 2));
    float x = y < below ? DeviceStrip::kListRight + 6 : 8;
    popover->visible = height > 40;
    popover->box = rack::math::Rect(x, y, ModulatorPopover::kWidth, height);
    shown->layout();
}

void DetailPane::step() {
    layout();
    EditorPane::step();
}

} // namespace tessera
