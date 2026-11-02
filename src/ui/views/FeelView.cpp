#include "FeelView.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/DeviceLookup.hpp"
#include "../../model/DeviceTypes.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/DeviceActions.hpp"
#include "../widgets/Draw.hpp"
#include "PianoRoll.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;
using playheads::Bend;

namespace {

constexpr float kPad = 6;
constexpr float kGripHit = 6;
constexpr const char* kShapeNames[] = {"Straight", "Ease in", "Ease out", "Ease in-out", "Swing", "Stairs"};
constexpr int kShapeCount = int(std::size(kShapeNames));

/// A dashed straight line from `a` to `b`: `on` px drawn, then `off` px left out.
void dashed(NVGcontext* vg, Vec a, Vec b, NVGcolor color, float width, float on, float off) {
    float length = b.minus(a).norm();
    if (length <= 0)
        return;
    Vec step = b.minus(a).div(length);
    nvgBeginPath(vg);
    for (float d = 0; d < length; d += on + off) {
        nvgMoveTo(vg, a.x + step.x * d, a.y + step.y * d);
        float e = std::min(length, d + on);
        nvgLineTo(vg, a.x + step.x * e, a.y + step.y * e);
    }
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

/// The Feel as a number from -1 to 1 for dragging: Ease in below the straight line (negative), Ease out above it.
/// Other shapes only grow, so they count from 0 up.
float signedAmount(const FeelView::Shown& f) {
    if (f.bend == Bend::Straight)
        return 0;
    return f.bend == Bend::EaseIn ? -f.amount : f.amount;
}

/// What a Feel reads as in the readout and the header: "Ease in 60%", or "Straight".
std::string describe(const FeelView::Shown& f) {
    if (f.bend == Bend::Straight || f.amount <= 0)
        return "Straight: even";
    char text[48];
    snprintf(text, sizeof(text), "%s %.0f%%", kShapeNames[int(f.bend)], double(f.amount) * 100);
    return text;
}

/// Dragging the line up or down: bends it more or less, one undo step on release. From straight, up eases out and
/// down eases in; an ease dragged across the straight line becomes the other one.
class BendDrag : public DragGesture {
  public:
    BendDrag(FeelView& view, FeelView::Shown start, float startY, float height)
        : view(view), start(start), feel(start), startY(startY), height(std::max(20.f, height)) {}
    void update(Vec pos, Modifiers mods) override {
        float delta = (startY - pos.y) / height;
        if (start.bend == Bend::Straight || start.bend == Bend::EaseIn || start.bend == Bend::EaseOut) {
            float v = std::clamp(signedAmount(start) + delta, -1.f, 1.f);
            if (std::fabs(v) < 0.02f) {
                feel.bend = Bend::Straight;
                feel.amount = 0;
            } else {
                feel.bend = v < 0 ? Bend::EaseIn : Bend::EaseOut;
                feel.amount = std::fabs(v);
            }
        } else {
            feel.amount = std::clamp(start.amount + delta, 0.f, 1.f);
        }
        if (!mods.alt) // whole percents unless held loose
            feel.amount = std::round(feel.amount * 100) / 100;
        view.preview = feel;
        moved = true;
    }
    void commit() override {
        view.preview.reset();
        if (moved)
            view.commit(feel, "Bend " + std::string(kShapeNames[int(feel.bend)]));
    }
    void cancel() override { view.preview.reset(); }
    std::string readout() const override { return describe(feel); }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    FeelView& view;
    FeelView::Shown start, feel;
    float startY, height;
    bool moved = false;
};

/// Dragging the right grip of cycle bar `index`: the cycle's length follows, snapped to the lengths it can have.
class CycleDrag : public DragGesture {
  public:
    CycleDrag(FeelView& view, FeelView::Shown start, int index) : view(view), feel(start), index(index) {}
    void update(Vec pos, Modifiers) override {
        double own = std::max(1.0, view.elapsedAt(pos.x) * view.rate() / double(index + 1));
        int best = feel.cycle;
        double bestGap = 1e9;
        for (int k = 0; k < int(playheads::cycleNames().size()); k++) {
            double gap = std::fabs(std::log(view.cycleTicks(k) / own));
            if (gap < bestGap) {
                bestGap = gap;
                best = k;
            }
        }
        moved = moved || best != feel.cycle;
        feel.cycle = best;
        view.preview = feel;
    }
    void commit() override {
        view.preview.reset();
        if (moved)
            view.commit(feel, "Set feel cycle");
    }
    void cancel() override { view.preview.reset(); }
    std::string readout() const override { return playheads::cycleNames()[size_t(feel.cycle)] + " cycle"; }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    FeelView& view;
    FeelView::Shown feel;
    int index;
    bool moved = false;
};

} // namespace

const Playhead* FeelView::playhead() {
    const Track* t = owner.track();
    const Device* d = t ? activePlayheads(*t) : nullptr;
    if (!d || d->playheads.empty())
        return nullptr;
    for (const Playhead& p : d->playheads)
        if (p.id.value == ctx.selectedPlayhead)
            return &p;
    return &d->playheads.front();
}

bool FeelView::hasFeel() { return playhead() != nullptr; }

std::string FeelView::title() {
    const Playhead* p = playhead();
    return p ? p->name + " · Feel" : "Feel";
}

FeelView::Shown FeelView::shown() {
    if (preview)
        return *preview;
    Shown f;
    const Playhead* p = playhead();
    if (!p)
        return f;
    const auto& info = devices::playheadInfo().params;
    using namespace playheads::knob;
    f.bend = Bend(std::clamp(int(devices::display(info[kBendShape], p->params[kBendShape])), 0, kShapeCount - 1));
    f.amount = devices::display(info[kBendAmount], p->params[kBendAmount]) / 100;
    f.cycle = int(devices::display(info[kBendCycle], p->params[kBendCycle]));
    return f;
}

void FeelView::commit(const Shown& feel, const std::string& name) {
    const Playhead* p = playhead();
    if (!p)
        return;
    preview.reset();
    Shown was = shown();
    using namespace playheads::knob;
    std::vector<std::pair<int, float>> values;
    if (feel.bend != was.bend)
        values.push_back({kBendShape, float(int(feel.bend))});
    if (std::fabs(feel.amount - was.amount) > 1e-4f)
        values.push_back({kBendAmount, feel.amount * 100});
    if (feel.cycle != was.cycle)
        values.push_back({kBendCycle, float(feel.cycle)});
    if (!values.empty())
        deviceActions::setPlayheadValues(ctx, p->id, values, name);
}

double FeelView::cycleTicks(int index) { return playheads::cycleTicks(index, owner.timeSig()); }

double FeelView::rate() {
    const Playhead* p = playhead();
    if (!p)
        return 1;
    const devices::ParamInfo& info = devices::playheadInfo().params[playheads::knob::kRate];
    return std::max(1e-3, playheads::playheadRate(int(devices::display(info, p->params[playheads::knob::kRate]))));
}

double FeelView::spanStart() {
    const Clip* c = owner.clip();
    const Track* t = owner.track();
    if (!c || !t)
        return 0;
    return double(clipSpan(*c, t->type == TrackType::Drum ? &t->drumLanes : nullptr).start);
}

double FeelView::cycleSongTicks() { return cycleTicks(shown().cycle) / rate(); }

double FeelView::restartEvery() {
    const Track* t = owner.track();
    const Device* d = t ? activePlayheads(*t) : nullptr;
    const Playhead* p = playhead();
    if (!d || !p)
        return 0;
    playheads::Motion m = playheads::motionFrom(p->params.data(), d->params.data(), 0, 1, owner.timeSig());
    return m.restartEvery;
}

// The piano roll's time axis counts from the notes' left edge, which is `left` across this pane.
float FeelView::xOf(double elapsed) { return left + float(owner.viewport.time.tickToX(spanStart() + elapsed)); }

double FeelView::elapsedAt(float x) { return owner.viewport.time.xToTick(x - left) - spanStart(); }

Rect FeelView::body() const {
    return Rect(left, kHead + kBar, std::max(0.f, box.size.x - left), std::max(0.f, box.size.y - kHead - kBar));
}

std::vector<FeelView::HeaderPart> FeelView::headerParts() {
    std::vector<HeaderPart> parts;
    float y = 3, h = kHead - 6;
    float x = left + kPad + 46; // "Shapes"
    for (int i = 0; i < kShapeCount; i++)
        parts.push_back({HeaderPart::Kind::Shape, Rect(x + float(i) * 30, y, 26, h), i});
    x += float(kShapeCount) * 30 + 16 + 40; // "Cycle"
    parts.push_back({HeaderPart::Kind::Cycle, Rect(x, y, 64, h)});
    parts.push_back({HeaderPart::Kind::Close, Rect(box.size.x - kPad - 20, y, 20, h)});
    return parts;
}

int FeelView::gripAt(Vec pos) {
    if (pos.y < kHead || pos.y > kHead + kBar)
        return -1;
    double cycle = cycleSongTicks(), restart = restartEvery();
    if (cycle <= 0)
        return -1;
    double elapsed = elapsedAt(pos.x);
    double local = restart > 0 ? std::fmod(std::max(0.0, elapsed), restart) : elapsed;
    int k = int(std::lround(local / cycle)) - 1;
    if (k < 0)
        return -1;
    double end = (elapsed - local) + double(k + 1) * cycle;
    if (restart > 0 && double(k + 1) * cycle > restart + 1e-6)
        return -1; // the last bar before a restart is cut short: it has no grip
    return std::fabs(xOf(end) - pos.x) <= kGripHit ? k : -1;
}

void FeelView::pickShape(int index) {
    Shown f = shown();
    f.bend = Bend(index);
    if (f.bend != Bend::Straight && f.amount < 0.05f)
        f.amount = 0.5f;
    commit(f, std::string("Feel: ") + kShapeNames[index]);
}

void FeelView::openCycleMenu() {
    int current = shown().cycle;
    FeelView* view = this;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("The Feel repeats every"));
    const std::vector<std::string>& names = playheads::cycleNames();
    for (int k = 0; k < int(names.size()); k++)
        menu->addChild(rack::createCheckMenuItem(names[size_t(k)], "", [current, k] { return current == k; },
                                                 [view, k] {
                                                     Shown f = view->shown();
                                                     f.cycle = k;
                                                     view->commit(f, "Set feel cycle");
                                                 }));
}

