#include "Arranger.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../editing/RecordActions.hpp"
#include "../editing/SendActions.hpp"
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
constexpr float kFoldedTimeline = 48; // what is left of the timeline while the clips take the width
constexpr float kEdgeTabWidth = 14;    // the clips' edge handle: widen / narrow (§190)
constexpr float kEdgeTabHeight = 20;
constexpr float kTimelineTabArrow = 16; // the folded timeline tab's arrow, this far under the ruler's height
constexpr float kTimelineTabArrowMin = 66; // and never higher: clear of two stacked buttons
constexpr float kQuietAlpha = 0.45f;  // the section that doesn't play

/// A chevron pointing left (`left`) or right, about 8 px tall, centred on `c`.
void drawChevron(NVGcontext* vg, rack::math::Vec c, bool left, NVGcolor color) {
    float d = left ? 1.f : -1.f;
    nvgBeginPath(vg);
    nvgMoveTo(vg, c.x + d * 2, c.y - 4);
    nvgLineTo(vg, c.x - d * 2, c.y);
    nvgLineTo(vg, c.x + d * 2, c.y + 4);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 1.5f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
}

} // namespace

// ---- ArrangerView -------------------------------------------------------------------------------------------

bool ArrangerView::timelineFolded() const { return timelineTab->visible; }

rack::math::Rect ArrangerView::maximizeRect() const { return maximizeButton->box; }

