#include "../engine/ClockDivisions.hpp"
#include "../module/Registry.hpp"
#include "../module/TesseraModule.hpp"
#include "../plugin.hpp"
#include "EditorWindow.hpp"
#include "ModuleUndo.hpp"
#include "PanelEditor.hpp"
#include "TesseraDisplay.hpp"
#include "Theme.hpp"
#include "editing/ProjectFile.hpp"
#include "widgets/Draw.hpp"
#include "widgets/Lights.hpp"
#include "widgets/MenuTextField.hpp"
#include "widgets/Panel.hpp"
#include "widgets/ToolButton.hpp"

#include <osdialog.h>

namespace tessera {

namespace {

using rack::math::Rect;
using rack::math::Vec;
using namespace rack::componentlibrary;

constexpr int kHp = 24;

// Layout in mm. The panel is 121.92 x 128.5.
const Rect kDisplay(6, 13, 109.92f, 34);
constexpr float kControlRow = 57;
constexpr float kSceneRow = 72;
const Vec kEditorButton(77, kControlRow);
const Vec kEditorButtonSize(15, 7);
const float kEditorScales[] = {0, 0.75f, 1, 1.25f, 1.5f, 1.75f, 2};
const int kPanelSizes[] = {EditorState::kPanelMinHp, 48, 80, 120};

// The editor area to the right of the controls (px, panel space).
constexpr float kEditorInset = 3;
const float kEditorTop = rack::RACK_GRID_WIDTH + 1; // below the screw row
constexpr float kHandleWidth = 8;

/// The background of the widened part of the panel, behind the in-panel editor.
struct PanelExtension : rack::widget::Widget {
    void draw(const DrawArgs& args) override {
        const theme::Palette& c = theme::panelColors();
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, c.bg1);
        nvgFill(args.vg);
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, 1, box.size.y);
        nvgRect(args.vg, box.size.x - 1, 0, 1, box.size.y);
        nvgFillColor(args.vg, c.line0);
        nvgFill(args.vg);
    }
};
constexpr float kButtonCol[5] = {12, 25, 38, 51, 64};
constexpr float kJackRow1 = 95;
constexpr float kJackRow2 = 112;
constexpr float kInputCol[4] = {8, 21.5f, 35, 48.5f};
constexpr float kTopInputCol[5] = {6.5f, 17.5f, 28.5f, 40, 53}; // CLOCK joins the first row
const Rect kRecordPlate(2, 101, 55, 19);
const Rect kOutputPlate(62, 84, 54, 38);
constexpr float kOutputCol[3] = {71, 88, 105};
constexpr float kStopAllCol = 54; // its label is wide
constexpr float kFillCol = 93;
constexpr float kFillJackCol = 106;
constexpr float kPlaysCol = 76;

/// PLAYS: what the tracks play, the clips or the timeline. One switch with two halves; the lit half plays, and a
/// click on a half picks it. Same param as the editor's Plays switch.
struct PlaysSwitch : rack::app::Switch {
    static constexpr float kWidthMm = 24;
    static constexpr float kHeightMm = 6.5f;

    PlaysSwitch() {
        box.size = rack::mm2px(Vec(kWidthMm, kHeightMm));
        split = box.size.x / 2;
    }

    bool timeline() {
        rack::engine::ParamQuantity* q = getParamQuantity();
        return q && q->getValue() > 0.5f;
    }

