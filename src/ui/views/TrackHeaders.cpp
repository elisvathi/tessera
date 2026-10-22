#include "TrackHeaders.hpp"

#include "../../model/TrackCommands.hpp"
#include "../../module/Registry.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/DeviceActions.hpp"
#include "../editing/TrackActions.hpp"
#include "../interaction/ClickOrDrag.hpp"
#include "../interaction/Navigation.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/InlineTextField.hpp"
#include "Arranger.hpp"
#include "AutomationEditor.hpp"

#include <algorithm>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kPad = 8;
constexpr float kIndent = 12;
constexpr float kButtonW = 20;
constexpr float kButtonH = 16;
constexpr float kButtonGap = 3;
// Below this column width (the in-panel editor), buttons shrink so the track name keeps its room.
constexpr float kCompactWidth = 180;
constexpr float kButtonWCompact = 16;
constexpr float kButtonHCompact = 14;
constexpr float kButtonGapCompact = 2;
constexpr float kArmWidth = 16;
constexpr float kArmWidthCompact = 13;
constexpr float kArrowWidth = 12;
constexpr float kArrowWidthCompact = 10;
constexpr float kTileWidth = 18;
constexpr float kTileWidthCompact = 16;
constexpr float kNameGap = 6;
constexpr float kDrumMarkWidth = 16;
constexpr float kEdgeZone = 4;
constexpr float kFoldZone = 22;
constexpr float kCompactHeight = 24;
constexpr float kExpandedHeight = 96;
constexpr float kRowTopPad = 6;
constexpr float kAddRowHeight = 28;

/// Drags a track's bottom edge. With Alt every track takes the new height.
class ResizeLaneGesture : public DragGesture {
  public:
    ResizeLaneGesture(EditorContext& ctx, AutomationId lane, float startY)
        : ctx(ctx), lane(lane), startY(startY), startHeight(ctx.view().laneHeight(lane)) {}
    void update(Vec pos, Modifiers mods) override {
        height = std::clamp(startHeight + (pos.y - startY), TrackAxis::kMinHeight, TrackAxis::kMaxHeight);
        ctx.view().laneHeights[lane.value] = height;
    }
    void cancel() override { ctx.view().laneHeights[lane.value] = startHeight; }
    std::string readout() const override { return std::to_string(int(height)) + " px"; }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    EditorContext& ctx;
    AutomationId lane;
    float startY;
    double startHeight;
    double height = 0;
};

class ResizeTrackGesture : public DragGesture {
  public:
    ResizeTrackGesture(EditorContext& ctx, TrackId track, float startY)
        : ctx(ctx), track(track), startY(startY), startHeights(ctx.view().trackHeights),
          startHeight(ctx.view().trackHeight(track)) {}

    void update(Vec pos, Modifiers mods) override {
        ViewState& view = ctx.view();
        view.trackHeights = startHeights;
        double h = std::clamp(startHeight + (pos.y - startY), TrackAxis::kMinHeight, TrackAxis::kMaxHeight);
        if (mods.alt) {
            for (const Track& t : ctx.module()->song().tracks)
                view.trackHeights[t.id.value] = h;
        } else {
            view.trackHeights[track.value] = h;
        }
        height = h;
    }
    void cancel() override { ctx.view().trackHeights = startHeights; }
    std::string readout() const override { return std::to_string(int(height)) + " px"; }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    EditorContext& ctx;
    TrackId track;
    float startY;
    std::map<uint64_t, double> startHeights;
    double startHeight;
    double height = 0;
};

/// Drags tracks (or a whole group) to a new place; an insertion line shows where they will land.
class ReorderGesture : public DragGesture {
  public:
    ReorderGesture(EditorContext& ctx, TrackHeaders& headers, std::vector<TrackId> ids, std::optional<GroupId> ownGroup)
        : ctx(ctx), headers(headers), ids(std::move(ids)), ownGroup(ownGroup) {}

    void update(Vec pos, Modifiers mods) override {
        target = headers.dropTargetAt(pos.y);
        if (ownGroup) {
            // A group moves as a whole and stays itself; groups don't nest. Dropped inside another group, it
            // lands just before that group.
            if (target.group && *target.group != *ownGroup) {
                const Song& song = ctx.module()->song();
                for (const TrackGroup& g : song.groups)
                    if (g.id == *target.group && !g.tracks.empty())
                        target.before = song.trackIndex(g.tracks.front());
            }
            target.group = ownGroup;
        }
    }
    void commit() override { trackActions::move(ctx, ids, target.before, target.group); }
    void drawOverlay(NVGcontext* vg) override {
        float x = target.indented ? kIndent : 0;
        nvgBeginPath(vg);
        nvgRect(vg, x, target.lineY - 1, headers.box.size.x - x, 2);
        nvgFillColor(vg, theme::colors().accent);
        nvgFill(vg);
    }
    std::string readout() const override {
        return ids.size() == 1 ? "Move track" : "Move " + std::to_string(ids.size()) + " tracks";
    }
    CursorShape cursor() const override { return CursorShape::Move; }
    bool autoScrollY() const override { return true; }

