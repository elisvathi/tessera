#include "ArrangerLanes.hpp"

#include "../../model/LauncherCommands.hpp"
#include "../../model/RangeEdits.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ArrangeActions.hpp"
#include "../editing/ClipActions.hpp"
#include "../editing/ClipGestures.hpp"
#include "../editing/TrackActions.hpp"
#include "../interaction/ClickOrDrag.hpp"
#include "../interaction/Snap.hpp"
#include "../widgets/CachedLayer.hpp"
#include "../widgets/Draw.hpp"
#include "Arranger.hpp"
#include "TimelineDraw.hpp"

#include <cmath>
#include <map>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kOffscreenArrowZone = 18;
constexpr float kMagnetPx = 8;
constexpr float kHeaderStrip = 14;
constexpr float kMinHeightForStrip = 36; // shorter rows get a thinner strip, so the notes still show
constexpr float kBodyAlpha = 0.28f;
constexpr float kMutedAlpha = 0.35f;
constexpr float kOverriddenAlpha = 0.45f;
constexpr float kSeamNotch = 4;

float headerHeight(float rowHeight) {
    return rowHeight >= kMinHeightForStrip ? kHeaderStrip : std::max(10.f, std::round(rowHeight * 0.4f));
}

/// Two linked rings: this clip plays in more than one place.
void drawLinkBadge(NVGcontext* vg, float x, float cy, NVGcolor color) {
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 1.2f);
    nvgBeginPath(vg);
    nvgCircle(vg, x + 2.5f, cy, 2.5f);
    nvgCircle(vg, x + 6.5f, cy, 2.5f);
    nvgStroke(vg);
}

} // namespace

ArrangerLanes::ArrangerLanes(EditorContext& ctx, ArrangerView& owner)
    : TimelineView(ctx, owner.viewport), owner(owner) {
    grid = new CachedLayer([this](NVGcontext* vg, Vec size) { paintGrid(vg, size); });
    addChild(grid);
    clips = new CachedLayer([this](NVGcontext* vg, Vec size) { paintClips(vg, size); });
    addChild(clips);
}

// ---- Vertical axis ------------------------------------------------------------------------------------------

double ArrangerLanes::verticalScroll() { return owner.tracks.scroll(); }

void ArrangerLanes::setVerticalScroll(double px) {
    owner.tracks.scrollTo(px);
    ctx.view().trackScroll = owner.tracks.scroll();
}

void ArrangerLanes::zoomVertical(double y, double factor) {
    owner.tracks.zoomAt(y, factor);
    owner.writeHeights();
}

void ArrangerLanes::showVerticalRange(double y0, double y1) {
    if (y1 - y0 < 1)
        return;
    owner.tracks.zoomAt(y0, box.size.y / (y1 - y0));
    owner.tracks.scrollByPx(y0);
    owner.writeHeights();
    ctx.view().trackScroll = owner.tracks.scroll();
}

void ArrangerLanes::zoomToFit() {
    TimeAxis fit = time();
    auto [start, end] = arrange::span(shown(), selection());
    if (end > start)
        fit.showRange(double(start), double(end));
    else
        fit.showRange(0, double(owner.source.fitEnd()));
    viewport.jump(fit.state());
    if (selection().empty()) {
        owner.tracks.fitAll();
        owner.writeHeights();
        ctx.view().trackScroll = owner.tracks.scroll();
    }
}

double ArrangerLanes::playhead() { return owner.source.playhead(); }

// ---- Rows ---------------------------------------------------------------------------------------------------

int ArrangerLanes::rowAt(float y) {
    int row = owner.tracks.trackAt(y);
    return row >= 0 && row < int(owner.rows.size()) ? row : -1;
}

TrackId ArrangerLanes::trackOfRow(int row) {
    if (row < 0 || row >= int(owner.rows.size()) || owner.rows[size_t(row)].kind != TrackRow::Kind::Track)
        return TrackId();
    return owner.rows[size_t(row)].track;
}

std::vector<TrackId> ArrangerLanes::trackOrder() {
    std::vector<TrackId> order;
    for (const TrackRow& r : owner.rows)
        if (r.kind == TrackRow::Kind::Track)
            order.push_back(r.track);
    return order;
}

int ArrangerLanes::orderIndexAt(float y) {
    int best = -1, index = -1;
    float bestDistance = INFINITY;
    for (size_t row = 0; row < owner.rows.size() && row < owner.tracks.heights().size(); row++) {
        if (owner.rows[row].kind != TrackRow::Kind::Track)
            continue;
        index++;
        float top = rowTop(int(row)), bottom = top + rowHeight(int(row));
        float d = y < top ? top - y : y > bottom ? y - bottom : 0;
        if (d < bestDistance) {
            bestDistance = d;
            best = index;
        }
    }
    return best;
}

float ArrangerLanes::rowTop(int row) { return float(owner.tracks.trackTop(row)); }
float ArrangerLanes::rowHeight(int row) { return float(owner.tracks.trackHeight(row)); }

int ArrangerLanes::rowOf(TrackId track) {
    for (size_t i = 0; i < owner.rows.size(); i++)
        if (owner.rows[i].kind == TrackRow::Kind::Track && owner.rows[i].track == track)
            return int(i);
    return -1;
}

const arrange::Stacking& ArrangerLanes::stackingOf(TrackId track) {
    TesseraModule* m = ctx.module();
    uint64_t key = previewGeneration * 0x9E3779B97F4A7C15ull + (m ? m->songRevision() : 0) + (preview ? 1 : 0);
    if (key != stacksKey) {
        stacks.clear();
        stacksKey = key;
    }
    auto it = stacks.find(track);
    if (it == stacks.end())
        it = stacks.emplace(track, arrange::stackOf(shown(), track)).first;
    return it->second;
}

std::pair<float, float> ArrangerLanes::instanceSpan(int row, const arrange::Placed& p) {
    float top = rowTop(row), h = rowHeight(row);
    const arrange::Stacking& s = stackingOf(p.track);
    if (s.rows <= 1)
        return {top, h};
    auto it = s.row.find(p.clip.id);
    int sub = it == s.row.end() ? 0 : it->second;
    float each = h / float(s.rows);
    return {top + each * float(sub), each};
}

