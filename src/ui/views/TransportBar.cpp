#include "TransportBar.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ExportMidi.hpp"
#include "../editing/KeyActions.hpp"
#include "../editing/ProjectFile.hpp"
#include "../editing/RecordActions.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/HoverTooltip.hpp"
#include "../widgets/EditTools.hpp"
#include "../widgets/ToolButton.hpp"
#include "TimelineDraw.hpp"

namespace tessera {

namespace {

constexpr float kButton = 28;
constexpr float kGap = 4;
constexpr float kGroupGap = 14;
constexpr float kBpmPerPx = 0.25f;
constexpr float kFineFactor = 0.1f;

/// An empty widget used as a fixed-width slot in the bar's layout.
struct Spacer : rack::widget::Widget {
    explicit Spacer(float width) { box.size.x = width; }
};

/// Tempo readout you drag up or down to change. One undo step per drag.
class BpmField : public rack::widget::OpaqueWidget {
  public:
    explicit BpmField(EditorContext& ctx) : ctx(ctx), tip("Tempo: drag up or down (Shift: fine)") {}

    void draw(const DrawArgs& args) override {
        const theme::Palette& c = theme::colors();
        TesseraModule* m = ctx.module();
        if (!m)
            return;
        NVGcolor bg = dragging ? c.line0 : hovered ? c.bg2 : nvgRGBAf(0, 0, 0, 0);
        draw::roundedRect(args.vg, rack::math::Rect(rack::math::Vec(), box.size), theme::metrics::kRadiusControl, bg);
        char text[24];
        // With a clock patched the tempo is the clock's ("EXT 120.3") and can't be dragged.
        if (m->externalClock())
            snprintf(text, sizeof(text), "EXT %.1f", m->tempo());
        else
            snprintf(text, sizeof(text), "%.2f", m->params[TesseraModule::BPM_PARAM].getValue());
        draw::TextStyle value{theme::fonts::mono(), 15, m->externalClock() ? c.accent : c.text0,
                              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
        draw::text(args.vg, value, box.size.x - 34, box.size.y / 2, text);
        draw::TextStyle unit{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text2,
                             NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(args.vg, unit, box.size.x - 30, box.size.y / 2 + 1, "BPM");
    }
    void step() override {
        tip.step();
        OpaqueWidget::step();
    }
    void onEnter(const EnterEvent& e) override {
        hovered = true;
        tip.enter();
    }
    void onLeave(const LeaveEvent& e) override {
        hovered = false;
        tip.leave();
    }
    void onHover(const HoverEvent& e) override {
        OpaqueWidget::onHover(e);
        ctx.cursor = CursorShape::ResizeV;
    }
    void onDragStart(const DragStartEvent& e) override {
        if (TesseraModule* m = ctx.module(); m && !m->externalClock()) {
            dragging = true;
            startValue = value = m->params[TesseraModule::BPM_PARAM].getValue();
        }
    }
    void onDragMove(const DragMoveEvent& e) override {
        TesseraModule* m = ctx.module();
        if (!dragging || !m)
            return;
        float dy = e.mouseDelta.y / getAbsoluteZoom();
        bool fine = APP->window->getMods() & GLFW_MOD_SHIFT;
        value -= dy * kBpmPerPx * (fine ? kFineFactor : 1.f);
        value = rack::math::clamp(value, float(Engine::kMinBpm), float(Engine::kMaxBpm));
        m->params[TesseraModule::BPM_PARAM].setValue(value);
    }
    void onDragEnd(const DragEndEvent& e) override {
        if (TesseraModule* m = ctx.module(); m && dragging)
            m->pushParamUndo(TesseraModule::BPM_PARAM, startValue, value, "Change tempo");
        dragging = false;
    }

  private:
    EditorContext& ctx;
    HoverTooltip tip;
    bool hovered = false;
    bool dragging = false;
    float startValue = 120;
    float value = 120;
};

/// What plays: the clips you launch, or the timeline. One switch, two halves; the lit half is what you hear (jade
/// while the transport runs). The same setting as PLAYS on the panel, so its undo, CV mapping and patches agree.
class PlaysSwitch : public rack::widget::OpaqueWidget {
  public:
    static constexpr float kCaption = 40;
    static constexpr float kHalf = 70;
    static constexpr float kWidth = kCaption + 2 * kHalf + 4;

    explicit PlaysSwitch(EditorContext& ctx)
        : ctx(ctx), tip("What plays: the clips you launch, or the timeline (PLAYS on the panel). A clip launched "
                        "while the timeline plays takes only its own track over") {}

    void draw(const DrawArgs& args) override {
        TesseraModule* m = ctx.module();
        if (!m)
            return;
        const theme::Palette& c = theme::colors();
        NVGcontext* vg = args.vg;
        draw::TextStyle caption{theme::fonts::uiStrong(), theme::metrics::kPanelLabel, c.text2,
                                NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme::metrics::kPanelLabelSpacing};
        draw::text(vg, caption, 2, box.size.y / 2, "PLAYS");
        rack::math::Rect well(kCaption, 0, box.size.x - kCaption, box.size.y);
        draw::roundedRect(vg, well, theme::metrics::kRadiusControl + 1, theme::recessed(c, c.bg1, 0.25f));
        bool timeline = m->playMode() == PlayMode::Arranger;
        for (int i = 0; i < 2; i++) {
            bool on = (i == 1) == timeline;
            rack::math::Rect half(kCaption + 2 + i * kHalf, 2, kHalf, box.size.y - 4);
            if (on)
                draw::roundedRect(vg, half, theme::metrics::kRadiusControl, m->playing() ? c.play : c.bg2);
            NVGcolor fg = on ? (m->playing() ? c.ink : c.text0) : hovered == i ? c.text1 : c.text2;
            draw::TextStyle word{on ? theme::fonts::uiStrong() : theme::fonts::ui(), theme::metrics::kTextUi, fg,
                                 NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
            const char* text = i == 0 ? "Clips" : "Timeline";
            float tw = draw::textWidth(vg, word, text);
            float x = half.pos.x + (half.size.x - tw - (on ? 12 : 0)) / 2;
            if (on) {
                // A play triangle on the half that plays.
                float cy = half.getCenter().y;
                nvgBeginPath(vg);
                nvgMoveTo(vg, x, cy - 4);
                nvgLineTo(vg, x + 7, cy);
                nvgLineTo(vg, x, cy + 4);
                nvgClosePath(vg);
                nvgFillColor(vg, fg);
                nvgFill(vg);
                x += 12;
            }
            draw::text(vg, word, x, half.getCenter().y, text);
        }
    }
    void step() override {
        tip.step();
        OpaqueWidget::step();
    }
    void onHover(const HoverEvent& e) override {
        OpaqueWidget::onHover(e);
        hovered = halfAt(e.pos.x);
    }
    void onEnter(const EnterEvent& e) override { tip.enter(); }
    void onLeave(const LeaveEvent& e) override {
        hovered = -1;
        tip.leave();
    }
    void onButton(const ButtonEvent& e) override {
        if (e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        e.consume(this);
        TesseraModule* m = ctx.module();
        int half = halfAt(e.pos.x);
        if (e.action != GLFW_PRESS || !m || half < 0)
            return;
        PlayMode mode = half == 1 ? PlayMode::Arranger : PlayMode::Launcher;
        if (mode != m->playMode())
            m->setPlayMode(mode);
    }

  private:
    int halfAt(float x) const {
        if (x < kCaption)
            return -1;
        return x < kCaption + 2 + kHalf ? 0 : 1;
    }

    EditorContext& ctx;
    HoverTooltip tip;
    int hovered = -1;
};

constexpr int kSwingSteps[] = {0, 10, 20, 30, 40, 50, 66, 100};
const TimeSig kTimeSigs[] = {{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 8}, {7, 8}, {9, 8}, {12, 8}};

} // namespace

TransportBar::TransportBar(EditorContext& ctx, std::function<void()> onClose) : ctx(ctx) {
    auto options = [](Icon icon, const std::string& text, const std::string& tooltip,
                      std::function<void()> action = nullptr) {
        ToolButton::Options o;
        o.icon = icon;
        o.text = text;
        o.tooltip = tooltip;
        o.action = std::move(action);
        return o;
    };
    auto button = [](ToolButton::Options o, float width = kButton) {
        auto* b = new ToolButton(std::move(o));
        b->box.size = rack::math::Vec(width, kButton);
        return b;
    };
    EditorContext* c = &ctx;

    // The instance menu: file actions and the colour scheme.
    auto instance = options(Icon::None, "Tessera",
                            "Open and save projects, export MIDI and DAWproject files, "
                            "choose the colour scheme",
                            [c] {
                                TesseraModule* m = c->module();
                                if (!m)
                                    return;
                                rack::ui::Menu* menu = rack::createMenu();
                                projectFile::appendFileMenu(
                                    menu, m, [c] { return c; }, [c](const std::string& text, bool) { c->toast(text); });
                                menu->addChild(new rack::ui::MenuSeparator);
                                menu->addChild(rack::createSubmenuItem(
                                    "Colour scheme", "", [](rack::ui::Menu* sub) { theme::appendSchemeMenu(sub); }));
                            });
    instance.caret = true;
    nameButton = button(instance, nameWidth);
    left.push_back(nameButton);

    std::vector<ToolButton*> transportButtons = makeTransportButtons(ctx);
    for (size_t i = 0; i < transportButtons.size(); i++) {
        transportButtons[i]->box.size = rack::math::Vec(kButton, kButton);
        left.push_back(transportButtons[i]);
        if (i == 2) {
            // Record's settings (count-in, input quantize, new clip length, Capture) behind a caret, not only a
            // right-click.
            ToolButton::Options more;
            more.caret = true;
            more.tooltip = "Recording settings: count-in, snapping played notes, new clip length, MIDI input, Capture";
            more.action = [c] { recordActions::openMenu(*c); };
            left.push_back(button(more, 14));
        }
    }
    left.push_back(new Spacer(kGroupGap));
    auto* plays = new PlaysSwitch(ctx);
    plays->box.size = rack::math::Vec(PlaysSwitch::kWidth, kButton);
    left.push_back(plays);

    left.push_back(new Spacer(kGroupGap));
    positionSlot = new Spacer(104);
    left.push_back(positionSlot);
    left.push_back(new Spacer(kGroupGap));
    auto* bpm = new BpmField(ctx);
    bpm->box.size = rack::math::Vec(104, kButton);
    left.push_back(bpm);
    left.push_back(new Spacer(kGap));

    // The time signature and swing: set once per song, so one chip with a menu.
    auto meter = options(Icon::None, "4/4", "Time signature and swing", [c] {
        TesseraModule* m = c->module();
        if (!m)
            return;
        rack::ui::Menu* menu = rack::createMenu();
        menu->addChild(rack::createMenuLabel("Time signature"));
        for (TimeSig ts : kTimeSigs) {
            std::string label = std::to_string(ts.num) + "/" + std::to_string(ts.den);
            menu->addChild(rack::createCheckMenuItem(
                label, "", [c, ts] { return c->module() && c->module()->song().timeSig == ts; },
                [c, ts, label] {
                    if (TesseraModule* m = c->module())
                        m->perform(setSongField(m->song(), &Song::timeSig, ts, "Set time signature to " + label));
                }));
        }
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuLabel("Swing: off-beat 1/16s land later"));
        for (int percent : kSwingSteps) {
            float value = float(percent) / 100.f;
            std::string label = percent == 0 ? "Off" : std::to_string(percent) + "%";
            menu->addChild(rack::createCheckMenuItem(
                label, "", [c, value] { return c->module() && std::fabs(c->module()->song().swing - value) < 0.005f; },
                [c, value, label] {
                    if (TesseraModule* m = c->module())
                        m->perform(setSongField(m->song(), &Song::swing, value,
                                                value > 0 ? "Set swing to " + label : "Swing off"));
                }));
        }
    });
    meter.caret = true;
    timeSigButton = button(meter, 56);
    left.push_back(timeSigButton);

    right.push_back(button(options(Icon::Close, "", withShortcut("Close editor", Action::Close), onClose)));

    auto inspector = options(Icon::Inspector, "", withShortcut("Inspector", Action::ToggleInspector),
                             [c] { c->view().inspectorOpen = !c->view().inspectorOpen; });
    inspector.active = [c] { return c->module() && c->view().inspectorOpen; };
    right.push_back(button(inspector));

    // A small on/off icon: whether to follow is often toggled, how (page or scroll) is set once in View.
    auto follow = options(Icon::Follow, "", withShortcut("Follow the playhead (page or scroll: View in the notes)",
                                                         Action::ToggleFollow),
                          [this, c] {
                              FollowMode& f = c->state().follow;
                              if (f != FollowMode::Off) {
                                  lastFollow = f;
                                  f = FollowMode::Off;
                              } else {
                                  f = lastFollow;
                              }
                          });
    follow.active = [c] { return c->module() && c->state().follow != FollowMode::Off; };
    followButton = button(follow);
    right.push_back(followButton);
    right.push_back(new Spacer(kGroupGap));

    auto redo = options(Icon::Redo, "", withShortcut("Redo", Action::Redo), [] { APP->history->redo(); });
    redo.enabled = [] { return APP->history->canRedo(); };
    ToolButton* redoButton = button(redo);
    right.push_back(redoButton);
    auto undo = options(Icon::Undo, "", withShortcut("Undo", Action::Undo), [] { APP->history->undo(); });
    undo.enabled = [] { return APP->history->canUndo(); };
    ToolButton* undoButton = button(undo);
    right.push_back(undoButton);
    // What gives way first when the window is narrow. Undo and redo stay on Ctrl/Cmd+Z.
    collapsible = {undoButton, redoButton};

    for (rack::widget::Widget* w : left)
        addChild(w);
    for (rack::widget::Widget* w : right)
        addChild(w);
}

void TransportBar::layout() {
    auto needed = [this] {
        float w = 12 + 8 + kGroupGap;
        for (const std::vector<rack::widget::Widget*>* group : {&left, &right})
            for (rack::widget::Widget* item : *group)
                if (item->visible)
                    w += item->box.size.x + kGap;
        return w;
    };
    for (rack::widget::Widget* item : collapsible)
        item->visible = true;
    for (rack::widget::Widget* item : collapsible)
        if (needed() > box.size.x)
            item->visible = false;

    float y = (box.size.y - kButton) / 2;
    float x = 12;
    for (rack::widget::Widget* w : left) {
        if (!w->visible)
            continue;
        w->box.pos = rack::math::Vec(x, y);
        x += w->box.size.x + kGap;
    }
    float r = box.size.x - 8;
    for (rack::widget::Widget* w : right) {
        if (!w->visible)
            continue;
        r -= w->box.size.x;
        w->box.pos = rack::math::Vec(r, y);
        r -= kGap;
    }
}

void TransportBar::step() {
    // Buttons whose text follows state: time signature, follow mode and the top view.
    if (TesseraModule* m = ctx.module()) {
        TimeSig ts = m->song().timeSig;
        std::string meter = std::to_string(ts.num) + "/" + std::to_string(ts.den);
        float swing = m->song().swing;
        if (swing > 0.005f)
            meter += "  ·  swing " + std::to_string(int(std::lround(swing * 100))) + "%";
        timeSigButton->setText(meter);
        timeSigButton->box.size.x = swing > 0.005f ? 132 : 56;
        nameButton->setText(m->instanceName());
    }
    layout();
    Widget::step();
}

void TransportBar::draw(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, box.size.y - 1, box.size.x, 1);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);

    if (TesseraModule* m = ctx.module()) {
        rack::math::Rect slot = positionSlot->box;
        draw::roundedRect(vg, slot.grow(rack::math::Vec(0, -2)), theme::metrics::kRadiusControl, c.bg0);
        draw::TextStyle pos{theme::fonts::mono(), 15, m->playing() ? c.text0 : c.text1,
                            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, pos, slot.getCenter().x, box.size.y / 2,
                   timeline::positionText(m->playheadTicks(), m->song().timeSig));
    }
    Widget::draw(args);
}

} // namespace tessera