  private:
    EditorContext& ctx;
    TrackHeaders& headers;
    std::vector<TrackId> ids;
    std::optional<GroupId> ownGroup;
    TrackHeaders::DropTarget target;
};

/// A small drum shell, drawn before a drum track's name; instrument tracks need no mark.
void drawDrumMark(NVGcontext* vg, float x, float cy, NVGcolor color) {
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 1.2f);
    nvgBeginPath(vg);
    nvgEllipse(vg, x + 5, cy - 1.5f, 4.5f, 2);
    nvgMoveTo(vg, x + 0.5f, cy - 1.5f);
    nvgLineTo(vg, x + 0.5f, cy + 2);
    nvgMoveTo(vg, x + 9.5f, cy - 1.5f);
    nvgLineTo(vg, x + 9.5f, cy + 2);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgEllipse(vg, x + 5, cy + 2, 4.5f, 2);
    nvgStroke(vg);
}

/// The jack mark for "no output module plays this track", in the queue colour so it reads as a warning.
void drawMissingOutput(NVGcontext* vg, Rect r, bool hover, NVGcolor color) {
    if (hover)
        draw::roundedRect(vg, r, 3, theme::colors().line0);
    Vec c = r.getCenter();
    nvgBeginPath(vg);
    nvgCircle(vg, c.x, c.y, 4);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 1.3f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, c.x, c.y, 1.3f);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawFoldArrow(NVGcontext* vg, float x, float cy, bool folded, NVGcolor color) {
    nvgBeginPath(vg);
    if (folded) {
        nvgMoveTo(vg, x, cy - 4);
        nvgLineTo(vg, x + 5, cy);
        nvgLineTo(vg, x, cy + 4);
    } else {
        nvgMoveTo(vg, x - 1, cy - 2.5f);
        nvgLineTo(vg, x + 7, cy - 2.5f);
        nvgLineTo(vg, x + 3, cy + 2.5f);
    }
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawSmallButton(NVGcontext* vg, Rect r, const char* label, bool on, bool hover, NVGcolor onColor) {
    const theme::Palette& c = theme::colors();
    NVGcolor fill = on ? onColor : hover ? c.line0 : c.bg2;
    draw::roundedRect(vg, r, 3, fill);
    draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kTextRuler, on ? c.ink : c.text1,
                          NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, style, r.getCenter().x, r.getCenter().y + 0.5f, label);
}

} // namespace

// ---- Geometry -----------------------------------------------------------------------------------------------

float TrackHeaders::lineY(float top, float h) {
    // Centered in compact rows; otherwise on the row's first line.
    return std::min(top + kRowTopPad + kButtonH / 2, top + h / 2);
}

float TrackHeaders::indentOf(const TrackRow& row) const { return row.inGroup ? kIndent : 0; }

bool TrackHeaders::compact() const { return box.size.x < kCompactWidth; }

Rect TrackHeaders::soloRect(float top, float h) {
    float w = compact() ? kButtonWCompact : kButtonW;
    float bh = compact() ? kButtonHCompact : kButtonH;
    float pad = compact() ? kPad / 2 : kPad;
    return Rect(box.size.x - pad - w, lineY(top, h) - bh / 2, w, bh);
}

Rect TrackHeaders::muteRect(float top, float h) {
    Rect s = soloRect(top, h);
    float gap = compact() ? kButtonGapCompact : kButtonGap;
    return Rect(s.pos.x - gap - s.size.x, s.pos.y, s.size.x, s.size.y);
}

Rect TrackHeaders::automationRect(float top, float h) {
    Rect m = muteRect(top, h);
    float w = compact() ? kArrowWidthCompact : kArrowWidth;
    return Rect(m.pos.x - (compact() ? kButtonGapCompact : kButtonGap) - w, m.pos.y, w, m.size.y);
}

Rect TrackHeaders::armRect(float top, float h) {
    Rect a = automationRect(top, h);
    float w = compact() ? kArmWidthCompact : kArmWidth;
    return Rect(a.pos.x - w, a.pos.y, w, a.size.y);
}

Rect TrackHeaders::outputRect(float top, float h) {
    Rect a = armRect(top, h);
    float w = compact() ? kArmWidthCompact : kArmWidth;
    return Rect(a.pos.x - w, a.pos.y, w, a.size.y);
}

bool TrackHeaders::missingOutput(const Track& track) {
    TesseraModule* m = ctx.module();
    return m && m->song().hasNotes(track) && !Registry::get().hasOutputFor(m->id, track.id);
}

