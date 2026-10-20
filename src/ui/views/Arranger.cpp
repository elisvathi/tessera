#include "Arranger.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../editing/RecordActions.hpp"
#include "../editing/TrackActions.hpp"
#include "../widgets/CachedLayer.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/EditTools.hpp"
#include "../widgets/ToolButton.hpp"
#include "ArrangerLanes.hpp"
#include "LauncherPanel.hpp"
#include "TimelineDraw.hpp"

#include <cmath>

namespace tessera {

namespace {

constexpr double kMinPxPerBar = 8; // max zoom-out: 8 px per bar (EDITOR_UX §3)
constexpr float kMinViewSize = 40;
constexpr float kToolSize = 18;
constexpr float kToolSizePanel = 16;
constexpr float kSceneButtonsHeight = 24; // the scene buttons sit at the bottom of the ruler's height
constexpr float kQuantizeWidth = 84;
constexpr float kNarrowWidth = 600; // narrower song panes fold the clips while the timeline plays
constexpr float kFoldTabWidth = 14;
constexpr float kBackWidth = 104;
constexpr float kFoldedTimeline = 48; // what is left of the timeline while the clips take the width
constexpr float kQuietAlpha = 0.45f;  // the section that doesn't play

} // namespace

// ---- ArrangerView -------------------------------------------------------------------------------------------

ArrangerView::ArrangerView(EditorContext& ctx) : EditorPane(ctx, Pane::Top), source(ctx) {
    headers = new TrackHeaders(ctx, *this);
    addChild(headers);

    EditorContext* c = &ctx;
    ToolButton::Options back;
    back.text = "Back to timeline";
    back.tooltip = "Back to timeline: you launched clips while the timeline plays, so those tracks left it. Click to "
                   "make them play the timeline again";
    back.action = [c] { c->module()->backToArrangement(); };
    back.active = [] { return true; };
    backButton = new ToolButton(back);
    backButton->visible = false;
    addChild(backButton);
    ruler = new Ruler(ctx, viewport, source);
    addChild(ruler);
    lanes = new ArrangerLanes(ctx, *this);
    addChild(lanes);
    panel = new LauncherPanel(ctx, *this);
    addChild(panel);

    toolSwitch = makeToolSwitch(
        ctx, {Tool::Erase, Tool::Slice, Tool::Mute}, [this](rack::ui::Menu* menu) { appendSongMenu(menu); },
        [c] { return c->module() && c->state().rangeTool ? Icon::Range : Icon::None; }, nullptr,
        [this] { return target == Target::Clips && !lanes->selection().empty() && lanes->clipAction(Action::Delete); });
    for (ToolButton* b : {toolSwitch.select, toolSwitch.draw, toolSwitch.more})
        addChild(b);

    ToolButton::Options quantize;
    quantize.tooltip = "Launch quantize: launched clips start on the next line of this grid. Red while the clips are "
                       "recorded into the timeline";
    quantize.action = [c] {
        rack::ui::Menu* menu = rack::createMenu();
        clipActions::appendQuantizeMenu(menu, *c);
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createCheckMenuItem(
            "Record into timeline", "", [c] { return c->module() && c->module()->recordingArrangement(); },
            [c] {
                if (TesseraModule* m = c->module())
                    m->setRecordArrangement(!m->recordingArrangement());
            }));
    };
    quantize.caret = true;
    quantize.activeColor = &theme::Palette::record;
    quantize.active = [c] { return c->module() && c->module()->recordingArrangement(); };
    quantizeButton = new ToolButton(quantize);

    ToolButton::Options stop;
    stop.icon = Icon::Stop;
    stop.tooltip = "Stop all clips (at the next launch-quantize line)";
    stop.action = [c] {
        if (TesseraModule* m = c->module())
            m->stopAllClips();
    };
    stopAllButton = new ToolButton(stop);
    quantizeButton->box.size = rack::math::Vec(kQuantizeWidth, kToolSize);
    stopAllButton->box.size = rack::math::Vec(kToolSize, kToolSize);
    addChild(quantizeButton);
    addChild(stopAllButton);

    ToolButton::Options fold;
    fold.tooltip = "Clips: open or fold them. A narrow song pane folds them while the timeline plays";
    fold.action = [c] { c->clipsOpenNarrow = !c->clipsOpenNarrow; };
    foldTab = new ToolButton(fold);
    foldTab->visible = false;
    addChild(foldTab);
    maximizeButton = makeMaximizeButton(ctx, Pane::Top);
    addChild(maximizeButton);
}

LauncherPanel* ArrangerView::panelView() { return panel->visible ? panel : nullptr; }

