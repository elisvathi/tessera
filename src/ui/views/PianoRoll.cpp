#include "PianoRoll.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../editing/ExportMidi.hpp"
#include "../editing/KeyActions.hpp"
#include "../editing/NoteClipboard.hpp"
#include "../editing/TrackActions.hpp"
#include "../editing/TransformMenu.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/EditTools.hpp"
#include "../widgets/IconButton.hpp"
#include "../widgets/ToolButton.hpp"
#include "ClipLaneView.hpp"
#include "ExpressionLane.hpp"
#include "LaneTabs.hpp"
#include "NoteGrid.hpp"
#include "NoteValueLane.hpp"
#include "PianoKeyboard.hpp"
#include "TimelineDraw.hpp"
#include "TrackHeaders.hpp"

#include <cmath>

namespace tessera {

using edits::IdSet;
using edits::NoteList;

namespace {

constexpr double kMinPxPerBeat = 2;
constexpr float kMinViewSize = 40; // below this the pane isn't really on screen yet
constexpr float kAuditionSeconds = 0.25f;
constexpr float kVelocityStep = 0.05f;
constexpr float kVelocityFineStep = 0.01f;
constexpr float kToolButton = 22;
constexpr float kSnapButton = 108;
constexpr float kSnapButtonShort = 74;
constexpr float kKeyButton = 100;
constexpr float kKeyButtonShort = 52;
constexpr float kNotesButton = 64;
constexpr float kViewButton = 56;
constexpr float kLaneHeightOverlay = 72;
constexpr float kLaneHeightPanel = 44;

/// The snap grid's choices, for the Snap chip's menu or the narrow header's "⋯".
void appendSnapItems(rack::ui::Menu* menu, EditorContext* c) {
    auto set = [c](std::function<void(SnapSetting&)> change) {
        return [c, change] {
            if (c->module())
                change(c->state().snap);
        };
    };
    auto snap = [c]() -> const SnapSetting& { return c->state().snap; };
    menu->addChild(rack::createCheckMenuItem(
        "Off", shortcutLabel(Action::SnapToggle), [snap] { return !snap().enabled; },
        set([](SnapSetting& s) { s.enabled = !s.enabled; })));
    menu->addChild(rack::createCheckMenuItem(
        "Adaptive", "", [snap] { return snap().enabled && snap().adaptive; }, set([](SnapSetting& s) {
            s.enabled = true;
            s.adaptive = true;
        })));
    for (int d = 0; d <= int(GridDivision::SixtyFourth); d++) {
        GridDivision division = GridDivision(d);
        menu->addChild(rack::createCheckMenuItem(
            divisionLabel(division, GridFeel::Straight), "",
            [snap, division] { return snap().enabled && !snap().adaptive && snap().division == division; },
            set([division](SnapSetting& s) {
                s.enabled = true;
                s.adaptive = false;
                s.division = division;
            })));
    }
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createCheckMenuItem(
        "Triplet", shortcutLabel(Action::GridTriplet), [snap] { return snap().feel == GridFeel::Triplet; },
        set([](SnapSetting& s) { s.feel = s.feel == GridFeel::Triplet ? GridFeel::Straight : GridFeel::Triplet; })));
    menu->addChild(rack::createCheckMenuItem(
        "Dotted", "", [snap] { return snap().feel == GridFeel::Dotted; },
        set([](SnapSetting& s) { s.feel = s.feel == GridFeel::Dotted ? GridFeel::Straight : GridFeel::Dotted; })));
    menu->addChild(rack::createCheckMenuItem(
        "Snap to other notes", "", [snap] { return snap().events; },
        set([](SnapSetting& s) { s.events = !s.events; })));
}

void openSnapMenu(EditorContext* c) {
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Snap"));
    appendSnapItems(menu, c);
}

} // namespace