ArrangerLanes::Hit ArrangerLanes::hitTest(Vec pos) {
    Hit hit;
    hit.row = rowAt(pos.y);
    TrackId track = trackOfRow(hit.row);
    if (!track.valid())
        return hit;
    float edge = theme::metrics::kEdgeHit;
    const arrange::IdSet& sel = selection();
    // Selected clips win where clips overlap (EDITOR_UX §9 hit priority). Clips the mouse is inside come before
    // edge zones reaching out of a neighbour, so where two clips touch each side grabs its own clip's edge.
    for (int pass = 0; pass < 4; pass++) {
        bool reachOut = pass >= 2;
        bool selectedPass = pass % 2 == 0;
        for (const arrange::Placed& p : shown()) {
            if (p.track != track || (sel.count(p.clip.id) > 0) != selectedPass)
                continue;
            auto [top, h] = instanceSpan(hit.row, p);
            if (pos.y < top || pos.y >= top + h)
                continue; // another sub-row of stacked clips
            float x0 = float(time().tickToX(double(p.clip.position)));
            float x1 = float(time().tickToX(double(instanceEnd(p.clip))));
            // Edge zones reach a little outside, so even a very narrow clip can be resized.
            float inner = std::min(edge, (x1 - x0) / 3);
            float reach = reachOut ? edge / 2 : 0;
            if (pos.x < x0 - reach || pos.x > x1 + reach)
                continue;
            hit.id = p.clip.id;
            if (pos.x >= x1 - inner)
                hit.part = Part::RightEdge;
            else if (pos.x <= x0 + inner)
                hit.part = Part::LeftEdge;
            else if (h >= kMinHeightForStrip && pos.y < top + 1 + kHeaderStrip)
                hit.part = Part::Header;
            else
                hit.part = Part::Body;
            return hit;
        }
    }
    return hit;
}

// ---- Editing ------------------------------------------------------------------------------------------------

arrange::Layout ArrangerLanes::settle(const arrange::Layout& layout, const arrange::IdSet& changed,
                                      const arrange::IdSource& newId) {
    TesseraModule* m = ctx.module();
    if (!m || ctx.state().stackClips)
        return layout;
    return arrange::resolveOverlaps(layout, changed, m->song().clips, newId);
}

const arrange::Layout& ArrangerLanes::committed() {
    TesseraModule* m = ctx.module();
    if (m && m->songRevision() != committedRevision) {
        committedRevision = m->songRevision();
        committedCache = arrange::layoutOf(m->song());
    }
    return committedCache;
}

void ArrangerLanes::setPreview(arrange::Layout layout) {
    preview = std::move(layout);
    previewGeneration++;
}

void ArrangerLanes::clearPreview() {
    if (preview) {
        preview.reset();
        previewGeneration++;
    }
}

void ArrangerLanes::commit(const std::string& name, const arrange::Layout& before, const arrange::Layout& after,
                           const arrange::IdSet& selectAfter, std::vector<Clip> newClips) {
    clearPreview();
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    auto compound = std::make_unique<CompoundCommand>(name);
    CommandPtr edit = arrange::replaceArrangement(m->song(), name, before, after);
    if (!edit)
        return;
    for (Clip& c : newClips)
        compound->add(std::make_unique<AddClip>(std::move(c)));
    compound->add(std::move(edit));
    if (ctx.state().automationFollowsClips)
        compound->add(rangeEdits::automationFollowing(m->song(), before, after));
    m->perform(std::move(compound));
    selection() = selectAfter;
    owner.target = ArrangerView::Target::Clips;
}

ArrangementId ArrangerLanes::newInstanceId() { return ctx.module()->newId<ArrangementId>(); }
ClipId ArrangerLanes::newClipId() { return ctx.module()->newId<ClipId>(); }

Tick ArrangerLanes::snapStep() {
    return tessera::snapStep(ctx.state().snap, time().pxPerTick(), owner.source.timeSig());
}

namespace {

/// Every place a dragged edge sticks to: other clips' edges, markers, the loop and the playhead.
std::vector<Tick> magnets(ArrangerLanes& lanes, const arrange::IdSet& exclude) {
    std::vector<Tick> out;
    for (const arrange::Placed& p : lanes.committed()) {
        if (exclude.count(p.clip.id))
            continue;
        out.push_back(p.clip.position);
        out.push_back(instanceEnd(p.clip));
    }
    for (const Marker& mk : lanes.owner.source.markers())
        out.push_back(mk.position);
    out.push_back(lanes.owner.source.loopStart());
    out.push_back(lanes.owner.source.loopEnd());
    double p = lanes.playhead();
    if (std::isfinite(p))
        out.push_back(Tick(std::llround(p)));
    return out;
}

/// The magnet nearest `tick` within `tolerance`, if any.
std::optional<Tick> nearestMagnet(double tick, const std::vector<Tick>& targets, double tolerance) {
    std::optional<Tick> best;
    double bestDistance = tolerance;
    for (Tick t : targets) {
        double d = std::fabs(double(t) - tick);
        if (d <= bestDistance) {
            bestDistance = d;
            best = t;
        }
    }
    return best;
}

} // namespace

Tick ArrangerLanes::snapTick(double tick, bool free, const arrange::IdSet& exclude) {
    if (free)
        return Tick(std::llround(tick));
    return snapToEvents(tick, magnets(*this, exclude), kMagnetPx / time().pxPerTick(), snapStep());
}

Tick ArrangerLanes::snapMove(Tick anchor, double delta, Tick length, bool free, const arrange::IdSet& exclude) {
    if (free)
        return anchor + Tick(std::llround(delta));
    double start = double(anchor) + delta;
    double tolerance = kMagnetPx / time().pxPerTick();
    std::vector<Tick> targets = magnets(*this, exclude);
    std::optional<Tick> atStart = nearestMagnet(start, targets, tolerance);
    std::optional<Tick> atEnd = nearestMagnet(start + double(length), targets, tolerance);
    if (atStart && (!atEnd || std::fabs(double(*atStart) - start) <= std::fabs(double(*atEnd) - start - length)))
        return *atStart;
    if (atEnd)
        return *atEnd - length;
    return anchor + snapMoveDelta(anchor, delta, snapStep());
}

