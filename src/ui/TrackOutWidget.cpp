#include "../module/Registry.hpp"
#include "../module/TesseraModule.hpp"
#include "../module/TrackOutModule.hpp"
#include "../plugin.hpp"
#include "ModuleUndo.hpp"
#include "OutputMenus.hpp"
#include "Theme.hpp"
#include "TrackOutDisplay.hpp"
#include "widgets/Draw.hpp"
#include "widgets/IconButton.hpp"
#include "widgets/Panel.hpp"

#include <algorithm>
#include <functional>

namespace tessera {

namespace {

using rack::math::Rect;
using rack::math::Vec;

constexpr int kHp = 6;

// Layout in mm. The panel is 30.48 x 128.5 (docs/design/AUDIO_MIXER.md §8.1, option A): the display, the track
// arrows, a plate of note jacks and AUTO, then the audio section, OUT over IN, so the effect loop reads top to bottom.
const Rect kDisplay(3, 11, 24.48f, 18);
constexpr float kButtonRow = 33.25f;
const Vec kButtonSize(9.5f, 6);
const Rect kNotesPlate(2.5f, 38.5f, 25.48f, 53.5f);
const Rect kAudioPlate(2.5f, 93, 25.48f, 31);
constexpr float kLeft = 9.f, kRight = 21.48f, kCenter = 15.24f;
constexpr float kNoteRows[4] = {47.5f, 60.7f, 73.9f, 87.1f};
constexpr float kOutRow = 106, kInRow = 119.5f;

/// Dims a jack (and its label) that does nothing for the bound track, so the panel shows what is live (§8.1). A
/// cable on a dim jack gets an amber ring. Takes no clicks: the jack under it still takes the cable.
struct DimVeil : rack::widget::TransparentWidget {
    TrackOutModule* module = nullptr;
    int port = 0;
    bool input = false;
    std::function<bool(BoundKind, const TrackOutModule&)> dim;

    void draw(const DrawArgs& args) override {
        if (!module || !dim(module->boundKind(), *module))
            return;
        const theme::Palette& c = theme::panelColors();
        NVGcolor ground = input ? c.bg1 : c.bg2;
        draw::roundedRect(args.vg, Rect(Vec(), box.size), rack::mm2px(1.2f), nvgTransRGBAf(ground, 0.68f));
        bool cabled = input ? module->inputs[size_t(port)].isConnected() : module->outputs[size_t(port)].isConnected();
        if (cabled) {
            // The jack's centre: the veil ends 4.6 mm below it.
            Vec jack(box.size.x / 2, box.size.y - rack::mm2px(4.6f));
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, jack.x, jack.y, rack::mm2px(4.4f));
            nvgStrokeColor(args.vg, theme::colors().queue);
            nvgStrokeWidth(args.vg, 1.5f);
            nvgStroke(args.vg);
        }
    }
};

} // namespace