void ArrangerView::layout() {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    float headerWidth = ctx.view().headerWidth;
    float rulerHeight = Ruler::heightFor(source, ctx.surface);
    headers->box = rack::math::Rect(0, rulerHeight, headerWidth, box.size.y - rulerHeight);
    // The clips sit between the headers and the timeline, taking at most half the room. Widened (F2) they take all
    // of it but a strip of timeline. A narrow pane folds them to a tab while the timeline plays.
    bool timelinePlays = m->playMode() == PlayMode::Arranger;
    bool narrow = box.size.x < kNarrowWidth;
    foldTab->visible = narrow && timelinePlays;
    float tab = foldTab->visible ? kFoldTabWidth : 0;
    float room = std::max(0.f, box.size.x - headerWidth - tab);
    float most = ctx.view().clipsWide ? room - kFoldedTimeline : room * 0.5f;
    float wanted = ctx.view().clipsWide ? panel->contentWidth() : ctx.view().launcherPanelWidth;
    float panelWidth = std::max(0.f, std::min({wanted, panel->contentWidth(), most}));
    if (foldTab->visible && !ctx.clipsOpenNarrow)
        panelWidth = 0;
    panel->visible = panelWidth > 0;
    panel->box = rack::math::Rect(headerWidth, 0, panelWidth, box.size.y);
    panel->rowsTop = rulerHeight;
    panel->headerTop = std::max(0.f, rulerHeight - kSceneButtonsHeight);
    foldTab->box = rack::math::Rect(headerWidth + panelWidth, 0, tab, box.size.y);
    maximizeButton->box.pos = rack::math::Vec(box.size.x - kMaximizeButton - 3, (rulerHeight - kMaximizeButton) / 2);
    float timeline = headerWidth + panelWidth + tab;
    ruler->box = rack::math::Rect(timeline, 0, box.size.x - timeline, rulerHeight);
    lanes->box = rack::math::Rect(timeline, rulerHeight, box.size.x - timeline, box.size.y - rulerHeight);
    // "Back to timeline" sits on the timeline's ruler, which it is about.
    backButton->visible = m->playMode() == PlayMode::Arranger && m->anyTrackOverridden();
    backButton->box = rack::math::Rect(timeline + 4, (rulerHeight - kToolSize) / 2,
                                       std::min(kBackWidth, std::max(0.f, ruler->box.size.x - 40)), kToolSize);
    layoutCorner(rulerHeight);
}

void ArrangerView::layoutCorner(float rulerHeight) {
    float headerWidth = ctx.view().headerWidth;
    // The in-panel editor's corner also holds the transport, so its tools are a little smaller.
    float tool = ctx.surface == Surface::Panel ? kToolSizePanel : kToolSize;
    float y = (rulerHeight - tool) / 2;
    // After the in-panel editor's transport, a gap and a divider (drawCornerLabels) keep the two groups apart.
    float x = cornerInset + (cornerInset > 0 ? 7 : theme::metrics::kSpacing);
    float caret = toolSwitchCaretWidth(toolSwitch, tool);
    toolSwitch.select->box = rack::math::Rect(x, y, tool, tool);
    toolSwitch.draw->box = rack::math::Rect(x + tool + 1, y, tool, tool);
    toolSwitch.more->box = rack::math::Rect(x + 2 * tool + 2, y, caret, tool);
    float switchRight = toolSwitch.more->box.getRight();
    for (ToolButton* b : {toolSwitch.select, toolSwitch.draw, toolSwitch.more})
        b->visible = b->box.getRight() <= headerWidth - 2;

    float right = headerWidth - 6;
    stopAllButton->box.pos = rack::math::Vec(right - kToolSize, y);
    quantizeButton->box.pos = rack::math::Vec(stopAllButton->box.pos.x - 2 - kQuantizeWidth, y);
    bool clipsFit = quantizeButton->box.pos.x >= switchRight + 6;
    quantizeButton->visible = clipsFit;
    stopAllButton->visible = clipsFit;

    float tagRight = clipsFit ? quantizeButton->box.pos.x - 6 : right;
    rippleTagX = ctx.module() && ctx.state().ripple && tagRight - switchRight >= 46 ? switchRight + 6 : -1;
}

