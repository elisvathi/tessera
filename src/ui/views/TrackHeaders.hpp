#pragma once

#include "../../model/Recording.hpp"
#include "../interaction/InteractiveView.hpp"
#include "../interaction/TrackRows.hpp"
#include "../widgets/HoverTooltip.hpp"

#include <optional>

namespace tessera {

class ArrangerView;

/// The mark beside a lane that says whether the next take records into it: a red dot, an amber "!" when its source
/// is missing, a crossed circle when it is kept, nothing when it won't record.
void drawLaneRecordMark(NVGcontext* vg, rack::math::Vec at, LaneRecordState state);

/// The column of track headers beside the arranger (EDITOR_UX §6.2).
///
/// Track rows: a colour tile with the track number, the name, then arm, lanes, mute and solo at a fixed size.
/// Arm and the lanes arrow are drawn small and dim until they are on; a missing output module shows as an
/// amber jack only when it is missing. Group rows: fold arrow, name, track
/// count. Click selects (Shift: range, Ctrl/Cmd: toggle); drag reorders, onto a group header to join it;
/// the bottom edge resizes (Alt: all tracks); double-click renames; right-click opens the menu.
class TrackHeaders : public InteractiveView {
  public:
    TrackHeaders(EditorContext& ctx, ArrangerView& owner) : InteractiveView(ctx), owner(owner), tip("") {}

    /// Where a drag would drop: before which track (song index), into which group, and where to draw the line.
    struct DropTarget {
        int before = 0;
        std::optional<GroupId> group;
        float lineY = 0;
        bool indented = false;
    };
    DropTarget dropTargetAt(float y);
    /// Opens the inline name field over a row.
    void startRename(int row);
    /// The thin Master row under the add row: shown once the song has something that reaches the master.
    static constexpr float kMasterRowHeight = 22;
    bool showsMaster() const;

  protected:
    void step() override;
    void drawView(const DrawArgs& args) override;
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    void onLeave(const LeaveEvent& e) override;

  private:
    enum class Part {
        None,
        Body,
        Name,
        Fold,
        Automation,
        Arm,
        Output,
        Mute,
        Solo,
        Edge,
        AddRow,
        Empty,
        LaneMute,
        LaneState, // the mark that says whether the lane records: not a button
        AddLane,
        Level, // an audio track's level bar: its fader and meter in one (docs/design/AUDIO_MIXER.md §7.5)
        Pan,
        Master,      // the Master row, outside its level bar
        MasterLevel, // the Master row's level bar
        MixFold,     // a track's or a part's fold for its mix rows, with a dot per row it hides
        MixLevel,    // a mix row's level: a part's, or a send's
        MixPan,      // a part's pan
        MixMute,     // an input's M
        MixSolo,     // an input's S
        WidthEdge,   // the column's right edge: drag to widen the headers
        From         // the words that say where the track's notes or sound come from: opens the menu that sets it
    };
    struct Hit {
        int row = -1;
        Part part = Part::None;
    };
    Hit hitTest(rack::math::Vec pos);
    float lineY(float top, float height);
    float indentOf(const TrackRow& row) const;
    /// Narrow columns (the in-panel editor) use smaller buttons so the name keeps its room.
    bool compact() const;
    /// The amber "no output module" mark, left of arm.
    rack::math::Rect outputRect(float top, float height);
    /// Whether a track shows that mark: it has notes but no output module plays them. An empty track has
    /// nothing to warn about, so a fresh song isn't covered in warnings.
    bool missingOutput(const Track& track);
    rack::math::Rect armRect(float top, float height);
    rack::math::Rect muteRect(float top, float height);
    rack::math::Rect soloRect(float top, float height);
    rack::math::Rect nameRect(int row);
    /// The second line of a track that makes sound: the level bar, its dB value and the pan knob. Empty rects for
    /// tracks that make no sound, and rows too short for a second line.
    rack::math::Rect levelRect(const Track& t, int row, float top, float height);
    rack::math::Rect panRect(const Track& t, int row, float top, float height);
    void drawLevelLine(NVGcontext* vg, const Track& t, int row, float top, float height);
    void drawTrackRow(NVGcontext* vg, int row, float top, float height);
    void drawGroupRow(NVGcontext* vg, int row, float top, float height);
    /// An automation lane's header: name, output, value now, mute. And the "+ Add lane" row under them.
    void drawLaneRow(NVGcontext* vg, int row, float top, float height);
    void drawAddLaneRow(NVGcontext* vg, int row, float top, float height);
    void drawAddRow(NVGcontext* vg);
    rack::math::Rect masterRowRect();
    rack::math::Rect masterLevelRect();
    /// The Master row's lanes arrow, between its name and its level bar.
    rack::math::Rect masterLanesRect();
    void drawMasterRow(NVGcontext* vg);
    /// The "+ Add track" row right after the last track.
    rack::math::Rect addRowRect();
    void openAddMenu();

