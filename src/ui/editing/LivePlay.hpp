#pragma once

#include "../../model/NoteEdits.hpp"
#include "../EditorContext.hpp"

#include <rack.hpp>

#include <map>
#include <optional>
#include <set>

namespace tessera {

class PianoRollView;

/// The computer keyboard as an instrument (EDITOR_UX §4.10), laid out like a piano: the A row plays white keys from C,
/// the W row the black keys, Z / X change octave, C / V velocity. Notes go to armed tracks like MIDI notes do
/// (monitor, record, capture) and are heard on the selected track when it isn't armed.
class KeyboardPlay {
  public:
    /// Returns true when the key belongs to keyboard play (only while it is on).
    bool handleKey(EditorContext& ctx, const rack::widget::Widget::KeyBaseEvent& e);
    /// Releases every held note (turning keyboard play off, closing the editor).
    void releaseAll(EditorContext& ctx);

    /// The note a key plays at an octave offset, if it is a note key.
    static std::optional<int> pitchFor(int key, int octave);

  private:
    std::map<int, int> held; // key -> pitch, so a release ends the note its press started
    TrackId heardOn;
};

/// Step input (EDITOR_UX §4.2): notes played on any input are written at a cursor that moves on by the grid.
/// Notes held together are a chord in one step; the cursor moves on when the last one is released.
class StepInput {
  public:
    void start(PianoRollView& view);
    /// Each frame while on: writes the notes played since the last frame.
    void update(PianoRollView& view);
    /// Right arrow: a rest. Shift+Right: the last step's notes last one step longer. Backspace: removes the last
    /// step. Left: the cursor steps back.
    void rest(PianoRollView& view);
    void tie(PianoRollView& view);
    void undoStep(PianoRollView& view);
    void back(PianoRollView& view);

  private:
    Tick step(PianoRollView& view);

    std::set<int> down;                // pitches held now
    edits::IdSet chord;                // notes written for the step being played
    Tick chordAt = -1;                 // where that step started
    std::vector<edits::IdSet> history; // each step's notes, for undoing steps (empty = a rest)
};

} // namespace tessera
