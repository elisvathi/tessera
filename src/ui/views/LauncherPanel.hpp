#pragma once

#include "../interaction/InteractiveView.hpp"

namespace tessera {

class ArrangerView;

/// The clip launcher inside the arranger, between the track headers and the timeline:
/// tracks are rows, lined up with the arranger's, and scenes are columns, with each scene's launch button on top.
///
/// Click a slot's triangle to launch it (an empty slot's square stops the track), click a clip to edit it,
/// double-click an empty slot to create one, right-click for the clip, slot or scene menu. Drag a clip onto the
/// timeline to place it there, or onto another slot to move it (Ctrl/Cmd or Alt copies). Clips dragged from the
/// timeline land in empty slots here. Wheel scrolls the tracks, Shift+wheel the scenes; drag the right edge to
/// resize the panel.
class LauncherPanel : public InteractiveView {
  public:
    LauncherPanel(EditorContext& ctx, ArrangerView& owner) : InteractiveView(ctx), owner(owner) {}

    static constexpr float kSlotWidth = 76;
    static constexpr float kMaxSlotHeight = 30;
    static constexpr float kEdge = 5; // the resize handle at the right edge

    static constexpr float kAddSceneWidth = 28;

    /// Where the rows start: below the scene headers, level with the arranger's lanes.
    float rowsTop = 40;
    /// Where the scene headers start: below the clips' toolbar.
    float headerTop = 0;
    /// The width that shows every scene, the "+" for a new one and the resize edge.
    float contentWidth() { return float(sceneCount()) * kSlotWidth + kAddSceneWidth + kEdge; }

    /// What is under a point (panel coordinates).
    struct Hit {
        enum class Part { None, SceneLaunch, SceneName, AddScene, Launch, Body, Edge } part = Part::None;
        int row = -1;
        int scene = -1;
        TrackId track;
    };
    Hit hitTest(rack::math::Vec pos);
    /// The slot cell of a row and scene, in panel coordinates.
    rack::math::Rect cellRect(int row, int scene);
    float columnX(int scene) const { return float(scene) * kSlotWidth - scrollX; }
    int sceneCount();

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void onLeave(const LeaveEvent& e) override;
    void drawView(const DrawArgs& args) override;

  private:
    void drawSceneHeaders(NVGcontext* vg);
    rack::math::Rect sceneHeaderRect(int scene);

    ArrangerView& owner;
    float scrollX = 0;
    Hit hover;

    friend class SlotDragGesture;
    friend class PanelResizeGesture;
};

} // namespace tessera