Rect TrackHeaders::nameRect(int row) {
    const TrackRow& r = owner.rows[size_t(row)];
    float top = float(owner.tracks.trackTop(row));
    float h = float(owner.tracks.trackHeight(row));
    float y = lineY(top, h);
    if (r.kind == TrackRow::Kind::Group)
        return Rect(kFoldZone + 14, y - 9, box.size.x - kFoldZone - 14 - kPad, 18);
    const Track* t = ctx.module() ? ctx.module()->song().findTrack(r.track) : nullptr;
    float x = indentOf(r) + (compact() ? kTileWidthCompact : kTileWidth) + kNameGap;
    if (t && t->type == TrackType::Drum)
        x += kDrumMarkWidth;
    bool noOutput = t && missingOutput(*t);
    float right = noOutput ? outputRect(top, h).pos.x : armRect(top, h).pos.x;
    return Rect(x, y - 9, std::max(0.f, right - 4 - x), 18);
}

Rect TrackHeaders::addRowRect() {
    float top = float(owner.tracks.contentHeight() - owner.tracks.scroll());
    return Rect(kPad / 2, top + 4, box.size.x - kPad, kAddRowHeight - 8);
}

void TrackHeaders::openAddMenu() {
    EditorContext* c = &ctx;
    rack::ui::Menu* menu = rack::createMenu();
    bool full = !trackActions::canAdd(ctx.module()->song(), 1);
    menu->addChild(rack::createMenuLabel(full ? trackActions::kTracksFull : "Add"));
    menu->addChild(rack::createMenuItem(
        "Instrument track", shortcutLabel(Action::AddInstrumentTrack),
        [c] { trackActions::add(*c, TrackType::Instrument); }, full));
    menu->addChild(rack::createMenuItem(
        "Drum track", shortcutLabel(Action::AddDrumTrack), [c] { trackActions::add(*c, TrackType::Drum); }, full));
}

TrackHeaders::Hit TrackHeaders::hitTest(Vec pos) {
    Hit hit;
    hit.row = owner.tracks.trackAt(pos.y);
    if (hit.row < 0 || hit.row >= int(owner.rows.size())) {
        // Below the tracks: the add row, then empty space (both offer to add a track).
        Hit empty;
        empty.part = addRowRect().contains(pos) ? Part::AddRow : Part::Empty;
        return empty;
    }
    const TrackRow& row = owner.rows[size_t(hit.row)];
    float top = float(owner.tracks.trackTop(hit.row));
    float h = float(owner.tracks.trackHeight(hit.row));
    if (row.kind == TrackRow::Kind::Group) {
        hit.part = pos.x < kFoldZone ? Part::Fold : nameRect(hit.row).contains(pos) ? Part::Name : Part::Body;
        return hit;
    }
    if (row.kind == TrackRow::Kind::AddLane) {
        hit.part = Part::AddLane;
        return hit;
    }
    if (row.kind == TrackRow::Kind::Lane) {
        hit.part = pos.y >= top + h - kEdgeZone     ? Part::Edge
                   : soloRect(top, h).contains(pos) ? Part::LaneMute
                   : muteRect(top, h).contains(pos) ? Part::LaneState
                                                    : Part::Body;
        return hit;
    }
    if (pos.y >= top + h - kEdgeZone)
        hit.part = Part::Edge;
    else if (automationRect(top, h).contains(pos))
        hit.part = Part::Automation;
    else if (armRect(top, h).contains(pos))
        hit.part = Part::Arm;
    else if (const Track* t = ctx.module() ? ctx.module()->song().findTrack(row.track) : nullptr;
             outputRect(top, h).contains(pos) && t && missingOutput(*t))
        hit.part = Part::Output;
    else if (muteRect(top, h).contains(pos))
        hit.part = Part::Mute;
    else if (soloRect(top, h).contains(pos))
        hit.part = Part::Solo;
    else if (nameRect(hit.row).contains(pos))
        hit.part = Part::Name;
    else
        hit.part = Part::Body;
    return hit;
}

TrackHeaders::DropTarget TrackHeaders::dropTargetAt(float y) {
    const Song& song = ctx.module()->song();
    DropTarget t;
    int row = owner.tracks.trackAt(y);
    if (row < 0 || row >= int(owner.rows.size())) {
        t.before = int(song.tracks.size());
        t.lineY = float(owner.tracks.contentHeight() - owner.tracks.scroll());
        return t;
    }
    const TrackRow& r = owner.rows[size_t(row)];
    float top = float(owner.tracks.trackTop(row));
    float h = float(owner.tracks.trackHeight(row));
    bool upper = y < top + h / 2;
    if (r.kind == TrackRow::Kind::Group) {
        const TrackGroup* g = song.findGroup(r.group);
        int first = g && !g->tracks.empty() ? song.trackIndex(g->tracks.front()) : int(song.tracks.size());
        t.before = first;
        if (upper) {
            t.lineY = top;
        } else {
            // Onto the header's lower half: into the group, at its top.
            t.group = r.group;
            t.lineY = top + h;
            t.indented = true;
        }
        return t;
    }
    int index = song.trackIndex(r.track);
    t.before = upper ? index : index + 1;
    t.lineY = upper ? top : top + h;
    if (r.inGroup) {
        t.group = r.group;
        t.indented = true;
    }
    return t;
}

