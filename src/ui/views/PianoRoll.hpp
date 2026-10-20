#pragma once

#include "../widgets/EditTools.hpp"

#include "../../model/ExpressionEdits.hpp"
#include "../../model/NoteEdits.hpp"
#include "../../model/Scale.hpp"
#include "../editing/LivePlay.hpp"
#include "../editing/NoteTarget.hpp"
#include "EditorPane.hpp"
#include "Ruler.hpp"
#include "Sources.hpp"
#include "TimelineView.hpp"

#include <functional>
#include <optional>

namespace tessera {

class NoteGrid;
class PianoKeyboard;
class NoteValueLane;
class ExpressionLane;
class ClipLaneView;
class ToolButton;
class LaneTabs;

/// The detail pane: the piano roll for the selected track's clip (EDITOR_UX §4).
///
/// It owns what the grid, keyboard and velocity lane share: the axes, the live preview of a running gesture,
/// and the editing API that gestures and keyboard commands use (selection, commit, audition, snap).
class PianoRollView : public EditorPane, public NoteTarget {
  public:
    explicit PianoRollView(EditorContext& ctx);

    /// Room the in-panel editor's "Full editor" button takes at the header's right end while this pane is on top.
    float headerReserve = 0;

    bool handleAction(Action action) override;
    void layout() override;
    void step() override;
    void draw(const DrawArgs& args) override;

    TimeViewport viewport;
    PitchAxis pitch;
    ClipSource source;
    /// The last click was in the automation lane, so editing keys act on its points rather than on notes.
    bool automationFocus = false;
    /// Notes whose velocity tag stays visible: the ones a velocity drag is changing.
    edits::IdSet velocityShown;
    /// Curve points picked in the grid (Bend tool, or a timbre / pressure strip): dragging one moves them all,
    /// Delete deletes them. They belong to `pointsCurve`. Not saved.
    expr::Curve pointsCurve = expr::Curve::Pitch;
    /// The last press was in the lower curve lane, so Delete acts on points picked there.
    bool curveLaneFocus = false;
    std::vector<expr::PointRef> curvePoints;
    bool pointPicked(NoteId note, expr::Curve curve, const ExprPoint& p) const;

    EditorContext& context() { return ctx; }

    // ---- What is being edited ----

    /// The track whose clip is shown (the selected track), or nullptr.
    const Track* track();
    /// The clip being edited, or nullptr when the track has none.
    const Clip* clip() override;
    ClipId clipId() const { return source.clip; }
    TimeSig timeSig() { return source.timeSig(); }

    /// The notes to draw: the running gesture's preview, or the clip's notes.
    const edits::NoteList& notes() override;
    /// The clip's notes as committed, for gestures to start from.
    const edits::NoteList& committedNotes() override;
    void setPreview(edits::NoteList notes) override { preview = std::move(notes); }
    void clearPreview() override { preview.reset(); }

    // ---- Selection ----

    /// The selected notes of this clip (empty when the selection belongs to another clip).
    edits::IdSet selectedIds() override;
    void select(edits::IdSet ids) override;
    bool isSelected(NoteId id) override;

    // ---- Editing ----

    /// Records `before` -> `after` as one undo step named `name`. Only the notes that differ are stored.
    void commit(const std::string& name, const edits::NoteList& before, const edits::NoteList& after,
                std::optional<edits::IdSet> selectAfter = std::nullopt) override;
    /// The grid step for snapping at the current zoom (0 when snapping is off).
    Tick snapStep();
    NoteId newNoteId();
    /// The channel new notes get: the selected notes' channel when they share one (so after setting a selection
    /// to channel 2, drawing goes on in channel 2), else the channel of the last note touched.
    uint8_t drawChannel();
    /// Plays a short preview of a note on the track's outputs (when audition is on).
    void audition(int pitchValue, float velocity);
    /// Holds a note (keyboard click) until auditionOff().
    void auditionOn(int pitchValue, float velocity);
    void auditionOff(int pitchValue);

    // ---- Key and scale tools (DESIGN.md §13) ----