void ArrangerView::appendSongMenu(rack::ui::Menu* menu) {
    EditorContext* c = &ctx;
    menu->addChild(rack::createCheckMenuItem(
        "Range: select time across tracks", shortcutLabel(Action::ToolRange),
        [c] { return c->module() && c->state().rangeTool; }, [c] { c->state().rangeTool = !c->state().rangeTool; }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Timeline"));
    auto flag = [c, menu](std::string text, bool EditorState::* field) {
        menu->addChild(rack::createCheckMenuItem(
            text, "", [c, field] { return c->module() && c->state().*field; },
            [c, field] { c->state().*field = !(c->state().*field); }));
    };
    flag("Ripple: deleting clips or time closes the gap", &EditorState::ripple);
    flag("Stack: overlapping clips play together", &EditorState::stackClips);
    flag("Automation follows clips", &EditorState::automationFollowsClips);
    if (quantizeButton->visible)
        return;
    // The corner has no room for the clips' own controls (the in-panel editor): they live here.
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Clips"));
    std::string quantize = c->module() ? clipActions::quantizeLabel(c->module()->song().launchQuantize) : "";
    menu->addChild(rack::createSubmenuItem("Launch quantize", quantize,
                                           [c](rack::ui::Menu* sub) { clipActions::appendQuantizeMenu(sub, *c); }));
    menu->addChild(rack::createMenuItem("Stop all clips", "", [c] {
        if (TesseraModule* m = c->module())
            m->stopAllClips();
    }));
    menu->addChild(rack::createCheckMenuItem(
        "Record into timeline", "", [c] { return c->module() && c->module()->recordingArrangement(); },
        [c] {
            if (TesseraModule* m = c->module())
                m->setRecordArrangement(!m->recordingArrangement());
        }));
}

void ArrangerView::applyZoomLimits() {
    Tick bar = ticksPerBar(source.timeSig());
    double width = std::max(1.f, lanes->box.size.x);
    viewport.time.setViewWidth(width);
    // From 8 px per bar out to one bar filling the view (EDITOR_UX §3).
    viewport.time.setZoomLimits(kMinPxPerBar / double(bar), width / double(bar));
    viewport.time.setContentEnd(source.contentEnd());
}

void ArrangerView::syncFromState() {
    ViewState& state = ctx.view();
    const Song& song = ctx.module()->song();
    rows = buildTrackRows(song, state.foldedGroups, state.openAutomation);
    std::vector<double> heights;
    heights.reserve(rows.size());
    for (const TrackRow& row : rows) {
        switch (row.kind) {
        case TrackRow::Kind::Group:
            heights.push_back(kGroupRowHeight);
            break;
        case TrackRow::Kind::Lane:
            heights.push_back(state.laneHeight(row.lane));
            break;
        case TrackRow::Kind::AddLane:
            heights.push_back(kAddLaneRowHeight);
            break;
        default: {
            // Stacked clips need room: the track grows for as long as they overlap, without changing its setting.
            int stacks = arrange::stackOf(lanes->committed(), row.track).rows;
            heights.push_back(std::max(state.trackHeight(row.track), stacks > 1 ? stacks * kMinStackRow : 0.0));
            break;
        }
        }
    }
    syncedHeights = heights;
    tracks.setViewHeight(lanes->box.size.y);
    tracks.setFooter(36); // room to scroll the "+ Add track" row into view
    tracks.setHeights(std::move(heights));
    tracks.scrollTo(state.trackScroll);

    if (!restored) {
        restored = true;
        if (state.arranger.pxPerTick > 0)
            viewport.time.restore(state.arranger);
        else
            viewport.time.showRange(0, double(source.fitEnd()));
    }
}

void ArrangerView::writeHeights() {
    for (size_t i = 0; i < rows.size() && i < tracks.heights().size(); i++) {
        if (i < syncedHeights.size() && tracks.heights()[i] == syncedHeights[i])
            continue; // not resized: keep the stored height (it may be smaller than a stacked track's)
        if (rows[i].kind == TrackRow::Kind::Track)
            ctx.view().trackHeights[rows[i].track.value] = tracks.heights()[i];
        else if (rows[i].kind == TrackRow::Kind::Lane)
            ctx.view().laneHeights[rows[i].lane.value] = tracks.heights()[i];
    }
}

void ArrangerView::saveToState() {
    ViewState& state = ctx.view();
    state.arranger = viewport.time.state();
    state.trackScroll = tracks.scroll();
}

void ArrangerView::step() {
    TesseraModule* m = ctx.module();
    if (m) {
        layout();
        if (lanes->box.size.x < kMinViewSize || lanes->box.size.y < kMinViewSize) {
            // Not really on screen (hidden or collapsed): don't fit or follow to a size nobody sees.
            EditorPane::step();
            return;
        }
        quantizeButton->setText("Launch " + clipActions::quantizeLabel(m->song().launchQuantize));
        applyZoomLimits();
        syncFromState();
        revealSelectedTrack();

        double now = EditorContext::now();
        viewport.time.step(lastStep > 0 ? now - lastStep : 0);
        lastStep = now;

        if (m->playing() && !ctx.followPaused())
            if (auto left = followScroll(ctx.state().follow, source.playhead(), viewport.time))
                viewport.time.scrollTo(*left);
        saveToState();
    }
    EditorPane::step();
}

void ArrangerView::revealSelectedTrack() {
    TrackId selected = ctx.state().selectedTrack;
    if (selected == revealedTrack)
        return;
    revealedTrack = selected;
    int row = lanes->rowOf(selected);
    if (row < 0)
        return;
    double top = tracks.trackTop(row);
    double bottom = top + tracks.trackHeight(row);
    if (top < 0)
        tracks.scrollByPx(top);
    else if (bottom > double(lanes->box.size.y))
        tracks.scrollByPx(std::min(top, bottom - double(lanes->box.size.y)));
}

void ArrangerView::draw(const DrawArgs& args) {
    const theme::Palette& c = theme::colors();
    NVGcontext* vg = args.vg;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);

    EditorPane::draw(args);
    drawPlayingSection(vg);
    drawCornerLabels(vg);
}

void ArrangerView::drawCornerLabels(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    if (cornerInset > 0) {
        float y = toolSwitch.select->box.pos.y;
        nvgBeginPath(vg);
        nvgRect(vg, cornerInset + 3, y + 3, 1, toolSwitch.select->box.size.y - 6);
        nvgFillColor(vg, c.line2);
        nvgFill(vg);
    }
    if (rippleTagX >= 0) {
        draw::TextStyle tag{theme::fonts::uiStrong(), 9, c.queue, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, tag, rippleTagX, toolSwitch.select->box.getCenter().y, "RIPPLE");
    }
    if (!foldTab->visible)
        return;
    rack::math::Rect r = foldTab->box;
    nvgBeginPath(vg);
    nvgRect(vg, r.getRight() - 1, r.pos.y, 1, r.size.y);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);
    // The word reads bottom to top, like a book's spine.
    nvgSave(vg);
    nvgTranslate(vg, r.getCenter().x, r.pos.y + std::min(r.size.y / 2, 60.f));
    nvgRotate(vg, -float(M_PI) / 2);
    draw::TextStyle word{theme::fonts::uiStrong(), 9, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, word, 0, 0, ctx.clipsOpenNarrow ? "FOLD CLIPS" : "CLIPS");
    nvgRestore(vg);
}

