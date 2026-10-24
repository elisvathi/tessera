#include "NoteValueLane.hpp"

#include "../../model/TrackColors.hpp"
#include "../Theme.hpp"
#include "../interaction/Navigation.hpp"
#include "../widgets/Draw.hpp"
#include "PianoRoll.hpp"
#include "TimelineDraw.hpp"

#include <cmath>

namespace tessera {

using edits::IdSet;
using edits::NoteList;
using rack::math::Vec;

namespace {

constexpr float kPad = 5;
constexpr float kFan = 3;     // px between stems of notes that start together
constexpr float kStemHit = 4; // grab distance
constexpr float kHandle = 3.5f;

/// Sets values: one stem dragged (several selected: all move by the same amount).
class StemGesture : public DragGesture {
  public:
    StemGesture(PianoRollView& view, NoteValueLane& lane, NoteId grabbed, float pressY)
        : view(view), lane(lane), field(lane.field), before(view.committedNotes()), grabbed(grabbed), pressY(pressY) {
        ids = view.isSelected(grabbed) ? view.selectedIds() : IdSet{grabbed};
        for (const Note& n : before)
            if (n.id == grabbed)
                start = value = fields::get(n, field);
    }
    void update(Vec pos, Modifiers mods) override {
        double delta = lane.valueAt(pos.y) - lane.valueAt(pressY);
        if (ids.size() > 1)
            result = fields::offset(before, ids, field, delta * (mods.shift ? 0.1 : 1));
        else
            result = fields::setAll(before, ids, field, mods.shift ? start + delta * 0.1 : lane.valueAt(pos.y));
        view.setPreview(result);
        for (const Note& n : result)
            if (n.id == grabbed)
                value = fields::get(n, field);
    }
    void commit() override {
        view.clearPreview();
        if (value != start)
            view.commit(ids.size() == 1 ? "Change " + fields::lowerName(field)
                                        : "Change " + fields::lowerName(field) + " of " + countNotes(ids.size()),
                        before, result);
    }
    void cancel() override { view.clearPreview(); }
    std::string readout() const override { return lane.describe(value); }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    PianoRollView& view;
    NoteValueLane& lane;
    fields::Field field;
    NoteList before;
    NoteId grabbed;
    float pressY;
    IdSet ids;
    double start = 0, value = 0;
    NoteList result;
};

/// Shift-drag or Draw tool: stems take the value under the pointer as it passes over them.
class FreehandGesture : public DragGesture {
  public:
    FreehandGesture(PianoRollView& view, NoteValueLane& lane, Vec press)
        : view(view), lane(lane), before(view.committedNotes()), allowed(lane.editable()), last(press) {
        stems = lane.stemPositions(before);
    }
    void update(Vec pos, Modifiers mods) override {
        float x0 = std::min(last.x, pos.x), x1 = std::max(last.x, pos.x);
        for (const auto& [id, x] : stems) {
            if (!allowed.count(id) || x < x0 - kStemHit || x > x1 + kStemHit)
                continue;
            // Interpolate between the previous and current pointer, so fast strokes stay smooth.
            float t = x1 > x0 ? (x - last.x) / (pos.x - last.x) : 1.f;
            values[id] = lane.valueAt(last.y + (pos.y - last.y) * std::clamp(t, 0.f, 1.f));
        }
        last = pos;
        result = before;
        for (Note& n : result)
            if (values.count(n.id))
                fields::set(n, lane.field, values[n.id]);
        view.setPreview(result);
        current = lane.valueAt(pos.y);
    }
    void commit() override {
        view.clearPreview();
        if (!values.empty())
            view.commit("Draw " + fields::lowerName(lane.field), before, result);
    }
    void cancel() override { view.clearPreview(); }
    std::string readout() const override { return lane.describe(current); }
    CursorShape cursor() const override { return CursorShape::Crosshair; }

  private:
    PianoRollView& view;
    NoteValueLane& lane;
    NoteList before;
    IdSet allowed;
    std::vector<std::pair<NoteId, float>> stems;
    std::unordered_map<NoteId, double> values;
    Vec last;
    double current = 0;
    NoteList result;
};

/// Alt-drag: a straight ramp from the press to the pointer across the stems in between.
class RampGesture : public DragGesture {
  public:
    RampGesture(PianoRollView& view, NoteValueLane& lane, Vec press)
        : view(view), lane(lane), before(view.committedNotes()), allowed(lane.editable()), press(press),
          pressTick(Tick(view.viewport.time.xToTick(press.x))), current(press) {}
    void update(Vec pos, Modifiers mods) override {
        current = pos;
        Tick tick = Tick(view.viewport.time.xToTick(pos.x));
        Tick t0 = std::min(pressTick, tick), t1 = std::max(pressTick, tick);
        double v0 = lane.valueAt(pressTick <= tick ? press.y : pos.y);
        double v1 = lane.valueAt(pressTick <= tick ? pos.y : press.y);
        count = 0;
        result = before;
        for (Note& n : result)
            if (allowed.count(n.id) && n.start >= t0 && n.start <= t1) {
                double x = t1 > t0 ? double(n.start - t0) / double(t1 - t0) : 0;
                fields::set(n, lane.field, v0 + (v1 - v0) * x);
                count++;
            }
        view.setPreview(result);
    }
    void commit() override {
        view.clearPreview();
        if (count > 0)
            view.commit(std::string(fields::info(lane.field).name) + " ramp", before, result);
    }
    void cancel() override { view.clearPreview(); }
    void drawOverlay(NVGcontext* vg) override {
        nvgBeginPath(vg);
        nvgMoveTo(vg, press.x, press.y);
        nvgLineTo(vg, current.x, current.y);
        nvgStrokeColor(vg, theme::colors().accent);
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
    }
    std::string readout() const override { return lane.describe(lane.valueAt(current.y)); }
    CursorShape cursor() const override { return CursorShape::Crosshair; }