bool isBlackKey(int pitch) {
    switch (((pitch % 12) + 12) % 12) {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

std::string pitchName(int pitch) { return std::string(pitchClassName(pitch)) + std::to_string(pitch / 12 - 1); }

namespace {

std::optional<expr::Curve> curveOf(NoteLane lane) {
    switch (lane) {
    case NoteLane::Pitch:
        return expr::Curve::Pitch;
    case NoteLane::Timbre:
        return expr::Curve::Timbre;
    case NoteLane::Pressure:
        return expr::Curve::Pressure;
    default:
        return std::nullopt;
    }
}

fields::Field fieldOf(NoteLane lane) {
    switch (lane) {
    case NoteLane::Release:
        return fields::Field::Release;
    case NoteLane::Chance:
        return fields::Field::Chance;
    case NoteLane::VelocitySpread:
        return fields::Field::VelocitySpread;
    case NoteLane::Repeats:
        return fields::Field::Repeats;
    case NoteLane::Micro:
        return fields::Field::Micro;
    default:
        return fields::Field::Velocity;
    }
}

std::string noteLaneName(NoteLane lane) {
    if (std::optional<expr::Curve> curve = curveOf(lane))
        return expr::info(*curve).name;
    return fields::info(fieldOf(lane)).name;
}

} // namespace

std::optional<expr::Curve> PianoRollView::stripCurve() {
    std::optional<expr::Curve> curve = shownCurve();
    if (!curve || *curve == expr::Curve::Pitch)
        return std::nullopt;
    return curve;
}

bool PianoRollView::bendMode() { return shownCurve() == expr::Curve::Pitch; }

std::optional<expr::Curve> PianoRollView::editableCurve() {
    if (std::optional<expr::Curve> strip = stripCurve())
        return strip;
    return expr::Curve::Pitch; // Select can drag the bend points of selected notes at any time
}

bool PianoRollView::pointPicked(NoteId note, expr::Curve curve, const ExprPoint& p) const {
    if (curve != pointsCurve)
        return false;
    for (const expr::PointRef& r : curvePoints)
        if (r.note == note && std::fabs(r.pos - p.pos) < 1e-5f && std::fabs(r.value - p.value) < 1e-5f)
            return true;
    return false;
}

std::optional<expr::Curve> PianoRollView::shownCurve() {
    if (!expression->visible)
        return std::nullopt;
    return expression->curve;
}

// ---- Construction and layout --------------------------------------------------------------------------------

PianoRollView::PianoRollView(EditorContext& ctx) : EditorPane(ctx, Pane::Detail), source(ctx) {
    grid = new NoteGrid(ctx, *this);
    keys = new PianoKeyboard(ctx, *this, *grid);
    ruler = new Ruler(ctx, viewport, source);
    values = new NoteValueLane(ctx, *this);
    expression = new ExpressionLane(ctx, *this);
    expression->visible = false;
    clipLane = new ClipLaneView(ctx, *this);
    clipLane->visible = false;
    addChild(grid);
    addChild(keys);
    addChild(ruler);
    addChild(values);
    addChild(expression);
    addChild(clipLane);

    EditorContext* c = &ctx;
    ToolButton::Options lower;
    lower.text = "Velocity";
    lower.caret = true;
    lower.caretBelow = true;
    lower.tooltip = "What the lower lane shows: a note value (velocity, chance...), a curve inside each note, or one "
                    "of the clip's automation lanes";
    lower.action = [this] { openLowerMenu(); };
    lowerButton = new ToolButton(lower);
    addChild(lowerButton);

    clipChip = makeClipChip(ctx, [this] { return source.track; }, [this] { return source.clip; });
    addChild(clipChip);
    devicesToggle = makeDevicesToggle(ctx);
    addChild(devicesToggle);

    tools = makeToolSwitch(
        ctx, {Tool::Paint, Tool::Erase, Tool::Slice, Tool::Mute, Tool::Velocity},
        [c](rack::ui::Menu* menu) {
            menu->addChild(new rack::ui::MenuSeparator);
            menu->addChild(rack::createSubmenuItem("Draw chords", keyActions::chordLabel(*c), [c](rack::ui::Menu* sub) {
                keyActions::appendChordItems(sub, *c);
            }));
        },
        [c] {
            if (!c->module())
                return Icon::None;
            return c->state().chord.on() ? Icon::Chord : Icon::None;
        },
        nullptr,
        [this] {
            if (selectedIds().empty())
                return false;
            handleAction(Action::Delete);
            return true;
        });
    for (ToolButton* b : {tools.select, tools.draw, tools.more})
        addChild(b);

    ToolButton::Options hear;
    hear.icon = Icon::Speaker;
    hear.tooltip = withShortcut("Hear notes while editing", Action::ToggleAudition);
    hear.action = [c] { c->state().audition = !c->state().audition; };
    hear.active = [c] { return c->module() && c->state().audition; };
    hearButton = new ToolButton(hear);
    addChild(hearButton);

    ToolButton::Options snap;
    snap.text = "Adaptive";
    snap.caret = true;
    snap.tooltip = "Snap: grid for placing and moving notes. Alt bypasses it while dragging";
    snap.action = [c] { openSnapMenu(c); };
    snapButton = new ToolButton(snap);
    addChild(snapButton);

    ToolButton::Options key;
    key.text = "No key";
    key.caret = true;
    key.tooltip = "Key: the song's, or this clip's own, and how notes follow it (highlight, snap, fold). Lit while "
                  "snap or fold is on";
    key.active = [c] { return c->module() && (c->state().scaleSnap || c->state().scaleFold); };
    key.action = [this, c] {
        if (clip())
            keyActions::openKeyPicker(*c, source.clip, true);
        else
            keyActions::openKeyPicker(*c, std::nullopt, true);
    };
    keyButton = new ToolButton(key);
    addChild(keyButton);

    ToolButton::Options notes;
    notes.text = "Notes";
    notes.caret = true;
    notes.tooltip = "Notes: quantize, humanize, strum, arpeggiate and other transforms";
    notes.action = [this] { transformMenu::open(*this); };
    notesButton = new ToolButton(notes);
    addChild(notesButton);

    ToolButton::Options view;
    view.text = "View";
    view.caret = true;
    view.tooltip = "Ghost notes, chance and repeats on notes, hearing notes while editing, following the playhead";
    view.action = [this] { appendViewItems(rack::createMenu()); };
    viewButton = new ToolButton(view);
    addChild(viewButton);

    ToolButton::Options overflow;
    overflow.icon = Icon::More;
    overflow.tooltip = "Notes (transforms) and View";
    overflow.action = [this] {
        rack::ui::Menu* menu = rack::createMenu();
        menu->addChild(
            rack::createMenuItem("Notes: quantize, humanize, strum…", "", [this] { transformMenu::open(*this); }));
        if (!snapButton->visible)
            menu->addChild(rack::createSubmenuItem("Snap", snapLabel(this->ctx.state().snap),
                                                   [this](rack::ui::Menu* sub) { appendSnapItems(sub, &this->ctx); }));
        if (!keyButton->visible) {
            const Track* t = track();
            if (t && t->type != TrackType::Drum)
                menu->addChild(rack::createMenuItem("Key: " + keyButton->getText() + "…", "", [this] {
                    if (clip())
                        keyActions::openKeyPicker(this->ctx, source.clip, true);
                    else
                        keyActions::openKeyPicker(this->ctx, std::nullopt, true);
                }));
        }
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuLabel("View"));
        appendViewItems(menu);
    };
    overflowButton = new ToolButton(overflow);
    addChild(overflowButton);

    drumSwitch = makeDrumViewSwitch(ctx);
    addChild(drumSwitch.first);
    addChild(drumSwitch.second);

    maximizeButton = makeMaximizeButton(ctx, Pane::Detail);
    addChild(maximizeButton);
}

bool PianoRollView::layoutToolbar(float header) {
    float y = (header - kToolButton) / 2;
    float right = box.size.x - 6;
    maximizeButton->box.pos = rack::math::Vec(right - kMaximizeButton, (header - kMaximizeButton) / 2);
    right -= kMaximizeButton + 6;
    right -= headerReserve;
    const Track* t = track();
    bool drum = t && t->type == TrackType::Drum;
    float limit = devicesToggle->box.getRight() + 6;
    float caret = toolSwitchCaretWidth(tools, kToolButton);

    // Words when there is room, short forms when not (design review, 13 Oct): first Notes and View merge into "⋯",
    // then the key and the snap lose their words.
    auto build = [&](bool narrow) {
        std::vector<ToolbarItem> items;
        items.push_back({hearButton, kToolButton, 2, 9});
        if (narrow) {
            items.push_back({overflowButton, kToolButton, 4, 8}); // it also holds what has no room
        } else {
            items.push_back({viewButton, kViewButton, 2, 6});
            items.push_back({notesButton, kNotesButton, 2, 6});
        }
        if (!drum)
            items.push_back({keyButton, narrow ? kKeyButtonShort : kKeyButton, 2, 5});
        items.push_back({snapButton, narrow ? kSnapButtonShort : kSnapButton, 8, 7});
        items.push_back({tools.more, caret, 1, 9});
        items.push_back({tools.draw, kToolButton, 1, 10});
        items.push_back({tools.select, kToolButton, drum ? 6.f : 0.f, 10});
        if (drum)
            items.push_back({drumSwitch.first, kDrumSwitchWidth, 0, 4});
        return items;
    };
    float wide = 0;
    for (const ToolbarItem& it : build(false))
        wide += it.width + it.gap;
    bool narrow = right - wide < limit;
    overflowButton->visible = narrow;
    viewButton->visible = notesButton->visible = !narrow;
    keyButton->visible = !drum;
    drumSwitch.first->visible = drumSwitch.second->visible = false;
    snapButton->setText(narrow ? snapLabel(ctx.state().snap) : "Snap " + snapLabel(ctx.state().snap));
    if (ctx.module()) {
        const Song& song = ctx.module()->song();
        const Clip* shown = clip();
        std::string keyText = keyActions::keyLabel(song, shown ? clipKeyRef(song, *shown) : song.scale);
        keyButton->setText(narrow ? keyActions::shortKeyLabel(keyText) : keyText);
    }
    toolbarLeft =
        fitToolbar(build(narrow), right, limit, y, kToolButton, [this](rack::widget::Widget* w, rack::math::Rect r) {
            if (w == drumSwitch.first) {
                drumSwitch.second->visible = true;
                placeDrumViewSwitch(drumSwitch, r);
            } else {
                w->box = r;
            }
        });
    return narrow;
}

void PianoRollView::layout() {
    using namespace theme::metrics;
    float header = kPaneHeader + topInset; // the device strip sits between the header and the ruler
    // A short pane gives way from the bottom up so the notes keep their room: the lower lane shrinks, then it and
    // its tabs hide, then the ruler is cut.
    float rest = std::max(0.f, box.size.y - header);
    float rulerHeight = std::min(Ruler::heightFor(source, ctx.surface), rest);
    rest -= rulerHeight;
    float lane = ctx.surface == Surface::Panel ? kLaneHeightPanel : kLaneHeightOverlay;
    float tabs = 0; // the lane's own label picks what it shows; no row of tabs
    constexpr float kMinGrid = 48, kMinLane = 28;
    if (rest - lane - tabs < kMinGrid)
        lane = std::max(0.f, rest - tabs - kMinGrid);
    bool lowerRoom = lane >= kMinLane;
    if (!lowerRoom)
        lane = tabs = 0;
    lowerButton->visible = lowerRoom;
    if (!lowerRoom)
        values->visible = expression->visible = clipLane->visible = false;
    float body = header + rulerHeight;
    float gridHeight = std::max(0.f, rest - lane - tabs);
    keys->box = rack::math::Rect(0, body, kKeyboardWidth, gridHeight);
    ruler->box = rack::math::Rect(kKeyboardWidth, header, box.size.x - kKeyboardWidth, rulerHeight);
    grid->box = rack::math::Rect(kKeyboardWidth, body, box.size.x - kKeyboardWidth, gridHeight);
    values->box = rack::math::Rect(kKeyboardWidth, body + gridHeight + tabs, box.size.x - kKeyboardWidth, lane);
    clipLane->box = values->box;
    expression->box = values->box;
    // The label stays inside the lane, however short the lane gets (the in-panel editor).
    lowerButton->box = rack::math::Rect(2, body + gridHeight + tabs + 2, kKeyboardWidth - 4, std::min(30.f, lane - 4));
    // The clip chip first, sized to its words, then Devices; the toolbar fills from the right. A narrow header
    // shortens the chip to the track's name, so snap and key keep their room.
    auto placeChip = [this](bool shortText) {
        placeClipChip(ctx, clipChip, source.clip, shortText, box.size.x);
        devicesToggle->box = rack::math::Rect(clipChip->box.getRight() + 4, 3, kDevicesToggleWidth, kPaneHeader - 6);
    };
    placeChip(false);
    if (layoutToolbar(kPaneHeader)) {
        placeChip(true);
        layoutToolbar(kPaneHeader);
    }
}

bool PianoRollView::anyNoteInView() {
    const Clip* c = clip();
    if (!c || c->notes.empty())
        return true;
    for (const Note& n : c->notes) {
        if (double(n.end()) <= viewport.time.left() || double(n.start) >= viewport.time.right() ||
            !pitch.shown(n.pitch))
            continue;
        double y = pitch.pitchToY(n.pitch);
        if (y + pitch.rowHeight() > 0 && y < pitch.viewHeight())
            return true;
    }
    return false;
}

void PianoRollView::applyZoomLimits() {
    double width = std::max(1.f, grid->box.size.x);
    Tick beat = ticksPerBeat(source.timeSig());
    viewport.time.setViewWidth(width);
    // Zoom out until the whole clip fits (or 2 px per beat); zoom in until a 1/16 fills the view.
    double minPpt = std::min(kMinPxPerBeat / double(beat), width / source.contentEnd());
    viewport.time.setZoomLimits(minPpt, width / double(kPpq / 4));
    viewport.time.setContentEnd(source.contentEnd());
}

void PianoRollView::step() {
    TesseraModule* m = ctx.module();
    if (m) {
        EditorState& state = ctx.state();
        const Song& song = m->song();
        if (!song.findTrack(state.selectedTrack) && !song.tracks.empty())
            state.selectedTrack = song.tracks.front().id;
        const Track* t = track();
        source.clip = t ? clipActions::shownClip(ctx, *t) : ClipId();
        source.track = t ? t->id : TrackId();
        updateDevicesToggle(ctx, devicesToggle);
        const Clip* shown = clip();
        const AutomationEnvelope* lowerLane =
            shown ? findLane(song, LaneOwner::ofClip(shown->id), AutomationId(ctx.view().lowerLane)) : nullptr;
        clipLane->lane = lowerLane ? lowerLane->id : AutomationId();
        clipLane->visible = lowerLane != nullptr;
        if (source.clip != pointsClip || state.tool != pointsTool) {
            curvePoints.clear();
            pointsClip = source.clip;
            pointsTool = state.tool;
        }
        NoteLane noteLane = ctx.view().noteLane;
        std::optional<expr::Curve> curve = curveOf(noteLane);
        expression->visible = !clipLane->visible && curve;
        values->visible = !clipLane->visible && !curve;
        if (curve)
            expression->curve = *curve;
        else
            values->field = fieldOf(noteLane);
        lowerButton->setText(lowerLane ? lowerLane->name : noteLaneName(noteLane));

        bool stepOn = state.stepInput && shown;
        if (stepOn && !stepWasOn)
            stepper.start(*this);
        if (stepOn)
            stepper.update(*this);
        stepWasOn = stepOn;

        layout();
        bool sized = grid->box.size.x > kMinViewSize && grid->box.size.y > kMinViewSize;
        if (!sized) {
            EditorPane::step();
            return;
        }
        applyZoomLimits();
        pitch.setViewHeight(grid->box.size.y);
        updateRows();

        if (!restored) {
            restored = true;
            shownClip = source.clip;
            if (ctx.view().detail.pxPerTick > 0) {
                viewport.time.restore(ctx.view().detail);
                pitch.restore(ctx.view().pitch);
                // The saved view may belong to another clip or a bigger window: never open on empty rows.
                if (!anyNoteInView())
                    grid->zoomToFit();
            } else {
                grid->zoomToFit();
            }
        } else if (source.clip != shownClip) {
            // Opening another clip scrolls to its notes, not to C0 (EDITOR_UX §0).
            shownClip = source.clip;
            preview.reset();
            grid->zoomToFit();
        }

        double now = EditorContext::now();
        viewport.time.step(lastStep > 0 ? now - lastStep : 0);
        lastStep = now;

        if (m->playing() && !ctx.followPaused())
            if (auto left = followScroll(state.follow, source.playhead(), viewport.time))
                viewport.time.scrollTo(*left);

        ctx.view().detail = viewport.time.state();
        ctx.view().pitch = pitch.state();
    }
    EditorPane::step();
}

// ---- Drawing ------------------------------------------------------------------------------------------------

void PianoRollView::draw(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    drawHeader(vg);

    EditorPane::draw(args);
    // Whether the next take records into the clip lane shown below, as the track header marks its lanes.
    TesseraModule* m = ctx.module();
    const Clip* shown = clip();
    if (m && shown && clipLane->visible && lowerButton->visible)
        if (const AutomationEnvelope* lane = findLane(m->song(), LaneOwner::ofClip(shown->id), clipLane->lane)) {
            LaneRecordState state =
                laneRecordState(m->song(), LaneOwner::ofClip(shown->id), *lane, m->autoRecInputs(), nullptr);
            const rack::math::Rect& b = lowerButton->box;
            drawLaneRecordMark(vg, rack::math::Vec(b.getRight() - 7, b.pos.y + 7), state);
        }
}

void PianoRollView::drawHeader(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    float h = theme::metrics::kPaneHeader;
    // What the chip doesn't say, where there is room: how much of the clip the timeline plays, and the selection.
    std::string text;
    if (clip())
        if (const ArrangerClip* a = source.instance()) {
            double bars = double(a->length) / double(ticksPerBar(m->song().timeSig));
            char on[48];
            snprintf(on, sizeof(on),
                     std::fabs(bars - std::round(bars)) < 1e-6 ? "%.0f bar%s on the timeline"
                                                               : "%.2f bar%s on the timeline",
                     bars, std::fabs(bars - 1) < 1e-6 ? "" : "s");
            text = on;
        }
    size_t selected = selectedIds().size();
    if (selected > 0)
        text += (text.empty() ? "" : "  ·  ") + countNotes(selected) + " selected";
    float x = devicesToggle->box.getRight() + 8;
    draw::TextStyle status{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    if (!text.empty() && toolbarLeft - x - 8 >= 40)
        draw::text(vg, status, x, h / 2, draw::ellipsize(vg, status, text, toolbarLeft - x - 8));

    nvgBeginPath(vg);
    nvgRect(vg, 0, h - 1, box.size.x, 1);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);
}

void PianoRollView::appendViewItems(rack::ui::Menu* menu) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    EditorContext* c = &ctx;
    const EditorState& state = ctx.state();
    std::string ghosts = "Off";
    if (state.ghosts == GhostSource::OtherClips)
        ghosts = "Other clips";
    else if (state.ghosts == GhostSource::AllTracks)
        ghosts = "All tracks";
    else if (state.ghosts == GhostSource::Track)
        if (const Track* g = m->song().findTrack(state.ghostTrack))
            ghosts = g->name;
    menu->addChild(rack::createSubmenuItem("Ghost notes", ghosts, [c](rack::ui::Menu* sub) {
        TesseraModule* mod = c->module();
        if (!mod)
            return;
        auto item = [&](std::string name, GhostSource source, TrackId track = TrackId()) {
            sub->addChild(rack::createCheckMenuItem(
                name, "",
                [c, source, track] {
                    return c->state().ghosts == source &&
                           (source != GhostSource::Track || c->state().ghostTrack == track);
                },
                [c, source, track] {
                    c->state().ghosts = source;
                    c->state().ghostTrack = track;
                }));
        };
        item("Off", GhostSource::None);
        item("Other clips on this track", GhostSource::OtherClips);
        item("All instrument tracks", GhostSource::AllTracks);
        sub->addChild(new rack::ui::MenuSeparator);
        for (const Track& t : mod->song().tracks)
            if (t.id != c->state().selectedTrack)
                item(t.name, GhostSource::Track, t.id);
    }));
    auto flag = [c, menu](std::string text, std::string right, bool EditorState::* field) {
        menu->addChild(rack::createCheckMenuItem(
            text, right, [c, field] { return c->module() && c->state().*field; },
            [c, field] { c->state().*field = !(c->state().*field); }));
    };
    flag("Show chance and repeats on notes", "", &EditorState::noteBadges);
    const Track* t = track();
    if (t && t->type == TrackType::Drum)
        flag("Fold: only the drum lanes", shortcutLabel(Action::ScaleFold), &EditorState::scaleFold);
    // Global: every note and key name in Tessera, saved with the plugin's settings like the colour scheme.
    menu->addChild(rack::createBoolMenuItem(
        "Name notes with flats (Db, not C#)", "", [] { return accidentals() == Accidentals::Flats; },
        [](bool on) { setAccidentals(on ? Accidentals::Flats : Accidentals::Sharps); }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Follow the playhead"));
    auto follow = [c, menu](std::string text, FollowMode mode) {
        menu->addChild(rack::createCheckMenuItem(
            text, "", [c, mode] { return c->module() && c->state().follow == mode; },
            [c, mode] { c->state().follow = mode; }));
    };
    follow("Off", FollowMode::Off);
    follow("Page by page", FollowMode::Page);
    follow("Scroll smoothly", FollowMode::Continuous);
}

// ---- What is being edited -----------------------------------------------------------------------------------

const Track* PianoRollView::track() {
    TesseraModule* m = ctx.module();
    return m ? m->song().findTrack(ctx.state().selectedTrack) : nullptr;
}

const Clip* PianoRollView::clip() {
    TesseraModule* m = ctx.module();
    return m ? m->song().findClip(source.clip) : nullptr;
}

const NoteList& PianoRollView::committedNotes() {
    static const NoteList none;
    const Clip* c = clip();
    return c ? c->notes : none;
}

const NoteList& PianoRollView::notes() { return preview ? *preview : committedNotes(); }

IdSet PianoRollView::selectedIds() {
    const NoteSelection& s = ctx.state().selection;
    return s.clip == source.clip ? s.notes : IdSet{};
}

bool PianoRollView::isSelected(NoteId id) {
    const NoteSelection& s = ctx.state().selection;
    return s.clip == source.clip && s.contains(id);
}

uint8_t PianoRollView::drawChannel() {
    IdSet ids = selectedIds();
    int shared = 0;
    for (const Note& n : committedNotes()) {
        if (!ids.count(n.id))
            continue;
        if (shared != 0 && shared != n.channel)
            return ctx.state().lastChannel;
        shared = n.channel;
    }
    return shared != 0 ? uint8_t(shared) : ctx.state().lastChannel;
}

void PianoRollView::select(IdSet ids) { ctx.state().selection = NoteSelection{source.clip, std::move(ids)}; }

// ---- Editing ------------------------------------------------------------------------------------------------

void PianoRollView::commit(const std::string& name, const NoteList& before, const NoteList& after,
                           std::optional<IdSet> selectAfter) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    NoteSelection selection{source.clip, {}};
    if (selectAfter) {
        selection.notes = *selectAfter;
    } else {
        // Keep the current selection, minus notes that no longer exist.
        IdSet present = edits::idsOf(after);
        for (NoteId id : selectedIds())
            if (present.count(id))
                selection.notes.insert(id);
    }
    CommandPtr command = editNotes(m->song(), name, source.clip, before, after);
    if (command)
        m->perform(std::move(command), selection);
    else
        ctx.state().selection = selection;
}

Tick PianoRollView::snapStep() { return tessera::snapStep(ctx.state().snap, viewport.time.pxPerTick(), timeSig()); }

NoteId PianoRollView::newNoteId() { return ctx.module()->newId<NoteId>(); }

void PianoRollView::audition(int pitchValue, float velocityValue) {
    TesseraModule* m = ctx.module();
    const Track* t = track();
    if (m && t && ctx.state().audition)
        m->audition(t->id, pitchValue, velocityValue, true, kAuditionSeconds);
}

void PianoRollView::auditionOn(int pitchValue, float velocityValue) {
    TesseraModule* m = ctx.module();
    if (const Track* t = track(); m && t)
        m->audition(t->id, pitchValue, velocityValue, true);
}

void PianoRollView::auditionOff(int pitchValue) {
    TesseraModule* m = ctx.module();
    if (const Track* t = track(); m && t)
        m->audition(t->id, pitchValue, 0, false);
}

void PianoRollView::createClip() {
    if (const Track* t = track(); ctx.module() && t)
        clipActions::createInFirstEmpty(ctx, t->id);
}

void PianoRollView::openLowerMenu(bool withNoteLanes) {
    TesseraModule* m = ctx.module();
    const Clip* shown = clip();
    if (!m)
        return;
    EditorContext* c = &ctx;
    rack::ui::Menu* menu = rack::createMenu();
    auto noteLaneItem = [&](NoteLane lane) {
        menu->addChild(rack::createCheckMenuItem(
            noteLaneName(lane), "", [c, lane] { return c->view().lowerLane == 0 && c->view().noteLane == lane; },
            [c, lane] {
                c->view().lowerLane = 0;
                c->view().noteLane = lane;
            }));
    };
    if (withNoteLanes) {
        menu->addChild(rack::createMenuLabel("Note values"));
        for (NoteLane lane :
             {NoteLane::Velocity, NoteLane::Release, NoteLane::Chance, NoteLane::Repeats, NoteLane::Micro})
            noteLaneItem(lane);
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuLabel("Note expression (curves inside each note)"));
        for (NoteLane lane : {NoteLane::Pitch, NoteLane::Timbre, NoteLane::Pressure})
            noteLaneItem(lane);
    }
    if (!shown)
        return;
    ClipId clipId = shown->id;
    if (withNoteLanes)
        menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Clip automation (loops with the clip)"));
    for (const AutomationEnvelope& l : shown->automation) {
        uint64_t id = l.id.value;
        menu->addChild(rack::createCheckMenuItem(
            l.name, laneOutputLabel(l), [c, id] { return c->view().lowerLane == id; },
            [c, id] { c->view().lowerLane = id; }));
    }
    TrackId trackId = source.track;
    menu->addChild(rack::createSubmenuItem("Add clip lane", "", [c, trackId, clipId](rack::ui::Menu* sub) {
        trackActions::appendAddLaneItems(sub, *c, trackId, LaneOwner::ofClip(clipId));
    }));
}

