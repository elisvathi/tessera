#include "AutomationEditor.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/DeviceActions.hpp"
#include "../interaction/ClickOrDrag.hpp"
#include "../interaction/Snap.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/MenuTextField.hpp"

#include <map>

namespace tessera {

using autoEdits::Points;
using autoEdits::Selection;
using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kSegmentHit = 5;
constexpr float kMinHandleSegment = 18; // px: narrower segments show no bend handle
constexpr float kTensionPerPx = 1.f / 60;
constexpr float kFineFactor = 0.1f;
constexpr float kNudge = 0.01f;
constexpr float kDrawTolerancePx = 1.5f;

/// Points copied with Ctrl/Cmd+C, times relative to the first.
Points& clipboard() {
    static Points points;
    return points;
}

/// The value of a point list at t (for drawing previews and inserting on the curve).
float valueOf(const Points& points, double t, float rest, size_t& cursor) {
    return evaluateAt(points, t, cursor, rest);
}

Tick snapped(double tick, Tick step, bool free) {
    return free || step <= 0 ? Tick(std::llround(tick)) : snapPosition(tick, step);
}

/// Shared by the gestures below: the starting points and the press in lane terms.
class LaneGesture : public DragGesture {
  public:
    LaneGesture(AutomationEditor& editor, const LaneFrame& frame)
        : editor(editor), frame(frame), before(editor.points(frame)) {}
    void cancel() override { editor.clearPreview(); }
    bool autoScrollX() const override { return true; }

  protected:
    AutomationEditor& editor;
    LaneFrame frame;
    Points before;
};

class MovePoints : public LaneGesture {
  public:
    MovePoints(AutomationEditor& e, const LaneFrame& f, Vec press, size_t grabbed)
        : LaneGesture(e, f), selected(e.selection(f)), anchor(before[grabbed]), pressTick(e.tickAt(f, press.x)),
          pressValue(e.valueAt(f, press.y)) {}

    void update(Vec pos, Modifiers mods) override {
        double dt = editor.tickAt(frame, pos.x) - pressTick;
        float dv = editor.valueAt(frame, pos.y) - pressValue;
        if (mods.shift) {
            // Shift: one axis only, and the value moves finely.
            double dx = dt * frame.time->pxPerTick(), dy = double(dv) * frame.rect.size.y;
            if (std::fabs(dx) > std::fabs(dy))
                dv = 0;
            else
                dt = 0, dv *= kFineFactor;
        }
        delta = snapped(double(anchor.t) + dt, frame.snap, mods.alt) - anchor.t;
        value = dv;
        result = autoEdits::movePoints(before, selected, delta, value, &moved);
        editor.setPreview(frame, result);
    }
    void commit() override {
        editor.clearPreview();
        if (delta != 0 || value != 0)
            editor.commit(frame, result, "Move automation points", moved);
    }
    std::string readout() const override { return editor.valueText(frame, std::clamp(anchor.value + value, 0.f, 1.f)); }
    CursorShape cursor() const override { return CursorShape::Move; }

  private:
    Selection selected, moved;
    AutoPoint anchor;
    double pressTick;
    float pressValue;
    Tick delta = 0;
    float value = 0;
    Points result;
};

/// Drag the bend handle of a segment: its tension follows the mouse (up bends it up).
class BendSegment : public LaneGesture {
  public:
    BendSegment(AutomationEditor& e, const LaneFrame& f, Vec press, size_t segment)
        : LaneGesture(e, f), segment(segment), pressY(press.y), start(before[segment].tension) {
        const AutoPoint& a = before[segment];
        const AutoPoint& b = before[segment + 1];
        // Positive tension lifts a rising segment and drops a falling one, so dragging up means more for rising.
        direction = b.value < a.value ? -1.f : 1.f;
    }
    void update(Vec pos, Modifiers mods) override {
        float scale = mods.shift ? kFineFactor : 1.f;
        tension = std::clamp(start + (pressY - pos.y) * kTensionPerPx * scale * direction, -1.f, 1.f);
        result = before;
        if (result[segment].curve == CurveType::Hold)
            result[segment].curve = CurveType::Linear;
        result[segment].tension = tension;
        editor.setPreview(frame, result);
    }
    void commit() override {
        editor.clearPreview();
        if (!(result == before))
            editor.commit(frame, result, "Bend automation curve", editor.selection(frame));
    }
    std::string readout() const override {
        char text[32];
        snprintf(text, sizeof(text), "Bend %+.0f%%", tension * 100);
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeV; }
    bool autoScrollX() const override { return false; }

  private:
    size_t segment;
    float pressY, start, direction = 1, tension = 0;
    Points result;
};

/// Ctrl/Cmd+drag on a segment: both its points move up and down together.
class MoveSegment : public LaneGesture {
  public:
    MoveSegment(AutomationEditor& e, const LaneFrame& f, Vec press, size_t segment)
        : LaneGesture(e, f), segment(segment), pressValue(e.valueAt(f, press.y)) {}
    void update(Vec pos, Modifiers mods) override {
        dv = (editor.valueAt(frame, pos.y) - pressValue) * (mods.shift ? kFineFactor : 1.f);
        result = autoEdits::movePoints(before, {segment, segment + 1}, 0, dv);
        editor.setPreview(frame, result);
    }
    void commit() override {
        editor.clearPreview();
        if (dv != 0)
            editor.commit(frame, result, "Move automation segment", {segment, segment + 1});
    }
    std::string readout() const override {
        return editor.valueText(frame, std::clamp(before[segment].value + dv, 0.f, 1.f));
    }
    CursorShape cursor() const override { return CursorShape::ResizeV; }
    bool autoScrollX() const override { return false; }

