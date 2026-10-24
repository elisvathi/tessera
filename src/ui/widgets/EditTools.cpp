#include "EditTools.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../editing/DeviceActions.hpp"
#include "../editing/RecordActions.hpp"
#include "../interaction/Shortcuts.hpp"
#include "Draw.hpp"

namespace tessera {

namespace {

struct ToolInfo {
    Icon icon;
    const char* name;
    Action action;
};

ToolInfo toolInfo(Tool tool) {
    switch (tool) {
    case Tool::Pointer:
        return {Icon::Pointer, "Pointer", Action::ToolPointer};
    case Tool::Draw:
        return {Icon::Pencil, "Draw", Action::ToolDraw};
    case Tool::Paint:
        return {Icon::Brush, "Paint", Action::ToolPaint};
    case Tool::Erase:
        return {Icon::Eraser, "Erase (deletes the selection when there is one)", Action::ToolErase};
    case Tool::Slice:
        return {Icon::Scissors, "Slice", Action::ToolSlice};
    case Tool::Mute:
        return {Icon::MuteNote, "Mute", Action::ToolMute};
    case Tool::Velocity:
    default:
        return {Icon::Velocity, "Velocity", Action::ToolVelocity};
    }
}

/// The tool's name as a menu item says it (the button tooltip may add a hint).
const char* toolName(Tool tool) {
    switch (tool) {
    case Tool::Pointer:
        return "Select";
    case Tool::Draw:
        return "Draw";
    case Tool::Paint:
        return "Paint";
    case Tool::Erase:
        return "Erase";
    case Tool::Slice:
        return "Slice";
    case Tool::Mute:
        return "Mute";
    case Tool::Velocity:
    default:
        return "Velocity";
    }
}

} // namespace

ToolSwitch makeToolSwitch(EditorContext& ctx, const std::vector<Tool>& menuTools,
                          std::function<void(rack::ui::Menu*)> extras, std::function<Icon()> extraIcon,
                          std::function<Tool(Tool)> acting, std::function<bool()> deleteSelection) {
    EditorContext* c = &ctx;
    std::vector<ToolButton*> pair = makeToolButtons(ctx, {Tool::Pointer, Tool::Draw}, 18, nullptr, acting);
    auto picked = [c, menuTools, extraIcon, acting] {
        if (extraIcon) {
            Icon icon = extraIcon();
            if (icon != Icon::None)
                return icon;
        }
        if (!c->module())
            return Icon::None;
        Tool tool = acting ? acting(c->state().tool) : c->state().tool;
        for (Tool t : menuTools)
            if (t == tool)
                return toolInfo(t).icon;
        return Icon::None;
    };
    ToolButton::Options o;
    o.caret = true;
    o.tooltip = "More tools";
    o.dynamicIcon = picked;
    o.active = [picked] { return picked() != Icon::None; };
    o.action = [c, menuTools, extras, deleteSelection] {
        rack::ui::Menu* menu = rack::createMenu();
        menu->addChild(rack::createMenuLabel("Tools"));
        for (Tool t : menuTools)
            menu->addChild(rack::createCheckMenuItem(
                toolName(t), shortcutLabel(toolInfo(t).action), [c, t] { return c->module() && c->state().tool == t; },
                [c, t, deleteSelection] {
                    // As the Erase button did: with a selection, Erase deletes it and the tool stays.
                    if (t == Tool::Erase && deleteSelection && deleteSelection())
                        return;
                    c->state().tool = t;
                }));
        if (extras)
            extras(menu);
    };
    return {pair[0], pair[1], new ToolButton(o)};
}

float toolSwitchCaretWidth(const ToolSwitch& s, float size) { return s.more->isShowingIcon() ? size + 8 : 13; }

std::vector<ToolButton*> makeToolButtons(EditorContext& ctx, const std::vector<Tool>& tools, float size,
                                         std::function<bool()> deleteSelection, std::function<Tool(Tool)> acting) {
    EditorContext* c = &ctx;
    std::vector<ToolButton*> out;
    for (Tool tool : tools) {
        ToolInfo info = toolInfo(tool);
        ToolButton::Options o;
        o.icon = info.icon;
        o.tooltip = withShortcut(info.name, info.action);
        o.action = [c, tool, deleteSelection] {
            if (!c->module())
                return;
            if (tool == Tool::Erase && deleteSelection && deleteSelection())
                return;
            c->state().tool = tool;
        };
        o.active = [c, tool, acting] {
            if (!c->module())
                return false;
            Tool chosen = c->state().tool;
            return (acting ? acting(chosen) : chosen) == tool;
        };
        auto* b = new ToolButton(o);
        b->box.size = rack::math::Vec(size, size);
        out.push_back(b);
    }
    return out;
}

float fitToolbar(const std::vector<ToolbarItem>& items, float right, float limit, float y, float height,
                 const std::function<void(rack::widget::Widget*, rack::math::Rect)>& place) {
    std::vector<bool> shown(items.size(), true);
    float need = 0;
    for (const ToolbarItem& it : items)
        need += it.width + it.gap;
    while (right - need < limit) {
        int drop = -1;
        for (int i = int(items.size()) - 1; i >= 0; i--)
            if (shown[size_t(i)] && (drop < 0 || items[size_t(i)].keep < items[size_t(drop)].keep))
                drop = i;
        if (drop < 0)
            break;
        shown[size_t(drop)] = false;
        need -= items[size_t(drop)].width + items[size_t(drop)].gap;
    }
    float x = right, left = right;
    for (size_t i = 0; i < items.size(); i++) {
        const ToolbarItem& it = items[i];
        it.widget->visible = shown[i];
        if (!shown[i])
            continue;
        x -= it.width;
        rack::math::Rect r(x, y, it.width, height);
        if (place)
            place(it.widget, r);
        else
            it.widget->box = r;
        left = x;
        x -= it.gap;
    }
    return left;
}

std::vector<ToolButton*> makeTransportButtons(EditorContext& ctx) {
    EditorContext* c = &ctx;
    auto options = [](Icon icon, const std::string& tooltip, std::function<void()> action) {
        ToolButton::Options o;
        o.icon = icon;
        o.tooltip = tooltip;
        o.action = std::move(action);
        return o;
    };
    std::vector<ToolButton*> out;
    auto play = options(Icon::Play, withShortcut("Play", Action::PlayStop),
                        [c] { c->module()->requestTransport(kRequestPlay); });
    play.active = [c] { return c->module() && c->module()->playing(); };
    play.activeColor = &theme::Palette::play;
    out.push_back(new ToolButton(play));

    // Stop while playing stops; stop while stopped returns to the start.
    out.push_back(new ToolButton(options(Icon::Stop, withShortcut("Stop", Action::PlayStop), [c] {
        c->module()->requestTransport(c->module()->playing() ? kRequestStop : kRequestReset);
    })));

    auto record = options(
        Icon::Record,
        withShortcut("Record armed tracks. Right-click: count-in, quantize, capture, MIDI input", Action::Record),
        [c] { c->module()->toggleRecord(); });
    // Lit while recording, blinking through the count-in.
    record.active = [c] {
        TesseraModule* m = c->module();
        return m && (m->recording() || (m->countingIn() && std::fmod(EditorContext::now(), 0.5) < 0.25));
    };
    record.rightClick = [c] { recordActions::openMenu(*c); };
    record.activeColor = &theme::Palette::record;
    out.push_back(new ToolButton(record));

    auto loop = options(Icon::Loop, withShortcut("Loop", Action::ToggleLoop), [c] {
        TesseraModule* m = c->module();
        bool on = !m->loopEnabled();
        m->setParamWithUndo(TesseraModule::LOOP_PARAM, on ? 1.f : 0.f, on ? "Loop on" : "Loop off");
    });
    loop.active = [c] { return c->module() && c->module()->loopEnabled(); };
    out.push_back(new ToolButton(loop));
    return out;
}

ToolButton* makeClipChip(EditorContext& ctx, std::function<TrackId()> track, std::function<ClipId()> clip) {
    EditorContext* c = &ctx;
    ToolButton::Options o;
    o.caret = true;
    o.tooltip = "This clip: rename, length, loop, save or export";
    o.swatch = [c] {
        const Track* t = c->module() ? c->module()->song().findTrack(c->state().selectedTrack) : nullptr;
        return t ? theme::track(t->color) : nvgRGBAf(0, 0, 0, 0);
    };
    o.action = [c, track, clip] { clipActions::openChipMenu(*c, track(), clip()); };
    return new ToolButton(o);
}

void placeClipChip(EditorContext& ctx, ToolButton* chip, ClipId clip, bool shortText, float paneWidth) {
    constexpr float kMaxWidth = 280;
    std::string text = clipActions::chipText(ctx, clip, shortText);
    chip->setText(text);
    float width = kMaxWidth;
    if (rack::window::Window* w = APP->window) {
        draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kTextUi, theme::colors().text0,
                              NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        width = draw::textWidth(w->vg, style, text) + 21 + 18; // the swatch on the left, the caret on the right
    }
    float room = std::max(60.f, paneWidth * 0.4f);
    float header = theme::metrics::kPaneHeader;
    chip->box = rack::math::Rect(6, 3, std::min({width, kMaxWidth, room}), header - 6);
}

ToolButton* makeDevicesToggle(EditorContext& ctx) {
    EditorContext* c = &ctx;
    ToolButton::Options o;
    o.text = "Devices";
    o.tooltip = withShortcut("The track's devices, above its notes", Action::ToggleDevices);
    o.action = [c] { deviceActions::setStripOpen(*c, !deviceActions::stripOpen(*c)); };
    o.active = [c] { return c->module() && deviceActions::stripOpen(*c); };
    o.filled = true;
    return new ToolButton(o);
}

void updateDevicesToggle(EditorContext& ctx, ToolButton* button) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(ctx.state().selectedTrack) : nullptr;
    size_t n = t ? t->devices.size() : 0;
    button->setText(n > 0 ? "Devices " + std::to_string(n) : "Devices");
}

namespace {

/// Shows Restore while its pane is maximized.
class MaximizeButton : public ToolButton {
  public:
    MaximizeButton(EditorContext& ctx, Options o) : ToolButton(std::move(o)), ctx(ctx) {}
    void step() override {
        setIcon(ctx.module() && ctx.view().maximized ? Icon::Restore : Icon::Maximize);
        ToolButton::step();
    }

  private:
    EditorContext& ctx;
};

} // namespace

ToolButton* makeMaximizeButton(EditorContext& ctx, Pane pane) {
    EditorContext* c = &ctx;
    ToolButton::Options o;
    o.icon = Icon::Maximize;
    o.tooltip = withShortcut(pane == Pane::Top ? "Maximize the song, or restore both panes"
                                               : "Maximize the notes, or restore both panes",
                             Action::MaximizePane);
    o.action = [c, pane] {
        ViewState& v = c->view();
        if (v.maximized)
            v.maximized.reset();
        else
            v.maximized = pane;
        v.focus = pane;
    };
    o.active = [c] { return c->module() && c->view().maximized.has_value(); };
    auto* b = new MaximizeButton(ctx, o);
    b->box.size = rack::math::Vec(kMaximizeButton, kMaximizeButton);
    return b;
}

} // namespace tessera
