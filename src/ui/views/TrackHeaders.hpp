#pragma once

#include "../../model/Recording.hpp"
#include "../interaction/InteractiveView.hpp"
#include "../interaction/TrackRows.hpp"
#include "../widgets/HoverTooltip.hpp"

namespace tessera {

class ArrangerView;

/// The mark beside a lane that says whether the next take records into it: a red dot, an amber "!" when its source
/// is missing, a crossed circle when it is kept, nothing when it won't record.
void drawLaneRecordMark(NVGcontext* vg, rack::math::Vec at, LaneRecordState state);

/// The column of track headers beside the arranger (EDITOR_UX §6.2).
///
/// Track rows: a colour tile with the track number, the name, then arm, lanes, mute and solo at a fixed size.
/// Arm and the lanes arrow are drawn small and dim until they are on; a missing output module shows as an
/// amber jack only when it is missing. Group rows: fold arrow, name, track
/// count. Click selects (Shift: range, Ctrl/Cmd: toggle); drag reorders, onto a group header to join it;
/// the bottom edge resizes (Alt: all tracks); double-click renames; right-click opens the menu.
class TrackHeaders : public InteractiveView {
  public:
    TrackHeaders(EditorContext& ctx, ArrangerView& owner) : InteractiveView(ctx), owner(owner), tip("") {}

    /// Where a drag would drop: before which track (song index), into which group, and where to draw the line.
    struct DropTarget {
        int before = 0;
        std::optional<GroupId> group;
        float lineY = 0;
        bool indented = false;
    };
    DropTarget dropTargetAt(float y);
    /// Opens the inline name field over a row.
    void startRename(int row);

  protected:
    void step() override;
    void drawView(const DrawArgs& args) override;
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    void onLeave(const LeaveEvent& e) override;

  private:
    enum class Part {
        None,
        Body,
        Name,
        Fold,
        Automation,
        Arm,
        Output,
        Mute,
        Solo,
        Edge,
        AddRow,
        Empty,
        LaneMute,
        LaneState, // the mark that says whether the lane records: not a button
        AddLane
    };
    struct Hit {
        int row = -1;
        Part part = Part::None;
    };
    Hit hitTest(rack::math::Vec pos);
    float lineY(float top, float height);
    float indentOf(const TrackRow& row) const;
    /// Narrow columns (the in-panel editor) use smaller buttons so the name keeps its room.
    bool compact() const;
    rack::math::Rect automationRect(float top, float height);
    /// The amber "no output module" mark, left of arm.
    rack::math::Rect outputRect(float top, float height);
    /// Whether a track shows that mark: it has notes but no output module plays them. An empty track has
    /// nothing to warn about, so a fresh song isn't covered in warnings.
    bool missingOutput(const Track& track);
    rack::math::Rect armRect(float top, float height);
    rack::math::Rect muteRect(float top, float height);
    rack::math::Rect soloRect(float top, float height);
    rack::math::Rect nameRect(int row);
    void drawTrackRow(NVGcontext* vg, int row, float top, float height);
    void drawGroupRow(NVGcontext* vg, int row, float top, float height);
    /// An automation lane's header: name, output, value now, mute. And the "+ Add lane" row under them.
    void drawLaneRow(NVGcontext* vg, int row, float top, float height);
    void drawAddLaneRow(NVGcontext* vg, int row, float top, float height);
    void drawAddRow(NVGcontext* vg);
    /// The "+ Add track" row right after the last track.
    rack::math::Rect addRowRect();
    void openAddMenu();

    /// The tooltip for the part under the mouse, or "" for none.
    std::string tipFor(const Hit& hit);

    ArrangerView& owner;
    Hit hover;
    Hit tipHit;
    HoverTooltip tip;
};

} // namespace tessera