// ---- Key and scale tools -----------------------------------------------------------------------------------

Scale PianoRollView::scale() {
    TesseraModule* m = ctx.module();
    if (!m)
        return Scale();
    const Song& song = m->song();
    const Clip* c = clip();
    // While the key picker is open it shows its choice here first.
    const KeyPreview& preview = ctx.state().keyPreview;
    if (preview.active) {
        std::optional<ScaleRef> own = c ? c->scaleOverride : std::nullopt;
        if (c && preview.clip == c->id)
            own = preview.clipKey;
        return resolveScale(own ? *own : preview.song, song.customScales);
    }
    return c ? clipScale(song, *c) : resolveScale(song.scale, song.customScales);
}

bool PianoRollView::keyActive() {
    const Track* t = track();
    return t && t->type == TrackType::Instrument && !scale().chromatic();
}

bool PianoRollView::highlightKey() { return ctx.module() && ctx.state().scaleHighlight && keyActive(); }

bool PianoRollView::snapToKey() { return ctx.module() && ctx.state().scaleSnap && keyActive(); }

int PianoRollView::stepPitch(int pitchValue, int rows, bool chromatic) {
    if (rows == 0)
        return pitchValue;
    if (snapToKey() && !chromatic)
        return scale().stepBy(pitchValue, -rows);
    return pitch.stepRows(pitchValue, rows);
}

