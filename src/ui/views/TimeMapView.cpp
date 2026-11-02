#include "TimeMapView.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/DeviceCommands.hpp"
#include "../../model/DeviceLookup.hpp"
#include "../../model/DeviceTypes.hpp"
#include "../../model/Playheads.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../widgets/Draw.hpp"
#include "PianoRoll.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kPointHit = 6;
constexpr float kPad = 4;

/// Dragging one point of a drawn path (a fresh one too): snapped to the grid, one undo step on release.
class PathPointDrag : public DragGesture {
  public:
    PathPointDrag(TimeMapView& view, PlayheadId id, std::vector<AutoPoint> points, int index,
                  std::function<AutoPoint(Vec, Modifiers)> place, std::string name)
        : view(view), id(id), points(std::move(points)), index(index), place(std::move(place)), name(std::move(name)) {
        view.preview = std::make_pair(id, this->points);
    }
    void update(Vec pos, Modifiers mods) override {
        AutoPoint p = place(pos, mods);
        // A point stays between its neighbours, so dragging never reorders the path.
        Tick lo = index > 0 ? points[size_t(index) - 1].t + 1 : 0;
        Tick hi = index + 1 < int(points.size()) ? points[size_t(index) + 1].t - 1 : std::numeric_limits<Tick>::max();
        p.t = std::clamp(p.t, lo, std::max(lo, hi));
        p.curve = points[size_t(index)].curve;
        p.tension = points[size_t(index)].tension;
        points[size_t(index)] = p;
        view.preview = std::make_pair(id, points);
        moved = true;
    }
    void commit() override {
        view.preview.reset();
        TesseraModule* m = view.module();
        if (m && (moved || fresh))
            m->perform(setPlayheadPath(m->song(), id, points, name));
    }
    void cancel() override { view.preview.reset(); }
    std::string readout() const override {
        char text[32];
        snprintf(text, sizeof(text), "%.0f%% of the region", double(points[size_t(index)].value) * 100);
        return text;
    }
    CursorShape cursor() const override { return CursorShape::Move; }
    bool fresh = false;

  private:
    TimeMapView& view;
    PlayheadId id;
    std::vector<AutoPoint> points;
    int index;
    std::function<AutoPoint(Vec, Modifiers)> place;
    std::string name;
    bool moved = false;
};

} // namespace

const Device* TimeMapView::device() {
    const Track* t = owner.track();
    return t ? activePlayheads(*t) : nullptr;
}

std::vector<TimeMapView::Row> TimeMapView::rows() {
    std::vector<Row> out;
    const Device* d = device();
    if (!d || d->playheads.empty())
        return out;
    float h = box.size.y / float(d->playheads.size());
    for (size_t i = 0; i < d->playheads.size(); i++)
        out.push_back({&d->playheads[i], Rect(0, float(i) * h, box.size.x, h)});
    return out;
}

const TimeMapView::Row* TimeMapView::rowAt(Vec pos) {
    static std::vector<Row> cache;
    cache = rows();
    for (const Row& r : cache)
        if (r.rect.contains(pos))
            return &r;
    return nullptr;
}

double TimeMapView::spanStart() {
    const Clip* c = owner.clip();
    const Track* t = owner.track();
    if (!c || !t)
        return 0;
    return double(clipSpan(*c, t->type == TrackType::Drum ? &t->drumLanes : nullptr).start);
}

const std::vector<AutoPoint>& TimeMapView::pointsOf(const Playhead& p) {
    return preview && preview->first == p.id ? preview->second : p.path;
}

double TimeMapView::cycleOf(const Playhead& p) {
    const devices::ParamInfo& info = devices::playheadInfo().params[playheads::knob::kPathCycle];
    return playheads::pathCycleTicks(int(devices::display(info, p.params[playheads::knob::kPathCycle])),
                                     owner.timeSig());
}

