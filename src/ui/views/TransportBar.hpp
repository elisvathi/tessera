#pragma once

#include "../EditorContext.hpp"

#include <rack.hpp>

#include <functional>

namespace tessera {

class ToolButton;

/// The strip across the top of the editor (DESIGN.md §15): transport, position, tempo, meter, key, mode,
/// and view toggles. Buttons show their shortcut in the tooltip.
class TransportBar : public rack::widget::Widget {
  public:
    /// `onClose` closes the editor.
    TransportBar(EditorContext& ctx, std::function<void()> onClose);

    void step() override;
    void draw(const DrawArgs& args) override;

  private:
    void layout();

    EditorContext& ctx;
    std::vector<rack::widget::Widget*> left;  // laid out left to right
    std::vector<rack::widget::Widget*> right; // laid out right to left
    rack::widget::Widget* positionSlot;
    ToolButton* nameButton;
    std::vector<rack::widget::Widget*> collapsible; // hidden in this order when the bar is too narrow
    ToolButton* timeSigButton;
    ToolButton* followButton;
    FollowMode lastFollow = FollowMode::Page; // what the follow icon turns back on
    float nameWidth = 110;
};

} // namespace tessera
