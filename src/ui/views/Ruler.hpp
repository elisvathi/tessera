#pragma once

#include "../../model/Song.hpp"
#include "../Theme.hpp"
#include "../widgets/HoverTooltip.hpp"
#include "TimelineView.hpp"
#include <cmath>

#include <functional>
#include <optional>

#include <memory>
#include <string>
#include <vector>

namespace tessera {

/// What a ruler shows and edits. The arranger's ruler works on the song; the piano roll's on its clip.
class TimelineSource {
  public:
    virtual ~TimelineSource() = default;

    virtual TimeSig timeSig() = 0;
    /// The overview minimap spans [0, contentEnd).
    virtual double contentEnd() = 0;

    virtual Tick loopStart() = 0;
    virtual Tick loopEnd() = 0;
    virtual bool loopOn() = 0;
    /// True when something else sets the loop (a drum clip's lane cycle): it shows, but can't be dragged or set.
    virtual bool loopLocked() { return false; }
    /// Records one undo step.
    virtual void setLoop(Tick start, Tick end) = 0;
    virtual void toggleLoop() = 0;

    /// The playhead in this timeline's time, or NaN when there is none to show.
    virtual double playhead() = 0;
    virtual bool canSeek() { return false; }
    /// While playing, a seek waits for its beat (DECISIONS §96).
    virtual void seek(double tick, bool play) {}
    /// Where a seek waiting for its beat will land, in this timeline's time; NaN when none waits.
    virtual double pendingSeek() { return NAN; }

    virtual bool hasMarkers() { return false; }
    virtual std::vector<Marker> markers() { return {}; }
    /// Replaces all markers as one undo step.
    virtual void setMarkers(std::vector<Marker> markers, const std::string& undoName) {}
    virtual MarkerId newMarkerId() { return MarkerId(); }

    /// Where the timeline clip this view was opened from stops, in this timeline's time with the loop's repeats
    /// counted (the piano roll opened from the arranger); NaN when there is none.
    virtual double instanceEnd() { return NAN; }
    /// Where that timeline clip starts playing, in the same time.
    virtual double instanceStart() { return NAN; }
    /// Shows `end` while the marker is dragged; NaN stops showing it.
    virtual void previewInstanceEnd(double end) {}
    /// Makes the timeline clip end at `end`, as one undo step.
    virtual void setInstanceEnd(double end) {}

    /// A playhead's segment (docs/design/PLAYHEADS.md §12): the part of the clip it plays, where in it it starts, and
    /// its colour. The selected playhead's shows as a bar with handles and its name; the others as thin lines.
    struct PlayheadSegment {
        uint64_t id;       // PlayheadId value
        double start, end; // on the piano roll, which is a canvas: anywhere from its start, past the clip's end too
        int color;
        bool selected;
        std::string name;
    };
    virtual std::vector<PlayheadSegment> playheadSegments() { return {}; }
    /// Moves a playhead's segment to [start, end) (already snapped), as one undo step.
    virtual void setPlayheadSegment(uint64_t id, double start, double end) {}
};

/// The strip above a timeline: markers row, loop strip, time labels and the overview minimap (EDITOR_UX §3).
class Ruler : public InteractiveView {
  public:
    Ruler(EditorContext& ctx, TimeViewport& viewport, TimelineSource& source)
        : InteractiveView(ctx), viewport(viewport), source(source) {}

    /// Height for the given source: the markers row only appears when the source has markers.
    static float heightFor(TimelineSource& source, Surface surface);

    /// The strips' heights, thinner in the in-panel editor.
    struct Strips {
        float markers, loop, time, overview;
    };
    static Strips stripsFor(Surface surface);

    /// Grid step for snapping at the current zoom.
    Tick snapStep();

  protected:
    void drawView(const DrawArgs& args) override;
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    /// Dragging the loop past either edge scrolls the view, so a loop can grow beyond what is on screen.
    void autoScroll(double dx, double dy) override;
    void step() override;
    void onLeave(const LeaveEvent& e) override;

  public:
    /// The playhead segment being dragged, if any, so the notes below can follow it.
    const std::optional<TimelineSource::PlayheadSegment>& draggedSegment() const { return segmentPreview; }

  private:
    enum class Zone { Segments, Markers, Loop, Time, Overview };
    Zone zoneAt(float y);
    Strips strips() const { return stripsFor(ctx.surface); }
    /// The playhead segments' row sits on top, when there are segments to show.
    float segmentsBottom() { return source.playheadSegments().empty() ? 0 : theme::metrics::kSegmentRow; }
    float markersBottom() { return segmentsBottom() + (source.hasMarkers() ? strips().markers : 0); }
    /// With playheads, they decide what plays and the clip's loop doesn't: its bar isn't drawn or dragged.
    bool loopByPlayheads() { return !source.playheadSegments().empty(); }
    float loopBottom() { return markersBottom() + loopRow(source, strips()); }
    /// The loop strip's height: none with playheads, unless it holds a timeline clip's end.
    static float loopRow(TimelineSource& source, const Strips& s);
    float timeBottom() { return loopBottom() + strips().time; }

    /// The marker whose flag is under x, or -1.
    int markerAt(const std::vector<Marker>& markers, float x);
    /// The overview window: the visible range drawn on the whole-content strip.
    rack::math::Rect overviewWindow();
    double overviewTickAt(float x);

    void drawMarkers(NVGcontext* vg);
    /// Opens a field to type a new name for the marker `id`; Enter renames it as one undo step.
    void startMarkerRename(MarkerId id);
    /// The right-click menu of the marker `id`: Rename, Colour and Delete.
    void openMarkerMenu(MarkerId id);
    /// Changes the marker `id` with `change` as one undo step named `undoName`; does nothing if it is gone.
    void changeMarker(MarkerId id, const std::string& undoName,
                      const std::function<void(std::vector<Marker>&, size_t)>& change);
    void drawLoop(NVGcontext* vg);
    void drawPlayheadSegments(NVGcontext* vg);
    /// What a press at x on the loop strip grabs of the selected playhead's segment.
    enum class SegmentGrab { None, Start, End, Move };
    SegmentGrab segmentGrabAt(float x, TimelineSource::PlayheadSegment* segment);
    /// The handle at the end of the timeline clip this view was opened from (its right edge is the end).
    rack::math::Rect instanceTag();
    void drawTimeLabels(NVGcontext* vg);
    void drawOverview(NVGcontext* vg);

    TimeViewport& viewport;
    TimelineSource& source;

    // Live previews while a gesture runs; the source is only changed on release.
    std::optional<std::pair<Tick, Tick>> loopPreview;
    std::optional<TimelineSource::PlayheadSegment> segmentPreview; // a playhead segment being dragged
    std::optional<std::vector<Marker>> markersPreview;
    // Explains a locked loop while the pointer rests on it.
    HoverTooltip tip{""};
    bool tipOn = false;

    friend class LoopGesture;
    friend class PlayheadSegmentGesture;
    friend class InstanceEndGesture;
    friend class MarkerGesture;
    friend class OverviewGesture;
    friend class RulerTimeGesture;
};

} // namespace tessera