float TimeMapView::xOf(double elapsed) { return float(owner.viewport.time.tickToX(spanStart() + elapsed)); }

double TimeMapView::ownAt(float x) { return owner.viewport.time.xToTick(x) - spanStart(); }

float TimeMapView::yOf(const Row& row, float value) const {
    return row.rect.getBottom() - kPad - value * (row.rect.size.y - 2 * kPad);
}

float TimeMapView::valueAt(const Row& row, float y) const {
    return std::clamp((row.rect.getBottom() - kPad - y) / std::max(1.f, row.rect.size.y - 2 * kPad), 0.f, 1.f);
}

namespace {

double rateOf(const Playhead& p) {
    const devices::ParamInfo& info = devices::playheadInfo().params[playheads::knob::kRate];
    return playheads::playheadRate(int(devices::display(info, p.params[playheads::knob::kRate])));
}

} // namespace

int TimeMapView::pointAt(const Row& row, Vec pos) {
    const Playhead& p = *row.playhead;
    if (!playheads::wantsDrawnPath(p.params.data()))
        return -1;
    const std::vector<AutoPoint>& points = pointsOf(p);
    double cycle = cycleOf(p), rate = rateOf(p);
    double own = std::max(0.0, ownAt(pos.x) * rate);
    double base = std::floor(own / cycle) * cycle; // the cycle under the mouse
    for (size_t i = 0; i < points.size(); i++) {
        float x = xOf((base + double(points[i].t)) / rate);
        float y = yOf(row, points[i].value);
        if (std::fabs(x - pos.x) <= kPointHit && std::fabs(y - pos.y) <= kPointHit)
            return int(i);
    }
    return -1;
}

std::unique_ptr<DragGesture> TimeMapView::startGesture(Vec pos, int button, Modifiers mods) {
    const Row* row = rowAt(pos);
    if (button != GLFW_MOUSE_BUTTON_LEFT || !row || !playheads::wantsDrawnPath(row->playhead->params.data()))
        return nullptr;
    Row r = *row;
    const Playhead& p = *r.playhead;
    double cycle = cycleOf(p), rate = rateOf(p);
    Tick snap = std::max<Tick>(1, owner.snapStep());
    auto place = [this, r, cycle, rate, snap](Vec at, Modifiers m) {
        double own = std::max(0.0, ownAt(at.x) * rate);
        double t = std::fmod(own, cycle);
        if (!m.alt)
            t = std::round(t / double(snap)) * double(snap);
        AutoPoint pt;
        pt.t = std::clamp<Tick>(Tick(t), 0, Tick(cycle) - 1);
        pt.value = valueAt(r, at.y);
        return pt;
    };
    std::vector<AutoPoint> points = p.path;
    int index = pointAt(r, pos);
    bool fresh = index < 0;
    if (fresh) {
        AutoPoint pt = place(pos, mods);
        auto it = std::upper_bound(points.begin(), points.end(), pt.t,
                                   [](Tick t, const AutoPoint& q) { return t < q.t; });
        if (it != points.begin() && std::prev(it)->t == pt.t)
            return nullptr; // a point is already there
        index = int(it - points.begin());
        points.insert(it, pt);
    }
    auto drag = std::make_unique<PathPointDrag>(*this, p.id, points, index, place, fresh ? "Add path point" : "Move path point");
    drag->fresh = fresh;
    return drag;
}

void TimeMapView::click(Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_RIGHT)
        return;
    if (const Row* row = rowAt(pos))
        openMenu(*row);
}

void TimeMapView::doubleClick(Vec pos, Modifiers mods) {
    const Row* row = rowAt(pos);
    TesseraModule* m = ctx.module();
    if (!row || !m)
        return;
    int index = pointAt(*row, pos);
    if (index < 0)
        return;
    std::vector<AutoPoint> points = row->playhead->path;
    points.erase(points.begin() + index);
    m->perform(setPlayheadPath(m->song(), row->playhead->id, points, "Delete path point"));
}

