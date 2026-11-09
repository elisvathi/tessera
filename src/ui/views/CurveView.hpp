#pragma once

#include "../../model/Device.hpp"
#include "../../model/DeviceCommands.hpp"
#include "../../model/DeviceTypes.hpp"
#include "../interaction/Axes.hpp"
#include "../interaction/InteractiveView.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace tessera {

class PianoRollView;
class TesseraModule;

/// The piano roll's lower pane as a Curve modulator's drawing (docs/manual/13-devices.md, canvas board U). Up and
/// down are the setting's own values: a setting with steps (key steps, octaves, a choice) shows one row per value
/// and points snap to them; the dashed line is the knob as set, which the curve adds to. Across is the curve's own
/// time, zoomed and scrolled on its own. A header holds Length, ready shapes and Remove; under it, a ruler with the
/// segment that plays (drag its grips or the bar). A point is a Step (a square: holds until the next point) or a
/// Ramp (a circle: glides to it); right-click a point to switch. With no modulator yet (EditorContext::curveTarget),
/// the first point or shape adds one.
class CurveView : public InteractiveView {
  public:
    CurveView(EditorContext& ctx, PianoRollView& owner) : InteractiveView(ctx), owner(owner) {}

    /// Whether there is a curve, or a setting waiting for one, to show (the lower pane shows this view only then).
    bool hasCurve();
    /// The name the lower pane's button shows ("Melody Rate").
    std::string title();
    /// The Tessera the editor shows, for gestures that commit edits.
    TesseraModule* module() { return ctx.module(); }

    /// What a running drag shows before it is committed.
    std::optional<CurveDrawing> preview;
    /// The picked points, by index into the drawing's points, in order. Step | Ramp in the header acts on them, and
    /// dragging one moves them all.
    std::vector<size_t> picked;
    /// Deletes the picked points as one undo step (the Delete key). Does nothing with none picked.
    void deletePicked();
    /// Whether point `index` is picked.
    bool isPicked(size_t index) const { return std::binary_search(picked.begin(), picked.end(), index); }
    /// Sets the curve pane's height (px) from a drag of its top edge; 0 goes back to the default.
    void setHeight(double height);
    /// Across: ticks into the drawing, zoomed and scrolled on its own.
    TimeAxis time;

    /// The column on the left for the row labels, under the lower pane's button (the piano keys' width). The header,
    /// ruler and drawing start right of it.
    float left = 0;

    static constexpr float kEdge = 5;  // the top edge that drags to resize the pane
    static constexpr float kHead = 24; // the header row: Length, Step | Ramp, shapes, Moves, Remove, close
    static constexpr float kBar = 28;  // the ruler (positions, then the segment), under the header

    /// Keeps the curve's own zoom inside its limits, and lines a newly shown curve up under the notes.
    void step() override;

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    /// Scrolls across while a drag holds the pointer at the pane's edge.
    void autoScroll(double dx, double dy) override;
    void drawView(const DrawArgs& args) override;

  public:
    /// The Curve modulator shown, or nullptr (none yet, or it was deleted).
    const Modulator* modulator(ModulatorPlace* place = nullptr);
    /// The setting the curve moves: its modulator's drawn route, or the one waiting for a curve.
    ParamRef target();
    /// What that setting is, its value as set (normalized) and how far the curve turns it (1: all the way).
    const devices::ParamInfo* targetInfo(float* base = nullptr, float* amount = nullptr);
    /// The drawing shown: the dragged one, the modulator's, or an empty one as long as the clip.
    CurveDrawing shown();
    /// Commits a drawing: onto the modulator, or a new one moving the waiting setting.
    void commit(CurveDrawing drawing);
    /// Where tick `t` of the drawing is across the pane, and back.
    float xOf(double t);
    double tAt(float x);
    /// Where drawn value `value` (0..1) is up the pane.
    float yOf(float value) const;
    /// The drawn value (0..1) at `y`, kept where the setting can go and snapped to its steps unless `free`.
    float valueAt(float y, bool free = false);
    /// The drawn value `v` as the setting plays it: on a setting with steps, the nearest step.
    float heard(float v);
    /// The point under `pos`, or -1.
    int pointAt(rack::math::Vec pos);
    /// The grid step points and the segment snap to: the piano roll's snap, at most a beat.
    Tick snap();
    /// What the setting reads when the curve is at `v` (0..1), as its knob shows it.
    std::string format(float v);
    /// A length in bars, beats or sixteenths: "4 bars", "3 beats".
    std::string lengthLabel(Tick length);
    /// Whether the setting has steps (rows) rather than a smooth range.
    bool stepped();
    /// The drawn values the setting can reach from where its knob is (low, high).
    std::pair<float, float> reachable();
    /// The kind a new point at `t` gets: the kind of the point before it, else Step on a setting with steps.
    CurveType kindAt(const std::vector<AutoPoint>& points, Tick t);

