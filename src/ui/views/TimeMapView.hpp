#pragma once

#include "../../model/Device.hpp"
#include "../interaction/InteractiveView.hpp"

#include <optional>
#include <vector>

namespace tessera {

class PianoRollView;
class TesseraModule;

/// The piano roll's lower pane as a time map (docs/design/PLAYHEADS.md §3.3): one row per playhead of the track's
/// Playheads device. Across, time since the clip started (lined up with the clip's ticks above); up, where in its
/// region the playhead reads. A path from the settings is drawn from them; a drawn path shows its points (placed in
/// the playhead's own time, so a faster rate squeezes them), which are added, dragged and deleted here.
class TimeMapView : public InteractiveView {
  public:
    TimeMapView(EditorContext& ctx, PianoRollView& owner) : InteractiveView(ctx), owner(owner) {}

    /// The track's Playheads device when it is on, else nullptr.
    const Device* device();

    /// The Tessera the editor shows, for gestures that commit edits.
    TesseraModule* module() { return ctx.module(); }

    // What a running drag shows before it is committed: one playhead's path.
    std::optional<std::pair<PlayheadId, std::vector<AutoPoint>>> preview;

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  private:
    struct Row {
        const Playhead* playhead;
        rack::math::Rect rect;
    };
    std::vector<Row> rows();
    const Row* rowAt(rack::math::Vec pos);
    /// Where the clip's span starts (own time 0 lines up with it) and how long the playhead's region is there.
    double spanStart();
    /// The points shown for a playhead: the dragged ones, else its own.
    const std::vector<AutoPoint>& pointsOf(const Playhead& p);
    double cycleOf(const Playhead& p);
    float xOf(double ownTicks);
    double ownAt(float x);
    float yOf(const Row& row, float value) const;
    float valueAt(const Row& row, float y) const;
    /// The point of `row` under `pos`, or -1.
    int pointAt(const Row& row, rack::math::Vec pos);
    void openMenu(const Row& row);
    void drawRow(NVGcontext* vg, const Row& row);

    PianoRollView& owner;
};

} // namespace tessera