void ArrangerView::drawPlayingSection(NVGcontext* vg) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    const theme::Palette& c = theme::colors();
    bool clips = m->playMode() == PlayMode::Launcher;
    if (clips && !panel->visible)
        return;
    // The quiet section steps back. The clips dim slot by slot instead (LauncherPanel), so the clips that took
    // tracks over from the timeline stay lit.
    if (clips) {
        nvgBeginPath(vg);
        nvgRect(vg, lanes->box.pos.x, ruler->box.pos.y, lanes->box.size.x, box.size.y);
        nvgFillColor(vg, nvgTransRGBAf(c.bg0, kQuietAlpha));
        nvgFill(vg);
    }
    rack::math::Rect section =
        clips ? panel->box : rack::math::Rect(lanes->box.pos.x, 0, lanes->box.size.x, box.size.y);
    // Drawn while stopped too, fainter: the switch says what will play.
    draw::roundedRectStroke(vg, section.grow(rack::math::Vec(-1, -1)), theme::metrics::kRadiusControl,
                            nvgTransRGBAf(c.play, m->playing() ? 0.9f : 0.45f), 1.5f);
}

double ArrangerView::tickUnderMouse() {
    // Hidden while the launcher shows in its place.
    return visible ? lanes->mouseTick() : NAN;
}

bool ArrangerView::handleAction(Action action) {
    TesseraModule* m = ctx.module();
    if (target == Target::Clips && lanes->clipAction(action))
        return true;
    if (target == Target::Automation && lanes->automationAction(action))
        return true;
    if (target == Target::Range && lanes->rangeAction(action))
        return true;
    switch (action) {
    case Action::Delete:
        trackActions::deleteSelected(ctx);
        return true;
    case Action::Duplicate:
        trackActions::duplicateSelected(ctx);
        return true;
    case Action::SelectAll:
        for (const Track& t : m->song().tracks)
            if (std::find(ctx.state().selectedTracks.begin(), ctx.state().selectedTracks.end(), t.id) ==
                ctx.state().selectedTracks.end())
                ctx.state().selectedTracks.push_back(t.id);
        return true;
    case Action::FitAllTracks:
        tracks.fitAll();
        writeHeights();
        ctx.view().trackScroll = tracks.scroll();
        return true;
    case Action::GoToStart:
        source.seek(0, false);
        viewport.time.scrollTo(0);
        return true;
    case Action::GoToEnd: {
        double end = double(source.songEnd());
        source.seek(end, false);
        viewport.time.scrollTo(end - viewport.time.visibleTicks() * 0.8);
        return true;
    }
    case Action::AddMarker:
        source.addMarker(Tick(snapTick(m->playheadTicks(), ruler->snapStep())));
        return true;
    default:
        return lanes->handleAction(action);
    }
}

} // namespace tessera
