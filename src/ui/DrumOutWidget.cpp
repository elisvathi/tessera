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

// Two layouts: the compact module is 10 HP (a trigger and a velocity jack per lane); expanded — the module
// menu's "Expression outputs" — it is 18 HP and every lane also has its timbre and pressure jacks.
constexpr int kCompactHp = 10;
constexpr int kExpandedHp = 18;

// Layout in mm. The compact panel is 50.8 x 128.5, the expanded 91.4 x 128.5.
const Rect kDisplay(3, 12.5f, 44.8f, 24);
const Rect kWideDisplay(23.3f, 12.5f, 44.8f, 24); // the same display, centred on the wide panel
constexpr float kButtonRow = 41.5f;
const Vec kButtonSize(12, 6);
constexpr float kLightAbove = 6.4f;

// Compact: two lanes per row, each a trigger jack and its velocity jack side by side.
const Rect kCompactPlate(2.5f, 47, 45.8f, 75);
constexpr float kCompactColumns[4] = {8.6f, 18.4f, 32.4f, 42.2f};
constexpr float kCompactLaneRows[4] = {57, 70, 83, 96};
constexpr float kCompactPolyRow = 112;

// Expanded: every lane is a group of four jacks (TRIG, VEL, TIMBRE, PRESS); two lanes per row, a clear gap
// between the left and the right lane.
const Rect kExpandedPlate(2.5f, 45, 87, 78);
constexpr float kExpandedLaneRows[4] = {56, 68.5f, 81, 93.5f};
constexpr float kExpandedGroups[2][4] = {{10.5f, 21, 31.5f, 42}, {56, 66.5f, 77, 87.5f}};
constexpr float kExpandedPolyRow = 107;

} // namespace

struct DrumOutWidget : rack::app::ModuleWidget {
    explicit DrumOutWidget(DrumOutModule* module) {
        setModule(module);
        builtExpanded = module->expanded;
        build(builtExpanded);
    }

    void step() override {
        if (auto* m = outputModule()) {
            m->refreshBinding();
            if (m->expanded != builtExpanded) {
                builtExpanded = m->expanded;
                build(builtExpanded);
            }
        }
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
        menu->addChild(rack::createBoolMenuItem("Expression outputs (TIMBRE / PRESS per lane)", "",
                                                [m] { return m->expanded; }, [m](bool on) { m->expanded = on; }));
    }

  private:
    bool builtExpanded = false;

    /// (Re)builds the panel and its controls for the compact or the expanded layout.
    void build(bool expand) {
        using M = DrumOutModule;
        DrumOutModule* module = outputModule();
        int hp = expand ? kExpandedHp : kCompactHp;
        box.size = rack::math::Vec(float(hp) * rack::RACK_GRID_WIDTH, rack::RACK_GRID_HEIGHT);
        clearChildren();
        PanelBuilder panel(this, hp, "DRUM OUT");

        const Rect& displayRect = expand ? kWideDisplay : kDisplay;
        auto* display = new TrackOutDisplay(module);
        display->box = Rect(rack::mm2px(displayRect.pos), rack::mm2px(displayRect.size));
        addChild(display);

        float centerShift = expand ? kWideDisplay.pos.x - kDisplay.pos.x : 0;
        addStepButton(IconButton::Icon::Previous, Vec(12 + centerShift, kButtonRow), "Previous drum track", -1);
        addStepButton(IconButton::Icon::Next, Vec(38.8f + centerShift, kButtonRow), "Next drum track", +1);

        panel.art().addPlate(expand ? kExpandedPlate : kCompactPlate);
        for (int i = 0; i < M::kTrigJacks; i++) {
            std::string lane = std::to_string(i + 1);
            int row = i / 2, side = i % 2;
            float y = expand ? kExpandedLaneRows[row] : kCompactLaneRows[row];
            Vec trig(expand ? kExpandedGroups[side][0] : kCompactColumns[side * 2], y);
            panel.output(trig, M::TRIG_OUTPUT + i, lane);
            panel.output(Vec(expand ? kExpandedGroups[side][1] : kCompactColumns[side * 2 + 1], y), M::VEL_OUTPUT + i,
                         "V" + lane);
            if (expand) {
                panel.output(Vec(kExpandedGroups[side][2], y), M::TIMBRE_OUTPUT + i, "T" + lane);
                panel.output(Vec(kExpandedGroups[side][3], y), M::PRESS_OUTPUT + i, "P" + lane);
            }
            addChild(rack::createLightCentered<rack::componentlibrary::SmallLight<PlayLight>>(
                rack::mm2px(trig.plus(Vec(-4.2f, -kLightAbove))), module, M::TRIG_LIGHT + i));
        }
        if (expand) {
            panel.output(Vec(kExpandedGroups[0][0], kExpandedPolyRow), M::TRIG_POLY_OUTPUT, "TRIGS");
            panel.output(Vec(kExpandedGroups[0][2], kExpandedPolyRow), M::VEL_POLY_OUTPUT, "VEL");
            panel.output(Vec(kExpandedGroups[1][0], kExpandedPolyRow), M::TIMBRE_POLY_OUTPUT, "TIMBRE");
            panel.output(Vec(kExpandedGroups[1][2], kExpandedPolyRow), M::PRESS_POLY_OUTPUT, "PRESS");
            panel.output(Vec(kExpandedGroups[1][3], kExpandedPolyRow), M::AUTO_OUTPUT, "AUTO");
        }
    }
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
