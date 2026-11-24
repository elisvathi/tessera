#pragma once

#include "../../model/Track.hpp"
#include "../interaction/InteractiveView.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../widgets/HoverTooltip.hpp"

#include <array>
#include <map>
#include <optional>
#include <tuple>

namespace tessera {
class ToolButton;
}

namespace tessera {

/// The mixer: a third pane under the notes pane, folded to a tab by default (docs/design/AUDIO_MIXER.md §7). One
/// strip per track that makes sound (in Phase 1a: the audio tracks), the Master pinned at the right. The folded tab
/// is never dead: it shows a thin live meter per track, the master's peak and "MAIN not patched".
///
/// The tab is also the splitter: a click opens or folds the mixer, a drag sets its height, a double-click maximises.
/// It is a pane like the song and the notes: a click focuses it, Tab reaches it while it is open, and its maximise
/// button at the tab's right end is the same button the other panes have.
class MixerPane : public InteractiveView {
  public:
    MixerPane(EditorContext& ctx, bool panel);

    /// The height of the folded tab: 22 px, 16 in the in-panel editor (22 there too while maximised, so the
    /// panel's transport fits in it).
    float tabHeight() const;
    /// Room the tab keeps free at its left and right ends for buttons the host lays over it (the panel editor's
    /// transport and Full editor, while the mixer is maximised).
    void reserveTab(float left, float right) {
        tabLeft = left;
        tabRight = right;
    }
    /// Whether the song has anything for the mixer: with no sound, the pane stays folded and says nothing.
    bool hasSound() const;

    void step() override;
    EditorContext& context() { return ctx; }
    void onButton(const ButtonEvent& e) override;
    void draw(const DrawArgs& args) override;

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  public:
    /// One strip as laid out: whose it is (an invalid ID is the master) and where its parts are.
    struct Strip {
        TrackId track;
        bool master = false;
        static constexpr int kInputStrip = 100;
        bool isInput() const { return lane >= kInputStrip; }
        bool knob = false; // a short pane: `fader` is a level knob
        int64_t inputModule = -1; // an input strip's output module; -1 = the track's clips (the master's mix)
        bool loop = false;        // an input strip that is an effect loop: its fader is the Mix (DECISIONS §153)
        std::string label;        // an input strip's name, or a drum lane strip's lane name
        int lane = -1; // a drum track's lane IN (Drum Out output 0..7): a narrow strip after its track's
        GroupId group; // a group's strip, after its last track's (§3.5)
        bool pinned = false; // the master and its inputs: they stay at the right while the tracks scroll
        int familyEnd = -1;  // a parent strip's last folded strip (an index in the strips), framed with it
        rack::math::Rect
            fold; // the arrow left of the name that folds the strips under it (inputs, lanes, a group's tracks)
        rack::math::Rect box, name, chip, pan, mute, solo, fader, meter, readout, clipLight, connect;
        // Sends (§7.6, DECISIONS §164, §175): a slider row (or, when short, a dot) per send it has, the "+ Send" that
        // adds one, and on a receiving track a FROM row per send it gets with "+ From", and the OUT meter.
        rack::math::Rect receives, addSend, addFrom, outMeter;
        rack::math::Rect destination; // "→ Master" under the level: a click picks the Master or only its sends
        std::vector<rack::math::Rect> sendKnobs;
        /// One send row: whose send (track and part) to which track; `incoming` for a receiving strip's FROM rows.
        struct SendRow {
            TrackId track;
            SendFrom from;
            TrackId to;
            bool incoming = false;
        };
        std::vector<SendRow> sendRows; // one per entry of sendKnobs
        float sendsLabelY = -1, fromLabelY = -1; // where SENDS and FROM sit, when shown
        int playerIndex = -1; // its index among the player's tracks
    };
    enum class Part {
        None,
        Tab,
        Name,
        Chip,
        Pan,
        Mute,
        Solo,
        Fader,
        Readout,
        ClipLight,
        Connect,
        Receives,
        SendKnob,
        AddSend,
        AddFrom,
        Destination,
        Fold
    };
    struct Hit {
        Part part = Part::None;
        int strip = -1;
        int send = -1; // which of the strip's send knobs
    };

    // Used by the drags.
    const std::vector<Strip>& layoutStrips() const { return strips; }
    Hit hitTest(rack::math::Vec pos) const;
    /// The fader's travel in px for a strip, and the fader position (0..1) of a level.
    static float faderPos(float db);
    static float faderDb(float pos);
    /// The volume shown for a strip: the committed value, or the one a drag holds.
    float shownVolume(const Strip& s) const;
    float shownPan(const Strip& s) const;
    /// The colour of the strip's fader (`pan` false) or pan lane, when it has one: the control draws a ring in it.
    std::optional<NVGcolor> laneColor(const Strip& s, bool pan) const;
    void showHeld(TrackId track, float volumeDb, float pan) { held[track.value] = {volumeDb, pan}; }
    void clearHeld() {
        releaseInputPreview();
        held.clear();
        heldLanes.clear();
        heldSends.clear();
    }
    /// The level (dB) of a send row's send: what a drag holds, what its lane plays, the song's, or -inf when there
    /// is no such send.
    float shownSend(const Strip::SendRow& row) const;
    void showHeldSend(const Strip::SendRow& row, float db) {
        heldSends[{row.track.value, row.from.key(), row.to.value}] = db;
    }
    /// Where the strip's fader cap sits (0..1): its volume, or in Show sends its send, gliding over 120 ms between
    /// the two.
    float capPos(const Strip& s) const;
    /// A lane strip's level and pan: plays at once while a drag holds it, and commits one undo step.
    void holdLane(const Strip& s, Channel c);
    void setLane(const Strip& s, Channel c, const std::string& name);
    /// The menu to type a level or reset it (right-click, or a double-click on the readout).
    void openTyping(const Strip& s);
    /// The "+" menu: every track this one (or its part) can send to, in track order; loops greyed.
    void openAddSend(TrackId from, SendFrom part);
    /// A send row's menu: type a level, before or after the fader, remove.
    void openSendMenu(const Strip::SendRow& row);
    /// The source chip's menu: Add a Track Out for this track, and Show in the rack for each module that plays it.
    void openChipMenu(const Strip& s);