// ---- Mouse --------------------------------------------------------------------------------------------------

std::unique_ptr<DragGesture> TrackHeaders::startGesture(Vec pos, int button, Modifiers mods) {
    owner.target = ArrangerView::Target::Tracks;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return nullptr;
    Hit hit = hitTest(pos);
    if (hit.row < 0 || hit.part == Part::Fold || hit.part == Part::Arm || hit.part == Part::Mute ||
        hit.part == Part::Solo || hit.part == Part::Automation)
        return nullptr;
    const TrackRow& row = owner.rows[size_t(hit.row)];
    if (row.isLane())
        return hit.part == Part::Edge && row.kind == TrackRow::Kind::Lane
                   ? std::make_unique<ResizeLaneGesture>(ctx, row.lane, pos.y)
                   : nullptr;
    if (row.kind == TrackRow::Kind::Group) {
        const TrackGroup* g = ctx.module()->song().findGroup(row.group);
        if (!g || g->tracks.empty())
            return nullptr;
        GroupId gid = row.group;
        auto onClick = [this, gid] {
            const TrackGroup* group = ctx.module()->song().findGroup(gid);
            if (group && !group->tracks.empty()) {
                ctx.state().selectedTracks = group->tracks;
                ctx.state().selectedTrack = group->tracks.front();
            }
        };
        return std::make_unique<ClickOrDrag>(std::make_unique<ReorderGesture>(ctx, *this, g->tracks, gid), pos,
                                             onClick);
    }
    if (hit.part == Part::Edge)
        return std::make_unique<ResizeTrackGesture>(ctx, row.track, pos.y);

    // A press selects at once (so a drag moves what you grabbed); a click in place adjusts the selection.
    TrackId id = row.track;
    const std::vector<TrackId>& sel = ctx.state().selectedTracks;
    bool wasSelected = std::find(sel.begin(), sel.end(), id) != sel.end();
    if (!wasSelected)
        trackActions::click(ctx, id, mods.shift, mods.ctrl);
    auto onClick = [this, id, wasSelected, mods] {
        if (wasSelected)
            trackActions::click(ctx, id, mods.shift, mods.ctrl);
    };
    return std::make_unique<ClickOrDrag>(
        std::make_unique<ReorderGesture>(ctx, *this, trackActions::targets(ctx, id), std::nullopt), pos, onClick);
}

