#include "ClipLaneView.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "PianoRoll.hpp"

namespace tessera {

using rack::math::Vec;

std::optional<LaneFrame> ClipLaneView::frame() {
    TesseraModule* m = ctx.module();
    const Clip* clip = owner.clip();
    const Track* track = owner.track();
    if (!m || !clip || !track || !findLane(m->song(), LaneOwner::ofClip(clip->id), lane))
        return std::nullopt;
    const AutomationEnvelope* l = findLane(m->song(), LaneOwner::ofClip(clip->id), lane);
    LaneFrame f;
    f.owner = LaneOwner::ofClip(clip->id);
    f.lane = lane;
    f.rect = rack::math::Rect(0, 0, box.size.x, box.size.y);
    f.time = &owner.viewport.time;
    f.snap = owner.snapStep();
    f.playhead = m->playing() ? owner.source.playhead() : NAN;
    f.color = theme::track(l->color ? l->color : track->color);
    return f;
}

bool ClipLaneView::handleAction(Action action) {
    double pasteAt = owner.source.playhead();
    if (!std::isfinite(pasteAt))
        pasteAt = owner.viewport.time.left();
    return editor.handleAction(action, owner.snapStep(), pasteAt);
}

std::unique_ptr<DragGesture> ClipLaneView::startGesture(Vec pos, int button, Modifiers mods) {
    owner.automationFocus = true;
    std::optional<LaneFrame> f = frame();
    return f ? editor.startGesture(*f, pos, button, mods) : nullptr;
}

void ClipLaneView::click(Vec pos, int button, Modifiers mods) {
    owner.automationFocus = true;
    if (std::optional<LaneFrame> f = frame())
        editor.click(*f, pos, button, mods);
}

void ClipLaneView::doubleClick(Vec pos, Modifiers mods) {
    if (std::optional<LaneFrame> f = frame())
        editor.doubleClick(*f, pos, mods);
}

CursorShape ClipLaneView::hoverCursor(Vec pos, Modifiers mods) {
    std::optional<LaneFrame> f = frame();
    return f ? editor.hover(*f, pos, mods) : CursorShape::Arrow;
}

void ClipLaneView::onLeave(const LeaveEvent& e) {
    editor.leave();
    InteractiveView::onLeave(e);
}

void ClipLaneView::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, theme::recessed(c, c.bg0, 0.2f));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, 1);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);
    // The clip loop, so it is clear where the lane repeats.
    const TimeAxis& t = owner.viewport.time;
    float x0 = float(t.tickToX(double(owner.source.loopStart())));
    float x1 = float(t.tickToX(double(owner.source.loopEnd())));
    nvgBeginPath(vg);
    if (x0 > 0)
        nvgRect(vg, 0, 0, x0, box.size.y);
    if (x1 < box.size.x)
        nvgRect(vg, std::max(0.f, x1), 0, box.size.x - std::max(0.f, x1), box.size.y);
    nvgFillColor(vg, nvgTransRGBAf(c.bg0, 0.5f));
    nvgFill(vg);
    if (std::optional<LaneFrame> f = frame())
        editor.draw(vg, *f);
}

} // namespace tessera