NoteList PianoRollView::stepPitches(const NoteList& notes, const IdSet& ids, int rows, bool chromatic) {
    if (rows == 0)
        return notes;
    if (snapToKey() && !chromatic)
        return edits::stepInScale(notes, ids, scale(), -rows);
    if (pitch.folded()) {
        // Copy the axis: the preview must not depend on the live one changing under it.
        PitchAxis axis = pitch;
        return edits::mapPitches(notes, ids, [&axis, rows](int p) { return axis.stepRows(p, rows); });
    }
    return edits::move(notes, ids, 0, -rows);
}

int PianoRollView::placePitch(int pitchValue, bool chromatic) {
    return snapToKey() && !chromatic ? scale().snap(pitchValue) : pitchValue;
}

std::vector<int> PianoRollView::stampPitches(int root) {
    const Track* t = track();
    if (ctx.module() && ctx.state().chord.on() && t && t->type == TrackType::Instrument) {
        // Folded or snapped to the key, chords are stacked in the key's steps (a major triad is two rows and two).
        bool inKey = keyActive() && (ctx.state().scaleFold || ctx.state().scaleSnap);
        std::vector<int> chord = chordPitches(root, ctx.state().chord, scale(), inKey);
        if (!chord.empty())
            return chord;
    }
    return {root};
}

