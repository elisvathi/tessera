#include "Panel.hpp"

#include "../Theme.hpp"
#include "Draw.hpp"

namespace tessera {

namespace {

constexpr float kTitleBaselineMm = 8.5f;

draw::TextStyle labelStyle(PanelArt::LabelKind kind) {
    using namespace theme;
    const Palette& c = panelColors();
    const int center = NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE;
    switch (kind) {
    case PanelArt::LabelKind::Title:
        return {fonts::uiStrong(), metrics::kPanelTitle, c.text0, center, 2.f};
    case PanelArt::LabelKind::Section:
        return {fonts::uiStrong(), metrics::kPanelLabel, c.text2, center, metrics::kPanelLabelSpacing};
    case PanelArt::LabelKind::RowName:
        return {fonts::uiStrong(), metrics::kPanelLabel, c.text1, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                metrics::kPanelLabelSpacing};
    case PanelArt::LabelKind::Output:
        return {fonts::uiStrong(), metrics::kPanelLabel, c.text0, center, metrics::kPanelLabelSpacing};
    case PanelArt::LabelKind::Control:
    default:
        return {fonts::uiStrong(), metrics::kPanelLabel, c.text1, center, metrics::kPanelLabelSpacing};
    }
}

} // namespace

void PanelArt::addLabel(rack::math::Vec mm, const std::string& text, LabelKind kind) {
    labels.push_back({mm, text, kind});
}

void PanelArt::addPlate(rack::math::Rect mm) { plates.push_back(mm); }

void PanelArt::addSunkPlate(rack::math::Rect mm) { sunkPlates.push_back(mm); }

void PanelArt::addGroupBox(rack::math::Rect mm, const std::string& title) { groupBoxes.push_back({mm, title}); }

void PanelArt::step() {
    // The artwork is cached; repaint it when Rack's panel setting (or the scheme) changes.
    if (drawnGeneration != theme::generation()) {
        drawnGeneration = theme::generation();
        if (auto* fb = dynamic_cast<rack::widget::FramebufferWidget*>(parent))
            fb->setDirty();
    }
    Widget::step();
}

void PanelArt::draw(const DrawArgs& args) {
    const theme::Palette& c = theme::panelColors();
    NVGcontext* vg = args.vg;

    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    draw::roundedRectStroke(vg, rack::math::Rect(rack::math::Vec(), box.size), 0, c.line0, 1.f);

    for (const rack::math::Rect& p : plates)
        draw::roundedRect(vg, rack::math::Rect(rack::mm2px(p.pos), rack::mm2px(p.size)),
                          theme::metrics::kPanelPlateRadius, c.bg2);

    for (const rack::math::Rect& p : sunkPlates) {
        rack::math::Rect r(rack::mm2px(p.pos), rack::mm2px(p.size));
        draw::roundedRect(vg, r, theme::metrics::kPanelPlateRadius, c.bg1);
        draw::roundedRectStroke(vg, r, theme::metrics::kPanelPlateRadius, c.line0, 0.75f);
    }

    for (const GroupBox& g : groupBoxes) {
        rack::math::Rect r(rack::mm2px(g.mm.pos), rack::mm2px(g.mm.size));
        draw::roundedRectStroke(vg, r, theme::metrics::kPanelPlateRadius, c.line2, 1.f);
        draw::TextStyle style = labelStyle(LabelKind::Section);
        style.align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE;
        // The panel colour behind the title cuts a gap into the top edge.
        float gap = draw::textWidth(vg, style, g.title) + 2 * theme::metrics::kPanelGroupTitlePad;
        float x = r.getCenter().x;
        draw::roundedRect(vg, rack::math::Rect(x - gap / 2, r.pos.y - 2, gap, 4), 0, c.bg1);
        draw::text(vg, style, x, r.pos.y + 0.5f, g.title);
    }

    for (const Label& l : labels) {
        rack::math::Vec px = rack::mm2px(l.mm);
        draw::text(vg, labelStyle(l.kind), px.x, px.y, l.text);
    }
}

PanelBuilder::PanelBuilder(rack::app::ModuleWidget* widget, int hp, const std::string& title) : mw(widget) {
    rack::math::Vec size(hp * rack::RACK_GRID_WIDTH, rack::RACK_GRID_HEIGHT);

    // The artwork never changes while the module is on screen, so cache it in a framebuffer.
    auto* fb = new rack::widget::FramebufferWidget;
    fb->box.size = size;
    artWidget = new PanelArt(size);
    fb->addChild(artWidget);
    mw->setPanel(fb);

    float widthMm = rack::RACK_GRID_WIDTH * hp / rack::mm2px(1.f);
    artWidget->addLabel(rack::math::Vec(widthMm / 2, kTitleBaselineMm), title, PanelArt::LabelKind::Title);

    const float right = size.x - 2 * rack::RACK_GRID_WIDTH;
    const float bottom = rack::RACK_GRID_HEIGHT - rack::RACK_GRID_WIDTH;
    mw->addChild(rack::createWidget<PanelScrew>(rack::math::Vec(rack::RACK_GRID_WIDTH, 0)));
    rightScrewWidgets.push_back(rack::createWidget<PanelScrew>(rack::math::Vec(right, bottom)));
    if (hp >= 10) {
        rightScrewWidgets.push_back(rack::createWidget<PanelScrew>(rack::math::Vec(right, 0)));
        mw->addChild(rack::createWidget<PanelScrew>(rack::math::Vec(rack::RACK_GRID_WIDTH, bottom)));
    }
    for (rack::widget::Widget* screw : rightScrewWidgets)
        mw->addChild(screw);
}

void PanelBuilder::input(rack::math::Vec mm, int inputId, const std::string& label) {
    mw->addInput(rack::createInputCentered<PanelJack>(rack::mm2px(mm), mw->getModule(), inputId));
    art().addLabel(mm.minus(rack::math::Vec(0, theme::metrics::kPanelLabelAboveJackMm)), label,
                   PanelArt::LabelKind::Control);
}

void PanelBuilder::output(rack::math::Vec mm, int outputId, const std::string& label) {
    mw->addOutput(rack::createOutputCentered<PanelJack>(rack::mm2px(mm), mw->getModule(), outputId));
    art().addLabel(mm.minus(rack::math::Vec(0, theme::metrics::kPanelLabelAboveJackMm)), label,
                   PanelArt::LabelKind::Output);
}

} // namespace tessera
