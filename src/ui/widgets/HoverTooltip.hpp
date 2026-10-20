#pragma once

#include <rack.hpp>

#include <string>

namespace tessera {

/// Shows a Rack tooltip after the pointer rests on the owning widget for a moment.
/// The owner forwards enter / leave and calls step() every frame.
class HoverTooltip {
  public:
    static constexpr double kDelaySeconds = 0.5;

    explicit HoverTooltip(std::string text) : text(std::move(text)) {}
    ~HoverTooltip() { hide(); }

    /// Changes the text for the next time it shows; used by views whose parts each have their own tip.
    void setText(std::string next) { text = std::move(next); }
    void enter() { hoverStart = rack::system::getTime(); }
    void leave() {
        hoverStart = -1;
        hide();
    }
    void step() {
        if (hoverStart >= 0 && !tooltip && rack::system::getTime() - hoverStart >= kDelaySeconds) {
            tooltip = new rack::ui::Tooltip;
            tooltip->text = text;
            APP->scene->addChild(tooltip);
        }
    }

  private:
    void hide() {
        // Let the scene delete it in its own step: the owner may be destroyed while the scene is walking its
        // children (closing the editor), and removing a sibling there leaves the scene on a freed entry.
        if (tooltip) {
            tooltip->requestDelete();
            tooltip = nullptr;
        }
    }

    std::string text;
    rack::ui::Tooltip* tooltip = nullptr;
    double hoverStart = -1;
};

} // namespace tessera
