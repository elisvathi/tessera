#pragma once

#include "EditorPane.hpp"

#include <functional>

namespace tessera {

class PianoRollView;
class DeviceStrip;
class ModulatorPopover;
class NoteTarget;

/// The detail pane: the piano roll, which is also the drum editor on drum tracks, with the device strip and the
/// modulator popover over it.
class DetailPane : public EditorPane {
  public:
    explicit DetailPane(EditorContext& ctx);
    /// Keeps `px` free at the right end of the notes header, for a button the host lays over it.
    void reserveHeader(float px);

    bool handleAction(Action action) override;
    void layout() override;
    void step() override;
    /// The piano roll draws its own focus ring. Everything stays inside the pane: a module's drawing isn't clipped, so
    /// anything taller than a short pane (the in-panel editor) would cover the rack below.
    void draw(const DrawArgs& args) override {
        nvgSave(args.vg);
        nvgIntersectScissor(args.vg, 0, 0, box.size.x, box.size.y);
        Widget::draw(args);
        nvgRestore(args.vg);
    }

    PianoRollView* pianoRoll() { return roll; }
    /// The note editor showing.
    NoteTarget* noteTarget();
    void onButton(const ButtonEvent& e) override;

  private:
    PianoRollView* roll;
    DeviceStrip* strip;
    ModulatorPopover* popover;
    bool stripFocus = false; // the last click was in the device strip
};

} // namespace tessera
