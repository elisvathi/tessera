#pragma once

#include "../../model/ArrangementEdits.hpp"
#include "../interaction/NoteBars.hpp"
#include "AutomationEditor.hpp"
#include "TimelineView.hpp"

#include <optional>

namespace tessera {

class ArrangerView;
class CachedLayer;

/// The arranger's lanes (EDITOR_UX §6.3): one row per track or group, the grid, the clips on the timeline and
/// the playhead. Clicks and drags on clips go through ClipGestures; menus and keys through arrangeActions.
///
/// Gestures preview their result here (setPreview) and commit one undo step at the end (commit).
class ArrangerLanes : public TimelineView {
  public:
    ArrangerLanes(EditorContext& ctx, ArrangerView& owner);

    // TimelineView
    double verticalScroll() override;
    void setVerticalScroll(double px) override;
    void zoomVertical(double y, double factor) override;
    void showVerticalRange(double y0, double y1) override;
    void zoomToFit() override;
    double playhead() override;
    void step() override;
    /// Keys for the selected automation points. Returns false when there is nothing to act on.
    bool automationAction(Action action);
    /// Keys for the time range: delete, delete time, insert silence, duplicate, loop.
    bool rangeAction(Action action);
    /// Clip editing keys (delete, split, nudge, ...). Returns false when there is nothing to act on.
    bool clipAction(Action action);

    /// What is under a point: a clip's body, its header strip or one of its edges.
    enum class Part { None, Body, Header, LeftEdge, RightEdge };
    struct Hit {
        int row = -1;
        ArrangementId id;
        Part part = Part::None;
    };
    Hit hitTest(rack::math::Vec pos);

    int rowAt(float y);
    /// The track a row shows; invalid for group rows and outside the rows.
    TrackId trackOfRow(int row);
    /// The tracks shown as rows, top to bottom (folded groups hide theirs). Clips move along this order.
    std::vector<TrackId> trackOrder();
    /// The position in trackOrder() of the track row nearest to `y`.
    int orderIndexAt(float y);
    float rowTop(int row);
    float rowHeight(int row);
    /// The row showing `track`, or -1.
    int rowOf(TrackId track);
    /// Where an instance is drawn in its track's row: overlapping clips stack in sub-rows (top, height).
    std::pair<float, float> instanceSpan(int row, const arrange::Placed& p);

    // Editing
    const arrange::Layout& committed();
    /// What is drawn: the preview while a gesture runs, else the committed layout.
    const arrange::Layout& shown() { return preview ? *preview : committed(); }
    void setPreview(arrange::Layout layout);
    void clearPreview();
    /// One undo step from `before` to `after`, adding `newClips` to the pool first; selects `selectAfter`.
    void commit(const std::string& name, const arrange::Layout& before, const arrange::Layout& after,
                const arrange::IdSet& selectAfter, std::vector<Clip> newClips = {});
    ArrangementId newInstanceId();
    /// After an edit: with stacking on, overlapping clips stay (they stack and play together); off, the changed
    /// clips win and trim or replace what they overlap.
    arrange::Layout settle(const arrange::Layout& layout, const arrange::IdSet& changed,
                           const arrange::IdSource& newId);
    ClipId newClipId();
    /// The grid step at this zoom (0 when snapping is off).
    Tick snapStep();
    /// Snaps `tick` to the grid, or to a nearby clip edge, marker, loop edge or the playhead (within 8 px).
    /// `exclude` are the clips being dragged. `free` (Alt) turns all snapping off.
    Tick snapTick(double tick, bool free, const arrange::IdSet& exclude);
    /// Where a block of `length` that started at `anchor` lands after moving by `delta`: one of its edges
    /// sticks to a nearby magnet, else it keeps its place relative to the grid (relative snap).
    Tick snapMove(Tick anchor, double delta, Tick length, bool free, const arrange::IdSet& exclude);

    arrange::IdSet& selection();
    /// Selects one instance (Shift adds, Ctrl/Cmd toggles) and makes its clip the piano roll's.
    void clickInstance(ArrangementId id, Modifiers mods);
    /// The tick under the mouse, or NaN when it isn't over the lanes.
    double mouseTick();

    EditorContext& context() { return ctx; }

    ArrangerView& owner;

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void onLeave(const LeaveEvent& e) override;
    void drawView(const DrawArgs& args) override;
    void drawForeground(const DrawArgs& args) override;

  private:
    void paintGrid(NVGcontext* vg, rack::math::Vec size);
    void paintClips(NVGcontext* vg, rack::math::Vec size);
    void drawInstance(NVGcontext* vg, const Song& song, const Track& track, const ArrangerClip& a, float top,
                      float height, bool dim);
    void drawRecording(NVGcontext* vg);
    /// Takes recording onto the timeline right now, with their notes so far.
    void drawTakes(NVGcontext* vg, double now);
    uint64_t clipsKey();
    /// The automation lane in a row, as the editor sees it; nothing for other rows.
    std::optional<LaneFrame> laneFrame(int row);
    void drawLanes(NVGcontext* vg);
    void drawRange(NVGcontext* vg);
    std::unique_ptr<DragGesture> rangeGesture(rack::math::Vec pos, Modifiers mods);
    void rippleDeleteSelected();

    AutomationEditor automation{ctx};
    CachedLayer* grid;
    CachedLayer* clips;
    std::optional<arrange::Layout> preview;
    uint64_t previewGeneration = 0;
    arrange::Layout committedCache;
    uint64_t committedRevision = ~uint64_t(0);
    std::vector<TimedNote> scratch;           // reused while drawing note previews
    std::vector<NoteBar> noteSpans, noteBars; // the same, as pixel spans and the bars drawn
    /// The sub-row layout of each track, for the layout shown; rebuilt when it changes.
    const arrange::Stacking& stackingOf(TrackId track);
    std::unordered_map<TrackId, arrange::Stacking> stacks;
    uint64_t stacksKey = ~uint64_t(0);
    Hit hover;
};

} // namespace tessera
