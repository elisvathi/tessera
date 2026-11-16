#pragma once

#include <rack.hpp>

#include <algorithm>
#include <cmath>

namespace tessera {

/// A framebuffer that never makes a new texture on every frame. Every cached picture in Tessera goes through it:
/// the editor's layers (CachedLayer) and the modules' panel artwork. See docs/DECISIONS.md §168.
///
/// Rack makes a new texture whenever the size in screen pixels changes: when the widget is resized (a pane being
/// dragged) and when Rack's zoom changes. On a Retina screen one such texture can be 100 MB or more, and macOS is slow
/// to give the old ones back; on 15 Nov 2026 a pane drag asking about 100 MB per mouse step froze a MacBook. So while
/// the size or the zoom keeps changing, and whenever the texture would be huge, this draws straight to the screen.
class GuardedFramebuffer : public rack::widget::FramebufferWidget {
  public:
    static constexpr double kSettleSeconds = 0.25; // a size and zoom held this long are cached again
    static constexpr float kMaxCachedPx = 8192;    // the longest texture side, in real pixels, that is still cached

    /// Picks between the cached picture and drawing directly, then draws.
    void draw(const DrawArgs& args) override {
        float xform[6];
        nvgCurrentTransform(args.vg, xform); // Rack's zoom and the screen's pixel ratio
        rack::math::Vec scale(std::fabs(xform[0]), std::fabs(xform[3]));
        double now = APP->window->getFrameTime();
        if (drawnOnce && (!box.size.equals(drawnSize) || !scale.equals(drawnScale)))
            changedAt = now;
        drawnOnce = true;
        drawnSize = box.size;
        drawnScale = scale;
        // Rack's texture is the transformed size times the pixel ratio again (measured: 4x the point size per side
        // at pixel ratio 2), so the limit is checked against that.
        float longest = std::max(box.size.x * scale.x, box.size.y * scale.y) * APP->window->pixelRatio;
        bool direct = now - changedAt < kSettleSeconds || longest > kMaxCachedPx;
        if (bypassed && !direct)
            setDirty(); // the old picture was made before the size settled
        bypassed = direct;
        FramebufferWidget::draw(args);
    }

  private:
    bool drawnOnce = false;
    rack::math::Vec drawnSize, drawnScale;
    double changedAt = -INFINITY;
};

} // namespace tessera
