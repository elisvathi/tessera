#pragma once

#include "../interaction/InteractiveView.hpp"
#include "../widgets/HoverTooltip.hpp"

namespace tessera {

class ArrangerView;
struct LauncherStatus;

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
    static constexpr float kEdge = 5; // the resize handle at the right edge

    static constexpr float kAddSceneWidth = 28;
    /// A column at the right edge, fixed while the scenes scroll, with each track's "back to timeline" button and one
    /// for every track at its top (docs/DECISIONS.md §185, §187).
    static constexpr float kBackColumn = 24;

    /// Where the rows start: below the scene headers, level with the arranger's lanes.
    float rowsTop = 40;
    /// Where the scene headers start: below the clips' toolbar.
    float headerTop = 0;
    /// The width that shows every scene, the "+" for a new one and the resize edge.
    float contentWidth() { return float(sceneCount()) * kSlotWidth + kAddSceneWidth + backColumn() + kEdge; }
    /// The back column's width: kBackColumn when the panel has a module, else 0.
    float backColumn();

    /// What is under a point (panel coordinates).
    struct Hit {
        enum class Part { None, SceneLaunch, SceneName, AddScene, Launch, Body, Edge, Back, BackAll } part = Part::None;
        int row = -1;
        int scene = -1;
        TrackId track;
        bool ownMark = false; // over a slot's own-devices card (a Body or Launch hit still), for its tooltip
    };
    Hit hitTest(rack::math::Vec pos);
    /// Files dropped on a slot of an audio track go into it and the slots below; anywhere else in the launcher
    /// they make new audio tracks, each with the clip in its first slot (§6.5).
    void onPathDrop(const PathDropEvent& e) override;
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
    void step() override;

  private:
    void drawSceneHeaders(NVGcontext* vg);
    /// A "back to timeline" button in the back column (`row` -1: the one for every track, in the header): faint when
    /// nothing would change, plain while it would, filled while a launched clip took the track over from the timeline
    /// that plays, blinking while it waits for the launch line.
    void drawBackButton(NVGcontext* vg, rack::math::Rect r, bool faint, bool away, bool waiting, bool hot);
    rack::math::Rect backRect(int row);
    rack::math::Rect backAllRect();
    /// Whether some track doesn't play the timeline and isn't already on its way back.
    bool anyTrackAway();
    HoverTooltip backTip{""};
    rack::math::Rect sceneHeaderRect(int scene);

    ArrangerView& owner;
    float scrollX = 0;
    Hit hover;

    friend class SlotDragGesture;
    friend class PanelResizeGesture;
};

} // namespace tessera
