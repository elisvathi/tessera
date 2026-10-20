#include "NoteGrid.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/NoteFields.hpp"
#include "../../model/NoteOperators.hpp"
#include "../../model/TrackColors.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../editing/TransformMenu.hpp"
#include "../interaction/ClickOrDrag.hpp"
#include "../widgets/CachedLayer.hpp"
#include "../widgets/ColorMenuItem.hpp"
#include "../widgets/Draw.hpp"
#include "ExpressionLane.hpp"
#include "PianoRoll.hpp"
#include "TimelineDraw.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tessera {

using edits::IdSet;
using edits::NoteList;
using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kOffscreenArrowZone = 18;
constexpr float kMinArchSpan = 14;    // px: narrower glides hide their arch handle (as hitTestCurves does)
constexpr float kStripMinHeight = 44; // the timbre / pressure strip: four rows, within these bounds
constexpr float kStripMaxHeight = 96;
constexpr float kStripGap = 2; // between the strip and its notes
constexpr float kStripPad = 4; // keeps 0% and 100% off the strip's edges
constexpr float kStripMinWidth = 8;
constexpr float kMinLabelWidth = 28; // note labels appear from this width (EDITOR_UX §8)
constexpr float kMinLabelRow = 12;
constexpr int kEmptyLow = 48; // C3..C5 when a clip has no notes
constexpr int kEmptyHigh = 72;
constexpr float kHandle = 7;    // transform box handle size
constexpr float kHandleHit = 6; // extra grab distance around a handle
constexpr float kOffKeyMark = 7;
constexpr float kMinTickGap = 3;        // px between repeat ticks, or they are not drawn
constexpr float kDotPitch = 4;          // px between recurrence dots
constexpr float kMinBadgeRow = 8;       // rows lower than this get no badges
constexpr float kMinVelocityWidth = 44; // notes this wide show their velocity on hover
constexpr float kSustainGlow = 0.5f;    // how bright a sounding note stays after its start flash

} // namespace

NoteGrid::NoteGrid(EditorContext& ctx, PianoRollView& owner) : TimelineView(ctx, owner.viewport), owner(owner) {
    layer = new CachedLayer([this](NVGcontext* vg, Vec size) { paintGrid(vg, size); });
    addChild(layer);
}

double NoteGrid::verticalScroll() { return owner.pitch.top() * owner.pitch.rowHeight(); }

void NoteGrid::setVerticalScroll(double px) {
    owner.pitch.restore({px / owner.pitch.rowHeight(), owner.pitch.rowHeight()});
}

void NoteGrid::zoomVertical(double y, double factor) { owner.pitch.zoomAt(y, factor); }

void NoteGrid::showVerticalRange(double y0, double y1) {
    owner.pitch.showPitches(owner.pitch.yToPitch(y1), owner.pitch.yToPitch(y0));
}

double NoteGrid::playhead() { return owner.source.playhead(); }

void NoteGrid::zoomToFit() {
    const Clip* clip = owner.clip();
    double start = 0, end = double(ticksPerBar(owner.timeSig()));
    int low = kEmptyLow, high = kEmptyHigh;
    std::vector<int> pitches;
    if (clip) {
        start = double(clip->loopStart);
        end = double(clip->loopEnd);
        IdSet selected = owner.selectedIds();
        bool any = false;
        for (const Note& n : clip->notes) {
            if (!selected.empty() && !selected.count(n.id))
                continue;
            pitches.push_back(n.pitch);
            if (!any) {
                start = double(n.start);
                end = double(n.end());
                low = high = n.pitch;
                any = true;
            }
            start = std::min(start, double(n.start));
            end = std::max(end, double(n.end()));
            low = std::min(low, int(n.pitch));
            high = std::max(high, int(n.pitch));
        }
    }
    // Never zoom tighter than a bar and an octave: one short note shouldn't fill the screen.
    double bar = double(ticksPerBar(owner.timeSig()));
    if (end - start < bar) {
        double mid = (start + end) / 2;
        start = std::max(0.0, mid - bar / 2);
        end = start + bar;
    }
    if (high - low < 12) {
        int mid = (low + high) / 2;
        low = std::max(0, mid - 6);
        high = std::min(127, low + 12);
    }
    // Room after the end, so the loop end can be grabbed and dragged longer.
    end += (end - start) / 4;
    TimeAxis fit = time();
    fit.showRange(start, end);
    viewport.jump(fit.state());
    // Rows stay tall enough to carry note names: in a short pane (the in-panel editor) an octave would squeeze
    // them into thin lines. When the notes don't fit at that height, the view centres on their median pitch, where
    // most of them are, and the rest is a scroll away.
    int fits = std::max(1, int(owner.pitch.viewHeight() / kMinLabelRow) - 2);
    if (high - low + 1 > fits && !pitches.empty()) {
        std::nth_element(pitches.begin(), pitches.begin() + pitches.size() / 2, pitches.end());
        int median = pitches[pitches.size() / 2];
        low = std::clamp(median - fits / 2, low, high);
        high = std::min(high, low + fits - 1);
    }
    owner.pitch.showPitches(low, high, kMinLabelRow);
}

void NoteGrid::step() {
    layer->setSize(box.size);
    owner.pitch.setViewHeight(box.size.y);
    uint64_t key = 0;
    key = hashMix(key, time().left());
    key = hashMix(key, time().pxPerTick());
    key = hashMix(key, owner.pitch.top());
    key = hashMix(key, owner.pitch.rowHeight());
    key = hashMix(key, double(owner.source.loopStart()));
    key = hashMix(key, double(owner.source.loopEnd()));
    key = hashMix(key, owner.source.loopOn() ? 1 : 0);
    key = hashMix(key, double(owner.clipId().value));
    if (ctx.module()) {
        key = hashMix(key, double(owner.snapStep()));
        key = hashMix(key, double(owner.timeSig().num * 64 + owner.timeSig().den));
        Scale scale = owner.scale();
        key = hashMix(key, double(owner.highlightKey() ? scale.root() * 4096 + scale.mask() + 1 : 0));
        if (owner.pitch.folded())
            for (int r = 0; r < owner.pitch.rowCount(); r++)
                key = hashMix(key, double(owner.pitch.pitchAtRow(r)));
    }
    layer->setKey(key);
    TimelineView::step();
}

// ---- Hit testing --------------------------------------------------------------------------------------------

NoteHit NoteGrid::hitAt(Vec pos) {
    return hitTestNotes(owner.committedNotes(), time(), owner.pitch, pos.x, pos.y, owner.selectedIds(),
                        theme::metrics::kEdgeHit);
}

BendHit NoteGrid::selectedBendAt(Vec pos) {
    IdSet selected = owner.selectedIds();
    if (selected.empty())
        return BendHit();
    auto isSelected = [&selected](const Note& n) { return selected.count(n.id) > 0; };
    return hitTestCurves(owner.committedNotes(), time(), expr::Curve::Pitch, pitchCurveY(), pos.x, pos.y,
                         theme::metrics::kEdgeHit, 0, isSelected);
}

BendHit NoteGrid::bendAt(Vec pos) {
    return hitTestBends(owner.committedNotes(), time(), owner.pitch, pos.x, pos.y, theme::metrics::kEdgeHit,
                        theme::metrics::kEdgeHit);
}

void NoteGrid::removeCurvePoint(const BendHit& hit, expr::Curve curve) {
    NoteList before = owner.committedNotes();
    NoteList after = before;
    float rest = expr::info(curve).rest;
    for (Note& n : after) {
        std::vector<ExprPoint>& points = expr::points(n, curve);
        if (n.id != hit.id || hit.point < 0 || size_t(hit.point) >= points.size())
            continue;
        expr::removePoint(points, size_t(hit.point));
        // Only resting points left: the note plays straight again, so drop the curve altogether.
        if (std::all_of(points.begin(), points.end(), [rest](const ExprPoint& p) { return p.value == rest; }))
            points.clear();
    }
    std::string what = curve == expr::Curve::Pitch ? "bend" : expr::info(curve).name;
    if (curve != expr::Curve::Pitch)
        what[0] = char(std::tolower(static_cast<unsigned char>(what[0])));
    owner.commit("Remove " + what + " point", before, after);
}

