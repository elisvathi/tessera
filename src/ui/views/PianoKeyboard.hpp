#pragma once

#include "../../model/Scale.hpp"
#include "../interaction/InteractiveView.hpp"

#include <bitset>
#include <map>
#include <string>

namespace tessera {

class CachedLayer;
class PianoRollView;
class NoteGrid;

/// The keys beside the note grid. Click a key to hear it on the track's outputs, drag for a glissando,
/// Ctrl/Cmd+click to select every note of that pitch. Scrolls and zooms with the grid.
///
/// The keys themselves are drawn once into a cached layer; each frame only redraws the few keys that are lit.
class PianoKeyboard : public InteractiveView {
  public:
    PianoKeyboard(EditorContext& ctx, PianoRollView& owner, NoteGrid& grid);

    /// The key being played, or -1 (drawn lit).
    int playing = -1;

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void onLeave(const LeaveEvent& e) override;
    void step() override;
    void drawForeground(const DrawArgs& args) override;

  private:
    /// What drawing a key depends on besides its pitch, worked out once per frame in step().
    struct Look {
        NVGcolor lit;
        bool rootMark = false;
        Scale key;
        // Folded rows aren't neighbouring keys, and drum lanes are names: a piano shape would lie, so each row is
        // then a named key of its own.
        bool named = false;
        std::map<int, std::string> laneNames;
    };

    /// The pitch of the key at height `y`.
    int pitchAt(float y);
    /// The rows on screen, top to bottom, and the lowest and highest pitch among them.
    void visibleRows(int& first, int& last, int& lo, int& hi) const;
    /// Paints every key unlit: the cached layer's contents.
    void paintKeys(NVGcontext* vg);
    /// Paints one white key with its name and root mark, lit while it sounds, tinted while hovered.
    void paintWhiteKey(NVGcontext* vg, int p, bool on, bool hover);
    /// `shadow` off repaints a key over its own cached image, which already has the shadow under it.
    void paintBlackKey(NVGcontext* vg, int p, bool on, bool hover, bool shadow);
    /// Paints one row as a named key of its own (folded rows, drum lanes).
    void paintNamedKey(NVGcontext* vg, int p, bool on, bool hover);
    /// Marks the key's root on the front edge of the key at `y`.
    void drawRootMark(NVGcontext* vg, float y, float row);
    /// The top and bottom edge of a white key, which reaches halfway under its black neighbours.
    std::pair<float, float> whiteKeySpan(int p) const;

    PianoRollView& owner;
    NoteGrid& grid;
    CachedLayer* layer;
    Look look;
    std::bitset<128> sounding;
    int hoverPitch = -1;
};

} // namespace tessera
