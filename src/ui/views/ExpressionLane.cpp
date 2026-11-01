#include "ExpressionLane.hpp"

#include "../../model/NoteOperators.hpp"
#include "../Theme.hpp"
#include "../editing/NoteGestures.hpp"
#include "../interaction/Navigation.hpp"
#include "../widgets/Draw.hpp"
#include "PianoRoll.hpp"
#include "TimelineDraw.hpp"

#include <cmath>

namespace tessera {

using edits::IdSet;
using edits::NoteList;
using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kPad = 5;
constexpr float kPointHit = 5;
constexpr float kPointRadius = 3;
constexpr float kCurveHit = 8; // px from a curve that still adds a point to it
constexpr float kBendRadius = 2.5f;
constexpr float kMinBendSpan = 14; // px: narrower segments hide their bend handle
constexpr float kSampleStep = 3;   // px between curve samples
constexpr float kTensionPerPx = 1.f / 60;
constexpr float kFocusHeader = 24; // px: the zoomed hit's header above its curve
constexpr float kFocusMargin = 16; // px left and right of the zoomed hit

/// Adds, moves or bends one point of one note's curve. One undo step on release.
class CurveGesture : public DragGesture {
  public:
    enum class Mode { Move, Bend };

    CurveGesture(PianoRollView& view, ExpressionLane& lane, NoteId note, size_t index, Mode mode, Vec press, bool added,
                 NoteList start)
        : view(view), lane(lane), note(note), index(index), mode(mode), press(press), added(added),
          before(view.committedNotes()), result(std::move(start)) {
        if (const Note* n = find(result))
            startTension =
                index < expr::points(*n, lane.curve).size() ? expr::points(*n, lane.curve)[index].tension : 0;
    }
    void update(Vec pos, Modifiers mods) override {
        NoteList next = result;
        Note* n = find(next);
        if (!n)
            return;
        std::vector<ExprPoint>& curve = expr::points(*n, lane.curve);
        if (index >= curve.size())
            return;
        if (mode == Mode::Bend) {
            if (index + 1 >= curve.size())
                return;
            // Positive tension lifts a rising segment, so dragging up bends toward the pointer either way.
            float direction = curve[index + 1].value >= curve[index].value ? 1.f : -1.f;
            curve[index].tension = std::clamp(startTension + (press.y - pos.y) * kTensionPerPx * direction, -1.f, 1.f);
            value = curve[index].tension;
        } else {
            float x0 = float(lane.axis().tickToX(double(n->start)));
            float x1 = float(lane.axis().tickToX(double(n->end())));
            float p = x1 > x0 ? (pos.x - x0) / (x1 - x0) : 0;
            float v = lane.valueAt(pos.y);
            if (lane.curve == expr::Curve::Pitch && mods.shift)
                v = std::round(v);
            expr::movePoint(curve, index, p, v);
            value = curve[index].value;
        }
        moved = true;
        result = std::move(next);
        view.setPreview(result);
    }
    void commit() override {
        view.clearPreview();
        if (!(result == before))
            view.commit(std::string(added                ? "Add "
                                    : mode == Mode::Bend ? "Bend "
                                                         : "Move ") +
                            lowerName() + (added ? " point" : " curve"),
                        before, result);
    }
    void cancel() override { view.clearPreview(); }
    std::string readout() const override {
        if (mode == Mode::Bend) {
            char text[32];
            snprintf(text, sizeof(text), "Bend %+.0f%%", value * 100);
            return text;
        }
        return lane.describe(value);
    }
    CursorShape cursor() const override { return mode == Mode::Bend ? CursorShape::ResizeV : CursorShape::Move; }

  private:
    Note* find(NoteList& notes) {
        for (Note& n : notes)
            if (n.id == note)
                return &n;
        return nullptr;
    }
    std::string lowerName() const {
        std::string s = expr::info(lane.curve).name;
        s[0] = char(std::tolower(static_cast<unsigned char>(s[0])));
        return s;
    }