std::vector<NoteGrid::CurveStrip> NoteGrid::curveStrips(const NoteList& notes) {
    std::vector<CurveStrip> strips;
    if (!owner.stripCurve())
        return strips;
    IdSet selected = owner.selectedIds();
    PitchAxis& pitch = owner.pitch;
    float row = float(pitch.rowHeight());
    float height = std::clamp(4 * row, kStripMinHeight, kStripMaxHeight);
    // Notes are sorted by start: notes that overlap in time share one strip, above the highest of them.
    CurveStrip* open = nullptr;
    Tick openEnd = 0;
    int low = 0, high = 0;
    auto place = [&](CurveStrip& s) {
        float top = float(pitch.pitchToY(high)) - height - kStripGap;
        if (top < 0)
            top = float(pitch.pitchToY(low)) + row + kStripGap; // no room above: below the lowest note
        float x0 = float(time().tickToX(double(s.start))), x1 = float(time().tickToX(double(s.end)));
        s.r = Rect(x0, top, std::max(x1 - x0, kStripMinWidth), height);
    };
    for (const Note& n : notes) {
        if (!selected.count(n.id))
            continue;
        if (!open || n.start >= openEnd) {
            if (open)
                place(*open);
            strips.push_back({Rect(), {}, n.id, n.start, n.end()});
            open = &strips.back();
            openEnd = n.end();
            low = high = n.pitch;
        }
        open->notes.push_back(n.id);
        open->end = std::max(open->end, n.end());
        openEnd = open->end;
        if (n.pitch > high) {
            high = n.pitch;
            open->lead = n.id;
        }
        low = std::min(low, int(n.pitch));
    }
    if (open)
        place(*open);
    return strips;
}

const NoteGrid::CurveStrip* NoteGrid::stripAt(Vec pos) {
    strips = curveStrips(owner.committedNotes());
    for (const CurveStrip& s : strips)
        if (s.r.contains(pos))
            return &s;
    return nullptr;
}

float NoteGrid::stripValueAt(const CurveStrip& s, double y) const {
    float inner = s.r.size.y - 2 * kStripPad;
    return std::clamp(float((s.r.getBottom() - kStripPad - y) / inner), 0.f, 1.f);
}

double NoteGrid::stripY(const CurveStrip& s, float value) const {
    return s.r.getBottom() - kStripPad - double(value) * (s.r.size.y - 2 * kStripPad);
}

BendHit NoteGrid::stripHit(const CurveStrip& s, Vec pos) {
    std::optional<expr::Curve> curve = owner.stripCurve();
    if (!curve)
        return BendHit();
    CurveStrip strip = s;
    CurveY yOf = [this, strip](const Note&, float v) { return stripY(strip, v); };
    auto inStrip = [&strip](const Note& n) {
        return std::find(strip.notes.begin(), strip.notes.end(), n.id) != strip.notes.end();
    };
    return hitTestCurves(owner.committedNotes(), time(), *curve, yOf, pos.x, pos.y, theme::metrics::kEdgeHit,
                         theme::metrics::kEdgeHit, inStrip);
}

std::unique_ptr<DragGesture> NoteGrid::stripGesture(const CurveStrip& s, Vec pos, Modifiers mods) {
    expr::Curve curve = *owner.stripCurve();
    BendHit hit = stripHit(s, pos);
    if (!hit.any()) {
        // Away from the curves, a drag picks points to move together.
        CurveStrip strip = s;
        CurveY yOf = [this, strip](const Note&, float v) { return stripY(strip, v); };
        auto inStrip = [strip](const Note& n) {
            return std::find(strip.notes.begin(), strip.notes.end(), n.id) != strip.notes.end();
        };
        return noteGestures::pickPoints(owner, pos, curve, yOf, inStrip, false, mods);
    }
    const Note* n = owner.clip()->findNote(hit.id);
    if (!n)
        return nullptr;
    const std::vector<ExprPoint>& points = expr::points(*n, curve);
    if (mods.alt && hit.point < 0) {
        int segment = segmentAt(points, hit.pos);
        if (segment >= 0 && points[size_t(segment)].value != points[size_t(segment) + 1].value) {
            hit.point = segment;
            hit.segment = true;
        }
    }
    if (hit.segment)
        return noteGestures::arch(owner, pos, hit, curve);
    if (hit.point < 0 && !mods.alt && n->length > 0) {
        double at = double(n->start) + double(hit.pos) * double(n->length);
        Tick snapped = snapPosition(at, owner.snapStep());
        hit.pos = std::clamp(float(double(snapped - n->start) / double(n->length)), 0.f, 1.f);
    }
    CurveStrip strip = s;
    return noteGestures::shapeCurve(owner, pos, hit, curve,
                                    [this, strip](double y) { return double(stripValueAt(strip, y)); });
}

Rect NoteGrid::transformFrame() {
    EditorState& state = ctx.state();
    IdSet selected = owner.selectedIds();
    if (!state.transformBox || selected.size() < 2 || state.tool != Tool::Pointer)
        return Rect();
    const NoteList& notes = owner.notes();
    auto [t0, t1] = edits::span(notes, selected);
    int low = 127, high = 0;
    for (const Note& n : notes)
        if (selected.count(n.id)) {
            low = std::min(low, int(n.pitch));
            high = std::max(high, int(n.pitch));
        }
    double x0 = time().tickToX(double(t0)), x1 = time().tickToX(double(t1));
    double y0 = owner.pitch.pitchToY(high), y1 = owner.pitch.pitchToY(low) + owner.pitch.rowHeight();
    return Rect(float(x0), float(y0), float(x1 - x0), float(y1 - y0));
}

noteGestures::BoxHandle NoteGrid::boxHandleAt(Vec pos) {
    using H = noteGestures::BoxHandle;
    Rect f = transformFrame();
    if (f.size.x <= 0)
        return H::None;
    float l = f.pos.x, r = f.pos.x + f.size.x, t = f.pos.y, b = f.pos.y + f.size.y;
    float cx = (l + r) / 2, cy = (t + b) / 2;
    auto near = [&](float x, float y) {
        return std::fabs(pos.x - x) <= kHandleHit && std::fabs(pos.y - y) <= kHandleHit;
    };
    if (near(l, t))
        return H::TopLeft;
    if (near(r, t))
        return H::TopRight;
    if (near(l, b))
        return H::BottomLeft;
    if (near(r, b))
        return H::BottomRight;
    if (near(l, cy))
        return H::Left;
    if (near(r, cy))
        return H::Right;
    if (near(cx, t))
        return H::Top;
    if (near(cx, b))
        return H::Bottom;
    return H::None;
}

void NoteGrid::selectForPress(NoteId id, Modifiers mods) {
    if (owner.isSelected(id))
        return;
    IdSet next = (mods.shift || mods.ctrl) ? owner.selectedIds() : IdSet{};
    next.insert(id);
    owner.select(next);
}

// ---- Mouse --------------------------------------------------------------------------------------------------