    /// The tooltip for the part under the mouse, or "" for none.
    std::string tipFor(const Hit& hit);

    // Mix rows (docs/DECISIONS.md §197): a track's parts, its sends and the sends it receives, under it.
  public:
    /// Where a mix row's parts are. Empty rects for what the row doesn't have.
    struct MixGeometry {
        rack::math::Rect fold, name, level, pan, mute, solo;
        float dbRight = 0; // where the dB text ends, 0 when there is no room for it
    };
    /// A level and pan being dragged on a mix row, shown until the drag ends.
    struct MixHold {
        TrackRow row;
        Channel value;
    };
    std::optional<MixHold> mixHold;
    /// A mix row's level (dB) and pan as shown: the drag's, else the song's or its module's.
    Channel mixValue(const TrackRow& row);
    /// The sending track, the receiving track and the part of a send or received-send row.
    static void sendOf(const TrackRow& row, TrackId& from, TrackId& to);
    /// Sets a mix row's level and pan as one undo step.
    void commitMix(const TrackRow& row, Channel value, const std::string& name);

  private:
    MixGeometry mixGeometry(int row, float top, float height);
    /// The fold of a track row with mix rows to show, or an empty rect.
    rack::math::Rect trackMixFold(int row, float top, float height);
    /// The rows a track's or part's fold hides, as the colours of their dots.
    std::vector<TrackId> mixDots(const TrackRow& row);
    /// Draws a fold arrow and its dots in `r`.
    void drawMixFold(NVGcontext* vg, rack::math::Rect r, bool open, const std::vector<TrackId>& dots, bool hot);
    void drawMixRow(NVGcontext* vg, int row, float top, float height);
    /// What a mix row is called: the part's name, "→ Reverb", "← Drums · Snare".
    std::string mixName(const TrackRow& row);
    /// The track whose colour a mix row wears: its own for a part, the receiver's for a send, the sender's for a
    /// received send.
    TrackId mixColorTrack(const TrackRow& row);
    /// Opens or folds a track's mix rows (`part` whole) or a part's sends.
    void toggleMix(TrackId track, SendFrom part);
    /// A click on a mix row: an input's M or S, or the right-click menu (a send's items, or a part's Send to).
    void clickMix(const TrackRow& row, const Hit& hit, int button, Modifiers mods);
    /// How loud a part of a track is now, in volts, as its mixer strip's meter reads it (the whole track for
    /// `part` whole).
    float partLevel(TrackId track, SendFrom part);
    /// A mix row's meter, in volts: its part's level, or for a send its sender's scaled by the send.
    float mixLevel(const TrackRow& row);
    /// Opens the name field over a part row: a drum lane's name, or an input's (the clips have none).
    void startMixRename(int row);
    /// The tooltip for a part of a mix row, or for a track's mix fold.
    std::string mixTip(const TrackRow& row, Part part);

    /// Where a track's notes or sound come from, in words (docs/DECISIONS.md §198): "Bass" for a track that plays
    /// another's notes, "2 sends (Drums, Keys)" for one that receives; both joined. Empty when neither. `color` is
    /// the source track's colour, or none for sends only.
    std::string fromWords(const Track& t, std::optional<NVGcolor>& color);
    /// Where those words sit: under the name in a tall row without a level line, else after the name, shortened
    /// before the name is. `nameWidth` gets how wide the name may be drawn. An empty rect when there is no room.
    rack::math::Rect fromRect(int row, float top, float height, float* nameWidth = nullptr);

    ArrangerView& owner;
    Hit hover;
    Hit tipHit;
    HoverTooltip tip;
};

} // namespace tessera