ArrangerView::ArrangerView(EditorContext& ctx) : EditorPane(ctx, Pane::Top), source(ctx) {
    headers = new TrackHeaders(ctx, *this);
    addChild(headers);

    EditorContext* c = &ctx;
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
        clipActions::appendNextActionsSwitch(menu, *c);
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
    // Widened with more scenes than fit, the timeline is this tab, top to bottom: a click brings it back.
    timelineTab = new PaintedButton(withShortcut("Timeline: bring it back beside the clips", Action::WidenClips),
                                    [c] { c->state().clipsWide = false; },
                                    [this](NVGcontext* vg, rack::math::Vec size, bool hot) {
                                        drawTimelineTab(vg, size, hot);
                                    });
    timelineTab->visible = false;
    addChild(timelineTab);
    maximizeButton = makeMaximizeButton(ctx, Pane::Top);
    addChild(maximizeButton);

    // The clips' edge handle (docs/DECISIONS.md §190): › widens the clips, ‹ puts them back. Over everything else.
    edgeTab = new PaintedButton(
        withShortcut("Widen the clips across the song pane, or put them back beside the timeline", Action::WidenClips),
        [c] {
            c->state().clipsWide = !c->state().clipsWide;
            c->state().focus = Pane::Top;
        },
        [c](NVGcontext* vg, rack::math::Vec size, bool hot) {
            const theme::Palette& p = theme::colors();
            rack::math::Rect r(0, 0, size.x, size.y);
            draw::roundedRect(vg, r, theme::metrics::kRadiusControl, hot ? p.line2 : p.bg2);
            draw::roundedRectStroke(vg, r.shrink(rack::math::Vec(0.5f, 0.5f)), theme::metrics::kRadiusControl,
                                    p.line2, 1);
            drawChevron(vg, r.getCenter(), c->module() && c->state().clipsWide, hot ? p.text0 : p.text1);
        });
    edgeTab->visible = false;
    addChild(edgeTab);
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
    float most = ctx.state().clipsWide ? room - kFoldedTimeline : room * 0.5f;
    // The saved width is the scenes' room; the back column comes on top, so old patches show the scenes they did.
    float wanted =
        ctx.state().clipsWide ? panel->contentWidth() : ctx.view().launcherPanelWidth + panel->backColumn();
    // Widened, the clips take the room their scenes need and the timeline keeps the rest: a next action can send a
    // track back to it (docs/DECISIONS.md §190). With more scenes than fit, the timeline folds to its strip.
    float panelWidth = std::max(0.f, std::min({wanted, panel->contentWidth(), most}));
    if (foldTab->visible && !ctx.clipsOpenNarrow && !ctx.state().clipsWide)
        panelWidth = 0;
    panel->visible = panelWidth > 0;
    panel->box = rack::math::Rect(headerWidth, 0, panelWidth, box.size.y);
    panel->rowsTop = rulerHeight;
    panel->headerTop = std::max(0.f, rulerHeight - kSceneButtonsHeight);
    foldTab->box = rack::math::Rect(headerWidth + panelWidth, 0, tab, box.size.y);
    float timeline = headerWidth + panelWidth + tab;
    ruler->box = rack::math::Rect(timeline, 0, box.size.x - timeline, rulerHeight);
    // Widened past the room, the timeline is a tab from top to bottom; the pane's maximise sits at its top.
    float stripWidth = box.size.x - timeline;
    timelineTab->visible = ctx.state().clipsWide && stripWidth <= kFoldedTimeline + 1;
    timelineTab->box = rack::math::Rect(timeline, 0, stripWidth, box.size.y);
    maximizeButton->box.pos =
        timelineTab->visible
            ? rack::math::Vec(timeline + (stripWidth - kMaximizeButton) / 2, (rulerHeight - kMaximizeButton) / 2)
            : rack::math::Vec(box.size.x - kMaximizeButton - 3, (rulerHeight - kMaximizeButton) / 2);
    // The handle sits on the clips' right edge, in the scene buttons' row, half over each side.
    float tabHeight = std::min(kEdgeTabHeight, rulerHeight - panel->headerTop - 2);
    // Only while widening shows more than now (or narrowing goes back): with every scene in view it would do nothing.
    float normalWidth = std::min({ctx.view().launcherPanelWidth + panel->backColumn(), panel->contentWidth(),
                                  std::max(0.f, room) * 0.5f});
    bool more = ctx.state().clipsWide || panel->contentWidth() > normalWidth + 1;
    edgeTab->visible = panel->visible && tabHeight >= 10 && more;
    edgeTab->box = rack::math::Rect(panel->box.getRight() - kEdgeTabWidth / 2,
                                    panel->headerTop + (rulerHeight - panel->headerTop - tabHeight) / 2, kEdgeTabWidth,
                                    tabHeight);
    lanes->box = rack::math::Rect(timeline, rulerHeight, box.size.x - timeline, box.size.y - rulerHeight);
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
    auto flag = [c, menu](std::string text, bool EditorState::* field, std::string right = "") {
        menu->addChild(rack::createCheckMenuItem(
            text, right, [c, field] { return c->module() && c->state().*field; },
            [c, field] { c->state().*field = !(c->state().*field); }));
    };
    flag("Ripple: deleting clips or time closes the gap", &EditorState::ripple);
    // Audio tracks never stack: there an overlap is a crossfade, whatever this says.
    flag("Stack: overlapping clips play together", &EditorState::stackClips, "audio crossfades");
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
    clipActions::appendNextActionsSwitch(menu, *c);
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
    EditorState& state = ctx.state();
    const Song& song = ctx.module()->song();
    // Only tracks that make sound have parts: they are what the mixer shows strips for.
    mix.parts.clear();
    for (const Track& t : song.tracks)
        if (ctx.module()->soundIndex(t.id) >= 0 || t.type == TrackType::Audio) {
            std::vector<SendFrom> parts = sendActions::sendingParts(*ctx.module(), t);
            if (!parts.empty())
                mix.parts[t.id.value] = std::move(parts);
        }
    // A patch saved before §201 kept tracks' lanes open in openAutomation: they open the track's one arrow now.
    for (auto it = state.openAutomation.begin(); it != state.openAutomation.end();) {
        if (*it == song.master.id.value) {
            ++it;
            continue;
        }
        state.openMix.insert({*it, 0});
        it = state.openAutomation.erase(it);
    }
    mix.open = state.openMix;
    rows = buildTrackRows(song, state.foldedGroups, state.openAutomation, headers->showsMaster(), &mix);
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
        case TrackRow::Kind::Mix:
            heights.push_back(kMixRowHeight);
            break;
        case TrackRow::Kind::Master:
            heights.push_back(TrackHeaders::kMasterRowHeight + 6);
            break;
        default: {
            // Stacked clips need room: the track grows for as long as they overlap, without changing its setting.
            const Track* t = ctx.module() ? ctx.module()->song().findTrack(row.track) : nullptr;
            int stacks = arrange::stackOf(lanes->committed(), row.track, t && t->type == TrackType::Audio).rows;
            // A track with a level line opens tall enough to show it. A height the user set stays.
            double shown = state.trackHeight(row.track);
            bool level = t && ctx.module()->soundIndex(t->id) >= 0;
            if (level && !state.trackHeights.count(t->id.value))
                shown = std::max(shown, double(theme::metrics::kLevelLineMinHeight));
            // A track that plays another track's notes opens tall enough to say so under its name (§198).
            bool listens = t && (t->listen == ListenSource::TrackAfter || t->listen == ListenSource::TrackBefore);
            if (listens && !state.trackHeights.count(t->id.value))
                shown = std::max(shown, double(theme::metrics::kRowFromMinHeight(level)));
            heights.push_back(std::max(shown, stacks > 1 ? stacks * kMinStackRow : 0.0));
            break;
        }
        }
    }
    syncedHeights = heights;
    tracks.setViewHeight(lanes->box.size.y);
    // Room to scroll the "+ Add track" row, after everything, into view.
    tracks.setFooter(36);
    tracks.setHeights(std::move(heights));
    tracks.scrollTo(state.trackScroll);

    if (!restored) {
        restored = true;
        if (state.arranger.pxPerTick > 0)
            viewport.time.restore(state.arranger);
        else {
            viewport.time.showRange(0, double(source.fitEnd()));
            // showRange centres what doesn't fit, but a new timeline always opens on bar 1.
            viewport.time.restore({0, viewport.time.pxPerTick()});
        }
    }
}