  private:
    size_t segment;
    float pressValue, dv = 0;
    Points result;
};

/// Pointer drag from empty space: selects the points inside the box (Shift adds).
class SelectBox : public LaneGesture {
  public:
    SelectBox(AutomationEditor& e, const LaneFrame& f, Vec press, bool add)
        : LaneGesture(e, f), pressTick(e.tickAt(f, press.x)), pressY(press.y),
          base(add ? e.selection(f) : Selection{}) {}
    void update(Vec pos, Modifiers mods) override {
        current = pos;
        double t0 = std::min(pressTick, editor.tickAt(frame, pos.x));
        double t1 = std::max(pressTick, editor.tickAt(frame, pos.x));
        float y0 = std::min(pressY, pos.y), y1 = std::max(pressY, pos.y);
        Selection s = base;
        for (size_t i = 0; i < before.size(); i++) {
            float y = editor.yOf(frame, before[i].value);
            if (double(before[i].t) >= t0 && double(before[i].t) <= t1 && y >= y0 - 2 && y <= y1 + 2)
                s.insert(i);
        }
        editor.select(frame, s);
    }
    void drawOverlay(NVGcontext* vg) override {
        const theme::Palette& c = theme::colors();
        float x0 = editor.xOf(frame, pressTick);
        Rect r(std::min(x0, current.x), std::min(pressY, current.y), std::fabs(current.x - x0),
               std::fabs(current.y - pressY));
        nvgBeginPath(vg);
        nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
        nvgFillColor(vg, nvgTransRGBAf(c.select, 0.15f));
        nvgFill(vg);
        nvgStrokeColor(vg, c.select);
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
    void cancel() override { editor.select(frame, base); }

  private:
    double pressTick;
    float pressY;
    Selection base;
    Vec current;
};

/// Draw tool: freehand, thinned to few points on release. With Alt, a straight line.
class DrawCurve : public LaneGesture {
  public:
    DrawCurve(AutomationEditor& e, const LaneFrame& f, Vec press)
        : LaneGesture(e, f), pressTick(e.tickAt(f, press.x)), pressValue(e.valueAt(f, press.y)) {
        add(press);
    }
    void update(Vec pos, Modifiers mods) override {
        line = mods.alt;
        if (line) {
            Tick a = snapped(pressTick, frame.snap, false), b = snapped(editor.tickAt(frame, pos.x), frame.snap, false);
            AutoPoint p0, p1;
            p0.t = std::min(a, b);
            p1.t = std::max(a, b);
            p0.value = a <= b ? pressValue : editor.valueAt(frame, pos.y);
            p1.value = a <= b ? editor.valueAt(frame, pos.y) : pressValue;
            result = autoEdits::replaceRange(before, p0.t, p1.t, {p0, p1});
        } else {
            // Fill the gap from the last position, so fast strokes leave no holes.
            Vec from = last;
            int steps = std::max(1, int(std::fabs(pos.x - from.x) / 2));
            for (int i = 1; i <= steps; i++)
                add(from.plus(pos.minus(from).mult(float(i) / float(steps))));
            Points drawn;
            for (const auto& [t, v] : samples) {
                AutoPoint p;
                p.t = t;
                p.value = v;
                drawn.push_back(p);
            }
            float tolerance = kDrawTolerancePx / std::max(1.f, frame.rect.size.y - 2 * AutomationEditor::kPad);
            drawn = autoEdits::simplify(drawn, tolerance);
            if (editor.stepped(frame))
                for (AutoPoint& p : drawn)
                    p.curve = CurveType::Hold;
            result = autoEdits::replaceRange(before, samples.begin()->first, samples.rbegin()->first, drawn);
        }
        last = pos;
        editor.setPreview(frame, result);
    }
    void commit() override {
        editor.clearPreview();
        if (!result.empty() || !before.empty())
            editor.commit(frame, result, line ? "Draw automation line" : "Draw automation", {});
    }
    CursorShape cursor() const override { return CursorShape::Crosshair; }

  private:
    void add(Vec p) {
        Tick t = std::max<Tick>(0, Tick(std::llround(editor.tickAt(frame, p.x))));
        samples[t] = editor.valueAt(frame, p.y);
        last = p;
    }
    double pressTick;
    float pressValue;
    std::map<Tick, float> samples;
    Vec last;
    bool line = false;
    Points result;
};

/// Paint tool: held steps at each grid division the pointer crosses (stepped modulation).
class PaintSteps : public LaneGesture {
  public:
    PaintSteps(AutomationEditor& e, const LaneFrame& f, Vec press) : LaneGesture(e, f) { update(press, Modifiers()); }
    void update(Vec pos, Modifiers mods) override {
        Tick step = frame.snap > 0 ? frame.snap : kPpq / 4;
        Tick cell = std::max<Tick>(0, Tick(std::floor(editor.tickAt(frame, pos.x) / double(step))) * step);
        if (lastCell < 0)
            lastCell = cell;
        for (Tick c = std::min(lastCell, cell); c <= std::max(lastCell, cell); c += step)
            if (!cells.count(c))
                cells[c] = editor.valueAt(frame, pos.y);
        cells[cell] = editor.valueAt(frame, pos.y);
        lastCell = cell;
        Points drawn;
        for (const auto& [t, v] : cells) {
            AutoPoint p;
            p.t = t;
            p.value = v;
            p.curve = CurveType::Hold;
            drawn.push_back(p);
        }
        // The step after the last painted cell goes back to the curve that was there.
        size_t cursor = 0;
        AutoPoint end;
        end.t = cells.rbegin()->first + step;
        end.value = valueOf(before, double(end.t), editor.lane(frame) ? restValue(*editor.lane(frame)) : 0, cursor);
        drawn.push_back(end);
        result = autoEdits::replaceRange(before, cells.begin()->first, end.t, drawn);
        editor.setPreview(frame, result);
    }
    void commit() override {
        editor.clearPreview();
        editor.commit(frame, result, "Paint automation steps", {});
    }
    CursorShape cursor() const override { return CursorShape::Crosshair; }

  private:
    std::map<Tick, float> cells;
    Tick lastCell = -1;
    Points result;
};

/// Erase tool: points the pointer passes over are deleted.
class ErasePoints : public LaneGesture {
  public:
    ErasePoints(AutomationEditor& e, const LaneFrame& f, Vec press) : LaneGesture(e, f) { update(press, Modifiers()); }
    void update(Vec pos, Modifiers mods) override {
        for (size_t i = 0; i < before.size(); i++) {
            Vec p(editor.xOf(frame, double(before[i].t)), editor.yOf(frame, before[i].value));
            if (std::fabs(p.x - pos.x) <= AutomationEditor::kPointHit &&
                std::fabs(p.y - pos.y) <= AutomationEditor::kPointHit * 2)
                gone.insert(i);
        }
        editor.setPreview(frame, autoEdits::deletePoints(before, gone));
    }
    void commit() override {
        editor.clearPreview();
        if (!gone.empty())
            editor.commit(frame, autoEdits::deletePoints(before, gone), "Erase automation points", {});
    }
    CursorShape cursor() const override { return CursorShape::NotAllowed; }