  private:
    PianoRollView& view;
    NoteValueLane& lane;
    NoteList before;
    IdSet allowed;
    Vec press;
    Tick pressTick;
    Vec current;
    size_t count = 0;
    NoteList result;
};

} // namespace

std::string NoteValueLane::describe(double value) const {
    return std::string(fields::info(field).name) + " " + fields::format(field, value);
}

std::vector<std::pair<NoteId, float>> NoteValueLane::stemPositions(const NoteList& notes) {
    std::vector<std::pair<NoteId, float>> out;
    out.reserve(notes.size());
    Tick lastStart = -1;
    int fan = 0;
    for (const Note& n : notes) {
        fan = n.start == lastStart ? fan + 1 : 0;
        lastStart = n.start;
        out.push_back({n.id, float(owner.viewport.time.tickToX(double(n.start))) + fan * kFan});
    }
    return out;
}

double NoteValueLane::valueAt(float y) const {
    const fields::Info& info = fields::info(field);
    float h = box.size.y - 2 * kPad;
    double x = std::clamp(1.0 - double(y - kPad) / std::max(1.0, double(h)), 0.0, 1.0);
    double v = info.min + x * (info.max - info.min);
    return info.integer ? std::round(v) : v;
}

float NoteValueLane::yFor(double value) const {
    const fields::Info& info = fields::info(field);
    double x = (std::clamp(value, info.min, info.max) - info.min) / (info.max - info.min);
    return kPad + float(1.0 - x) * (box.size.y - 2 * kPad);
}

float NoteValueLane::baseY() const {
    const fields::Info& info = fields::info(field);
    return info.min < 0 ? yFor(0) : box.size.y - kPad;
}

IdSet NoteValueLane::editable() {
    IdSet selected = owner.selectedIds();
    if (!selected.empty())
        return selected;
    // A drum track's hits line up across lanes, so the lane shows the lane last clicked; the others are faded.
    if (owner.drumMode() && owner.laneOf(owner.focusPitch)) {
        IdSet lane;
        for (const Note& n : owner.committedNotes())
            if (n.pitch == owner.focusPitch)
                lane.insert(n.id);
        return lane;
    }
    return edits::idsOf(owner.committedNotes());
}

NoteId NoteValueLane::stemAt(Vec pos) {
    IdSet allowed = editable();
    NoteId best;
    float bestDistance = kStemHit + 1;
    for (const auto& [id, x] : stemPositions(owner.committedNotes())) {
        float d = std::fabs(pos.x - x);
        if (allowed.count(id) && d <= kStemHit && d < bestDistance) {
            best = id;
            bestDistance = d;
        }
    }
    return best;
}

std::unique_ptr<DragGesture> NoteValueLane::startGesture(Vec pos, int button, Modifiers mods) {
    owner.automationFocus = false;
    if (button != GLFW_MOUSE_BUTTON_LEFT || !owner.clip())
        return nullptr;
    if (mods.alt)
        return std::make_unique<RampGesture>(owner, *this, pos);
    if (mods.shift || ctx.state().tool == Tool::Draw)
        return std::make_unique<FreehandGesture>(owner, *this, pos);
    NoteId id = stemAt(pos);
    if (id.valid())
        return std::make_unique<StemGesture>(owner, *this, id, pos.y);
    return std::make_unique<FreehandGesture>(owner, *this, pos);
}

void NoteValueLane::wheel(Vec pos, Vec delta, Modifiers mods) {
    ctx.touchNavigation();
    if (mods.ctrl)
        owner.viewport.time.animateZoomAt(pos.x, wheelZoomFactor(delta.y), kZoomAnimationSeconds);
    else
        owner.viewport.time.scrollByPx(-(delta.y + delta.x));
}

CursorShape NoteValueLane::hoverCursor(Vec pos, Modifiers mods) {
    if (mods.alt || mods.shift || ctx.state().tool == Tool::Draw)
        return CursorShape::Crosshair;
    return stemAt(pos).valid() ? CursorShape::ResizeV : CursorShape::Arrow;
}

void NoteValueLane::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, 1);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);

    const Clip* clip = owner.clip();
    if (!clip)
        return;
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    timeline::drawGrid(vg, owner.viewport.time, owner.timeSig(), box.size.y, owner.snapStep());

    const Track* track = owner.track();
    uint32_t base = track ? track->color : 0x9AA1AB;
    IdSet selected = owner.selectedIds();
    IdSet allowed = editable();
    const NoteList& notes = owner.notes();
    auto stems = stemPositions(notes);
    float bottom = baseY();
    if (fields::info(field).min < 0) {
        nvgBeginPath(vg);
        nvgRect(vg, 0, bottom, box.size.x, 1);
        nvgFillColor(vg, c.line2);
        nvgFill(vg);
    }
    for (size_t i = 0; i < notes.size(); i++) {
        float x = stems[i].second;
        if (x < -kHandle || x > box.size.x + kHandle)
            continue;
        const Note& n = notes[i];
        bool editableNote = allowed.count(n.id) > 0;
        NVGcolor full = theme::track(channelColor(base, n.channel));
        NVGcolor color = editableNote ? full : nvgTransRGBAf(full, 0.3f);
        if (selected.count(n.id))
            color = nvgLerpRGBA(color, c.text0, 0.15f);
        float y = yFor(fields::get(n, field));
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + 0.5f, bottom);
        nvgLineTo(vg, x + 0.5f, y);
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, x + 0.5f, y, kHandle);
        nvgFillColor(vg, color);
        nvgFill(vg);
    }
    nvgRestore(vg);
}

} // namespace tessera