void TrackHeaders::click(Vec pos, int button, Modifiers mods) {
    owner.target = ArrangerView::Target::Tracks;
    Hit hit = hitTest(pos);
    if (hit.row < 0) {
        // Empty space and the add row: a click on the add row or a right-click anywhere below the tracks.
        if (hit.part == Part::AddRow || button == GLFW_MOUSE_BUTTON_RIGHT)
            openAddMenu();
        return;
    }
    const TrackRow& row = owner.rows[size_t(hit.row)];
    if (row.kind == TrackRow::Kind::AddLane) {
        trackActions::addAutomationLane(ctx, row.track);
        return;
    }
    if (row.kind == TrackRow::Kind::Lane) {
        LaneOwner laneOwner = LaneOwner::ofTrack(row.track);
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            AutomationEditor::appendLaneMenu(rack::createMenu(), ctx, laneOwner, row.lane);
        } else if (button == GLFW_MOUSE_BUTTON_LEFT && hit.part == Part::LaneMute) {
            TesseraModule* m = ctx.module();
            if (const AutomationEnvelope* l = findLane(m->song(), laneOwner, row.lane)) {
                AutomationEnvelope next = *l;
                next.mute = !next.mute;
                m->perform(replaceLane(m->song(), laneOwner, next, next.mute ? "Mute lane" : "Unmute lane"));
            }
        }
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        rack::ui::Menu* menu = rack::createMenu();
        int index = hit.row;
        if (row.kind == TrackRow::Kind::Group) {
            trackActions::appendGroupMenu(menu, ctx, row.group, [this, index] { startRename(index); });
        } else {
            const std::vector<TrackId>& sel = ctx.state().selectedTracks;
            if (std::find(sel.begin(), sel.end(), row.track) == sel.end())
                trackActions::click(ctx, row.track, false, false);
            trackActions::appendTrackMenu(menu, ctx, row.track, [this, index] { startRename(index); });
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    switch (hit.part) {
    case Part::Fold: {
        std::set<uint64_t>& folded = ctx.view().foldedGroups;
        if (!folded.erase(row.group.value))
            folded.insert(row.group.value);
        break;
    }
    case Part::Automation: {
        std::set<uint64_t>& open = ctx.view().openAutomation;
        if (!open.erase(row.track.value))
            open.insert(row.track.value);
        break;
    }
    case Part::Arm:
        trackActions::toggleArm(ctx, row.track, mods.ctrl);
        break;
    case Part::Mute:
        trackActions::toggleMute(ctx, row.track);
        break;
    case Part::Solo:
        trackActions::toggleSolo(ctx, row.track, !mods.shift);
        break;
    default:
        break;
    }
}

void TrackHeaders::doubleClick(Vec pos, Modifiers mods) {
    Hit hit = hitTest(pos);
    if (hit.part == Part::Empty) {
        // Double-click in the empty space below the tracks adds an instrument track.
        trackActions::add(ctx, TrackType::Instrument);
        return;
    }
    if (hit.row < 0)
        return;
    const TrackRow& row = owner.rows[size_t(hit.row)];
    if (row.isLane())
        return;
    if (hit.part == Part::Edge) {
        // Double-clicking the bottom edge toggles between compact and expanded.
        ViewState& view = ctx.view();
        view.trackHeights[row.track.value] =
            view.trackHeight(row.track) > kCompactHeight ? kCompactHeight : kExpandedHeight;
        return;
    }
    if (hit.part == Part::Name || hit.part == Part::Body)
        startRename(hit.row);
}

void TrackHeaders::startRename(int row) {
    if (row < 0 || row >= int(owner.rows.size()))
        return;
    const TrackRow r = owner.rows[size_t(row)];
    const Song& song = ctx.module()->song();
    std::string current;
    if (r.kind == TrackRow::Kind::Group) {
        const TrackGroup* g = song.findGroup(r.group);
        current = g ? g->name : "";
    } else {
        const Track* t = song.findTrack(r.track);
        current = t ? t->name : "";
    }
    EditorContext* c = &ctx;
    auto* field = new InlineTextField(current, [c, r](const std::string& name) {
        if (r.kind == TrackRow::Kind::Group)
            trackActions::renameGroup(*c, r.group, name);
        else
            trackActions::rename(*c, r.track, name);
    });
    field->box = nameRect(row);
    addChild(field);
    field->focus();
}

CursorShape TrackHeaders::hoverCursor(Vec pos, Modifiers mods) {
    hover = hitTest(pos);
    if (hover.row != tipHit.row || hover.part != tipHit.part) {
        tipHit = hover;
        tip.leave();
        std::string text = tipFor(hover);
        if (!text.empty()) {
            tip.setText(text);
            tip.enter();
        }
    }
    if (hover.part == Part::AddRow || hover.part == Part::AddLane)
        return CursorShape::Hand;
    return hover.part == Part::Edge ? CursorShape::ResizeV : CursorShape::Arrow;
}

std::string TrackHeaders::tipFor(const Hit& hit) {
    if (hit.row < 0 || hit.row >= int(owner.rows.size()) || owner.rows[size_t(hit.row)].kind != TrackRow::Kind::Track)
        return "";
    const Track* t = ctx.module() ? ctx.module()->song().findTrack(owner.rows[size_t(hit.row)].track) : nullptr;
    if (!t)
        return "";
    switch (hit.part) {
    case Part::Arm:
        return t->armed ? "Armed: records when the transport records. Click to disarm" : "Arm for recording";
    case Part::Automation:
        return t->automation.empty() ? "Automation lanes: add one to draw CV or turn a knob"
                                     : "Automation lanes (" + std::to_string(t->automation.size()) + ")";
    case Part::Output:
        return t->type == TrackType::Drum ? "Nothing plays this track. Add a Tessera Drum Out and pick this track"
                                          : "Nothing plays this track. Add a Tessera Track Out and pick this track";
    case Part::Mute:
        return withShortcut("Mute", Action::MuteTracks);
    case Part::Solo:
        return withShortcut("Solo (Shift+click: only this track)", Action::SoloTracks);
    default:
        return "";
    }
}

void TrackHeaders::step() {
    InteractiveView::step();
    tip.step();
}

void TrackHeaders::onLeave(const LeaveEvent& e) {
    hover = Hit();
    tipHit = Hit();
    tip.leave();
    InteractiveView::onLeave(e);
}

void TrackHeaders::wheel(Vec pos, Vec delta, Modifiers mods) {
    ctx.touchNavigation();
    if (mods.alt) {
        owner.tracks.zoomAt(pos.y, wheelZoomFactor(delta.y));
        owner.writeHeights();
    } else {
        owner.tracks.scrollByPx(-delta.y);
    }
    ctx.view().trackScroll = owner.tracks.scroll();
}

// ---- Drawing ------------------------------------------------------------------------------------------------

void TrackHeaders::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);

    if (!ctx.module())
        return;
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    for (size_t i = 0; i < owner.rows.size() && i < owner.tracks.heights().size(); i++) {
        float top = float(owner.tracks.trackTop(int(i)));
        float h = float(owner.tracks.trackHeight(int(i)));
        if (top > box.size.y || top + h < 0)
            continue;
        switch (owner.rows[i].kind) {
        case TrackRow::Kind::Group:
            drawGroupRow(vg, int(i), top, h);
            break;
        case TrackRow::Kind::Lane:
            drawLaneRow(vg, int(i), top, h);
            break;
        case TrackRow::Kind::AddLane:
            drawAddLaneRow(vg, int(i), top, h);
            break;
        default:
            drawTrackRow(vg, int(i), top, h);
            break;
        }
    }
    drawAddRow(vg);
    nvgRestore(vg);

    nvgBeginPath(vg);
    nvgRect(vg, box.size.x - 1, 0, 1, box.size.y);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);
}

