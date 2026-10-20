#pragma once

#include "../../model/DeviceLookup.hpp"
#include "../interaction/InteractiveView.hpp"

#include <optional>
#include <vector>

namespace tessera {

/// A modulator's settings, opened from its row in the device strip (docs/DEVICES.md §1.1). It floats over the notes
/// so the strip keeps its height: the modulator's settings (and the Steps editor for Steps), then "Moves": what it
/// turns and how far, with Map to knobs and CV out.
class ModulatorPopover : public InteractiveView {
  public:
    explicit ModulatorPopover(EditorContext& ctx) : InteractiveView(ctx) {}

    static constexpr float kWidth = 270;
    /// The height its content needs (0 when nothing is open).
    float wantedHeight();

    struct Part {
        enum class Kind { Close, Setting, StepBars, RouteAmount, RouteRemove, Map, CvOut };
        Kind kind;
        rack::math::Rect rect;
        int index = 0; // the setting or the route
    };

    // What running gestures show before they are committed.
    std::optional<std::pair<int, float>> settingPreview; // setting index, value
    std::optional<std::pair<int, float>> amountPreview;  // route index, amount
    std::optional<std::vector<float>> stepsPreview;

    void step() override;

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  private:
    const Modulator* modulator(ModulatorPlace* place = nullptr);
    void layoutParts();
    const Part* partAt(rack::math::Vec pos) const;
    ParamRef settingRef(int index) const { return {ParamRef::Kind::Modulator, ctx.openModulator, uint16_t(index)}; }

    std::vector<Part> parts;
    int hovered = -1;
    float movesTitleY = 0, height = 0;
    float scroll = 0; // when the pane is too short for all of it
};

} // namespace tessera
