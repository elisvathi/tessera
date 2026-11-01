#pragma once

#include "../../model/NoteEdits.hpp"
#include "../../model/NoteFields.hpp"
#include "../interaction/InteractiveView.hpp"

namespace tessera {

class PianoRollView;

/// A stem lane under the piano roll (EDITOR_UX §4.6): one stem per note showing one of its values (velocity,
/// release, chance, velocity spread, repeats or micro-timing).
///
/// Drag a stem to set it (with several notes selected, all change together). Shift-drag (or the Draw tool)
/// draws values freehand across stems; Alt-drag draws a straight ramp. While notes are selected, only they
/// can be changed, so nothing else is edited by accident.
///
/// On drum hits it also shows pitch, timbre or pressure as one value per hit. A hit whose curve is shaped has a
/// small wave on its stem, and dragging the stem moves the whole curve. Double-click a stem to zoom onto the
/// hit's curve; right-click it to shape, flatten or clear it.
class NoteValueLane : public InteractiveView {
  public:
    NoteValueLane(EditorContext& ctx, PianoRollView& owner) : InteractiveView(ctx), owner(owner) {}

    fields::Field field = fields::Field::Velocity;

    /// Stem x for each note, with chords fanned a few pixels apart so each stem can be grabbed.
    std::vector<std::pair<NoteId, float>> stemPositions(const edits::NoteList& notes);
    /// The values the lane's height shows: the field's range, or a choosable window for pitch (±2 to ±48).
    double low() const;
    double high() const;
    /// The field's value (display units) at a height, and back.
    double valueAt(float y) const;
    float yFor(double value) const;
    /// Where stems start: the bottom, or the zero line for values that can go negative.
    float baseY() const;
    /// The notes this lane may change: the selection if there is one, else all.
    edits::IdSet editable();
    std::string describe(double value) const;

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  private:
    NoteId stemAt(rack::math::Vec pos);
    /// The field is a hit's pitch, timbre or pressure (a curve shown as one value).
    bool expressionField() const;
    rack::math::Rect rangeButton() const;
    /// A stem's right-click menu: shape the curve, flatten it to one value, or clear it.
    void openStemMenu(NoteId id);

    PianoRollView& owner;
};

} // namespace tessera
