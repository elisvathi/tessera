#include "DrumLaneHeaders.hpp"

#include "../../model/DrumKit.hpp"
#include "../../module/DrumOutModule.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/DrumActions.hpp"
#include "../interaction/Navigation.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/InlineTextField.hpp"
#include "PianoRoll.hpp"

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kChip = 3;
constexpr float kToggle = 16;
constexpr float kMinTextRow = 8; // shorter rows draw no words

} // namespace

DrumLaneHeaders::DrumLaneHeaders(EditorContext& ctx, PianoRollView& owner) : InteractiveView(ctx), owner(owner) {}

void DrumLaneHeaders::step() {
    // A lane lights up while one of its hits sounds, as the keys do.
    sounding.reset();
    TesseraModule* m = ctx.module();
    double now = m && m->playing() && owner.clip() ? owner.source.playhead() : NAN;
    if (std::isfinite(now))
        for (const Note& n : owner.notes())
            if (!n.muted && double(n.start) <= now && now < double(n.end()) && n.pitch >= 0)
                sounding.set(size_t(n.pitch));
    InteractiveView::step();
}

Rect DrumLaneHeaders::toggleRect(float top, float h, int fromRight) const {
    float x = box.size.x - 6 - kToggle * float(fromRight + 1) - 2 * float(fromRight);
    float th = std::min(14.f, h - 2);
    return Rect(x, top + (h - th) / 2, kToggle, th);
}

DrumLaneHeaders::Hit DrumLaneHeaders::hitTest(Vec pos) const {
    Hit hit;
    const Track* t = owner.track();
    if (!t)
        return hit;
    const PitchAxis& pitch = owner.pitch;
    int row = pitch.yToRow(pos.y);
    if (row < 0)
        return hit;
    if (row >= pitch.rowCount()) {
        if (row == pitch.rowCount() && int(t->drumLanes.size()) < kMaxDrumLanes)
            hit.part = Part::AddLane;
        return hit;
    }
    hit.pitch = pitch.pitchAtRow(row);
    hit.lane = laneOfPitch(t->drumLanes, hit.pitch);
    if (hit.lane < 0) {
        hit.part = Part::Other;
        return hit;
    }
    float top = float(pitch.pitchToY(hit.pitch)), h = float(pitch.rowHeight());
    hit.part = toggleRect(top, h, 0).contains(pos)   ? Part::Solo
               : toggleRect(top, h, 1).contains(pos) ? Part::Mute
                                                     : Part::Name;
    return hit;
}

void DrumLaneHeaders::click(Vec pos, int button, Modifiers mods) {
    const Track* t = owner.track();
    if (!t || !ctx.module())
        return;
    Hit hit = hitTest(pos);
    TrackId track = t->id;
    int lane = hit.lane;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (lane >= 0)
            drumActions::appendLaneMenu(rack::createMenu(), ctx, track, lane, [this, lane] { startRename(lane); });
        else
            drumActions::appendKitMenu(rack::createMenu(), ctx, track);
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    switch (hit.part) {
    case Part::Name:
    case Part::Other:
        if (mods.ctrl) {
            // Ctrl/Cmd+click: every hit of the lane, as Ctrl/Cmd+click on a key selects its notes.
            edits::IdSet same;
            for (const Note& n : owner.committedNotes())
                if (n.pitch == hit.pitch)
                    same.insert(n.id);
            owner.select(same);
        } else if (lane >= 0) {
            drumActions::audition(ctx, track, lane);
        }
        owner.focusPitch = hit.pitch;
        break;
    case Part::Mute: {
        bool mute = !t->drumLanes[size_t(lane)].mute;
        drumActions::editLane(
            ctx, track, lane, [mute](DrumLane& d) { d.mute = mute; }, mute ? "Mute lane" : "Unmute lane");
        break;
    }
    case Part::Solo: {
        bool solo = !t->drumLanes[size_t(lane)].solo;
        drumActions::editLane(
            ctx, track, lane, [solo](DrumLane& d) { d.solo = solo; }, solo ? "Solo lane" : "Unsolo lane");
        break;
    }
    case Part::AddLane:
        drumActions::addLane(ctx, track);
        break;
    default:
        break;
    }
}

void DrumLaneHeaders::doubleClick(Vec pos, Modifiers mods) {
    Hit hit = hitTest(pos);
    if (hit.part == Part::Name)
        startRename(hit.lane);
}

void DrumLaneHeaders::startRename(int lane) {
    const Track* t = owner.track();
    if (!t || lane < 0 || lane >= int(t->drumLanes.size()))
        return;
    const DrumLane& l = t->drumLanes[size_t(lane)];
    float top = float(owner.pitch.pitchToY(l.pitch)), h = float(owner.pitch.rowHeight());
    auto* field = new InlineTextField(l.name, [c = &ctx, track = t->id, lane](const std::string& name) {
        drumActions::editLane(*c, track, lane, [name](DrumLane& d) { d.name = name; }, "Rename lane");
    });
    float fieldH = std::max(14.f, h - 4);
    field->box = Rect(kChip + 6, top + (h - fieldH) / 2, box.size.x - 2 * kToggle - 16 - kChip, fieldH);
    addChild(field);
    field->focus();
}

void DrumLaneHeaders::wheel(Vec pos, Vec delta, Modifiers mods) {
    ctx.touchNavigation();
    PitchAxis& pitch = owner.pitch;
    if (mods.alt || mods.ctrl)
        pitch.zoomAt(pos.y, wheelZoomFactor(delta.y));
    else
        pitch.scrollByPx(-delta.y);
}

