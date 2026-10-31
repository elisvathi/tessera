#include "CurveView.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/Automation.hpp"
#include "../../model/DeviceLookup.hpp"
#include "../../model/Modulators.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/DeviceActions.hpp"
#include "../interaction/Navigation.hpp"
#include "../widgets/Draw.hpp"
#include "PianoRoll.hpp"
#include "TimelineDraw.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kPad = 6;
constexpr float kHit = 6;
constexpr float kGripHit = 8;
constexpr int kRowsEachWay = 7;    // rows shown either side of the knob as set, when a curve opens
constexpr float kRulerLabels = 12; // the ruler's top row, with the positions; the segment is under it

bool discrete(const devices::ParamInfo& info) {
    using K = devices::ParamInfo::Kind;
    return info.kind == K::Integer || info.kind == K::Choice || info.kind == K::Toggle;
}

/// The display values a stepped setting can take, lowest first.
std::vector<float> stepsOf(const devices::ParamInfo& info) {
    std::vector<float> out;
    if (info.kind == devices::ParamInfo::Kind::Choice) {
        for (size_t i = 0; i < info.choices.size(); i++)
            out.push_back(float(i));
    } else if (info.kind == devices::ParamInfo::Kind::Toggle) {
        out = {0, 1};
    } else {
        for (float d = info.min; d <= info.max + 0.5f; d += 1)
            out.push_back(d);
    }
    return out;
}

/// Whether a point is a Step (holds until the next point) rather than a Ramp.
bool isStep(const AutoPoint& p) { return p.curve == CurveType::Hold; }

/// Ready shapes, over the segment and across the rows shown: (place in the segment, value) pairs.
struct Shape {
    const char* name;
    std::vector<std::pair<double, float>> points;
    CurveType curve;
};
const Shape kShapes[] = {{"Rise", {{0, 0}, {1, 1}}, CurveType::Linear},
                         {"Fall", {{0, 1}, {1, 0}}, CurveType::Linear},
                         {"Up and down", {{0, 0.5f}, {0.25, 1}, {0.75, 0}, {1, 0.5f}}, CurveType::Linear},
                         {"Two steps", {{0, 0.5f}, {0.5, 1}}, CurveType::Hold}};

/// Dragging a point, or an end of the segment, or the whole segment. One undo step on release.
class CurveDrag : public DragGesture {
  public:
    enum class Mode { Point, SegmentStart, SegmentEnd, Segment };
    CurveDrag(CurveView& view, CurveDrawing drawing, Mode mode, int index, double grabbed)
        : view(view), drawing(std::move(drawing)), start(this->drawing), mode(mode), index(index), grabbed(grabbed) {
        view.preview = this->drawing;
    }
    void update(Vec pos, Modifiers mods) override {
        Tick snap = mods.alt ? 1 : view.snap();
        double t = view.tAt(pos.x);
        auto snapped = [&](double v) { return Tick(std::llround(v / double(snap))) * snap; };
        Tick shortest = std::max<Tick>(snap, kPpq / 4);
        switch (mode) {
        case Mode::Point: {
            // A point stays between its neighbours, so dragging never reorders the curve.
            Tick lo = index > 0 ? drawing.points[size_t(index) - 1].t : 0;
            Tick hi = index + 1 < int(drawing.points.size()) ? drawing.points[size_t(index) + 1].t : kMaxCurveLength;
            drawing.points[size_t(index)].t = std::clamp(snapped(t), lo, hi);
            drawing.points[size_t(index)].value = view.valueAt(pos.y, mods.alt);
            break;
        }
        case Mode::SegmentStart:
            drawing.segmentStart = std::clamp(snapped(t), Tick(0), start.segmentEnd - shortest);
            break;
        case Mode::SegmentEnd:
            drawing.segmentEnd = std::clamp(snapped(t), start.segmentStart + shortest, start.length);
            break;
        case Mode::Segment: {
            Tick span = start.segmentEnd - start.segmentStart;
            Tick from = std::clamp(start.segmentStart + snapped(t - grabbed), Tick(0), start.length - span);
            drawing.segmentStart = from;
            drawing.segmentEnd = from + span;
            break;
        }
        }
        view.preview = drawing;
        moved = true;
    }
    void commit() override {
        view.preview.reset();
        if (moved || fresh)
            view.commit(drawing);
    }
    void cancel() override { view.preview.reset(); }
    std::string readout() const override {
        if (mode == Mode::Point)
            return view.format(drawing.points[size_t(index)].value);
        return "Plays " + view.lengthLabel(drawing.segmentEnd - drawing.segmentStart) + ", from " +
               timeline::positionText(double(drawing.segmentStart),
                                      view.module() ? view.module()->song().timeSig : TimeSig());
    }
    CursorShape cursor() const override { return mode == Mode::Point ? CursorShape::Move : CursorShape::ResizeH; }
    bool fresh = false;

  private:
    CurveView& view;
    CurveDrawing drawing;
    CurveDrawing start;
    Mode mode;
    int index;
    double grabbed; // Segment: where in the drawing the press was
    bool moved = false;
};

} // namespace

const Modulator* CurveView::modulator(ModulatorPlace* place) {
    TesseraModule* m = ctx.module();
    if (!m || !ctx.curveModulator)
        return nullptr;
    const Modulator* mod = findModulator(m->song(), ModulatorId(ctx.curveModulator), place);
    return mod && mod->type == ModulatorType::Curve ? mod : nullptr;
}

