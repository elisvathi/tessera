#pragma once

#include "../interaction/InteractiveView.hpp"

#include <array>
#include <map>
#include <optional>

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
        int lane = -1; // a drum track's lane IN (Drum Out output 0..7): a narrow strip after its track's
        rack::math::Rect box, name, chip, pan, mute, solo, fader, meter, readout, clipLight, connect;
        // Sends (§7.6): "← N" when it receives some, a knob per send it has, the "+" that adds one, the OUT meter
        // of a track that receives sends, and "pre" above its fader while Show sends shows its send.
        rack::math::Rect receives, addSend, outMeter, pre;
        std::vector<rack::math::Rect> sendKnobs;
        int playerIndex = -1; // its index among the player's tracks
    };
    enum class Part {
        None, Tab, Name, Chip, Pan, Mute, Solo, Fader, Readout, ClipLight, Connect,
        Receives, SendKnob, AddSend, Pre, BandClose
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
        held.clear();
        heldLanes.clear();
        heldSends.clear();
    }
    /// The level (dB) of the send from strip `s`'s track to `to`: what a drag holds, the song's, or -inf when there
    /// is no send.
    float shownSend(const Strip& s, TrackId to) const;
    void showHeldSend(TrackId from, TrackId to, float db) { heldSends[{from.value, to.value}] = db; }
    /// Where the strip's fader cap sits (0..1): its volume, or in Show sends its send, gliding over 120 ms between
    /// the two.
    float capPos(const Strip& s) const;
    /// Whether Show sends is on (for a track that still has a strip).
    bool showingSends() const { return ctx.view().showSends.valid(); }
    /// A lane strip's level and pan: plays at once while a drag holds it, and commits one undo step.
    void holdLane(const Strip& s, Channel c);
    void setLane(const Strip& s, Channel c, const std::string& name);
    /// The menu to type a level or reset it (right-click, or a double-click on the readout).
    void openTyping(const Strip& s);
    /// The "+" menu: every track this one can send to, in track order; the ones that would make a loop greyed.
    void openAddSend(TrackId from);
    /// A send knob's menu: type a level, before or after the fader, remove.
    void openSendMenu(TrackId from, TrackId to);
    /// The source chip's menu: Add a Track Out for this track, and Show in the rack for each module that plays it.
    void openChipMenu(const Strip& s);

  private:
    void layout();
    void drawTab(NVGcontext* vg);
    void drawStrip(NVGcontext* vg, const Strip& s);
    /// A drum lane IN's narrow strip: the lane's name and colour, pan, fader, meter and level.
    void drawLaneStrip(NVGcontext* vg, const Strip& s, const Track& t);
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
    std::map<std::pair<uint64_t, uint64_t>, float> heldSends; // send drags in progress: from, to → dB
    rack::math::Rect band, bandClose; // Show sends' band across the top of the strips
    struct Peak {
        float level = 0;
        double at = 0;
    };
    std::map<uint64_t, Peak> peaks;
    size_t missingTold = 0; // how many missing files the last toast was about
};

} // namespace tessera
