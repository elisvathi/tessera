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
    // One switch at the left of the header in both windows: Notes, Devices, then Mixer once something makes sound.
    EditorContext* c = &ctx;
    ToolButton::Options o;
    o.text = "Devices";
    o.tooltip = withShortcut("The track's devices: above its notes, or in their place when the pane is short",
                             Action::ToggleDevices);
    o.action = [this] { toggleDevices(); };
    o.active = [this, c] { return c->module() && !showsMixer() && deviceActions::stripOpen(*c); };
    o.enabled = [this] { return !audioTrack(); };
    devicesTab = new ToolButton(o);
    addChild(devicesTab);
    // A narrow pane folds the switch into one button that names what shows.
    ToolButton::Options folded;
    folded.caret = true;
    folded.tooltip = "What this pane shows: the notes, the track's devices or the mixer";
    folded.action = [this, c] {
        rack::ui::Menu* menu = rack::createMenu();
        menu->addChild(rack::createMenuLabel("Show"));
        bool audio = audioTrack();
        menu->addChild(rack::createCheckMenuItem(
            audio ? "Audio" : "Notes", "", [this, c] { return !showsMixer() && !c->devicesFillPane; },
            [this, c] {
                c->state().mixerOpen = false;
                if (shortPane && c->module())
                    deviceActions::setStripOpen(*c, false);
            }));
        if (!audio)
            menu->addChild(rack::createCheckMenuItem(
                devicesTab->getText(), shortcutLabel(Action::ToggleDevices),
                [this, c] { return !showsMixer() && c->module() && deviceActions::stripOpen(*c); },
                [this] { toggleDevices(); }));
        if (mixer && mixer->hasSound())
            menu->addChild(rack::createCheckMenuItem(
                "Mixer", shortcutLabel(Action::ToggleMixer), [this] { return showsMixer(); },
                [c] { c->state().mixerOpen = !c->state().mixerOpen; }));
    };
    foldedTab = new ToolButton(folded);
    addChild(foldedTab);
}

void DetailPane::toggleDevices() {
    if (!ctx.module())
        return;
    // A second click, with the devices showing, closes them.
    bool showing = !showsMixer() && deviceActions::stripOpen(ctx);
    ctx.state().mixerOpen = false;
    ctx.state().focus = Pane::Detail;
    deviceActions::setStripOpen(ctx, !showing);
}

bool DetailPane::audioTrack() const {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(ctx.state().selectedTrack) : nullptr;
    return t && t->type == TrackType::Audio;
}

void DetailPane::reserveHeader(float px) {
    roll->headerReserve = px;
    rightReserve = px;
}

namespace {
constexpr float kSwitchWidth = 46;
constexpr float kFoldedWidth = 74;
// Below this much room for the rest of the header (clip chip and tools), the switch folds into one button.
constexpr float kFoldBelowRest = 300;
} // namespace

void DetailPane::setMixer(MixerPane* m) {
    mixer = m;
    addChild(mixer);
    // The switch sits over the mixer's header.
    for (ToolButton* b : {devicesTab, foldedTab}) {
        removeChild(b);
        addChild(b);
    }
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
    float room = std::max(0.f, box.size.y - theme::metrics::kPaneHeader);
    strip->paneRoom = room;
    // Audio tracks have no notes for devices to change.
    bool devices = ctx.module() && !audioTrack();
    float stripHeight = devices ? strip->wantedHeight() : 0;
    shortPane = room < DeviceStrip::kOpenHeight + DeviceStrip::kMinNotes;
    // Open with too little room left for the notes, the devices take the whole pane: the Devices button then
    // switches between devices and notes.
    ctx.devicesFillPane = false;
    if (devices && deviceActions::stripOpen(ctx) && room - stripHeight < DeviceStrip::kMinNotes) {
        stripHeight = room;
        ctx.devicesFillPane = !showsMixer();
    }
    if (stripHeight > room)
        stripHeight = room >= DeviceStrip::kMinOpenHeight ? room : std::min(DeviceStrip::kClosedHeight, room);
    strip->roomBelow = room - stripHeight;
    roll->box = rack::math::Rect(rack::math::Vec(), box.size);
    roll->topInset = stripHeight;
    strip->visible = stripHeight > 0;
    strip->box = rack::math::Rect(0, theme::metrics::kPaneHeader, box.size.x, stripHeight);
    // The open modulator's settings sit right of its section, inside the strip (docs/DECISIONS.md §163): the strip
    // made room for them. Settings on a card that scrolled under the part left of the chain are cut there.
    std::optional<rack::math::Rect> settings;
    if (devices && deviceActions::stripOpen(ctx) && stripHeight >= DeviceStrip::kMinOpenHeight &&
        popover->wantedHeight() > 0)
        settings = strip->settingsRect();
    popover->visible = false;
    if (settings) {
        rack::math::Rect r = *settings;
        r.pos = r.pos.plus(strip->box.pos);
        r.size.y = std::min(r.size.y, strip->box.getBottom() - r.pos.y);
        float cut = strip->box.pos.x + strip->chainStart();
        bool inChain = settings->pos.x >= strip->chainStart() - 1;
        popover->clipLeft = inChain ? std::max(0.f, cut - r.pos.x) : 0;
        popover->clipRight = std::max(0.f, r.getRight() - strip->box.getRight());
        popover->visible = r.size.y > 40 && popover->clipLeft < r.size.x - 20 && r.pos.x < strip->box.getRight() - 20;
        popover->box = r;
    }
    // The header starts with the switch: Notes, Devices, and Mixer once something makes sound. In a narrow pane
    // it folds into one button that names what shows.
    bool sound = mixer && mixer->hasSound();
    bool tabs = ctx.module() && notesTab;
    float h = theme::metrics::kPaneHeader - 6, tx = 4;
    const std::vector<Device>* shown = ctx.module() ? &deviceActions::shownDevices(ctx) : nullptr;
    size_t n = shown && !audioTrack() ? shown->size() : 0;
    devicesTab->setText(n > 0 ? "Devices " + std::to_string(n) : "Devices");
    float devicesWidth = kSwitchWidth + (n > 0 ? 22 : 12);
    float full = kSwitchWidth + 2 + devicesWidth + 2 + (sound ? kSwitchWidth + 2 : 0);
    bool fold = tabs && box.size.x < full + kFoldBelowRest;
    if (notesTab) {
        notesTab->visible = tabs && !fold;
        mixerTab->visible = tabs && !fold && sound;
    }
    devicesTab->visible = tabs && !fold;
    foldedTab->visible = fold;
    if (fold) {
        std::string what = showsMixer() ? "Mixer" : ctx.devicesFillPane ? "Devices" : audioTrack() ? "Audio" : "Notes";
        foldedTab->setText(what);
        foldedTab->box = rack::math::Rect(tx, 3, kFoldedWidth, h);
        tx += kFoldedWidth + 2;
    } else if (tabs) {
        notesTab->box = rack::math::Rect(tx, 3, kSwitchWidth, h);
        tx += kSwitchWidth + 2;
        devicesTab->box = rack::math::Rect(tx, 3, devicesWidth, h);
        tx += devicesWidth + 2;
        if (sound) {
            mixerTab->box = rack::math::Rect(tx, 3, kSwitchWidth, h);
            tx += kSwitchWidth + 2;
        }
    }
    float switchRoom = tabs ? tx + 6 : 0;
    roll->headerLeft = switchRoom;
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