arrange::IdSet& ArrangerLanes::selection() { return ctx.state().clipSelection; }

void ArrangerLanes::clickInstance(ArrangementId id, Modifiers mods) {
    arrange::IdSet& sel = selection();
    if (mods.ctrl) {
        if (!sel.erase(id))
            sel.insert(id);
    } else if (mods.shift) {
        sel.insert(id);
    } else {
        sel = {id};
    }
    owner.target = ArrangerView::Target::Clips;
    if (const arrange::Placed* p = arrange::find(committed(), id); p && sel.count(id))
        clipActions::select(ctx, p->track, p->clip.clip);
}

double ArrangerLanes::mouseTick() {
    Vec p = localMouse();
    if (!owner.visible || p.x < 0 || p.y < 0 || p.x > box.size.x || p.y > box.size.y)
        return NAN;
    return time().xToTick(p.x);
}

bool ArrangerLanes::clipAction(Action action) {
    TesseraModule* m = ctx.module();
    if (!m)
        return false;
    // Forget instances that undo or other edits removed.
    arrange::IdSet& sel = selection();
    for (auto it = sel.begin(); it != sel.end();)
        it = arrange::find(committed(), *it) ? std::next(it) : sel.erase(it);
    bool any = !sel.empty();
    Tick step = std::max<Tick>(snapStep(), 1);
    Tick bar = ticksPerBar(owner.source.timeSig());
    switch (action) {
    case Action::LoopToSelection: {
        // The song's loop covers the selected clips, and loop turns on.
        if (!any)
            return false;
        Tick from = std::numeric_limits<Tick>::max(), to = 0;
        for (const arrange::Placed& p : arrange::pick(committed(), sel)) {
            from = std::min(from, p.clip.position);
            to = std::max(to, instanceEnd(p.clip));
        }
        if (to <= from)
            return false;
        owner.source.setLoop(from, to);
        if (!owner.source.loopOn())
            owner.source.toggleLoop();
        return true;
    }
    case Action::Paste:
        arrangeActions::paste(*this, false);
        return true;
    case Action::PasteAtMouse:
        arrangeActions::paste(*this, true);
        return true;
    case Action::SelectAll:
        arrangeActions::selectAll(*this);
        return true;
    case Action::Split: {
        double at = mouseTick();
        arrangeActions::split(*this, snapTick(std::isfinite(at) ? at : m->playheadTicks(), false, {}));
        return true;
    }
    default:
        break;
    }
    if (!any)
        return false;
    switch (action) {
    case Action::Delete:
        if (ctx.state().ripple)
            rippleDeleteSelected();
        else
            arrangeActions::deleteSelected(*this);
        return true;
    case Action::Duplicate:
        arrangeActions::duplicateSelected(*this);
        return true;
    case Action::Glue:
        arrangeActions::glueSelected(*this);
        return true;
    case Action::MakeUnique:
        arrangeActions::makeUniqueSelected(*this);
        return true;
    case Action::Copy:
        arrangeActions::copy(*this);
        return true;
    case Action::Cut:
        arrangeActions::cut(*this);
        return true;
    case Action::SelectNone:
        arrangeActions::selectNone(*this);
        return true;
    case Action::SelectInvert:
        arrangeActions::selectInvert(*this);
        return true;
    case Action::NudgeLeft:
        arrangeActions::nudge(*this, -step);
        return true;
    case Action::NudgeRight:
        arrangeActions::nudge(*this, step);
        return true;
    case Action::NudgeFineLeft:
        arrangeActions::nudge(*this, -std::max<Tick>(1, step / 4));
        return true;
    case Action::NudgeFineRight:
        arrangeActions::nudge(*this, std::max<Tick>(1, step / 4));
        return true;
    case Action::NudgeBarLeft:
        arrangeActions::nudge(*this, -bar);
        return true;
    case Action::NudgeBarRight:
        arrangeActions::nudge(*this, bar);
        return true;
    case Action::LengthShorter:
        arrangeActions::changeLength(*this, -step);
        return true;
    case Action::LengthLonger:
        arrangeActions::changeLength(*this, step);
        return true;
    case Action::TransposeUp:
        arrangeActions::transpose(*this, 1);
        return true;
    case Action::TransposeDown:
        arrangeActions::transpose(*this, -1);
        return true;
    case Action::OctaveUp:
        arrangeActions::transpose(*this, 12);
        return true;
    case Action::OctaveDown:
        arrangeActions::transpose(*this, -12);
        return true;
    default:
        return false;
    }
}

// ---- Mouse --------------------------------------------------------------------------------------------------

