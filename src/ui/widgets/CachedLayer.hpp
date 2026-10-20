#pragma once

#include "../Theme.hpp"

#include <rack.hpp>

#include <cstdint>
#include <cstring>
#include <functional>

namespace tessera {

/// A drawing that is rendered once into a framebuffer and reused until its key changes (DESIGN.md §18:
/// static layers such as grids are cached and redrawn only on zoom, scroll or edit).
///
/// The owner sets `key` every frame to a hash of everything the drawing depends on.
class CachedLayer : public rack::widget::FramebufferWidget {
  public:
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
            setDirty();
        }
    }

  private:
    struct Painter : rack::widget::Widget {
        explicit Painter(std::function<void(NVGcontext*, rack::math::Vec)> f) : paint(std::move(f)) {}
        void draw(const DrawArgs& args) override { paint(args.vg, box.size); }
        std::function<void(NVGcontext*, rack::math::Vec)> paint;
    };
    Painter* painter;
    uint64_t key = ~uint64_t(0);
};

/// Folds values into a cache key.
inline uint64_t hashMix(uint64_t h, double v) {
    uint64_t bits;
    static_assert(sizeof(bits) == sizeof(v), "double must be 64-bit");
    memcpy(&bits, &v, sizeof(bits));
    return (h ^ bits) * 0x100000001B3ull + 0x9E3779B97F4A7C15ull;
}

} // namespace tessera
