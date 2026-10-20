#pragma once

#include "../interaction/InteractiveView.hpp"
#include "AutomationEditor.hpp"

namespace tessera {

class PianoRollView;

/// One of the shown clip's automation lanes under the piano roll, in clip time (it loops with the clip).
/// Editing works as in the arranger's lanes (EDITOR_UX §6.6).
class ClipLaneView : public InteractiveView {
  public:
    ClipLaneView(EditorContext& ctx, PianoRollView& owner) : InteractiveView(ctx), owner(owner), editor(ctx) {}

    /// The lane shown, or an invalid ID when the clip has none to show.
    AutomationId lane;

    bool handleAction(Action action);

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void onLeave(const LeaveEvent& e) override;
    void drawView(const DrawArgs& args) override;

  private:
    std::optional<LaneFrame> frame();

    PianoRollView& owner;
    AutomationEditor editor;
};

} // namespace tessera