std::unique_ptr<DragGesture> ArrangerLanes::startGesture(Vec pos, int button, Modifiers mods) {
    if (auto nav = navigationGesture(pos, button, mods))
        return nav;
    if (button != GLFW_MOUSE_BUTTON_LEFT || !ctx.module())
        return nullptr;
    int row = rowAt(pos.y);
    std::optional<LaneFrame> frame = laneFrame(row);
    if (frame && !ctx.state().rangeTool) {
        owner.target = ArrangerView::Target::Automation;
        return automation.startGesture(*frame, pos, button, mods);
    }
    Hit hit = hitTest(pos);
    // Time ranges: with the Range tool anywhere, or Alt+drag where there is no clip (EDITOR_UX §6.4).
    if (ctx.state().rangeTool || (mods.alt && !mods.ctrl && hit.part == Part::None && row >= 0)) {
        owner.target = ArrangerView::Target::Range;
        return rangeGesture(pos, mods);
    }
    if (row >= 0 && owner.rows[size_t(row)].isLane())
        return nullptr;
    Tool tool = ctx.state().tool;
    // Ctrl/Cmd+drag (with or without Alt) from empty space selects with any tool. From a clip, Ctrl/Cmd+drag makes
    // a linked copy and Ctrl/Cmd+Alt+drag an independent one (DECISIONS §41).
    if (mods.ctrl && hit.part == Part::None)
        return clipGestures::rubberBand(*this, pos, mods);
    if (tool == Tool::Erase)
        return clipGestures::erase(*this, pos);
    if (tool == Tool::Mute)
        return clipGestures::mute(*this, pos);
    if (tool == Tool::Slice)
        return nullptr; // a click splits
    if (hit.part == Part::None) {
        if (tool == Tool::Draw && trackOfRow(hit.row).valid())
            return clipGestures::draw(*this, pos);
        return clipGestures::rubberBand(*this, pos, mods);
    }

    // A press selects at once, so a drag moves what was grabbed; a click in place adjusts the selection.
    ArrangementId id = hit.id;
    bool wasSelected = selection().count(id) > 0;
    arrange::IdSet previous = selection();
    // Ctrl/Cmd+drag on an unselected clip copies just that clip; Ctrl/Cmd+click still toggles it.
    bool ctrlOnNew = !wasSelected && mods.ctrl;
    if (ctrlOnNew)
        clickInstance(id, Modifiers());
    else if (!wasSelected)
        clickInstance(id, mods);
    owner.target = ArrangerView::Target::Clips;
    auto onClick = [this, id, wasSelected, ctrlOnNew, previous, mods] {
        if (ctrlOnNew) {
            selection() = previous;
            clickInstance(id, mods);
        } else if (wasSelected) {
            clickInstance(id, mods);
        }
    };
    std::unique_ptr<DragGesture> inner;
    switch (hit.part) {
    case Part::RightEdge:
        inner = clipGestures::resizeEnd(*this, pos, id, mods.alt);
        break;
    case Part::LeftEdge:
        inner = clipGestures::trimStart(*this, pos, id);
        break;
    case Part::Header:
        inner = clipGestures::header(*this, pos, id);
        break;
    default:
        inner = mods.alt && !mods.ctrl ? clipGestures::slip(*this, pos, id)
                                       : clipGestures::move(*this, pos, id, mods.ctrl && mods.alt);
        break;
    }
    return std::make_unique<ClickOrDrag>(std::move(inner), pos, onClick);
}

void ArrangerLanes::click(Vec pos, int button, Modifiers mods) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    int row = rowAt(pos.y);
    if (std::optional<LaneFrame> frame = laneFrame(row)) {
        owner.target = ArrangerView::Target::Automation;
        automation.click(*frame, pos, button, mods);
        return;
    }
    if (row >= 0 && owner.rows[size_t(row)].kind == TrackRow::Kind::AddLane) {
        if (button == GLFW_MOUSE_BUTTON_LEFT)
            trackActions::addAutomationLane(ctx, owner.rows[size_t(row)].track);
        return;
    }
    Hit hit = hitTest(pos);
    Tick at = snapTick(time().xToTick(pos.x), mods.alt, {});
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        rack::ui::Menu* menu = rack::createMenu();
        if (hit.id.valid()) {
            if (!selection().count(hit.id))
                clickInstance(hit.id, Modifiers());
            arrangeActions::appendClipMenu(menu, *this, hit.id, at);
        } else if (TrackId track = trackOfRow(hit.row); track.valid()) {
            arrangeActions::appendLaneMenu(menu, *this, track, at);
        } else {
            // Below the last track: offer to add one, as in the headers.
            EditorContext* c = &ctx;
            bool full = !trackActions::canAdd(ctx.module()->song(), 1);
            menu->addChild(rack::createMenuLabel(full ? trackActions::kTracksFull : "Add"));
            menu->addChild(rack::createMenuItem(
                "Instrument track", shortcutLabel(Action::AddInstrumentTrack),
                [c] { trackActions::add(*c, TrackType::Instrument); }, full));
            menu->addChild(rack::createMenuItem(
                "Drum track", shortcutLabel(Action::AddDrumTrack), [c] { trackActions::add(*c, TrackType::Drum); },
                full));
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    if (ctx.state().tool == Tool::Slice && hit.id.valid()) {
        selection() = {hit.id};
        arrangeActions::split(*this, at);
        return;
    }
    // The off-screen playhead arrow jumps back to the playhead.
    double p = playhead();
    if (std::isfinite(p)) {
        float x = float(time().tickToX(p));
        bool left = x < 0 && pos.x < kOffscreenArrowZone;
        bool right = x > box.size.x && pos.x > box.size.x - kOffscreenArrowZone;
        if ((left || right) && std::fabs(pos.y - box.size.y / 2) < kOffscreenArrowZone) {
            time().scrollTo(p - time().visibleTicks() / 3);
            return;
        }
    }
    if (TrackId track = trackOfRow(hit.row); track.valid())
        trackActions::click(ctx, track, mods.shift, mods.ctrl);
}

void ArrangerLanes::doubleClick(Vec pos, Modifiers mods) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    int row = rowAt(pos.y);
    if (std::optional<LaneFrame> frame = laneFrame(row)) {
        automation.doubleClick(*frame, pos, mods);
        return;
    }
    if (row >= 0 && owner.rows[size_t(row)].isLane())
        return;
    Hit hit = hitTest(pos);
    if (hit.id.valid()) {
        arrangeActions::openInEditor(*this, hit.id);
        return;
    }
    TrackId track = trackOfRow(hit.row);
    if (!track.valid())
        return;
    // A one-bar clip in the bar that was double-clicked.
    Tick bar = ticksPerBar(owner.source.timeSig());
    Tick start = Tick(std::floor(std::max(0.0, time().xToTick(pos.x)) / double(bar))) * bar;
    ClipId clip = newClipId();
    ArrangementId id = newInstanceId();
    m->perform(arrange::drawClip(
        m->song(), track, start, bar, clip, id, [this] { return newInstanceId(); }, ctx.state().stackClips));
    selection() = {id};
    owner.target = ArrangerView::Target::Clips;
    clipActions::select(ctx, track, clip);
}