    PianoRollView& view;
    ExpressionLane& lane;
    NoteId note;
    size_t index;
    Mode mode;
    Vec press;
    bool added;
    NoteList before, result;
    float startTension = 0, value = 0;
    bool moved = false;
};

} // namespace

float ExpressionLane::low() const { return curve == expr::Curve::Pitch ? -owner.pitchWindow : expr::info(curve).min; }

float ExpressionLane::high() const { return curve == expr::Curve::Pitch ? owner.pitchWindow : expr::info(curve).max; }

void ExpressionLane::step() {
    focus = owner.curveFocus();
    if (focus.valid())
        for (const Note& n : owner.committedNotes())
            if (n.id == focus) {
                focusAxis.setZoomLimits(1e-6, 1e6);
                focusAxis.setViewWidth(std::max(1.f, box.size.x - 2 * kFocusMargin));
                focusAxis.showRange(double(n.start), double(n.end()));
                // showRange keeps its own small margin; the lane's margin goes in front of it.
                focusAxis.scrollByPx(-kFocusMargin);
            }
    InteractiveView::step();
}

const TimeAxis& ExpressionLane::axis() const { return focus.valid() ? focusAxis : owner.viewport.time; }

float ExpressionLane::top() const { return focus.valid() ? kFocusHeader : kPad; }

float ExpressionLane::valueAt(float y) const {
    float h = std::max(1.f, box.size.y - top() - kPad);
    float x = std::clamp(1.f - (y - top()) / h, 0.f, 1.f);
    return low() + x * (high() - low());
}

float ExpressionLane::yFor(float value) const {
    float x = (std::clamp(value, low(), high()) - low()) / (high() - low());
    return top() + (1.f - x) * (box.size.y - top() - kPad);
}

IdSet ExpressionLane::editable() {
    if (focus.valid())
        return IdSet{focus};
    IdSet selected = owner.selectedIds();
    return selected.empty() ? edits::idsOf(owner.committedNotes()) : selected;
}

std::string ExpressionLane::describe(float value) const {
    char text[48];
    if (curve == expr::Curve::Pitch)
        snprintf(text, sizeof(text), "Pitch %+.2f st", double(value));
    else
        snprintf(text, sizeof(text), "%s %.0f%%", expr::info(curve).name, double(value) * 100);
    return text;
}

Rect ExpressionLane::rangeButton() const {
    return focus.valid() ? Rect(box.size.x - 44, 5, 40, 14) : Rect(box.size.x - 44, 3, 40, 14);
}

Rect ExpressionLane::backButton() const { return Rect(6, 4, 56, 16); }

ExpressionLane::Hit ExpressionLane::hitTest(Vec pos) {
    Hit best;
    float bestDistance = kPointHit + 1;
    IdSet allowed = editable();
    const TimeAxis& t = axis();
    float rest = expr::info(curve).rest;
    for (const Note& n : owner.committedNotes()) {
        if (!allowed.count(n.id))
            continue;
        float x0 = float(t.tickToX(double(n.start)));
        float x1 = float(t.tickToX(double(n.end())));
        if (pos.x < x0 - kPointHit || pos.x > x1 + kPointHit)
            continue;
        const std::vector<ExprPoint>& points = expr::points(n, curve);
        for (size_t i = 0; i < points.size(); i++) {
            float px = x0 + points[i].pos * (x1 - x0);
            float d = Vec(px, yFor(points[i].value)).minus(pos).norm();
            if (d < bestDistance) {
                bestDistance = d;
                best = {Hit::Kind::Point, n.id, i};
            }
        }
        for (size_t i = 0; i + 1 < points.size(); i++) {
            float ax = x0 + points[i].pos * (x1 - x0), bx = x0 + points[i + 1].pos * (x1 - x0);
            if (bx - ax < kMinBendSpan)
                continue;
            float mid = (points[i].pos + points[i + 1].pos) / 2;
            float d = Vec((ax + bx) / 2, yFor(expressionAt(points, mid, rest))).minus(pos).norm();
            if (d < bestDistance) {
                bestDistance = d;
                best = {Hit::Kind::Bend, n.id, i};
            }
        }
    }
    if (best.kind != Hit::Kind::None)
        return best;
    // Inside a note's span and near its curve: the note whose curve passes closest. Farther away a drag picks
    // points, so pressing must be close to a curve to add one.
    float closest = kCurveHit;
    for (const Note& n : owner.committedNotes()) {
        if (!allowed.count(n.id))
            continue;
        float x0 = float(t.tickToX(double(n.start)));
        float x1 = float(t.tickToX(double(n.end())));
        if (pos.x < x0 || pos.x > x1)
            continue;
        float p = x1 > x0 ? (pos.x - x0) / (x1 - x0) : 0;
        float d = std::fabs(yFor(expressionAt(expr::points(n, curve), p, rest)) - pos.y);
        if (d < closest) {
            closest = d;
            best = {Hit::Kind::Span, n.id, 0};
        }
    }
    return best;
}