  private:
    Selection gone;
};

/// An armed shape stamp: drag across a range; up and down sets the depth around the press value.
class StampShape : public LaneGesture {
  public:
    StampShape(AutomationEditor& e, const LaneFrame& f, Vec press, autoEdits::Shape shape)
        : LaneGesture(e, f), shape(shape), pressTick(e.tickAt(f, press.x)), pressValue(e.valueAt(f, press.y)),
          pressY(press.y) {}
    void update(Vec pos, Modifiers mods) override {
        Tick a = snapped(pressTick, frame.snap, mods.alt),
             b = snapped(editor.tickAt(frame, pos.x), frame.snap, mods.alt);
        from = std::min(a, b);
        to = std::max(a, b);
        float depth = std::clamp(0.25f + (pressY - pos.y) / std::max(1.f, frame.rect.size.y), 0.02f, 1.f);
        low = std::clamp(pressValue - depth, 0.f, 1.f);
        high = std::clamp(pressValue + depth, 0.f, 1.f);
        // One cycle per grid step, or per beat without snap.
        Tick unit = frame.snap > 0 ? frame.snap : kPpq;
        cycles = std::max(1.0, std::round(double(to - from) / double(unit)));
        result = to > from ? autoEdits::stampShape(before, from, to, shape, cycles, low, high, 0, uint32_t(from + 1))
                           : before;
        editor.setPreview(frame, result);
    }
    void commit() override {
        editor.clearPreview();
        if (to > from)
            editor.commit(frame, result, std::string("Stamp ") + autoEdits::shapeName(shape), {});
    }
    std::string readout() const override {
        char text[64];
        snprintf(text, sizeof(text), "%s  ×%.0f  %s – %s", autoEdits::shapeName(shape), cycles,
                 editor.valueText(frame, low).c_str(), editor.valueText(frame, high).c_str());
        return text;
    }
    CursorShape cursor() const override { return CursorShape::Crosshair; }

  private:
    autoEdits::Shape shape;
    double pressTick;
    float pressValue, pressY;
    Tick from = 0, to = 0;
    float low = 0, high = 1;
    double cycles = 1;
    Points result;
};

/// Drags a transform box handle: scale values, stretch time or tilt the depth of the selected points.
class BoxEdit : public LaneGesture {
  public:
    using Handle = AutomationEditor::BoxHandle;
    BoxEdit(AutomationEditor& e, const LaneFrame& f, Handle handle)
        : LaneGesture(e, f), handle(handle), selected(e.selection(f)) {
        lo = 1, hi = 0;
        for (size_t i : selected) {
            lo = std::min(lo, before[i].value);
            hi = std::max(hi, before[i].value);
            first = std::min(first, before[i].t);
            last = std::max(last, before[i].t);
        }
    }
    void update(Vec pos, Modifiers mods) override {
        float v = editor.valueAt(frame, pos.y);
        Tick t = snapped(editor.tickAt(frame, pos.x), frame.snap, mods.alt);
        float span = std::max(1e-4f, hi - lo);
        switch (handle) {
        case Handle::Top:
            if (mods.alt) // around the middle
                result = autoEdits::scaleSelection(before, selected, (lo + hi) / 2, (v - (lo + hi) / 2) / (span / 2),
                                                   first, 1);
            else
                result = autoEdits::scaleSelection(before, selected, lo, (v - lo) / span, first, 1);
            break;
        case Handle::Bottom:
            result = autoEdits::scaleSelection(before, selected, hi, (hi - v) / span, first, 1);
            break;
        case Handle::Left:
            result = autoEdits::scaleSelection(before, selected, 0, 1, last,
                                               last > first ? double(last - t) / double(last - first) : 1);
            break;
        case Handle::Right:
            result = autoEdits::scaleSelection(before, selected, 0, 1, first,
                                               last > first ? double(t - first) / double(last - first) : 1);
            break;
        case Handle::TopLeft:
            result = autoEdits::tilt(before, selected, lo, (v - lo) / span, 1);
            break;
        case Handle::TopRight:
            result = autoEdits::tilt(before, selected, lo, 1, (v - lo) / span);
            break;
        default:
            result = before;
        }
        editor.setPreview(frame, result);
    }
    void commit() override {
        editor.clearPreview();
        if (!(result == before))
            editor.commit(frame, result, "Transform automation points", selectedAfter());
    }
    CursorShape cursor() const override {
        return handle == Handle::Left || handle == Handle::Right ? CursorShape::ResizeH : CursorShape::ResizeV;
    }
    bool autoScrollX() const override { return false; }