std::unique_ptr<DragGesture> FeelView::startGesture(Vec pos, int button, Modifiers mods) {
    const Playhead* p = playhead();
    if (button != GLFW_MOUSE_BUTTON_LEFT || !p || playheads::wantsDrawnPath(p->params.data()))
        return nullptr;
    int grip = gripAt(pos);
    if (grip >= 0)
        return std::make_unique<CycleDrag>(*this, shown(), grip);
    Rect b = body();
    if (!b.contains(pos))
        return nullptr;
    return std::make_unique<BendDrag>(*this, shown(), pos.y, b.size.y);
}

void FeelView::click(Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    for (const HeaderPart& part : headerParts()) {
        if (!part.rect.contains(pos))
            continue;
        switch (part.kind) {
        case HeaderPart::Kind::Shape:
            pickShape(part.shape);
            break;
        case HeaderPart::Kind::Cycle:
            openCycleMenu();
            break;
        case HeaderPart::Kind::Close:
            ctx.view().lowerLane = 0;
            break;
        }
        return;
    }
}

void FeelView::doubleClick(Vec pos, Modifiers mods) {
    if (!body().contains(pos))
        return;
    Shown f = shown();
    f.bend = Bend::Straight;
    commit(f, "Feel: Straight");
}

CursorShape FeelView::hoverCursor(Vec pos, Modifiers mods) {
    for (const HeaderPart& part : headerParts())
        if (part.rect.contains(pos))
            return CursorShape::Hand;
    const Playhead* p = playhead();
    if (!p || playheads::wantsDrawnPath(p->params.data()))
        return CursorShape::Arrow;
    if (gripAt(pos) >= 0)
        return CursorShape::ResizeH;
    return body().contains(pos) ? CursorShape::ResizeV : CursorShape::Arrow;
}

