#pragma once

#include "../interaction/InteractiveView.hpp"

#include <bitset>

namespace tessera {

class PianoRollView;
struct DrumLane;

/// The lane headers beside a drum track's piano roll, in place of the keys (EDITOR_UX §5): one row per lane with
/// its colour, name, output, mute and solo, then "+ Add lane". Click a name to hear the lane (and show its hits in
/// the velocity lane), Ctrl/Cmd+click to select its hits, double-click to rename, right-click for the lane menu.
/// Rows of notes that sit on no lane show the note's name. Scrolls and zooms with the grid.
class DrumLaneHeaders : public InteractiveView {
  public:
    DrumLaneHeaders(EditorContext& ctx, PianoRollView& owner);

    void step() override;

  protected:
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void onLeave(const LeaveEvent& e) override;
    void drawView(const DrawArgs& args) override;

  private:
    enum class Part { None, Name, Mute, Solo, AddLane, Other };
    struct Hit {
        Part part = Part::None;
        int lane = -1; // index into the track's lanes
        int pitch = -1;
    };
    Hit hitTest(rack::math::Vec pos) const;
    /// The mute or solo button of the row at `top`; `fromRight` 0 is solo, 1 is mute.
    rack::math::Rect toggleRect(float top, float h, int fromRight) const;
    void startRename(int lane);
    void drawLane(NVGcontext* vg, const DrumLane& lane, int index, float top, float h, bool anySolo);

    PianoRollView& owner;
    Hit hover;
    std::bitset<128> sounding;
};

} // namespace tessera
