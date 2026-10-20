#pragma once

#include "EditorPane.hpp"

namespace tessera {

class ArrangerView;

/// The top pane: the song, with the clips beside the timeline (the arranger and its launcher panel).
class TopPane : public EditorPane {
  public:
    explicit TopPane(EditorContext& ctx);

    bool handleAction(Action action) override;
    void layout() override;
    void step() override;
    /// Everything stays inside the pane: a module's drawing isn't clipped, so it would cover the rack around it.
    void draw(const DrawArgs& args) override {
        nvgSave(args.vg);
        nvgIntersectScissor(args.vg, 0, 0, box.size.x, box.size.y);
        Widget::draw(args);
        nvgRestore(args.vg);
    }

    ArrangerView* arranger() { return arrangerView; }

  private:
    ArrangerView* arrangerView;
};

} // namespace tessera
