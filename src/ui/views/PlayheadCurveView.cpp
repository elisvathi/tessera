#include "PlayheadCurveView.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/DeviceCommands.hpp"
#include "../../model/DeviceLookup.hpp"
#include "../../model/DeviceTypes.hpp"
#include "../../model/Playheads.hpp"
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

namespace {

constexpr float kPad = 6;
constexpr float kHit = 6;

/// Dragging a point, or the length bar's end. One undo step on release.
class CurveDrag : public DragGesture {
  public:
    enum class Mode { Point, Length };
    CurveDrag(PlayheadCurveView& view, PlayheadId id, int param, std::vector<AutoPoint> points, Tick length,
              Mode mode, int index, double cycleBase)
        : view(view), id(id), param(param), points(std::move(points)), length(length), mode(mode), index(index),
          cycleBase(cycleBase) {
        view.preview = std::make_pair(this->points, length);
    }
    void update(Vec pos, Modifiers mods) override {
        Tick snap = mods.alt ? 1 : view.snap();
        if (mode == Mode::Length) {
            double t = view.tAt(pos.x) - view.spanStart();
            length = std::max<Tick>(snap, Tick(std::llround(t / double(snap))) * snap);
            for (AutoPoint& p : points)
                p.t = std::min(p.t, length);
        } else {
            double t = view.tAt(pos.x) - view.spanStart() - cycleBase;
            Tick at = Tick(std::llround(t / double(snap))) * snap;
            // A point stays between its neighbours, so dragging never reorders the curve.
            Tick lo = index > 0 ? points[size_t(index) - 1].t : 0;
            Tick hi = index + 1 < int(points.size()) ? points[size_t(index) + 1].t : length;
            points[size_t(index)].t = std::clamp(at, lo, hi);
            points[size_t(index)].value = view.valueAt(pos.y);
        }
        view.preview = std::make_pair(points, length);
        moved = true;
    }
    void commit() override {
        view.preview.reset();
        TesseraModule* m = view.module();
        if (m && (moved || fresh))
            m->perform(setPlayheadCurve(m->song(), id, param, length, points));
    }
    void cancel() override { view.preview.reset(); }
    std::string readout() const override {
        const devices::ParamInfo* info = devices::playheadParamInfo(param);
        if (mode == Mode::Length || !info)
            return "Lasts " + std::to_string(length / (kPpq / 4)) + " steps";
        return devices::format(*info, points[size_t(index)].value);
    }
    CursorShape cursor() const override { return mode == Mode::Length ? CursorShape::ResizeH : CursorShape::Move; }
    bool fresh = false;

  private:
    PlayheadCurveView& view;
    PlayheadId id;
    int param;
    std::vector<AutoPoint> points;
    Tick length;
    Mode mode;
    int index;
    double cycleBase;
    bool moved = false;
};

} // namespace

const Playhead* PlayheadCurveView::playhead() { return deviceActions::shownPlayhead(ctx); }

const PlayheadCurve* PlayheadCurveView::curve() {
    const Playhead* p = playhead();
    if (!p)
        return nullptr;
    for (const PlayheadCurve& c : p->curves)
        if (c.param == ctx.playheadCurve)
            return &c;
    return nullptr;
}

bool PlayheadCurveView::hasCurve() {
    return playhead() && devices::playheadParamInfo(ctx.playheadCurve) != nullptr;
}

std::string PlayheadCurveView::title() {
    const devices::ParamInfo* info = devices::playheadParamInfo(ctx.playheadCurve);
    return info ? std::string(info->name) + " curve" : "Curve";
}

double PlayheadCurveView::spanStart() {
    const Clip* c = owner.clip();
    const Track* t = owner.track();
    if (!c || !t)
        return 0;
    return double(clipSpan(*c, t->type == TrackType::Drum ? &t->drumLanes : nullptr).start);
}

std::pair<std::vector<AutoPoint>, Tick> PlayheadCurveView::shown() {
    if (preview)
        return *preview;
    if (const PlayheadCurve* c = curve())
        return {c->points, c->length};
    // No curve yet: no points over the clip's length (drawView draws the setting's value as a flat line).
    const Clip* clip = owner.clip();
    return {{}, clip ? std::max<Tick>(kPpq, clip->playEnd() - clip->playStart()) : 4 * kPpq};
}

float PlayheadCurveView::xOf(double t) { return float(owner.viewport.time.tickToX(t)); }
double PlayheadCurveView::tAt(float x) { return owner.viewport.time.xToTick(x); }