  private:
    /// The transformed points, found again after sorting: each selected point moved by the same transform.
    Selection selectedAfter() const {
        Points moved;
        for (size_t i : selected)
            moved.push_back(before[i]);
        Selection out;
        for (size_t i = 0; i < result.size(); i++) {
            bool unchanged = false;
            for (const AutoPoint& q : before)
                unchanged |= q == result[i];
            // A point that wasn't in the lane before, or was selected, belongs to the transformed set.
            if (!unchanged || std::find(moved.begin(), moved.end(), result[i]) != moved.end())
                out.insert(i);
        }
        return out;
    }
    Handle handle;
    Selection selected;
    float lo, hi;
    Tick first = INT64_MAX, last = INT64_MIN;
    Points result;
};

} // namespace

// ---- Transform box ------------------------------------------------------------------------------------------

Rect AutomationEditor::selectionBox(const LaneFrame& f) const {
    Selection s = selection(f);
    const Points& p = points(f);
    if (s.size() < 2 || ctx.state().tool != Tool::Pointer)
        return Rect();
    float lo = 1, hi = 0;
    Tick first = INT64_MAX, last = INT64_MIN;
    for (size_t i : s) {
        if (i >= p.size())
            return Rect();
        lo = std::min(lo, p[i].value);
        hi = std::max(hi, p[i].value);
        first = std::min(first, p[i].t);
        last = std::max(last, p[i].t);
    }
    float x0 = xOf(f, double(first)), x1 = xOf(f, double(last));
    float y0 = yOf(f, hi), y1 = yOf(f, lo);
    return Rect(x0, y0, std::max(1.f, x1 - x0), std::max(1.f, y1 - y0));
}

AutomationEditor::BoxHandle AutomationEditor::boxHandleAt(const LaneFrame& f, Vec pos) const {
    Rect b = selectionBox(f);
    if (b.size.x <= 0)
        return BoxHandle::None;
    // Handles sit just outside the box, so they never cover the points at its corners.
    const float out = 7, hit = 5;
    float l = b.pos.x, r = b.getRight(), t = b.pos.y, bot = b.getBottom(), cx = b.getCenter().x, cy = b.getCenter().y;
    auto near = [&](float x, float y) { return std::fabs(pos.x - x) <= hit && std::fabs(pos.y - y) <= hit; };
    if (near(l - out, t - out))
        return BoxHandle::TopLeft;
    if (near(r + out, t - out))
        return BoxHandle::TopRight;
    if (near(cx, t - out))
        return BoxHandle::Top;
    if (near(cx, bot + out))
        return BoxHandle::Bottom;
    if (near(l - out, cy))
        return BoxHandle::Left;
    if (near(r + out, cy))
        return BoxHandle::Right;
    return BoxHandle::None;
}

// ---- State ----------------------------------------------------------------------------------------------------

const AutomationEnvelope* AutomationEditor::lane(const LaneFrame& f) const {
    TesseraModule* m = ctx.module();
    return m ? findLane(m->song(), f.owner, f.lane) : nullptr;
}

const Points& AutomationEditor::points(const LaneFrame& f) const {
    static const Points none;
    if (preview && preview->first == f.lane)
        return preview->second;
    const AutomationEnvelope* l = lane(f);
    return l ? l->points : none;
}

void AutomationEditor::setPreview(const LaneFrame& f, Points p) { preview = std::make_pair(f.lane, std::move(p)); }

void AutomationEditor::commit(const LaneFrame& f, const Points& after, const std::string& name,
                              const Selection& selectAfter) {
    TesseraModule* m = ctx.module();
    const AutomationEnvelope* l = lane(f);
    if (!m || !l)
        return;
    AutomationEnvelope next = *l;
    next.points = after;
    m->perform(replaceLane(m->song(), f.owner, next, name));
    select(f, selectAfter);
}

Selection AutomationEditor::selection(const LaneFrame& f) const {
    const AutoSelection& s = ctx.state().autoSelection;
    return s.on(f.owner, f.lane) ? s.points : Selection{};
}

void AutomationEditor::select(const LaneFrame& f, Selection points) {
    ctx.state().autoSelection = AutoSelection{f.owner, f.lane, std::move(points)};
}

bool AutomationEditor::stepped(const LaneFrame& f) const {
    const AutomationEnvelope* l = lane(f);
    if (!l || !l->paramTarget.valid() || !ctx.module())
        return false;
    const devices::ParamInfo* info = deviceActions::infoOf(ctx.module()->song(), l->paramTarget);
    using Kind = devices::ParamInfo::Kind;
    return info && (info->kind == Kind::Choice || info->kind == Kind::Toggle || info->kind == Kind::Integer);
}

std::string AutomationEditor::valueText(const LaneFrame& f, float value) const {
    const AutomationEnvelope* l = lane(f);
    if (l && l->paramTarget.valid()) {
        const devices::ParamInfo* info =
            ctx.module() ? deviceActions::infoOf(ctx.module()->song(), l->paramTarget) : nullptr;
        return info ? devices::format(*info, value) : "Target missing";
    }
    char text[32];
    snprintf(text, sizeof(text), "%.2f V", l ? double(toVolts(*l, value)) : double(value));
    return text;
}

// ---- Hit testing ------------------------------------------------------------------------------------------------

AutomationEditor::Hover AutomationEditor::hitTest(const LaneFrame& f, Vec pos) const {
    Hover h;
    h.lane = f.lane;
    const Points& p = points(f);
    float best = kPointHit;
    for (size_t i = 0; i < p.size(); i++) {
        float d = std::hypot(xOf(f, double(p[i].t)) - pos.x, yOf(f, p[i].value) - pos.y);
        if (d <= best) {
            best = d;
            h.point = int(i);
        }
    }
    if (h.point >= 0)
        return h;
    for (size_t i = 0; i + 1 < p.size(); i++) {
        float x0 = xOf(f, double(p[i].t)), x1 = xOf(f, double(p[i + 1].t));
        if (pos.x < x0 || pos.x > x1)
            continue;
        h.segment = int(i);
        if (x1 - x0 >= kMinHandleSegment && p[i].curve != CurveType::Hold) {
            double mid = (double(p[i].t) + double(p[i + 1].t)) / 2;
            size_t cursor = i;
            float y = yOf(f, valueOf(p, mid, 0, cursor));
            if (std::hypot((x0 + x1) / 2 - pos.x, y - pos.y) <= kPointHit)
                h.handle = int(i);
        }
        size_t cursor = i;
        float y = yOf(f, valueOf(p, tickAt(f, pos.x), 0, cursor));
        if (std::fabs(y - pos.y) > kSegmentHit && h.handle < 0)
            h.segment = -1;
        break;
    }
    return h;
}

CursorShape AutomationEditor::hover(const LaneFrame& f, Vec pos, Modifiers mods) {
    hovered = hitTest(f, pos);
    hovered.box = hovered.point < 0 ? boxHandleAt(f, pos) : BoxHandle::None;
    if (hovered.box == BoxHandle::Left || hovered.box == BoxHandle::Right)
        return CursorShape::ResizeH;
    if (hovered.box != BoxHandle::None)
        return CursorShape::ResizeV;
    if (ctx.state().stampShape)
        return CursorShape::Crosshair;
    switch (ctx.state().tool) {
    case Tool::Draw:
    case Tool::Paint:
        return CursorShape::Crosshair;
    case Tool::Erase:
        return CursorShape::NotAllowed;
    default:
        break;
    }
    if (hovered.point >= 0)
        return CursorShape::Move;
    if (hovered.handle >= 0 || (hovered.segment >= 0 && mods.ctrl))
        return CursorShape::ResizeV;
    return CursorShape::Arrow;
}

// ---- Mouse --------------------------------------------------------------------------------------------------

std::unique_ptr<DragGesture> AutomationEditor::startGesture(const LaneFrame& f, Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || !lane(f))
        return nullptr;
    if (std::optional<autoEdits::Shape> shape = ctx.state().stampShape) {
        ctx.state().stampShape.reset(); // one stamp per arming
        return std::make_unique<StampShape>(*this, f, pos, *shape);
    }
    Hover h = hitTest(f, pos);
    switch (ctx.state().tool) {
    case Tool::Draw:
        if (h.point < 0)
            return std::make_unique<DrawCurve>(*this, f, pos);
        break;
    case Tool::Paint:
        return std::make_unique<PaintSteps>(*this, f, pos);
    case Tool::Erase:
        return std::make_unique<ErasePoints>(*this, f, pos);
    default:
        break;
    }
    if (h.point < 0) {
        BoxHandle handle = boxHandleAt(f, pos);
        if (handle != BoxHandle::None)
            return std::make_unique<BoxEdit>(*this, f, handle);
    }
    if (h.point >= 0) {
        size_t i = size_t(h.point);
        Selection s = selection(f);
        if (!s.count(i)) {
            if (!mods.shift)
                s.clear();
            s.insert(i);
            select(f, s);
        }
        return std::make_unique<MovePoints>(*this, f, pos, i);
    }
    if (h.handle >= 0)
        return std::make_unique<BendSegment>(*this, f, pos, size_t(h.handle));
    if (h.segment >= 0 && mods.ctrl)
        return std::make_unique<MoveSegment>(*this, f, pos, size_t(h.segment));
    // Empty space: a click adds a point (Alt: on the curve, keeping its shape); a drag selects.
    LaneFrame frame = f;
    bool alt = mods.alt;
    auto onClick = [this, frame, pos, alt] {
        const AutomationEnvelope* l = lane(frame);
        if (!l)
            return;
        Points p = l->points;
        Tick t = snapped(tickAt(frame, pos.x), frame.snap, alt);
        CurveType curve = stepped(frame) ? CurveType::Hold : CurveType::Linear;
        size_t i = alt ? autoEdits::insertOnCurve(p, t, restValue(*l))
                       : autoEdits::addPoint(p, t, valueAt(frame, pos.y), curve);
        commit(frame, p, "Add automation point", {i});
    };
    return std::make_unique<ClickOrDrag>(std::make_unique<SelectBox>(*this, f, pos, mods.shift), pos, onClick);
}