ParamRef CurveView::target() {
    if (const Modulator* mod = modulator()) {
        int r = mods::drawnRoute(*mod);
        return r >= 0 ? mod->routes[size_t(r)].target : ParamRef();
    }
    return ctx.curveTarget;
}

const devices::ParamInfo* CurveView::targetInfo(float* base, float* amount) {
    TesseraModule* m = ctx.module();
    ParamRef ref = target();
    if (!m || !ref.valid())
        return nullptr;
    const float* value = findParam(m->song(), ref);
    const devices::ParamInfo* info = deviceActions::infoOf(m->song(), ref);
    if (!value || !info)
        return nullptr;
    if (base)
        *base = *value;
    if (amount) {
        *amount = 1;
        if (const Modulator* mod = modulator())
            if (int r = mods::drawnRoute(*mod); r >= 0)
                *amount = mod->routes[size_t(r)].amount;
    }
    return info;
}

bool CurveView::stepped() {
    float amount = 1;
    const devices::ParamInfo* info = targetInfo(nullptr, &amount);
    return info && discrete(*info) && std::fabs(amount) > 1e-4f;
}

bool CurveView::hasCurve() { return modulator() || (ctx.curveTarget.valid() && targetInfo()); }

std::string CurveView::title() {
    if (const Modulator* mod = modulator())
        return mod->name;
    TesseraModule* m = ctx.module();
    return m && ctx.curveTarget.valid() ? deviceActions::paramName(m->song(), ctx.curveTarget) : "Curve";
}

double CurveView::spanStart() {
    const Clip* c = owner.clip();
    const Track* t = owner.track();
    if (!c || !t)
        return 0;
    return double(clipSpan(*c, t->type == TrackType::Drum ? &t->drumLanes : nullptr).start);
}

CurveDrawing CurveView::shown() {
    if (preview)
        return *preview;
    if (const Modulator* mod = modulator())
        return {mod->length, mod->points, mod->segmentStart, mod->segmentEnd};
    // A new curve is as long as the clip (whole bars), so it first sits under the notes.
    Tick bar = std::max<Tick>(1, ticksPerBar(owner.timeSig()));
    const Clip* clip = owner.clip();
    Tick span = clip ? std::max<Tick>(kPpq, clip->playEnd() - clip->playStart()) : bar;
    Tick length = std::max<Tick>(1, (span + bar - 1) / bar) * bar;
    return {length, {}, 0, length};
}

void CurveView::commit(CurveDrawing drawing) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    if (const Modulator* mod = modulator()) {
        m->perform(setCurve(m->song(), mod->id, std::move(drawing)));
        return;
    }
    const Track* t = owner.track();
    ParamRef ref = ctx.curveTarget;
    if (!t || !ref.valid())
        return;
    // Named after what it moves: "Melody Rate", "Echo Time".
    std::string name = deviceActions::paramName(m->song(), ref);
    for (size_t at = name.find(" · "); at != std::string::npos; at = name.find(" · "))
        name.replace(at, std::string(" · ").size(), " ");
    if (ref.kind == ParamRef::Kind::Playhead)
        if (const Playhead* p = findPlayhead(m->song(), PlayheadId(ref.owner)))
            if (const devices::ParamInfo* info = devices::playheadParamInfo(ref.param))
                name = p->name + " " + info->name;
    ModulatorId added;
    CommandPtr add = animateWithCurve(m->song(), t->id, ref, name, std::move(drawing), m->ids(), &added);
    if (!add) {
        ctx.toast("This track has no room for another modulator");
        return;
    }
    m->perform(std::move(add));
    deviceActions::showCurve(ctx, added);
    // The new modulator is the same curve: keep the view where it is.
    fittedModulator = added.value;
}

float CurveView::xOf(double t) { return left + float(time.tickToX(t)); }

double CurveView::tAt(float x) { return time.xToTick(x - left); }

float CurveView::yOf(float value) const {
    float top = kHead + kBar + kPad, bottom = std::max(top + 1, box.size.y - kPad);
    return bottom - (value - low) / std::max(1e-6f, high - low) * (bottom - top);
}

std::pair<float, float> CurveView::reachable() {
    float base = 0, amount = 1;
    if (!targetInfo(&base, &amount) || std::fabs(amount) < 1e-4f)
        return {0, 1};
    // The setting reads base + amount * (2v - 1), kept in 0..1.
    float a = 0.5f - base / (2 * amount), b = 0.5f + (1 - base) / (2 * amount);
    if (a > b)
        std::swap(a, b);
    return {std::clamp(a, 0.f, 1.f), std::clamp(b, 0.f, 1.f)};
}