void PianoRollView::updateRows() {
    std::vector<int> shown;
    const Track* t = track();
    if (ctx.state().scaleFold && t) {
        if (t->type == TrackType::Drum) {
            for (const DrumLane& lane : t->drumLanes)
                shown.push_back(lane.pitch);
        } else if (keyActive()) {
            Scale s = scale();
            for (int p = 0; p < PitchAxis::kRows; p++)
                if (s.inScale(p))
                    shown.push_back(p);
        }
        // Notes off the folded rows keep a row of their own, so folding never hides a note.
        if (!shown.empty())
            for (const Note& n : committedNotes())
                shown.push_back(n.pitch);
    }
    pitch.setShownPitches(shown);
}

// ---- Keyboard -----------------------------------------------------------------------------------------------

bool PianoRollView::handleAction(Action action) {
    if (automationFocus && clipLane->visible && clipLane->handleAction(action))
        return true;
    switch (action) {
    case Action::GoToStart:
        viewport.time.scrollTo(double(source.loopStart()));
        return true;
    case Action::GoToEnd:
        viewport.time.scrollTo(double(source.loopEnd()) - viewport.time.visibleTicks() * 0.8);
        return true;
    case Action::LoopToSelection: {
        // The clip's loop covers the selected notes, out to the grid, and the clip loops.
        IdSet ids = selectedIds();
        if (!clip() || ids.empty())
            return false;
        Tick from = std::numeric_limits<Tick>::max(), to = 0;
        for (const Note& n : committedNotes())
            if (ids.count(n.id)) {
                from = std::min(from, n.start);
                to = std::max(to, n.end());
            }
        Tick step = std::max<Tick>(ruler->snapStep(), 1);
        from = std::max<Tick>(0, from / step * step);
        to = (to + step - 1) / step * step;
        if (to <= from)
            return false;
        source.setLoop(from, to);
        if (!source.loopOn())
            source.toggleLoop();
        return true;
    }
    default:
        if (clip() && editAction(action))
            return true;
        return grid->handleAction(action);
    }
}