float PlayheadCurveView::yOf(float value) const {
    float top = kBar + kPad, bottom = box.size.y - kPad;
    return bottom - value * (bottom - top);
}

float PlayheadCurveView::valueAt(float y) const {
    float top = kBar + kPad, bottom = box.size.y - kPad;
    return std::clamp((bottom - y) / std::max(1.f, bottom - top), 0.f, 1.f);
}

Tick PlayheadCurveView::snap() { return std::clamp<Tick>(owner.snapStep(), 1, kPpq); }

int PlayheadCurveView::pointAt(Vec pos) {
    auto [points, length] = shown();
    if (length <= 0)
        return -1;
    double t = tAt(pos.x) - spanStart();
    double base = std::floor(t / double(length)) * double(length);
    for (size_t i = 0; i < points.size(); i++) {
        float x = xOf(spanStart() + base + double(points[i].t));
        if (std::fabs(x - pos.x) <= kHit && std::fabs(yOf(points[i].value) - pos.y) <= kHit)
            return int(i);
    }
    return -1;
}

std::unique_ptr<DragGesture> PlayheadCurveView::startGesture(Vec pos, int button, Modifiers mods) {
    const Playhead* p = playhead();
    if (button != GLFW_MOUSE_BUTTON_LEFT || !p || !hasCurve())
        return nullptr;
    auto [points, length] = shown();
    if (pos.y < kBar) {
        // Only the bar's end (◀) is a handle: the curve always starts with the clip.
        if (std::fabs(pos.x - xOf(spanStart() + double(length))) > 8)
            return nullptr;
        return std::make_unique<CurveDrag>(*this, p->id, ctx.playheadCurve, points, length, CurveDrag::Mode::Length,
                                           -1, 0);
    }
    double t = tAt(pos.x) - spanStart();
    if (t < 0)
        return nullptr;
    double base = std::floor(t / double(length)) * double(length);
    int index = pointAt(pos);
    bool fresh = index < 0;
    if (fresh) {
        // The first point keeps the setting where it is: the curve starts from the knob's value.
        if (points.empty() && ctx.playheadCurve < int(p->params.size()))
            points.push_back({0, p->params[size_t(ctx.playheadCurve)], CurveType::Linear, 0});
        Tick at = Tick(std::llround((t - base) / double(snap()))) * snap();
        AutoPoint pt{std::clamp<Tick>(at, 0, length), valueAt(pos.y), CurveType::Linear, 0};
        auto it = std::upper_bound(points.begin(), points.end(), pt.t,
                                   [](Tick v, const AutoPoint& q) { return v < q.t; });
        index = int(it - points.begin());
        points.insert(it, pt);
    }
    auto drag = std::make_unique<CurveDrag>(*this, p->id, ctx.playheadCurve, points, length, CurveDrag::Mode::Point,
                                            index, base);
    drag->fresh = fresh;
    return drag;
}

void PlayheadCurveView::doubleClick(Vec pos, Modifiers mods) {
    const Playhead* p = playhead();
    TesseraModule* m = ctx.module();
    int index = pointAt(pos);
    if (!p || !m || index < 0)
        return;
    auto [points, length] = shown();
    points.erase(points.begin() + index);
    m->perform(setPlayheadCurve(m->song(), p->id, ctx.playheadCurve, length, points));
}

void PlayheadCurveView::click(Vec pos, int button, Modifiers mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
        openMenu();
}

void PlayheadCurveView::openMenu() {
    const Playhead* p = playhead();
    if (!p)
        return;
    PlayheadId id = p->id;
    int param = ctx.playheadCurve;
    Tick length = shown().second;
    EditorContext* c = &ctx;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(title()));
    menu->addChild(rack::createMenuItem("Stop animating", "", [c, id, param] {
        if (TesseraModule* m = c->module())
            m->perform(setPlayheadCurve(m->song(), id, param, 0, {}));
    }));
    // Shapes to start from, over the curve's length, between the knob's lowest and highest.
    struct Shape {
        const char* name;
        std::vector<std::pair<double, float>> points; // (place in the length, value)
        CurveType curve;
    };
    const Shape shapes[] = {{"Rise", {{0, 0}, {1, 1}}, CurveType::Linear},
                            {"Fall", {{0, 1}, {1, 0}}, CurveType::Linear},
                            {"Up and down", {{0, 0}, {0.5, 1}, {1, 0}}, CurveType::SCurve},
                            {"Two steps", {{0, 0}, {0.5, 1}, {1, 1}}, CurveType::Hold}};
    menu->addChild(rack::createSubmenuItem("Shape", "", [=](rack::ui::Menu* sub) {
        for (const Shape& s : shapes)
            sub->addChild(rack::createMenuItem(s.name, "", [=] {
                TesseraModule* m = c->module();
                if (!m)
                    return;
                std::vector<AutoPoint> points;
                for (auto [at, v] : s.points)
                    points.push_back({Tick(at * double(length)), v, s.curve, 0});
                m->perform(setPlayheadCurve(m->song(), id, param, length, points));
            }));
    }));
    menu->addChild(rack::createMenuLabel("Drag the bar's end to set how long it lasts; Alt: off the grid"));
}