  private:
    /// One clickable part of the header row, rebuilt when needed.
    struct HeaderPart {
        enum class Kind { Length, Step, Ramp, Shape, Moves, Remove, Close } kind;
        rack::math::Rect rect;
        int shape = -1;
    };
    std::vector<HeaderPart> headerParts();
    /// Replaces the drawing with ready shape `index` over the segment (making the curve if there is none yet).
    void applyShape(int index);
    /// Sets how long the drawing is, as one undo step; a segment playing all of it grows or shrinks with it.
    void setLength(Tick length);
    /// Makes every picked point a Step (Hold) or a Ramp (Linear), as one undo step.
    void setPickedKind(CurveType kind);
    /// The pane's right-click menu: shapes, fitting the view, removing the curve, and how to edit.
    void openMenu();
    /// A point's right-click menu: Step, Ramp or delete.
    void openPointMenu(int index);
    /// The Length field's list of lengths, 1 to 256 bars.
    void openLengthMenu();
    /// What the curve moves, from the header's Moves button: each setting (the one it is drawn in, others with
    /// their amounts) to remove, mapping it to more knobs, and its CV out. The curve needs no popover of its own.
    void openMovesMenu();
    /// The Moves button's text: what the curve is drawn in, and how many more it moves.
    std::string movesText();
    /// The header row: Length, Shapes and Remove curve.
    void drawHeader(NVGcontext* vg, NVGcolor color);
    /// The curve's ruler: positions in the drawing on top, the segment that plays under them.
    void drawRuler(NVGcontext* vg, NVGcolor color, const CurveDrawing& d);
    /// The rows (one per step of a stepped setting), their labels in the left column, and the knob's line.
    void drawRows(NVGcontext* vg);
    /// The segment's light, the grid, the line (bright in the segment), a ramp's stairs and the points.
    void drawCurve(NVGcontext* vg, NVGcolor color, const CurveDrawing& d);
    /// The values each of the setting's steps is drawn at, lowest first (only those it can reach).
    std::vector<float> rowValues();
    /// Lines the drawing up under the notes (the segment's start under the clip's start, the piano roll's zoom)
    /// and shows about seven rows either side of the knob as set.
    void fit();
    /// Shows the whole drawing across.
    void fitAll();
    /// Zooms up and down by `factor`, keeping the value under `y` in place, between three rows and all of them.
    void zoomValues(float y, float factor);
    /// Scrolls up and down by `dy` pixels, inside what the setting can reach.
    void scrollValues(float dy);
    /// Where the curve's track started its clip, in ticks ago (NaN when nothing plays): its playhead counts from
    /// there, as the engine's does.
    double sinceClipStart();
    /// Where the shown clip's notes start on the piano roll (its loop, or a drum clip's lane cycle).
    double spanStart();

    PianoRollView& owner;
    float low = 0, high = 1; // the drawn values shown, bottom to top
    uint64_t fittedModulator = 0;
    ParamRef fittedTarget;
    bool fitted = false;
};

} // namespace tessera