std::unique_ptr<DragGesture> NoteGrid::startGesture(Vec pos, int button, Modifiers mods) {
    owner.automationFocus = false;
    owner.curveLaneFocus = false;
    if (auto nav = navigationGesture(pos, button, mods))
        return nav;
    if (!owner.clip())
        return nullptr;
    Tool tool = ctx.state().tool;
    // Layered editing: the clip strip, and a click on another clip's note edits that clip.
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (layerClose.contains(pos)) {
            ctx.state().layered.clear();
            return nullptr;
        }
        for (const auto& [r, id] : layerChips)
            if (r.contains(pos)) {
                if (TesseraModule* m = ctx.module())
                    switchTo(clipActions::trackOf(m->song(), id), id);
                return nullptr;
            }
        if (!hitAt(pos).any() && !mods.alt)
            if (const Ghost* g = ghostAt(pos); g && g->layer) {
                switchTo(g->track, g->clip, g->note.id);
                return nullptr;
            }
    }
    if (const CurveStrip* strip = stripAt(pos)) {
        if (button != GLFW_MOUSE_BUTTON_LEFT)
            return nullptr; // a right-click deletes a point when released (click)
        return stripGesture(*strip, pos, mods);
    }
    // Alt+Shift+click on a ghost note opens its clip.
    if (button == GLFW_MOUSE_BUTTON_LEFT && mods.alt && mods.shift && !mods.ctrl)
        if (const Ghost* g = ghostAt(pos)) {
            ctx.state().selectedTrack = g->track;
            ctx.state().selectedClip = g->clip;
            ctx.state().selection = NoteSelection{g->clip, {g->note.id}};
            return nullptr;
        }

    // Ctrl/Cmd+drag from empty space selects with any tool, so there is no need to switch tools to select;
    // from a note it copies. Ctrl/Cmd+Alt+drag selects wherever it starts.
    if (button == GLFW_MOUSE_BUTTON_LEFT && mods.ctrl && (mods.alt || !hitAt(pos).any()))
        return noteGestures::rubberBand(owner, pos, mods);
    // In the Draw tool, right-click and right-drag delete.
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
        return tool == Tool::Draw ? noteGestures::erase(owner, pos) : nullptr;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return nullptr;

    if (tool == Tool::Pointer) {
        noteGestures::BoxHandle handle = boxHandleAt(pos);
        if (handle != noteGestures::BoxHandle::None)
            return noteGestures::transformBox(owner, pos, handle);
    }

    NoteHit hit = hitAt(pos);
    // While the lower pane shows Pitch, Select and Draw bend the notes instead of moving them: picking the lane is
    // the switch, so there is no extra tool. Edges still resize; Draw still draws in empty space.
    if (owner.bendMode() && (tool == Tool::Pointer || tool == Tool::Draw)) {
        BendHit bend = bendAt(pos);
        bool edge = hit.part == NoteHit::Part::StartEdge || hit.part == NoteHit::Part::EndEdge;
        if (bend.point < 0 && edge) {
            selectForPress(hit.id, mods);
            return noteGestures::resize(owner, pos, hit.id, hit.part == NoteHit::Part::StartEdge, false);
        }
        if (bend.any())
            return pitchCurveGesture(bend, pos, mods);
        return tool == Tool::Draw
                   ? noteGestures::draw(owner, pos)
                   : noteGestures::pickPoints(owner, pos, expr::Curve::Pitch, pitchCurveY(), nullptr, true, mods);
    }
    switch (tool) {
    case Tool::Pointer:
    case Tool::Draw: {
        if (tool == Tool::Pointer) {
            // The bend points and arch handles of selected notes are drawn, so they can be dragged right here.
            BendHit bend = selectedBendAt(pos);
            if (bend.point >= 0)
                return pitchCurveGesture(bend, pos, mods);
            owner.curvePoints.clear(); // anything else picked: Delete is for the notes again
        }
        if (!hit.any())
            // Select's box picks the bend points inside it too, so they can then be dragged together.
            return tool == Tool::Draw
                       ? noteGestures::draw(owner, pos)
                       : noteGestures::pickPoints(owner, pos, expr::Curve::Pitch, pitchCurveY(), nullptr, true, mods);
        bool wasSelected = owner.isSelected(hit.id);
        selectForPress(hit.id, mods);
        NoteId id = hit.id;
        if (hit.part != NoteHit::Part::Body)
            return noteGestures::resize(owner, pos, id, hit.part == NoteHit::Part::StartEdge, mods.alt && mods.shift);
        if (mods.alt)
            return noteGestures::velocity(owner, pos, id);
        // A click without a drag: Ctrl/Cmd toggles, a plain click on a group picks just this note.
        auto onClick = [this, id, wasSelected, mods] {
            if (mods.ctrl && wasSelected) {
                IdSet next = owner.selectedIds();
                next.erase(id);
                owner.select(next);
            } else if (!mods.shift && !mods.ctrl) {
                owner.select(IdSet{id});
            }
            if (const Note* n = owner.clip()->findNote(id)) {
                owner.audition(n->pitch, n->velocity);
                ctx.state().lastLength = n->length;
                ctx.state().lastVelocity = n->velocity;
                ctx.state().lastChannel = n->channel;
            }
        };
        return std::make_unique<ClickOrDrag>(noteGestures::move(owner, pos, id), pos, onClick);
    }
    case Tool::Paint:
        return noteGestures::paint(owner, pos);
    case Tool::Erase:
        return noteGestures::erase(owner, pos);
    case Tool::Mute:
        return noteGestures::mute(owner, pos);
    case Tool::Slice:
        if (mods.shift && hit.part == NoteHit::Part::Body) {
            openSliceMenu(hit.id);
            return nullptr;
        }
        return noteGestures::slice(owner, pos);
    case Tool::Velocity:
        return hit.any() ? noteGestures::velocity(owner, pos, hit.id) : nullptr;
    }
    return nullptr;
}

std::unique_ptr<DragGesture> NoteGrid::pitchCurveGesture(BendHit bend, Vec pos, Modifiers mods) {
    if (mods.alt && bend.point < 0)
        if (const Note* n = owner.clip()->findNote(bend.id)) {
            // Alt on a glide arches it; Alt elsewhere still places a point off the grid.
            int segment = segmentAt(n->pitchExpr, bend.pos);
            if (segment >= 0 && n->pitchExpr[size_t(segment)].value != n->pitchExpr[size_t(segment) + 1].value) {
                bend.point = segment;
                bend.segment = true;
            }
        }
    if (bend.segment)
        return noteGestures::arch(owner, pos, bend, expr::Curve::Pitch);
    if (bend.point < 0 && !mods.alt)
        if (const Note* n = owner.clip()->findNote(bend.id); n && n->length > 0) {
            // A new point lands on the grid, like a drawn note.
            double at = double(n->start) + double(bend.pos) * double(n->length);
            Tick snapped = snapPosition(at, owner.snapStep());
            bend.pos = std::clamp(float(double(snapped - n->start) / double(n->length)), 0.f, 1.f);
        }
    return noteGestures::bend(owner, pos, bend);
}

CurveY NoteGrid::pitchCurveY() {
    PitchAxis* axis = &owner.pitch;
    return [axis](const Note& n, float value) { return axis->bentPitchToY(n.pitch + double(value)); };
}

void NoteGrid::click(Vec pos, int button, Modifiers mods) {
    double p = playhead();
    if (button == GLFW_MOUSE_BUTTON_LEFT && std::isfinite(p)) {
        // The off-screen playhead arrow jumps back to the playhead.
        float x = float(time().tickToX(p));
        bool left = x < 0 && pos.x < kOffscreenArrowZone;
        bool right = x > box.size.x && pos.x > box.size.x - kOffscreenArrowZone;
        if ((left || right) && std::fabs(pos.y - box.size.y / 2) < kOffscreenArrowZone) {
            time().scrollTo(p - time().visibleTicks() / 3);
            return;
        }
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT && owner.clip())
        if (const CurveStrip* strip = stripAt(pos)) {
            BendHit hit = stripHit(*strip, pos);
            if (hit.point >= 0 && !hit.segment)
                removeCurvePoint(hit, *owner.stripCurve());
            return;
        }
    if (button == GLFW_MOUSE_BUTTON_RIGHT && owner.clip()) {
        BendHit bend = owner.bendMode() ? bendAt(pos) : selectedBendAt(pos);
        if (bend.point >= 0 && !bend.segment) {
            removeCurvePoint(bend, expr::Curve::Pitch);
            return;
        }
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT && owner.clip()) {
        NoteHit hit = hitAt(pos);
        if (hit.any()) {
            selectForPress(hit.id, mods);
            openNoteMenu(hit.id);
        } else {
            openGridMenu();
        }
    }
}

void NoteGrid::doubleClick(Vec pos, Modifiers mods) {
    if (!owner.clip()) {
        owner.createClip();
        return;
    }
    if (hitAt(pos).any())
        return;
    // Double-clicking empty space creates a note with any tool (EDITOR_UX §4.2).
    auto gesture = noteGestures::draw(owner, pos);
    gesture->update(pos, mods);
    gesture->commit();
}

CursorShape NoteGrid::hoverCursor(Vec pos, Modifiers mods) {
    hoverPos = pos;
    hovering = true;
    if (ctx.spaceHeld || ctx.zoomToolHeld)
        return TimelineView::hoverCursor(pos, mods);
    if (!owner.clip()) {
        hovered = NoteId();
        return CursorShape::Arrow;
    }
    if (const CurveStrip* strip = stripAt(pos)) {
        hovered = NoteId();
        BendHit h = stripHit(*strip, pos);
        if (!h.any())
            return CursorShape::Arrow;
        return h.segment ? CursorShape::ResizeV : h.point >= 0 ? CursorShape::Move : CursorShape::AddPoint;
    }
    NoteHit hit = hitAt(pos);
    hovered = hit.id;
    Tool tool = ctx.state().tool;
    if (tool == Tool::Pointer) {
        using H = noteGestures::BoxHandle;
        H handle = boxHandleAt(pos);
        if (handle == H::Left || handle == H::Right)
            return CursorShape::ResizeH;
        if (handle == H::Top || handle == H::Bottom)
            return CursorShape::ResizeV;
        if (handle != H::None)
            return CursorShape::Move;
    }
    if (owner.bendMode() && (tool == Tool::Pointer || tool == Tool::Draw)) {
        BendHit bend = bendAt(pos);
        if (bend.point < 0 && (hit.part == NoteHit::Part::StartEdge || hit.part == NoteHit::Part::EndEdge))
            return CursorShape::ResizeH;
        if (bend.any())
            return bend.segment ? CursorShape::ResizeV : bend.point >= 0 ? CursorShape::Move : CursorShape::AddPoint;
        return tool == Tool::Draw ? CursorShape::Crosshair : CursorShape::Arrow;
    }
    if (tool == Tool::Pointer) {
        BendHit bend = selectedBendAt(pos);
        if (bend.point >= 0)
            return bend.segment ? CursorShape::ResizeV : CursorShape::Move;
    }
    switch (tool) {
    case Tool::Pointer:
    case Tool::Draw:
        if (hit.part == NoteHit::Part::StartEdge || hit.part == NoteHit::Part::EndEdge)
            return CursorShape::ResizeH;
        if (hit.any())
            return mods.alt ? CursorShape::ResizeV : CursorShape::Move;
        return tool == Tool::Draw ? CursorShape::Crosshair : CursorShape::Arrow;
    case Tool::Paint:
        return CursorShape::Crosshair;
    case Tool::Erase:
        return CursorShape::NotAllowed;
    case Tool::Slice:
        return CursorShape::IBeam;
    case Tool::Mute:
        return CursorShape::Hand;
    case Tool::Velocity:
        return hit.any() ? CursorShape::ResizeV : CursorShape::Arrow;
    }
    return CursorShape::Arrow;
}

