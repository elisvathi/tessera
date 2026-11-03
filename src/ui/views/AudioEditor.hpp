#pragma once

#include "../interaction/InteractiveView.hpp"

#include "../../model/AudioSource.hpp"
#include "../../model/Song.hpp"

#include <optional>

namespace tessera {

/// The audio editor: what the lower pane shows for an audio clip, in the piano roll's place under the same header
/// (docs/design/AUDIO_MIXER.md §6.4). Its x axis is file time: the whole file runs across, the part the clip plays
/// (its window) bright and the rest dim. Dragging the window slips it through the file, its edges trim it, and the
/// beat ruler on top sets the clip tempo: drag a beat line and the grid stretches from beat 1.
class AudioEditor : public InteractiveView {
  public:
    explicit AudioEditor(EditorContext& ctx) : InteractiveView(ctx) {}

    /// The clip shown, set by the piano roll each frame.
    ClipId clip;

    void step() override;
    EditorContext& context() { return ctx; }

    // ---- For the drags ----

    /// What is under a point: the window, one of its edges, or a beat line of the ruler.
    enum class Part { None, Window, LeftEdge, RightEdge, Beat };
    struct Hit {
        Part part = Part::None;
        int beat = 0; // Beat: which beat line (1 is the first after beat 1)
    };
    Hit hitTest(rack::math::Vec pos);

    /// The shown clip, its audio as a drag previews it, and its file.
    const Clip* shownClip();
    AudioContent shownAudio();
    const AudioSource* source();
    /// Source frames the clip's window covers, [start, end), as a drag previews them.
    std::pair<double, double> window();
    /// Source frames per beat at the clip tempo (the shown one, while a drag changes it).
    double framesPerBeat();

    double frameToX(double frame) const { return (frame - left) / framesPerPx; }
    double xToFrame(double x) const { return left + x * framesPerPx; }
    double pixelFrames() const { return framesPerPx; }

    /// What a drag shows before it commits: another start in the file, another window, another tempo.
    struct Preview {
        std::optional<int64_t> sourceStart;
        std::optional<std::pair<int64_t, int64_t>> window;
        std::optional<double> tempo;
    };
    Preview preview;

    /// Shows the whole file.
    void fit();

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  private:
    void drawRuler(NVGcontext* vg, rack::math::Rect r);
    /// The timeline copy of the clip that playback and "play from here" use: the selected one, else the first.
    const ArrangerClip* instance();
    rack::math::Rect waveRect() const;

    double left = 0;          // the source frame at x = 0
    double framesPerPx = 100; // zoom
    ClipId fitted;            // the clip the view was last fitted to
    double fittedWidth = 0;
};

} // namespace tessera
