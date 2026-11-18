#include "PaneStack.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/DeviceActions.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/Splitter.hpp"
#include "../widgets/ToolButton.hpp"
#include "EditorPane.hpp"

namespace tessera {

/// While one pane is maximised, the other folds to this bar with its switch (docs/DECISIONS.md §190), so it is a
/// click away: the lower pane's Notes · Devices · Mixer at the bottom, the song pane's Clips · Timeline at the top.
/// A click brings both panes back showing that view; the arrow at the right restores them as they were.
class FoldedBar : public rack::widget::OpaqueWidget {
  public:
    Pane folded = Pane::Detail;

    explicit FoldedBar(EditorContext& ctx) : ctx(ctx) {
        EditorContext* c = &ctx;
        auto show = [c](Pane pane, std::function<void(EditorContext&)> then) {
            return [c, pane, then] {
                c->state().maximized.reset();
                c->state().focus = pane;
                then(*c);
            };
        };
        notes = button("Notes", withShortcut("Bring the lower pane back with the notes", Action::ShowNotes),
                       show(Pane::Detail, [](EditorContext& e) {
                           e.state().mixerOpen = false;
                           if (e.devicesFillPane && e.module())
                               deviceActions::setStripOpen(e, false);
                       }));
        devices = button("Devices", withShortcut("Bring the lower pane back with the devices", Action::ToggleDevices),
                         show(Pane::Detail, [](EditorContext& e) {
                             e.state().mixerOpen = false;
                             deviceActions::setStripOpen(e, true);
                         }));
        mixer = button("Mixer", withShortcut("Bring the lower pane back with the mixer", Action::ToggleMixer),
                       show(Pane::Detail, [](EditorContext& e) { e.state().mixerOpen = true; }));
        clips = button("Clips", withShortcut("Bring the song pane back with the clips across it", Action::WidenClips),
                       show(Pane::Top, [](EditorContext& e) { e.state().clipsWide = true; }));
        timeline = button("Timeline", "Bring the song pane back with the timeline beside the clips",
                          show(Pane::Top, [](EditorContext& e) { e.state().clipsWide = false; }));
        ToolButton::Options r;
        r.icon = Icon::Restore;
        r.tooltip = withShortcut("Restore both panes", Action::MaximizePane);
        r.action = [c] { c->state().maximized.reset(); };
        restore = new ToolButton(r);
        addChild(restore);
    }

    void step() override {
        TesseraModule* m = ctx.module();
        const Track* t = m ? m->song().findTrack(ctx.state().selectedTrack) : nullptr;
        bool audio = t && t->type == TrackType::Audio;
        bool lower = folded == Pane::Detail;
        notes->setText(audio ? "Audio" : "Notes");
        notes->visible = lower;
        devices->visible = lower && !audio;
        mixer->visible = lower && m && m->makesSound();
        clips->visible = timeline->visible = !lower;
        float h = box.size.y - 6;
        float x = 22;
        for (ToolButton* b : {notes, devices, mixer, clips, timeline}) {
            if (!b->visible)
                continue;
            float w = draw::textWidth(APP->window->vg, label, b->getText()) + 16;
            b->box = rack::math::Rect(x, 3, w, h);
            x += w + 2;
        }
        restore->box = rack::math::Rect(box.size.x - h - 4, 3, h, h);
        OpaqueWidget::step();
    }

    void draw(const DrawArgs& args) override {
        const theme::Palette& c = theme::colors();
        NVGcontext* vg = args.vg;
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(vg, c.bg1);
        nvgFill(vg);
        // The line on the side the panes meet, and an arrow toward where the folded pane opens.
        nvgBeginPath(vg);
        nvgRect(vg, 0, folded == Pane::Detail ? 0 : box.size.y - 1, box.size.x, 1);
        nvgFillColor(vg, c.line1);
        nvgFill(vg);
        float cx = 11, cy = box.size.y / 2, s = 3.5f;
        float dir = folded == Pane::Detail ? -1 : 1;
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - s, cy - dir * s * 0.5f);
        nvgLineTo(vg, cx, cy + dir * s * 0.5f);
        nvgLineTo(vg, cx + s, cy - dir * s * 0.5f);
        nvgStrokeColor(vg, c.text2);
        nvgStrokeWidth(vg, 1.3f);
        nvgStroke(vg);
        OpaqueWidget::draw(args);
    }

  private:
    EditorContext& ctx;
    ToolButton *notes, *devices, *mixer, *clips, *timeline, *restore;
    draw::TextStyle label{theme::fonts::uiStrong(), theme::metrics::kTextUi, theme::colors().text0,
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};

    /// A text button of the bar.
    ToolButton* button(const char* text, std::string tooltip, std::function<void()> action) {
        ToolButton::Options o;
        o.text = text;
        o.tooltip = std::move(tooltip);
        o.action = std::move(action);
        o.filled = true;
        auto* b = new ToolButton(o);
        addChild(b);
        return b;
    }
};

PaneStack::PaneStack(EditorContext& ctx, EditorPane* top, rack::widget::Widget* bottom, float minPaneHeight,
                     std::function<void()> onSplitterDoubleClick)
    : ctx(ctx), top(top), bottom(bottom), minPaneHeight(minPaneHeight) {
    splitter = new Splitter(
        Splitter::Orientation::Horizontal,
        [this](float y) {
            if (!this->ctx.module())
                return;
            float panes = box.size.y - theme::metrics::kSplitter;
            this->ctx.state().topFraction = rack::math::clamp(y / panes, 0.f, 1.f);
        },
        std::move(onSplitterDoubleClick));
    addChild(top);
    addChild(bottom);
    addChild(splitter);
    bar = new FoldedBar(ctx);
    bar->visible = false;
    addChild(bar);
}

bool PaneStack::splitterActive() const { return splitter->visible && splitter->active(); }

void PaneStack::layout() {
    using rack::math::Rect;
    rack::math::Vec size = box.size;
    // A hidden pane still steps. Left at Rack's default infinite size, its views would fit themselves to it and
    // keep that size for a frame after it shows again: the ruler then drew lines without end (DECISIONS §181).
    float folded = std::min(theme::metrics::kFoldedPaneBar, size.y / 2);
    bar->visible = ctx.state().maximized.has_value();
    if (ctx.state().maximized == Pane::Top) {
        top->box = Rect(0, 0, size.x, size.y - folded);
        bottom->box = Rect(0, 0, 0, 0);
        top->visible = true;
        bottom->visible = splitter->visible = false;
        bar->folded = Pane::Detail;
        bar->box = Rect(0, size.y - folded, size.x, folded);
    } else if (ctx.state().maximized == Pane::Detail) {
        top->box = Rect(0, 0, 0, 0);
        bottom->box = Rect(0, folded, size.x, size.y - folded);
        bottom->visible = true;
        top->visible = splitter->visible = false;
        bar->folded = Pane::Top;
        bar->box = Rect(0, 0, size.x, folded);
    } else {
        float panes = size.y - theme::metrics::kSplitter;
        float topHeight = rack::math::clamp(ctx.state().topFraction * panes, minPaneHeight, panes - minPaneHeight);
        top->box = Rect(0, 0, size.x, topHeight);
        splitter->box = Rect(0, topHeight, size.x, theme::metrics::kSplitter);
        bottom->box = Rect(0, topHeight + theme::metrics::kSplitter, size.x, panes - topHeight);
        top->visible = bottom->visible = splitter->visible = true;
    }
    top->layout();
}

} // namespace tessera
