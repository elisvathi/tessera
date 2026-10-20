#pragma once

#include "../../model/DeviceLookup.hpp"
#include "../editing/DeviceActions.hpp"
#include "../interaction/InteractiveView.hpp"
#include "../interaction/Shortcuts.hpp"

#include <array>
#include <map>
#include <optional>
#include <vector>

namespace tessera {

/// The selected track's devices, in a strip of fixed height above its notes (docs/DEVICES.md §1.1): the
/// modulator list on the left, then the device cards left to right in the order notes flow, each with its knobs in
/// one line. Closed, a thin bar names the chain.
class DeviceStrip : public InteractiveView {
  public:
    explicit DeviceStrip(EditorContext& ctx) : InteractiveView(ctx) {}

    static constexpr float kOpenHeight = 128;
    static constexpr float kClosedHeight = 24;
    static constexpr float kListRight = 222; // the right edge of the modulator list
    /// Below this the knobs don't fit: an open strip then shows as a bar that says so.
    static constexpr float kMinOpenHeight = 112;
    /// Room under the strip inside the pane, set by the pane: the mapping message floats there when it fits.
    float roomBelow = 0;
    /// Open, but the pane is too short for it.
    bool squeezed() { return deviceActions::stripOpen(ctx) && box.size.y < kMinOpenHeight; }
    /// Open, the closed bar (when the track has devices or modulators), or nothing.
    float wantedHeight();
    /// Delete and Duplicate act on the selected card. True when handled.
    bool handleAction(Action action);
    /// The bottom of the modulator list, where a modulator's popover opens (view coordinates).
    rack::math::Vec popoverAnchor() const { return rack::math::Vec(8, box.size.y - 4); }

    /// One clickable or draggable part, rebuilt every frame.
    struct Part {
        enum class Kind {
            ClosedBar,
            AddModulator,
            ModulatorRow,
            ModulatorMap,
            Card,
            CardHeader,
            Power,
            Menu,
            Dot,
            Knob,
            KnobValue,
            Insert,
            AddTile
        };
        Kind kind;
        rack::math::Rect rect;
        int device = -1; // index in the chain
        int param = -1;
        uint64_t id = 0; // device or modulator ID
    };

    // What running gestures show before they are committed.
    std::optional<std::pair<ParamRef, float>> knobPreview;   // a knob being dragged
    std::optional<std::pair<ParamRef, float>> amountPreview; // the mapped modulator's amount on a knob
    int dropIndex = -1;                                      // where a dragged card would land

    void step() override;

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;
    void drawForeground(const DrawArgs& args) override;

  private:
    const Track* track();
    void layoutParts();
    const Part* partAt(rack::math::Vec pos) const;
    ParamRef knobRef(const Part& p) const { return {ParamRef::Kind::Device, p.id, uint16_t(p.param)}; }
    /// While mapping: whether the mapped modulator may reach this knob.
    bool reachableNow(const ParamRef& ref);
    void drawClosed(NVGcontext* vg);
    void drawModulators(NVGcontext* vg);
    void drawCard(NVGcontext* vg, const Device& d, int index);
    void drawKnob(NVGcontext* vg, const Part& part);
    void sampleScopes();
    /// How far the modulator list can scroll: 0 while every row fits.
    float modScrollMax();

    std::vector<Part> parts;
    int hovered = -1;
    float scrollX = 0, contentWidth = 0;
    float modScroll = 0;
    float chainLeft = 0; // where the scrolling chain starts (the modulator list stays put)
    // Each modulator's last outputs (0..1 for drawing), for its row's little graph.
    std::map<uint64_t, std::array<float, 40>> scopes;
    int scopeHead = 0;
    TrackId shownTrack;
};

} // namespace tessera