void TrackHeaders::drawAddRow(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    Rect r = addRowRect();
    if (r.pos.y > box.size.y || int(ctx.module()->song().tracks.size()) >= kMaxTracks)
        return;
    bool hovered = hover.part == Part::AddRow && !hasGesture();
    if (hovered)
        draw::roundedRect(vg, r, theme::metrics::kRadiusControl, c.bg2);
    // A dashed outline, so it reads as "a place for a new track" rather than a track.
    nvgBeginPath(vg);
    for (float x = r.pos.x; x < r.pos.x + r.size.x; x += 8) {
        float x1 = std::min(x + 4, r.pos.x + r.size.x);
        nvgMoveTo(vg, x, r.pos.y + 0.5f);
        nvgLineTo(vg, x1, r.pos.y + 0.5f);
        nvgMoveTo(vg, x, r.pos.y + r.size.y - 0.5f);
        nvgLineTo(vg, x1, r.pos.y + r.size.y - 0.5f);
    }
    nvgStrokeColor(vg, hovered ? c.line2 : c.line1);
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
    draw::TextStyle text{theme::fonts::uiStrong(), theme::metrics::kTextUi, hovered ? c.text0 : c.text2,
                         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, text, r.getCenter().x, r.getCenter().y, "+  Add track");
}

void TrackHeaders::drawGroupRow(NVGcontext* vg, int index, float top, float h) {
    const theme::Palette& c = theme::colors();
    const TrackRow& row = owner.rows[size_t(index)];
    const TrackGroup* g = ctx.module()->song().findGroup(row.group);
    if (!g)
        return;
    bool folded = ctx.view().foldedGroups.count(g->id.value) > 0;
    bool hovered = hover.row == index && !hasGesture();
    nvgBeginPath(vg);
    nvgRect(vg, 0, top, box.size.x, h);
    nvgFillColor(vg, hovered ? c.bg2 : nvgLerpRGBA(c.bg1, c.bg0, 0.5f));
    nvgFill(vg);

    float y = top + h / 2;
    drawFoldArrow(vg, 8, y, folded, hovered && hover.part == Part::Fold ? c.text0 : c.text1);
    draw::roundedRect(vg, Rect(kFoldZone, y - 5, 8, 10), 2, theme::track(g->color));
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle count{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    std::string tracks = std::to_string(g->tracks.size()) + (g->tracks.size() == 1 ? " track" : " tracks");
    float countWidth = draw::textWidth(vg, count, tracks);
    draw::text(vg, count, box.size.x - kPad, y, tracks);
    float x = kFoldZone + 14;
    draw::text(vg, name, x, y, draw::ellipsize(vg, name, g->name, box.size.x - kPad - countWidth - 8 - x));

    nvgBeginPath(vg);
    nvgRect(vg, 0, top + h - 1, box.size.x, 1);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);
}

void drawLaneRecordMark(NVGcontext* vg, rack::math::Vec at, LaneRecordState state) {
    const theme::Palette& c = theme::colors();
    switch (state) {
    case LaneRecordState::Records:
        nvgBeginPath(vg);
        nvgCircle(vg, at.x, at.y, 3.5f);
        nvgFillColor(vg, c.record);
        nvgFill(vg);
        return;
    case LaneRecordState::Missing: {
        draw::TextStyle bang{theme::fonts::uiStrong(), theme::metrics::kTextUi, c.queue,
                             NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, bang, at.x, at.y, "!");
        return;
    }
    case LaneRecordState::Kept:
        nvgBeginPath(vg);
        nvgCircle(vg, at.x, at.y, 3.5f);
        nvgMoveTo(vg, at.x - 2.5f, at.y + 2.5f);
        nvgLineTo(vg, at.x + 2.5f, at.y - 2.5f);
        nvgStrokeColor(vg, c.text2);
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
        return;
    case LaneRecordState::Off:
        return;
    }
}

