#pragma once

#include "Gesture.hpp"

#include <functional>
#include <memory>

namespace tessera {

/// Runs `inner` once the pointer really moves; a press and release in place runs `onClick` instead.
/// Lets a press on a note or track start a move right away while a plain click still just selects.
class ClickOrDrag : public DragGesture {
  public:
    static constexpr double kSlopPx = 3;

    ClickOrDrag(std::unique_ptr<DragGesture> inner, rack::math::Vec press, std::function<void()> onClick)
        : inner(std::move(inner)), press(press), onClick(std::move(onClick)) {}

    void update(rack::math::Vec pos, Modifiers mods) override {
        if (!moved && pos.minus(press).norm() > kSlopPx)
            moved = true;
        if (moved)
            inner->update(pos, mods);
    }
    void commit() override {
        if (moved) {
            inner->commit();
        } else {
            inner->cancel();
            if (onClick)
                onClick();
        }
    }
    void cancel() override { inner->cancel(); }
    std::string readout() const override { return moved ? inner->readout() : ""; }
    CursorShape cursor() const override { return inner->cursor(); }
    void drawOverlay(NVGcontext* vg) override {
        if (moved)
            inner->drawOverlay(vg);
    }
    bool autoScrollX() const override { return moved && inner->autoScrollX(); }
    bool autoScrollY() const override { return moved && inner->autoScrollY(); }

  private:
    std::unique_ptr<DragGesture> inner;
    rack::math::Vec press;
    std::function<void()> onClick;
    bool moved = false;
};

} // namespace tessera
