#include "PaneStack.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../widgets/Splitter.hpp"
#include "EditorPane.hpp"
#include "MixerPane.hpp"

namespace tessera {

PaneStack::PaneStack(EditorContext& ctx, EditorPane* top, rack::widget::Widget* bottom, float minPaneHeight,
                     std::function<void()> onSplitterDoubleClick)
    : ctx(ctx), top(top), bottom(bottom), minPaneHeight(minPaneHeight) {
    splitter = new Splitter(
        Splitter::Orientation::Horizontal,
        [this](float y) {
            if (!this->ctx.module())
                return;
            float panes = box.size.y - theme::metrics::kSplitter;
            this->ctx.view().topFraction = rack::math::clamp(y / panes, 0.f, 1.f);
        },
        std::move(onSplitterDoubleClick));
    addChild(top);
    addChild(bottom);
    addChild(splitter);
}

void PaneStack::setMixer(MixerPane* m) {
    mixer = m;
    addChild(mixer);
}

bool PaneStack::splitterActive() const { return splitter->visible && splitter->active(); }

void PaneStack::layout() {
    using rack::math::Rect;
    ViewState& view = ctx.view();
    rack::math::Vec size = box.size;
    // The mixer takes the bottom: its tab, the height it was opened to, or everything when maximised. A song with
    // nothing that makes sound shows no mixer at all, so Tessera looks as it did before audio (DECISIONS §106).
    if (mixer && !mixer->hasSound())
        mixer->visible = false;
    if (mixer && mixer->hasSound()) {
        bool sound = true;
        float tab = mixer->tabHeight();
        bool max = sound && view.mixerMax;
        float h = max ? size.y
                      : sound && view.mixerOpen
                            ? rack::math::clamp(view.mixerHeight, tab + 40, size.y - 2 * minPaneHeight - theme::metrics::kSplitter)
                            : tab;
        h = std::max(tab, std::min(h, size.y));
        mixer->box = Rect(0, size.y - h, size.x, h);
        mixer->visible = true;
        if (max) {
            top->visible = bottom->visible = splitter->visible = false;
            return;
        }
        size.y -= h;
    }

    if (view.maximized == Pane::Top) {
        top->box = Rect(0, 0, size.x, size.y);
        top->visible = true;
        bottom->visible = splitter->visible = false;
    } else if (view.maximized == Pane::Detail) {
        bottom->box = Rect(0, 0, size.x, size.y);
        bottom->visible = true;
        top->visible = splitter->visible = false;
    } else {
        float panes = size.y - theme::metrics::kSplitter;
        float topHeight = rack::math::clamp(view.topFraction * panes, minPaneHeight, panes - minPaneHeight);
        top->box = Rect(0, 0, size.x, topHeight);
        splitter->box = Rect(0, topHeight, size.x, theme::metrics::kSplitter);
        bottom->box = Rect(0, topHeight + theme::metrics::kSplitter, size.x, panes - topHeight);
        top->visible = bottom->visible = splitter->visible = true;
    }
    top->layout();
}

} // namespace tessera