CursorShape TimeMapView::hoverCursor(Vec pos, Modifiers mods) {
    const Row* row = rowAt(pos);
    if (!row || !playheads::wantsDrawnPath(row->playhead->params.data()))
        return CursorShape::Arrow;
    return pointAt(*row, pos) >= 0 ? CursorShape::Move : CursorShape::Crosshair;
}

void TimeMapView::openMenu(const Row& row) {
    TesseraModule* m = ctx.module();
    const Device* d = device();
    if (!m || !d)
        return;
    const Playhead& p = *row.playhead;
    PlayheadId id = p.id;
    EditorContext* c = &ctx;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(p.name));
    ParamRef pathKnob{ParamRef::Kind::Playhead, id.value, uint16_t(playheads::knob::kPath)};
    const devices::ParamInfo& pathInfo = devices::playheadInfo().params[playheads::knob::kPath];
    if (!playheads::wantsDrawnPath(p.params.data())) {
        // Starts from where the settings take it, so drawing begins from what plays now.
        const Clip* clip = owner.clip();
        const Track* t = owner.track();
        if (!clip || !t)
            return;
        ClipSpan span = clipSpan(*clip, t->type == TrackType::Drum ? &t->drumLanes : nullptr);
        auto [segStart, segLength] = playheadSpan(*clip, t->type == TrackType::Drum ? &t->drumLanes : nullptr);
        playheads::Motion motion = playheads::motionFrom(p.params.data(), d->params.data(), double(segStart),
                                                         double(segLength), owner.timeSig());
        // The cycle covers the clip (the shortest one that does), so the drawn path is the whole clip's.
        const std::vector<std::string>& cycles = playheads::pathCycleNames();
        int cycleIndex = int(cycles.size()) - 1;
        for (int k = 0; k < int(cycles.size()); k++)
            if (playheads::pathCycleTicks(k, owner.timeSig()) >= double(span.length())) {
                cycleIndex = k;
                break;
            }
        std::vector<AutoPoint> drawn = playheads::pathFromSettings(
            motion, playheads::pathCycleTicks(cycleIndex, owner.timeSig()), double(kPpq) / 4);
        menu->addChild(rack::createMenuItem("Draw this path", "", [c, id, pathKnob, pathInfo, drawn, cycleIndex] {
            TesseraModule* mod = c->module();
            if (!mod)
                return;
            const devices::ParamInfo& cycleInfo = devices::playheadInfo().params[playheads::knob::kPathCycle];
            // Each step is made on the song as the steps before left it: they each replace the whole chain.
            Song work = mod->song();
            auto all = std::make_unique<CompoundCommand>("Draw path");
            auto step = [&](CommandPtr command) {
                if (command)
                    command->apply(work);
                all->add(std::move(command));
            };
            step(setPlayheadPath(work, id, drawn));
            step(setParam(work, pathKnob, devices::normalize(pathInfo, 1)));
            step(setParam(work, {ParamRef::Kind::Playhead, id.value, uint16_t(playheads::knob::kPathCycle)},
                          devices::normalize(cycleInfo, float(cycleIndex))));
            mod->perform(std::move(all));
        }));
    } else {
        menu->addChild(rack::createMenuItem("Back to settings", "", [c, pathKnob, pathInfo] {
            if (TesseraModule* mod = c->module())
                mod->perform(setParam(mod->song(), pathKnob, devices::normalize(pathInfo, 0)));
        }));
        menu->addChild(rack::createMenuItem("Clear points", "", [c, id] {
            if (TesseraModule* mod = c->module())
                mod->perform(setPlayheadPath(mod->song(), id, {}, "Clear path"));
        }));
    }
    const devices::ParamInfo& cycleInfo = devices::playheadInfo().params[playheads::knob::kPathCycle];
    int current = int(devices::display(cycleInfo, p.params[playheads::knob::kPathCycle]));
    menu->addChild(rack::createSubmenuItem("Path cycle", cycleInfo.choices[size_t(current)], [c, id, cycleInfo,
                                                                                               current](rack::ui::Menu* sub) {
        for (size_t k = 0; k < cycleInfo.choices.size(); k++)
            sub->addChild(rack::createCheckMenuItem(
                cycleInfo.choices[k], "", [current, k] { return current == int(k); }, [c, id, cycleInfo, k] {
                    if (TesseraModule* mod = c->module())
                        mod->perform(setParam(mod->song(),
                                              {ParamRef::Kind::Playhead, id.value, uint16_t(playheads::knob::kPathCycle)},
                                              devices::normalize(cycleInfo, float(k))));
                }));
    }));
}