void PianoRollView::editSelection(const std::string& name,
                                  const std::function<NoteList(const NoteList&, const IdSet&)>& change) {
    IdSet ids = selectedIds();
    if (ids.empty())
        return;
    NoteList before = committedNotes();
    NoteList after = edits::resolveOverlaps(change(before, ids), ids);
    commit(name + " " + countNotes(ids.size()), before, after);
}

bool PianoRollView::editAction(Action action) {
    if (ctx.state().stepInput) {
        switch (action) {
        case Action::NudgeRight:
            stepper.rest(*this);
            return true;
        case Action::NudgeBarRight:
            stepper.tie(*this);
            return true;
        case Action::NudgeLeft:
            stepper.back(*this);
            return true;
        case Action::Delete:
            stepper.undoStep(*this);
            return true;
        default:
            break;
        }
    }
    switch (action) {
    case Action::Quantize:
        transformMenu::quantizeNow(*this);
        return true;
    case Action::QuantizeDialog:
        transformMenu::openQuantize(*this);
        return true;
    default:
        break;
    }
    Tick grid = snapStep() > 0 ? snapStep() : kPpq / 16;
    Tick bar = ticksPerBar(timeSig());
    auto auditionSelection = [&] {
        for (const Note& n : committedNotes())
            if (isSelected(n.id)) {
                audition(n.pitch, n.velocity);
                break;
            }
    };
    auto moveBy = [&](const char* verb, Tick dt, int dp) {
        editSelection(verb, [=](const NoteList& n, const IdSet& ids) { return edits::move(n, ids, dt, dp); });
        if (dp != 0)
            auditionSelection();
    };
    auto stepBy = [&](int rows) {
        editSelection("Transpose",
                      [=](const NoteList& n, const IdSet& ids) { return stepPitches(n, ids, rows, false); });
        auditionSelection();
    };
    auto velocityBy = [&](float dv) {
        editSelection("Change velocity of",
                      [=](const NoteList& n, const IdSet& ids) { return edits::changeVelocity(n, ids, dv); });
    };

    switch (action) {
    case Action::Delete: {
        // Picked curve points go first where curves are being edited: the Bend tool, a strip, or the lower lane.
        // With Select, Delete stays with the notes.
        bool inLane = curveLaneFocus && shownCurve() == pointsCurve;
        bool inGrid =
            !curveLaneFocus && (stripCurve() == pointsCurve || (bendMode() && pointsCurve == expr::Curve::Pitch));
        if ((inLane || inGrid) && !curvePoints.empty()) {
            expr::Curve curve = pointsCurve;
            NoteList before = committedNotes(), after = before;
            size_t removed = expr::removePoints(after, curve, curvePoints);
            curvePoints.clear();
            if (removed > 0) {
                commit(removed == 1 ? "Delete curve point" : "Delete " + std::to_string(removed) + " curve points",
                       before, after);
                return true;
            }
        }
        IdSet ids = selectedIds();
        if (ids.empty())
            return true;
        NoteList before = committedNotes(), after;
        for (const Note& n : before)
            if (!ids.count(n.id))
                after.push_back(n);
        commit("Delete " + countNotes(ids.size()), before, after, IdSet{});
        return true;
    }
    case Action::SelectAll:
        curvePoints.clear();
        select(edits::idsOf(committedNotes()));
        return true;
    case Action::SelectNone:
        curvePoints.clear();
        select({});
        return true;
    case Action::SelectInvert: {
        IdSet current = selectedIds(), next;
        for (const Note& n : committedNotes())
            if (!current.count(n.id))
                next.insert(n.id);
        select(next);
        return true;
    }
    case Action::Copy:
        copySelection(false);
        return true;
    case Action::Cut:
        copySelection(true);
        return true;
    case Action::Paste:
        paste(false);
        return true;
    case Action::PasteAtMouse:
        paste(true);
        return true;
    case Action::Duplicate: {
        IdSet ids = selectedIds();
        if (ids.empty())
            return true;
        NoteList before = committedNotes();
        // Stamp the copy right after the selection, on the grid, so repeated Ctrl+D keeps stamping.
        Tick offset = edits::duplicateOffset(before, ids, grid, timeSig());
        IdSet added;
        NoteList after = edits::duplicate(before, ids, offset, [this] { return newNoteId(); }, &added);
        commit("Duplicate " + countNotes(ids.size()), before, edits::resolveOverlaps(after, added), added);
        return true;
    }
    case Action::TransposeUp:
        stepBy(-1);
        return true;
    case Action::TransposeDown:
        stepBy(1);
        return true;
    case Action::OctaveUp:
        moveBy("Transpose", 0, 12);
        return true;
    case Action::OctaveDown:
        moveBy("Transpose", 0, -12);
        return true;
    case Action::NudgeLeft:
        moveBy("Nudge", -grid, 0);
        return true;
    case Action::NudgeRight:
        moveBy("Nudge", grid, 0);
        return true;
    case Action::NudgeFineLeft:
        moveBy("Nudge", -std::max<Tick>(1, grid / 10), 0);
        return true;
    case Action::NudgeFineRight:
        moveBy("Nudge", std::max<Tick>(1, grid / 10), 0);
        return true;
    case Action::NudgeBarLeft:
        moveBy("Nudge", -bar, 0);
        return true;
    case Action::NudgeBarRight:
        moveBy("Nudge", bar, 0);
        return true;
    case Action::LengthShorter:
        editSelection("Shorten",
                      [=](const NoteList& n, const IdSet& ids) { return edits::resizeEnd(n, ids, -grid, grid); });
        return true;
    case Action::LengthLonger:
        editSelection("Lengthen",
                      [=](const NoteList& n, const IdSet& ids) { return edits::resizeEnd(n, ids, grid, grid); });
        return true;
    case Action::VelocityUp:
        velocityBy(kVelocityStep);
        return true;
    case Action::VelocityDown:
        velocityBy(-kVelocityStep);
        return true;
    case Action::VelocityUpFine:
        velocityBy(kVelocityFineStep);
        return true;
    case Action::VelocityDownFine:
        velocityBy(-kVelocityFineStep);
        return true;
    case Action::CycleLeft:
    case Action::CycleRight:
    case Action::CycleUp:
    case Action::CycleDown:
        cycleSelection(action);
        return true;
    default:
        return false;
    }
}