void AutomationEditor::doubleClick(const LaneFrame& f, Vec pos, Modifiers mods) {
    Hover h = hitTest(f, pos);
    if (h.point < 0)
        return;
    commit(f, autoEdits::deletePoints(points(f), {size_t(h.point)}), "Delete automation point", {});
}

void AutomationEditor::click(const LaneFrame& f, Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_RIGHT || !lane(f))
        return;
    Hover h = hitTest(f, pos);
    if (h.point >= 0) {
        Selection s = selection(f);
        if (!s.count(size_t(h.point)))
            select(f, {size_t(h.point)});
        openPointMenu(f, size_t(h.point));
        return;
    }
    rack::ui::Menu* menu = rack::createMenu();
    appendLaneMenu(menu, ctx, f.owner, f.lane);
}

void AutomationEditor::openPointMenu(const LaneFrame& f, size_t point) {
    rack::ui::Menu* menu = rack::createMenu();
    Selection s = selection(f);
    menu->addChild(rack::createMenuLabel(s.size() == 1 ? "Point" : std::to_string(s.size()) + " points"));
    menu->addChild(rack::createMenuLabel("Curve to the next point"));
    static const char* const curves[] = {"Hold", "Linear", "Exponential", "S-curve"};
    const Points& p = points(f);
    CurveType current = point < p.size() ? p[point].curve : CurveType::Linear;
    LaneFrame frame = f;
    for (int c = 0; c < 4; c++) {
        CurveType curve = CurveType(c);
        menu->addChild(rack::createCheckMenuItem(
            curves[c], "", [current, curve] { return current == curve; },
            [this, frame, s, curve] {
                commit(frame, autoEdits::setCurve(points(frame), s, curve), "Set automation curve", s);
            }));
    }
    menu->addChild(new rack::ui::MenuSeparator);
    const AutomationEnvelope* l = lane(f);
    const devices::ParamInfo* knob =
        l && l->paramTarget.valid() ? deviceActions::infoOf(ctx.module()->song(), l->paramTarget) : nullptr;
    if (knob && point < p.size()) {
        // A knob lane: values in the knob's own units, choices by name.
        if (knob->kind == devices::ParamInfo::Kind::Choice) {
            menu->addChild(rack::createMenuLabel("Value"));
            for (size_t i = 0; i < knob->choices.size(); i++) {
                float v = devices::normalize(*knob, float(i));
                menu->addChild(rack::createCheckMenuItem(
                    knob->choices[i], "", [cur = p[point].value, v] { return std::fabs(cur - v) < 1e-4f; },
                    [this, frame, s, v] {
                        commit(frame, autoEdits::setValue(points(frame), s, v), "Set automation value", s);
                    }));
            }
        } else {
            menu->addChild(rack::createMenuLabel(std::string("Value") + (*knob->unit ? " (" : "") + knob->unit +
                                                 (*knob->unit ? ")" : "")));
            char text[32];
            snprintf(text, sizeof(text), "%g", double(devices::display(*knob, p[point].value)));
            devices::ParamInfo info = *knob;
            menu->addChild(new MenuTextField(text, [this, frame, s, info](const std::string& typed) {
                char* end = nullptr;
                double v = std::strtod(typed.c_str(), &end);
                if (end == typed.c_str())
                    return;
                commit(frame, autoEdits::setValue(points(frame), s, devices::normalize(info, float(v))),
                       "Set automation value", s);
            }));
        }
    } else if (l && !l->paramTarget.valid() && point < p.size()) {
        menu->addChild(rack::createMenuLabel("Value (volts)"));
        char text[32];
        snprintf(text, sizeof(text), "%.3f", double(toVolts(*l, p[point].value)));
        menu->addChild(new MenuTextField(text, [this, frame, s](const std::string& typed) {
            const AutomationEnvelope* l = lane(frame);
            char* end = nullptr;
            double volts = std::strtod(typed.c_str(), &end);
            if (!l || end == typed.c_str())
                return;
            commit(frame, autoEdits::setValue(points(frame), s, fromVolts(*l, float(volts))), "Set automation value",
                   s);
        }));
    }
    menu->addChild(rack::createMenuItem("Delete", shortcutLabel(Action::Delete), [this, frame, s] {
        commit(frame, autoEdits::deletePoints(points(frame), s), "Delete automation points", {});
    }));
}

// ---- Keyboard -----------------------------------------------------------------------------------------------

