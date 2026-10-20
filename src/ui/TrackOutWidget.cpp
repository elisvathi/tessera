#include "../module/Registry.hpp"
#include "../module/TesseraModule.hpp"
#include "../module/TrackOutModule.hpp"
#include "../plugin.hpp"
#include "ModuleUndo.hpp"
#include "OutputMenus.hpp"
#include "Theme.hpp"
#include "TrackOutDisplay.hpp"
#include "widgets/IconButton.hpp"
#include "widgets/Panel.hpp"

#include <algorithm>

namespace tessera {

namespace {

using rack::math::Rect;
using rack::math::Vec;

constexpr int kHp = 6;

// Layout in mm. The panel is 30.48 x 128.5.
const Rect kDisplay(3, 12.5f, 24.48f, 30);
constexpr float kButtonRow = 48.5f;
const Vec kButtonSize(9.5f, 6);
const Rect kOutputPlate(2.5f, 56, 25.48f, 65);
constexpr float kLeft = 9.f;
constexpr float kRight = 21.48f;
constexpr float kCenter = 15.24f;
constexpr float kRows[4] = {66, 82, 98, 114};

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

        panel.art().addPlate(kOutputPlate);
        panel.output(Vec(kLeft, kRows[0]), M::VOCT_OUTPUT, "V/OCT");
        panel.output(Vec(kRight, kRows[0]), M::GATE_OUTPUT, "GATE");
        panel.output(Vec(kLeft, kRows[1]), M::VEL_OUTPUT, "VEL");
        panel.output(Vec(kRight, kRows[1]), M::REL_OUTPUT, "REL");
        panel.output(Vec(kLeft, kRows[2]), M::TIMBRE_OUTPUT, "TIMB");
        panel.output(Vec(kRight, kRows[2]), M::PRESS_OUTPUT, "PRESS");
        panel.output(Vec(kCenter, kRows[3]), M::AUTO_OUTPUT, "AUTO");
    }

    void step() override {
        if (auto* m = outputModule())
            m->refreshBinding();
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
    rack::createModel<tessera::TrackOutModule, tessera::TrackOutWidget>("TesseraTrackOut");