    /// The key the clip is edited in: its own, or the song's. Chromatic when there is none.
    Scale scale();
    /// A real key (not chromatic) on an instrument track: drum notes are lanes, so keys never touch them.
    bool keyActive();
    bool highlightKey();
    bool snapToKey();
    /// Moves a pitch by grid rows (positive = down): key steps with scale snap (unless `chromatic`), shown rows
    /// when folded, else semitones.
    int stepPitch(int pitchValue, int rows, bool chromatic);
    edits::NoteList stepPitches(const edits::NoteList& notes, const edits::IdSet& ids, int rows, bool chromatic);
    /// The pitch a new note pointed at `pitchValue` gets: the nearest key note with scale snap (unless
    /// `chromatic`).
    int placePitch(int pitchValue, bool chromatic);
    /// The pitches Draw creates at `root`: the chord stamp's chord, or just the root.
    std::vector<int> stampPitches(int root);

    /// The expression curve the lower pane edits, if it shows one (the grid then draws it inside each note).
    std::optional<expr::Curve> shownCurve();
    /// The curve the grid shows in a strip above the selected notes, to edit there: timbre or pressure, while the
    /// lower pane shows it.
    std::optional<expr::Curve> stripCurve();
    /// The lower pane shows Pitch: a press on a note in the grid adds and drags bend points instead of moving it.
    bool bendMode();
    /// The curve whose points the grid edits now: the strip's, else Pitch.
    std::optional<expr::Curve> editableCurve();

    /// Creates an empty clip on the selected track (the "double-click to create" empty state).
    void createClip();

  private:
    void applyZoomLimits();
    /// True when at least one note of the shown clip is on screen; a clip with no notes counts as in view.
    /// Used on open, so a saved scroll never leaves the clip looking empty.
    bool anyNoteInView();
    /// What the lower lane shows; `withNoteLanes` false lists only the clip's automation lanes (the last tab).
    void openLowerMenu(bool withNoteLanes = true);
    void openAutomationMenu() { openLowerMenu(false); }
    /// Folds the pitch axis to the key's rows (or a drum track's lanes) when fold is on.
    void updateRows();
    void drawHeader(NVGcontext* vg);
    /// The View menu's items: ghost notes, note badges, hearing notes, follow (and fold for drum tracks).
    void appendViewItems(rack::ui::Menu* menu);
    /// Places the header's chips and buttons. Returns true when it had to use the short forms.
    bool layoutToolbar(float header);
    bool editAction(Action action);
    /// Applies a keyboard edit to the selection (or the whole clip for select-type actions).
    void editSelection(const std::string& name,
                       const std::function<edits::NoteList(const edits::NoteList&, const edits::IdSet&)>& change);
    void copySelection(bool cut);
    void paste(bool atMouse);
    void cycleSelection(Action action);

    PianoKeyboard* keys;
    Ruler* ruler;
    NoteGrid* grid;
    NoteValueLane* values;
    ExpressionLane* expression;
    ClipLaneView* clipLane;
    ToolButton* lowerButton; // the lower lane's label, which picks what it shows: a note value, a curve, automation
    ToolButton* clipChip;    // "BASS · Verse · 8 bars ▾": the clip's menu (rename, length, loop, files)
    ToolSwitch tools;
    ToolButton* snapButton;
    ToolButton* keyButton; // the key, with its highlight, snap and fold switches under the picker
    std::pair<ToolButton*, ToolButton*> drumSwitch; // drum tracks: steps / piano roll
    ToolButton* notesButton;                        // the transforms menu
    ToolButton* viewButton;                         // ghost notes, note badges, hearing notes, follow
    ToolButton* overflowButton;                     // a narrow header: Notes and View in one "⋯"
    ToolButton* hearButton;                         // hear notes while editing: on screen, since it is switched often
    ToolButton* maximizeButton;
    ToolButton* devicesToggle;
    float toolbarLeft = 0;

    StepInput stepper;
    bool stepWasOn = false;
    ClipId pointsClip;               // the clip curvePoints were picked in
    Tool pointsTool = Tool::Pointer; // and the tool

    std::optional<edits::NoteList> preview;
    bool restored = false;
    ClipId shownClip;
    double lastStep = 0;

    friend class NoteGrid;
    friend class NoteValueLane;
};

/// "C4", "F#2": MIDI note names with C4 = 60.
std::string pitchName(int pitch);
bool isBlackKey(int pitch);

} // namespace tessera