bool AutomationEditor::handleAction(Action action, Tick grid, double pasteAt) {
    TesseraModule* m = ctx.module();
    AutoSelection sel = ctx.state().autoSelection;
    if (!m || !sel.lane.valid())
        return false;
    const AutomationEnvelope* l = findLane(m->song(), sel.owner, sel.lane);
    if (!l)
        return false;
    LaneFrame f;
    f.owner = sel.owner;
    f.lane = sel.lane;
    const Points& p = l->points;
    Selection s = sel.points;
    Tick step = grid > 0 ? grid : kPpq / 4;
    auto nudge = [&](Tick dt, float dv, const char* name) {
        if (s.empty())
            return;
        Selection moved;
        Points after = autoEdits::movePoints(p, s, dt, dv, &moved);
        commit(f, after, name, moved);
    };
    switch (action) {
    case Action::Delete:
        if (s.empty())
            return false;
        commit(f, autoEdits::deletePoints(p, s), "Delete automation points", {});
        return true;
    case Action::SelectAll: {
        Selection all;
        for (size_t i = 0; i < p.size(); i++)
            all.insert(i);
        select(f, all);
        return true;
    }
    case Action::SelectNone:
        select(f, {});
        return true;
    case Action::Copy:
    case Action::Cut: {
        if (s.empty())
            return false;
        Points copied;
        for (size_t i : s)
            copied.push_back(p[i]);
        Tick first = copied.front().t;
        for (AutoPoint& q : copied)
            q.t -= first;
        clipboard() = copied;
        if (action == Action::Cut)
            commit(f, autoEdits::deletePoints(p, s), "Cut automation points", {});
        return true;
    }
    case Action::Paste:
    case Action::PasteAtMouse: {
        if (clipboard().empty() || !std::isfinite(pasteAt))
            return false;
        Tick at = std::max<Tick>(0, Tick(std::llround(pasteAt)));
        Points shifted = clipboard();
        for (AutoPoint& q : shifted)
            q.t += at;
        Points after = autoEdits::replaceRange(p, at, shifted.back().t, shifted);
        Selection pasted;
        for (size_t i = 0; i < after.size(); i++)
            if (after[i].t >= at && after[i].t <= shifted.back().t)
                pasted.insert(i);
        commit(f, after, "Paste automation points", pasted);
        return true;
    }
    case Action::TransposeUp:
        nudge(0, kNudge, "Nudge automation");
        return !s.empty();
    case Action::TransposeDown:
        nudge(0, -kNudge, "Nudge automation");
        return !s.empty();
    case Action::OctaveUp:
        nudge(0, kNudge * kFineFactor, "Nudge automation");
        return !s.empty();
    case Action::OctaveDown:
        nudge(0, -kNudge * kFineFactor, "Nudge automation");
        return !s.empty();
    case Action::NudgeLeft:
        nudge(-step, 0, "Nudge automation");
        return !s.empty();
    case Action::NudgeRight:
        nudge(step, 0, "Nudge automation");
        return !s.empty();
    case Action::NudgeFineLeft:
        nudge(-std::max<Tick>(1, step / 10), 0, "Nudge automation");
        return !s.empty();
    case Action::NudgeFineRight:
        nudge(std::max<Tick>(1, step / 10), 0, "Nudge automation");
        return !s.empty();
    default:
        return false;
    }
}

// ---- Lane menu ----------------------------------------------------------------------------------------------

