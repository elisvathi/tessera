#pragma once

#include "../../model/AutomationEdits.hpp"
#include "../EditorContext.hpp"
#include "../interaction/Axes.hpp"
#include "../interaction/Shortcuts.hpp"

#include <cmath>
#include <memory>
#include <optional>

namespace tessera {

/// Where a lane sits inside its host view, and how its time maps to x.
struct LaneFrame {
    LaneOwner owner;
    AutomationId lane;
    rack::math::Rect rect;          // in the host view's coordinates
    const TimeAxis* time = nullptr; // lane ticks (timeline or clip) to x
    Tick snap = 0;                  // grid step for snapping, 0 = off
    double playhead = NAN;          // lane ticks, NaN when not playing
    NVGcolor color;
};

/// Draws and edits one automation lane inside a host view (EDITOR_UX §6.6). The arranger uses it for track
/// lanes and the piano roll for clip lanes, so editing works the same everywhere.
class AutomationEditor {
  public:
    static constexpr float kPad = 4; // room above and below the curve
    static constexpr float kPointRadius = 3.5f;
    static constexpr float kPointHit = 6;

    explicit AutomationEditor(EditorContext& ctx) : ctx(ctx) {}

    void draw(NVGcontext* vg, const LaneFrame& frame);
    std::unique_ptr<DragGesture> startGesture(const LaneFrame& frame, rack::math::Vec pos, int button, Modifiers mods);
    /// Right-click: the point's menu or the lane's.
    void click(const LaneFrame& frame, rack::math::Vec pos, int button, Modifiers mods);
    void doubleClick(const LaneFrame& frame, rack::math::Vec pos, Modifiers mods);
    CursorShape hover(const LaneFrame& frame, rack::math::Vec pos, Modifiers mods);
    void leave() { hovered = Hover(); }

    /// Keyboard edits on the selected points. `pasteAt` is where Paste puts points (lane ticks).
    bool handleAction(Action action, Tick grid, double pasteAt);

    /// The lane menu: settings, functions, shape stamps, delete. Shared with the lane header.
    static void appendLaneMenu(rack::ui::Menu* menu, EditorContext& ctx, LaneOwner owner, AutomationId lane);

    // For gestures

    const AutomationEnvelope* lane(const LaneFrame& frame) const;
    const autoEdits::Points& points(const LaneFrame& frame) const;
    void setPreview(const LaneFrame& frame, autoEdits::Points points);
    void clearPreview() { preview.reset(); }
    /// One undo step replacing the lane's points; the selection becomes `selectAfter`.
    void commit(const LaneFrame& frame, const autoEdits::Points& after, const std::string& name,
                const autoEdits::Selection& selectAfter);
    autoEdits::Selection selection(const LaneFrame& frame) const;
    void select(const LaneFrame& frame, autoEdits::Selection points);

    float yOf(const LaneFrame& f, float value) const {
        return f.rect.pos.y + kPad + (1 - value) * (f.rect.size.y - 2 * kPad);
    }
    float valueAt(const LaneFrame& f, float y) const {
        return std::clamp(1 - (y - f.rect.pos.y - kPad) / std::max(1.f, f.rect.size.y - 2 * kPad), 0.f, 1.f);
    }
    float xOf(const LaneFrame& f, double t) const { return float(f.time->tickToX(t)); }
    double tickAt(const LaneFrame& f, float x) const { return f.time->xToTick(x); }
    /// The readout: volts, or the knob's value for a knob lane.
    std::string valueText(const LaneFrame& f, float value) const;
    /// A knob lane on a choice, switch or whole number: new points hold until the next one.
    bool stepped(const LaneFrame& f) const;

    EditorContext& ctx;

  private:
  public:
    /// The transform box around 2+ selected points (EDITOR_UX §6.6).
    enum class BoxHandle { None, Top, Bottom, Left, Right, TopLeft, TopRight };
    /// The box in view coordinates, or an empty rect when it isn't shown.
    rack::math::Rect selectionBox(const LaneFrame& frame) const;
    BoxHandle boxHandleAt(const LaneFrame& frame, rack::math::Vec pos) const;

  private:
    struct Hover {
        AutomationId lane;
        BoxHandle box = BoxHandle::None;
        int point = -1;   // a point under the pointer
        int handle = -1;  // the segment whose bend handle is under the pointer
        int segment = -1; // the segment the pointer is over
    };
    Hover hitTest(const LaneFrame& frame, rack::math::Vec pos) const;
    void openPointMenu(const LaneFrame& frame, size_t point);

    Hover hovered;
    std::optional<std::pair<AutomationId, autoEdits::Points>> preview;
};

} // namespace tessera
