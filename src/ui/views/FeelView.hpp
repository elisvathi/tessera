#pragma once

#include "../../model/Device.hpp"
#include "../../model/Playheads.hpp"
#include "../interaction/InteractiveView.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tessera {

class PianoRollView;
class TesseraModule;

/// Draws a Feel's shape in `r` (how far through a cycle against time, rising left to right): in the Feel view, its
/// shape buttons and the Playheads card's Feel cell.
void drawFeelShape(NVGcontext* vg, rack::math::Rect r, playheads::Bend bend, float amount, NVGcolor color, float width);

/// The piano roll's lower pane as the shown playhead's Feel: how it speeds up and slows down inside a repeating cycle
/// (its bend). Across, time since the clip started, lined up with the notes above; up, how far through the cycle the
/// playhead is. The straight dashed line is an even playhead; the coloured line is the Feel. A header holds the
/// shapes (Straight, Ease in, Ease out, Ease in-out, Swing, Stairs) and the cycle's length; the ruler under it shows
/// each cycle as a bar whose right grip drags the length. Dragging the line up or down bends it more or less, as
/// one undo step.
class FeelView : public InteractiveView {
  public:
    FeelView(EditorContext& ctx, PianoRollView& owner) : InteractiveView(ctx), owner(owner) {}

    /// Whether there is a playhead to show (the lower pane shows this view only then).
    bool hasFeel();
    /// The name the lower pane's button shows ("Melody · Feel").
    std::string title();
    /// The Tessera the editor shows, for gestures that commit edits.
    TesseraModule* module() { return ctx.module(); }

    /// What a running drag shows before it is committed.
    struct Shown {
        playheads::Bend bend = playheads::Bend::Straight;
        float amount = 0; // 0..1
        int cycle = 0;    // index into playheads::cycleNames()
    };
    std::optional<Shown> preview;

    /// The column on the left, under the lower pane's button (the piano keys' width). The header, ruler and
    /// drawing start right of it.
    float left = 0;
    static constexpr float kHead = 24; // the header row: shapes, cycle, close
    static constexpr float kBar = 18;  // the ruler with the cycle bars

    /// The shown playhead, or nullptr.
    const Playhead* playhead();
    /// Its Feel as stored, or as the running drag has it.
    Shown shown();
    /// Commits a Feel to the shown playhead, as one undo step named `name`.
    void commit(const Shown& feel, const std::string& name);
    /// The cycle's length in the playhead's own ticks for choice `index`.
    double cycleTicks(int index);
    /// How fast the playhead moves against the song (its Rate).
    double rate();
    /// Where `elapsed` song ticks since the clip started are across the pane, and back.
    float xOf(double elapsed);
    double elapsedAt(float x);

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  private:
    /// One clickable part of the header row.
    struct HeaderPart {
        enum class Kind { Shape, Cycle, Close } kind;
        rack::math::Rect rect;
        int shape = -1;
    };
    std::vector<HeaderPart> headerParts();
    /// The drawing area under the header and the ruler.
    rack::math::Rect body() const;
    /// Where the clip's span starts: elapsed time 0 lines up with it.
    double spanStart();
    /// The song ticks one cycle lasts, and when a restart begins the cycles again (0 = never).
    double cycleSongTicks();
    double restartEvery();
    /// The cycle bar whose right grip is under `pos` (counted from the clip's start), or -1.
    int gripAt(rack::math::Vec pos);
    /// Picks shape `index` (keeping the amount, or a middle one when it was straight).
    void pickShape(int index);
    /// The list of cycle lengths, 1/16 to 4 bars.
    void openCycleMenu();
    void drawHeader(NVGcontext* vg, NVGcolor color, const Shown& feel);
    void drawRuler(NVGcontext* vg, NVGcolor color);
    void drawLines(NVGcontext* vg, NVGcolor color, const Shown& feel);

    PianoRollView& owner;
};

} // namespace tessera