void NoteGrid::onLeave(const LeaveEvent& e) {
    hovering = false;
    hovered = NoteId();
    TimelineView::onLeave(e);
}

// ---- Menus --------------------------------------------------------------------------------------------------

void NoteGrid::openNoteMenu(NoteId id) {
    rack::ui::Menu* menu = rack::createMenu();
    PianoRollView* view = &owner;
    size_t count = owner.selectedIds().size();
    menu->addChild(rack::createMenuLabel(countNotes(count)));
    menu->addChild(
        rack::createMenuItem("Delete", shortcutLabel(Action::Delete), [view] { view->handleAction(Action::Delete); }));
    menu->addChild(rack::createMenuItem("Duplicate", shortcutLabel(Action::Duplicate),
                                        [view] { view->handleAction(Action::Duplicate); }));
    menu->addChild(
        rack::createMenuItem("Copy", shortcutLabel(Action::Copy), [view] { view->handleAction(Action::Copy); }));
    menu->addChild(
        rack::createMenuItem("Cut", shortcutLabel(Action::Cut), [view] { view->handleAction(Action::Cut); }));
    menu->addChild(rack::createMenuItem("Mute / unmute", "", [view] {
        IdSet ids = view->selectedIds();
        NoteList before = view->committedNotes();
        bool anyPlaying = false;
        for (const Note& n : before)
            if (ids.count(n.id) && !n.muted)
                anyPlaying = true;
        view->commit(std::string(anyPlaying ? "Mute " : "Unmute ") + countNotes(ids.size()), before,
                     edits::setMuted(before, ids, anyPlaying));
    }));
    // The selection's channel, when the notes share one.
    int shared = 0;
    for (const Note& n : owner.committedNotes())
        if (owner.selectedIds().count(n.id))
            shared = shared == 0 || shared == n.channel ? n.channel : -1;
    const Track* track = owner.track();
    uint32_t base = track ? track->color : 0x9AA1AB;
    menu->addChild(rack::createSubmenuItem(
        "Channel", shared > 0 ? std::to_string(shared) : "mixed", [view, shared, base](rack::ui::Menu* sub) {
            for (int ch = 1; ch <= kMaxNoteChannel; ch++)
                sub->addChild(new ColorMenuItem(
                    "Channel " + std::to_string(ch), channelColor(base, ch), shared == ch, [view, ch] {
                        IdSet ids = view->selectedIds();
                        NoteList before = view->committedNotes();
                        view->commit("Put " + countNotes(ids.size()) + " on channel " + std::to_string(ch), before,
                                     fields::setAll(before, ids, fields::Field::Channel, ch));
                        view->context().state().lastChannel = uint8_t(ch);
                    }));
        }));
    if (owner.keyActive())
        menu->addChild(rack::createMenuItem("Fit to key", "nearest key note", [view] {
            IdSet ids = view->selectedIds();
            NoteList before = view->committedNotes();
            NoteList after = edits::resolveOverlaps(edits::fitToScale(before, ids, view->scale()), ids);
            view->commit("Fit " + countNotes(ids.size()) + " to key", before, after);
        }));
    menu->addChild(
        rack::createSubmenuItem("Transform", "", [view](rack::ui::Menu* sub) { transformMenu::addItems(sub, *view); }));
    menu->addChild(rack::createMenuItem("Select all notes of this pitch", "", [view, id] {
        const Note* n = view->clip() ? view->clip()->findNote(id) : nullptr;
        if (!n)
            return;
        IdSet same;
        for (const Note& other : view->committedNotes())
            if (other.pitch == n->pitch)
                same.insert(other.id);
        view->select(same);
    }));
    menu->addChild(rack::createMenuItem("Select all notes on this channel", "", [view, id] {
        const Note* n = view->clip() ? view->clip()->findNote(id) : nullptr;
        if (!n)
            return;
        IdSet same;
        for (const Note& other : view->committedNotes())
            if (other.channel == n->channel)
                same.insert(other.id);
        view->select(same);
    }));
}

void NoteGrid::openGridMenu() {
    rack::ui::Menu* menu = rack::createMenu();
    PianoRollView* view = &owner;
    menu->addChild(
        rack::createMenuItem("Paste", shortcutLabel(Action::Paste), [view] { view->handleAction(Action::Paste); }));
    menu->addChild(rack::createMenuItem("Select all", shortcutLabel(Action::SelectAll),
                                        [view] { view->handleAction(Action::SelectAll); }));
    menu->addChild(
        rack::createSubmenuItem("Transform", "", [view](rack::ui::Menu* sub) { transformMenu::addItems(sub, *view); }));
}

void NoteGrid::openSliceMenu(NoteId id) {
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Split into equal parts"));
    PianoRollView* view = &owner;
    for (int parts : {2, 3, 4, 6, 8}) {
        menu->addChild(rack::createMenuItem(std::to_string(parts) + " parts", "", [view, id, parts] {
            NoteList before = view->committedNotes();
            NoteList after = edits::sliceEqual(before, id, parts, [view] { return view->newNoteId(); });
            view->commit("Split note into " + std::to_string(parts), before, after);
        }));
    }
}

// ---- Drawing ------------------------------------------------------------------------------------------------

void NoteGrid::drawView(const DrawArgs& args) {
    nvgBeginPath(args.vg);
    nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(args.vg, theme::colors().bg0);
    nvgFill(args.vg);
}

void NoteGrid::paintGrid(NVGcontext* vg, Vec size) {
    if (!ctx.module())
        return;
    const theme::Palette& c = theme::colors();
    PitchAxis& pitch = owner.pitch;
    float row = float(pitch.rowHeight());

    // Rows: black-key rows a shade darker, a stronger line under each C so octaves read at a glance. With a
    // key highlighted, the key's rows are the light ones, rows outside it dim, and the line marks each root.
    bool highlight = owner.highlightKey();
    Scale key = owner.scale();
    NVGcolor light = c.bg0;
    int first = std::max(0, pitch.yToRow(0));
    int last = std::min(pitch.rowCount() - 1, pitch.yToRow(size.y));
    for (int r = first; r <= last; r++) {
        int p = pitch.pitchAtRow(r);
        float y = float(pitch.pitchToY(p));
        NVGcolor fill = isBlackKey(p) ? theme::recessed(c, c.bg0, 0.14f) : light;
        bool octave = p % 12 == 0;
        if (highlight) {
            fill = key.inScale(p) ? light : theme::recessed(c, c.bg0, 0.22f);
            if (key.isRoot(p))
                fill = nvgLerpRGBA(light, c.accent, 0.08f);
            octave = key.isRoot(p);
        }
        nvgBeginPath(vg);
        nvgRect(vg, 0, y, size.x, row);
        nvgFillColor(vg, fill);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, 0, y + row - 1, size.x, 1);
        nvgFillColor(vg, octave ? c.line1 : nvgTransRGBAf(c.line0, 0.6f));
        nvgFill(vg);
    }
    timeline::drawGrid(vg, time(), owner.timeSig(), size.y, owner.snapStep());

    // Content outside the clip loop is dimmed. Without a loop every note plays, so nothing is.
    if (!owner.source.loopOn())
        return;
    float loopX0 = float(time().tickToX(double(owner.source.loopStart())));
    float loopX1 = float(time().tickToX(double(owner.source.loopEnd())));
    nvgBeginPath(vg);
    if (loopX0 > 0)
        nvgRect(vg, 0, 0, loopX0, size.y);
    if (loopX1 < size.x)
        nvgRect(vg, std::max(0.f, loopX1), 0, size.x - std::max(0.f, loopX1), size.y);
    nvgFillColor(vg, nvgTransRGBAf(c.bg0, 0.55f));
    nvgFill(vg);
}

void NoteGrid::drawForeground(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    if (owner.clip()) {
        drawHoverGuide(vg);
        collectGhosts();
        drawGhosts(vg);
        drawTimelineRepeats(vg);
        drawNotes(vg);
        drawBends(vg);
        drawStrips(vg);
        drawTakeNotes(vg);
        drawTransformFrame(vg);
        drawLivePlay(vg);
        drawLayerStrip(vg);
    }
    drawEmptyState(vg);

    double p = playhead();
    if (std::isfinite(p) && ctx.module() && ctx.module()->playing()) {
        float x = float(time().tickToX(p));
        if (x < 0)
            timeline::drawOffscreenArrow(vg, false, box.size.x, box.size.y / 2);
        else if (x > box.size.x)
            timeline::drawOffscreenArrow(vg, true, box.size.x, box.size.y / 2);
        else
            timeline::drawPlayhead(vg, x, box.size.y);
    }
    nvgRestore(vg);
}

