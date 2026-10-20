#pragma once

#include "../interaction/TrackRows.hpp"
#include "EditorPane.hpp"
#include "Ruler.hpp"
#include "Sources.hpp"
#include "TimelineView.hpp"
#include "TrackHeaders.hpp"
#include "../widgets/EditTools.hpp"

namespace tessera {

class CachedLayer;
class ArrangerLanes;
class ToolButton;
class LauncherPanel;

/// The top pane, the song (EDITOR_UX §6): track headers, the clips (LauncherPanel) and the timeline (ruler and one
/// lane per row with the clips placed on it), side by side. Rows are tracks and group headers (TrackRows); folded
/// groups hide their tracks. The section that plays (the PLAYS switch) is framed; the other one steps back.
class ArrangerView : public EditorPane {
  public:
    explicit ArrangerView(EditorContext& ctx);

    bool handleAction(Action action) override;
    void layout() override;
    void step() override;
    void draw(const DrawArgs& args) override;

    TimeViewport viewport;
    TrackAxis tracks; // one entry per row
    ArrangerSource source;
    std::vector<TrackRow> rows; // rebuilt every frame from the song and the folded groups

    static constexpr double kGroupRowHeight = 26;
    static constexpr double kMinStackRow = 28; // a track with stacked clips grows so each sub-row keeps this
    static constexpr double kAddLaneRowHeight = 24;

    /// Stores the track rows' heights (after a vertical zoom) in the view state.
    void writeHeights();
    /// The heights rows were given this frame, to tell a user's resize from the growth stacked clips cause.
    std::vector<double> syncedHeights;

    ArrangerLanes* lanesView() { return lanes; }
    /// The launcher panel, or nullptr while it is hidden.
    LauncherPanel* panelView();

    /// The tick under the mouse, or NaN when the mouse isn't over the lanes.
    double tickUnderMouse();

    /// What Delete, Ctrl/Cmd+D and Ctrl/Cmd+A act on: clips after a click in the lanes, tracks after a click
    /// in the headers.
    enum class Target { Clips, Tracks, Automation, Range };
    Target target = Target::Tracks;

    /// Room the in-panel editor keeps at the corner's left for its transport buttons.
    float cornerInset = 0;

  private:
    void syncFromState();
    void saveToState();
    void applyZoomLimits();
    /// Scrolls the selected track's row into view when the selection changes.
    void revealSelectedTrack();
    /// The corner above the track headers: the tool switch, then the clips' launch quantize and stop all when
    /// there is room (otherwise the tool caret's menu holds them).
    void layoutCorner(float rulerHeight);
    /// The tool caret's menu after the tools: Range, the timeline's settings, and the clips' when the corner has
    /// no room for them.
    void appendSongMenu(rack::ui::Menu* menu);
    void drawPlayingSection(NVGcontext* vg);
    /// The tab that stands for the folded clips in a narrow song pane, and the RIPPLE tag.
    void drawCornerLabels(NVGcontext* vg);

    TrackHeaders* headers;
    ToolSwitch toolSwitch;
    ToolButton* backButton;     // "back to timeline", shown while launched clips have taken tracks over
    ToolButton* quantizeButton; // "Launch 1 bar": launch quantize, and recording the clips into the timeline
    ToolButton* stopAllButton;
    ToolButton* foldTab; // a narrow pane folds the clips while the timeline plays; this opens and closes them
    ToolButton* maximizeButton;
    float rippleTagX = -1; // where the RIPPLE tag goes, or -1 when there is no room
    Ruler* ruler;
    ArrangerLanes* lanes;
    LauncherPanel* panel;
    bool restored = false;
    double lastStep = 0;
    TrackId revealedTrack;
};

} // namespace tessera
