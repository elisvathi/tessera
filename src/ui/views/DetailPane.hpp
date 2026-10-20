#pragma once

#include "EditorPane.hpp"

#include <functional>

namespace tessera {

class PianoRollView;
class DrumEditorView;
class DeviceStrip;
class ModulatorPopover;
class NoteTarget;

/// The detail pane: the drum step grid for drum tracks, the piano roll otherwise. G switches a drum track to
/// the piano roll and back (a per-surface view setting).
class DetailPane : public EditorPane {
  public:
    explicit DetailPane(EditorContext& ctx);
    /// Keeps `px` free at the right end of the notes header, for a button the host lays over it.
    void reserveHeader(float px);

    bool handleAction(Action action) override;
    void layout() override;
    void step() override;
    /// Both views draw their own focus ring. Everything stays inside the pane: a module's drawing isn't clipped, so
    /// anything taller than a short pane (the in-panel editor) would cover the rack below.
    void draw(const DrawArgs& args) override {
        nvgSave(args.vg);
        nvgIntersectScissor(args.vg, 0, 0, box.size.x, box.size.y);
        Widget::draw(args);
        nvgRestore(args.vg);
    }

    PianoRollView* pianoRoll() { return roll; }
    /// The note editor showing: the drum step grid or the piano roll.
    NoteTarget* noteTarget();
    void onButton(const ButtonEvent& e) override;

  private:
    bool drumTrackSelected();
    EditorPane* current();

    PianoRollView* roll;
    DrumEditorView* drums;
    DeviceStrip* strip;
    ModulatorPopover* popover;
    bool stripFocus = false; // the last click was in the device strip
};

} // namespace tessera
