#pragma once

#include "../widgets/EditTools.hpp"

#include "../../model/NoteEdits.hpp"
#include "../editing/NoteTarget.hpp"
#include "EditorPane.hpp"
#include "Sources.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace tessera {

class DrumGrid;
class DrumVelocityLane;
class ToolButton;

/// The drum editor's step grid (DESIGN.md §9, EDITOR_UX §5): one row per lane, with the lane's header (name,
/// mute, solo) and its steps. A step's fill height is its velocity. Lanes with their own loop length show their
/// repeats faded (polymeter).
///
/// The tools are the piano roll's that make sense on steps (Pointer, Draw, Erase, Mute, Velocity):
/// - Pointer: click an empty step to turn it on, click a step to select it (Shift adds, Ctrl/Cmd toggles), drag
///   from empty space to select a box, drag selected steps to move them (Ctrl/Cmd+drag copies).
/// - Draw: click or drag across steps to turn them on or off; right-drag removes them.
/// - Erase, Mute: click or drag across steps. Velocity: drag a step up and down (the selection with it).
/// With any tool, Alt+drag a step for its velocity and Ctrl/Cmd+Alt+drag to select. Right-click a step for the
/// selection's operators, or a lane header for its menu. G switches to the piano roll. The velocity lane below
/// shows the steps of the lane last clicked; drag across it to set their velocities.
class DrumEditorView : public EditorPane, public NoteTarget {
  public:
    explicit DrumEditorView(EditorContext& ctx);

    /// Room the in-panel editor's "Full editor" button takes at the header's right end while this pane is on top.
    float headerReserve = 0;

    bool handleAction(Action action) override;
    void layout() override;
    void step() override;
    void draw(const DrawArgs& args) override;

    EditorContext& context() { return ctx; }
    const Track* track();

    const Clip* clip() override;
    const edits::NoteList& notes() override;
    const edits::NoteList& committedNotes() override;
    void setPreview(edits::NoteList notes) override { preview = std::move(notes); }
    void clearPreview() override { preview.reset(); }
    edits::IdSet selectedIds() override;
    bool isSelected(NoteId id) override;
    void select(edits::IdSet ids) override;
    void commit(const std::string& name, const edits::NoteList& before, const edits::NoteList& after,
                std::optional<edits::IdSet> selectAfter = std::nullopt) override;

    /// The tool the step grid uses for the chosen one: it has no Paint or Slice, so those draw.
    static Tool acting(Tool chosen);

    ClipSource source; // the shown clip's playhead
    /// The lane the velocity lane shows: the one last clicked. Session only.
    int velocityLane = 0;

  private:
    ToolButton* clipChip; // the clip's name, length and menu, as the piano roll's header starts
    ToolButton* kitButton;
    std::pair<ToolButton*, ToolButton*> drumSwitch; // steps / piano roll
    ToolButton* maximizeButton;
    ToolButton* devicesToggle;
    ToolSwitch tools;       // Select, Draw, and a caret with Erase, Mute and Velocity
    ToolButton* hearButton; // hear steps while editing
    DrumGrid* grid;
    DrumVelocityLane* velocity;
    std::optional<edits::NoteList> preview;
};

} // namespace tessera