    void draw(const DrawArgs& args) override {
        const theme::Palette& c = theme::panelColors();
        Rect r(Vec(), box.size);
        draw::roundedRect(args.vg, r, theme::metrics::kRadiusControl + 1, theme::recessed(c, c.bg1, 0.35f));
        drawHalves(args.vg, false);
    }
    void drawLayer(const DrawArgs& args, int layer) override {
        // The lit half glows: readable with the room lights down.
        if (layer == 1)
            drawHalves(args.vg, true);
        Switch::drawLayer(args, layer);
    }
    void onButton(const ButtonEvent& e) override {
        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT)
            pressedPlaying = (e.pos.x >= split) == timeline();
        Switch::onButton(e);
    }
    void onDragStart(const DragStartEvent& e) override {
        // A click on the half that already plays changes nothing; the other half switches.
        if (e.button == GLFW_MOUSE_BUTTON_LEFT && pressedPlaying)
            return;
        Switch::onDragStart(e);
    }

  private:
    bool pressedPlaying = false;
    float split = 0; // where the halves meet: "TIMELINE" gets more room than "CLIPS"

    void drawHalves(NVGcontext* vg, bool lit) {
        const theme::Palette& c = theme::panelColors();
        const char* labels[2] = {"CLIPS", "TIMELINE"};
        draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kPanelLabel, c.text2,
                              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, 0.1f};
        // Each half is its word plus an even share of the room left, so neither word touches its edges.
        float inner = box.size.x - 4;
        float widths[2] = {draw::textWidth(vg, style, labels[0]), draw::textWidth(vg, style, labels[1])};
        float spare = std::max(0.f, inner - widths[0] - widths[1]);
        float first = widths[0] + spare / 2;
        split = 2 + first;
        for (int i = 0; i < 2; i++) {
            bool on = (i == 1) == timeline();
            if (on != lit)
                continue;
            Rect r = i == 0 ? Rect(2, 2, first, box.size.y - 4) : Rect(split, 2, inner - first, box.size.y - 4);
            if (on)
                draw::roundedRect(vg, r, theme::metrics::kRadiusControl, c.play);
            style.color = on ? c.ink : c.text2;
            draw::text(vg, style, r.getCenter().x, r.getCenter().y + 0.5f, labels[i]);
        }
    }
};

} // namespace

