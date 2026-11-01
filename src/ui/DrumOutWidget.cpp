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
const Rect kDisplay(23.3f, 12.5f, 44.8f, 24);
constexpr float kButtonRow = 41.5f;
const Vec kButtonSize(12, 6);
const Rect kOutputPlate(2.5f, 45, 86.4f, 78);
// One vertical strip per lane: the lane's name on top, then its four jacks. TRIG and VEL sit on the raised
// plate; TIMBRE and PRESS recede into a tray below, so the trigger and velocity stay the visual focus.
const Rect kExpressionTray(4.8f, 78.5f, 81.8f, 28);
constexpr float kTrigRow = 61, kVelRow = 73.5f, kTimbreRow = 89.5f, kPressRow = 102;
constexpr float kLaneX[8] = {8.2f, 18.75f, 29.3f, 39.85f, 50.4f, 60.95f, 71.5f, 82.05f};
constexpr float kPolyRow = 115;
constexpr float kPolyX[5] = {11.2f, 28.5f, 45.7f, 63.f, 80.3f};
constexpr float kLightAbove = 6.4f;

/// The lane's name above its jack strip, in the lane's colour, so the panel mirrors the drum editor's lanes.
struct LaneHeader : rack::widget::Widget {
    LaneHeader(DrumOutModule* module, int lane) : module(module), lane(lane) {}

    void draw(const DrawArgs& args) override {
        const theme::Palette& c = theme::panelColors();
        std::string name = "Lane " + std::to_string(lane + 1);
        NVGcolor color = c.text2;
        if (module) {
            if (TesseraModule* src = module->source()) {
                const Song& song = src->song();
                int index = song.trackIndex(module->binding().track);
                if (index >= 0 && lane < int(song.tracks[size_t(index)].drumLanes.size())) {
                    const DrumLane& l = song.tracks[size_t(index)].drumLanes[size_t(lane)];
                    if (!l.name.empty())
                        name = l.name;
                    if (l.color)
                        color = theme::rgb(l.color);
                }
            }
        }
        // `draw` alone would find this class's draw() method; qualify the helpers' namespace.
        tessera::draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kPanelLaneName, color,
                                       NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE, 0.f};
        tessera::draw::text(args.vg, style, box.size.x / 2, box.size.y - 1.2f,
                            tessera::draw::ellipsize(args.vg, style, name, box.size.x));
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

        // The track stepper flanks the display.
        addStepButton(IconButton::Icon::Previous, Vec(12.5f, kButtonRow), "Previous drum track", -1);
        addStepButton(IconButton::Icon::Next, Vec(78.9f, kButtonRow), "Next drum track", +1);

        panel.art().addPlate(kOutputPlate);
        panel.art().addSunkPlate(kExpressionTray);
        for (int i = 0; i < M::kTrigJacks; i++) {
            std::string lane = std::to_string(i + 1);
            float x = kLaneX[i];
            panel.output(Vec(x, kTrigRow), M::TRIG_OUTPUT + i, lane);
            panel.output(Vec(x, kVelRow), M::VEL_OUTPUT + i, "V" + lane);
            panel.output(Vec(x, kTimbreRow), M::TIMBRE_OUTPUT + i, "T" + lane);
            panel.output(Vec(x, kPressRow), M::PRESS_OUTPUT + i, "P" + lane);
            addChild(rack::createLightCentered<rack::componentlibrary::SmallLight<PlayLight>>(
                rack::mm2px(Vec(x - 4.2f, kTrigRow - kLightAbove)), module, M::TRIG_LIGHT + i));
            auto* header = new LaneHeader(module, i);
            header->box = Rect(rack::mm2px(Vec(x - 5.3f, 47.6f)), rack::mm2px(Vec(10.6f, 5.8f)));
            addChild(header);
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
