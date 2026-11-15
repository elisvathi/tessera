#pragma once

#include "../Theme.hpp"

#include <rack.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>

namespace tessera {

/// A drawing that is rendered once into a framebuffer and reused until its key changes (DESIGN.md §18:
/// static layers such as grids are cached and redrawn only on zoom, scroll or edit).
///
/// The owner sets `key` every frame to a hash of everything the drawing depends on.
///
/// While its size keeps changing (a pane being dragged), or when the framebuffer would be huge (zoomed far in on
/// a high-density screen), it draws straight to the screen instead. Otherwise each frame of a drag allocates a new
/// framebuffer, tens of megabytes on a Retina screen, and some graphics drivers are slow to give that memory back.
class CachedLayer : public rack::widget::FramebufferWidget {
  public:
    static constexpr double kSettleSeconds = 0.25; // a size held this long is cached again
    static constexpr float kMaxCachedPx = 8192;    // the longest side, in screen pixels, that is still cached

    explicit CachedLayer(std::function<void(NVGcontext*, rack::math::Vec size)> paint) {
        painter = new Painter(std::move(paint));
        addChild(painter);
    }

    void setKey(uint64_t newKey) {
        newKey ^= theme::generation() * 0x9E3779B97F4A7C15ull; // a new scheme repaints too
        if (newKey != key) {
            key = newKey;
            setDirty();
        }
    }
    void setSize(rack::math::Vec size) {
        if (!box.size.equals(size)) {
            box.size = size;
            painter->box.size = size;
            resizedAt = APP->window->getFrameTime();
            setDirty();
        }
    }

    /// Picks between the cached picture and drawing directly, then draws.
    void draw(const DrawArgs& args) override {
        float xform[6];
        nvgCurrentTransform(args.vg, xform); // includes Rack's zoom and the screen's pixel ratio
        float longest = std::max(box.size.x * std::fabs(xform[0]), box.size.y * std::fabs(xform[3]));
        bool resizing = APP->window->getFrameTime() - resizedAt < kSettleSeconds;
        bool direct = resizing || longest > kMaxCachedPx;
        if (bypassed && !direct)
            setDirty(); // the old picture was made before the size settled
        bypassed = direct;
        FramebufferWidget::draw(args);
    }

  private:
    struct Painter : rack::widget::Widget {
        explicit Painter(std::function<void(NVGcontext*, rack::math::Vec)> f) : paint(std::move(f)) {}
        void draw(const DrawArgs& args) override { paint(args.vg, box.size); }
        std::function<void(NVGcontext*, rack::math::Vec)> paint;
    };
    Painter* painter;
    uint64_t key = ~uint64_t(0);
    double resizedAt = -INFINITY;
};

/// Folds values into a cache key.
inline uint64_t hashMix(uint64_t h, double v) {
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(v), "double must be 64-bit");
    memcpy(&bits, &v, sizeof(bits));
    return (h ^ bits) * 0x100000001B3ull + 0x9E3779B97F4A7C15ull;
}

} // namespace tessera
