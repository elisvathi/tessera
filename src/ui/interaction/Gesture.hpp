#pragma once

#include <rack.hpp>

#include <string>

namespace tessera {

/// Modifier keys in Tessera's vocabulary (EDITOR_UX §2). `ctrl` is Cmd on macOS.
struct Modifiers {
    bool shift = false;
    bool alt = false;
    bool ctrl = false;

    static Modifiers fromGlfw(int mods) {
        Modifiers m;
        m.shift = mods & GLFW_MOD_SHIFT;
        m.alt = mods & GLFW_MOD_ALT;
        m.ctrl = mods & RACK_MOD_CTRL;
        return m;
    }
    /// The bits used by the shortcut table (kModShift | kModCtrl | kModAlt).
    int bits() const { return (shift ? 1 : 0) | (ctrl ? 2 : 0) | (alt ? 4 : 0); }
    bool operator==(const Modifiers& o) const { return shift == o.shift && alt == o.alt && ctrl == o.ctrl; }
    bool operator!=(const Modifiers& o) const { return !(*this == o); }
};

/// AddPoint is Tessera's own (a point ring with a plus): a click here adds a curve point.
enum class CursorShape { Arrow, IBeam, Crosshair, Hand, ResizeH, ResizeV, Move, NotAllowed, AddPoint };

/// One mouse drag, from press to release (EDITOR_UX §2 "Gesture engine").
///
/// A gesture captures what it needs when it is created, then update() recomputes the whole result from that
/// starting point on every call. Never update incrementally: that keeps snapping stable and makes toggling
/// Shift/Alt mid-drag and Esc-cancel trivial. commit() runs once on release and records at most one undo
/// step; cancel() restores the starting state.
class DragGesture {
  public:
    virtual ~DragGesture() = default;

    /// `pos` is the pointer in the owning view's local coordinates.
    virtual void update(rack::math::Vec pos, Modifiers mods) = 0;
    virtual void commit() {}
    virtual void cancel() {}

    /// Text for the readout pill near the cursor; empty hides it.
    virtual std::string readout() const { return {}; }
    virtual CursorShape cursor() const { return CursorShape::Arrow; }
    /// Draws live feedback (rubber band, guides) over the owning view.
    virtual void drawOverlay(NVGcontext* vg) {}

    /// Whether dragging near the view's edges should scroll it.
    virtual bool autoScrollX() const { return false; }
    virtual bool autoScrollY() const { return false; }
};

} // namespace tessera