struct TesseraWidget : rack::app::ModuleWidget {
    explicit TesseraWidget(TesseraModule* module) {
        using M = TesseraModule;
        setModule(module);
        PanelBuilder panel(this, kHp, "TESSERA");

        auto* display = new TesseraDisplay(module);
        display->box = Rect(rack::mm2px(kDisplay.pos), rack::mm2px(kDisplay.size));
        addChild(display);

        panel.lightParam<VCVLightButton<MediumSimpleLight<PlayLight>>>(Vec(kButtonCol[0], kControlRow), M::PLAY_PARAM,
                                                                       M::PLAY_LIGHT, "PLAY");
        panel.lightParam<VCVLightButton<MediumSimpleLight<RecordLight>>>(Vec(kButtonCol[1], kControlRow), M::REC_PARAM,
                                                                         M::REC_LIGHT, "REC");
        panel.param<VCVButton>(Vec(kButtonCol[2], kControlRow), M::RESET_PARAM, "RESET");
        panel.lightParam<VCVLightLatch<MediumSimpleLight<AccentLight>>>(Vec(kButtonCol[3], kControlRow), M::LOOP_PARAM,
                                                                        M::LOOP_LIGHT, "LOOP");
        panel.lightParam<VCVLightButton<MediumSimpleLight<AccentLight>>>(Vec(kButtonCol[4], kControlRow), M::TAP_PARAM,
                                                                         M::BEAT_LIGHT, "TAP");
        panel.param<RoundLargeBlackKnob>(Vec(100, kControlRow + 1), M::BPM_PARAM, "TEMPO", 10);

        panel.param<VCVButton>(Vec(kButtonCol[0], kSceneRow), M::SCENE_PREV_PARAM, "PREV");
        panel.param<VCVButton>(Vec(kButtonCol[1], kSceneRow), M::SCENE_NEXT_PARAM, "NEXT");
        panel.lightParam<VCVLightButton<MediumSimpleLight<QueueLight>>>(
            Vec(kButtonCol[2], kSceneRow), M::SCENE_LAUNCH_PARAM, M::LAUNCH_LIGHT, "LAUNCH");
        panel.param<VCVButton>(Vec(kStopAllCol, kSceneRow), M::STOP_ALL_PARAM, "STOP ALL");
        panel.lightParam<VCVLightButton<MediumSimpleLight<QueueLight>>>(Vec(kFillCol, kSceneRow), M::FILL_PARAM,
                                                                        M::FILL_LIGHT, "FILL");
        panel.input(Vec(kFillJackCol, kSceneRow + 1), M::FILL_INPUT, "");
        panel.param<PlaysSwitch>(Vec(kPlaysCol, kSceneRow), M::MODE_PARAM, "PLAYS");

        ToolButton::Options open;
        open.text = "EDITOR";
        open.tooltip = "Open the editor";
        open.action = [this] { openEditor(); };
        open.filled = true;
        open.onPanel = true;
        auto* editorButton = new ToolButton(open);
        editorButton->box.size = rack::mm2px(kEditorButtonSize);
        editorButton->box.pos = rack::mm2px(kEditorButton).minus(editorButton->box.size.div(2));
        addChild(editorButton);

        panel.input(Vec(kTopInputCol[0], kJackRow1), M::CLOCK_INPUT, "CLOCK");
        panel.input(Vec(kTopInputCol[1], kJackRow1), M::RESET_INPUT, "RESET");
        panel.input(Vec(kTopInputCol[2], kJackRow1), M::RUN_INPUT, "RUN");
        panel.input(Vec(kTopInputCol[3], kJackRow1), M::SCENE_INPUT, "SCENE");
        panel.input(Vec(kTopInputCol[4], kJackRow1), M::LAUNCH_INPUT, "LAUNCH");
        // The record inputs, on a plate of their own so they read as one group.
        panel.art().addPlate(kRecordPlate);
        panel.input(Vec(kInputCol[0], kJackRow2), M::REC_VOCT_INPUT, "V/OCT");
        panel.input(Vec(kInputCol[1], kJackRow2), M::REC_GATE_INPUT, "GATE");
        panel.input(Vec(kInputCol[2], kJackRow2), M::REC_VEL_INPUT, "VEL");
        panel.input(Vec(kInputCol[3], kJackRow2), M::AUTO_REC_INPUT, "AUTO");

        panel.art().addPlate(kOutputPlate);
        panel.output(Vec(kOutputCol[0], kJackRow1), M::CLOCK_OUTPUT, "CLOCK");
        panel.output(Vec(kOutputCol[1], kJackRow1), M::RESET_OUTPUT, "RESET");
        panel.output(Vec(kOutputCol[2], kJackRow1), M::RUN_OUTPUT, "RUN");
        panel.output(Vec(kOutputCol[0], kJackRow2), M::EOC_OUTPUT, "EOC");
        panel.output(Vec(kOutputCol[1], kJackRow2), M::BAR_OUTPUT, "BAR");
        panel.output(Vec(kOutputCol[2], kJackRow2), M::CLICK_OUTPUT, "CLICK");

        rightScrews = panel.rightScrews();
        controlsWidth = box.size.x;
        if (module) {
            extension = new PanelExtension;
            addChild(extension);
            editor = new PanelEditor(module->id, [this] { openEditor(); });
            addChild(editor);
            handle = new PanelResizeHandle(this);
            addChild(handle);
            applyWidth(module->editor().panelHp);
        }
    }

    void step() override {
        if (auto* m = static_cast<TesseraModule*>(module)) {
            m->uiStep();
            // Undo, redo and patch loads change the saved width; follow it.
            applyWidth(m->editor().panelHp);
        }
        ModuleWidget::step();
    }

    void onButton(const ButtonEvent& e) override {
        // Clicking a Tessera makes it the one new output modules follow.
        if (module && e.action == GLFW_PRESS)
            Registry::get().touch(static_cast<TesseraModule*>(module));
        ModuleWidget::onButton(e);
    }

