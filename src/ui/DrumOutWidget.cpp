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

constexpr int kHp = 20;

// Layout in mm. The panel is 101.6 x 128.5.
const Rect kDisplay(4, 12.5f, 93.6f, 24);
constexpr float kButtonRow = 40.75f; // the arrows sit exactly halfway between the display and the plate
const Vec kButtonSize(12, 6);
const Rect kOutputPlate(2.5f, 45, 96.6f, 78.5f);
// One mixer-style strip per lane output: a colour chip with its number, the hit light, then five jacks. The rows are
// named once, at the left, instead of a letter in every strip.
constexpr float kStripW = 9.6f, kStripPitch = 10.44f, kFirstStrip = 20.7f;
constexpr float kStripTop = 46.5f, kStripH = 60;
constexpr float kChipTop = 47.3f, kChipH = 4;
constexpr float kLightRow = 53.4f;
constexpr float kRows[5] = {59, 69.5f, 80, 90.5f, 101}; // trigger, velocity, pitch, timbre, pressure
constexpr float kRowNameRight = 14.4f;
constexpr float kPolyRow = 116.5f, kFirstPoly = 10.55f, kPolyPitch = 16.1f;

/// The drum track this Drum Out plays, or nullptr when it has no source or the track is gone.
const Track* trackOf(DrumOutModule* module) {
    TesseraModule* src = module ? module->source() : nullptr;
    if (!src)
        return nullptr;
    const Song& song = src->song();
    int index = song.trackIndex(module->binding().track);
    return index < 0 ? nullptr : &song.tracks[size_t(index)];
}

/// The lanes that play on lane output `output` (several lanes may share one), in the lanes' order.
std::vector<const DrumLane*> lanesOn(const Track& track, int output) {
    std::vector<const DrumLane*> out;
    for (const DrumLane& l : track.drumLanes)
        if (l.outputIndex == output)
            out.push_back(&l);
    return out;
}

/// The strip's colour chip: the colour of the first lane on this output (the track's when the lane has none) with
/// the output's number, like a mixer channel's coloured tag. Grey while no lane plays there.
struct LaneChip : rack::widget::Widget {
    LaneChip(DrumOutModule* module, int output) : module(module), output(output) {}

    void draw(const DrawArgs& args) override {
        const theme::Palette& c = theme::panelColors();
        NVGcolor chip = c.bg2;
        NVGcolor ink = c.text1;
        if (const Track* t = trackOf(module)) {
            std::vector<const DrumLane*> lanes = lanesOn(*t, output);
            if (!lanes.empty()) {
                chip = theme::rgb(lanes[0]->color ? lanes[0]->color : t->color);
                ink = c.ink;
            }
        }
        draw::roundedRect(args.vg, Rect(Vec(), box.size), rack::mm2px(1.4f), chip);
        tessera::draw::TextStyle number{theme::fonts::uiStrong(), theme::metrics::kPanelLabel, ink,
                                        NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        tessera::draw::text(args.vg, number, box.size.x / 2, box.size.y / 2 + 0.3f, std::to_string(output + 1));
    }

    DrumOutModule* module;
    int output;
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

        addStepButton(IconButton::Icon::Previous, Vec(44.3f, kButtonRow), "Previous drum track", -1);
        addStepButton(IconButton::Icon::Next, Vec(57.3f, kButtonRow), "Next drum track", +1);

        panel.art().addPlate(kOutputPlate);
        const char* rowNames[5] = {"TRIG", "VEL", "PITCH", "TIMBRE", "PRESS"};
        for (int r = 0; r < 5; r++)
            panel.art().addLabel(Vec(kRowNameRight, kRows[r]), rowNames[r], PanelArt::LabelKind::RowName);
        for (int i = 0; i < M::kTrigJacks; i++) {
            float x = kFirstStrip + float(i) * kStripPitch;
            panel.art().addSunkPlate(Rect(x - kStripW / 2, kStripTop, kStripW, kStripH));
            auto* chip = new LaneChip(module, i);
            chip->box =
                Rect(rack::mm2px(Vec(x - kStripW / 2 + 0.6f, kChipTop)), rack::mm2px(Vec(kStripW - 1.2f, kChipH)));
            addChild(chip);
            const int jacks[5] = {M::TRIG_OUTPUT + i, M::VEL_OUTPUT + i, M::PITCH_OUTPUT + i, M::TIMBRE_OUTPUT + i,
                                  M::PRESS_OUTPUT + i};
            for (int r = 0; r < 5; r++)
                panel.output(Vec(x, kRows[r]), jacks[r], "");
            // A smaller-than-stock light: the stock 2 mm halo would touch the trigger jack.
            auto* light = new rack::componentlibrary::SmallLight<PlayLight>;
            light->module = module;
            light->firstLightId = M::TRIG_LIGHT + i;
            light->box.size = rack::mm2px(Vec(1.6f, 1.6f));
            light->box.pos = rack::mm2px(Vec(x, kLightRow)).minus(light->box.size.div(2));
            addChild(light);
        }
        const char* polyLabels[6] = {"TRIGS", "VEL", "PITCH", "TIMBRE", "PRESS", "AUTO"};
        const int polyOutputs[6] = {M::TRIG_POLY_OUTPUT,   M::VEL_POLY_OUTPUT,   M::PITCH_POLY_OUTPUT,
                                    M::TIMBRE_POLY_OUTPUT, M::PRESS_POLY_OUTPUT, M::AUTO_OUTPUT};
        for (int k = 0; k < 6; k++)
            panel.output(Vec(kFirstPoly + float(k) * kPolyPitch, kPolyRow), polyOutputs[k], polyLabels[k]);
    }

    void step() override {
        if (auto* m = outputModule()) {
            m->refreshBinding();
            nameJacks(m);
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
    }

  private:
    /// The module this widget shows, or nullptr in the module browser.
    DrumOutModule* outputModule() const { return static_cast<DrumOutModule*>(module); }

    /// Names each lane jack after the lanes that play on it ("Snare pitch"), so hovering a jack says which drum it
    /// is; back to "Lane 3 pitch" while no lane plays there.
    void nameJacks(DrumOutModule* m) {
        using M = DrumOutModule;
        const Track* track = trackOf(m);
        for (int i = 0; i < M::kTrigJacks; i++) {
            std::string name;
            if (track)
                for (const DrumLane* l : lanesOn(*track, i))
                    name += (name.empty() ? "" : " + ") + l->name;
            if (name.empty())
                name = "Lane " + std::to_string(i + 1);
            if (name == jackNames[size_t(i)])
                continue;
            jackNames[size_t(i)] = name;
            m->outputInfos[size_t(M::TRIG_OUTPUT + i)]->name = name;
            m->outputInfos[size_t(M::VEL_OUTPUT + i)]->name = name + " velocity";
            m->outputInfos[size_t(M::PITCH_OUTPUT + i)]->name = name + " pitch";
            m->outputInfos[size_t(M::TIMBRE_OUTPUT + i)]->name = name + " timbre";
            m->outputInfos[size_t(M::PRESS_OUTPUT + i)]->name = name + " pressure";
            m->lightInfos[size_t(M::TRIG_LIGHT + i)]->name = name;
        }
    }

    std::array<std::string, DrumOutModule::kTrigJacks> jackNames;

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
