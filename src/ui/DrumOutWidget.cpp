#include "../module/DrumOutModule.hpp"
#include "../module/TesseraModule.hpp"
#include "../plugin.hpp"
#include "ModuleUndo.hpp"
#include "OutputMenus.hpp"
#include "TrackOutDisplay.hpp"
#include "widgets/Draw.hpp"
#include "widgets/IconButton.hpp"
#include "widgets/Lights.hpp"
#include "widgets/Panel.hpp"

namespace tessera {

namespace {

using rack::math::Rect;
using rack::math::Vec;

constexpr int kHp = 18;

// Layout in mm. The panel is 91.4 x 128.5.
const Rect kDisplay(4, 12.5f, 83.4f, 24);
constexpr float kButtonRow = 41.5f;
const Vec kButtonSize(12, 6);
const Rect kOutputPlate(2.5f, 45, 86.4f, 78);
// One mixer-style channel strip per lane: a color chip with the lane's number on top, then its four jacks
// (trigger, velocity, timbre, pressure). The strips recede into the plate; the chip carries the identity.
constexpr float kStripW = 9.6f;
constexpr float kChipTop = 49.1f, kChipH = 4.3f;
constexpr float kTrigRow = 60, kVelRow = 72, kTimbreRow = 87, kPressRow = 98.5f;
constexpr float kLaneX[8] = {8.2f, 18.75f, 29.3f, 39.85f, 50.4f, 60.95f, 71.5f, 82.05f};
constexpr float kPolyRow = 116;
constexpr float kPolyX[5] = {11.2f, 28.5f, 45.7f, 63.f, 80.3f};
constexpr float kLightAbove = 6.4f;

const DrumLane* laneOf(DrumOutModule* module, int lane) {
    if (!module)
        return nullptr;
    TesseraModule* src = module->source();
    if (!src)
        return nullptr;
    const Song& song = src->song();
    int index = song.trackIndex(module->binding().track);
    if (index < 0 || lane >= int(song.tracks[size_t(index)].drumLanes.size()))
        return nullptr;
    return &song.tracks[size_t(index)].drumLanes[size_t(lane)];
}

/// The strip's color chip: the lane's colour with its number in ink, like a mixer channel's coloured tag.
struct LaneChip : rack::widget::Widget {
    LaneChip(DrumOutModule* module, int lane) : module(module), lane(lane) {}

    void draw(const DrawArgs& args) override {
        const theme::Palette& c = theme::panelColors();
        uint32_t color = laneOf(module, lane) ? laneOf(module, lane)->color : 0;
        NVGcolor chip = color ? theme::rgb(color) : c.bg2;
        draw::roundedRect(args.vg, Rect(Vec(), box.size), rack::mm2px(1.4f), chip);
        tessera::draw::TextStyle number{theme::fonts::uiStrong(), theme::metrics::kPanelLabel, c.ink,
                                        NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        tessera::draw::text(args.vg, number, box.size.x / 2, box.size.y / 2 + 0.3f, std::to_string(lane + 1));
    }

    DrumOutModule* module;
    int lane;
};

} // namespace

struct DrumOutWidget : rack::app::ModuleWidget {
    explicit DrumOutWidget(DrumOutModule* module) {
        using M = DrumOutModule;
        setModule(module);
        PanelBuilder panel(this, kHp, "DRUM OUT");

        auto* display = new TrackOutDisplay(module);
        display->box = Rect(rack::mm2px(kDisplay.pos), rack::mm2px(kDisplay.size));
        addChild(display);

        addStepButton(IconButton::Icon::Previous, Vec(39.2f, kButtonRow), "Previous drum track", -1);
        addStepButton(IconButton::Icon::Next, Vec(52.2f, kButtonRow), "Next drum track", +1);

        panel.art().addPlate(kOutputPlate);
        for (int i = 0; i < M::kTrigJacks; i++) {
            float x = kLaneX[i];
            panel.art().addSunkPlate(Rect(x - kStripW / 2, 48.5f, kStripW, 57));
            auto* chip = new LaneChip(module, i);
            chip->box = Rect(rack::mm2px(Vec(x - kStripW / 2 + 0.6f, kChipTop)),
                             rack::mm2px(Vec(kStripW - 1.2f, kChipH)));
            addChild(chip);
            panel.output(Vec(x, kTrigRow), M::TRIG_OUTPUT + i, "");
            panel.output(Vec(x, kVelRow), M::VEL_OUTPUT + i, "V");
            panel.output(Vec(x, kTimbreRow), M::TIMBRE_OUTPUT + i, "T");
            panel.output(Vec(x, kPressRow), M::PRESS_OUTPUT + i, "P");
            addChild(rack::createLightCentered<rack::componentlibrary::SmallLight<PlayLight>>(
                rack::mm2px(Vec(x - 3.4f, kTrigRow - kLightAbove)), module, M::TRIG_LIGHT + i));
        }
        const char* polyLabels[5] = {"TRIGS", "VEL", "TIMBRE", "PRESS", "AUTO"};
        const int polyOutputs[5] = {M::TRIG_POLY_OUTPUT, M::VEL_POLY_OUTPUT, M::TIMBRE_POLY_OUTPUT,
                                    M::PRESS_POLY_OUTPUT, M::AUTO_OUTPUT};
        for (int k = 0; k < 5; k++)
            panel.output(Vec(kPolyX[k], kPolyRow), polyOutputs[k], polyLabels[k]);
    }

    void step() override {
        if (auto* m = outputModule())
            m->refreshBinding();
        ModuleWidget::step();
    }

    void appendContextMenu(rack::ui::Menu* menu) override {
        DrumOutModule* m = outputModule();
        if (!m)
            return;
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createSubmenuItem("Source", m->binding().sourceName,
                                               [m](rack::ui::Menu* sub) { appendSourceMenu(sub, m, "Drum Out"); }));
        menu->addChild(rack::createSubmenuItem(
            "Track", "", [m](rack::ui::Menu* sub) { appendTrackMenu(sub, m, "Drum Out"); }, m->source() == nullptr));
    }

  private:
    DrumOutModule* outputModule() const { return static_cast<DrumOutModule*>(module); }

    void addStepButton(IconButton::Icon icon, Vec centerMm, const std::string& tooltip, int delta) {
        auto* button = new IconButton(icon, tooltip, [this, delta] {
            if (auto* m = outputModule())
                withModuleUndo(m, "change Drum Out track", [&] { m->stepTrack(delta); });
        });
        button->box.size = rack::mm2px(kButtonSize);
        button->box.pos = rack::mm2px(centerMm).minus(button->box.size.div(2));
        addChild(button);
    }
};

} // namespace tessera

rack::plugin::Model* modelDrumOut = rack::createModel<tessera::DrumOutModule, tessera::DrumOutWidget>("TesseraDrumOut");
