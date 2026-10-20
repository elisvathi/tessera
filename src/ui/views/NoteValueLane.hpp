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
class NoteValueLane : public InteractiveView {
  public:
    NoteValueLane(EditorContext& ctx, PianoRollView& owner) : InteractiveView(ctx), owner(owner) {}

    fields::Field field = fields::Field::Velocity;

    /// Stem x for each note, with chords fanned a few pixels apart so each stem can be grabbed.
    std::vector<std::pair<NoteId, float>> stemPositions(const edits::NoteList& notes);
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
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  private:
    NoteId stemAt(rack::math::Vec pos);

    PianoRollView& owner;
};

} // namespace tessera
