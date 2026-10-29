#include "CurveView.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/Automation.hpp"
#include "../../model/DeviceCommands.hpp"
#include "../../model/DeviceLookup.hpp"
#include "../../model/Modulators.hpp"
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

/// Dragging a point, or the length bar's end. One undo step on release.
class CurveDrag : public DragGesture {
  public:
    enum class Mode { Point, Length };
    CurveDrag(CurveView& view, std::vector<AutoPoint> points, Tick length, Mode mode, int index, double cycleBase)
        : view(view), points(std::move(points)), length(length), mode(mode), index(index), cycleBase(cycleBase) {
        view.preview = std::make_pair(this->points, length);
    }
    void update(Vec pos, Modifiers mods) override {
        Tick snap = mods.alt ? 1 : view.snap();
        if (mode == Mode::Length) {
            double t = view.tAt(pos.x) - view.spanStart();
            Tick old = length;
            length = std::max<Tick>(snap, Tick(std::llround(t / double(snap))) * snap);
            // The shape stretches with its cycle, so a longer cycle plays the same shape slower. Per pass, points
            // stay on their passes: the cycle just gains or loses passes.
            if (!view.perPass())
                for (AutoPoint& p : points)
                    p.t = Tick(std::llround(double(p.t) * double(length) / double(std::max<Tick>(1, old))));
            else
                for (AutoPoint& p : points)
                    p.t = std::min(p.t, length);
        } else {
            double t = view.tAt(pos.x) - view.spanStart() - cycleBase;
            Tick at = Tick(std::llround(t / double(snap))) * snap;
            // A point stays between its neighbours, so dragging never reorders the curve.
            Tick lo = index > 0 ? points[size_t(index) - 1].t : 0;
            Tick hi = index + 1 < int(points.size()) ? points[size_t(index) + 1].t : length;
            points[size_t(index)].t = std::clamp(at, lo, hi);
            points[size_t(index)].value = view.valueAt(pos.y, mods.alt);
        }
        view.preview = std::make_pair(points, length);
        moved = true;
    }
    void commit() override {
        view.preview.reset();
        if (moved || fresh)
            view.commit(length, points);
    }
    void cancel() override { view.preview.reset(); }
    std::string readout() const override {
        if (mode == Mode::Length)
            return "Lasts " + std::to_string(length / (kPpq / 4)) + " steps";
        return view.format(points[size_t(index)].value);
    }
    CursorShape cursor() const override { return mode == Mode::Length ? CursorShape::ResizeH : CursorShape::Move; }
    bool fresh = false;

  private:
    CurveView& view;
    std::vector<AutoPoint> points;
    Tick length;
    Mode mode;
    int index;
    double cycleBase;
    bool moved = false;
};

} // namespace

const Modulator* CurveView::modulator() {
    TesseraModule* m = ctx.module();
    if (!m || !ctx.curveModulator)
        return nullptr;
    const Modulator* mod = findModulator(m->song(), ModulatorId(ctx.curveModulator));
    return mod && mod->type == ModulatorType::Curve ? mod : nullptr;
}

ParamRef CurveView::target() {
    if (const Modulator* mod = modulator()) {
        for (const Modulation& r : mod->routes)
            if (r.kind == Modulation::Kind::Param)
                return r.target;
        return ParamRef();
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
        *amount = curveAmount(*info);
        if (const Modulator* mod = modulator())
            for (const Modulation& r : mod->routes)
                if (r.kind == Modulation::Kind::Param && r.target == ref) {
                    *amount = r.amount;
                    break;
                }
    }
    return info;
}

bool CurveView::synced() {
    const Modulator* mod = modulator();
    if (!mod)
        return true;
    const devices::ParamInfo* info = devices::paramInfo(ModulatorType::Curve, mods::knob::kCurveSync);
    return perPass() || (info && devices::display(*info, mod->params[mods::knob::kCurveSync]) > 0.5f);
}

bool CurveView::perPass() {
    const Modulator* mod = modulator();
    return mod && mods::perPass(mod->type, mod->params.data());
}

bool CurveView::hasCurve() { return modulator() || (ctx.curveTarget.valid() && targetInfo()); }

std::string CurveView::title() {
    if (const Modulator* mod = modulator())
        return mod->name;
    TesseraModule* m = ctx.module();
    return m && ctx.curveTarget.valid() ? deviceActions::paramName(m->song(), ctx.curveTarget) : "Curve";
}

