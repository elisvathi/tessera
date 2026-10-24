#pragma once

#include "../../model/ExpressionEdits.hpp"
#include "../editing/NoteGestures.hpp"
#include "../editing/NoteHitTest.hpp"
#include "TimelineView.hpp"

namespace tessera {

class PianoRollView;
class CachedLayer;

/// The piano roll's note area: draws rows, grid and notes, and turns presses into editing gestures
/// according to the active tool and modifiers (EDITOR_UX §4.2-4.3).
class NoteGrid : public TimelineView {
  public:
    NoteGrid(EditorContext& ctx, PianoRollView& owner);

    double verticalScroll() override;
    void setVerticalScroll(double px) override;
    void zoomVertical(double y, double factor) override;
    void showVerticalRange(double y0, double y1) override;
    double playhead() override;
    /// Fits the selection when there is one, else all notes (or the loop and a middle octave range).
    void zoomToFit() override;

    void step() override;

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void onLeave(const LeaveEvent& e) override;
    void drawView(const DrawArgs& args) override;
    void drawForeground(const DrawArgs& args) override;

  private:
    NoteHit hitAt(rack::math::Vec pos);
    /// The pitch curve point or note the Bend tool would grab at pos.
    BendHit bendAt(rack::math::Vec pos);
    /// A bend point or arch handle of a selected note at pos (the Select tool can drag those).
    BendHit selectedBendAt(rack::math::Vec pos);
    /// Starts the drag for a press on a pitch curve: arch a glide, move a point (or all picked points), or add one.
    std::unique_ptr<DragGesture> pitchCurveGesture(BendHit bend, rack::math::Vec pos, Modifiers mods);
    /// Where the grid draws a pitch curve value of a note.
    CurveY pitchCurveY();
    /// Right-click on a curve point: deletes it, and the whole curve once only resting points are left.
    void removeCurvePoint(const BendHit& hit, expr::Curve curve);

    /// The strip above notes that overlap in time, where their timbre or pressure curve is drawn large and
    /// edited (one strip per group of selected notes). The lead note is the highest; a press in the strip
    /// shapes its curve, and the rest of the selection follows.
    struct CurveStrip {
        rack::math::Rect r;
        std::vector<NoteId> notes;
        NoteId lead;
        Tick start, end;
    };
    /// The strips for the selected notes, when the piano roll shows one (PianoRollView::stripCurve).
    std::vector<CurveStrip> curveStrips(const edits::NoteList& notes);
    /// The strip under pos, if any (from the committed notes).
    const CurveStrip* stripAt(rack::math::Vec pos);
    float stripValueAt(const CurveStrip& s, double y) const;
    double stripY(const CurveStrip& s, float value) const;
    /// What a press in a strip grabs: a point, an arch handle, else a new point on the lead note's curve.
    BendHit stripHit(const CurveStrip& s, rack::math::Vec pos);
    std::unique_ptr<DragGesture> stripGesture(const CurveStrip& s, rack::math::Vec pos, Modifiers mods);
    /// The transform box around the selection, in view coordinates; empty when it isn't shown.
    rack::math::Rect transformFrame();
    noteGestures::BoxHandle boxHandleAt(rack::math::Vec pos);
    /// Selects the pressed note the way a press should: alone, or added with Shift / Ctrl.
    void selectForPress(NoteId id, Modifiers mods);
    void openNoteMenu(NoteId id);
    void openGridMenu();
    void openSliceMenu(NoteId id);

    void paintGrid(NVGcontext* vg, rack::math::Vec size);
    /// A drum track's grid: every step of each lane as a cell, beats shaded in turn, bar lines in the gaps.
    void paintDrumSteps(NVGcontext* vg, rack::math::Vec size);
    void drawNotes(NVGcontext* vg);
    /// Ghost notes: one other note, drawn as an outline, with where it lives.
    struct Ghost {
        Note note;
        uint32_t color;
        TrackId track;
        ClipId clip;
        bool layer = false; // a clip opened together with this one: clicking its notes edits it
    };
    /// Layered editing: the strip of clips opened together, at the bottom of the grid.
    void drawLayerStrip(NVGcontext* vg);
    /// Makes `clip` (on `track`) the edited clip, selecting `note` if given.
    void switchTo(TrackId track, ClipId clip, NoteId note = NoteId());
    /// The ghost notes in view (EDITOR_UX §4.1), refilled each frame.
    void collectGhosts();
    void drawGhosts(NVGcontext* vg);
    /// Opened from a timeline clip: the loop's repeats, faded, up to where that clip stops, and a line there.
    void drawTimelineRepeats(NVGcontext* vg);
    /// Drum lanes with a loop of their own (polymeter): where it ends on the lane's row, and its repeats, faded,
    /// until the clip's loop ends.
    void drawLaneLoops(NVGcontext* vg);
    /// Rows of muted lanes (or lanes left out by a solo) are dimmed, as their headers are.
    void dimSilentLanes(NVGcontext* vg);
    const Ghost* ghostAt(rack::math::Vec pos);
    /// The step input cursor, and a banner while step input or keyboard play is on.
    void drawLivePlay(NVGcontext* vg);
    /// Operator marks on a note: repeat ticks, chance and occurrence text, recurrence dots (EDITOR_UX §4.5).
    void drawBadges(NVGcontext* vg, const Note& note, rack::math::Rect r, float labelRight);
    /// The note's expression curve, small, inside its body (EDITOR_UX §4.6).
    void drawCurveInside(NVGcontext* vg, const Note& note, rack::math::Rect r, expr::Curve curve);
    /// Every note's pitch curve, drawn at the pitch it plays, with its points while the Bend tool is on or the
    /// note is selected.
    void drawBends(NVGcontext* vg);
    /// The timbre or pressure strips above the selection.
    void drawStrips(NVGcontext* vg);
    /// Notes being recorded into this clip right now.
    void drawTakeNotes(NVGcontext* vg);
    void drawTransformFrame(NVGcontext* vg);
    void drawHoverGuide(NVGcontext* vg);
    void drawEmptyState(NVGcontext* vg);

    PianoRollView& owner;
    CachedLayer* layer;
    std::vector<Ghost> ghosts;
    std::vector<CurveStrip> strips; // from the last stripAt, which points into it
    std::vector<std::pair<rack::math::Rect, ClipId>> layerChips;
    rack::math::Rect layerClose;
    NoteId hovered;
    rack::math::Vec hoverPos;
    bool hovering = false;
};

} // namespace tessera