void AutomationEditor::appendLaneMenu(rack::ui::Menu* menu, EditorContext& ctx, LaneOwner owner, AutomationId id) {
    TesseraModule* m = ctx.module();
    const AutomationEnvelope* lane = m ? findLane(m->song(), owner, id) : nullptr;
    if (!lane)
        return;
    EditorContext* c = &ctx;
    // Changes a copy of the lane and records it.
    auto edit = [c, owner, id](const std::string& name, std::function<void(AutomationEnvelope&)> change) {
        return [c, owner, id, name, change] {
            TesseraModule* m = c->module();
            const AutomationEnvelope* l = m ? findLane(m->song(), owner, id) : nullptr;
            if (!l)
                return;
            AutomationEnvelope next = *l;
            change(next);
            m->perform(replaceLane(m->song(), owner, next, name));
        };
    };
    // Functions act on the selected points of this lane, or on all of them.
    auto targets = [c, owner, id](const AutomationEnvelope& l) {
        const AutoSelection& s = c->state().autoSelection;
        if (s.on(owner, id) && !s.empty())
            return s.points;
        Selection all;
        for (size_t i = 0; i < l.points.size(); i++)
            all.insert(i);
        return all;
    };

    menu->addChild(rack::createMenuLabel(lane->name));
    menu->addChild(new MenuTextField(lane->name, [edit](const std::string& name) {
        if (!name.empty())
            edit("Rename lane", [name](AutomationEnvelope& l) { l.name = name; })();
    }));
    // A knob lane turns a device knob: it has no output, voltage range or slew.
    bool knob = lane->paramTarget.valid();
    if (knob) {
        std::string turns = deviceActions::infoOf(m->song(), lane->paramTarget)
                                ? "Turns " + deviceActions::paramName(m->song(), lane->paramTarget)
                                : "Its knob was deleted (target missing)";
        menu->addChild(rack::createMenuLabel(turns));
    } else {
        menu->addChild(rack::createSubmenuItem(
            "Output", "AUTO " + std::to_string(lane->outputIndex + 1), [edit, lane](rack::ui::Menu* sub) {
                for (int o = 0; o < kMaxAutomationLanes; o++)
                    sub->addChild(rack::createCheckMenuItem(
                        "AUTO channel " + std::to_string(o + 1), "",
                        [o, current = lane->outputIndex] { return current == o; },
                        edit("Set lane output", [o](AutomationEnvelope& l) { l.outputIndex = o; })));
            }));
        char range[48];
        snprintf(range, sizeof(range), "%g to %g V", double(lane->rangeMin), double(lane->rangeMax));
        menu->addChild(rack::createSubmenuItem("Voltage range", range, [edit, lane](rack::ui::Menu* sub) {
            for (const VoltageRange& r : voltageRanges())
                sub->addChild(rack::createCheckMenuItem(
                    r.name, "", [r, lo = lane->rangeMin, hi = lane->rangeMax] { return lo == r.min && hi == r.max; },
                    edit(std::string("Range ") + r.name, [r](AutomationEnvelope& l) {
                        l.rangeMin = r.min;
                        l.rangeMax = r.max;
                        l.bipolar = r.bipolar;
                    })));
        }));
        menu->addChild(rack::createCheckMenuItem(
            "Bipolar (rests in the middle)", "", [b = lane->bipolar] { return b; },
            edit("Toggle bipolar", [](AutomationEnvelope& l) { l.bipolar = !l.bipolar; })));
        menu->addChild(
            rack::createSubmenuItem("Slew", lane->slewMs > 0 ? std::to_string(int(lane->slewMs)) + " ms" : "Off",
                                    [edit, lane](rack::ui::Menu* sub) {
                                        for (float ms : {0.f, 2.f, 5.f, 10.f, 25.f, 50.f, 100.f})
                                            sub->addChild(rack::createCheckMenuItem(
                                                ms == 0 ? "Off" : std::to_string(int(ms)) + " ms", "",
                                                [ms, cur = lane->slewMs] { return cur == ms; },
                                                edit("Set slew", [ms](AutomationEnvelope& l) { l.slewMs = ms; })));
                                    }));
    }
    if (owner.clip.valid())
        menu->addChild(rack::createSubmenuItem(
            "With the track's lane", lane->combine == CombineMode::Override ? "Override" : "Modulate",
            [edit, lane](rack::ui::Menu* sub) {
                sub->addChild(rack::createCheckMenuItem(
                    "Override (the clip wins)", "", [m = lane->combine] { return m == CombineMode::Override; },
                    edit("Clip lane overrides", [](AutomationEnvelope& l) { l.combine = CombineMode::Override; })));
                sub->addChild(rack::createCheckMenuItem(
                    "Modulate (added around the middle)", "",
                    [m = lane->combine] { return m == CombineMode::Modulate; },
                    edit("Clip lane modulates", [](AutomationEnvelope& l) { l.combine = CombineMode::Modulate; })));
            }));
    menu->addChild(rack::createCheckMenuItem(
        "Mute", "", [mute = lane->mute] { return mute; },
        edit("Toggle lane mute", [](AutomationEnvelope& l) { l.mute = !l.mute; })));
    if (owner.track.valid()) {
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuLabel("Recording (while the transport records)"));
        menu->addChild(rack::createCheckMenuItem(
            "Armed", "", [armed = lane->armed] { return armed; },
            edit("Toggle lane arm", [](AutomationEnvelope& l) { l.armed = !l.armed; })));
        std::string cv = "AUTO REC input, channel " + std::to_string(lane->outputIndex + 1);
        if (!knob)
            menu->addChild(rack::createSubmenuItem(
                "Record from", lane->source == AutoSource::Knob ? "Mapped knob" : "AUTO REC input",
                [edit, lane, cv](rack::ui::Menu* sub) {
                    sub->addChild(rack::createCheckMenuItem(
                        "The first mapped knob (turn it by hand)", "",
                        [s = lane->source] { return s == AutoSource::Knob; },
                        edit("Record from the knob", [](AutomationEnvelope& l) { l.source = AutoSource::Knob; })));
                    sub->addChild(rack::createCheckMenuItem(
                        cv, "", [s = lane->source] { return s == AutoSource::Cv; },
                        edit("Record from AUTO REC", [](AutomationEnvelope& l) { l.source = AutoSource::Cv; })));
                }));
        static const char* const modes[] = {"Touch (while it moves)", "Latch (from the first move)",
                                            "Write (all the time)"};
        static const char* const modeNames[] = {"Touch", "Latch", "Write"};
        menu->addChild(
            rack::createSubmenuItem("Record mode", modeNames[int(lane->recordMode)], [edit, lane](rack::ui::Menu* sub) {
                for (int i = 0; i < 3; i++) {
                    AutoRecordMode mode = AutoRecordMode(i);
                    sub->addChild(rack::createCheckMenuItem(
                        modes[i], "", [cur = lane->recordMode, mode] { return cur == mode; },
                        edit("Set record mode", [mode](AutomationEnvelope& l) { l.recordMode = mode; })));
                }
            }));
    }

    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Points (the selected ones, or all)"));
    auto function = [&](const char* name, std::function<Points(const Points&, const Selection&)> f) {
        menu->addChild(rack::createMenuItem(
            name, "", edit(name, [f, targets](AutomationEnvelope& l) { l.points = f(l.points, targets(l)); })));
    };
    function("Thin", [](const Points& p, const Selection&) { return autoEdits::thin(p, 0.01f); });
    function("Quantize times to the beat",
             [](const Points& p, const Selection& s) { return autoEdits::quantizeTimes(p, s, kPpq / 4); });
    function("Invert", [](const Points& p, const Selection& s) { return autoEdits::invert(p, s); });
    function("Reverse", [](const Points& p, const Selection& s) { return autoEdits::reverse(p, s); });
    function("Randomize (±10%)", [](const Points& p, const Selection& s) {
        return autoEdits::randomize(p, s, 0.1f, uint32_t(rack::random::u32()));
    });
    function("Clear", [](const Points& p, const Selection& s) { return autoEdits::deletePoints(p, s); });
    menu->addChild(rack::createSubmenuItem("Stamp a shape", "drag across the lane", [c](rack::ui::Menu* sub) {
        for (int i = 0; i <= int(autoEdits::Shape::ExpFall); i++) {
            autoEdits::Shape shape = autoEdits::Shape(i);
            sub->addChild(
                rack::createMenuItem(autoEdits::shapeName(shape), "", [c, shape] { c->state().stampShape = shape; }));
        }
    }));

    menu->addChild(new rack::ui::MenuSeparator);
    if (owner.track.valid() && !knob) {
        menu->addChild(rack::createMenuLabel("Knobs this lane turns (no cable needed)"));
        auto* learn = rack::createMenuItem("Map a knob: touch it in the rack", "", [c, owner, id] {
            if (TesseraModule* m = c->module())
                m->startLearn(owner.track, id);
        });
        learn->disabled = lane->targets.size() >= 4;
        menu->addChild(learn);
        for (size_t k = 0; k < lane->targets.size(); k++) {
            ParamTarget target = lane->targets[k];
            std::string name = TesseraModule::targetName(target);
            if (m->targetTakenOver(id, int(k)))
                name += " (taken over)";
            menu->addChild(rack::createSubmenuItem(name, "", [edit, k, target](rack::ui::Menu* sub) {
                struct Span {
                    const char* name;
                    float min, max;
                };
                static const Span spans[] = {{"Full range", 0, 1},
                                             {"Lower half", 0, 0.5f},
                                             {"Upper half", 0.5f, 1},
                                             {"Middle half", 0.25f, 0.75f},
                                             {"Reversed", 1, 0}};
                sub->addChild(rack::createMenuLabel("How far the lane turns it"));
                for (const Span& span : spans)
                    sub->addChild(rack::createCheckMenuItem(
                        span.name, "", [target, span] { return target.min == span.min && target.max == span.max; },
                        edit("Set mapped range", [k, span](AutomationEnvelope& l) {
                            if (k < l.targets.size()) {
                                l.targets[k].min = span.min;
                                l.targets[k].max = span.max;
                            }
                        })));
                sub->addChild(new rack::ui::MenuSeparator);
                sub->addChild(
                    rack::createMenuItem("Remove mapping", "", edit("Remove mapping", [k](AutomationEnvelope& l) {
                                             if (k < l.targets.size())
                                                 l.targets.erase(l.targets.begin() + long(k));
                                         })));
            }));
        }
    } else {
        menu->addChild(rack::createMenuLabel("Knobs are mapped on the track's lane for this AUTO channel"));
    }

    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Delete lane", "", [c, owner, id] {
        if (TesseraModule* m = c->module())
            m->perform(removeLane(m->song(), owner, id));
    }));
}

// ---- Drawing ------------------------------------------------------------------------------------------------