CursorShape ArrangerLanes::hoverCursor(Vec pos, Modifiers mods) {
    CursorShape nav = TimelineView::hoverCursor(pos, mods);
    hover = hitTest(pos);
    if (nav != CursorShape::Arrow)
        return nav;
    int row = rowAt(pos.y);
    if (std::optional<LaneFrame> frame = laneFrame(row))
        return automation.hover(*frame, pos, mods);
    automation.leave();
    if (row >= 0 && owner.rows[size_t(row)].isLane())
        return owner.rows[size_t(row)].kind == TrackRow::Kind::AddLane ? CursorShape::Hand : CursorShape::Arrow;
    switch (ctx.state().tool) {
    case Tool::Erase:
        return CursorShape::NotAllowed;
    case Tool::Slice:
        return hover.id.valid() ? CursorShape::IBeam : CursorShape::Arrow;
    case Tool::Draw:
        if (!hover.id.valid())
            return trackOfRow(hover.row).valid() ? CursorShape::Crosshair : CursorShape::Arrow;
        break;
    default:
        break;
    }
    switch (hover.part) {
    case Part::LeftEdge:
    case Part::RightEdge:
        return CursorShape::ResizeH;
    case Part::Header:
        return mods.alt ? CursorShape::Move : CursorShape::ResizeV;
    case Part::Body:
        return CursorShape::Move;
    default:
        return CursorShape::Arrow;
    }
}

std::optional<LaneFrame> ArrangerLanes::laneFrame(int row) {
    TesseraModule* m = ctx.module();
    if (!m || row < 0 || row >= int(owner.rows.size()) || owner.rows[size_t(row)].kind != TrackRow::Kind::Lane)
        return std::nullopt;
    const TrackRow& r = owner.rows[size_t(row)];
    const Track* track = m->song().findTrack(r.track);
    const AutomationEnvelope* lane = findLane(m->song(), LaneOwner::ofTrack(r.track), r.lane);
    if (!track || !lane)
        return std::nullopt;
    LaneFrame f;
    f.owner = LaneOwner::ofTrack(r.track);
    f.lane = r.lane;
    f.rect = Rect(0, rowTop(row) + 1, box.size.x, rowHeight(row) - 2);
    f.time = &time();
    f.snap = snapStep();
    f.playhead = m->playing() ? playhead() : NAN;
    f.color = theme::track(lane->color ? lane->color : track->color);
    return f;
}

namespace {

/// Drags out a time range over the tracks between the press and the pointer.
class RangeGesture : public DragGesture {
  public:
    RangeGesture(EditorContext& ctx, Vec press, std::function<ArrangerRange(Vec, Vec)> make)
        : ctx(ctx), press(press), make(std::move(make)), before(ctx.state().range) {}
    void update(Vec pos, Modifiers mods) override { ctx.state().range = make(press, pos); }
    void cancel() override { ctx.state().range = before; }
    CursorShape cursor() const override { return CursorShape::IBeam; }
    bool autoScrollX() const override { return true; }
    std::string readout() const override {
        const ArrangerRange& r = ctx.state().range;
        TimeSig ts = ctx.module() ? ctx.module()->song().timeSig : TimeSig();
        return timeline::positionText(double(r.from), ts) + " – " + timeline::positionText(double(r.to), ts);
    }

  private:
    EditorContext& ctx;
    Vec press;
    std::function<ArrangerRange(Vec, Vec)> make;
    ArrangerRange before;
};

} // namespace

std::unique_ptr<DragGesture> ArrangerLanes::rangeGesture(Vec pos, Modifiers mods) {
    auto make = [this](Vec a, Vec b) {
        ArrangerRange r;
        Tick t0 = snapTick(time().xToTick(a.x), false, {});
        Tick t1 = snapTick(time().xToTick(b.x), false, {});
        r.from = std::max<Tick>(0, std::min(t0, t1));
        r.to = std::max(t0, t1);
        int r0 = rowAt(std::min(a.y, b.y)), r1 = rowAt(std::max(a.y, b.y));
        if (r0 < 0)
            r0 = 0;
        if (r1 < 0)
            r1 = int(owner.rows.size()) - 1;
        for (int i = r0; i <= r1 && i < int(owner.rows.size()); i++) {
            const TrackRow& row = owner.rows[size_t(i)];
            if (row.kind == TrackRow::Kind::Group) {
                if (const TrackGroup* g = ctx.module()->song().findGroup(row.group))
                    for (TrackId t : g->tracks)
                        if (!r.covers(t))
                            r.tracks.push_back(t);
            } else if (row.track.valid() && !r.covers(row.track)) {
                r.tracks.push_back(row.track);
            }
        }
        return r;
    };
    auto inner = std::make_unique<RangeGesture>(ctx, pos, make);
    EditorContext* c = &ctx;
    // A click without a drag drops the range.
    return std::make_unique<ClickOrDrag>(std::move(inner), pos, [c] { c->state().range = ArrangerRange(); });
}

bool ArrangerLanes::rangeAction(Action action) {
    TesseraModule* m = ctx.module();
    ArrangerRange& r = ctx.state().range;
    if (!m || r.empty())
        return false;
    NextId ids = m->ids();
    switch (action) {
    case Action::Delete:
        m->perform(ctx.state().ripple ? rangeEdits::deleteTime(m->song(), r.tracks, r.from, r.to, ids)
                                      : rangeEdits::clearRange(m->song(), r.tracks, r.from, r.to, ids));
        return true;
    case Action::DeleteTime:
        m->perform(rangeEdits::deleteTime(m->song(), r.tracks, r.from, r.to, ids));
        r.to = r.from;
        return true;
    case Action::InsertSilence:
        m->perform(rangeEdits::insertSilence(m->song(), r.tracks, r.from, r.to - r.from, ids));
        return true;
    case Action::Duplicate: {
        m->perform(rangeEdits::duplicateRange(m->song(), r.tracks, r.from, r.to, ids));
        // The range moves onto the copy, so repeating keeps stamping.
        Tick length = r.to - r.from;
        r.from += length;
        r.to += length;
        return true;
    }
    case Action::LoopToSelection:
        owner.source.setLoop(r.from, r.to);
        if (!owner.source.loopOn())
            owner.source.toggleLoop();
        return true;
    case Action::SelectNone:
        r = ArrangerRange();
        return true;
    default:
        return false;
    }
}