CursorShape PlayheadCurveView::hoverCursor(Vec pos, Modifiers mods) {
    if (!hasCurve())
        return CursorShape::Arrow;
    if (pos.y < kBar)
        return std::fabs(pos.x - xOf(spanStart() + double(shown().second))) <= 8 ? CursorShape::ResizeH
                                                                                   : CursorShape::Arrow;
    return pointAt(pos) >= 0 ? CursorShape::Move : CursorShape::Crosshair;
}

void PlayheadCurveView::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);
    const Playhead* p = playhead();
    const devices::ParamInfo* info = devices::playheadParamInfo(ctx.playheadCurve);
    if (!p || !info)
        return;
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    NVGcolor color = theme::playheadColor(p->color);
    auto [points, length] = shown();
    double start = spanStart();

    // The length bar: ▶ where the clip starts, ◀ where the curve repeats.
    float x0 = xOf(start), x1 = xOf(start + double(length));
    draw::roundedRect(vg, Rect(x0, 2, std::max(2.f, x1 - x0), kBar - 4), 2, nvgTransRGBAf(color, 0.25f));
    nvgBeginPath(vg);
    nvgMoveTo(vg, x0, 2);
    nvgLineTo(vg, x0 + 8, kBar / 2);
    nvgLineTo(vg, x0, kBar - 2);
    nvgClosePath(vg);
    nvgMoveTo(vg, x1, 2);
    nvgLineTo(vg, x1 - 8, kBar / 2);
    nvgLineTo(vg, x1, kBar - 2);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);

    // The curve, every cycle across the view; the first one bright, its repeats faint.
    float flat = p->params[size_t(ctx.playheadCurve)];
    double t0 = owner.viewport.time.xToTick(0), t1 = owner.viewport.time.xToTick(box.size.x);
    for (double base = start; base < t1; base += double(length)) {
        if (base + double(length) < t0)
            continue;
        nvgBeginPath(vg);
        bool first = true;
        for (int i = 0; i <= 64; i++) {
            double t = double(length) * i / 64;
            size_t cursor = 0;
            float v = points.empty() ? flat : evaluateAt(points, t, cursor, flat);
            float x = xOf(base + t), y = yOf(v);
            first ? nvgMoveTo(vg, x, y) : nvgLineTo(vg, x, y);
            first = false;
        }
        nvgStrokeColor(vg, nvgTransRGBAf(color, base == start ? 1.f : 0.35f));
        nvgStrokeWidth(vg, base == start ? 2.f : 1.4f);
        nvgStroke(vg);
        for (const AutoPoint& pt : points) {
            nvgBeginPath(vg);
            nvgCircle(vg, xOf(base + double(pt.t)), yOf(pt.value), 3.5f);
            nvgFillColor(vg, c.bg0);
            nvgFill(vg);
            nvgStrokeColor(vg, nvgTransRGBAf(color, base == start ? 1.f : 0.35f));
            nvgStrokeWidth(vg, 1.4f);
            nvgStroke(vg);
        }
    }
    draw::TextStyle label{theme::fonts::ui(), 10, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
    draw::text(vg, label, 6, kBar + 2, devices::format(*info, 1));
    label.align = NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM;
    draw::text(vg, label, 6, box.size.y - 2, devices::format(*info, 0));
    if (points.empty()) {
        draw::TextStyle hint{theme::fonts::ui(), 11, c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, box.size.x / 2, (kBar + box.size.y) / 2,
                   "Click to add a point: " + std::string(info->name) + " starts moving. Right-click for shapes.");
    }
    nvgRestore(vg);
}

} // namespace tessera