void TrackHeaders::drawLaneRow(NVGcontext* vg, int index, float top, float h) {
    const theme::Palette& c = theme::colors();
    TesseraModule* m = ctx.module();
    const TrackRow& row = owner.rows[size_t(index)];
    const Track* track = m->song().findTrack(row.track);
    const AutomationEnvelope* lane = findLane(m->song(), LaneOwner::ofTrack(row.track), row.lane);
    if (!track || !lane)
        return;
    bool hovered = hover.row == index && !hasGesture();
    float indent = indentOf(row) + kIndent;
    nvgBeginPath(vg);
    nvgRect(vg, 0, top, box.size.x, h);
    nvgFillColor(vg, nvgLerpRGBA(c.bg1, c.bg0, hovered ? 0.3f : 0.5f));
    nvgFill(vg);
    NVGcolor color = theme::track(lane->color ? lane->color : track->color);
    nvgBeginPath(vg);
    nvgRect(vg, indent, top + 2, 2, h - 4);
    nvgFillColor(vg, lane->mute ? c.text2 : color);
    nvgFill(vg);

    float y = lineY(top, h);
    Rect mute = soloRect(top, h);
    drawSmallButton(vg, mute, "M", lane->mute, hovered && hover.part == Part::LaneMute, c.queue);
    // Whether the lane records: the track arm is the switch, this only says what will happen.
    Rect arm = muteRect(top, h);
    std::string missing;
    LaneRecordState state = laneRecordState(
        m->song(), LaneOwner::ofTrack(row.track), *lane, m->autoRecInputs(),
        [](const ParamTarget& t) { return APP->engine->getModule(t.moduleId) != nullptr; }, &missing);
    drawLaneRecordMark(vg, arm.getCenter(), state);
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextUi, lane->mute ? c.text2 : c.text1,
                         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    float x = indent + 8;
    draw::text(vg, name, x, y, draw::ellipsize(vg, name, lane->name, arm.pos.x - x - 6));
    if (h >= 34) {
        // The output it drives and the value now, in volts; for a knob lane, the knob's value.
        double now = m->playheadTicks();
        draw::TextStyle detail{theme::fonts::mono(), theme::metrics::kTextRuler, c.text2,
                               NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        std::string text;
        if (lane->paramTarget.valid()) {
            const devices::ParamInfo* info = deviceActions::infoOf(m->song(), lane->paramTarget);
            if (info) {
                text = "knob  ·  " + devices::format(*info, evaluate(*lane, now));
            } else {
                text = "Target missing";
                detail.color = c.record;
            }
        } else {
            char volts[48];
            snprintf(volts, sizeof(volts), "  ·  %.2f V", double(toVolts(*lane, evaluate(*lane, now))));
            text = laneOutputLabel(*lane) + volts;
            std::string source = laneSourceLabel(*lane);
            if (!source.empty())
                text += "  ·  " + source;
        }
        if (state == LaneRecordState::Kept)
            text += "  ·  kept";
        if (state == LaneRecordState::Missing) {
            text = missing;
            detail.color = c.queue;
        }
        draw::text(vg, detail, x, top + kRowTopPad + kButtonH + 12,
                   draw::ellipsize(vg, detail, text, box.size.x - x - 6));
    }
    if (h >= 50 && !lane->paramTarget.valid()) {
        // What it is mapped to, or that it is waiting for a knob.
        std::string mapped;
        if (m->learningLane() == lane->id)
            mapped = "Move a MIDI control or touch a knob…";
        else if (!lane->targets.empty())
            mapped = "→ " + TesseraModule::targetName(lane->targets.front()) +
                     (lane->targets.size() > 1 ? "  +" + std::to_string(lane->targets.size() - 1) : "");
        draw::TextStyle map{theme::fonts::ui(), theme::metrics::kTextRuler,
                            m->learningLane() == lane->id ? c.accent : color, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, map, x, top + kRowTopPad + kButtonH + 26, draw::ellipsize(vg, map, mapped, box.size.x - x - 6));
    }
    nvgBeginPath(vg);
    nvgRect(vg, 0, top + h - 1, box.size.x, 1);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);
}

void TrackHeaders::drawAddLaneRow(NVGcontext* vg, int index, float top, float h) {
    const theme::Palette& c = theme::colors();
    const TrackRow& row = owner.rows[size_t(index)];
    bool hovered = hover.row == index && !hasGesture();
    nvgBeginPath(vg);
    nvgRect(vg, 0, top, box.size.x, h);
    nvgFillColor(vg, nvgLerpRGBA(c.bg1, c.bg0, 0.5f));
    nvgFill(vg);
    draw::TextStyle add{theme::fonts::ui(), theme::metrics::kTextRuler, hovered ? c.text0 : c.text2,
                        NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::text(vg, add, indentOf(row) + kIndent + 8, top + h / 2, "+ Add automation lane");
    nvgBeginPath(vg);
    nvgRect(vg, 0, top + h - 1, box.size.x, 1);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);
}

