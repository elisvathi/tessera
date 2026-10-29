#pragma once

#include "../../model/Device.hpp"
#include "../../model/DeviceTypes.hpp"
#include "../interaction/InteractiveView.hpp"

#include <optional>
#include <vector>

namespace tessera {

class PianoRollView;
class TesseraModule;

/// The piano roll's lower pane as a Curve modulator's shape: a drawn line that repeats, under the notes it moves.
/// Up and down are in the units of the setting it moves (its first route): a setting with steps, such as Octave or
/// a playhead's Steps, shows one row per value and points snap to them; the middle line is the setting as set.
/// Synced, a bar at the top with ▶ ◀ ends sets how long a cycle lasts; points are added with a click, dragged, and
/// deleted with a double-click. With no modulator yet (EditorContext::curveTarget), the first point adds one.
class CurveView : public InteractiveView {
  public:
    CurveView(EditorContext& ctx, PianoRollView& owner) : InteractiveView(ctx), owner(owner) {}

    /// Whether there is a curve, or a setting waiting for one, to show (the lower pane shows this view only then).
    bool hasCurve();
    /// The name the lower pane's button shows ("Melody Rate").
    std::string title();
    /// The Tessera the editor shows, for gestures that commit edits.
    TesseraModule* module() { return ctx.module(); }

    // What a running drag shows before it is committed: the points and the length.
    std::optional<std::pair<std::vector<AutoPoint>, Tick>> preview;

    static constexpr float kBar = 18;      // the length bar at the top
    static constexpr float kPassWidth = 72; // one pass across, per pass
    static constexpr float kPassLeft = 46;  // where pass 1 starts, per pass (room for the row labels)

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  public:
    /// The Curve modulator shown, or nullptr (none yet, or it was deleted).
    const Modulator* modulator();
    /// The setting the curve moves: its modulator's first route to a setting, or the one waiting for a curve.
    ParamRef target();
    /// What that setting is, its value as set (normalized) and how far the curve turns it (-1..1).
    const devices::ParamInfo* targetInfo(float* base = nullptr, float* amount = nullptr);
    /// Whether the cycle follows the song (Sync on, or Per pass): only then does the bar set its length.
    bool synced();
    /// Whether the curve steps once a pass (Per pass): across are passes, not the piano roll's time.
    bool perPass();
    /// The points and length shown: the dragged ones, the modulator's, or none over a bar.
    std::pair<std::vector<AutoPoint>, Tick> shown();
    /// Commits a new shape: onto the modulator, or a new one moving the waiting setting.
    void commit(Tick length, std::vector<AutoPoint> points);
    double spanStart();
    float xOf(double t);
    double tAt(float x);
    float yOf(float value) const;
    /// The drawn value (0..1) at `y`, snapped to the setting's steps unless `free`.
    float valueAt(float y, bool free = false);
    /// The point under `pos` (in the cycle shown at x), or -1.
    int pointAt(rack::math::Vec pos);
    Tick snap();
    /// What the setting reads when the curve is at `v` (0..1), as its knob shows it.
    std::string format(float v);

  private:
    void openMenu();
    PianoRollView& owner;
};

} // namespace tessera