std::vector<float> CurveView::rowValues() {
    float base = 0, amount = 1;
    const devices::ParamInfo* info = targetInfo(&base, &amount);
    std::vector<float> rows;
    if (!info || !discrete(*info) || std::fabs(amount) < 1e-4f)
        return rows;
    for (float d : stepsOf(*info)) {
        float v = ((devices::normalize(*info, d) - base) / amount + 1) / 2;
        if (v >= -1e-4f && v <= 1 + 1e-4f)
            rows.push_back(std::clamp(v, 0.f, 1.f));
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

float CurveView::heard(float v) {
    float base = 0, amount = 1;
    const devices::ParamInfo* info = targetInfo(&base, &amount);
    if (!info || !discrete(*info) || std::fabs(amount) < 1e-4f)
        return v;
    float n = std::clamp(base + amount * (2 * v - 1), 0.f, 1.f);
    float snapped = devices::normalize(*info, std::round(devices::display(*info, n)));
    return std::clamp(((snapped - base) / amount + 1) / 2, 0.f, 1.f);
}

float CurveView::valueAt(float y, bool free) {
    float top = kHead + kBar + kPad, bottom = std::max(top + 1, box.size.y - kPad);
    float v = low + (bottom - y) / std::max(1.f, bottom - top) * (high - low);
    auto [a, b] = reachable();
    v = std::clamp(v, a, b);
    return free ? v : heard(v);
}

std::string CurveView::format(float v) {
    float base = 0, amount = 1;
    const devices::ParamInfo* info = targetInfo(&base, &amount);
    if (!info)
        return std::to_string(int(std::lround(v * 100))) + "%";
    return devices::format(*info, std::clamp(base + amount * (2 * v - 1), 0.f, 1.f));
}

Tick CurveView::snap() { return std::clamp<Tick>(owner.snapStep(), 1, kPpq); }

CurveType CurveView::kindAt(const std::vector<AutoPoint>& points, Tick t) {
    const AutoPoint* before = nullptr;
    for (const AutoPoint& p : points)
        if (p.t <= t)
            before = &p;
    if (before)
        return isStep(*before) ? CurveType::Hold : CurveType::Linear;
    return stepped() ? CurveType::Hold : CurveType::Linear;
}

int CurveView::pointAt(Vec pos) {
    CurveDrawing d = shown();
    for (size_t i = 0; i < d.points.size(); i++)
        if (std::fabs(xOf(double(d.points[i].t)) - pos.x) <= kHit && std::fabs(yOf(d.points[i].value) - pos.y) <= kHit)
            return int(i);
    return -1;
}

void CurveView::fit() {
    CurveDrawing d = shown();
    // Across: the piano roll's zoom, with the segment's start under the clip's start, as it plays.
    time.setZoom(owner.viewport.time.pxPerTick(), 0);
    time.scrollTo(double(d.segmentStart) + owner.viewport.time.left() - spanStart());
    // Up and down: about seven rows either side of the knob as set, or the whole range it can reach.
    auto [a, b] = reachable();
    low = a;
    high = b;
    std::vector<float> rows = rowValues();
    if (rows.size() > size_t(2 * kRowsEachWay + 1)) {
        int middle = int(std::min_element(rows.begin(), rows.end(),
                                          [](float x, float y) { return std::fabs(x - 0.5f) < std::fabs(y - 0.5f); }) -
                         rows.begin());
        int first = std::clamp(middle - kRowsEachWay, 0, int(rows.size()) - (2 * kRowsEachWay + 1));
        float half = (rows[1] - rows[0]) / 2;
        low = rows[size_t(first)] - half;
        high = rows[size_t(first + 2 * kRowsEachWay)] + half;
    } else if (rows.size() > 1) {
        float half = (rows[1] - rows[0]) / 2;
        low = rows.front() - half;
        high = rows.back() + half;
    }
}

void CurveView::fitAll() {
    CurveDrawing d = shown();
    time.showRange(0, double(d.length));
}

void CurveView::zoomValues(float y, float factor) {
    auto [a, b] = reachable();
    std::vector<float> rows = rowValues();
    float half = rows.size() > 1 ? (rows[1] - rows[0]) / 2 : 0;
    float lo = a - half, hi = b + half;
    float smallest = rows.size() > 1 ? 6 * half : 0.02f; // three rows, or 2% of the range
    float at = valueAt(y, true);
    float span = std::clamp((high - low) / factor, smallest, hi - lo);
    float k = (at - low) / std::max(1e-6f, high - low);
    low = at - k * span;
    high = low + span;
    if (low < lo) {
        high += lo - low;
        low = lo;
    }
    if (high > hi) {
        low -= high - hi;
        high = hi;
    }
}

void CurveView::scrollValues(float dy) {
    auto [a, b] = reachable();
    std::vector<float> rows = rowValues();
    float half = rows.size() > 1 ? (rows[1] - rows[0]) / 2 : 0;
    float lo = a - half, hi = b + half;
    float body = std::max(1.f, box.size.y - kHead - kBar - 2 * kPad);
    float shift = dy / body * (high - low);
    shift = std::clamp(shift, lo - low, hi - high);
    low += shift;
    high += shift;
}

void CurveView::step() {
    CurveDrawing d = shown();
    double width = std::max(1.f, box.size.x - left);
    time.setViewWidth(width);
    time.setZoomLimits(width / (double(kMaxCurveLength) * 1.1), width / double(kPpq / 4));
    time.setContentEnd(double(std::max(d.length, d.segmentEnd)) * 1.1 + double(ticksPerBar(owner.timeSig())));
    // A different curve (or a new setting waiting for one) opens lined up under the notes.
    uint64_t id = modulator() ? modulator()->id.value : 0;
    ParamRef ref = target();
    if (hasCurve() && visible && (!fitted || id != fittedModulator || !(ref == fittedTarget))) {
        fit();
        fitted = true;
        fittedModulator = id;
        fittedTarget = ref;
    }
    InteractiveView::step();
}

double CurveView::sinceClipStart() {
    TesseraModule* m = ctx.module();
    ModulatorPlace place;
    const Modulator* mod = modulator(&place);
    if (!m || !m->playing())
        return NAN;
    double song = m->playheadTicks();
    if (mod && place.level == ModLevel::Song)
        return song;
    const Track* track = m->song().findTrack(mod ? place.track : owner.track() ? owner.track()->id : TrackId());
    if (!track)
        return NAN;
    LauncherStatus status = m->launcherStatus(track->id);
    if (status.playing >= 0 && std::isfinite(status.elapsed))
        return status.elapsed;
    if (m->playMode() == PlayMode::Arranger && !status.overridden)
        if (const ArrangerClip* a = instanceAt(*track, Tick(song)); a && !a->muted)
            return song - double(a->position);
    return NAN;
}

std::unique_ptr<DragGesture> CurveView::startGesture(Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || !hasCurve())
        return nullptr;
    CurveDrawing d = shown();
    if (pos.y < kHead) // the header's buttons act on click
        return nullptr;
    if (pos.y < kHead + kBar) {
        float x0 = xOf(double(d.segmentStart)), x1 = xOf(double(d.segmentEnd));
        if (pos.y < kHead + kRulerLabels)
            return nullptr;
        if (std::fabs(pos.x - x0) <= kGripHit)
            return std::make_unique<CurveDrag>(*this, d, CurveDrag::Mode::SegmentStart, -1, 0);
        if (std::fabs(pos.x - x1) <= kGripHit)
            return std::make_unique<CurveDrag>(*this, d, CurveDrag::Mode::SegmentEnd, -1, 0);
        if (pos.x > x0 && pos.x < x1)
            return std::make_unique<CurveDrag>(*this, d, CurveDrag::Mode::Segment, -1, tAt(pos.x));
        return nullptr;
    }
    if (pos.x < left) // the row labels
        return nullptr;
    double t = tAt(pos.x);
    int index = pointAt(pos);
    bool fresh = index < 0;
    if (fresh) {
        if (t < 0 || t > double(kMaxCurveLength))
            return nullptr;
        Tick at = std::clamp<Tick>(Tick(std::llround(t / double(snap()))) * snap(), 0, kMaxCurveLength);
        // The first point keeps the setting where it is: the curve starts from no change.
        if (d.points.empty() && at > 0)
            d.points.push_back({0, 0.5f, kindAt(d.points, 0), 0});
        AutoPoint pt{at, valueAt(pos.y, mods.alt), kindAt(d.points, at), 0};
        auto it = std::upper_bound(d.points.begin(), d.points.end(), pt.t,
                                   [](Tick v, const AutoPoint& q) { return v < q.t; });
        index = int(it - d.points.begin());
        d.points.insert(it, pt);
    }
    auto drag = std::make_unique<CurveDrag>(*this, d, CurveDrag::Mode::Point, index, 0);
    drag->fresh = fresh;
    return drag;
}

void CurveView::doubleClick(Vec pos, Modifiers mods) {
    if (pos.y >= kHead && pos.y < kHead + kBar) {
        fitAll();
        return;
    }
    int index = pointAt(pos);
    if (index < 0 || !modulator())
        return;
    CurveDrawing d = shown();
    d.points.erase(d.points.begin() + index);
    commit(d);
}

void CurveView::click(Vec pos, int button, Modifiers mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        int index = pos.y >= kHead + kBar ? pointAt(pos) : -1;
        if (index >= 0 && modulator())
            openPointMenu(index);
        else
            openMenu();
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    for (const HeaderPart& part : headerParts()) {
        if (!part.rect.contains(pos))
            continue;
        switch (part.kind) {
        case HeaderPart::Kind::Length:
            openLengthMenu();
            break;
        case HeaderPart::Kind::Shape:
            applyShape(part.shape);
            break;
        case HeaderPart::Kind::Remove:
            if (const Modulator* mod = modulator())
                if (TesseraModule* m = ctx.module())
                    m->perform(removeModulator(m->song(), mod->id));
            break;
        }
        return;
    }
}

void CurveView::wheel(Vec pos, Vec delta, Modifiers mods) {
    ctx.touchNavigation();
    if (mods.ctrl)
        time.zoomAt(pos.x - left, wheelZoomFactor(delta.y));
    else if (mods.alt)
        zoomValues(pos.y, float(wheelZoomFactor(delta.y)));
    else if (mods.shift || std::fabs(delta.x) > std::fabs(delta.y))
        time.scrollByPx(-(delta.y + delta.x));
    else
        scrollValues(delta.y);
}

std::vector<CurveView::HeaderPart> CurveView::headerParts() {
    std::vector<HeaderPart> parts;
    float y = 3, h = kHead - 6;
    float x = left + kPad + 44; // "Length"
    parts.push_back({HeaderPart::Kind::Length, Rect(x, y, 70, h)});
    x += 70 + 16 + 46; // "Shapes"
    for (int i = 0; i < int(std::size(kShapes)); i++)
        parts.push_back({HeaderPart::Kind::Shape, Rect(x + float(i) * 30, y, 26, h), i});
    if (modulator())
        parts.push_back({HeaderPart::Kind::Remove, Rect(box.size.x - kPad - 96, y, 96, h)});
    return parts;
}

void CurveView::applyShape(int index) {
    if (index < 0 || index >= int(std::size(kShapes)))
        return;
    CurveDrawing d = shown();
    // Across the segment that plays, from the lowest row shown to the highest.
    double from = double(d.segmentStart), span = double(d.segmentEnd - d.segmentStart);
    float lo = std::max(low, reachable().first), hi = std::min(high, reachable().second);
    d.points.clear();
    for (auto [at, v] : kShapes[index].points)
        d.points.push_back({Tick(std::llround(from + at * span)), heard(lo + v * (hi - lo)), kShapes[index].curve, 0});
    commit(d);
}

void CurveView::setLength(Tick length) {
    CurveDrawing d = shown();
    // A segment playing the whole drawing keeps doing so; any other stays where it is (cut to fit).
    bool whole = d.segmentStart == 0 && d.segmentEnd == d.length;
    d.length = length;
    if (whole)
        d.segmentEnd = length;
    commit(d);
}

std::string CurveView::lengthLabel(Tick length) {
    auto plural = [](Tick n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    };
    Tick bar = ticksPerBar(owner.timeSig());
    if (bar > 0 && length % bar == 0)
        return plural(length / bar, "bar", "bars");
    if (length % kPpq == 0)
        return plural(length / kPpq, "beat", "beats");
    return plural(std::max<Tick>(1, length / (kPpq / 4)), "sixteenth", "sixteenths");
}

void CurveView::openLengthMenu() {
    CurveView* view = this;
    Tick bar = std::max<Tick>(1, ticksPerBar(owner.timeSig()));
    Tick now = shown().length;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("How long the drawing is"));
    for (int bars : {1, 2, 4, 8, 16, 32, 64, 128, 256}) {
        Tick length = std::min<Tick>(kMaxCurveLength, Tick(bars) * bar);
        menu->addChild(rack::createCheckMenuItem(
            lengthLabel(length), "", [=] { return now == length; }, [view, length] { view->setLength(length); }));
    }
    menu->addChild(rack::createMenuLabel("The segment on the ruler says which part plays"));
}