void AutomationEditor::draw(NVGcontext* vg, const LaneFrame& f) {
    const AutomationEnvelope* l = lane(f);
    if (!l)
        return;
    const theme::Palette& c = theme::colors();
    const Points& p = points(f);
    Rect r = f.rect;
    float rest = restValue(*l);
    NVGcolor color = l->mute ? c.text2 : f.color;

    nvgSave(vg);
    nvgIntersectScissor(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
    if (l->bipolar) {
        nvgBeginPath(vg);
        nvgRect(vg, r.pos.x, yOf(f, 0.5f), r.size.x, 1);
        nvgFillColor(vg, c.line0);
        nvgFill(vg);
    }

    // The curve, sampled every 2 px; the area under it (from the rest line) faintly filled.
    float zero = yOf(f, rest);
    size_t cursor = 0;
    nvgBeginPath(vg);
    bool first = true;
    for (float x = r.pos.x; x <= r.pos.x + r.size.x + 2; x += 2) {
        float y = yOf(f, valueOf(p, tickAt(f, x), rest, cursor));
        if (first) {
            nvgMoveTo(vg, x, zero);
            first = false;
        }
        nvgLineTo(vg, x, y);
    }
    nvgLineTo(vg, r.pos.x + r.size.x + 2, zero);
    nvgClosePath(vg);
    nvgFillColor(vg, nvgTransRGBAf(color, 0.12f));
    nvgFill(vg);

    cursor = 0;
    nvgBeginPath(vg);
    first = true;
    for (float x = r.pos.x; x <= r.pos.x + r.size.x + 2; x += 2) {
        float y = yOf(f, valueOf(p, tickAt(f, x), rest, cursor));
        first ? nvgMoveTo(vg, x, y) : nvgLineTo(vg, x, y);
        first = false;
    }
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);

    Selection selected = selection(f);
    for (size_t i = 0; i < p.size(); i++) {
        float x = xOf(f, double(p[i].t));
        if (x < r.pos.x - 8 || x > r.pos.x + r.size.x + 8)
            continue;
        float y = yOf(f, p[i].value);
        bool hot = hovered.lane == f.lane && hovered.point == int(i);
        nvgBeginPath(vg);
        nvgCircle(vg, x, y, kPointRadius + (hot ? 1 : 0));
        if (selected.count(i)) {
            nvgFillColor(vg, c.select);
            nvgFill(vg);
        } else {
            nvgFillColor(vg, c.bg0);
            nvgFill(vg);
            nvgStrokeColor(vg, color);
            nvgStrokeWidth(vg, 1.5f);
            nvgStroke(vg);
        }
    }
    // What is being recorded into this lane right now, in red, over the old curve.
    if (TesseraModule* m = ctx.module())
        if (const AutomationTake* take = m->automationTake(f.lane)) {
            for (const Points& run : take->written()) {
                nvgBeginPath(vg);
                for (size_t i = 0; i < run.size(); i++) {
                    float x = xOf(f, double(run[i].t)), y = yOf(f, run[i].value);
                    i == 0 ? nvgMoveTo(vg, x, y) : nvgLineTo(vg, x, y);
                }
                nvgStrokeColor(vg, c.record);
                nvgStrokeWidth(vg, 2);
                nvgStroke(vg);
            }
        }
    // The transform box and its handles.
    Rect box = selectionBox(f);
    if (box.size.x > 0) {
        nvgBeginPath(vg);
        nvgRect(vg, box.pos.x - 0.5f, box.pos.y - 0.5f, box.size.x + 1, box.size.y + 1);
        nvgStrokeColor(vg, nvgTransRGBAf(c.select, 0.6f));
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
        const float out = 7;
        float l = box.pos.x, r = box.getRight(), t = box.pos.y, b = box.getBottom();
        Vec handles[] = {Vec(l - out, t - out),           Vec(r + out, t - out),
                         Vec(box.getCenter().x, t - out), Vec(box.getCenter().x, b + out),
                         Vec(l - out, box.getCenter().y), Vec(r + out, box.getCenter().y)};
        for (Vec hp : handles) {
            nvgBeginPath(vg);
            nvgRect(vg, hp.x - 3, hp.y - 3, 6, 6);
            nvgFillColor(vg, c.bg0);
            nvgFill(vg);
            nvgStrokeColor(vg, c.select);
            nvgStroke(vg);
        }
    }
    // The bend handle of the hovered segment.
    if (hovered.lane == f.lane && hovered.segment >= 0 && size_t(hovered.segment) + 1 < p.size() &&
        p[size_t(hovered.segment)].curve != CurveType::Hold) {
        size_t i = size_t(hovered.segment);
        double mid = (double(p[i].t) + double(p[i + 1].t)) / 2;
        size_t cur = i;
        nvgBeginPath(vg);
        nvgCircle(vg, xOf(f, mid), yOf(f, valueOf(p, mid, rest, cur)), 2.5f);
        nvgFillColor(vg, hovered.handle >= 0 ? c.text0 : c.text1);
        nvgFill(vg);
    }
    // A dot rides the curve at the playhead.
    if (std::isfinite(f.playhead)) {
        size_t cur = 0;
        float x = xOf(f, f.playhead);
        if (x >= r.pos.x && x <= r.pos.x + r.size.x) {
            nvgBeginPath(vg);
            nvgCircle(vg, x, yOf(f, valueOf(p, f.playhead, rest, cur)), 3);
            nvgFillColor(vg, c.accent);
            nvgFill(vg);
        }
    }

    // Value scale at the left edge (volts, or the knob's own units), and a hint on empty lanes.
    draw::TextStyle scale{theme::fonts::mono(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
    std::string hi = valueText(f, 1), lo = valueText(f, 0);
    if (drivesOutput(*l)) {
        char volts[16];
        snprintf(volts, sizeof(volts), "%g V", double(l->rangeMax));
        hi = volts;
        snprintf(volts, sizeof(volts), "%g V", double(l->rangeMin));
        lo = volts;
    }
    if (r.size.y >= 30) {
        draw::text(vg, scale, r.pos.x + 3, r.pos.y + 2, hi);
        scale.align = NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM;
        draw::text(vg, scale, r.pos.x + 3, r.pos.y + r.size.y - 2, lo);
    }
    if (p.empty() && r.size.y >= 24) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2,
                             NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, r.pos.x + r.size.x / 2, r.pos.y + r.size.y / 2,
                   ctx.state().stampShape ? "Drag across the lane to stamp the shape"
                                          : "Click to add a point, or draw with D");
    }
    nvgRestore(vg);
}

} // namespace tessera
