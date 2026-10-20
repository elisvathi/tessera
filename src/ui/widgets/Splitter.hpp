#pragma once

#include "../Theme.hpp"

#include <rack.hpp>

#include <functional>

namespace tessera {

/// A draggable divider between two panes. It reports the pointer position along its axis while dragging;
/// the owner turns that into a layout. Double-click runs `onDoubleClick` (maximize the focused pane).
class Splitter : public rack::widget::OpaqueWidget {
  public:
    enum class Orientation { Horizontal, Vertical }; // Horizontal = a horizontal bar, dragged up and down

    Splitter(Orientation o, std::function<void(float pos)> onDrag, std::function<void()> onDoubleClick)
        : orientation(o), dragged(std::move(onDrag)), doubleClicked(std::move(onDoubleClick)) {}

    void draw(const DrawArgs& args) override {
        const theme::Palette& c = theme::colors();
        nvgBeginPath(args.vg);
        if (orientation == Orientation::Horizontal)
            nvgRect(args.vg, 0, box.size.y / 2 - 0.5f, box.size.x, 1);
        else
            nvgRect(args.vg, box.size.x / 2 - 0.5f, 0, 1, box.size.y);
        nvgFillColor(args.vg, hovered || dragging ? c.accent : c.line1);
        nvgFill(args.vg);
    }
    void onEnter(const EnterEvent& e) override { hovered = true; }
    void onLeave(const LeaveEvent& e) override { hovered = false; }
    void onDragStart(const DragStartEvent& e) override { dragging = true; }
    void onDragEnd(const DragEndEvent& e) override { dragging = false; }
    void onDragMove(const DragMoveEvent& e) override {
        // Position of the pointer in the parent's coordinates, along the drag axis.
        rack::math::Vec origin = parent->getAbsoluteOffset(rack::math::Vec());
        rack::math::Vec p = APP->scene->getMousePos().minus(origin).div(parent->getAbsoluteZoom());
        dragged(orientation == Orientation::Horizontal ? p.y : p.x);
    }
    void onDoubleClick(const DoubleClickEvent& e) override {
        if (doubleClicked)
            doubleClicked();
    }

    bool active() const { return hovered || dragging; }

  private:
    Orientation orientation;
    std::function<void(float)> dragged;
    std::function<void()> doubleClicked;
    bool hovered = false;
    bool dragging = false;
};

} // namespace tessera