void CurveView::openPointMenu(int index) {
    CurveView* view = this;
    CurveDrawing d = shown();
    if (index < 0 || index >= int(d.points.size()))
        return;
    bool step = isStep(d.points[size_t(index)]);
    auto setKind = [view, index](CurveType kind) {
        CurveDrawing now = view->shown();
        if (index >= int(now.points.size()))
            return;
        now.points[size_t(index)].curve = kind;
        now.points[size_t(index)].tension = 0;
        view->commit(now);
    };
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(format(d.points[size_t(index)].value)));
    menu->addChild(rack::createCheckMenuItem(
        "Step", "holds until the next point", [step] { return step; }, [setKind] { setKind(CurveType::Hold); }));
    menu->addChild(rack::createCheckMenuItem(
        "Ramp", "glides to the next point", [step] { return !step; }, [setKind] { setKind(CurveType::Linear); }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Delete point", "", [view, index] {
        CurveDrawing now = view->shown();
        if (index < int(now.points.size())) {
            now.points.erase(now.points.begin() + index);
            view->commit(now);
        }
    }));
}

void CurveView::openMenu() {
    if (!hasCurve())
        return;
    EditorContext* c = &ctx;
    CurveView* view = this;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(title()));
    menu->addChild(rack::createSubmenuItem("Shape", "", [=](rack::ui::Menu* sub) {
        for (int i = 0; i < int(std::size(kShapes)); i++)
            sub->addChild(rack::createMenuItem(kShapes[i].name, "", [view, i] { view->applyShape(i); }));
    }));
    menu->addChild(rack::createMenuItem("Show the whole drawing", "", [view] { view->fitAll(); }));
    menu->addChild(rack::createMenuItem("Line up under the notes", "", [view] { view->fit(); }));
    if (const Modulator* mod = modulator()) {
        ModulatorId id = mod->id;
        menu->addChild(rack::createMenuItem("Remove curve", "", [c, id] {
            if (TesseraModule* m = c->module())
                m->perform(removeModulator(m->song(), id));
        }));
    }
    menu->addChild(rack::createMenuLabel("Click to add a point, double-click to delete; Alt: off the grid"));
    menu->addChild(rack::createMenuLabel("Right-click a point: Step or Ramp"));
    menu->addChild(rack::createMenuLabel("Ctrl+wheel zooms across, Alt+wheel up and down"));
}

