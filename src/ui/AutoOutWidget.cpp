#include "../model/Automation.hpp"
#include "../module/AutoOutModule.hpp"
#include "../module/TesseraModule.hpp"
#include "../plugin.hpp"
#include "ModuleUndo.hpp"
#include "OutputMenus.hpp"
#include "Theme.hpp"
#include "TrackOutDisplay.hpp"
#include "widgets/Draw.hpp"
#include "widgets/IconButton.hpp"
#include "widgets/Panel.hpp"

namespace tessera {

namespace {

using rack::math::Rect;
using rack::math::Vec;

constexpr int kHp = 8;

// Layout in mm. The panel is 40.64 x 128.5.
const Rect kDisplay(3, 12.5f, 34.64f, 26);
constexpr float kButtonRow = 43;
const Vec kButtonSize(12, 6);
const Rect kOutputPlate(2.5f, 48, 35.64f, 74);
const Rect kLaneScreen(4, 50, 23.5f, 70);
constexpr float kJackX = 32.6f;
constexpr float kFirstRow = 54.5f;
constexpr float kRowStep = 8.7f;

/// The lanes' names beside their jacks, each with a bar showing its current value, so a patch reads at a
/// glance: "Cutoff → this jack".
class LaneScreen : public rack::widget::Widget {
  public:
    explicit LaneScreen(AutoOutModule* module) : module(module) {}

    void draw(const DrawArgs& args) override {
        const theme::Palette& c = theme::screenColors();
        Rect r(Vec(), box.size);
        draw::roundedRect(args.vg, r, theme::metrics::kPanelDisplayRadius, c.bg0);
        draw::roundedRectStroke(args.vg, r, theme::metrics::kPanelDisplayRadius, c.line0, 1.f);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer == 1)
            drawLanes(args.vg);
        Widget::drawLayer(args, layer);
    }

  private:
    struct Row {
        std::string name;
        uint32_t color = 0;
        float low = 0, high = 10;
        bool used = false;
    };

    std::array<Row, AutoOutModule::kLanes> gather() {
        std::array<Row, AutoOutModule::kLanes> rows;
        TesseraModule* src = module ? module->source() : nullptr;
        const Track* t =
            src && module->status() == BindingStatus::Bound ? src->song().findTrack(module->binding().track) : nullptr;
        if (!module) {
            // The module browser's preview.
            rows[0] = {"Cutoff", 0xFF8A3D, 0, 10, true};
            rows[1] = {"Resonance", 0x7AA7FF, 0, 10, true};
            return rows;
        }
        if (!t)
            return rows;
        for (const AutomationEnvelope& lane : t->automation)
            if (drivesOutput(lane) && lane.outputIndex >= 0 && lane.outputIndex < AutoOutModule::kLanes)
                rows[size_t(lane.outputIndex)] = {lane.name, t->color, lane.rangeMin, lane.rangeMax, true};
        // Clip lanes on a channel the track has none on still name it.
        auto fromClip = [&](ClipId id) {
            if (const Clip* clip = src->song().findClip(id))
                for (const AutomationEnvelope& lane : clip->automation) {
                    size_t o = size_t(lane.outputIndex);
                    if (drivesOutput(lane) && o < rows.size() && !rows[o].used)
                        rows[o] = {lane.name, t->color, lane.rangeMin, lane.rangeMax, true};
                }
        };
        for (const ClipSlot& s : t->slots)
            if (s.clip)
                fromClip(*s.clip);
        for (const ArrangerClip& a : t->arrangement)
            fromClip(a.clip);
        return rows;
    }

    void drawLanes(NVGcontext* vg) {
        const theme::Palette& c = theme::screenColors();
        auto rows = gather();
        float px = box.size.x;
        float step = rack::mm2px(kRowStep);
        float first = rack::mm2px(kFirstRow - kLaneScreen.pos.y);
        draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kDisplaySmall, c.text0,
                             NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE};
        draw::TextStyle empty{theme::fonts::ui(), theme::metrics::kDisplaySmall, c.text2,
                              NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE};
        const float pad = 5;
        for (size_t i = 0; i < rows.size(); i++) {
            float y = first + float(i) * step;
            const Row& row = rows[i];
            if (!row.used) {
                draw::text(vg, empty, pad, y + 1, std::to_string(i + 1) + "  —");
                continue;
            }
            draw::text(vg, name, pad, y + 1,
                       draw::ellipsize(vg, name, std::to_string(i + 1) + "  " + row.name, px - 2 * pad));
            // The lane's value within its voltage range.
            float v = module ? module->volts[i].load(std::memory_order_relaxed) : (i == 0 ? 6.f : 3.f);
            float f = row.high != row.low ? (v - row.low) / (row.high - row.low) : 0;
            Rect track(pad, y + 4, px - 2 * pad, 2.5f);
            draw::roundedRect(vg, track, 1, c.line0);
            draw::roundedRect(vg, Rect(track.pos, Vec(track.size.x * std::clamp(f, 0.f, 1.f), track.size.y)), 1,
                              theme::rgb(row.color));
        }
    }

    AutoOutModule* module;
};

} // namespace

struct AutoOutWidget : rack::app::ModuleWidget {
    explicit AutoOutWidget(AutoOutModule* module) {
        using M = AutoOutModule;
        setModule(module);
        PanelBuilder panel(this, kHp, "AUTO OUT");

        auto* display = new TrackOutDisplay(module);
        display->box = Rect(rack::mm2px(kDisplay.pos), rack::mm2px(kDisplay.size));
        addChild(display);

        addStepButton(IconButton::Icon::Previous, Vec(12, kButtonRow), "Previous track", -1);
        addStepButton(IconButton::Icon::Next, Vec(28.64f, kButtonRow), "Next track", +1);

        panel.art().addPlate(kOutputPlate);
        auto* screen = new LaneScreen(module);
        screen->box = Rect(rack::mm2px(kLaneScreen.pos), rack::mm2px(kLaneScreen.size));
        addChild(screen);
        for (int i = 0; i < M::kLanes; i++)
            addOutput(rack::createOutputCentered<PanelJack>(rack::mm2px(Vec(kJackX, kFirstRow + i * kRowStep)), module,
                                                            M::LANE_OUTPUT + i));
    }

    void step() override {
        if (auto* m = outputModule())
            m->refreshBinding();
        ModuleWidget::step();
    }

    void appendContextMenu(rack::ui::Menu* menu) override {
        AutoOutModule* m = outputModule();
        if (!m)
            return;
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createSubmenuItem("Source", m->binding().sourceName,
                                               [m](rack::ui::Menu* sub) { appendSourceMenu(sub, m, "Auto Out"); }));
        menu->addChild(rack::createSubmenuItem(
            "Track", "", [m](rack::ui::Menu* sub) { appendTrackMenu(sub, m, "Auto Out"); }, m->source() == nullptr));
    }

  private:
    AutoOutModule* outputModule() const { return static_cast<AutoOutModule*>(module); }

    void addStepButton(IconButton::Icon icon, Vec centerMm, const std::string& tooltip, int delta) {
        auto* button = new IconButton(icon, tooltip, [this, delta] {
            if (auto* m = outputModule())
                withModuleUndo(m, "change Auto Out track", [&] { m->stepTrack(delta); });
        });
        button->box.size = rack::mm2px(kButtonSize);
        button->box.pos = rack::mm2px(centerMm).minus(button->box.size.div(2));
        addChild(button);
    }
};

} // namespace tessera

rack::plugin::Model* modelAutoOut = rack::createModel<tessera::AutoOutModule, tessera::AutoOutWidget>("TesseraAutoOut");