double CurveView::spanStart() {
    if (perPass())
        return 0;
    const Clip* c = owner.clip();
    const Track* t = owner.track();
    if (!c || !t)
        return 0;
    return double(clipSpan(*c, t->type == TrackType::Drum ? &t->drumLanes : nullptr).start);
}

std::pair<std::vector<AutoPoint>, Tick> CurveView::shown() {
    if (preview)
        return *preview;
    if (const Modulator* mod = modulator())
        return {mod->points, mod->length};
    // A new curve lasts as long as the clip, so its first cycle sits under the notes.
    const Clip* clip = owner.clip();
    return {{}, clip ? std::max<Tick>(kPpq, clip->playEnd() - clip->playStart()) : 4 * kPpq};
}

void CurveView::commit(Tick length, std::vector<AutoPoint> points) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    if (const Modulator* mod = modulator()) {
        m->perform(setCurve(m->song(), mod->id, length, std::move(points)));
        return;
    }
    const Track* t = owner.track();
    ParamRef ref = ctx.curveTarget;
    if (!t || !ref.valid())
        return;
    // Named after what it moves: "Melody Rate", "Echo Time".
    std::string name = deviceActions::paramName(m->song(), ref);
    for (size_t at = name.find(" \u00b7 "); at != std::string::npos; at = name.find(" \u00b7 "))
        name.replace(at, std::string(" \u00b7 ").size(), " ");
    if (ref.kind == ParamRef::Kind::Playhead)
        if (const Playhead* p = findPlayhead(m->song(), PlayheadId(ref.owner)))
            if (const devices::ParamInfo* info = devices::playheadParamInfo(ref.param))
                name = p->name + " " + info->name;
    ModulatorId added;
    const devices::ParamInfo* info = targetInfo();
    CommandPtr add = animateWithCurve(m->song(), t->id, ref, name, length, std::move(points), m->ids(), &added,
                                      info ? curveAmount(*info) : 1.f);
    if (!add) {
        ctx.toast("This track has no room for another modulator");
        return;
    }
    m->perform(std::move(add));
    deviceActions::showCurve(ctx, added);
}

float CurveView::xOf(double t) {
    if (perPass())
        return kPassLeft + float(t / double(mods::kPassTicks)) * kPassWidth;
    return float(owner.viewport.time.tickToX(t));
}

double CurveView::tAt(float x) {
    if (perPass())
        return double(x - kPassLeft) / double(kPassWidth) * double(mods::kPassTicks);
    return owner.viewport.time.xToTick(x);
}

float CurveView::yOf(float value) const {
    float top = kBar + kPad, bottom = box.size.y - kPad;
    return bottom - value * (bottom - top);
}

float CurveView::valueAt(float y, bool free) {
    float top = kBar + kPad, bottom = box.size.y - kPad;
    float v = std::clamp((bottom - y) / std::max(1.f, bottom - top), 0.f, 1.f);
    float base = 0, amount = 1;
    const devices::ParamInfo* info = targetInfo(&base, &amount);
    if (free || !info || !discrete(*info) || std::fabs(amount) < 1e-4f)
        return v;
    // Onto the nearest of the setting's steps.
    float n = std::clamp(base + amount * (2 * v - 1), 0.f, 1.f);
    float stepped = devices::normalize(*info, std::round(devices::display(*info, n)));
    return std::clamp(((stepped - base) / amount + 1) / 2, 0.f, 1.f);
}

std::string CurveView::format(float v) {
    float base = 0, amount = 1;
    const devices::ParamInfo* info = targetInfo(&base, &amount);
    if (!info)
        return std::to_string(int(std::lround(v * 100))) + "%";
    return devices::format(*info, std::clamp(base + amount * (2 * v - 1), 0.f, 1.f));
}

Tick CurveView::snap() { return perPass() ? mods::kPassTicks : std::clamp<Tick>(owner.snapStep(), 1, kPpq); }