    void onHoverKey(const HoverKeyEvent& e) override {
        // Rack's ModuleWidget runs its own shortcuts (Delete removes the module, Ctrl+D clones it) before its
        // children see a key, so the panel editor gets the key first while the mouse is on it.
        if (editor && editor->visible && editor->box.contains(e.pos)) {
            HoverKeyEvent inner = e;
            inner.pos = e.pos.minus(editor->box.pos);
            editor->onHoverKey(inner);
            if (e.isConsumed())
                return;
        }
        ModuleWidget::onHoverKey(e);
    }

    void appendContextMenu(rack::ui::Menu* menu) override {
        auto* m = static_cast<TesseraModule*>(module);
        if (!m)
            return;

        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuItem("Open editor", "", [this] { openEditor(); }));
        int64_t id = m->id;
        menu->addChild(rack::createSubmenuItem("File", "", [this, m, id](rack::ui::Menu* sub) {
            projectFile::appendFileMenu(
                sub, m,
                [this, id] {
                    openEditor();
                    return EditorWindow::contextFor(id);
                },
                [id](const std::string& text, bool problem) {
                    if (EditorContext* c = EditorWindow::contextFor(id))
                        c->toast(text);
                    else if (problem)
                        osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, text.c_str());
                });
        }));
        menu->addChild(rack::createSubmenuItem("MIDI input (for recording)", "", [m](rack::ui::Menu* sub) {
            rack::app::appendMidiMenu(sub, &m->midiInput());
        }));
        std::vector<std::string> sizes;
        for (int hp : kPanelSizes)
            sizes.push_back(hp == EditorState::kPanelMinHp ? "Off" : std::to_string(hp) + " HP");
        menu->addChild(rack::createIndexSubmenuItem(
            "Panel editor", sizes,
            [m] {
                for (size_t i = 0; i < std::size(kPanelSizes); i++)
                    if (kPanelSizes[i] == m->editor().panelHp)
                        return i;
                return size_t(-1); // a dragged width that isn't one of the presets
            },
            [this, m](size_t i) { resizeTesseraPanel(this, kPanelSizes[i], m->editor().panelHp, true); }));
        menu->addChild(
            rack::createSubmenuItem("Colour scheme", "", [](rack::ui::Menu* sub) { theme::appendSchemeMenu(sub); }));
        std::vector<std::string> scales;
        for (float s : kEditorScales)
            scales.push_back(s == 0 ? "Auto" : std::to_string(int(s * 100)) + "%");
        menu->addChild(rack::createIndexSubmenuItem(
            "Editor size", scales,
            [m] {
                for (size_t i = 0; i < std::size(kEditorScales); i++)
                    if (kEditorScales[i] == m->editor().uiScale)
                        return i;
                return size_t(0);
            },
            [m](size_t i) { m->editor().uiScale = kEditorScales[i]; }));

        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuLabel("Instance name"));
        menu->addChild(new MenuTextField(m->instanceName(), [m](const std::string& text) {
            std::string name = text.empty() ? "Tessera" : text;
            withModuleUndo(m, "rename Tessera", [&] { m->setInstanceName(Registry::get().uniqueSourceName(name)); });
        }));

        std::vector<std::string> divisions;
        for (const ClockDivision& d : kClockDivisions)
            divisions.push_back(d.label);
        menu->addChild(rack::createIndexSubmenuItem(
            "CLOCK output", divisions, [m] { return size_t(m->clockDivision()); },
            [m](size_t index) { withModuleUndo(m, "set clock output", [&] { m->setClockDivision(int(index)); }); }));

        static const int kInputPpqn[] = {1, 2, 4, 8, 12, 24, 48, 96};
        std::vector<std::string> ppqnLabels;
        for (int p : kInputPpqn)
            ppqnLabels.push_back(std::to_string(p) + " PPQN" + (p == 24 ? " (MIDI clock)" : ""));
        menu->addChild(rack::createIndexSubmenuItem(
            "CLOCK input", ppqnLabels,
            [m] {
                for (size_t i = 0; i < 8; i++)
                    if (kInputPpqn[i] == m->clockInputPpqn())
                        return i;
                return size_t(5);
            },
            [m](size_t index) {
                withModuleUndo(m, "set CLOCK input", [&] { m->setClockInputPpqn(kInputPpqn[index]); });
            }));

        menu->addChild(rack::createSubmenuItem("Tracks", "", [m](rack::ui::Menu* sub) { appendTracksMenu(sub, m); }));

        menu->addChild(rack::createIndexSubmenuItem(
            "SCENE input", {"0.1 V per scene", "1 semitone per scene"}, [m] { return size_t(m->sceneCvMode()); },
            [m](size_t index) {
                withModuleUndo(m, "set SCENE input mode", [&] { m->setSceneCvMode(SceneCvMode(index)); });
            }));

        menu->addChild(rack::createIndexSubmenuItem(
            "RUN input", {"Toggle (each trigger starts/stops)", "Gate (high = play)"},
            [m] { return size_t(m->runInputMode()); },
            [m](size_t index) {
                withModuleUndo(m, "set RUN input mode", [&] { m->setRunInputMode(RunInputMode(index)); });
            }));
    }

  private:
    /// Lays out the panel for `hp`: the controls stay put, the editor fills the rest, screws follow the edge.
    void applyWidth(int hp) {
        float width = hp * rack::RACK_GRID_WIDTH;
        box.size.x = width;
        bool wide = hp > EditorState::kPanelMinHp;
        for (rack::widget::Widget* screw : rightScrews)
            screw->box.pos.x = width - 2 * rack::RACK_GRID_WIDTH;
        extension->visible = editor->visible = wide;
        float extra = std::max(0.f, width - controlsWidth);
        extension->box = rack::math::Rect(controlsWidth, 0, extra, box.size.y);
        editor->box = rack::math::Rect(controlsWidth + kEditorInset, kEditorTop,
                                       std::max(0.f, extra - kEditorInset - kHandleWidth), box.size.y - 2 * kEditorTop);
        handle->box = rack::math::Rect(width - kHandleWidth, kEditorTop, kHandleWidth, box.size.y - 2 * kEditorTop);
    }

    void openEditor() {
        if (module)
            EditorWindow::open(static_cast<TesseraModule*>(module));
    }

    /// Mute and solo per track, so the panel alone can shape what plays until the editor exists.
    std::vector<rack::widget::Widget*> rightScrews;
    float controlsWidth = 0;
    PanelExtension* extension = nullptr;
    PanelEditor* editor = nullptr;
    PanelResizeHandle* handle = nullptr;

    static void appendTracksMenu(rack::ui::Menu* menu, TesseraModule* m) {
        const Song& song = m->song();
        for (size_t i = 0; i < song.tracks.size(); i++) {
            const Track& t = song.tracks[i];
            TrackId id = t.id;
            std::string label = std::to_string(i + 1) + "  " + t.name;
            menu->addChild(rack::createSubmenuItem(label, trackState(t), [m, id](rack::ui::Menu* sub) {
                sub->addChild(toggleItem(m, id, &Track::mute, "Mute"));
                sub->addChild(toggleItem(m, id, &Track::solo, "Solo"));
            }));
        }
    }

    static std::string trackState(const Track& t) {
        if (t.mute)
            return "muted";
        return t.solo ? "solo" : "";
    }

    static rack::ui::MenuItem* toggleItem(TesseraModule* m, TrackId id, bool Track::* field, const std::string& verb) {
        return rack::createCheckMenuItem(
            verb, "",
            [m, id, field] {
                const Track* t = m->song().findTrack(id);
                return t && t->*field;
            },
            [m, id, field, verb] {
                const Track* t = m->song().findTrack(id);
                if (!t)
                    return;
                bool on = !(t->*field);
                std::string name = (on ? verb : "Un" + rack::string::lowercase(verb)) + " " + t->name;
                m->perform(setTrackField(m->song(), id, field, on, name));
            });
    }
};

} // namespace tessera

rack::plugin::Model* modelTessera = rack::createModel<tessera::TesseraModule, tessera::TesseraWidget>("Tessera");