CursorShape DrumLaneHeaders::hoverCursor(Vec pos, Modifiers mods) {
    hover = hitTest(pos);
    switch (hover.part) {
    case Part::Name:
    case Part::Mute:
    case Part::Solo:
    case Part::AddLane:
        return CursorShape::Hand;
    default:
        return CursorShape::Arrow;
    }
}

void DrumLaneHeaders::onLeave(const LeaveEvent& e) {
    hover = Hit();
    InteractiveView::onLeave(e);
}

void DrumLaneHeaders::drawLane(NVGcontext* vg, const DrumLane& l, int index, float top, float h, bool anySolo) {
    const theme::Palette& c = theme::colors();
    const Track& track = *owner.track();
    float w = box.size.x - 1;
    NVGcolor color = theme::track(l.color ? l.color : track.color);
    bool audible = anySolo ? l.solo : !l.mute;

    NVGcolor fill = index % 2 ? c.bg1 : nvgLerpRGBA(c.bg1, c.bg2, 0.4f);
    if (l.pitch == owner.focusPitch)
        fill = nvgLerpRGBA(fill, c.select, 0.12f);
    if (l.pitch >= 0 && sounding.test(size_t(l.pitch)))
        fill = nvgLerpRGBA(fill, color, 0.35f);
    nvgBeginPath(vg);
    nvgRect(vg, 0, top, w, h);
    nvgFillColor(vg, fill);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 4, top + std::min(4.f, h / 4), kChip, std::max(1.f, h - 2 * std::min(4.f, h / 4)));
    nvgFillColor(vg, color);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, top + h - 1, w, 1);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);
    if (h < kMinTextRow)
        return;

    float text = std::min(theme::metrics::kTextUi, h - 2);
    bool nameHover = hover.part == Part::Name && hover.lane == index;
    draw::TextStyle name{theme::fonts::uiStrong(), text,
                         !audible    ? c.text2
                         : nameHover ? c.text0
                                     : c.text1,
                         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::string label = l.name;
    const Clip* clip = owner.clip();
    if (clip && hasOwnLoop(l, *clip))
        label += "  " + std::to_string(laneRegion(l, *clip).steps(l.stepLength));
    Rect solo = toggleRect(top, h, 0), mute = toggleRect(top, h, 1);
    // The Drum Out jack the lane plays on; outputs past the 8 jacks are only on the poly outputs.
    std::string output =
        "→" + std::to_string(l.outputIndex + 1) + (l.outputIndex < DrumOutModule::kTrigJacks ? "" : " poly");
    draw::TextStyle out{theme::fonts::mono(), std::min(theme::metrics::kTextRuler, h - 2), c.text2,
                        NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    float outRight = mute.pos.x - 4;
    draw::text(vg, out, outRight, top + h / 2, output);
    float nameRight = outRight - draw::textWidth(vg, out, output) - 6;
    draw::text(vg, name, kChip + 10, top + h / 2, draw::ellipsize(vg, name, label, nameRight - kChip - 10));
    auto toggle = [&](Rect r, const char* letter, bool active, NVGcolor on) {
        draw::roundedRect(vg, r, theme::metrics::kRadiusControl, active ? on : c.bg2);
        draw::TextStyle s{theme::fonts::uiStrong(), std::min(theme::metrics::kTextRuler, r.size.y),
                          active ? c.ink : c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, s, r.getCenter().x, r.getCenter().y, letter);
    };
    toggle(mute, "M", l.mute, c.queue);
    toggle(solo, "S", l.solo, c.play);
}

void DrumLaneHeaders::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    const Track* t = owner.track();
    if (!t)
        return;
    nvgSave(vg);
    nvgIntersectScissor(vg, 0, 0, box.size.x, box.size.y);
    const PitchAxis& pitch = owner.pitch;
    float h = float(pitch.rowHeight());
    bool anySolo = std::any_of(t->drumLanes.begin(), t->drumLanes.end(), [](const DrumLane& d) { return d.solo; });
    int first = std::max(0, pitch.yToRow(0)), last = std::min(pitch.rowCount() - 1, pitch.yToRow(box.size.y));
    for (int r = first; r <= last; r++) {
        int p = pitch.pitchAtRow(r);
        float top = float(pitch.pitchToY(p));
        int lane = laneOfPitch(t->drumLanes, p);
        if (lane >= 0) {
            drawLane(vg, t->drumLanes[size_t(lane)], lane, top, h, anySolo);
            continue;
        }
        // A note on no lane: it doesn't play, but it keeps a row so it can be seen and moved onto a lane.
        nvgBeginPath(vg);
        nvgRect(vg, 0, top, box.size.x - 1, h);
        nvgFillColor(vg, c.bg0);
        nvgFill(vg);
        if (h >= kMinTextRow) {
            draw::TextStyle other{theme::fonts::ui(), std::min(theme::metrics::kTextRuler, h - 2), c.text2,
                                  NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
            draw::text(vg, other, kChip + 10, top + h / 2, pitchName(p) + "  (no lane)");
        }
    }
    float addTop = float((pitch.rowCount() - pitch.top()) * pitch.rowHeight());
    if (int(t->drumLanes.size()) < kMaxDrumLanes && addTop < box.size.y && h >= kMinTextRow) {
        draw::TextStyle add{theme::fonts::ui(), std::min(theme::metrics::kTextUi, h - 2),
                            hover.part == Part::AddLane ? c.text0 : c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, add, 10, addTop + h / 2, "+ Add lane");
    }
    nvgRestore(vg);
    nvgBeginPath(vg);
    nvgRect(vg, box.size.x - 1, 0, 1, box.size.y);
    nvgFillColor(vg, c.line2);
    nvgFill(vg);
}

} // namespace tessera