void drawFeelShape(NVGcontext* vg, Rect r, Bend bend, float amount, NVGcolor color, float width) {
    nvgBeginPath(vg);
    for (int i = 0; i <= 24; i++) {
        double f = double(i) / 24;
        float x = r.pos.x + float(f) * r.size.x;
        float y = r.getBottom() - float(playheads::warp(bend, amount, f)) * r.size.y;
        if (i == 0)
            nvgMoveTo(vg, x, y);
        else
            nvgLineTo(vg, x, y);
    }
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, width);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
}

void FeelView::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);
    const Playhead* p = playhead();
    if (!p)
        return;
    NVGcolor color = theme::playheadColor(p->color);
    Shown feel = shown();
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    drawHeader(vg, color, feel);
    if (playheads::wantsDrawnPath(p->params.data())) {
        Rect b = body();
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, b.getCenter().x, b.getCenter().y - kBar / 2,
                   draw::ellipsize(vg, hint,
                                   "This playhead follows a drawn path, which replaces its Feel. More › Path › From "
                                   "settings brings the Feel back.",
                                   b.size.x - 20));
    } else {
        drawRuler(vg, color);
        drawLines(vg, color, feel);
    }
    nvgRestore(vg);
}

void FeelView::drawHeader(NVGcontext* vg, NVGcolor color, const Shown& feel) {
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, left, 0, box.size.x - left, kHead);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    draw::TextStyle label{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle choice{theme::fonts::uiStrong(), theme::metrics::kTextRuler, color, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    bool straight = feel.bend == Bend::Straight || feel.amount <= 0;
    float after = left;
    for (const HeaderPart& part : headerParts()) {
        Rect r = part.rect;
        switch (part.kind) {
        case HeaderPart::Kind::Shape: {
            if (part.shape == 0)
                draw::text(vg, label, r.pos.x - 44, r.getCenter().y, "Shapes");
            bool on = straight ? part.shape == 0 : int(feel.bend) == part.shape;
            draw::roundedRect(vg, r, 3, on ? nvgTransRGBAf(color, 0.25f) : c.bg2);
            if (on)
                draw::roundedRectStroke(vg, r, 3, color, 1);
            drawFeelShape(vg, r.shrink(Vec(6, 4)), Bend(part.shape), 0.6f, on ? color : c.text1, 1.3f);
            break;
        }
        case HeaderPart::Kind::Cycle: {
            draw::text(vg, label, r.pos.x - 36, r.getCenter().y, "Cycle");
            draw::roundedRect(vg, r, 3, c.bg2);
            draw::text(vg, choice, r.pos.x + 7, r.getCenter().y, playheads::cycleNames()[size_t(feel.cycle)]);
            float ax = r.getRight() - 9, ay = r.getCenter().y;
            nvgBeginPath(vg);
            nvgMoveTo(vg, ax - 3, ay - 1.5f);
            nvgLineTo(vg, ax + 3, ay - 1.5f);
            nvgLineTo(vg, ax, ay + 2);
            nvgClosePath(vg);
            nvgFillColor(vg, c.text1);
            nvgFill(vg);
            after = r.getRight() + 14;
            break;
        }
        case HeaderPart::Kind::Close: {
            Vec m = r.getCenter();
            nvgBeginPath(vg);
            nvgMoveTo(vg, m.x - 4, m.y - 4);
            nvgLineTo(vg, m.x + 4, m.y + 4);
            nvgMoveTo(vg, m.x + 4, m.y - 4);
            nvgLineTo(vg, m.x - 4, m.y + 4);
            nvgStrokeColor(vg, c.text1);
            nvgStrokeWidth(vg, 1.4f);
            nvgStroke(vg);
            break;
        }
        }
    }
    // What it is now, and how to change it.
    std::string now = describe(feel) + "  ·  drag the line up or down to bend it, double-click for straight";
    float room = box.size.x - kPad - 30 - after;
    if (room > 40)
        draw::text(vg, label, after, kHead / 2, draw::ellipsize(vg, label, now, room));
}

void FeelView::drawRuler(NVGcontext* vg, NVGcolor color) {
    const theme::Palette& c = theme::colors();
    Rect bar(left, kHead, box.size.x - left, kBar);
    nvgBeginPath(vg);
    nvgRect(vg, bar.pos.x, bar.pos.y, bar.size.x, bar.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    nvgSave(vg);
    nvgIntersectScissor(vg, bar.pos.x, bar.pos.y, bar.size.x, bar.size.y);
    double cycle = cycleSongTicks(), restart = restartEvery();
    if (cycle * double(owner.viewport.time.pxPerTick()) < 2) {
        nvgRestore(vg);
        return; // too many to draw
    }
    double from = std::max(0.0, elapsedAt(left)), to = elapsedAt(box.size.x);
    double base = restart > 0 ? std::floor(from / restart) * restart : 0;
    draw::TextStyle name{theme::fonts::ui(), theme::metrics::kTextRuler, c.onTile, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::string cycleName = playheads::cycleNames()[size_t(shown().cycle)] + " cycle";
    for (; base < to; base += restart > 0 ? restart : 1e18) {
        double pieceEnd = restart > 0 ? base + restart : 1e18;
        double first = std::max(0.0, std::floor((from - base) / cycle)) * cycle;
        for (double s = base + first; s < std::min(to, pieceEnd); s += cycle) {
            double e = std::min(s + cycle, pieceEnd);
            float x0 = xOf(s) + 1, x1 = xOf(e) - 1;
            Rect r(x0, bar.pos.y + 3, std::max(1.f, x1 - x0), kBar - 6);
            draw::roundedRect(vg, r, 2, nvgTransRGBAf(color, s == base + first ? 0.85f : 0.45f));
            // The grip at its right end, where the bar is a whole cycle.
            if (e - s >= cycle - 1e-6) {
                nvgBeginPath(vg);
                nvgRect(vg, r.getRight() - 3, r.pos.y, 3, r.size.y);
                nvgFillColor(vg, color);
                nvgFill(vg);
            }
            if (s == base + first && r.size.x > 50)
                draw::text(vg, name, r.pos.x + 5, r.getCenter().y, draw::ellipsize(vg, name, cycleName, r.size.x - 10));
        }
        if (restart <= 0)
            break;
    }
    nvgRestore(vg);
}

void FeelView::drawLines(NVGcontext* vg, NVGcolor color, const Shown& feel) {
    const theme::Palette& c = theme::colors();
    Rect b = body();
    float top = b.pos.y + kPad, bottom = b.getBottom() - kPad;
    // The left column names the two ends: the start of a cycle and its end.
    if (bottom - top >= 30) { // a short pane (the in-panel editor) has no room for both
        draw::TextStyle side{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
        draw::text(vg, side, left - 6, top, "cycle end");
        draw::text(vg, side, left - 6, bottom, "start");
    }
    nvgSave(vg);
    nvgIntersectScissor(vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
    double cycle = cycleSongTicks(), restart = restartEvery();
    if (cycle * double(owner.viewport.time.pxPerTick()) < 2) {
        nvgRestore(vg);
        return;
    }
    double from = std::max(0.0, elapsedAt(b.pos.x)), to = elapsedAt(b.getRight());
    double base = restart > 0 ? std::floor(from / restart) * restart : 0;
    for (; base < to; base += restart > 0 ? restart : 1e18) {
        double pieceEnd = restart > 0 ? base + restart : 1e18;
        double first = std::max(0.0, std::floor((from - base) / cycle)) * cycle;
        for (double s = base + first; s < std::min(to, pieceEnd); s += cycle) {
            double e = std::min(s + cycle, pieceEnd);
            float x0 = xOf(s), x1 = xOf(s + cycle);
            float cut = xOf(e);
            nvgSave(vg);
            nvgIntersectScissor(vg, x0, b.pos.y, std::max(0.f, cut - x0), b.size.y);
            nvgBeginPath(vg);
            nvgRect(vg, x0, b.pos.y, 1, b.size.y);
            nvgFillColor(vg, c.line0);
            nvgFill(vg);
            // Even: the straight line. The Feel: how far through the cycle the playhead is at each moment.
            dashed(vg, Vec(x0, bottom), Vec(x1, top), nvgTransRGBAf(c.text2, 0.6f), 1, 4, 3);
            drawFeelShape(vg, Rect(x0, top, x1 - x0, bottom - top), feel.bend, feel.amount, color, 1.8f);
            nvgRestore(vg);
        }
        if (restart <= 0)
            break;
    }
    nvgRestore(vg);
}

} // namespace tessera
