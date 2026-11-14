#include "DetailPane.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/DeviceActions.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../widgets/ToolButton.hpp"
#include "DeviceStrip.hpp"
#include "MixerPane.hpp"
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
    // In a short pane the devices take the notes' place, so Devices is a third tab.
    EditorContext* c = &ctx;
    ToolButton::Options o;
    o.text = "Devices";
    o.tooltip = withShortcut("Show the track's devices in this pane", Action::ToggleDevices);
    o.action = [c] {
        c->state().mixerOpen = false;
        c->state().focus = Pane::Detail;
        deviceActions::setStripOpen(*c, true);
    };
    o.active = [this, c] { return c->module() && !showsMixer() && deviceActions::stripOpen(*c); };
    devicesTab = new ToolButton(o);
    devicesTab->visible = false;
    addChild(devicesTab);
}

void DetailPane::reserveHeader(float px) {
    roll->headerReserve = px;
    rightReserve = px;
}

namespace {
constexpr float kSwitchWidth = 46;
} // namespace

void DetailPane::setMixer(MixerPane* m) {
    mixer = m;
    addChild(mixer);
    // The switch: the same pane shows the notes or the mixer, never both (one window language).
    EditorContext* c = &ctx;
    for (bool toMixer : {false, true}) {
        ToolButton::Options o;
        o.text = toMixer ? "Mixer" : "Notes";
        o.tooltip = withShortcut(toMixer ? "Show the mixer in this pane" : "Show the selected clip in this pane",
                                 Action::ToggleMixer);
        o.action = [this, c, toMixer] {
            c->state().mixerOpen = toMixer;
            c->state().focus = Pane::Detail;
            // In a short pane the devices fill it: Notes goes back to the notes.
            if (!toMixer && shortPane && c->module())
                deviceActions::setStripOpen(*c, false);
        };
        o.active = [this, c, toMixer] {
            if (toMixer)
                return c->state().mixerOpen;
            return !c->state().mixerOpen && !(shortPane && c->module() && deviceActions::stripOpen(*c));
        };
        auto* b = new ToolButton(o);
        addChild(b);
        (toMixer ? mixerTab : notesTab) = b;
    }
}

bool DetailPane::showsMixer() const { return mixer && mixer->hasSound() && ctx.state().mixerOpen; }

void DetailPane::onButton(const ButtonEvent& e) {
    // Delete and Ctrl+D act on the device cards only right after a click in the strip.
    if (e.action == GLFW_PRESS && ctx.module()) {
        stripFocus = strip->visible && strip->box.contains(e.pos);
        roll->notePress(e.pos);
    }
    EditorPane::onButton(e);
}

NoteTarget* DetailPane::noteTarget() { return roll; }

bool DetailPane::handleAction(Action action) {
    if (showsMixer())
        return false;
    if (stripFocus && strip->handleAction(action))
        return true;
    return roll->handleAction(action);
}

void DetailPane::layout() {
    // The strip never runs past the pane: a module's drawing isn't clipped, it would cover the rack below.
    float stripHeight = ctx.module() ? strip->wantedHeight() : 0;
    float room = std::max(0.f, box.size.y - theme::metrics::kPaneHeader);
    strip->paneRoom = room;
    shortPane = room < DeviceStrip::kOpenHeight + DeviceStrip::kMinNotes;
    // Open with too little room left for the notes, the devices take the whole pane: the Devices button then
    // switches between devices and notes.
    if (ctx.module() && deviceActions::stripOpen(ctx) && room - stripHeight < DeviceStrip::kMinNotes)
        stripHeight = room;
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
    // With something that makes sound, the header starts with the Notes | Mixer switch.
    // In a short pane Devices joins them, wider for its count.
    bool sound = mixer && mixer->hasSound();
    bool tabs = sound || shortPane;
    float h = theme::metrics::kPaneHeader - 6, tx = 4;
    if (notesTab) {
        notesTab->visible = tabs;
        mixerTab->visible = sound;
        if (tabs) {
            notesTab->box = rack::math::Rect(tx, 3, kSwitchWidth, h);
            tx += kSwitchWidth + 2;
        }
        if (sound) {
            mixerTab->box = rack::math::Rect(tx, 3, kSwitchWidth, h);
            tx += kSwitchWidth + 2;
        }
    }
    devicesTab->visible = shortPane && ctx.module();
    if (devicesTab->visible) {
        const Track* t = deviceActions::track(ctx);
        size_t n = t ? t->devices.size() : 0;
        devicesTab->setText(n > 0 ? "Devices " + std::to_string(n) : "Devices");
        devicesTab->box = rack::math::Rect(tx, 3, kSwitchWidth + 22, h);
        tx += kSwitchWidth + 24;
    }
    float switchRoom = tabs ? tx + 6 : 0;
    roll->headerLeft = switchRoom;
    roll->devicesInTabs = devicesTab->visible;
    roll->layout();
    if (mixer) {
        bool show = showsMixer();
        mixer->visible = show;
        mixer->reserveTab(switchRoom, rightReserve);
        if (show) {
            roll->visible = strip->visible = popover->visible = false;
            mixer->box = rack::math::Rect(rack::math::Vec(), box.size);
        } else {
            roll->visible = true;
        }
    }
}

void DetailPane::step() {
    if (ctx.module())
        ctx.state().closeCurveIfTrackChanged();
    if (notesTab) {
        TesseraModule* m = ctx.module();
        const Track* t = m ? m->song().findTrack(ctx.state().selectedTrack) : nullptr;
        notesTab->setText(t && t->type == TrackType::Audio ? "Audio" : "Notes");
    }
    layout();
    EditorPane::step();
}

} // namespace tessera