int CurveView::pointAt(Vec pos) {
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

std::unique_ptr<DragGesture> CurveView::startGesture(Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || !hasCurve())
        return nullptr;
    auto [points, length] = shown();
    if (pos.y < kBar) {
        // Only the bar's end (◀) is a handle, and only while synced: free, the Rate knob sets the speed.
        if (!synced() || std::fabs(pos.x - xOf(spanStart() + double(length))) > 8)
            return nullptr;
        return std::make_unique<CurveDrag>(*this, points, length, CurveDrag::Mode::Length, -1, 0);
    }
    double t = tAt(pos.x) - spanStart();
    if (t < 0)
        return nullptr;
    double base = std::floor(t / double(length)) * double(length);
    int index = pointAt(pos);
    bool fresh = index < 0;
    if (fresh) {
        // The first point keeps the setting where it is: the curve starts from no change.
        if (points.empty())
            points.push_back({0, 0.5f, CurveType::Linear, 0});
        Tick at = Tick(std::llround((t - base) / double(snap()))) * snap();
        AutoPoint pt{std::clamp<Tick>(at, 0, length), valueAt(pos.y, mods.alt), CurveType::Linear, 0};
        auto it = std::upper_bound(points.begin(), points.end(), pt.t,
                                   [](Tick v, const AutoPoint& q) { return v < q.t; });
        index = int(it - points.begin());
        points.insert(it, pt);
    }
    auto drag = std::make_unique<CurveDrag>(*this, points, length, CurveDrag::Mode::Point, index, base);
    drag->fresh = fresh;
    return drag;
}

void CurveView::doubleClick(Vec pos, Modifiers mods) {
    int index = pointAt(pos);
    if (index < 0 || !modulator())
        return;
    auto [points, length] = shown();
    points.erase(points.begin() + index);
    commit(length, points);
}

void CurveView::click(Vec pos, int button, Modifiers mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
        openMenu();
}

void CurveView::openMenu() {
    if (!hasCurve())
        return;
    Tick length = shown().second;
    EditorContext* c = &ctx;
    CurveView* view = this;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(title()));
    if (const Modulator* mod = modulator()) {
        ModulatorId id = mod->id;
        menu->addChild(rack::createMenuItem("Stop animating (delete the curve)", "", [c, id] {
            if (TesseraModule* m = c->module())
                m->perform(removeModulator(m->song(), id));
        }));
        auto knob = [id](int k) { return ParamRef{ParamRef::Kind::Modulator, id.value, uint16_t(k)}; };
        // Timing: one cycle as long as the bar (synced), at a rate in Hz (free), or one point a pass (per pass).
        int timing = perPass() ? 2 : synced() ? 0 : 1;
        Tick clipLength = std::max<Tick>(kPpq, owner.clip() ? owner.clip()->playEnd() - owner.clip()->playStart() : 0);
        menu->addChild(rack::createSubmenuItem(
            "Timing", timing == 2 ? "Per pass" : timing == 0 ? "Synced" : "Free", [=](rack::ui::Menu* sub) {
                const char* names[] = {"Synced (the bar sets a cycle)", "Free (Rate in Hz)", "Per pass (one point a pass)"};
                for (int k = 0; k < 3; k++)
                    sub->addChild(rack::createCheckMenuItem(names[k], "", [timing, k] { return timing == k; }, [=] {
                        TesseraModule* m = c->module();
                        const Modulator* now = m ? findModulator(m->song(), id) : nullptr;
                        if (!now || timing == k)
                            return;
                        // Made step by step on a copy: each command sees what the one before did.
                        Song work = m->song();
                        auto all = std::make_unique<CompoundCommand>("Set " + now->name + " timing");
                        auto step = [&](CommandPtr command) {
                            if (command)
                                command->apply(work);
                            all->add(std::move(command));
                        };
                        step(setParam(work, knob(mods::knob::kCurveSync), k == 1 ? 0.f : 1.f));
                        step(setParam(work, knob(mods::knob::kCurvePerPass), k == 2 ? 1.f : 0.f));
                        // Into or out of passes, the shape keeps its look over the new cycle: 4 passes, or the clip.
                        if ((k == 2) != (timing == 2)) {
                            Tick length = k == 2 ? 4 * mods::kPassTicks : clipLength;
                            std::vector<AutoPoint> points = now->points;
                            for (AutoPoint& p : points)
                                p.t = Tick(std::llround(double(p.t) * double(length) / double(std::max<Tick>(1, now->length))));
                            step(setCurve(work, id, length, points));
                        }
                        m->perform(std::move(all));
                    }));
            }));
        const devices::ParamInfo& restart =
            devices::modulatorInfo(ModulatorType::Curve).params[mods::knob::kCurveRestart];
        int now = int(std::lround(devices::display(restart, mod->params[mods::knob::kCurveRestart])));
        menu->addChild(rack::createSubmenuItem("Restart", restart.choices[size_t(now)], [=](rack::ui::Menu* sub) {
            for (size_t i = 0; i < restart.choices.size(); i++)
                sub->addChild(rack::createCheckMenuItem(
                    restart.choices[i], "", [now, i] { return now == int(i); }, [c, knob, restart, i] {
                        if (TesseraModule* m = c->module())
                            m->perform(setParam(m->song(), knob(mods::knob::kCurveRestart),
                                                devices::normalize(restart, float(i))));
                    }));
        }));
    }
    // Shapes to start from, over the cycle, across the whole height.
    struct Shape {
        const char* name;
        std::vector<std::pair<double, float>> points; // (place in the cycle, value)
        CurveType curve;
    };
    const Shape shapes[] = {{"Rise", {{0, 0}, {1, 1}}, CurveType::Linear},
                            {"Fall", {{0, 1}, {1, 0}}, CurveType::Linear},
                            {"Up and down", {{0, 0.5f}, {0.25, 1}, {0.75, 0}, {1, 0.5f}}, CurveType::Linear},
                            {"Two steps", {{0, 0.5f}, {0.5, 1}, {1, 1}}, CurveType::Hold}};
    menu->addChild(rack::createSubmenuItem("Shape", "", [=](rack::ui::Menu* sub) {
        for (const Shape& s : shapes)
            sub->addChild(rack::createMenuItem(s.name, "", [=] {
                std::vector<AutoPoint> points;
                for (auto [at, v] : s.points)
                    points.push_back({Tick(at * double(length)), v, s.curve, 0});
                view->commit(length, points);
            }));
    }));
    menu->addChild(rack::createMenuLabel("Click to add a point, double-click to delete; Alt: off the grid"));
}