void ArrangerLanes::rippleDeleteSelected() {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    // Right to left, each on the song as the previous one left it, so the gaps close one after another.
    std::vector<arrange::Placed> doomed = arrange::pick(committed(), selection());
    std::sort(doomed.begin(), doomed.end(),
              [](const arrange::Placed& a, const arrange::Placed& b) { return a.clip.position > b.clip.position; });
    Song scratch = m->song();
    auto out = std::make_unique<CompoundCommand>("Ripple delete " + arrange::countClips(doomed.size()));
    for (const arrange::Placed& p : doomed) {
        CommandPtr c = rangeEdits::deleteTime(scratch, {p.track}, p.clip.position, instanceEnd(p.clip), m->ids());
        if (!c)
            continue;
        c->apply(scratch);
        out->add(std::move(c));
    }
    if (!out->empty())
        m->perform(std::move(out));
    selection().clear();
}

void ArrangerLanes::drawRange(NVGcontext* vg) {
    const ArrangerRange& r = ctx.state().range;
    if (r.empty())
        return;
    const theme::Palette& c = theme::colors();
    float x0 = float(time().tickToX(double(r.from))), x1 = float(time().tickToX(double(r.to)));
    for (size_t i = 0; i < owner.rows.size() && i < owner.tracks.heights().size(); i++) {
        const TrackRow& row = owner.rows[i];
        if (!row.track.valid() || !r.covers(row.track))
            continue;
        Rect band(x0, rowTop(int(i)), x1 - x0, rowHeight(int(i)));
        nvgBeginPath(vg);
        nvgRect(vg, band.pos.x, band.pos.y, band.size.x, band.size.y);
        nvgFillColor(vg, nvgTransRGBAf(c.select, 0.15f));
        nvgFill(vg);
    }
    nvgBeginPath(vg);
    nvgRect(vg, x0, 0, 1, box.size.y);
    nvgRect(vg, x1 - 1, 0, 1, box.size.y);
    nvgFillColor(vg, nvgTransRGBAf(c.select, 0.8f));
    nvgFill(vg);
}

bool ArrangerLanes::automationAction(Action action) {
    double pasteAt = playhead();
    if (!std::isfinite(pasteAt))
        pasteAt = time().left();
    return automation.handleAction(action, snapStep(), pasteAt);
}

void ArrangerLanes::drawLanes(NVGcontext* vg) {
    for (size_t i = 0; i < owner.rows.size() && i < owner.tracks.heights().size(); i++) {
        float top = rowTop(int(i)), h = rowHeight(int(i));
        if (top > box.size.y || top + h < 0)
            continue;
        if (std::optional<LaneFrame> frame = laneFrame(int(i)))
            automation.draw(vg, *frame);
    }
}

void ArrangerLanes::onLeave(const LeaveEvent& e) {
    automation.leave();
    hover = Hit();
    TimelineView::onLeave(e);
}

// ---- Drawing ------------------------------------------------------------------------------------------------

uint64_t ArrangerLanes::clipsKey() {
    TesseraModule* m = ctx.module();
    uint64_t key = 0;
    key = hashMix(key, double(m->songRevision()));
    key = hashMix(key, time().left());
    key = hashMix(key, time().pxPerTick());
    key = hashMix(key, owner.tracks.scroll());
    key = hashMix(key, double(previewGeneration));
    for (double h : owner.tracks.heights())
        key = hashMix(key, h);
    uint64_t selected = 0;
    for (ArrangementId id : selection())
        selected += id.value * 0x9E3779B97F4A7C15ull; // order-independent
    key = hashMix(key, double(selected));
    uint64_t overridden = 0;
    const Song& song = m->song();
    for (size_t i = 0; i < song.tracks.size(); i++) {
        LauncherStatus st = m->launcherStatus(song.tracks[i].id);
        if (st.overridden || st.queued == kNoSlot)
            overridden |= uint64_t(1) << (i % 64);
    }
    key = hashMix(key, double(overridden));
    return key;
}

void ArrangerLanes::step() {
    grid->setSize(box.size);
    clips->setSize(box.size);
    if (TesseraModule* m = ctx.module()) {
        const Song& song = m->song();
        uint64_t key = 0;
        key = hashMix(key, time().left());
        key = hashMix(key, time().pxPerTick());
        key = hashMix(key, owner.tracks.scroll());
        key = hashMix(key, double(song.timeSig.num * 64 + song.timeSig.den));
        key = hashMix(key, double(snapStep()));
        key = hashMix(key, double(ctx.state().selectedTrack.value));
        for (double h : owner.tracks.heights())
            key = hashMix(key, h);
        grid->setKey(key);
        clips->setKey(clipsKey());
    }
    TimelineView::step();
}

void ArrangerLanes::paintGrid(NVGcontext* vg, Vec size) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    const theme::Palette& c = theme::colors();
    const Song& song = m->song();
    for (size_t i = 0; i < owner.rows.size() && i < owner.tracks.heights().size(); i++) {
        float top = rowTop(int(i));
        float h = rowHeight(int(i));
        if (top > size.y || top + h < 0)
            continue;
        const TrackRow& row = owner.rows[i];
        bool selected = row.kind == TrackRow::Kind::Track && ctx.state().selectedTrack == row.track;
        NVGcolor fill = row.kind == TrackRow::Kind::Group ? nvgLerpRGBA(c.bg0, c.bg1, 0.25f)
                        : row.isLane()                    ? theme::recessed(c, c.bg0, 0.2f)
                        : selected                        ? nvgLerpRGBA(c.bg0, c.bg1, 0.7f)
                                                          : (i % 2 ? c.bg0 : nvgLerpRGBA(c.bg0, c.bg1, 0.35f));
        nvgBeginPath(vg);
        nvgRect(vg, 0, top, size.x, h);
        nvgFillColor(vg, fill);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, 0, top + h - 1, size.x, 1);
        nvgFillColor(vg, c.line0);
        nvgFill(vg);
    }
    timeline::drawGrid(vg, time(), song.timeSig, size.y, snapStep());
}