  private:
    void layout();
    void drawTab(NVGcontext* vg);
    void drawStrip(NVGcontext* vg, const Strip& s);
    /// A group's strip: its name and colour, pan, M, fader, meter and level, then "→ Master".
    void drawGroupStrip(NVGcontext* vg, const Strip& s, const TrackGroup& g);
    /// A narrow strip: a drum lane IN's (name and colour, pan, fader, meter and level) or an input's of a track or of
    /// the master (null `t`); an effect loop's fader is its Mix.
    void drawLaneStrip(NVGcontext* vg, const Strip& s, const Track* t);
    /// A strip in a short pane: level and pan knobs instead of a fader.
    void drawKnobStrip(NVGcontext* vg, const Strip& s);
    /// How loud a strip is now, in volts.
    float stripLevel(const Strip& s) const;
    /// Whether `track` (or the master) shows a strip per input: two or more inputs on a note or audio track, or the
    /// master's mix and a Track Out of the master.
    bool hasInputStrips(TrackId track) const;

  public:
    /// While a strip's name is dragged to reorder the chain: the strip and where the pointer is, for the marker.
    void showReorder(int strip, float x) {
        reorderStrip = strip;
        reorderX = x;
    }
    /// The input strip the chain module dropped at `x` lands before, as a module ID; -1 for the end of the chain.
    int64_t reorderTarget(int strip, float x) const;

  private:
    int reorderStrip = -1;
    float reorderX = 0;
    float hintX = 12; // where "N tracks play notes only" starts
    /// Ends the live level and pan of the input strip being dragged, if any.
    void releaseInputPreview();
    std::optional<std::pair<TrackId, int64_t>> previewedInput;
    void drawMeter(NVGcontext* vg, rack::math::Rect r, float left, float right);
    /// The peaks the meters hold for 1.5 s, by strip key.
    float heldPeak(uint64_t key, float level);

    bool panel;
    std::vector<Strip> strips;
    int notesOnly = 0; // tracks without strips
    float tabLeft = 0, tabRight = 0;
    rack::math::Rect tab;
    ToolButton* maximizeButton;
    std::map<uint64_t, std::pair<float, float>> held; // drags in progress: volume dB, pan
    std::map<std::pair<uint64_t, int>, Channel> heldLanes; // lane strip drags in progress
    std::map<std::tuple<uint64_t, uint64_t, uint64_t>, float> heldSends; // send drags: track, part, to → dB
    struct Peak {
        float level = 0;
        double at = 0;
    };
    std::map<uint64_t, Peak> peaks;
    size_t missingTold = 0; // how many missing files the last toast was about
    bool sendDots = false;  // sends show as dots: the pane is too short for their rows
    float scrollX = 0;      // how far the tracks are scrolled to the left, in px
    float zoom = 1;         // how wide the strips are, 1 = their usual width
    float contentWidth = 0; // the tracks' strips from the first to the last, unscrolled
    float scrollRight = 0;  // where the scrolling strips end: the pinned master strips start there
    /// The drum lane INs of `t` that have a cable, each with a strip of its own.
    std::vector<int> patchedLanes(const Track& t) const;
    /// The part of its track a strip sends from: the whole track, a lane, the clips or one input (DECISIONS §175).
    static SendFrom partOf(const Strip& s);
    /// Where a lane on a drum lane's or an input's level and pan has it now (NAN for whichever has none); false when
    /// no lane plays on it.
    bool partAutomated(const Strip& s, float& db, float& pan) const;
    /// Whether a strip can have sends: a track's strip, a drum lane's, or a track's clips or input that adds sound.
    bool canSend(const Strip& s) const;
    /// Whether a track's strip shows FROM rows: it receives sends, or it has an effect patched to it.
    bool showsFrom(const Track& t) const;
    /// The send rows of a strip (its SENDS and FROM) and "+ Send" / "+ From".
    void drawSends(NVGcontext* vg, const Strip& s);
    HoverTooltip tip{""};
    std::string tipText; // what the tooltip says now; empty when none shows
    /// The frames that hold a parent strip and what it folds (`pinned`: the master's, else the tracks'), drawn behind
    /// the strips in the parent's colour.
    void drawFamilies(NVGcontext* vg, bool pinned);
    /// Shows the tooltip for the send or "+ Send" under the mouse.
    void updateTip();

  public:
    /// Scrolls the tracks sideways by `px` (positive shows more on the right), kept inside the strips.
    void scrollBy(float px);
    /// Widens or narrows every strip by `factor`, keeping what is at `x` in place.
    void zoomAt(float x, float factor);
    /// The view keys, as in the piano roll: = and - zoom, F fits every strip, Home and End, Page Up and Down. True
    /// when handled.
    bool handleAction(Action action);
};

} // namespace tessera
