#pragma once

#include "../interaction/InteractiveView.hpp"

#include <array>
#include <map>

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

    /// The height of the folded tab: 22 px, 16 in the in-panel editor.
    float tabHeight() const;
    /// Whether the song has anything for the mixer: with no sound, the pane stays folded and says nothing.
    bool hasSound() const;

    void step() override;
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
        rack::math::Rect box, name, chip, pan, mute, solo, fader, meter, readout, clipLight, connect;
        int playerIndex = -1; // its index among the player's tracks
    };
    enum class Part { None, Tab, Name, Pan, Mute, Solo, Fader, Readout, ClipLight, Connect };
    struct Hit {
        Part part = Part::None;
        int strip = -1;
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
    void showHeld(TrackId track, float volumeDb, float pan) { held[track.value] = {volumeDb, pan}; }
    void clearHeld() { held.clear(); }
    /// The menu to type a level or reset it (right-click, or a double-click on the readout).
    void openTyping(const Strip& s);

  private:
    void layout();
    void drawTab(NVGcontext* vg);
    void drawStrip(NVGcontext* vg, const Strip& s);
    void drawMeter(NVGcontext* vg, rack::math::Rect r, float left, float right);
    /// The peaks the meters hold for 1.5 s, by strip key.
    float heldPeak(uint64_t key, float level);

    bool panel;
    std::vector<Strip> strips;
    int notesOnly = 0; // tracks without strips
    rack::math::Rect tab;
    ToolButton* maximizeButton;
    std::map<uint64_t, std::pair<float, float>> held; // drags in progress: volume dB, pan
    struct Peak {
        float level = 0;
        double at = 0;
    };
    std::map<uint64_t, Peak> peaks;
    size_t missingTold = 0; // how many missing files the last toast was about
};

} // namespace tessera