void PianoRollView::copySelection(bool cut) {
    IdSet ids = selectedIds();
    if (ids.empty())
        return;
    NoteList before = committedNotes();
    NoteClipboard::get().copy(edits::pick(before, ids));
    if (!cut)
        return;
    NoteList after;
    for (const Note& n : before)
        if (!ids.count(n.id))
            after.push_back(n);
    commit("Cut " + countNotes(ids.size()), before, after, IdSet{});
}

void PianoRollView::paste(bool atMouse) {
    NoteClipboard& clipboard = NoteClipboard::get();
    if (clipboard.empty())
        return;
    double at = NAN;
    if (atMouse) {
        rack::math::Vec m = grid->localMouse();
        if (m.x >= 0 && m.x <= grid->box.size.x)
            at = viewport.time.xToTick(m.x);
    }
    if (!std::isfinite(at))
        at = source.playhead();
    if (!std::isfinite(at))
        at = viewport.time.left();
    Tick start = snapPosition(at, snapStep());
    NoteList pasted = clipboard.pasteAt(start, [this] { return newNoteId(); });
    NoteList before = committedNotes();
    NoteList after = before;
    after.insert(after.end(), pasted.begin(), pasted.end());
    std::sort(after.begin(), after.end(), noteOrder);
    IdSet ids = edits::idsOf(pasted);
    commit("Paste " + countNotes(pasted.size()), before, edits::resolveOverlaps(after, ids), ids);
}