void ArrangerLanes::paintClips(NVGcontext* vg, Vec size) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    const Song& song = m->song();
    const arrange::Layout& layout = shown();
    bool arranger = m->playMode() == PlayMode::Arranger;
    for (size_t i = 0; i < owner.rows.size() && i < owner.tracks.heights().size(); i++) {
        float top = rowTop(int(i));
        float h = rowHeight(int(i));
        if (top > size.y || top + h < 0)
            continue;
        const TrackRow& row = owner.rows[i];
        if (row.kind == TrackRow::Kind::Group) {
            // A folded group shows its members' clips flattened into its row.
            if (!ctx.view().foldedGroups.count(row.group.value))
                continue;
            const TrackGroup* g = song.findGroup(row.group);
            for (const arrange::Placed& p : layout)
                if (g && std::find(g->tracks.begin(), g->tracks.end(), p.track) != g->tracks.end())
                    if (const Track* t = song.findTrack(p.track))
                        drawInstance(vg, song, *t, p.clip, top, h, true);
            continue;
        }
        if (!row.isTrack())
            continue;
        const Track* track = song.findTrack(row.track);
        if (!track)
            continue;
        // Taken over by the launcher, or about to be: a stop waiting for the launch quantize dims it already.
        LauncherStatus st = m->launcherStatus(track->id);
        bool dim = arranger && (st.overridden || st.queued == kNoSlot);
        for (const arrange::Placed& p : layout)
            if (p.track == row.track) {
                auto [subTop, subHeight] = instanceSpan(int(i), p);
                drawInstance(vg, song, *track, p.clip, subTop, subHeight, dim);
            }
    }
}