void NoteGrid::drawHoverGuide(NVGcontext* vg) {
    if (!hovering || hasGesture())
        return;
    // A faint band along the hovered row and grid column, so pitch and time read at a glance (EDITOR_UX §8).
    float row = float(owner.pitch.rowHeight());
    int hoveredPitch = owner.pitch.yToPitch(hoverPos.y);
    float rowY = float(owner.pitch.pitchToY(hoveredPitch));
    Tick step = owner.snapStep() > 0 ? owner.snapStep() : kPpq / 4;
    Tick col = Tick(std::floor(time().xToTick(hoverPos.x) / double(step))) * step;
    float cx0 = float(time().tickToX(double(col))), cx1 = float(time().tickToX(double(col + step)));
    nvgBeginPath(vg);
    nvgRect(vg, 0, rowY, box.size.x, row);
    nvgRect(vg, cx0, 0, cx1 - cx0, rowY);
    nvgRect(vg, cx0, rowY + row, cx1 - cx0, box.size.y - rowY - row);
    nvgFillColor(vg, nvgTransRGBAf(theme::colors().text0, 0.045f));
    nvgFill(vg);
    // The Slice tool shows where a click would cut, across the whole grid.
    if (ctx.state().tool != Tool::Slice)
        return;
    Tick at = snapPosition(time().xToTick(hoverPos.x), owner.snapStep());
    float x = float(time().tickToX(double(at)));
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + 0.5f, 0);
    nvgLineTo(vg, x + 0.5f, box.size.y);
    nvgStrokeColor(vg, nvgTransRGBAf(theme::colors().accent, 0.6f));
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
}