void PianoRollView::cycleSelection(Action action) {
    const NoteList& notes = committedNotes();
    if (notes.empty())
        return;
    IdSet selected = selectedIds();
    const Note* anchor = nullptr;
    for (const Note& n : notes)
        if (selected.count(n.id)) {
            anchor = &n;
            break;
        }
    const Note* next = nullptr;
    if (!anchor) {
        next = &notes.front();
    } else if (action == Action::CycleRight || action == Action::CycleLeft) {
        // Notes are kept in time order, so the neighbour in the list is the next note in time.
        size_t i = size_t(anchor - notes.data());
        if (action == Action::CycleRight && i + 1 < notes.size())
            next = &notes[i + 1];
        if (action == Action::CycleLeft && i > 0)
            next = &notes[i - 1];
    } else {
        // Up / down: the nearest note above or below, preferring notes that sound at the same time.
        bool up = action == Action::CycleUp;
        double best = 1e18;
        for (const Note& n : notes) {
            int dp = n.pitch - anchor->pitch;
            if ((up && dp <= 0) || (!up && dp >= 0))
                continue;
            bool together = n.start <= anchor->start && anchor->start < n.end();
            double score = (together ? 0 : 1e9) + std::abs(double(n.start - anchor->start)) * 1e3 + std::abs(dp);
            if (score < best) {
                best = score;
                next = &n;
            }
        }
    }
    if (!next)
        return;
    select(IdSet{next->id});
    audition(next->pitch, next->velocity);
}

} // namespace tessera