void TrackHeaders::drawTrackRow(NVGcontext* vg, int index, float top, float h) {
    const theme::Palette& c = theme::colors();
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    const TrackRow& row = owner.rows[size_t(index)];
    const Track* track = song.findTrack(row.track);
    if (!track)
        return;
    const Track& t = *track;
    const std::vector<TrackId>& sel = ctx.state().selectedTracks;
    bool selected = std::find(sel.begin(), sel.end(), t.id) != sel.end() || ctx.state().selectedTrack == t.id;
    bool focus = ctx.state().selectedTrack == t.id;
    bool hovered = hover.row == index && !hasGesture();
    NVGcolor color = theme::track(t.color);
    float indent = indentOf(row);

    NVGcolor bg = selected ? c.bg2 : hovered ? nvgLerpRGBA(c.bg1, c.bg2, 0.5f) : c.bg1;
    nvgBeginPath(vg);
    nvgRect(vg, 0, top, box.size.x, h);
    nvgFillColor(vg, bg);
    nvgFill(vg);

    // The colour tile carries the track number, so the name gets the rest of the row.
    float tileW = compact() ? kTileWidthCompact : kTileWidth;
    bool audible = song.isAudible(t);
    nvgBeginPath(vg);
    nvgRect(vg, indent, top, tileW, h - 1);
    nvgFillColor(vg, audible ? color : nvgTransRGBAf(color, 0.35f));
    nvgFill(vg);
    float y = lineY(top, h);
    draw::TextStyle number{theme::fonts::uiStrong(), theme::metrics::kTextRuler,
                           audible ? c.onTile : nvgTransRGBAf(c.text1, 0.8f), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, number, indent + tileW / 2, y + 0.5f, std::to_string(song.trackIndex(t.id) + 1));
    if (t.type == TrackType::Drum)
        drawDrumMark(vg, indent + tileW + kNameGap, y, c.text2);

    if (missingOutput(t))
        drawMissingOutput(vg, outputRect(top, h), hovered && hover.part == Part::Output, c.queue);

    // Arm and the lanes arrow are always there but small and dim until they are on.
    Rect arm = armRect(top, h);
    bool armHover = hovered && hover.part == Part::Arm;
    if (armHover)
        draw::roundedRect(vg, arm, 3, c.line0);
    nvgBeginPath(vg);
    nvgCircle(vg, arm.getCenter().x, arm.getCenter().y, compact() ? 3 : 3.5f);
    if (t.armed) {
        nvgFillColor(vg, c.record);
        nvgFill(vg);
    } else {
        nvgStrokeColor(vg, armHover ? c.record : nvgTransRGBAf(c.text2, 0.7f));
        nvgStrokeWidth(vg, 1.2f);
        nvgStroke(vg);
    }
    Rect lanes = automationRect(top, h);
    bool lanesOpen = ctx.view().openAutomation.count(t.id.value) > 0;
    bool lanesHover = hovered && hover.part == Part::Automation;
    if (lanesHover)
        draw::roundedRect(vg, lanes, 3, c.line0);
    NVGcolor arrow = lanesHover ? c.text0 : !t.automation.empty() ? c.text1 : nvgTransRGBAf(c.text2, 0.55f);
    drawFoldArrow(vg, lanes.getCenter().x - (lanesOpen ? 3 : 2), lanes.getCenter().y, !lanesOpen, arrow);

    drawSmallButton(vg, muteRect(top, h), "M", t.mute, hovered && hover.part == Part::Mute, c.queue);
    drawSmallButton(vg, soloRect(top, h), "S", t.solo, hovered && hover.part == Part::Solo, c.select);

    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextUi, focus ? c.text0 : c.text1,
                         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    Rect nameBox = nameRect(index);
    draw::text(vg, name, nameBox.pos.x, y, draw::ellipsize(vg, name, t.name, nameBox.size.x));

    // Arranger mode, but a launched clip has taken this track over: say why its timeline is silent.
    if (h >= 44 && m->playMode() == PlayMode::Arranger && m->launcherStatus(t.id).overridden) {
        draw::TextStyle detail{theme::fonts::ui(), theme::metrics::kTextRuler, c.queue,
                               NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        float x = indent + tileW + kNameGap;
        draw::text(vg, detail, x, top + kRowTopPad + kButtonH + 12,
                   draw::ellipsize(vg, detail, "Launcher clip playing  ·  \"Back to timeline\" resumes it",
                                   box.size.x - x - kPad));
    }

    nvgBeginPath(vg);
    nvgRect(vg, 0, top + h - 1, box.size.x, 1);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);
}

} // namespace tessera