void NoteGrid::drawNotes(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    TimeAxis& t = time();
    PitchAxis& pitch = owner.pitch;
    float row = float(pitch.rowHeight());
    const Track* track = owner.track();
    uint32_t base = track ? track->color : 0x9AA1AB;
    draw::TextStyle label{theme::fonts::uiStrong(), std::min(theme::metrics::kTextRuler, row - 2), c.onTile,
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    double right = t.right();
    IdSet selected = owner.selectedIds();
    bool flagOffKey = owner.highlightKey();
    Scale key = owner.scale();
    bool badges = ctx.state().noteBadges;
    std::optional<expr::Curve> curve = owner.shownCurve();
    // A sounding note glows for its whole length: a bright flash as it starts (150 ms), then a steady glow.
    TesseraModule* m = ctx.module();
    double playing = m && m->playing() ? playhead() : NAN;
    double glowTicks = m ? std::max(1.0, 0.15 * m->tempo() / 60.0 * double(kPpq)) : 1;

    // Notes are sorted by start, so stop at the first one past the right edge.
    for (const Note& n : owner.notes()) {
        if (double(n.start) > right)
            break;
        if (double(n.end()) < t.left())
            continue;
        float y = float(pitch.pitchToY(n.pitch));
        if (y + row < 0 || y > box.size.y)
            continue;
        float x0 = float(t.tickToX(double(n.start)));
        float x1 = float(t.tickToX(double(n.end())));
        Rect r(x0, y + 0.5f, std::max(2.f, x1 - x0 - 1), row - 1);
        bool isSelected = selected.count(n.id) > 0;

        // Brightness follows velocity (40-100%); selected notes are a little brighter, hovered ones too.
        NVGcolor full = theme::track(channelColor(base, n.channel));
        float amount = 0.4f + 0.6f * n.velocity;
        if (isSelected)
            amount = std::min(1.f, amount + 0.1f);
        NVGcolor fill = nvgLerpRGBA(c.bg0, full, amount);
        if (n.id == hovered && !hasGesture())
            fill = nvgLerpRGBA(fill, c.text0, 0.06f);
        if (n.muted)
            fill = nvgTransRGBAf(fill, 0.35f);
        float radius = std::min(theme::metrics::kRadiusItem, row / 3);
        float glow = 0;
        if (std::isfinite(playing) && double(n.start) <= playing && playing < double(n.end()) && !n.muted) {
            float attack = float(std::max(0.0, 1.0 - (playing - double(n.start)) / glowTicks));
            glow = kSustainGlow + (1 - kSustainGlow) * attack;
        }
        if (glow > 0) {
            draw::roundedRect(vg, Rect(r.pos.minus(Vec(2, 2)), r.size.plus(Vec(4, 4))), radius + 2,
                              nvgTransRGBAf(full, 0.45f * glow));
            fill = nvgLerpRGBA(fill, c.text0, 0.3f * glow);
        }
        draw::roundedRect(vg, r, radius, fill);
        if (n.muted && r.size.x > 4) {
            // Muted: hatched as well as faded, so it reads as "off" in any colour.
            nvgSave(vg);
            nvgIntersectScissor(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
            nvgBeginPath(vg);
            // Only the hatch lines on screen, counted (a zoomed-in note can be far wider than the view).
            float first = r.pos.x - r.size.y;
            int k0 = std::max(0, int(std::floor((-r.size.y - first) / 5)));
            int k1 = int(std::ceil((std::min(r.getRight(), box.size.x) - first) / 5));
            for (int k = k0; k < k1; k++) {
                float hx = first + 5.f * float(k);
                nvgMoveTo(vg, hx, r.getBottom());
                nvgLineTo(vg, hx + r.size.y, r.pos.y);
            }
            nvgStrokeColor(vg, nvgTransRGBAf(full, 0.5f));
            nvgStrokeWidth(vg, 1);
            nvgStroke(vg);
            nvgRestore(vg);
        }
        if (isSelected)
            draw::roundedRectStroke(vg, r, radius, c.select, 1.5f);
        else
            draw::roundedRectStroke(vg, r, radius, nvgLerpRGBA(fill, c.onTile, 0.4f), 1);

        float labelX = x0 + 4;
        if (flagOffKey && !key.inScale(n.pitch)) {
            // A small amber corner: this note is outside the key (EDITOR_UX §4.11).
            float m = std::min(kOffKeyMark, std::min(r.size.x, r.size.y));
            nvgBeginPath(vg);
            nvgMoveTo(vg, r.pos.x, r.pos.y);
            nvgLineTo(vg, r.pos.x + m, r.pos.y);
            nvgLineTo(vg, r.pos.x, r.pos.y + m);
            nvgClosePath(vg);
            nvgFillColor(vg, c.queue);
            nvgFill(vg);
            labelX += m / 2;
        }
        float labelRight = labelX;
        if (r.size.x >= kMinLabelWidth && row >= kMinLabelRow) {
            draw::text(vg, label, labelX, y + row / 2, pitchName(n.pitch));
            labelRight = labelX + draw::textWidth(vg, label, pitchName(n.pitch));
        }
        if (curve && *curve != expr::Curve::Pitch)
            drawCurveInside(vg, n, r, *curve);
        if (badges && row >= kMinBadgeRow)
            drawBadges(vg, n, r, labelRight);
        bool showVelocity = (n.id == hovered && !hasGesture()) || owner.velocityShown.count(n.id) > 0;
        if (showVelocity && r.size.x >= kMinVelocityWidth && row >= kMinLabelRow) {
            // Velocity on hover, and on every note a velocity drag changes, when zoomed in enough (EDITOR_UX §8).
            draw::TextStyle v{theme::fonts::mono(), std::min(theme::metrics::kTextRuler, row - 2), c.onTile,
                              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
            std::string text = std::to_string(int(std::lround(n.velocity * 100))) + "%";
            float w = draw::textWidth(vg, v, text) + 6;
            draw::roundedRect(vg, Rect(r.getRight() - w - 2, r.pos.y + 1, w, r.size.y - 2), 2,
                              nvgLerpRGBA(fill, c.text0, 0.5f));
            draw::text(vg, v, r.getRight() - 5, r.getCenter().y, text);
        } else if (badges && !(n.ops == NoteOperators()) && r.size.x >= 4) {
            // Too small for badges: a dot still says "this note has operators".
            nvgBeginPath(vg);
            nvgCircle(vg, r.getRight() - 2.5f, r.getCenter().y, std::min(1.5f, r.size.y / 3));
            nvgFillColor(vg, nvgTransRGBAf(theme::colors().onTile, 0.8f));
            nvgFill(vg);
        }
    }
}

void NoteGrid::drawBends(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    TimeAxis& t = time();
    PitchAxis& pitch = owner.pitch;
    const Track* track = owner.track();
    NVGcolor line = nvgLerpRGBA(theme::track(track ? track->color : 0x9AA1AB), c.text0, 0.55f);
    bool bendMode = owner.bendMode();
    IdSet selected = owner.selectedIds();
    double right = t.right();
    for (const Note& n : owner.notes()) {
        if (double(n.start) > right)
            break;
        if (n.pitchExpr.empty() || double(n.end()) < t.left())
            continue;
        float x0 = float(t.tickToX(double(n.start))), x1 = float(t.tickToX(double(n.end())));
        auto yAt = [&](float pos) {
            return float(pitch.bentPitchToY(n.pitch + double(expressionAt(n.pitchExpr, pos, 0))));
        };
        // The curve at the pitch it really plays, so a glide visibly lands on the row it heads for. Where it rests
        // on the note's own pitch the note body already says so, and a line there would cover the pitch label.
        float width = x1 - x0;
        float rest = float(pitch.bentPitchToY(n.pitch));
        float px = 0, py = rest;
        bool open = false;
        nvgBeginPath(vg);
        draw::sampleSpan(x0, x1, 0, box.size.x, 3, [&](float x, bool first) {
            float y = yAt(width > 0 ? (x - x0) / width : 0);
            if (!first && (py != rest || y != rest)) {
                if (!open)
                    nvgMoveTo(vg, px, py);
                nvgLineTo(vg, x, y);
                open = true;
            } else {
                open = false;
            }
            px = x;
            py = y;
        });
        nvgStrokeColor(vg, n.muted ? nvgTransRGBAf(line, 0.4f) : line);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
        if (!bendMode && !selected.count(n.id))
            continue;
        for (size_t i = 0; i + 1 < n.pitchExpr.size(); i++) {
            // A hollow handle in the middle of each glide: drag it to arch the glide.
            const ExprPoint& a = n.pitchExpr[i];
            const ExprPoint& b = n.pitchExpr[i + 1];
            if (a.value == b.value || (b.pos - a.pos) * width < kMinArchSpan)
                continue;
            float mid = (a.pos + b.pos) / 2;
            nvgBeginPath(vg);
            nvgCircle(vg, x0 + mid * width, yAt(mid), 2.5f);
            nvgFillColor(vg, c.bg0);
            nvgFill(vg);
            nvgStrokeColor(vg, line);
            nvgStrokeWidth(vg, 1.2f);
            nvgStroke(vg);
        }
        for (const ExprPoint& p : n.pitchExpr) {
            if (p.pos == 0 && p.value == 0)
                continue; // the resting start point sits on the pitch label; it is still there to grab
            bool picked = owner.pointPicked(n.id, expr::Curve::Pitch, p);
            nvgBeginPath(vg);
            nvgCircle(vg, x0 + p.pos * width, float(pitch.bentPitchToY(n.pitch + double(p.value))), picked ? 4 : 3);
            nvgFillColor(vg, picked ? c.select : line);
            nvgFill(vg);
            nvgStrokeColor(vg, c.bg0);
            nvgStrokeWidth(vg, 1);
            nvgStroke(vg);
        }
    }
}

void NoteGrid::drawStrips(NVGcontext* vg) {
    std::optional<expr::Curve> curve = owner.stripCurve();
    if (!curve)
        return;
    const theme::Palette& c = theme::colors();
    const Track* track = owner.track();
    NVGcolor full = theme::track(track ? track->color : 0x9AA1AB);
    NVGcolor line = nvgLerpRGBA(full, c.text0, 0.55f);
    float rest = expr::info(*curve).rest;
    const NoteList& notes = owner.notes();
    auto find = [&notes](NoteId id) -> const Note* {
        for (const Note& n : notes)
            if (n.id == id)
                return &n;
        return nullptr;
    };
    for (const CurveStrip& s : curveStrips(notes)) {
        if (s.r.getRight() < 0 || s.r.pos.x > box.size.x)
            continue;
        draw::roundedRect(vg, s.r, theme::metrics::kRadiusItem, nvgTransRGBAf(c.bg1, 0.95f));
        draw::roundedRectStroke(vg, s.r, theme::metrics::kRadiusItem, nvgTransRGBAf(full, 0.6f), 1);
        nvgBeginPath(vg);
        for (float v : {0.25f, 0.5f, 0.75f}) {
            float y = std::round(float(stripY(s, v))) + 0.5f;
            nvgMoveTo(vg, s.r.pos.x + 1, y);
            nvgLineTo(vg, s.r.getRight() - 1, y);
        }
        nvgStrokeColor(vg, nvgTransRGBAf(c.line1, 0.6f));
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
        if (s.r.size.x >= 60) {
            draw::TextStyle style{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2,
                                  NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
            draw::text(vg, style, s.r.pos.x + 4, s.r.pos.y + 2, expr::info(*curve).name);
        }
        nvgSave(vg);
        nvgIntersectScissor(vg, s.r.pos.x, s.r.pos.y, s.r.size.x, s.r.size.y);
        // The other notes of the strip faintly, the lead note (the highest) on top.
        std::vector<NoteId> order = s.notes;
        std::stable_partition(order.begin(), order.end(), [&s](NoteId id) { return id != s.lead; });
        for (NoteId id : order) {
            const Note* n = find(id);
            if (!n)
                continue;
            const std::vector<ExprPoint>& points = expr::points(*n, *curve);
            bool lead = id == s.lead;
            float x0 = float(time().tickToX(double(n->start))), x1 = float(time().tickToX(double(n->end())));
            float width = std::max(1.f, x1 - x0);
            nvgBeginPath(vg);
            draw::sampleSpan(x0, x1, s.r.pos.x, s.r.getRight(), 3, [&](float x, bool first) {
                float y = float(stripY(s, expressionAt(points, (x - x0) / width, rest)));
                if (first)
                    nvgMoveTo(vg, x, y);
                else
                    nvgLineTo(vg, x, y);
            });
            nvgStrokeColor(vg, lead ? line : nvgTransRGBAf(line, 0.45f));
            nvgStrokeWidth(vg, lead ? 1.5f : 1);
            nvgStroke(vg);
            for (size_t i = 0; i + 1 < points.size(); i++) {
                const ExprPoint& a = points[i];
                const ExprPoint& b = points[i + 1];
                if (a.value == b.value || (b.pos - a.pos) * width < kMinArchSpan)
                    continue;
                float mid = (a.pos + b.pos) / 2;
                nvgBeginPath(vg);
                nvgCircle(vg, x0 + mid * width, float(stripY(s, expressionAt(points, mid, rest))), 2.5f);
                nvgFillColor(vg, c.bg0);
                nvgFill(vg);
                nvgStrokeColor(vg, line);
                nvgStrokeWidth(vg, 1.2f);
                nvgStroke(vg);
            }
            for (const ExprPoint& p : points) {
                bool picked = owner.pointPicked(n->id, *curve, p);
                nvgBeginPath(vg);
                nvgCircle(vg, x0 + p.pos * width, float(stripY(s, p.value)), picked ? 4 : 3);
                nvgFillColor(vg, picked ? c.select : lead ? line : nvgTransRGBAf(line, 0.6f));
                nvgFill(vg);
                nvgStrokeColor(vg, c.bg0);
                nvgStrokeWidth(vg, 1);
                nvgStroke(vg);
            }
        }
        nvgRestore(vg);
    }
}

void NoteGrid::drawBadges(NVGcontext* vg, const Note& n, Rect r, float labelRight) {
    const theme::Palette& c = theme::colors();
    const NoteOperators& ops = n.ops;
    NVGcolor mark = nvgTransRGBAf(c.onTile, 0.7f);

    int count = operators::hitCount(n, kPpq);
    if (count > 1 && r.size.x / float(count) >= kMinTickGap) {
        nvgBeginPath(vg);
        for (int i = 1; i < count; i++) {
            operators::Hit h = operators::hit(n, i, count);
            float x = r.pos.x + float(h.start / double(n.length)) * r.size.x;
            nvgRect(vg, std::round(x), r.pos.y + 2, 1, r.size.y - 4);
        }
        nvgFillColor(vg, mark);
        nvgFill(vg);
    }

    int length = std::clamp<int>(ops.recurrenceLength, 1, 16);
    if (length > 1 && r.size.x >= length * kDotPitch + 4) {
        float y = r.getBottom() - 2.5f;
        for (int i = 0; i < length; i++) {
            nvgBeginPath(vg);
            nvgCircle(vg, r.pos.x + 3 + i * kDotPitch, y, 1.2f);
            bool on = (ops.recurrenceMask >> i) & 1;
            nvgFillColor(vg, on ? mark : nvgTransRGBAf(c.onTile, 0.25f));
            nvgFill(vg);
        }
    }

    static const char* const kOccurrence[] = {"", "1st", "not 1st", "prev", "not prev", "fill", "not fill"};
    std::string text;
    if (ops.chance < 1)
        text = std::to_string(int(std::lround(ops.chance * 100))) + "%";
    if (ops.occurrence != Occurrence::Always) {
        if (!text.empty())
            text += " ";
        text += kOccurrence[std::clamp(int(ops.occurrence), 0, 6)];
    }
    if (text.empty())
        return;
    draw::TextStyle style{theme::fonts::uiStrong(), std::min(theme::metrics::kTextRuler, r.size.y - 2), mark,
                          NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    float width = draw::textWidth(vg, style, text);
    if (r.getRight() - 3 - width > labelRight + 3)
        draw::text(vg, style, r.getRight() - 3, r.getCenter().y, text);
    else if (r.size.x >= 6) {
        // No room for the text: a dot in the corner still says "this note has conditions".
        nvgBeginPath(vg);
        nvgCircle(vg, r.getRight() - 3, r.pos.y + 3, 1.5f);
        nvgFillColor(vg, mark);
        nvgFill(vg);
    }
}

void NoteGrid::drawCurveInside(NVGcontext* vg, const Note& n, Rect r, expr::Curve curve) {
    const std::vector<ExprPoint>& points = expr::points(n, curve);
    if (points.empty() || r.size.x < 6)
        return;
    const ExpressionLane& lane = *owner.expression;
    float low = lane.low(), high = lane.high();
    float rest = expr::info(curve).rest;
    auto yOf = [&](float v) {
        float x = (std::clamp(v, low, high) - low) / (high - low);
        return r.getBottom() - 1.5f - x * (r.size.y - 3);
    };
    nvgBeginPath(vg);
    draw::sampleSpan(r.pos.x, r.getRight(), 0, box.size.x, 3, [&](float x, bool first) {
        float y = yOf(expressionAt(points, (x - r.pos.x) / r.size.x, rest));
        if (first)
            nvgMoveTo(vg, x, y);
        else
            nvgLineTo(vg, x, y);
    });
    nvgStrokeColor(vg, nvgTransRGBAf(theme::colors().onTile, 0.8f));
    nvgStrokeWidth(vg, 1.2f);
    nvgStroke(vg);
}

void NoteGrid::collectGhosts() {
    ghosts.clear();
    TesseraModule* m = ctx.module();
    const Track* current = owner.track();
    const Clip* editing = owner.clip();
    EditorState& state = ctx.state();
    if (!m || !current || !editing)
        return;
    const Song& song = m->song();
    double left = time().left(), right = time().right();
    auto push = [&](const Note& n, const Track& t, ClipId id, bool layer) {
        if (double(n.end()) >= left && double(n.start) <= right)
            ghosts.push_back({n, t.color, t.id, id, layer});
    };
    // A clip as the clips panel plays it: a looping clip repeats its loop, lined up as if launched together with
    // the clip being edited (both start at their loop start).
    ClipSpan editSpan = clipSpan(*editing);
    auto add = [&](const Track& t, ClipId id, bool layer = false) {
        const Clip* c = song.findClip(id);
        if (!c || id == owner.clipId())
            return;
        ClipSpan span = clipSpan(*c);
        if (layer || !span.looping || span.length() <= 0) {
            for (const Note& n : c->notes)
                push(n, t, id, layer);
            return;
        }
        Tick origin = editSpan.start - span.start;
        for (Tick pass = 0;; pass++) {
            Tick shift = origin + pass * span.length();
            if (double(span.start + shift) > right)
                break;
            if (double(span.end + shift) < left)
                continue;
            for (const Note& n : c->notes) {
                if (n.start < span.start || n.start >= span.end)
                    continue;
                Note g = n;
                g.start = n.start + shift;
                g.length = std::max<Tick>(1, std::min(n.end(), span.end) - n.start);
                push(g, t, id, false);
            }
        }
    };
    // Opened from a timeline clip: what the other tracks play on the timeline under it, in this clip's time.
    const ArrangerClip* instance = owner.source.instance();
    std::vector<TimedNote> played;
    auto addTimeline = [&](const Track& t, ArrangementId except) {
        double stretch = instance->stretch > 0 ? double(instance->stretch) : 1.0;
        Tick from = instance->position, to = instanceEnd(*instance);
        for (const ArrangerClip& a : t.arrangement) {
            const Clip* c = song.findClip(a.clip);
            if (!c || a.id == except || a.muted || a.position >= to || instanceEnd(a) <= from)
                continue;
            played.clear();
            renderInstance(*c, a, played, t.transpose);
            for (const TimedNote& tn : played) {
                if (tn.end <= from || tn.start >= to || !tn.source)
                    continue;
                Note g = *tn.source;
                g.start = instance->offset + Tick(std::llround(double(tn.start - from) / stretch));
                g.length = std::max<Tick>(1, Tick(std::llround(double(tn.end - tn.start) / stretch)));
                g.pitch = tn.pitch;
                push(g, t, a.clip, false);
            }
        }
    };
    bool layered = std::find(state.layered.begin(), state.layered.end(), owner.clipId()) != state.layered.end();
    if (layered)
        for (ClipId id : state.layered)
            if (const Track* t = song.findTrack(clipActions::trackOf(song, id)))
                add(*t, id, true);
    auto ghostTrack = [&](const Track& t) {
        if (instance)
            addTimeline(t, ArrangementId());
        else
            add(t, clipActions::shownClip(ctx, t));
    };
    switch (state.ghosts) {
    case GhostSource::Track:
        if (const Track* t = song.findTrack(state.ghostTrack); t && t->id != current->id)
            ghostTrack(*t);
        break;
    case GhostSource::AllTracks:
        // Drum notes are lanes, not pitches, so they would only add noise to a melody.
        for (const Track& t : song.tracks)
            if (t.id != current->id && t.type == TrackType::Instrument)
                ghostTrack(t);
        break;
    case GhostSource::OtherClips: {
        if (instance) {
            // The track's other timeline clips under this one (stacked clips).
            addTimeline(*current, instance->id);
            break;
        }
        std::vector<ClipId> seen;
        auto once = [&](std::optional<ClipId> id) {
            if (id && std::find(seen.begin(), seen.end(), *id) == seen.end()) {
                seen.push_back(*id);
                add(*current, *id);
            }
        };
        for (const ClipSlot& s : current->slots)
            once(s.clip);
        for (const ArrangerClip& a : current->arrangement)
            once(a.clip);
        break;
    }
    default:
        break;
    }
}

void NoteGrid::drawTimelineRepeats(NVGcontext* vg) {
    double end = owner.source.instanceEnd();
    const Clip* clip = owner.clip();
    const Track* track = owner.track();
    if (!std::isfinite(end) || !clip || !track)
        return;
    TimeAxis& t = time();
    if (clip->loopMode == LoopMode::Loop && clip->loopEnd > clip->loopStart) {
        Tick length = clip->loopEnd - clip->loopStart;
        float row = float(owner.pitch.rowHeight());
        float radius = std::min(theme::metrics::kRadiusItem, row / 3);
        NVGcolor color = nvgTransRGBAf(theme::track(track->color), 0.35f);
        // The repeats are the same notes: they can't be edited, so they are drawn like ghosts.
        for (int pass = 1;; pass++) {
            double shift = double(pass) * double(length);
            double from = double(clip->loopStart) + shift;
            if (from >= end || t.tickToX(from) > box.size.x)
                break;
            if (t.tickToX(from + double(length)) < 0)
                continue;
            for (const Note& n : owner.notes()) {
                if (n.start < clip->loopStart || n.start >= clip->loopEnd)
                    continue;
                double start = double(n.start) + shift;
                if (start >= end)
                    continue;
                double stop = std::min(double(std::min(n.end(), clip->loopEnd)) + shift, end);
                float x0 = float(t.tickToX(start)), x1 = float(t.tickToX(stop));
                float y = float(owner.pitch.pitchToY(n.pitch));
                if (x1 < 0 || x0 > box.size.x || y + row < 0 || y > box.size.y)
                    continue;
                draw::roundedRect(vg, Rect(x0, y + 0.5f, std::max(2.f, x1 - x0 - 1), row - 1), radius, color);
            }
        }
    }
    float x = float(t.tickToX(end));
    if (x >= -1 && x <= box.size.x + 1) {
        nvgBeginPath(vg);
        nvgRect(vg, x - 1, 0, 2, box.size.y);
        nvgFillColor(vg, theme::colors().queue);
        nvgFill(vg);
    }
}

void NoteGrid::drawGhosts(NVGcontext* vg) {
    if (ghosts.empty())
        return;
    TimeAxis& t = time();
    float row = float(owner.pitch.rowHeight());
    for (const Ghost& g : ghosts) {
        float y = float(owner.pitch.pitchToY(g.note.pitch));
        if (y + row < 0 || y > box.size.y)
            continue;
        float x0 = float(t.tickToX(double(g.note.start)));
        float x1 = float(t.tickToX(double(g.note.end())));
        Rect r(x0, y + 0.5f, std::max(2.f, x1 - x0 - 1), row - 1);
        float radius = std::min(theme::metrics::kRadiusItem, row / 3);
        if (g.layer) {
            // Another clip being edited together: solid but dimmed, so it reads as editable with one click.
            NVGcolor color = nvgLerpRGBA(theme::colors().bg0, theme::track(g.color), 0.45f);
            draw::roundedRect(vg, r, radius, color);
            continue;
        }
        NVGcolor color = nvgTransRGBAf(theme::track(g.color), 0.3f);
        draw::roundedRect(vg, r, radius, nvgTransRGBAf(color, 0.08f));
        draw::roundedRectStroke(vg, r, radius, color, 1);
    }
}

const NoteGrid::Ghost* NoteGrid::ghostAt(Vec pos) {
    float row = float(owner.pitch.rowHeight());
    for (const Ghost& g : ghosts) {
        float y = float(owner.pitch.pitchToY(g.note.pitch));
        float x0 = float(time().tickToX(double(g.note.start)));
        float x1 = float(time().tickToX(double(g.note.end())));
        if (pos.x >= x0 - 2 && pos.x <= x1 + 2 && pos.y >= y && pos.y <= y + row)
            return &g;
    }
    return nullptr;
}

void NoteGrid::switchTo(TrackId track, ClipId clip, NoteId note) {
    EditorState& state = ctx.state();
    state.selectedTrack = track;
    state.selectedTracks = {track};
    state.selectedClip = clip;
    state.selection = note.valid() ? NoteSelection{clip, {note}} : NoteSelection{};
}

void NoteGrid::drawLayerStrip(NVGcontext* vg) {
    layerChips.clear();
    layerClose = Rect();
    TesseraModule* m = ctx.module();
    const std::vector<ClipId>& layered = ctx.state().layered;
    if (!m || std::find(layered.begin(), layered.end(), owner.clipId()) == layered.end())
        return;
    const theme::Palette& c = theme::colors();
    const Song& song = m->song();
    draw::TextStyle style{theme::fonts::ui(), theme::metrics::kTextRuler, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    float x = 8, y = box.size.y - 26, h = 18;
    draw::TextStyle hint = style;
    hint.color = c.text2;
    for (ClipId id : layered) {
        const Clip* clip = song.findClip(id);
        const Track* t = song.findTrack(clipActions::trackOf(song, id));
        if (!clip || !t)
            continue;
        std::string name = t->name + " · " + (clip->name.empty() ? "Clip" : clip->name);
        float w = draw::textWidth(vg, style, name) + 22;
        Rect r(x, y, w, h);
        bool editing = id == owner.clipId();
        draw::roundedRect(vg, r, 9, nvgTransRGBAf(c.bg1, 0.95f));
        draw::roundedRectStroke(vg, r, 9, editing ? c.accent : c.line2, editing ? 1.5f : 1);
        nvgBeginPath(vg);
        nvgCircle(vg, x + 9, y + h / 2, 3.5f);
        nvgFillColor(vg, theme::track(t->color));
        nvgFill(vg);
        style.color = editing ? c.text0 : c.text1;
        draw::text(vg, style, x + 16, y + h / 2, name);
        layerChips.push_back({r, id});
        x += w + 4;
    }
    layerClose = Rect(x, y, h, h);
    draw::roundedRect(vg, layerClose, 9, nvgTransRGBAf(c.bg1, 0.95f));
    hint.align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE;
    draw::text(vg, hint, layerClose.getCenter().x, layerClose.getCenter().y, "×");
    const char* help = "Click a dimmed note or a clip to edit it";
    float helpWidth = draw::textWidth(vg, hint, help) + 16;
    Rect helpRect(x + h + 4, y, helpWidth, h);
    draw::roundedRect(vg, helpRect, 9, nvgTransRGBAf(c.bg1, 0.85f));
    draw::text(vg, hint, helpRect.getCenter().x, helpRect.getCenter().y, help);
}

void NoteGrid::drawLivePlay(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    EditorState& state = ctx.state();
    std::string text;
    if (state.stepInput) {
        float x = float(time().tickToX(double(state.stepCursor)));
        nvgBeginPath(vg);
        nvgRect(vg, x - 1, 0, 2, box.size.y);
        nvgFillColor(vg, c.record);
        nvgFill(vg);
        Tick step = owner.snapStep() > 0 ? owner.snapStep() : kPpq / 4;
        float x1 = float(time().tickToX(double(state.stepCursor + step)));
        nvgBeginPath(vg);
        nvgRect(vg, x, 0, std::max(1.f, x1 - x), box.size.y);
        nvgFillColor(vg, nvgTransRGBAf(c.record, 0.08f));
        nvgFill(vg);
        text = "Step input: play notes · → rest · Shift+→ tie · Backspace removes a step · Ctrl+R or Esc to stop";
    }
    if (state.keyboardPlay) {
        char line[160];
        snprintf(line, sizeof(line),
                 "Keyboard play: A–L white keys, W–P black · Z/X octave (%s) · C/V velocity (%d) · Ctrl+K to stop",
                 pitchName(60 + 12 * state.keyboardOctave).c_str(), int(std::lround(state.keyboardVelocity * 127)));
        text = text.empty() ? line : std::string(line) + "\n" + text;
    }
    if (text.empty())
        return;
    draw::TextStyle style{theme::fonts::ui(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    size_t split = text.find('\n');
    std::vector<std::string> lines{text.substr(0, split)};
    if (split != std::string::npos)
        lines.push_back(text.substr(split + 1));
    float y = 8;
    for (const std::string& line : lines) {
        std::string shown = draw::ellipsize(vg, style, line, box.size.x - 24);
        float w = draw::textWidth(vg, style, shown) + 16;
        Rect pill(box.size.x / 2 - w / 2, y, w, 20);
        draw::roundedRect(vg, pill, 10, nvgTransRGBAf(c.bg1, 0.92f));
        draw::roundedRectStroke(vg, pill, 10, state.stepInput && &line == &lines.back() ? c.record : c.accent, 1);
        draw::text(vg, style, pill.getCenter().x, pill.getCenter().y, shown);
        y += 24;
    }
}

void NoteGrid::drawTakeNotes(NVGcontext* vg) {
    TesseraModule* m = ctx.module();
    const Clip* clip = owner.clip();
    if (!m || !clip)
        return;
    const theme::Palette& c = theme::colors();
    ClipSpan span = clipSpan(*clip);
    double playTick = m->playTicks();
    float row = float(owner.pitch.rowHeight());
    auto draw = [&](Tick start, Tick length, int pitch) {
        // Takes count clip time from the loop start without wrapping; show them where they will land.
        if (span.looping && span.length() > 0)
            start = span.start + ((start - span.start) % span.length() + span.length()) % span.length();
        float x0 = float(time().tickToX(double(start)));
        float x1 = float(time().tickToX(double(start + length)));
        Rect r(x0, float(owner.pitch.pitchToY(pitch)) + 0.5f, std::max(2.f, x1 - x0 - 1), row - 1);
        draw::roundedRect(vg, r, std::min(theme::metrics::kRadiusItem, row / 3), nvgTransRGBAf(c.record, 0.55f));
        draw::roundedRectStroke(vg, r, std::min(theme::metrics::kRadiusItem, row / 3), c.record, 1);
    };
    for (const Recorder::Session& take : m->takes().sessions()) {
        if (take.target != Recorder::Target::Overdub || take.clip != clip->id)
            continue;
        for (const TakeNote& n : take.done)
            draw(n.start, n.length, n.pitch);
        for (const auto& [pitch, open] : take.open)
            draw(open.start, Recorder::openLength(open, playTick), pitch);
    }
}

void NoteGrid::drawTransformFrame(NVGcontext* vg) {
    Rect f = transformFrame();
    if (f.size.x <= 0)
        return;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, f.pos.x + 0.5f, f.pos.y + 0.5f, f.size.x, f.size.y);
    nvgStrokeColor(vg, nvgTransRGBAf(c.select, 0.6f));
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
    float l = f.pos.x, r = f.pos.x + f.size.x, t = f.pos.y, b = f.pos.y + f.size.y;
    float cx = (l + r) / 2, cy = (t + b) / 2;
    for (Vec p : {Vec(l, t), Vec(r, t), Vec(l, b), Vec(r, b), Vec(l, cy), Vec(r, cy), Vec(cx, t), Vec(cx, b)}) {
        nvgBeginPath(vg);
        nvgRect(vg, p.x - kHandle / 2, p.y - kHandle / 2, kHandle, kHandle);
        nvgFillColor(vg, c.bg0);
        nvgFill(vg);
        nvgStrokeColor(vg, c.select);
        nvgStroke(vg);
    }
}

void NoteGrid::drawEmptyState(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    const char* message = nullptr;
    const Clip* clip = owner.clip();
    if (!owner.track())
        message = "Select a track to see its clip.";
    else if (!clip)
        message = "Double-click to create a clip.";
    else if (clip->notes.empty() && !hasGesture() && ghosts.empty()) // the hint would cover the ghost notes
        message = "Draw with D, or press Ctrl/Cmd+R for step input.";
    if (!message)
        return;
    draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, hint, box.size.x / 2, box.size.y / 2, message);
}

} // namespace tessera