void ArrangerView::writeHeights() {
    for (size_t i = 0; i < rows.size() && i < tracks.heights().size(); i++) {
        if (i < syncedHeights.size() && tracks.heights()[i] == syncedHeights[i])
            continue; // not resized: keep the stored height (it may be smaller than a stacked track's)
        if (rows[i].kind == TrackRow::Kind::Track)
            ctx.state().trackHeights[rows[i].track.value] = tracks.heights()[i];
        else if (rows[i].kind == TrackRow::Kind::Lane)
            ctx.state().laneHeights[rows[i].lane.value] = tracks.heights()[i];
    }
}

void ArrangerView::zoomTracksAt(double y, double factor) {
    int row = tracks.trackAt(y);
    double fraction = row >= 0 ? (y - tracks.trackTop(row)) / tracks.trackHeight(row) : 0;
    tracks.zoomAt(y, factor);
    writeHeights();
    // Group and "add lane" rows keep their size and stacked tracks their minimum, so the rows are rebuilt from
    // the saved heights before the spot is put back under the mouse.
    syncFromState();
    if (row >= 0 && row < int(tracks.heights().size()))
        tracks.anchorScroll(tracks.scroll() + tracks.trackTop(row) + fraction * tracks.trackHeight(row) - y);
    ctx.state().trackScroll = tracks.scroll();
}

void ArrangerView::saveToState() {
    EditorState& state = ctx.state();
    if (ctx.drivesView()) {
        state.arranger = viewport.time.state();
        state.trackScroll = tracks.scroll();
    } else if (state.arranger.pxPerTick > 0) {
        viewport.time.restore(state.arranger);
    }
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

        if (ctx.drivesView() && m->playing() && !ctx.followPaused())
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
    if (!ctx.drivesView())
        return;
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

void ArrangerView::drawTimelineTab(NVGcontext* vg, rack::math::Vec size, bool hot) {
    // A plain tab: the timeline squeezed this narrow says nothing; an arrow, the word and a click say enough.
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, size.x, size.y);
    nvgFillColor(vg, hot ? c.bg2 : c.bg1);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, 1, size.y);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);
    // Under the ruler's height, and under the buttons stacked at the tab's top (two in the panel editor).
    float top = std::max(Ruler::heightFor(source, ctx.surface) + kTimelineTabArrow, kTimelineTabArrowMin);
    drawChevron(vg, rack::math::Vec(size.x / 2, top), true, hot ? c.text0 : c.text1);
    nvgSave(vg);
    nvgTranslate(vg, size.x / 2, top + 50);
    nvgRotate(vg, -float(M_PI) / 2);
    draw::TextStyle word{theme::fonts::uiStrong(), 9, hot ? c.text0 : c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, word, 0, 0, "TIMELINE");
    nvgRestore(vg);
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
    // tracks over from the timeline stay lit; on the timeline, the rows of tracks sent back to it stay lit
    // (docs/DECISIONS.md §187).
    if (clips) {
        float x = lanes->box.pos.x, w = lanes->box.size.x, lanesTop = lanes->box.pos.y;
        auto shade = [&](float y0, float y1) {
            y0 = std::max(y0, ruler->box.pos.y);
            if (y1 <= y0)
                return;
            nvgBeginPath(vg);
            nvgRect(vg, x, y0, w, y1 - y0);
            nvgFillColor(vg, nvgTransRGBAf(c.bg0, kQuietAlpha));
            nvgFill(vg);
        };
        bool anyOnTimeline = false;
        float y = lanesTop;
        for (size_t i = 0; i < rows.size() && i < tracks.heights().size(); i++) {
            float top = lanesTop + float(tracks.trackTop(int(i)));
            float bottom = top + float(tracks.trackHeight(int(i)));
            const TrackRow& row = rows[i];
            bool lit = (row.isTrack() || row.isLane()) && row.track.valid() && m->launcherStatus(row.track).onTimeline;
            anyOnTimeline |= lit;
            if (!lit)
                shade(std::max(top, lanesTop), bottom);
            y = std::max(y, bottom);
        }
        shade(std::max(y, lanesTop), box.size.y);
        if (!anyOnTimeline)
            shade(ruler->box.pos.y, lanesTop); // the song position matters only to a track that plays the timeline
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
        ctx.state().trackScroll = tracks.scroll();
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
