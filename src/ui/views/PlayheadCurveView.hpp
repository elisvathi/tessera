#pragma once

#include "../../model/Device.hpp"
#include "../interaction/InteractiveView.hpp"

#include <optional>
#include <vector>

namespace tessera {

class PianoRollView;
class TesseraModule;

/// The piano roll's lower pane as the curve of one playhead setting (docs/design/PLAYHEADS.md §12): the setting's
/// value over time since the clip started, repeating every curve length. A bar at the top with ▶ ◀ ends sets that
/// length (so how fast it moves); points are added with a click, dragged, and deleted with a double-click.
class PlayheadCurveView : public InteractiveView {
  public:
    PlayheadCurveView(EditorContext& ctx, PianoRollView& owner) : InteractiveView(ctx), owner(owner) {}

    /// Whether there is a playhead setting to show (the lower pane shows this view only then).
    bool hasCurve();
    /// The name the lower pane's button shows ("Rate curve").
    std::string title();
    /// The Tessera the editor shows, for gestures that commit edits.
    TesseraModule* module() { return ctx.module(); }

    // What a running drag shows before it is committed: the points and the length.
    std::optional<std::pair<std::vector<AutoPoint>, Tick>> preview;

    static constexpr float kBar = 18; // the length bar at the top

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  public:
    /// The shown playhead and the curve of the shown setting (nullptr when it has none yet).
    const Playhead* playhead();
    const PlayheadCurve* curve();
    /// The points and length shown: the dragged ones, the curve's, or a flat line at the setting's value over the
    /// clip's length when it has no curve yet.
    std::pair<std::vector<AutoPoint>, Tick> shown();
    double spanStart();
    float xOf(double t);
    double tAt(float x);
    float yOf(float value) const;
    float valueAt(float y) const;
    /// The point under `pos` (in the first cycle shown at x), or -1.
    int pointAt(rack::math::Vec pos);
    Tick snap();

  private:
    void openMenu();
    PianoRollView& owner;
};

} // namespace tessera
