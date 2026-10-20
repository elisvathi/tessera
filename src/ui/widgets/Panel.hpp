#pragma once

#include "../Theme.hpp"

#include <rack.hpp>

#include <string>
#include <vector>

namespace tessera {

// Rack's light and dark jacks and screws, picked by the panel's scheme rather than by Rack's setting alone.
struct PanelJack : rack::componentlibrary::ThemedPJ301MPort {
    void step() override {
        rack::app::SvgPort::setSvg(theme::panelColors().dark ? darkSvg : lightSvg);
        rack::app::SvgPort::step();
    }
};
struct PanelScrew : rack::componentlibrary::ThemedScrew {
    void step() override {
        rack::app::SvgScrew::setSvg(theme::panelColors().dark ? darkSvg : lightSvg);
        rack::app::SvgScrew::step();
    }
};

/// Static module panel artwork drawn from theme tokens instead of an SVG, so panels follow the theme and
/// labels sit exactly where the builder put the controls. Positions are in mm, like Rack's helpers.
class PanelArt : public rack::widget::Widget {
  public:
    enum class LabelKind { Control, Output, Section, Title };

    explicit PanelArt(rack::math::Vec sizePx) { box.size = sizePx; }

    void addLabel(rack::math::Vec mm, const std::string& text, LabelKind kind);
    /// A raised area that groups outputs (Rack convention: outputs sit on a contrasting plate).
    void addPlate(rack::math::Rect mm);

    void draw(const DrawArgs& args) override;
    void step() override;

  private:
    uint64_t drawnGeneration = 0;
    struct Label {
        rack::math::Vec mm;
        std::string text;
        LabelKind kind;
    };
    std::vector<Label> labels;
    std::vector<rack::math::Rect> plates;
};

/// Builds a module panel: the cached artwork, screws, and controls with their labels in one call each.
class PanelBuilder {
  public:
    PanelBuilder(rack::app::ModuleWidget* widget, int hp, const std::string& title);

    PanelArt& art() { return *artWidget; }
    /// The screws on the right edge, so a resizable module can move them with its edge.
    std::vector<rack::widget::Widget*> rightScrews() const { return rightScrewWidgets; }

    void input(rack::math::Vec mm, int inputId, const std::string& label);
    void output(rack::math::Vec mm, int outputId, const std::string& label);

    /// A param control with an optional label below it.
    template <typename TParamWidget>
    TParamWidget* param(rack::math::Vec mm, int paramId, const std::string& label = "", float labelBelowMm = 7.f) {
        auto* w = rack::createParamCentered<TParamWidget>(rack::mm2px(mm), mw->getModule(), paramId);
        mw->addParam(w);
        if (!label.empty())
            art().addLabel(mm.plus(rack::math::Vec(0, labelBelowMm)), label, PanelArt::LabelKind::Control);
        return w;
    }

    template <typename TParamWidget>
    TParamWidget* lightParam(rack::math::Vec mm, int paramId, int lightId, const std::string& label = "",
                             float labelBelowMm = 7.f) {
        auto* w = rack::createLightParamCentered<TParamWidget>(rack::mm2px(mm), mw->getModule(), paramId, lightId);
        mw->addParam(w);
        if (!label.empty())
            art().addLabel(mm.plus(rack::math::Vec(0, labelBelowMm)), label, PanelArt::LabelKind::Control);
        return w;
    }

  private:
    rack::app::ModuleWidget* mw;
    PanelArt* artWidget;
    std::vector<rack::widget::Widget*> rightScrewWidgets;
};

} // namespace tessera
