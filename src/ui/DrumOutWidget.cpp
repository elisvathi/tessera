#include "../module/DrumOutModule.hpp"
#include "../plugin.hpp"
#include "ModuleUndo.hpp"
#include "OutputMenus.hpp"
#include "TrackOutDisplay.hpp"
#include "widgets/IconButton.hpp"
#include "widgets/Lights.hpp"
#include "widgets/Panel.hpp"

namespace tessera {

namespace {

using rack::math::Rect;
using rack::math::Vec;

constexpr int kHp = 10;

// Layout in mm. The panel is 50.8 x 128.5.
const Rect kDisplay(3, 12.5f, 44.8f, 24);
constexpr float kButtonRow = 41.5f;
const Vec kButtonSize(12, 6);
const Rect kOutputPlate(2.5f, 47, 45.8f, 75);
// Two lanes per row, each a trigger jack and its velocity jack side by side.
constexpr float kColumns[4] = {8.6f, 18.4f, 32.4f, 42.2f};
constexpr float kLaneRows[4] = {57, 70, 83, 96};
constexpr float kPolyRow = 112;
constexpr float kLightAbove = 6.4f;

} // namespace

struct DrumOutWidget : rack::app::ModuleWidget {
    explicit DrumOutWidget(DrumOutModule* module) {
        using M = DrumOutModule;
        setModule(module);
        PanelBuilder panel(this, kHp, "DRUM OUT");

        auto* display = new TrackOutDisplay(module);
        display->box = Rect(rack::mm2px(kDisplay.pos), rack::mm2px(kDisplay.size));
        addChild(display);

        addStepButton(IconButton::Icon::Previous, Vec(12, kButtonRow), "Previous drum track", -1);
        addStepButton(IconButton::Icon::Next, Vec(38.8f, kButtonRow), "Next drum track", +1);

        panel.art().addPlate(kOutputPlate);
        for (int i = 0; i < M::kTrigJacks; i++) {
            std::string lane = std::to_string(i + 1);
            float row = kLaneRows[i / 2];
            Vec trig(kColumns[(i % 2) * 2], row);
            panel.output(trig, M::TRIG_OUTPUT + i, lane);
            panel.output(Vec(kColumns[(i % 2) * 2 + 1], row), M::VEL_OUTPUT + i, "V" + lane);
            addChild(rack::createLightCentered<rack::componentlibrary::SmallLight<PlayLight>>(
                rack::mm2px(trig.plus(Vec(-4.2f, -kLightAbove))), module, M::TRIG_LIGHT + i));
        }
        panel.output(Vec(kColumns[0] + 2, kPolyRow), M::TRIG_POLY_OUTPUT, "TRIGS");
        panel.output(Vec((kColumns[1] + kColumns[2]) / 2, kPolyRow), M::VEL_POLY_OUTPUT, "VEL");
        panel.output(Vec(kColumns[3] - 2, kPolyRow), M::AUTO_OUTPUT, "AUTO");
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
