#include "DetailPane.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "DeviceStrip.hpp"
#include "ModulatorPopover.hpp"
#include "PianoRoll.hpp"

namespace tessera {

DetailPane::DetailPane(EditorContext& ctx) : EditorPane(ctx, Pane::Detail) {
    roll = new PianoRollView(ctx);
    addChild(roll);
    strip = new DeviceStrip(ctx);
    addChild(strip); // after the editor: it sits over its top
    popover = new ModulatorPopover(ctx);
    addChild(popover); // last: it floats over the notes
}

void DetailPane::reserveHeader(float px) { roll->headerReserve = px; }

void DetailPane::onButton(const ButtonEvent& e) {
    // Delete and Ctrl+D act on the device cards only right after a click in the strip.
    if (e.action == GLFW_PRESS && ctx.module())
        stripFocus = strip->visible && strip->box.contains(e.pos);
    EditorPane::onButton(e);
}

NoteTarget* DetailPane::noteTarget() { return roll; }

bool DetailPane::handleAction(Action action) {
    if (stripFocus && strip->handleAction(action))
        return true;
    return roll->handleAction(action);
}

void DetailPane::layout() {
    // The strip never runs past the pane: a module's drawing isn't clipped, it would cover the rack below.
    float stripHeight = ctx.module() ? strip->wantedHeight() : 0;
    float room = std::max(0.f, box.size.y - theme::metrics::kPaneHeader);
    if (stripHeight > room)
        stripHeight = room >= DeviceStrip::kMinOpenHeight ? room : std::min(DeviceStrip::kClosedHeight, room);
    strip->roomBelow = room - stripHeight;
    roll->box = rack::math::Rect(rack::math::Vec(), box.size);
    roll->topInset = stripHeight;
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
    roll->layout();
}

void DetailPane::step() {
    layout();
    EditorPane::step();
}

} // namespace tessera