void ArrangerLanes::drawInstance(NVGcontext* vg, const Song& song, const Track& track, const ArrangerClip& a, float top,
                                 float h, bool dim) {
    const theme::Palette& c = theme::colors();
    float x0 = float(time().tickToX(double(a.position)));
    float x1 = float(time().tickToX(double(instanceEnd(a))));
    if (x1 < 0 || x0 > box.size.x)
        return;
    Rect r(x0, top + 1, std::max(2.f, x1 - x0), h - 2);
    const Clip* clip = song.findClip(a.clip);
    uint32_t color = track.color;
    float alpha = (a.muted ? kMutedAlpha : 1.f) * (dim ? kOverriddenAlpha : 1.f);
    float radius = theme::metrics::kRadiusItem;
    float strip = std::min(headerHeight(h), r.size.y);

    draw::roundedRect(vg, r, radius, theme::track(color, kBodyAlpha * alpha));
    nvgBeginPath(vg);
    if (strip < r.size.y)
        nvgRoundedRectVarying(vg, r.pos.x, r.pos.y, r.size.x, strip, radius, radius, 0, 0);
    else
        nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, radius);
    nvgFillColor(vg, theme::track(color, 0.9f * alpha));
    nvgFill(vg);

    nvgSave(vg);
    nvgIntersectScissor(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
    Rect body(r.pos.x, r.pos.y + strip, r.size.x, r.size.y - strip);
    if (clip) {
        // Loop seams: a notch and a faint line where the content starts over.
        for (Tick seam : loopSeams(*clip, a)) {
            float x = float(time().tickToX(double(seam)));
            nvgBeginPath(vg);
            nvgMoveTo(vg, x - kSeamNotch, body.pos.y);
            nvgLineTo(vg, x + kSeamNotch, body.pos.y);
            nvgLineTo(vg, x, body.pos.y + kSeamNotch);
            nvgClosePath(vg);
            nvgFillColor(vg, theme::track(color, 0.9f * alpha));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRect(vg, x, body.pos.y, 1, body.size.y);
            nvgFillColor(vg, theme::track(color, 0.25f * alpha));
            nvgFill(vg);
        }
        // The notes it plays, in miniature.
        if (body.size.y >= 6 && !clip->notes.empty()) {
            int lo = 127, hi = 0;
            for (const Note& n : clip->notes) {
                lo = std::min<int>(lo, n.pitch);
                hi = std::max<int>(hi, n.pitch);
            }
            // Drum lanes keep their notes (no track transpose) and may loop on their own length.
            bool drum = track.type == TrackType::Drum;
            int trackTranspose = drum ? 0 : track.transpose;
            lo = std::max(0, lo + a.transpose + trackTranspose);
            hi = std::min(127, hi + a.transpose + trackTranspose);
            scratch.clear();
            renderInstance(*clip, a, scratch, trackTranspose, 1, drum ? &track.drumLanes : nullptr);
            float pad = 2;
            float rowH = std::min(4.f, (body.size.y - 2 * pad) / float(hi - lo + 1));
            float span = rowH * float(hi - lo + 1);
            float base = body.pos.y + pad + (body.size.y - 2 * pad - span) / 2;
            NVGcolor noteColor = nvgLerpRGBA(theme::track(color), c.text0, 0.35f);
            noteColor.a = alpha;
            nvgBeginPath(vg);
            for (const TimedNote& n : scratch) {
                float nx0 = float(time().tickToX(double(n.start)));
                float nx1 = float(time().tickToX(double(n.end)));
                if (nx1 < 0 || nx0 > box.size.x)
                    continue;
                nvgRect(vg, nx0, base + float(hi - n.pitch) * rowH, std::max(1.f, nx1 - nx0 - 0.5f),
                        std::max(1.f, rowH - 0.5f));
            }
            nvgFillColor(vg, noteColor);
            nvgFill(vg);
        }

        // Name, then what makes this instance different from its clip, then the link badge.
        std::string extras;
        if (a.transpose != 0)
            extras += (a.transpose > 0 ? " +" : " ") + std::to_string(a.transpose);
        if (std::fabs(a.velocityScale - 1.f) > 0.005f)
            extras += " " + std::to_string(int(std::lround(a.velocityScale * 100))) + "%";
        if (std::fabs(a.stretch - 1.f) > 0.001f) {
            char text[16];
            snprintf(text, sizeof(text), " ×%.2g", double(a.stretch));
            extras += text;
        }
        bool linked = song.clipUses(a.clip) > 1;
        draw::TextStyle label{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.onTile,
                              NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        float cy = r.pos.y + std::min(strip, 16.f) / 2;
        float right = r.getRight() - 4;
        if (linked && r.size.x > 40) {
            right -= 10;
            drawLinkBadge(vg, right, cy, c.onTile);
            right -= 3;
        }
        if (!extras.empty() && r.size.x > 60) {
            draw::TextStyle small = label;
            small.font = theme::fonts::mono();
            small.align = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;
            draw::text(vg, small, right, cy, extras);
            right -= draw::textWidth(vg, small, extras) + 4;
        }
        if (right - r.pos.x > 16)
            draw::text(vg, label, r.pos.x + 4, cy, draw::ellipsize(vg, label, clip->name, right - r.pos.x - 4));
    }
    nvgRestore(vg);

    if (selection().count(a.id))
        draw::roundedRectStroke(vg, r, radius, c.select, 1.5f);
}

void ArrangerLanes::drawView(const DrawArgs& args) {
    nvgBeginPath(args.vg);
    nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(args.vg, theme::colors().bg0);
    nvgFill(args.vg);
}

void ArrangerLanes::drawRecording(NVGcontext* vg) {
    TesseraModule* m = ctx.module();
    const theme::Palette& c = theme::colors();
    double now = m->playheadTicks();
    drawTakes(vg, now);
    if (!m->recordingArrangement())
        return;
    for (const auto& [track, segment] : m->capture().active()) {
        int row = rowOf(TrackId(track));
        if (row < 0 || now <= segment.start)
            continue;
        float x0 = float(time().tickToX(segment.start));
        float x1 = float(time().tickToX(now));
        Rect r(x0, rowTop(row) + 1, x1 - x0, rowHeight(row) - 2);
        draw::roundedRect(vg, r, theme::metrics::kRadiusItem, nvgTransRGBAf(c.record, 0.25f));
        draw::roundedRectStroke(vg, r, theme::metrics::kRadiusItem, c.record, 1);
    }
}

void ArrangerLanes::drawTakes(NVGcontext* vg, double now) {
    TesseraModule* m = ctx.module();
    const theme::Palette& c = theme::colors();
    Tick bar = ticksPerBar(m->song().timeSig);
    double playTick = m->playTicks();
    for (const Recorder::Session& take : m->takes().sessions()) {
        if (take.target != Recorder::Target::Arranger)
            continue;
        int row = rowOf(take.track);
        double start = std::floor(take.passFrom / double(bar)) * double(bar);
        if (row < 0 || now <= start)
            continue;
        float top = rowTop(row) + 1, h = rowHeight(row) - 2;
        float x0 = float(time().tickToX(start));
        float x1 = float(time().tickToX(now));
        Rect r(x0, top, x1 - x0, h);
        draw::roundedRect(vg, r, theme::metrics::kRadiusItem, nvgTransRGBAf(c.record, 0.25f));
        draw::roundedRectStroke(vg, r, theme::metrics::kRadiusItem, c.record, 1);

        // The notes so far, spread over the lane by pitch.
        int low = 127, high = 0;
        auto widen = [&](int p) {
            low = std::min(low, p);
            high = std::max(high, p);
        };
        for (const TakeNote& n : take.done)
            widen(n.pitch);
        for (const auto& [pitch, open] : take.open)
            widen(pitch);
        if (low > high)
            continue;
        float span = float(std::max(12, high - low + 1));
        auto mark = [&](Tick from, Tick length, int pitch) {
            float y = top + 3 + (h - 6) * (1.f - float(pitch - low + 1) / span);
            float a = float(time().tickToX(double(from)));
            float b = float(time().tickToX(double(from + length)));
            nvgBeginPath(vg);
            nvgRect(vg, a, y, std::max(1.f, b - a), 2);
            nvgFillColor(vg, c.text0);
            nvgFill(vg);
        };
        for (const TakeNote& n : take.done)
            mark(n.start, n.length, n.pitch);
        for (const auto& [pitch, open] : take.open)
            mark(open.start, Recorder::openLength(open, playTick), pitch);
    }
}

void ArrangerLanes::drawForeground(const DrawArgs& args) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);

    // The loop region, faintly, so it reads across all lanes.
    if (owner.source.loopOn()) {
        float x0 = float(time().tickToX(double(owner.source.loopStart())));
        float x1 = float(time().tickToX(double(owner.source.loopEnd())));
        nvgBeginPath(vg);
        nvgRect(vg, x0, 0, x1 - x0, box.size.y);
        nvgFillColor(vg, nvgTransRGBAf(c.accent, 0.04f));
        nvgFill(vg);
    }

    // Hover: a light outline on the clip a click would grab.
    if (hover.id.valid() && !ctx.dragging) {
        if (const arrange::Placed* p = arrange::find(shown(), hover.id)) {
            int row = rowOf(p->track);
            if (row >= 0) {
                float x0 = float(time().tickToX(double(p->clip.position)));
                float x1 = float(time().tickToX(double(instanceEnd(p->clip))));
                auto [top, h] = instanceSpan(row, *p);
                draw::roundedRectStroke(vg, Rect(x0, top + 1, x1 - x0, h - 2), theme::metrics::kRadiusItem,
                                        nvgTransRGBAf(c.text0, 0.35f), 1);
                // The Slice tool shows where a click would cut, snapped as the click is (Trello, 13 Oct).
                if (ctx.state().tool == Tool::Slice) {
                    bool free = Modifiers::fromGlfw(APP->window->getMods()).alt;
                    Tick at = snapTick(time().xToTick(localMouse().x), free, {});
                    if (at > p->clip.position && at < instanceEnd(p->clip)) {
                        float x = std::round(float(time().tickToX(double(at)))) + 0.5f;
                        nvgBeginPath(vg);
                        nvgMoveTo(vg, x, top + 1);
                        nvgLineTo(vg, x, top + h - 1);
                        nvgStrokeColor(vg, c.text0);
                        nvgStrokeWidth(vg, 1.5f);
                        nvgStroke(vg);
                    }
                }
            }
        }
    }

    drawLanes(vg);
    drawRange(vg);
    drawRecording(vg);

    if (shown().empty()) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        // Only in the empty space below the tracks, never over them.
        float contentBottom = float(owner.tracks.contentHeight() - owner.tracks.scroll());
        if (contentBottom + 30 < box.size.y)
            draw::text(vg, hint, box.size.x / 2, contentBottom + 20,
                       "Drag a clip here from the launcher, or draw one with D.");
    }

    double p = playhead();
    if (std::isfinite(p)) {
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

} // namespace tessera
