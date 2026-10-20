#pragma once

#include "../../model/Song.hpp"
#include "../Theme.hpp"
#include "TimelineView.hpp"
#include <cmath>

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
    /// Records one undo step.
    virtual void setLoop(Tick start, Tick end) = 0;
    virtual void toggleLoop() = 0;

    /// The playhead in this timeline's time, or NaN when there is none to show.
    virtual double playhead() = 0;
    virtual bool canSeek() { return false; }
    virtual void seek(double tick, bool play) {}

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
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    /// Dragging the loop past either edge scrolls the view, so a loop can grow beyond what is on screen.
    void autoScroll(double dx, double dy) override;

  private:
    enum class Zone { Markers, Loop, Time, Overview };
    Zone zoneAt(float y);
    Strips strips() const { return stripsFor(ctx.surface); }
    float markersBottom() { return source.hasMarkers() ? strips().markers : 0; }
    float loopBottom() { return markersBottom() + strips().loop; }
    float timeBottom() { return loopBottom() + strips().time; }

    /// The marker whose flag is under x, or -1.
    int markerAt(const std::vector<Marker>& markers, float x);
    /// The overview window: the visible range drawn on the whole-content strip.
    rack::math::Rect overviewWindow();
    double overviewTickAt(float x);

    void drawMarkers(NVGcontext* vg);
    void drawLoop(NVGcontext* vg);
    /// The handle at the end of the timeline clip this view was opened from (its right edge is the end).
    rack::math::Rect instanceTag();
    void drawTimeLabels(NVGcontext* vg);
    void drawOverview(NVGcontext* vg);

    TimeViewport& viewport;
    TimelineSource& source;

    // Live previews while a gesture runs; the source is only changed on release.
    std::optional<std::pair<Tick, Tick>> loopPreview;
    std::optional<std::vector<Marker>> markersPreview;

    friend class LoopGesture;
    friend class InstanceEndGesture;
    friend class MarkerGesture;
    friend class OverviewGesture;
    friend class RulerTimeGesture;
};

} // namespace tessera
