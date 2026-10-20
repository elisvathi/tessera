#pragma once

#include "../../model/ExpressionEdits.hpp"
#include "../../model/NoteEdits.hpp"
#include "../interaction/InteractiveView.hpp"

namespace tessera {

class PianoRollView;

/// A per-note expression lane under the piano roll (EDITOR_UX §4.6): each note's pitch bend, timbre or
/// pressure curve, drawn across the note's time span.
///
/// Click inside a note's span to add a point, drag points to move them, drag a segment's middle handle to bend
/// it, double-click a point to delete it. Pitch values snap to semitones with Shift. With notes selected, only
/// their curves can be changed.
class ExpressionLane : public InteractiveView {
  public:
    ExpressionLane(EditorContext& ctx, PianoRollView& owner) : InteractiveView(ctx), owner(owner) {}

    expr::Curve curve = expr::Curve::Pitch;

    /// The values the lane's height shows. Pitch has a choosable window (±2 to ±24 semitones).
    float low() const;
    float high() const;
    float valueAt(float y) const;
    float yFor(float value) const;
    edits::IdSet editable();
    std::string describe(float value) const;

    /// What the pointer is over.
    struct Hit {
        enum class Kind { None, Point, Bend, Span } kind = Kind::None;
        NoteId note;
        size_t index = 0; // the point, or the segment's first point
    };
    Hit hitTest(rack::math::Vec pos);

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  private:
    rack::math::Rect rangeButton() const;
    void openRangeMenu();

    PianoRollView& owner;
    float pitchWindow = 12;
};

} // namespace tessera