CursorShape CurveView::hoverCursor(Vec pos, Modifiers mods) {
    if (!hasCurve())
        return CursorShape::Arrow;
    if (pos.y < kHead) {
        for (const HeaderPart& part : headerParts())
            if (part.rect.contains(pos))
                return CursorShape::Hand;
        return CursorShape::Arrow;
    }
    if (pos.y < kHead + kBar) {
        CurveDrawing d = shown();
        float x0 = xOf(double(d.segmentStart)), x1 = xOf(double(d.segmentEnd));
        if (pos.y < kHead + kRulerLabels)
            return CursorShape::Arrow;
        if (std::fabs(pos.x - x0) <= kGripHit || std::fabs(pos.x - x1) <= kGripHit)
            return CursorShape::ResizeH;
        return pos.x > x0 && pos.x < x1 ? CursorShape::Move : CursorShape::Arrow;
    }
    if (pos.x < left)
        return CursorShape::Arrow;
    return pointAt(pos) >= 0 ? CursorShape::Move : CursorShape::AddPoint;
}

void CurveView::drawHeader(NVGcontext* vg, NVGcolor color) {
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, left, 0, box.size.x - left, kHead);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    draw::TextStyle label{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle choice{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text1,
                           NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    bool labelledShapes = false;
    for (const HeaderPart& part : headerParts()) {
        Rect r = part.rect;
        switch (part.kind) {
        case HeaderPart::Kind::Length: {
            draw::text(vg, label, r.pos.x - 40, r.getCenter().y, "Length");
            draw::roundedRect(vg, r, 3, c.bg2);
            draw::TextStyle value = choice;
            value.color = color;
            value.align = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE;
            draw::text(vg, value, r.pos.x + 7, r.getCenter().y, lengthLabel(shown().length));
            // A small arrow: it opens a list.
            float ax = r.getRight() - 9, ay = r.getCenter().y;
            nvgBeginPath(vg);
            nvgMoveTo(vg, ax - 3, ay - 1.5f);
            nvgLineTo(vg, ax + 3, ay - 1.5f);
            nvgLineTo(vg, ax, ay + 2);
            nvgClosePath(vg);
            nvgFillColor(vg, c.text1);
            nvgFill(vg);
            break;
        }
        case HeaderPart::Kind::Shape: {
            if (!labelledShapes) {
                draw::text(vg, label, r.pos.x - 42, r.getCenter().y, "Shapes");
                labelledShapes = true;
            }
            draw::roundedRect(vg, r, 3, c.bg2);
            // The shape in small, inside its button.
            Rect icon = r.shrink(Vec(5, 4));
            const Shape& shape = kShapes[part.shape];
            nvgBeginPath(vg);
            for (size_t i = 0; i < shape.points.size(); i++) {
                float x = icon.pos.x + float(shape.points[i].first) * icon.size.x;
                float y = icon.getBottom() - shape.points[i].second * icon.size.y;
                if (i == 0) {
                    nvgMoveTo(vg, x, y);
                    continue;
                }
                if (shape.curve == CurveType::Hold) // a step: across at the last value, then up or down
                    nvgLineTo(vg, x, icon.getBottom() - shape.points[i - 1].second * icon.size.y);
                nvgLineTo(vg, x, y);
            }
            if (shape.curve == CurveType::Hold)
                nvgLineTo(vg, icon.getRight(), icon.getBottom() - shape.points.back().second * icon.size.y);
            nvgStrokeColor(vg, c.text1);
            nvgStrokeWidth(vg, 1.3f);
            nvgStroke(vg);
            break;
        }
        case HeaderPart::Kind::Remove:
            draw::roundedRectStroke(vg, r, 3, c.line2, 1);
            choice.color = c.text1;
            draw::text(vg, choice, r.getCenter().x, r.getCenter().y, "Remove curve");
            break;
        }
    }
}

void CurveView::drawRuler(NVGcontext* vg, NVGcolor color, const CurveDrawing& d) {
    const theme::Palette& c = theme::colors();
    TimeSig ts = owner.timeSig();
    nvgBeginPath(vg);
    nvgRect(vg, left, kHead, box.size.x - left, kBar);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);
    nvgSave(vg);
    nvgIntersectScissor(vg, left, kHead, box.size.x - left, kBar);
    // Positions in the drawing, as on the piano roll's ruler.
    GridSpacing grid = gridSpacing(time.pxPerTick(), ts);
    draw::TextStyle label{theme::fonts::mono(), 9, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    for (double t = std::floor(time.left() / double(grid.label)) * double(grid.label); t < time.right();
         t += double(grid.label)) {
        if (t < 0)
            continue;
        float x = xOf(t);
        nvgBeginPath(vg);
        nvgRect(vg, x, kHead + 2, 1, kRulerLabels - 3);
        nvgFillColor(vg, c.line1);
        nvgFill(vg);
        draw::text(vg, label, x + 3, kHead + kRulerLabels / 2 + 1, timeline::positionLabel(Tick(t), grid.label, ts));
    }
    // The segment that plays, under the positions, drawn like a playhead's segment: grips at both ends, the bar
    // between.
    float x0 = xOf(double(d.segmentStart)), x1 = xOf(double(d.segmentEnd));
    float row = kHead + kRulerLabels, rowH = kBar - kRulerLabels;
    float top = row + 3, h = rowH - 6;
    Rect bar(x0, top, std::max(2.f, x1 - x0), h);
    draw::roundedRect(vg, bar, 2, nvgTransRGBAf(color, 0.35f));
    draw::roundedRectStroke(vg, bar, 2, color, 1);
    float grip = theme::metrics::kSegmentGrip;
    draw::roundedRect(vg, Rect(x0 - 2, row + 1, grip, rowH - 2), 1.5f, color);
    draw::roundedRect(vg, Rect(x1 - grip + 2, row + 1, grip, rowH - 2), 1.5f, color);
    draw::TextStyle plays{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text0,
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::string text = "plays " + lengthLabel(d.segmentEnd - d.segmentStart);
    if (x0 + grip + 3 + draw::textWidth(vg, plays, text) < x1 - grip - 3)
        draw::text(vg, plays, std::max(x0, left) + grip + 3, top + h / 2, text);
    nvgRestore(vg);
}

void CurveView::drawRows(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    float top = kHead + kBar, bottom = box.size.y;
    draw::TextStyle label{theme::fonts::mono(), 9.5f, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::vector<float> rows = rowValues();
    std::vector<std::pair<float, std::string>> labels;
    if (rows.size() > 1) {
        float spacing = std::fabs(yOf(rows[0]) - yOf(rows[1]));
        int every = std::max(1, int(std::ceil(13 / std::max(1.f, spacing))));
        // Labels count out from the row as set, so it always has one.
        int middle = int(std::min_element(rows.begin(), rows.end(),
                                          [](float a, float b) { return std::fabs(a - 0.5f) < std::fabs(b - 0.5f); }) -
                         rows.begin());
        for (size_t i = 0; i < rows.size(); i++) {
            float y = yOf(rows[i]);
            if (y < top - spacing || y > bottom + spacing)
                continue;
            if ((int(i) - middle) % 2 == 0) {
                nvgBeginPath(vg);
                nvgRect(vg, left, y - spacing / 2, box.size.x - left, spacing);
                nvgFillColor(vg, theme::recessed(c, c.bg0, 0.25f));
                nvgFill(vg);
            }
            if ((int(i) - middle) % every == 0)
                labels.push_back({y, format(rows[i])});
        }
    } else {
        for (int k = 0; k <= 4; k++) {
            float v = low + (high - low) * float(k) / 4;
            labels.push_back({std::clamp(yOf(v), top + 6, bottom - 6), format(v)});
        }
    }
    // The middle: the setting as set, which the curve adds to.
    float mid = yOf(0.5f);
    nvgBeginPath(vg);
    for (float x = left; x < box.size.x; x += 6) {
        nvgMoveTo(vg, x, mid);
        nvgLineTo(vg, x + 3, mid);
    }
    nvgStrokeColor(vg, c.text2);
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
    // Labels sit in the column on the left, where the piano keys are above, so the curve never hides them.
    nvgBeginPath(vg);
    nvgRect(vg, 0, top, left, bottom - top);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    label.align = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;
    for (auto& [y, text] : labels)
        if (y > top && y < bottom)
            draw::text(vg, label, left - 5, y, draw::ellipsize(vg, label, text, left - 8));
    label.align = NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM;
    if (mid > top + 10 && mid < bottom)
        draw::text(vg, label, box.size.x - 6, mid - 2, "as set: " + format(0.5f));
}

void CurveView::drawCurve(NVGcontext* vg, NVGcolor color, const CurveDrawing& d) {
    const theme::Palette& c = theme::colors();
    float top = kHead + kBar, bottom = box.size.y;
    // The segment lit across the rows; past the drawing's end, darker.
    float s0 = xOf(double(d.segmentStart)), s1 = xOf(double(d.segmentEnd)), end = xOf(double(d.length));
    nvgBeginPath(vg);
    nvgRect(vg, s0, top, s1 - s0, bottom - top);
    nvgFillColor(vg, nvgTransRGBAf(color, 0.07f));
    nvgFill(vg);
    for (float x : {s0, s1}) {
        nvgBeginPath(vg);
        nvgRect(vg, x - 0.5f, top, 1, bottom - top);
        nvgFillColor(vg, nvgTransRGBAf(color, 0.6f));
        nvgFill(vg);
    }
    if (end < box.size.x) {
        nvgBeginPath(vg);
        nvgRect(vg, end, top, box.size.x - end, bottom - top);
        nvgFillColor(vg, theme::recessed(c, c.bg0, 0.35f));
        nvgFill(vg);
    }
    nvgSave(vg);
    nvgTranslate(vg, left, top);
    timeline::drawGrid(vg, time, owner.timeSig(), bottom - top, snap());
    nvgRestore(vg);

    // The line, piece by piece: a step goes across then up or down, a ramp straight to the next point. Inside the
    // segment it is bright, outside faint.
    std::vector<AutoPoint> pts = d.points;
    float firstValue = pts.empty() ? 0.5f : pts.front().value;
    auto strokeSpan = [&](double from, double to, float alpha, float width) {
        if (to <= from)
            return;
        mods::Drawn drawn{pts.data(), int(pts.size()), d.length, d.segmentStart, d.segmentEnd};
        nvgSave(vg);
        nvgIntersectScissor(vg, xOf(from), top, std::max(0.f, xOf(to) - xOf(from)), bottom - top);
        nvgBeginPath(vg);
        double a = std::max(0.0, time.left()), b = std::min(double(d.length), time.right());
        nvgMoveTo(vg, xOf(a), yOf(pts.empty() ? 0.5f : mods::drawnAt(drawn, a)));
        for (size_t i = 0; i < pts.size(); i++) {
            const AutoPoint& p = pts[i];
            double t = double(p.t);
            if (t < a)
                continue;
            if (t > b)
                break;
            const AutoPoint* prev = i > 0 ? &pts[i - 1] : nullptr;
            if (!prev || isStep(*prev)) {
                nvgLineTo(vg, xOf(t), yOf(prev ? prev->value : firstValue));
            } else if (prev->curve != CurveType::Linear || prev->tension != 0) {
                for (int k = 1; k < 24; k++) {
                    double u = double(prev->t) + (t - double(prev->t)) * k / 24;
                    if (u > a)
                        nvgLineTo(vg, xOf(u), yOf(mods::drawnAt(drawn, u)));
                }
            }
            nvgLineTo(vg, xOf(t), yOf(p.value));
        }
        nvgLineTo(vg, xOf(b), yOf(pts.empty() ? 0.5f : mods::drawnAt(drawn, b)));
        nvgStrokeColor(vg, nvgTransRGBAf(color, alpha));
        nvgStrokeWidth(vg, width);
        nvgStroke(vg);
        nvgRestore(vg);
    };
    strokeSpan(0, double(d.segmentStart), 0.4f, 1.4f);
    strokeSpan(double(d.segmentEnd), double(d.length), 0.4f, 1.4f);
    strokeSpan(double(d.segmentStart), double(d.segmentEnd), 1, 2);

    // A ramp on a setting with steps is heard a step at a time: those stairs, dotted under it.
    if (stepped()) {
        mods::Drawn drawn{pts.data(), int(pts.size()), d.length, d.segmentStart, d.segmentEnd};
        for (size_t i = 0; i + 1 < pts.size(); i++) {
            if (isStep(pts[i]) || pts[i].t >= pts[i + 1].t)
                continue;
            // Only where it plays at all: inside the drawing.
            float xa = std::max(left, xOf(double(pts[i].t)));
            float xb = std::min({box.size.x, xOf(double(pts[i + 1].t)), xOf(double(d.length))});
            if (xb <= xa)
                continue;
            nvgBeginPath(vg);
            float lastY = yOf(heard(mods::drawnAt(drawn, tAt(xa))));
            nvgMoveTo(vg, xa, lastY);
            for (float x = xa + 1; x <= xb; x += 1) {
                float y = yOf(heard(mods::drawnAt(drawn, tAt(x))));
                if (y != lastY) {
                    nvgLineTo(vg, x, lastY);
                    nvgLineTo(vg, x, y);
                    lastY = y;
                }
            }
            nvgLineTo(vg, xb, lastY);
            nvgStrokeColor(vg, nvgTransRGBAf(color, 0.55f));
            nvgStrokeWidth(vg, 1);
            nvgStroke(vg);
        }
    }

    // Points: a square is a step, a circle a ramp; dim outside the segment and past the end.
    for (const AutoPoint& p : pts) {
        float x = xOf(double(p.t)), y = yOf(p.value);
        if (x < -6 || x > box.size.x + 6)
            continue;
        bool inside = p.t >= d.segmentStart && p.t <= d.segmentEnd;
        float alpha = p.t > d.length ? 0.3f : inside ? 1.f : 0.5f;
        nvgBeginPath(vg);
        if (isStep(p))
            nvgRect(vg, x - 3.5f, y - 3.5f, 7, 7);
        else
            nvgCircle(vg, x, y, 3.8f);
        nvgFillColor(vg, c.bg0);
        nvgFill(vg);
        nvgFillColor(vg, nvgTransRGBAf(color, alpha * 0.6f));
        nvgFill(vg);
        nvgStrokeColor(vg, nvgTransRGBAf(color, alpha));
        nvgStrokeWidth(vg, 1.4f);
        nvgStroke(vg);
    }
}

void CurveView::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);
    TesseraModule* m = ctx.module();
    if (!m || !hasCurve())
        return;
    nvgSave(vg);
    nvgIntersectScissor(vg, 0, 0, box.size.x, box.size.y);
    ParamRef ref = target();
    NVGcolor color = c.accent;
    if (const Modulator* mod = modulator())
        color = deviceActions::colorOf(mod->id);
    if (ref.kind == ParamRef::Kind::Playhead)
        if (const Playhead* p = findPlayhead(m->song(), PlayheadId(ref.owner)))
            color = theme::playheadColor(p->color);
    if (ref.kind == ParamRef::Kind::Device)
        if (const Device* d = findDevice(m->song(), DeviceId(ref.owner)); d && d->type == DeviceType::Motion)
            color = theme::playheadColor(d->color);
    CurveDrawing d = shown();

    drawRows(vg);
    nvgSave(vg);
    nvgIntersectScissor(vg, left, kHead + kBar, box.size.x - left, box.size.y - kHead - kBar);
    drawCurve(vg, color, d);
    // The curve's own playhead: where it reads now, counted from when its clip started.
    double since = sinceClipStart();
    if (std::isfinite(since)) {
        mods::Drawn drawn{d.points.data(), int(d.points.size()), d.length, d.segmentStart, d.segmentEnd};
        float x = xOf(mods::drawnPlace(drawn, since));
        nvgSave(vg);
        nvgTranslate(vg, 0, kHead + kBar);
        timeline::drawPlayhead(vg, x, box.size.y - kHead - kBar);
        nvgRestore(vg);
    }
    nvgRestore(vg);
    drawRuler(vg, color, d);
    drawHeader(vg, color);
    if (d.points.empty()) {
        draw::TextStyle hint{theme::fonts::ui(), 11, c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, (left + box.size.x) / 2, (kHead + kBar + box.size.y) / 2 - 14,
                   "Click to add a point, or pick a shape above: " + title() + " starts moving.");
    }
    nvgRestore(vg);
}

} // namespace tessera