std::unique_ptr<DragGesture> ExpressionLane::startGesture(Vec pos, int button, Modifiers mods) {
    owner.automationFocus = false;
    if (!owner.clip())
        return nullptr;
    if (button == GLFW_MOUSE_BUTTON_LEFT && curve == expr::Curve::Pitch && rangeButton().contains(pos)) {
        owner.openPitchWindowMenu();
        return nullptr;
    }
    if (focus.valid() && backButton().contains(pos)) {
        owner.focusCurve(NoteId());
        return nullptr;
    }
    if (focus.valid() && button == GLFW_MOUSE_BUTTON_RIGHT) {
        doubleClick(pos, mods); // deletes the point under the pointer, if any
        return nullptr;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return nullptr;
    owner.curveLaneFocus = true;
    Hit hit = hitTest(pos);
    // Zoomed onto one hit, a press away from its curve does nothing, and points move on this lane's own time.
    if (focus.valid() && hit.kind == Hit::Kind::None)
        return nullptr;
    if (focus.valid() && hit.kind == Hit::Kind::Point)
        return std::make_unique<CurveGesture>(owner, *this, hit.note, hit.index, CurveGesture::Mode::Move, pos, false,
                                              owner.committedNotes());
    if (hit.kind == Hit::Kind::None) {
        IdSet allowed = editable();
        ExpressionLane* lane = this;
        return noteGestures::pickPoints(
            owner, pos, curve, [lane](const Note&, float v) { return double(lane->yFor(v)); },
            [allowed](const Note& n) { return allowed.count(n.id) > 0; }, false, mods);
    }
    if (hit.kind == Hit::Kind::Point) {
        // Moves the point, or every picked point when it is one of them; a click picks it (Shift adds).
        const Note* n = nullptr;
        for (const Note& c : owner.committedNotes())
            if (c.id == hit.note)
                n = &c;
        if (n && hit.index < expr::points(*n, curve).size()) {
            BendHit grab{hit.note, int(hit.index), expr::points(*n, curve)[hit.index].pos, false};
            ExpressionLane* lane = this;
            return noteGestures::shapeCurve(owner, pos, grab, curve,
                                            [lane](double y) { return double(lane->valueAt(float(y))); });
        }
    }
    NoteList notes = owner.committedNotes();
    if (hit.kind == Hit::Kind::Span) {
        for (Note& n : notes)
            if (n.id == hit.note) {
                float x0 = float(axis().tickToX(double(n.start)));
                float x1 = float(axis().tickToX(double(n.end())));
                float v = valueAt(pos.y);
                if (curve == expr::Curve::Pitch && mods.shift)
                    v = std::round(v);
                hit.index = expr::addPoint(expr::points(n, curve), x1 > x0 ? (pos.x - x0) / (x1 - x0) : 0, v,
                                           expr::info(curve).rest);
            }
        return std::make_unique<CurveGesture>(owner, *this, hit.note, hit.index, CurveGesture::Mode::Move, pos, true,
                                              std::move(notes));
    }
    return std::make_unique<CurveGesture>(owner, *this, hit.note, hit.index,
                                          hit.kind == Hit::Kind::Bend ? CurveGesture::Mode::Bend
                                                                      : CurveGesture::Mode::Move,
                                          pos, false, std::move(notes));
}

void ExpressionLane::doubleClick(Vec pos, Modifiers mods) {
    Hit hit = hitTest(pos);
    if (hit.kind != Hit::Kind::Point)
        return;
    NoteList before = owner.committedNotes();
    NoteList after = before;
    for (Note& n : after)
        if (n.id == hit.note)
            expr::removePoint(expr::points(n, curve), hit.index);
    std::string name = expr::info(curve).name;
    owner.commit("Delete " + name + " point", before, after);
}

void ExpressionLane::wheel(Vec pos, Vec delta, Modifiers mods) {
    if (focus.valid())
        return; // the zoomed hit always fills the lane
    ctx.touchNavigation();
    if (mods.ctrl)
        owner.viewport.time.animateZoomAt(pos.x, wheelZoomFactor(delta.y), kZoomAnimationSeconds);
    else
        owner.viewport.time.scrollByPx(-(delta.y + delta.x));
}

CursorShape ExpressionLane::hoverCursor(Vec pos, Modifiers mods) {
    if (focus.valid() && backButton().contains(pos))
        return CursorShape::Hand;
    if (curve == expr::Curve::Pitch && rangeButton().contains(pos))
        return CursorShape::Hand;
    switch (hitTest(pos).kind) {
    case Hit::Kind::Point:
        return CursorShape::Move;
    case Hit::Kind::Bend:
        return CursorShape::ResizeV;
    case Hit::Kind::Span:
        return CursorShape::AddPoint;
    default:
        return CursorShape::Arrow;
    }
}

void ExpressionLane::drawView(const DrawArgs& args) {
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
    if (!focus.valid())
        timeline::drawGrid(vg, owner.viewport.time, owner.timeSig(), box.size.y, owner.snapStep());
    float rest = expr::info(curve).rest;
    if (curve == expr::Curve::Pitch) {
        // The no-bend line, and a faint line at each octave inside the window.
        nvgBeginPath(vg);
        nvgRect(vg, 0, yFor(0), box.size.x, 1);
        nvgFillColor(vg, c.line2);
        nvgFill(vg);
        nvgBeginPath(vg);
        for (float st = 12; st < owner.pitchWindow; st += 12) {
            nvgRect(vg, 0, yFor(st), box.size.x, 1);
            nvgRect(vg, 0, yFor(-st), box.size.x, 1);
        }
        nvgFillColor(vg, c.line0);
        nvgFill(vg);
    }

    const Track* track = owner.track();
    NVGcolor base = theme::track(track ? track->color : 0x9AA1AB);
    IdSet selected = owner.selectedIds();
    const TimeAxis& t = axis();
    for (const Note& n : owner.notes()) {
        if (focus.valid() && n.id != focus)
            continue;
        float x0 = float(t.tickToX(double(n.start)));
        float x1 = float(t.tickToX(double(n.end())));
        if (x1 < 0)
            continue;
        if (x0 > box.size.x)
            break;
        if (focus.valid())
            drawFocusHeader(vg, n);
        bool editableNote = focus.valid() || selected.empty() || selected.count(n.id);
        NVGcolor color = editableNote ? base : nvgTransRGBAf(base, 0.3f);
        // The note's span, so it is clear where a click adds a point.
        nvgBeginPath(vg);
        nvgRect(vg, x0, top(), std::max(1.f, x1 - x0 - 1), box.size.y - top() - kPad);
        nvgFillColor(vg, nvgTransRGBAf(base, selected.count(n.id) ? 0.12f : 0.05f));
        nvgFill(vg);

        const std::vector<ExprPoint>& points = expr::points(n, curve);
        nvgBeginPath(vg);
        draw::sampleSpan(x0, x1, 0, box.size.x, kSampleStep, [&](float x, bool first) {
            float y = yFor(expressionAt(points, x1 > x0 ? (x - x0) / (x1 - x0) : 0, rest));
            if (first)
                nvgMoveTo(vg, x, y);
            else
                nvgLineTo(vg, x, y);
        });
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, points.empty() ? 1.f : 1.5f);
        nvgStroke(vg);
        if (!editableNote)
            continue;
        for (size_t i = 0; i < points.size(); i++) {
            bool picked = owner.pointPicked(n.id, curve, points[i]);
            nvgBeginPath(vg);
            nvgCircle(vg, x0 + points[i].pos * (x1 - x0), yFor(points[i].value),
                      picked ? kPointRadius + 1 : kPointRadius);
            nvgFillColor(vg, picked ? theme::colors().select : color);
            nvgFill(vg);
        }
        for (size_t i = 0; i + 1 < points.size(); i++) {
            float ax = x0 + points[i].pos * (x1 - x0), bx = x0 + points[i + 1].pos * (x1 - x0);
            if (bx - ax < kMinBendSpan)
                continue;
            float mid = (points[i].pos + points[i + 1].pos) / 2;
            nvgBeginPath(vg);
            nvgCircle(vg, (ax + bx) / 2, yFor(expressionAt(points, mid, rest)), kBendRadius);
            nvgStrokeColor(vg, color);
            nvgStrokeWidth(vg, 1);
            nvgStroke(vg);
        }
    }

    if (curve == expr::Curve::Pitch) {
        Rect b = rangeButton();
        draw::roundedRect(vg, b, 3, c.bg2);
        draw::TextStyle style{theme::fonts::mono(), theme::metrics::kTextRuler, c.text1,
                              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        char text[16];
        snprintf(text, sizeof(text), "±%.0f", double(owner.pitchWindow));
        draw::text(vg, style, b.getCenter().x, b.getCenter().y, text);
    }
    nvgRestore(vg);
}

void ExpressionLane::drawFocusHeader(NVGcontext* vg, const Note& note) {
    const theme::Palette& c = theme::colors();
    Rect back = backButton();
    draw::roundedRect(vg, back, theme::metrics::kRadiusControl, c.bg2);
    draw::TextStyle button{theme::fonts::ui(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, button, back.getCenter().x, back.getCenter().y, "← Back");

    std::string what = expr::info(curve).name;
    what[0] = char(std::tolower(static_cast<unsigned char>(what[0])));
    const DrumLane* lane = owner.laneOf(note.pitch);
    std::string title = (lane ? lane->name : std::string("Hit")) + "  ·  hit at " +
                        fields::format(fields::Field::Start, double(note.start) / kPpq, owner.timeSig()) + "  ·  " +
                        what;
    draw::TextStyle titleStyle{theme::fonts::uiStrong(), theme::metrics::kTextUi, c.text0,
                               NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    float x = back.getRight() + 10;
    draw::text(vg, titleStyle, x, back.getCenter().y, title);
    // A trigger is over in 1 ms, but the curve still plays across the written hit: say how long that is.
    std::string hint = fields::format(fields::Field::Length, double(note.length) / kPpq, owner.timeSig()) +
                       ", then holds until the next hit";
    draw::TextStyle hintStyle{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2,
                              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    float right = (curve == expr::Curve::Pitch ? rangeButton().pos.x : box.size.x) - 8;
    float titleRight = x + draw::textWidth(vg, titleStyle, title) + 16;
    if (right - titleRight > 60)
        draw::text(vg, hintStyle, right, back.getCenter().y, draw::ellipsize(vg, hintStyle, hint, right - titleRight));
}

} // namespace tessera
