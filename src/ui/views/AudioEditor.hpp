#pragma once

#include "../interaction/InteractiveView.hpp"
#include "../interaction/Shortcuts.hpp"

#include "../../model/AudioRegions.hpp"
#include "../../model/AudioSource.hpp"
#include "../../model/Song.hpp"

#include <optional>

namespace tessera {

/// The audio editor: what the lower pane shows for an audio clip, in the piano roll's place under the same header
/// (docs/design/AUDIO_EDITING.md §3). Its x axis is the clip's own time. The clip's parts sit along it, each cut, moved,
/// trimmed, faded and crossfaded on its own; the rest of the file shows dim before the first part and after the last.
/// Every edit changes the clip, so linked copies follow and the files never change.
class AudioEditor : public InteractiveView {
  public:
    explicit AudioEditor(EditorContext& ctx) : InteractiveView(ctx) {}

    /// The clip shown, set by the piano roll each frame.
    ClipId clip;

    void step() override;
    EditorContext& context() { return ctx; }
    ~AudioEditor() override;
    /// Delete, Delete closing the gap, Split, Select all, the take of the selected parts, and Show takes. False for any
    /// other action.
    bool handleAction(Action action);

    // ---- For the drags ----

    /// What is under a point.
    enum class Part {
        None,
        Body,      // a part's waveform: moves it, or slips it when it fills the clip
        LeftEdge,  // the edges trim
        RightEdge,
        Gain,      // the strip along a part's top
        FadeIn,    // the top corners set the fades
        FadeOut,
        FadeInCurve, // the dot in the middle of a fade bends it
        FadeOutCurve,
        Seam,       // the handle where two parts meet makes their crossfade longer
        Beat,       // a beat line of the ruler
        Flag,       // beat 1's flag in the ruler
        Warp,       // a warp marker's handle
        MakeUnique, // the button beside "∞ 3 copies"
        TakesButton, // shows or hides the take rows
        TakeRow,     // a take's row: a drag makes that stretch play it
        TakeHear,    // a row's Hear button
    };
    struct Hit {
        Part part = Part::None;
        int beat = 0;  // Beat: which beat line (1 is the first after beat 1)
        int warp = -1; // Warp: which marker
        int take = -1; // Warp, TakeRow, TakeHear: which take
        AudioRegionId region;
        AudioRegionId next; // Seam: the part after the seam
    };
    Hit hitTest(rack::math::Vec pos);

    /// The shown clip, its audio as a drag previews it, and its file.
    const Clip* shownClip();
    AudioContent shownAudio();
    const AudioSource* source();
    /// The clip's parts as a drag previews them; one over the whole clip when it has no edits.
    regions::List parts();
    /// The sample rate and length of the files the clip plays.
    regions::Files files();
    /// Whether the clip is one plain stretch of its file, so its edges and body change the clip itself.
    bool plainClip();
    /// The selected parts.
    regions::Ids& selection() { return selected; }
    /// Source frames per clip tick.
    double framesPerClipTick();
    /// The snap step in ticks at this zoom, 0 when snapping is off.
    Tick gridStep();

    double tickToX(double tick) const { return (tick - left) / ticksPerPx; }
    double xToTick(double x) const { return left + x * ticksPerPx; }
    double pixelTicks() const { return ticksPerPx; }
    /// Keeps the same audio under the same pixels after clip tick 0 moved by `ticks` (a trim of the clip's start).
    void shiftAxis(double ticks) { left -= ticks; }
    /// Keeps the same audio under the same pixels after the clip's ticks were scaled (a new clip tempo).
    void scaleAxis(double factor) {
        left *= factor;
        ticksPerPx *= factor;
    }

    /// What a drag shows before it commits.
    struct Preview {
        std::optional<int64_t> sourceStart;
        std::optional<double> tempo;
        std::optional<std::vector<WarpMarker>> warps;
        int warpsTake = -1; // the take `warps` belongs to
        std::optional<regions::List> regions;
        std::optional<float> gainDb;
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
    void drawPart(NVGcontext* vg, const AudioRegion& r, rack::math::Rect wave, uint32_t color);
    void drawCopies(NVGcontext* vg, rack::math::Rect wave);
    /// The take rows under the comp, newest first.
    void drawTakeRows(NVGcontext* vg);
    /// How many take rows show: the takes of the clip, its own included, or none while they are hidden.
    int takeRowCount();
    /// The height all the rows may use, and of one row.
    float rowsHeight();
    float rowHeight();
    /// Row `index`, 0 the newest take, in view coordinates.
    rack::math::Rect rowRect(int index);
    /// The take row `index` shows (-1 the clip's own).
    int takeOfRow(int index);
    /// Which row `pos` is over, -1 when none.
    int rowAt(rack::math::Vec pos);
    /// The Hear button of row rect `row`.
    rack::math::Rect hearRect(rack::math::Rect row) const;
    /// The right-click menu of a take's row.
    void takeMenu(int take);
    /// Stops Hear, if this editor started it.
    void stopHearing();
    /// The timeline copy of the clip that playback and "play from here" use: the selected one, else the first.
    const ArrangerClip* instance();
    rack::math::Rect waveRect();
    /// The part `id`, or null.
    static const AudioRegion* find(const regions::List& list, AudioRegionId id);
    /// Clip ticks where the file starts and ends, seen from the first and last part: as far as the view can go.
    std::pair<double, double> reach(const regions::List& list);
    /// The right-click menu of part `id`.
    void partMenu(const Hit& hit, Tick at);
    /// Cuts every part `at` falls in.
    void cutAt(Tick at);
    /// Cuts part `id` at each of `ticks`, as one undo step.
    void cutManyAt(const std::vector<Tick>& ticks);
    /// Cuts the part at `at` at every hit of its audio.
    void cutAtHits(AudioRegionId id);
    /// Cuts part `id` every `step` ticks.
    void cutEvery(AudioRegionId id, Tick step);
    /// Removes the parts `ids`. With `close`, later parts move up and the clip gets shorter.
    void removeParts(const regions::Ids& ids, bool close);
    /// Stores `list`, named `name`, as one undo step.
    void store(const regions::List& list, const std::string& name);

    regions::Ids selected;
    rack::math::Rect uniqueButton, takesButton;
    bool takesOpen = false;
    float rowsScroll = 0; // px the rows have scrolled by, when more of them fit than the pane can show

    double left = 0;         // the clip tick at x = 0
    double ticksPerPx = 8;   // zoom
    ClipId fitted;           // the clip the view was last fitted to
    double fittedWidth = 0;
};

} // namespace tessera