void TimeMapView::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    for (const Row& row : rows())
        drawRow(vg, row);
    nvgRestore(vg);
}

void TimeMapView::drawRow(NVGcontext* vg, const Row& row) {
    const theme::Palette& c = theme::colors();
    const Device* d = device();
    const Clip* clip = owner.clip();
    const Track* t = owner.track();
    if (!d || !clip || !t)
        return;
    const Playhead& p = *row.playhead;
    NVGcolor color = theme::playheadColor(p.color);
    nvgBeginPath(vg);
    nvgRect(vg, 0, row.rect.getBottom() - 1, box.size.x, 1);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);

    auto [segStart, segLength] = playheadSpan(*clip, t->type == TrackType::Drum ? &t->drumLanes : nullptr);
    playheads::Motion motion = playheads::motionFrom(p.params.data(), d->params.data(), double(segStart),
                                                     double(segLength), owner.timeSig());
    bool drawn = playheads::wantsDrawnPath(p.params.data());
    if (drawn)
        motion.path = &pointsOf(p);
    // The path, sampled across the view; a jump (a wrap, a restart) breaks the line.
    nvgBeginPath(vg);
    bool open = false;
    float lastValue = 0;
    for (float x = 0; x <= box.size.x; x += 2) {
        double elapsed = ownAt(x);
        if (elapsed < 0 || motion.regionLength <= 0) {
            open = false;
            continue;
        }
        float value = float((playheads::positionAt(motion, elapsed) - motion.regionStart) / motion.regionLength);
        float y = yOf(row, value);
        if (!open || std::fabs(value - lastValue) > 0.5f)
            nvgMoveTo(vg, x, y);
        else
            nvgLineTo(vg, x, y);
        open = true;
        lastValue = value;
    }
    nvgStrokeColor(vg, p.mute ? nvgTransRGBAf(color, 0.35f) : color);
    nvgStrokeWidth(vg, 1.6f);
    nvgStroke(vg);

    if (drawn) {
        const std::vector<AutoPoint>& points = pointsOf(p);
        double cycle = cycleOf(p), rate = rateOf(p);
        double first = std::floor(std::max(0.0, ownAt(0) * rate) / cycle) * cycle;
        for (double base = first; xOf(base / rate) < box.size.x; base += cycle)
            for (const AutoPoint& pt : points) {
                nvgBeginPath(vg);
                nvgCircle(vg, xOf((base + double(pt.t)) / rate), yOf(row, pt.value), 3.5f);
                nvgFillColor(vg, c.bg0);
                nvgFill(vg);
                nvgStrokeColor(vg, color);
                nvgStrokeWidth(vg, 1.4f);
                nvgStroke(vg);
            }
    }
    draw::TextStyle label{theme::fonts::ui(), 10, color, NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
    std::string text = p.name + (drawn ? " · drawn" : " · from settings, right-click to draw");
    if (drawn && p.path.empty())
        text = p.name + " · click to add points";
    draw::text(vg, label, 6, row.rect.pos.y + 3, draw::ellipsize(vg, label, text, box.size.x - 12));
}

} // namespace tessera
