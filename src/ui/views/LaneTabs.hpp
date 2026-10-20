#pragma once

#include "../EditorContext.hpp"

#include <rack.hpp>

#include <functional>

namespace tessera {

/// The tabs above the piano roll's lower lane (EDITOR_UX §4.6): one click shows a note value, an operator, an
/// expression curve, or (last tab, a menu) one of the clip's automation lanes.
class LaneTabs : public rack::widget::OpaqueWidget {
  public:
    LaneTabs(EditorContext& ctx, std::function<void()> openAutomationMenu)
        : ctx(ctx), openAutomationMenu(std::move(openAutomationMenu)) {}

    /// The name shown on the automation tab while a clip lane is open.
    std::string automationName;

    void draw(const DrawArgs& args) override;
    void onButton(const ButtonEvent& e) override;
    void onHover(const HoverEvent& e) override;
    void onLeave(const LeaveEvent& e) override { hovered = -1; }

  private:
    /// Tab rectangles for the current width; the automation tab is the last.
    std::vector<rack::math::Rect> layout(NVGcontext* vg);
    int tabAt(rack::math::Vec pos);

    EditorContext& ctx;
    std::function<void()> openAutomationMenu;
    std::vector<rack::math::Rect> rects;
    int hovered = -1;
};

} // namespace tessera