CursorShape CurveView::hoverCursor(Vec pos, Modifiers mods) {
    if (!hasCurve())
        return CursorShape::Arrow;
    if (pos.y < kBar)
        return synced() && std::fabs(pos.x - xOf(spanStart() + double(shown().second))) <= 8 ? CursorShape::ResizeH
                                                                                              : CursorShape::Arrow;
    return pointAt(pos) >= 0 ? CursorShape::Move : CursorShape::Crosshair;
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
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
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
    auto [points, length] = shown();
    double start = spanStart();
    float base = 0, amount = 1;
    const devices::ParamInfo* info = targetInfo(&base, &amount);
    draw::TextStyle label{theme::fonts::mono(), 9.5f, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};

    // Up and down in the setting's own units: a row per step for a stepped setting, a few marks otherwise.
    if (info && discrete(*info) && std::fabs(amount) > 1e-4f) {
        std::vector<float> rows;
        for (float d : stepsOf(*info)) {
            float v = ((devices::normalize(*info, d) - base) / amount + 1) / 2;
            if (v >= -1e-4f && v <= 1 + 1e-4f)
                rows.push_back(v);
        }
        std::sort(rows.begin(), rows.end());
        float spacing = rows.size() > 1 ? std::fabs(yOf(rows[0]) - yOf(rows[1])) : box.size.y;
        int every = std::max(1, int(std::ceil(13 / std::max(1.f, spacing))));
        // Labels count out from the row as set, so it always has one.
        int middle = int(std::min_element(rows.begin(), rows.end(),
                                          [](float a, float b) { return std::fabs(a - 0.5f) < std::fabs(b - 0.5f); }) -
                         rows.begin());
        for (size_t i = 0; i < rows.size(); i++) {
            float y = yOf(rows[i]);
            if ((int(i) - middle) % 2 == 0) {
                nvgBeginPath(vg);
                nvgRect(vg, 0, y - spacing / 2, box.size.x, spacing);
                nvgFillColor(vg, theme::recessed(c, c.bg0, 0.25f));
                nvgFill(vg);
            }
            if ((int(i) - middle) % every == 0)
                draw::text(vg, label, 6, y, format(rows[i]));
        }
    } else {
        for (float v : {0.f, 0.25f, 0.75f, 1.f}) {
            float y = std::clamp(yOf(v), kBar + 6, box.size.y - 6);
            draw::text(vg, label, 6, y, format(v));
        }
    }
    // The middle: the setting as set, which the curve moves away from.
    float mid = yOf(0.5f);
    nvgBeginPath(vg);
    for (float x = 0; x < box.size.x; x += 6) {
        nvgMoveTo(vg, x, mid);
        nvgLineTo(vg, x + 3, mid);
    }
    nvgStrokeColor(vg, c.text2);
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
    label.align = NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM;
    draw::text(vg, label, box.size.x - 6, mid - 2, "as set: " + format(0.5f));

    // The length bar: ▶ where the clip starts, ◀ where the curve repeats. Free-running, it only marks the cycle.
    bool sync = synced();
    float x0 = xOf(start), x1 = xOf(start + double(length));
    draw::roundedRect(vg, Rect(x0, 2, std::max(2.f, x1 - x0), kBar - 4), 2, nvgTransRGBAf(color, sync ? 0.25f : 0.1f));
    if (sync) {
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
    } else if (const Modulator* mod = modulator()) {
        // Free running: the bar only marks a cycle; Rate sets how fast it goes round.
        const devices::ParamInfo& rate = devices::modulatorInfo(ModulatorType::Curve).params[mods::knob::kCurveRate];
        draw::TextStyle free{theme::fonts::ui(), 10, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, free, x0 + 6, kBar / 2,
                   "Free: " + devices::format(rate, mod->params[mods::knob::kCurveRate]) + " a cycle");
    }

    if (perPass()) {
        // A column per pass, numbered, over the cycle and a little past it.
        draw::TextStyle pass{theme::fonts::ui(), 10, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM};
        int passes = int(std::max<Tick>(1, length / mods::kPassTicks));
        for (int k = 0; xOf(double(k) * double(mods::kPassTicks)) < box.size.x; k++) {
            float x = xOf(double(k) * double(mods::kPassTicks));
            nvgBeginPath(vg);
            nvgRect(vg, x, kBar, 1, box.size.y - kBar);
            nvgFillColor(vg, k % passes == 0 ? c.line1 : c.line0);
            nvgFill(vg);
            pass.color = k < passes ? c.text2 : nvgTransRGBAf(c.text2, 0.5f);
            draw::text(vg, pass, x + 4, box.size.y - 2, "pass " + std::to_string(k + 1));
        }
    }

    // The curve, every cycle across the view; the first one bright, its repeats faint.
    double t1 = tAt(box.size.x), t0 = tAt(0);
    mods::Drawn drawn{points.data(), int(points.size()), length};
    for (double cycle = start; cycle < t1 && length > 0; cycle += double(length)) {
        if (cycle + double(length) < t0)
            continue;
        bool first = cycle == start;
        nvgBeginPath(vg);
        if (perPass()) {
            // A pass takes the value at its start: a step each.
            for (Tick k = 0; k * mods::kPassTicks < length; k++) {
                double t = double(k * mods::kPassTicks);
                float y = yOf(points.empty() ? 0.5f : mods::drawnAt(drawn, t));
                float x0 = xOf(cycle + t), x1 = xOf(cycle + t + double(mods::kPassTicks));
                k == 0 ? nvgMoveTo(vg, x0, y) : nvgLineTo(vg, x0, y);
                nvgLineTo(vg, x1, y);
            }
        } else {
            for (int i = 0; i <= 128; i++) {
                double t = double(length) * i / 128;
                float x = xOf(cycle + t), y = yOf(points.empty() ? 0.5f : mods::drawnAt(drawn, t));
                i == 0 ? nvgMoveTo(vg, x, y) : nvgLineTo(vg, x, y);
            }
        }
        nvgStrokeColor(vg, nvgTransRGBAf(color, first ? 1.f : 0.35f));
        nvgStrokeWidth(vg, first ? 2.f : 1.4f);
        nvgStroke(vg);
        for (const AutoPoint& pt : points) {
            nvgBeginPath(vg);
            nvgCircle(vg, xOf(cycle + double(pt.t)), yOf(pt.value), 3.5f);
            nvgFillColor(vg, c.bg0);
            nvgFill(vg);
            nvgStrokeColor(vg, nvgTransRGBAf(color, first ? 1.f : 0.35f));
            nvgStrokeWidth(vg, 1.4f);
            nvgStroke(vg);
        }
    }
    if (points.empty()) {
        draw::TextStyle hint{theme::fonts::ui(), 11, c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, box.size.x / 2, (kBar + box.size.y) / 2 - 14,
                   "Click to add a point: " + title() + " starts moving. Right-click for shapes.");
    }
    nvgRestore(vg);
}

} // namespace tessera