struct TrackOutWidget : rack::app::ModuleWidget {
    explicit TrackOutWidget(TrackOutModule* module) {
        using M = TrackOutModule;
        setModule(module);
        PanelBuilder panel(this, kHp, "TRACK OUT");

        auto* display = new TrackOutDisplay(module);
        display->box = Rect(rack::mm2px(kDisplay.pos), rack::mm2px(kDisplay.size));
        addChild(display);

        addStepButton(IconButton::Icon::Previous, Vec(kLeft, kButtonRow), "Previous track", -1);
        addStepButton(IconButton::Icon::Next, Vec(kRight, kButtonRow), "Next track", +1);

        panel.art().addPlate(kNotesPlate);
        panel.art().addPlate(kAudioPlate);
        panel.art().addLabel(Vec(kCenter, kAudioPlate.pos.y + 3), "AUDIO", PanelArt::LabelKind::Section);
        using Role = int; // 0 notes, 1 OUT, 2 IN, 3 AUTO (live for every track and the master)
        struct Jack {
            int id;
            const char* label;
            Vec at;
            Role role;
        };
        const Jack outs[] = {
            {M::VOCT_OUTPUT, "V/OCT", {kLeft, kNoteRows[0]}, 0},  {M::GATE_OUTPUT, "GATE", {kRight, kNoteRows[0]}, 0},
            {M::VEL_OUTPUT, "VEL", {kLeft, kNoteRows[1]}, 0},     {M::REL_OUTPUT, "REL", {kRight, kNoteRows[1]}, 0},
            {M::TIMBRE_OUTPUT, "TIMB", {kLeft, kNoteRows[2]}, 0}, {M::PRESS_OUTPUT, "PRESS", {kRight, kNoteRows[2]}, 0},
            {M::AUTO_OUTPUT, "AUTO", {kCenter, kNoteRows[3]}, 3}, {M::OUT_L_OUTPUT, "OUT L", {kLeft, kOutRow}, 1},
            {M::OUT_R_OUTPUT, "OUT R", {kRight, kOutRow}, 1},
        };
        for (const Jack& j : outs) {
            panel.output(j.at, j.id, j.label);
            addVeil(module, j.id, false, j.at, j.role);
        }
        // IN sits in the audio section with OUT: the section is the effect loop, out and back.
        panel.input(Vec(kLeft, kInRow), M::IN_L_INPUT, "IN L");
        panel.input(Vec(kRight, kInRow), M::IN_R_INPUT, "IN R");
        addVeil(module, M::IN_L_INPUT, true, Vec(kLeft, kInRow), 2);
        addVeil(module, M::IN_R_INPUT, true, Vec(kRight, kInRow), 2);
        // The IN light between IN L and IN R: green while audio comes back, red above 0 dB.
        auto* light =
            rack::createLightCentered<rack::componentlibrary::SmallLight<rack::componentlibrary::GreenRedLight>>(
                rack::mm2px(Vec(kCenter, kInRow)), module, M::IN_LIGHT);
        addChild(light);
    }

    void step() override {
        if (auto* m = outputModule()) {
            m->refreshBinding();
        }
        ModuleWidget::step();
    }

    void appendContextMenu(rack::ui::Menu* menu) override {
        TrackOutModule* m = outputModule();
        if (!m)
            return;
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createSubmenuItem("Source", m->binding().sourceName,
                                               [m](rack::ui::Menu* sub) { appendSourceMenu(sub, m, "Track Out"); }));
        menu->addChild(rack::createSubmenuItem(
            "Track", "", [m](rack::ui::Menu* sub) { appendTrackMenu(sub, m, "Track Out"); }, m->source() == nullptr));
    }

  private:
    TrackOutModule* outputModule() const { return static_cast<TrackOutModule*>(module); }

    /// A veil over jack `id` and its label, dim when the jack's `role` does nothing for the bound track: note jacks
    /// for audio tracks and the master, OUT for note tracks (until sends exist), OUT and IN while following the
    /// editor's selection.
    void addVeil(TrackOutModule* m, int id, bool input, Vec centerMm, int role) {
        auto* veil = new DimVeil;
        veil->module = m;
        veil->port = id;
        veil->input = input;
        veil->dim = [role](BoundKind kind, const TrackOutModule& mod) {
            if (role == 3)
                return false;
            if (role == 0)
                return kind == BoundKind::Audio || kind == BoundKind::Master;
            if (mod.binding().followSelected)
                return true;
            return role == 1 && (kind == BoundKind::Instrument || kind == BoundKind::Drum);
        };
        Rect mm(centerMm.x - 5.6f, centerMm.y - theme::metrics::kPanelLabelAboveJackMm - 2.4f, 11.2f,
                theme::metrics::kPanelLabelAboveJackMm + 2.4f + 4.6f);
        veil->box = Rect(rack::mm2px(mm.pos), rack::mm2px(mm.size));
        addChild(veil);
    }

    void addStepButton(IconButton::Icon icon, Vec centerMm, const std::string& tooltip, int delta) {
        auto* button = new IconButton(icon, tooltip, [this, delta] {
            if (auto* m = outputModule())
                withModuleUndo(m, "change Track Out track", [&] { m->stepTrack(delta); });
        });
        button->box.size = rack::mm2px(kButtonSize);
        button->box.pos = rack::mm2px(centerMm).minus(button->box.size.div(2));
        addChild(button);
    }
};

} // namespace tessera

rack::plugin::Model* modelTrackOut =
    rack::createModel<tessera::TrackOutModule, tessera::TrackOutWidget>("TrackOut");
