#include "MixerPane.hpp"

#include "../../model/AudioEdits.hpp"
#include "../../model/Sends.hpp"
#include "../../module/OutputModuleBase.hpp"
#include "../../module/Registry.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/AudioActions.hpp"
#include "../editing/SendActions.hpp"
#include "../editing/Sources.hpp"
#include "../editing/TrackActions.hpp"
#include "../widgets/ColorMenuItem.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/EditTools.hpp"
#include "../widgets/MenuTextField.hpp"
#include "AudioClipDraw.hpp"

#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kStripWidth = 64;
constexpr float kLaneStripWidth = 40; // a drum lane IN's strip: narrower, and set against its track's
constexpr float kMasterWidth = 88;
constexpr float kGap = 4;
constexpr float kKnobStripsBelow = 200; // pane height (px) under which the strips use knobs instead of faders
constexpr float kKnobsStackFrom = 80;   // pane height (px) from which a strip's knobs sit one above the other
constexpr float kKnobStripWidth = 62;
constexpr float kKnobLaneWidth = 50;
constexpr float kMinZoom = 0.6f, kMaxZoom = 2.f; // how far the strips narrow and widen
constexpr float kKnobTravel = 140; // px of drag across a level knob's whole range
constexpr float kFaderSnapPx = 6;  // the soft detent at 0 dB
constexpr float kSendRow = 16;     // a send's row: the receiving track's name over a thin level bar
constexpr float kSendsLabel = 10;  // the small "SENDS" over the rows
constexpr float kDotRow = 10;      // a row of send dots, when the rows don't fit
constexpr int kDotsPerRow = 5;
constexpr float kStripFixed = 160;       // a tall strip's height besides its sends and fader
constexpr float kSendRowsMinTravel = 80; // the fader travel the send rows must leave
constexpr float kMixBarLabel = 11;       // room for "wet" above an effect's Mix bar and "dry" below it
constexpr float kMixBarMinTravel = 70;   // shorter than this, the bar keeps its whole travel and no words
constexpr float kZeroDbPos = kZeroDbFaderPos;
constexpr double kPeakHoldSeconds = 1.5;
constexpr float kWheelDb = 0.5f;
constexpr float kFine = 0.1f;
/// The part of a source chip, from its left, whose click folds the input strips (where the arrow is drawn).

/// A level in volts as dB of full scale (10 V).
float voltsDb(float volts) { return gainToDb(volts / kFullScaleVolts); }

/// Where a level sits on a meter running from -60 to +6 dB (0..1).
float meterPos(float db) { return std::clamp((db + 60) / 66, 0.f, 1.f); }

/// What a strip's source chip says, and whether it warns (amber).
struct ChipText {
    std::string text;
    bool warn = false;
    bool dim = false;
};

/// The source chip of a track's strip (§7.2): its clips, the module whose IN brings its sound, "2 sources", or
/// what is wrong ("Missing file", "Bypassed": a module that brings its sound is bypassed; the chip's menu says
/// which). Null `t` is the master, whose chip the caller writes.
ChipText chipText(TesseraModule& m, const Track* t) {
    if (!t)
        return {};
    const Song& song = m.song();
    for (const auto& [id, clip] : song.clips) {
        if (!clip.audio || song.clipUses(id) == 0)
            continue;
        bool used = std::any_of(t->arrangement.begin(), t->arrangement.end(),
                                [&](const ArrangerClip& a) { return a.clip == id; }) ||
                    std::any_of(t->slots.begin(), t->slots.end(), [&](const ClipSlot& sl) { return sl.clip == id; });
        if (used && audioDraw::stateOf(m, clip).status.state == SourceState::Missing)
            return {"Missing file", true};
    }
    for (OutputModuleBase* o : m.sourceModules(t->id))
        if (o->isBypassed())
            return {"Bypassed", true};
    std::vector<sources::Row> rows = sources::rowsOf(m, t->id);
    int counted = 0, inUse = 0, loops = 0;
    for (const sources::Row& r : rows) {
        if (r.module && r.module->isLoop()) {
            loops++;
            continue;
        }
        if (!r.countedOnce) {
            counted++;
            inUse += r.level.use ? 1 : 0;
        }
    }
    // A track whose only Track Out is an effect loop (a send return) plays only what it is sent through it.
    if (loops > 0 && counted == 0)
        return {std::to_string(loops) + " fx"};
    if (loops > 0)
        return {std::to_string(counted) + " in \xC2\xB7 " + std::to_string(loops) + " fx"};
    if (sources::needsList(rows)) {
        if (inUse == 0)
            return {"No source in use", false, true};
        return {counted == 1 ? "1 source" : std::to_string(counted) + " sources"};
    }
    for (const sources::Row& r : rows)
        if (r.module)
            return {"IN · " + r.name};
    return {t->type == TrackType::Audio ? "Clips" : "IN"};
}

/// A small menu with one field to type a new name in; Enter commits it. The mixer has no inline name field, so its
/// menus' Rename opens this.
void renameMenu(const std::string& title, const std::string& now, std::function<void(const std::string&)> commit) {
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(title));
    auto* field = new MenuTextField(now, [commit](const std::string& text) { commit(text); });
    menu->addChild(field);
    APP->event->setSelectedWidget(field);
}

/// The menu for typing a level: "Type a value", then Reset.
/// The menu to type a level: `automate`, when set, adds "Automate in the timeline" (§7.3).
void typingMenu(EditorContext& ctx, const std::string& title, float now, std::function<void(float)> set,
                std::function<void()> automate = nullptr) {
    rack::ui::Menu* menu = rack::createMenu();
    if (automate)
        menu->addChild(rack::createMenuItem("Automate in the timeline", "", automate));
    menu->addChild(rack::createMenuLabel(title));
    char text[16];
    snprintf(text, sizeof text, "%.1f", double(now));
    auto* field = new MenuTextField(now <= kSilentDb ? "-inf" : text, [set](const std::string& typed) {
        if (typed == "-inf" || typed == "-∞") {
            set(kSilentDb);
            return;
        }
        char* end = nullptr;
        double v = std::strtod(typed.c_str(), &end);
        if (end != typed.c_str())
            set(float(v));
    });
    menu->addChild(field);
    menu->addChild(rack::createMenuItem("Reset to 0 dB", "double-click", [set] { set(0); }));
    APP->event->setSelectedWidget(field);
    (void)ctx;
}

/// Drags a fader: relative, so a click never jumps it. Shift or Ctrl/Cmd for fine; a soft detent at 0 dB that Alt
/// skips. On a selected strip with others selected, every selected strip moves by the same dB (§7.3).
/// Makes the undo steps pushed since there were `from` of them a single step, for a click that changes two things.
void joinUndoSteps(size_t from, const std::string& name) {
    rack::history::State* history = APP->history;
    if (history->actions.size() < from + 2 || history->actionIndex != int(history->actions.size()))
        return;
    auto* joined = new rack::history::ComplexAction;
    joined->name = name;
    for (size_t i = from; i < history->actions.size(); i++)
        joined->push(history->actions[i]);
    history->actions.resize(from);
    history->actionIndex = int(from);
    history->push(joined);
}

/// What a narrow strip is called in readouts and undo steps.
std::string laneName(const MixerPane::Strip& s) {
    return !s.label.empty() ? s.label : "Lane " + std::to_string(s.lane + 1);
}

class FaderGesture : public DragGesture {
  public:
    FaderGesture(MixerPane& pane, EditorContext& ctx, int strip, Vec press) : pane(pane), ctx(ctx), press(press) {
        const MixerPane::Strip& s = pane.layoutStrips()[size_t(strip)];
        master = s.master;
        track = s.track;
        travel = s.knob ? kKnobTravel : std::max(10.f, s.fader.size.y);
        startDb = pane.shownVolume(s);
        TesseraModule* m = ctx.module();
        if (master) {
            track = m->song().master.id;
            return;
        }
        groupId = s.group;
        if (groupId.valid()) {
            const TrackGroup* g = m->song().findGroup(groupId);
            groupPan = g ? g->channel.pan : 0;
            return;
        }
        const std::vector<TrackId>& sel = ctx.state().selectedTracks;
        bool inSelection = std::find(sel.begin(), sel.end(), s.track) != sel.end() && sel.size() > 1;
        for (const MixerPane::Strip& o : pane.layoutStrips()) {
            if (o.master || o.lane >= 0 ||
                (o.track != s.track && (!inSelection || std::find(sel.begin(), sel.end(), o.track) == sel.end())))
                continue;
            if (const Track* t = m->song().findTrack(o.track))
                group.push_back({t->id, t->channel, t->channel});
        }
    }

    void update(Vec pos, Modifiers mods) override {
        float scale = mods.shift || mods.ctrl ? kFine : 1.f;
        float p = MixerPane::faderPos(startDb) - (pos.y - press.y) / travel * scale;
        float db = MixerPane::faderDb(std::clamp(p, 0.f, 1.f));
        if (!mods.alt && std::abs(MixerPane::faderPos(db) - kZeroDbPos) * travel < kFaderSnapPx)
            db = 0;
        now = db;
        TesseraModule* m = ctx.module();
        if (master || groupId.valid()) {
            m->holdChannel(track, now, groupPan, false);
            pane.showHeld(track, now, groupPan);
            return;
        }
        for (const SetChannels::Change& g : group) {
            float v = g.track == track ? now : afterDelta(g.before.volumeDb);
            m->holdChannel(g.track, v, g.before.pan, false);
            pane.showHeld(g.track, v, g.before.pan);
        }
    }
    void commit() override {
        TesseraModule* m = ctx.module();
        if (now != startDb) {
            if (master)
                m->perform(setMasterVolume(m->song(), now));
            else if (groupId.valid())
                m->perform(
                    setGroupChannel(m->song(), groupId, {now, groupPan}, "Group volume " + audioDraw::dbText(now)));
            else if (group.size() == 1)
                m->perform(
                    setChannel(m->song(), track, {now, group[0].before.pan}, "Volume " + audioDraw::dbText(now)));
            else
                m->perform(nudgeVolumes(m->song(), group, now - startDb));
        }
        m->releaseChannelHolds();
        pane.clearHeld();
    }
    void cancel() override {
        ctx.module()->audioPlayer().releaseHolds();
        pane.clearHeld();
    }
    std::string readout() const override {
        if (group.size() > 1) {
            char text[48];
            snprintf(text, sizeof text, "%zu tracks \xC2\xB7 %+.1f dB", group.size(), double(now - startDb));
            return text;
        }
        return (master ? "Master " : "Volume ") + audioDraw::dbText(now);
    }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    float afterDelta(float from) const {
        return from <= kSilentDb ? kSilentDb : std::clamp(from + (now - startDb), kSilentDb, kMaxFaderDb);
    }
    MixerPane& pane;
    EditorContext& ctx;
    Vec press;
    bool master = false;
    TrackId track;
    GroupId groupId; // a group strip's fader
    float groupPan = 0;
    float travel = 100;
    float startDb = 0, now = 0;
    std::vector<SetChannels::Change> group;
};

/// Drags a drum lane strip's fader: like a track's, without the selection (lanes aren't selected).
class LaneFaderGesture : public DragGesture {
  public:
    LaneFaderGesture(MixerPane& pane, int strip, Vec press) : pane(pane), press(press) {
        s = pane.layoutStrips()[size_t(strip)];
        travel = s.knob ? kKnobTravel : std::max(10.f, s.fader.size.y);
        start = {pane.shownVolume(s), pane.shownPan(s)};
        now = start;
    }
    void update(Vec pos, Modifiers mods) override {
        float scale = mods.shift || mods.ctrl ? kFine : 1.f;
        float p = MixerPane::faderPos(start.volumeDb) - (pos.y - press.y) / travel * scale;
        now.volumeDb = MixerPane::faderDb(std::clamp(p, 0.f, 1.f));
        if (!mods.alt && std::abs(MixerPane::faderPos(now.volumeDb) - kZeroDbPos) * travel < kFaderSnapPx)
            now.volumeDb = 0;
        pane.holdLane(s, now);
    }
    void commit() override {
        if (now != start)
            pane.setLane(s, now, laneName(s) + " volume " + audioDraw::dbText(now.volumeDb));
        pane.clearHeld();
        pane.context().module()->releaseDrumInputHolds();
    }
    void cancel() override {
        pane.clearHeld();
        pane.context().module()->releaseDrumInputHolds();
    }
    std::string readout() const override { return laneName(s) + " " + audioDraw::dbText(now.volumeDb); }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    MixerPane& pane;
    Vec press;
    MixerPane::Strip s;
    float travel = 100;
    Channel start, now;
};

/// Drags an effect loop strip's fader: its Mix, 0..100% up the travel. Relative; Shift or Ctrl/Cmd for fine.
class MixGesture : public DragGesture {
  public:
    MixGesture(MixerPane& pane, int strip, Vec press) : press(press) {
        const MixerPane::Strip& s = pane.layoutStrips()[size_t(strip)];
        module = s.inputModule;
        label = s.label;
        travel = s.knob ? kKnobTravel : std::max(10.f, s.fader.size.y);
        start = now = sources::mixOf(module);
    }
    void update(Vec pos, Modifiers mods) override {
        float scale = mods.shift || mods.ctrl ? kFine : 1.f;
        now = std::clamp(start - (pos.y - press.y) / travel * scale, 0.f, 1.f);
        sources::previewMix(module, now);
    }
    void commit() override { sources::commitMix(module, now); }
    void cancel() override { sources::previewMix(module, std::nullopt); }
    std::string readout() const override { return label + " " + sources::mixText(now); }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    Vec press;
    int64_t module = -1;
    std::string label;
    float travel = 100;
    float start = 1, now = 1;
};

/// Drags an input strip by its name to move its module in the track's chain. A press without a move selects the
/// track, as a click on the name does.
class ReorderGesture : public DragGesture {
  public:
    ReorderGesture(MixerPane& pane, int strip, Vec press) : pane(pane), strip(strip), press(press) {
        s = pane.layoutStrips()[size_t(strip)];
    }
    void update(Vec pos, Modifiers mods) override {
        moved |= std::abs(pos.x - press.x) > 4;
        at = pos.x;
        pane.showReorder(moved ? strip : -1, at);
    }
    void commit() override {
        pane.showReorder(-1, 0);
        TesseraModule* m = pane.context().module();
        if (!m)
            return;
        if (!moved) {
            if (m->song().findTrack(s.track))
                pane.context().state().selectedTrack = s.track;
            return;
        }
        sources::moveInChain(*m, s.track, s.inputModule, pane.reorderTarget(strip, at));
    }
    void cancel() override { pane.showReorder(-1, 0); }
    std::string readout() const override { return moved ? "Move " + s.label + " in the chain" : ""; }
    CursorShape cursor() const override { return moved ? CursorShape::ResizeH : CursorShape::Hand; }

  private:
    MixerPane& pane;
    int strip;
    Vec press;
    MixerPane::Strip s;
    bool moved = false;
    float at = 0;
};

/// Drags a pan knob up/down or sideways: relative, Shift or Ctrl/Cmd for fine.
class PanGesture : public DragGesture {
  public:
    PanGesture(MixerPane& pane, EditorContext& ctx, int strip, Vec press) : pane(pane), ctx(ctx), press(press) {
        const MixerPane::Strip& s = pane.layoutStrips()[size_t(strip)];
        laneStrip = s;
        track = s.track;
        groupId = s.group;
        start = pane.shownPan(s);
        volume = pane.shownVolume(s);
        now = start;
    }
    void update(Vec pos, Modifiers mods) override {
        float scale = mods.shift || mods.ctrl ? kFine : 1.f;
        float d = ((pos.x - press.x) - (pos.y - press.y)) / 100 * scale;
        now = std::clamp(start + d, -1.f, 1.f);
        if (!mods.alt && std::abs(now) < 0.03f)
            now = 0;
        if (laneStrip.lane >= 0) {
            pane.holdLane(laneStrip, {volume, now});
            return;
        }
        ctx.module()->holdChannel(track, volume, now, true);
        pane.showHeld(track, volume, now);
    }
    void commit() override {
        TesseraModule* m = ctx.module();
        if (laneStrip.lane >= 0) {
            if (now != start)
                pane.setLane(laneStrip, {volume, now}, laneName(laneStrip) + " pan");
            m->releaseDrumInputHolds();
            pane.clearHeld();
            return;
        }
        if (now != start && groupId.valid())
            m->perform(setGroupChannel(m->song(), groupId, {volume, now}, "Group pan"));
        else if (now != start)
            m->perform(setChannel(m->song(), track, {volume, now}, "Pan"));
        m->releaseChannelHolds();
        pane.clearHeld();
    }
    void cancel() override {
        ctx.module()->audioPlayer().releaseHolds();
        pane.clearHeld();
    }
    std::string readout() const override {
        if (now == 0)
            return "Pan centre";
        char text[32];
        snprintf(text, sizeof text, "Pan %.0f%% %s", double(std::abs(now) * 100), now < 0 ? "left" : "right");
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    MixerPane& pane;
    EditorContext& ctx;
    Vec press;
    TrackId track;
    GroupId groupId;
    MixerPane::Strip laneStrip;
    float start = 0, now = 0, volume = 0;
};

/// Drags a send: a send knob up/down, or in Show sends a strip's fader. Relative; Shift or Ctrl/Cmd for fine; a soft
/// detent at 0 dB. It plays at once and is one undo step on release; a fader dragged up from -inf adds the send.
/// Middle-drag, or Space and drag: grabs the strips and moves them sideways, as in the other panes.
class ScrollGesture : public DragGesture {
  public:
    ScrollGesture(MixerPane& pane, Vec press) : pane(pane), last(press) {}
    void update(Vec pos, Modifiers mods) override {
        pane.scrollBy(last.x - pos.x);
        last = pos;
    }
    void commit() override {}
    void cancel() override {}
    CursorShape cursor() const override { return CursorShape::Hand; }

  private:
    MixerPane& pane;
    Vec last;
};

class SendGesture : public DragGesture {
  public:
    /// `sideways`: a send row's bar, dragged left and right; otherwise up and down (a dot, or a fader in Show sends).
    SendGesture(MixerPane& pane, EditorContext& ctx, const MixerPane::Strip& s, TrackId to, Vec press, float travel,
                bool sideways = false)
        : pane(pane), ctx(ctx), from(s.track), to(to), press(press), travel(travel), sideways(sideways) {
        start = now = pane.shownSend(s, to);
    }
    void update(Vec pos, Modifiers mods) override {
        float scale = mods.shift || mods.ctrl ? kFine : 1.f;
        float moved = sideways ? pos.x - press.x : press.y - pos.y;
        float p = MixerPane::faderPos(start) + moved / travel * scale;
        now = MixerPane::faderDb(std::clamp(p, 0.f, 1.f));
        if (!mods.alt && std::abs(MixerPane::faderPos(now) - kZeroDbPos) * travel < kFaderSnapPx)
            now = 0;
        ctx.module()->audioPlayer().holdSend(from.value, to.value, dbToGain(now));
        pane.showHeldSend(from, to, now);
    }
    void commit() override {
        TesseraModule* m = ctx.module();
        const Song& song = m->song();
        if (now != start) {
            if (const Send* s = findSend(song, from, to))
                m->perform(setSend(song, from, {to, now, s->preFader}, "Send " + audioDraw::dbText(now)));
            else if (now > kSilentDb)
                m->perform(addSend(song, from, to, now));
        }
        m->releaseChannelHolds();
        pane.clearHeld();
    }
    void cancel() override {
        ctx.module()->audioPlayer().releaseHolds();
        pane.clearHeld();
    }
    std::string readout() const override {
        const Track* t = ctx.module()->song().findTrack(to);
        return "Send to " + (t ? t->name : std::string("track")) + " " + audioDraw::dbText(now);
    }
    CursorShape cursor() const override { return sideways ? CursorShape::ResizeH : CursorShape::ResizeV; }

  private:
    MixerPane& pane;
    EditorContext& ctx;
    TrackId from, to;
    Vec press;
    float travel;
    bool sideways;
    float start = kSilentDb, now = kSilentDb;
};

} // namespace

float MixerPane::faderPos(float db) { return faderPosOfDb(db); }

float MixerPane::faderDb(float pos) { return faderDbOfPos(pos); }

float MixerPane::tabHeight() const { return theme::metrics::kPaneHeader; }

MixerPane::MixerPane(EditorContext& ctx, bool panel) : InteractiveView(ctx), panel(panel) {
    maximizeButton = makeMaximizeButton(ctx, Pane::Detail);
    addChild(maximizeButton);
}

void MixerPane::onButton(const ButtonEvent& e) {
    if (e.action == GLFW_PRESS && ctx.module())
        ctx.state().focus = Pane::Detail; // clicking anywhere in a pane focuses it, as in the others
    InteractiveView::onButton(e);
}

void MixerPane::draw(const DrawArgs& args) {
    InteractiveView::draw(args);
    if (ctx.module() && ctx.state().focus == Pane::Detail && box.size.y > tab.size.y + 20)
        draw::roundedRectStroke(args.vg, Rect(Vec(), box.size), 0, nvgTransRGBAf(theme::colors().accent, 0.5f), 1);
}

bool MixerPane::hasSound() const {
    TesseraModule* m = ctx.module();
    return m && m->makesSound();
}

void MixerPane::releaseInputPreview() {
    if (previewedInput && ctx.module())
        sources::endInputPreview(ctx, previewedInput->first, previewedInput->second);
    previewedInput.reset();
}

void MixerPane::holdLane(const Strip& s, Channel c) {
    heldLanes[{s.track.value, s.lane}] = c;
    if (s.isInput()) {
        previewedInput = std::make_pair(s.track, s.inputModule);
        sources::previewInput(ctx, s.track, s.inputModule, c.volumeDb, c.pan);
        return;
    }
    ctx.module()->holdDrumInput(s.track, s.lane, c);
}

void MixerPane::setLane(const Strip& s, Channel c, const std::string& name) {
    TesseraModule* m = ctx.module();
    if (s.isInput()) {
        SourceLevel next = sources::inputLevel(ctx, s.track, s.inputModule);
        next.db = c.volumeDb;
        next.pan = c.pan;
        sources::commitInput(ctx, s.track, s.inputModule, next, name);
        return;
    }
    m->perform(setDrumInput(m->song(), s.track, s.lane, c, name));
}

float MixerPane::shownVolume(const Strip& s) const {
    TesseraModule* m = ctx.module();
    if (s.group.valid()) {
        auto it = held.find(s.group.value);
        if (it != held.end())
            return it->second.first;
        const TrackGroup* g = m->song().findGroup(s.group);
        return g ? g->channel.volumeDb : 0;
    }
    if (s.lane >= 0) {
        auto held = heldLanes.find({s.track.value, s.lane});
        if (held != heldLanes.end())
            return held->second.volumeDb;
        const Track* t = m->song().findTrack(s.track);
        if (s.isInput())
            return sources::inputLevel(ctx, s.track, s.inputModule).db;
        return t ? t->drumInputs[size_t(s.lane)].volumeDb : 0;
    }
    uint64_t key = s.master ? m->song().master.id.value : s.track.value;
    auto it = held.find(key);
    if (it != held.end())
        return it->second.first;
    // A drag on this track's control in the track headers moves the strip too.
    float dragged;
    if (m->audioPlayer().held(key, false, dragged))
        return gainToDb(dragged);
    // A fader lane playing moves the fader with it (§7.3).
    float automated;
    if (s.master)
        return m->audioPlayer().masterAutomated(automated) ? automated : m->song().master.volumeDb;
    if (m->audioPlayer().automated(s.playerIndex, false, automated))
        return automated;
    const Track* t = m->song().findTrack(s.track);
    return t ? t->channel.volumeDb : 0;
}

float MixerPane::shownPan(const Strip& s) const {
    if (s.group.valid()) {
        auto it = held.find(s.group.value);
        if (it != held.end())
            return it->second.second;
        const TrackGroup* g = ctx.module()->song().findGroup(s.group);
        return g ? g->channel.pan : 0;
    }
    if (s.lane >= 0) {
        auto held = heldLanes.find({s.track.value, s.lane});
        if (held != heldLanes.end())
            return held->second.pan;
        const Track* t = ctx.module()->song().findTrack(s.track);
        if (s.isInput())
            return sources::inputLevel(ctx, s.track, s.inputModule).pan;
        return t ? t->drumInputs[size_t(s.lane)].pan : 0;
    }
    auto it = held.find(s.track.value);
    if (it != held.end())
        return it->second.second;
    float dragged;
    if (ctx.module()->audioPlayer().held(s.track.value, true, dragged))
        return dragged;
    float automated;
    if (ctx.module()->audioPlayer().automated(s.playerIndex, true, automated))
        return automated;
    const Track* t = ctx.module()->song().findTrack(s.track);
    return t ? t->channel.pan : 0;
}

float MixerPane::shownSend(const Strip& s, TrackId to) const {
    auto it = heldSends.find({s.track.value, to.value});
    if (it != heldSends.end())
        return it->second;
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(s.track) : nullptr;
    if (!t)
        return kSilentDb;
    for (size_t k = 0; k < t->sends.size(); k++)
        if (t->sends[k].to == to) {
            // A Send lane playing moves the send with it.
            float db;
            if (m->audioPlayer().sendAutomated(s.playerIndex, int(k), db))
                return db;
            return t->sends[k].db;
        }
    return kSilentDb;
}

float MixerPane::capPos(const Strip& s) const { return faderPos(shownVolume(s)); }

std::optional<NVGcolor> MixerPane::laneColor(const Strip& s, bool pan) const {
    if (s.isInput())
        return std::nullopt;
    // The ring shows whenever the track has the lane, playing or not: the control belongs to it.
    const Song& song = ctx.module()->song();
    const Track* t = s.master ? nullptr : song.findTrack(s.track);
    if (!t && !s.master)
        return std::nullopt;
    for (const AutomationEnvelope& l : t ? t->automation : song.master.automation)
        if (l.drive == (pan ? LaneDrive::Pan : LaneDrive::Volume) && !l.mute)
            return theme::track(l.color ? l.color : t ? t->color : theme::kMasterColor);
    return std::nullopt;
}

void MixerPane::layout() {
    strips.clear();
    notesOnly = 0;
    TesseraModule* m = ctx.module();
    float tabH = tabHeight();
    tab = Rect(0, 0, box.size.x, tabH);
    float button = std::min(kMaximizeButton, tabH - 2);
    maximizeButton->box = Rect(box.size.x - button - 3, (tabH - button) / 2, button, button);
    if (!m)
        return;
    float top = tabH + kGap, bottom = box.size.y - kGap;
    float h = bottom - top;
    bool knobs = h < kKnobStripsBelow;
    // Zoom widens or narrows every strip, as Ctrl+wheel widens time in the piano roll.
    float stripWidth = std::round((knobs ? kKnobStripWidth : kStripWidth) * zoom);
    float laneWidth = std::round((knobs ? kKnobLaneWidth : kLaneStripWidth) * zoom);
    float masterWidth = std::round((knobs ? kKnobStripWidth : kMasterWidth) * zoom);
    bool roomy = h >= 190; // short panes drop the source chip and pan, keeping the fader usable
    // Every strip keeps the same room for sends, so the faders stay level.
    bool anyReceives = false;
    int mostSends = 0;
    for (const Track& t : m->song().tracks) {
        if (m->soundIndex(t.id) < 0)
            continue;
        anyReceives |= !sendersTo(m->song(), t.id).empty();
        mostSends = std::max(mostSends, int(std::min<size_t>(t.sends.size(), kMaxSendsPerTrack)));
    }
    // Sends as named rows with a level bar while the faders keep enough travel; otherwise as coloured dots.
    float rowsBlock = mostSends > 0 ? kSendsLabel + float(mostSends) * kSendRow : 0;
    sendDots = mostSends > 0 && h - kStripFixed - rowsBlock < kSendRowsMinTravel;
    float sendBlock = !sendDots ? rowsBlock : float((mostSends + kDotsPerRow - 1) / kDotsPerRow) * kDotRow + 2;
    auto partsOf = [&](Strip& s, float x, float w) {
        s.box = Rect(x, top, w, h);
        if (knobs) {
            s.knob = true;
            bool buttons = !s.master && h >= 62;
            s.name = Rect(x, top + 3, w, 13);
            if (buttons) {
                s.mute = Rect(x + 3, top + h - 15, w / 2 - 4, 13);
                s.solo = Rect(x + w / 2 + 1, top + h - 15, w / 2 - 4, 13);
            }
            float from = top + 17, room = h - 17 - (buttons ? 17 : 3);
            if (h >= kKnobsStackFrom) {
                // One above the other, the level taking most of the height, spaced evenly so no gap collects.
                float size = std::clamp(room * 0.56f, 24.f, std::min(w - 12, 50.f));
                float small = s.master ? 0 : size * 0.72f, text = room >= 90 ? 12 : 0;
                float gap = (room - size - text - small) / (s.master ? 2 : 3);
                float y = from + gap;
                s.fader = Rect(x + (w - size) / 2, y, size, size);
                if (text > 0)
                    s.readout = Rect(x + 2, y + size, w - 4, text);
                if (!s.master)
                    s.pan = Rect(x + (w - small) / 2, y + size + text + gap, small, small);
                return;
            }
            // Side by side: the two knobs share the width, so a narrow strip takes smaller ones.
            float size = std::min(std::clamp(h - 34, 18.f, 30.f), (w - 8) * 0.57f), small = size * 0.75f;
            float y = from + std::max(0.f, (room - size) / 2);
            s.fader = Rect(x + 3, y, size, size);
            if (!s.master)
                s.pan = Rect(x + w - 3 - small, y + (size - small) / 2, small, small);
            return;
        }
        float y = top + 3;
        s.name = Rect(x, y, w, 16);
        y += 18;
        if (anyReceives && roomy) {
            s.receives = Rect(x + 4, y - 3, w - 8, 12);
            y += 10;
        }
        if (roomy) {
            s.chip = Rect(x + 3, y, w - 6, 14);
            y += 18;
            if (!s.master && s.lane < 0) {
                const Track* t = m->song().findTrack(s.track);
                size_t n = t ? std::min<size_t>(t->sends.size(), kMaxSendsPerTrack) : 0;
                float first = sendDots ? y : y + kSendsLabel;
                for (size_t k = 0; k < n; k++)
                    s.sendKnobs.push_back(sendDots ? Rect(x + 6 + float(k % kDotsPerRow) * 9,
                                                          first + float(k / kDotsPerRow) * kDotRow, 7, 7)
                                                   : Rect(x + 4, first + float(k) * kSendRow, w - 8, kSendRow - 2));
                s.addSend = Rect(x + 4, y + sendBlock, w - 8, 12);
            }
            y += sendBlock + 16;
            s.pan = Rect(x + w / 2 - 12, y, 24, 24);
            y += 28;
        }
        if (!s.master) {
            s.mute = Rect(x + 4, y, w / 2 - 6, 14);
            s.solo = Rect(x + w / 2 + 2, y, w / 2 - 6, 14);
        } else {
            s.clipLight = Rect(x + 4, y, w - 8, 14);
        }
        y += 18;
        float faderBottom = bottom - 34;
        s.fader = Rect(x + w / 2 - 14, y + 4, 12, std::max(20.f, faderBottom - y - 8));
        s.meter = Rect(x + w / 2 + 2, y + 4, 9, s.fader.size.y);
        s.readout = Rect(x + 2, faderBottom, w - 4, 14);
        if (!s.master && s.lane < 0) {
            if (!sendersTo(m->song(), s.track).empty())
                s.outMeter = Rect(s.fader.pos.x - 8, s.fader.pos.y, 3, s.fader.size.y);
            if (!s.group.valid())
                s.destination = Rect(x + 2, s.readout.getBottom() + 1, w - 4, 14);
        }
    };
    // A track's (or the master's) inputs in the order of its chain: the clips or the mix first, then its Track Outs.
    // An effect loop's fader is its Mix; it has no pan or solo, and neither has the master's mix.
    auto inputStrips = [&](TrackId track, float from) {
        std::vector<Strip> out;
        bool master = track == m->song().master.id;
        const Track* t = m->song().findTrack(track);
        int number = 0, adds = 0;
        std::vector<OutputModuleBase*> inputs = m->sourceModules(track);
        for (OutputModuleBase* o : inputs)
            adds += o->isLoop() ? 0 : 1;
        auto add = [&](OutputModuleBase* o, const std::string& label) {
            Strip input;
            input.track = track;
            input.lane = Strip::kInputStrip + number++;
            input.inputModule = o ? o->id : -1;
            input.loop = o && o->isLoop();
            input.label = label;
            input.playerIndex = -1;
            partsOf(input, from, laneWidth);
            input.chip = Rect();
            // An effect's Mix is a dry to wet bar with its two ends named, so it never reads as a volume fader.
            if (input.loop && input.fader.size.y >= kMixBarMinTravel) {
                input.fader.pos.y += kMixBarLabel;
                input.fader.size.y -= 2 * kMixBarLabel;
                input.meter.pos.y = input.fader.pos.y;
                input.meter.size.y = input.fader.size.y;
            }
            if (input.loop || master) {
                input.pan = Rect();
                input.solo = Rect();
            }
            out.push_back(input);
            from += laneWidth + 1;
        };
        if (master)
            add(nullptr, "Tracks");
        else if (t && t->type == TrackType::Audio)
            add(nullptr, "Clips");
        bool drum = t && t->type == TrackType::Drum;
        int addNumber = 0;
        for (OutputModuleBase* o : inputs) {
            if (drum && !o->isLoop())
                continue;
            if (o->isLoop()) {
                add(o, sources::chainLabel(o, "FX"));
                continue;
            }
            addNumber++;
            add(o, adds > 1 ? "IN " + std::to_string(addNumber) : "IN");
        }
        return out;
    };
    // The master and its inputs stay at the right; the tracks scroll sideways under the rest of the width. As
    // everywhere, the master comes first and its inputs open to its right.
    TrackId masterId = m->song().master.id;
    bool masterInputs = hasInputStrips(masterId) && !ctx.state().foldedInputs.count(masterId.value);
    size_t masterInputCount = masterInputs ? m->sourceModules(masterId).size() + 1 : 0;
    float masterInputsWidth = float(masterInputCount) * (laneWidth + 1);
    float masterX = box.size.x - kGap - masterInputsWidth - masterWidth;
    scrollRight = std::max(0.f, masterX - kGap - 2);
    scrollX = std::clamp(scrollX, 0.f, std::max(0.f, contentWidth - scrollRight));
    const float foldWidth = 11;
    // The arrow left of a strip's name, for strips with strips of their own under them.
    auto addFold = [&](Strip& s) {
        s.fold = Rect(s.name.pos.x + 1, s.name.pos.y, foldWidth, s.name.size.y);
        s.name.pos.x += foldWidth;
        s.name.size.x -= foldWidth;
    };
    float x = kGap - scrollX;
    // A parent comes first and what it folds opens to its right: a group's strip, then its tracks; a track's strip,
    // then its lanes and inputs. A frame in the parent's colour holds them together.
    std::set<uint64_t> groupsPlaced;
    int openGroup = -1; // the strip index of the group whose tracks are being laid out
    for (const Track& t : m->song().tracks) {
        // Tracks that make sound have strips (§7.9): audio tracks, and any track whose IN brings audio back.
        int index = m->soundIndex(t.id);
        if (t.type != TrackType::Audio && index < 0) {
            notesOnly++;
            continue;
        }
        const TrackGroup* group = groupOf(m->song(), t.id);
        bool groupStrip = group && m->groupIndex(group->id) >= 0;
        if (!groupStrip)
            openGroup = -1;
        if (groupStrip && !groupsPlaced.count(group->id.value)) {
            groupsPlaced.insert(group->id.value);
            Strip gs;
            gs.group = group->id;
            gs.track = TrackId(group->id.value);
            gs.playerIndex = m->groupIndex(group->id);
            partsOf(gs, x, stripWidth);
            addFold(gs);
            gs.solo = Rect();
            gs.mute = Rect(gs.box.pos.x + 4, gs.mute.pos.y, stripWidth - 8, gs.mute.size.y);
            gs.sendKnobs.clear();
            gs.addSend = gs.receives = gs.outMeter = Rect();
            strips.push_back(gs);
            openGroup = int(strips.size()) - 1;
            x += stripWidth + kGap;
        }
        // A folded group shows only its own strip.
        if (groupStrip && ctx.state().foldedMixerGroups.count(group->id.value))
            continue;
        Strip s;
        s.track = t.id;
        s.playerIndex = index;
        partsOf(s, x, stripWidth);
        std::vector<int> lanes = t.type == TrackType::Drum ? patchedLanes(t) : std::vector<int>();
        if (hasInputStrips(t.id) || !lanes.empty())
            addFold(s);
        strips.push_back(s);
        size_t parent = strips.size() - 1;
        x += stripWidth + kGap;
        bool open = !ctx.state().foldedInputs.count(t.id.value);
        // A track with two or more inputs: a narrow strip for each, after the track's, unless folded. A drum track's
        // lanes come first: they are its sound before its chain.
        auto addInputs = [&] {
            if (open && hasInputStrips(t.id))
                for (const Strip& input : inputStrips(t.id, x - kGap + 1)) {
                    strips.push_back(input);
                    x += laneWidth + 1;
                }
        };
        if (t.type != TrackType::Drum)
            addInputs();
        // A drum track's lane INs with a cable get narrow strips of their own, right after the track's (§8.1).
        for (int k : open ? lanes : std::vector<int>()) {
            Strip lane;
            lane.track = t.id;
            lane.lane = k;
            for (const DrumLane& l : t.drumLanes)
                if (l.outputIndex == k) {
                    lane.label = l.name;
                    break;
                }
            lane.playerIndex = -1;
            partsOf(lane, x - kGap + 1, laneWidth);
            lane.mute = lane.solo = lane.chip = Rect();
            strips.push_back(lane);
            x += laneWidth + 1;
        }
        if (t.type == TrackType::Drum)
            addInputs();
        if (strips.size() - 1 > parent) {
            strips[parent].familyEnd = int(strips.size()) - 1;
            x += 2; // room for the frame before the next strip
        }
        if (openGroup >= 0)
            strips[size_t(openGroup)].familyEnd = int(strips.size()) - 1;
    }
    contentWidth = x + scrollX;
    hintX = x + 8;
    Strip master;
    master.master = true;
    master.pinned = true;
    partsOf(master, masterX, masterWidth);
    if (hasInputStrips(masterId))
        addFold(master);
    if (!knobs && !m->mainPatched())
        master.connect = Rect(master.box.pos.x + 4, master.readout.pos.y + 16, masterWidth - 8, 14);
    strips.push_back(master);
    size_t masterIndex = strips.size() - 1;
    if (masterInputs)
        for (Strip input : inputStrips(masterId, masterX + masterWidth + 1)) {
            input.pinned = true;
            strips.push_back(input);
        }
    if (strips.size() - 1 > masterIndex)
        strips[masterIndex].familyEnd = int(strips.size()) - 1;
}

std::vector<int> MixerPane::patchedLanes(const Track& t) const {
    std::vector<int> out;
    TesseraModule* m = ctx.module();
    if (!m)
        return out;
    for (int k = 0; k < kDrumInputs; k++) {
        bool patched = false;
        for (OutputModuleBase* o : Registry::get().outputsOf(m->id))
            patched |= o->boundTrack() == t.id && o->sourcePatched(k);
        if (patched)
            out.push_back(k);
    }
    return out;
}

void MixerPane::zoomAt(float x, float factor) {
    float next = std::clamp(zoom * factor, kMinZoom, kMaxZoom);
    if (next == zoom)
        return;
    // The strips scale from where the scroll starts: keep the content under `x` there.
    float content = scrollX + x;
    scrollX = std::max(0.f, content * next / zoom - x);
    zoom = next;
}

bool MixerPane::handleAction(Action action) {
    switch (action) {
    case Action::ZoomIn:
        zoomAt(0, 1.25f);
        return true;
    case Action::ZoomOut:
        zoomAt(0, 1 / 1.25f);
        return true;
    case Action::ZoomToFit:
    case Action::FitAllTracks:
        // Every strip in view: as narrow as it takes, never past the smallest zoom.
        if (contentWidth > 0 && scrollRight > 0)
            zoom = std::clamp(zoom * scrollRight / contentWidth, kMinZoom, 1.f);
        scrollX = 0;
        return true;
    case Action::GoToStart:
        scrollX = 0;
        return true;
    case Action::GoToEnd:
        scrollBy(contentWidth);
        return true;
    case Action::PageLeft:
        scrollBy(-scrollRight * 0.8f);
        return true;
    case Action::PageRight:
        scrollBy(scrollRight * 0.8f);
        return true;
    default:
        return false;
    }
}

void MixerPane::scrollBy(float px) {
    scrollX = std::clamp(scrollX + px, 0.f, std::max(0.f, contentWidth - scrollRight));
}

void MixerPane::updateTip() {
    std::string want;
    TesseraModule* m = ctx.module();
    if (m && !hasGesture() && Rect(Vec(), box.size).contains(localMouse())) {
        Hit h = hitTest(localMouse());
        const Strip* s = h.strip >= 0 ? &strips[size_t(h.strip)] : nullptr;
        const Track* t = s ? m->song().findTrack(s->track) : nullptr;
        if (t && h.part == Part::SendKnob && h.send < int(t->sends.size())) {
            const Send& send = t->sends[size_t(h.send)];
            const Track* to = m->song().findTrack(send.to);
            want = "Send to " + (to ? to->name : std::string("?")) + ": " + audioDraw::dbText(send.db) +
                   (send.preFader ? ", before the fader" : ", after the fader") + ". Drag " +
                   (sendDots ? "up or down" : "sideways") +
                   " to change it, double-click for -inf, right-click for more";
        } else if (t && h.part == Part::AddSend) {
            want = "Send this track to another one, such as a reverb return. A send follows this fader and mute; "
                   "cables in the rack don't";
        }
    }
    if (want != tipText) {
        tipText = want;
        tip.leave();
        tip.setText(want);
        if (!want.empty())
            tip.enter();
    }
    tip.step();
}

void MixerPane::step() {
    layout();
    updateTip();
    // Once a loaded patch has settled, say if files are missing (§8.7): one toast, with Find….
    if (TesseraModule* m = ctx.module())
        if (AudioStore* store = m->audioStore(); store && store->idle()) {
            std::vector<AudioSourceId> missing = audioActions::missingSources(*m);
            if (missing.size() != missingTold) {
                missingTold = missing.size();
                if (!missing.empty()) {
                    EditorContext* c = &ctx;
                    std::string text = missing.size() == 1
                                           ? "1 audio file is missing"
                                           : std::to_string(missing.size()) + " audio files are missing";
                    ctx.toast(text, "Find…", [c] { audioActions::findMissing(*c); });
                }
            }
        }
    InteractiveView::step();
}

MixerPane::Hit MixerPane::hitTest(Vec pos) const {
    Hit hit;
    if (tab.contains(pos)) {
        hit.part = Part::Tab;
        return hit;
    }
    for (size_t i = 0; i < strips.size(); i++) {
        const Strip& s = strips[i];
        if (!s.box.contains(pos) && !s.connect.contains(pos))
            continue;
        // A scrolled strip under the pinned master strips can't be reached.
        if (!s.pinned && pos.x >= scrollRight)
            continue;
        hit.strip = int(i);
        if (s.fold.size.x > 0 && s.fold.contains(pos)) {
            hit.part = Part::Fold;
            return hit;
        }
        Rect faderZone(s.fader.pos.x - 6, s.fader.pos.y - 6, s.fader.size.x + s.meter.size.x + 18, s.fader.size.y + 12);
        for (size_t k = 0; k < s.sendKnobs.size(); k++)
            if (s.sendKnobs[k].grow(Vec(3, 3)).contains(pos)) {
                hit.part = Part::SendKnob;
                hit.send = int(k);
                return hit;
            }
        if (s.connect.size.x > 0 && s.connect.contains(pos))
            hit.part = Part::Connect;
        else if (s.name.contains(pos))
            hit.part = Part::Name;
        else if (s.receives.size.x > 0 && s.receives.contains(pos) && ctx.module() &&
                 !sendersTo(ctx.module()->song(), s.track).empty())
            hit.part = Part::Receives;
        else if (s.destination.size.x > 0 && s.destination.contains(pos) && !s.isInput())
            hit.part = Part::Destination;
        else if (s.addSend.size.x > 0 && s.addSend.grow(Vec(2, 2)).contains(pos))
            hit.part = Part::AddSend;
        else if (s.chip.size.x > 0 && s.chip.contains(pos))
            hit.part = Part::Chip;
        else if (s.pan.size.x > 0 && s.pan.grow(Vec(4, 4)).contains(pos))
            hit.part = Part::Pan;
        else if (s.mute.contains(pos))
            hit.part = Part::Mute;
        else if (s.solo.contains(pos))
            hit.part = Part::Solo;
        else if (s.clipLight.size.x > 0 && s.clipLight.contains(pos))
            hit.part = Part::ClipLight;
        else if (faderZone.contains(pos))
            hit.part = Part::Fader;
        else if (s.readout.contains(pos))
            hit.part = Part::Readout;
        return hit;
    }
    return hit;
}

std::unique_ptr<DragGesture> MixerPane::startGesture(Vec pos, int button, Modifiers mods) {
    if (!ctx.module())
        return nullptr;
    if (button == GLFW_MOUSE_BUTTON_MIDDLE || (button == GLFW_MOUSE_BUTTON_LEFT && ctx.spaceHeld))
        return std::make_unique<ScrollGesture>(*this, pos);
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return nullptr;
    Hit h = hitTest(pos);
    switch (h.part) {
    case Part::SendKnob: {
        const Strip& s = strips[size_t(h.strip)];
        const Track* t = ctx.module()->song().findTrack(s.track);
        if (!t || h.send >= int(t->sends.size()))
            return nullptr;
        return std::make_unique<SendGesture>(*this, ctx, s, t->sends[size_t(h.send)].to, pos, 100, !sendDots);
    }
    case Part::Fader: {
        const Strip& s = strips[size_t(h.strip)];
        if (s.loop)
            return std::make_unique<MixGesture>(*this, h.strip, pos);
        if (s.lane >= 0)
            return std::make_unique<LaneFaderGesture>(*this, h.strip, pos);
        return std::make_unique<FaderGesture>(*this, ctx, h.strip, pos);
    }
    case Part::Name: {
        const Strip& s = strips[size_t(h.strip)];
        if (s.isInput() && s.inputModule >= 0)
            return std::make_unique<ReorderGesture>(*this, h.strip, pos);
        return nullptr;
    }
    case Part::Pan:
        return std::make_unique<PanGesture>(*this, ctx, h.strip, pos);
    default:
        return nullptr;
    }
}

void MixerPane::openTyping(const Strip& s) {
    TesseraModule* m = ctx.module();
    if (s.loop) {
        int64_t module = s.inputModule;
        rack::ui::Menu* menu = rack::createMenu();
        menu->addChild(rack::createMenuLabel(s.label + " mix (%)"));
        menu->addChild(rack::createMenuLabel("100: only the effect · 50: dry and effect in full"));
        char text[16];
        snprintf(text, sizeof text, "%.0f", double(sources::mixOf(module) * 100));
        auto* field = new MenuTextField(text, [module](const std::string& typed) {
            char* end = nullptr;
            double v = std::strtod(typed.c_str(), &end);
            if (end != typed.c_str())
                sources::commitMix(module, float(v / 100));
        });
        menu->addChild(field);
        menu->addChild(
            rack::createMenuItem("Reset to 100%", "double-click", [module] { sources::commitMix(module, 1); }));
        APP->event->setSelectedWidget(field);
        return;
    }
    if (s.lane >= 0) {
        Strip lane = s;
        float pan = shownPan(s);
        typingMenu(ctx, laneName(s) + " volume (dB)", shownVolume(s), [this, lane, pan](float db) {
            setLane(lane, {db, pan}, laneName(lane) + " volume " + audioDraw::dbText(db));
        });
        return;
    }
    if (s.group.valid()) {
        GroupId group = s.group;
        float pan = shownPan(s);
        typingMenu(ctx, "Group volume (dB)", shownVolume(s), [m, group, pan](float db) {
            m->perform(setGroupChannel(m->song(), group, {db, pan}, "Group volume " + audioDraw::dbText(db)));
        });
        return;
    }
    TrackId track = s.track;
    bool master = s.master;
    float pan = master ? 0 : shownPan(s);
    EditorContext* c = &ctx;
    TrackId automated = master ? m->song().master.id : track;
    std::function<void()> automate = [c, automated] { trackActions::automateMix(*c, automated, false); };
    typingMenu(
        ctx, master ? "Master volume (dB)" : "Volume (dB)", shownVolume(s),
        [m, track, master, pan](float db) {
            if (master)
                m->perform(setMasterVolume(m->song(), db));
            else
                m->perform(setChannel(m->song(), track, {db, pan}, "Volume " + audioDraw::dbText(db)));
        },
        automate);
}

bool MixerPane::hasInputStrips(TrackId track) const {
    TesseraModule* m = ctx.module();
    if (!m)
        return false;
    if (track == m->song().master.id)
        return !m->sourceModules(track).empty();
    const Track* t = m->song().findTrack(track);
    if (!t)
        return false;
    std::vector<OutputModuleBase*> inputs = m->sourceModules(track);
    // An effect loop always gets its strip, for its Mix, even as the track's only input (a send return). A drum
    // track's lanes have strips of their own, so only a loop on its kit adds one.
    bool loop = std::any_of(inputs.begin(), inputs.end(), [](OutputModuleBase* o) { return o->isLoop(); });
    if (loop || t->type == TrackType::Drum)
        return loop;
    return inputs.size() + (t->type == TrackType::Audio ? 1 : 0) >= 2;
}

int64_t MixerPane::reorderTarget(int strip, float x) const {
    if (strip < 0 || strip >= int(strips.size()))
        return -1;
    const Strip& moving = strips[size_t(strip)];
    for (size_t i = 0; i < strips.size(); i++) {
        const Strip& o = strips[i];
        if (int(i) == strip || !o.isInput() || o.inputModule < 0 || o.track != moving.track)
            continue;
        if (x < o.box.getCenter().x)
            return o.inputModule;
    }
    return -1;
}

void MixerPane::openChipMenu(const Strip& s) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    const Song& song = m->song();
    TrackId track = s.master ? song.master.id : s.track;
    bool list = sources::needsList(sources::rowsOf(*m, track));
    bool mainMissing = s.master && !m->mainPatched();
    EditorContext* c = &ctx;
    if (list && !mainMissing) {
        sources::openList(ctx, track);
        return;
    }
    const Track* t = song.findTrack(track);
    if (!t && !s.master)
        return;
    rack::ui::Menu* menu = rack::createMenu();
    if (mainMissing) {
        menu->addChild(rack::createMenuLabel("MAIN not patched: the mix goes nowhere"));
        audioActions::appendConnectMainItem(menu, ctx);
        if (list) {
            menu->addChild(new rack::ui::MenuSeparator);
            menu->addChild(
                rack::createMenuItem("The master's sources…", "", [c, track] { sources::openList(*c, track); }));
            return;
        }
        menu->addChild(new rack::ui::MenuSeparator);
    }
    menu->addChild(rack::createMenuLabel(t ? t->name : "Master"));
    audioActions::appendOutputItems(menu, ctx, track);
}

void MixerPane::openAddSend(TrackId from) { sendActions::appendAddSendItems(rack::createMenu(), ctx, from); }

void MixerPane::openSendMenu(TrackId from, TrackId to) {
    sendActions::appendSendItems(rack::createMenu(), ctx, from, to);
}

void MixerPane::click(Vec pos, int button, Modifiers mods) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    Hit h = hitTest(pos);
    const Strip* s = h.strip >= 0 ? &strips[size_t(h.strip)] : nullptr;
    if (s && h.part == Part::Fold && button == GLFW_MOUSE_BUTTON_LEFT) {
        std::set<uint64_t>& folded = s->group.valid() ? ctx.state().foldedMixerGroups : ctx.state().foldedInputs;
        uint64_t key = s->group.valid() ? s->group.value : s->master ? m->song().master.id.value : s->track.value;
        if (!folded.erase(key))
            folded.insert(key);
        return;
    }
    if (s && h.part == Part::Chip && s->lane < 0) {
        openChipMenu(*s);
        return;
    }
    if (s && h.part == Part::SendKnob) {
        const Track* t = m->song().findTrack(s->track);
        if (t && h.send < int(t->sends.size()) && button == GLFW_MOUSE_BUTTON_RIGHT)
            openSendMenu(s->track, t->sends[size_t(h.send)].to);
        return;
    }
    if (s && h.part == Part::AddSend) {
        openAddSend(s->track);
        return;
    }
    if (s && h.part == Part::Destination) {
        rack::ui::Menu* menu = rack::createMenu();
        menu->addChild(rack::createMenuLabel("After the fader, its sound goes"));
        sendActions::appendDestinationItems(menu, ctx, s->track);
        return;
    }
    if (s && h.part == Part::Receives) {
        // "← 3": who sends here, each send's level and menu.
        sendActions::appendReceivedItems(rack::createMenu(), ctx, s->track);
        return;
    }
    if (s && s->isInput() && s->inputModule >= 0 && h.part == Part::Name && button == GLFW_MOUSE_BUTTON_RIGHT) {
        int64_t module = s->inputModule;
        TrackId track = s->track;
        rack::ui::Menu* menu = rack::createMenu();
        menu->addChild(rack::createMenuItem("Show this module in the rack", "", [m, track, module] {
            for (OutputModuleBase* o : audioActions::outputsFor(m, track))
                if (o->id == module)
                    audioActions::showInRack(o);
        }));
        // Its neighbours in the chain, as the strips show them: moving left puts it before the one on its left.
        int64_t left = -2, right = -2, afterRight = -1;
        for (size_t i = 0; i < strips.size(); i++) {
            if (int(i) != h.strip || i == 0)
                continue;
            const Strip& before = strips[i - 1];
            if (before.isInput() && before.track == track && before.inputModule >= 0)
                left = before.inputModule;
        }
        for (size_t i = size_t(h.strip) + 1; i < strips.size(); i++) {
            const Strip& next = strips[i];
            if (!next.isInput() || next.track != track || next.inputModule < 0)
                break;
            if (right == -2)
                right = next.inputModule;
            else {
                afterRight = next.inputModule;
                break;
            }
        }
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuLabel("Drag the name sideways to move it in the chain"));
        if (left != -2)
            menu->addChild(rack::createMenuItem(
                "Move left", "", [m, track, module, left] { sources::moveInChain(*m, track, module, left); }));
        if (right != -2)
            menu->addChild(rack::createMenuItem("Move right", "", [m, track, module, afterRight] {
                sources::moveInChain(*m, track, module, afterRight);
            }));
        return;
    }
    // A strip's name opens the same menu as its track header (or group header), with the mixer's own items on top;
    // the Master's name has the master's items. One thing, one menu, wherever it is clicked.
    if (s && h.part == Part::Name && button == GLFW_MOUSE_BUTTON_RIGHT && s->group.valid()) {
        GroupId group = s->group;
        const TrackGroup* g = m->song().findGroup(group);
        std::string name = g ? g->name : "";
        EditorContext* c = &ctx;
        trackActions::appendGroupMenu(rack::createMenu(), ctx, group, [c, group, name] {
            renameMenu("Rename group", name,
                       [c, group](const std::string& text) { trackActions::renameGroup(*c, group, text); });
        });
        return;
    }
    if (s && h.part == Part::Name && button == GLFW_MOUSE_BUTTON_RIGHT && s->master) {
        EditorContext* c = &ctx;
        TrackId master = m->song().master.id;
        rack::ui::Menu* menu = rack::createMenu();
        menu->addChild(rack::createMenuLabel("Master"));
        audioActions::appendConnectMainItem(menu, ctx);
        menu->addChild(rack::createMenuItem("Automate its volume in the timeline", "",
                                            [c, master] { trackActions::automateMix(*c, master, false); }));
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuLabel("An effect on everything: a Track Out on the master"));
        audioActions::appendOutputItems(menu, ctx, master);
        return;
    }
    if (s && h.part == Part::Name && button == GLFW_MOUSE_BUTTON_RIGHT && s->lane < 0) {
        TrackId track = s->track;
        EditorContext* c = &ctx;
        const std::vector<TrackId>& selected = ctx.state().selectedTracks;
        if (std::find(selected.begin(), selected.end(), track) == selected.end())
            trackActions::click(ctx, track, false, false);
        rack::ui::Menu* menu = rack::createMenu();
        menu->addChild(rack::createMenuLabel("Mixer"));
        if (!sendersTo(m->song(), track).empty())
            menu->addChild(rack::createSubmenuItem(
                "Sends to this track", std::to_string(sendersTo(m->song(), track).size()),
                [c, track](rack::ui::Menu* sub) { sendActions::appendReceivedItems(sub, *c, track); }));
        // Extra delay this track's sound has (a slow effect): its recordings and bounces move earlier by it.
        const Track* t = m->song().findTrack(track);
        menu->addChild(new rack::ui::MenuSeparator);
        menu->addChild(rack::createMenuLabel("Latency (samples): " + std::to_string(t ? t->latencySamples : 0)));
        menu->addChild(
            new MenuTextField(t ? std::to_string(t->latencySamples) : "0", [m, track](const std::string& text) {
                char* end = nullptr;
                long n = std::strtol(text.c_str(), &end, 10);
                if (end != text.c_str())
                    if (CommandPtr set = setTrackField(m->song(), track, &Track::latencySamples,
                                                       int(std::clamp(n, 0L, 96000L)), "Set track latency"))
                        m->perform(std::move(set));
            }));
        menu->addChild(new rack::ui::MenuSeparator);
        std::string name = t ? t->name : "";
        trackActions::appendTrackMenu(menu, ctx, track, [c, track, name] {
            renameMenu("Rename track", name,
                       [c, track](const std::string& text) { trackActions::rename(*c, track, text); });
        });
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (s && (h.part == Part::Fader || h.part == Part::Readout))
            openTyping(*s);
        else if (s && h.part == Part::Pan && s->lane >= 0) {
            rack::ui::Menu* menu = rack::createMenu();
            Strip lane = *s;
            float volume = shownVolume(*s);
            menu->addChild(rack::createMenuLabel(laneName(lane) + " pan"));
            menu->addChild(rack::createMenuItem("Centre", "double-click", [this, lane, volume] {
                setLane(lane, {volume, 0}, laneName(lane) + " pan centre");
            }));
        } else if (s && h.part == Part::Pan && !s->group.valid()) {
            TrackId track = s->track;
            float volume = shownVolume(*s);
            rack::ui::Menu* menu = rack::createMenu();
            EditorContext* c = &ctx;
            menu->addChild(rack::createMenuItem("Automate in the timeline", "",
                                                [c, track] { trackActions::automateMix(*c, track, true); }));
            menu->addChild(rack::createMenuLabel("Pan"));
            menu->addChild(rack::createMenuItem("Centre", "double-click", [m, track, volume] {
                m->perform(setChannel(m->song(), track, {volume, 0}, "Pan centre"));
            }));
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    switch (h.part) {
    case Part::Name:
        if (s && !s->master && !s->group.valid() && m->song().findTrack(s->track))
            ctx.state().selectedTrack = s->track;
        break;
    case Part::Mute:
        if (s->isInput()) {
            SourceLevel next = sources::inputLevel(ctx, s->track, s->inputModule);
            next.use = !next.use;
            sources::commitInput(ctx, s->track, s->inputModule, next, next.use ? "Unmute input" : "Mute input");
        } else if (s->group.valid()) {
            if (const TrackGroup* g = m->song().findGroup(s->group))
                m->perform(setGroupMute(m->song(), s->group, !g->mute));
        } else {
            trackActions::toggleMute(ctx, s->track);
        }
        break;
    case Part::Solo:
        if (s->isInput()) {
            size_t undoFrom = APP->history->actions.size();
            SourceLevel next = sources::inputLevel(ctx, s->track, s->inputModule);
            next.solo = !next.solo;
            sources::commitInput(ctx, s->track, s->inputModule, next, next.solo ? "Solo input" : "Unsolo input");
            // Hearing one input alone means hearing its track too; the last input to let go lets the track go.
            const Track* soloed = m->song().findTrack(s->track);
            bool wanted = sources::anyInputSolo(ctx, s->track);
            if (soloed && soloed->solo != wanted)
                trackActions::toggleSolo(ctx, s->track, true);
            joinUndoSteps(undoFrom, next.solo ? "Solo input" : "Unsolo input");
            break;
        }
        trackActions::toggleSolo(ctx, s->track, mods.ctrl || mods.shift);
        break;
    case Part::ClipLight:
        m->audioPlayer().clearClip();
        break;
    case Part::Connect:
        ctx.toast(audioActions::addAudioModuleForMain(m));
        break;
    default:
        break;
    }
}

void MixerPane::doubleClick(Vec pos, Modifiers mods) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    Hit h = hitTest(pos);
    if (h.strip < 0)
        return;
    const Strip& s = strips[size_t(h.strip)];
    // A double-click on a send sets it to -inf (§7.3).
    TrackId sendTo;
    if (h.part == Part::SendKnob) {
        const Track* t = m->song().findTrack(s.track);
        if (t && h.send < int(t->sends.size()))
            sendTo = t->sends[size_t(h.send)].to;
    }
    if (sendTo.valid()) {
        if (const Send* send = findSend(m->song(), s.track, sendTo))
            m->perform(setSend(m->song(), s.track, {sendTo, kSilentDb, send->preFader}, "Send -inf"));
        return;
    }
    if (s.group.valid()) {
        if (h.part == Part::Fader)
            m->perform(setGroupChannel(m->song(), s.group, {0, shownPan(s)}, "Group volume 0 dB"));
        else if (h.part == Part::Pan)
            m->perform(setGroupChannel(m->song(), s.group, {shownVolume(s), 0}, "Group pan centre"));
        return;
    }
    if (s.loop) {
        if (h.part == Part::Fader)
            sources::commitMix(s.inputModule, 1);
        else if (h.part == Part::Readout)
            openTyping(s);
        return;
    }
    if (s.lane >= 0) {
        if (h.part == Part::Fader)
            setLane(s, {0, shownPan(s)}, laneName(s) + " volume 0 dB");
        else if (h.part == Part::Pan)
            setLane(s, {shownVolume(s), 0}, laneName(s) + " pan centre");
        return;
    }
    if (h.part == Part::Fader) {
        if (s.master)
            m->perform(setMasterVolume(m->song(), 0));
        else
            m->perform(setChannel(m->song(), s.track, {0, shownPan(s)}, "Volume 0 dB"));
    } else if (h.part == Part::Pan) {
        m->perform(setChannel(m->song(), s.track, {shownVolume(s), 0}, "Pan centre"));
    } else if (h.part == Part::Readout) {
        openTyping(s);
    }
}

void MixerPane::wheel(Vec pos, Vec delta, Modifiers mods) {
    TesseraModule* m = ctx.module();
    Hit h = hitTest(pos);
    // Sideways as in the other panes: a trackpad's sideways swipe, Shift+wheel anywhere, and a plain wheel over
    // anything that isn't a level. On a level the wheel turns it, Ctrl+wheel in fine steps.
    bool level = h.part == Part::SendKnob || h.part == Part::Fader;
    if (mods.ctrl && delta.y != 0) {
        // Ctrl+wheel zooms, the spot under the mouse staying put, as in the piano roll.
        zoomAt(pos.x, delta.y > 0 ? 1.15f : 1 / 1.15f);
        return;
    }
    if (delta.x != 0 || mods.shift || (delta.y != 0 && !level)) {
        ctx.touchNavigation();
        scrollBy(-(delta.x + delta.y));
        return;
    }
    mods.shift = mods.alt; // on a level, Alt+wheel takes fine steps
    if (m && h.part == Part::SendKnob && delta.y != 0) {
        const Strip& s = strips[size_t(h.strip)];
        const Track* t = m->song().findTrack(s.track);
        if (!t || h.send >= int(t->sends.size()))
            return;
        Send send = t->sends[size_t(h.send)];
        float step = (mods.shift ? kFine : kWheelDb) * (delta.y > 0 ? 1.f : -1.f);
        send.db = send.db <= kSilentDb ? (delta.y > 0 ? -60.f : kSilentDb)
                                       : std::clamp(send.db + step, kSilentDb, kMaxFaderDb);
        m->perform(setSend(m->song(), s.track, send, "Send " + audioDraw::dbText(send.db)));
        return;
    }
    if (!m || h.part != Part::Fader || delta.y == 0)
        return;
    const Strip& s = strips[size_t(h.strip)];
    if (s.loop) {
        float mix = sources::mixOf(s.inputModule) + (mods.shift ? 0.01f : 0.05f) * (delta.y > 0 ? 1.f : -1.f);
        sources::commitMix(s.inputModule, std::clamp(mix, 0.f, 1.f));
        return;
    }
    float step = (mods.shift ? kFine * 1 : kWheelDb) * (delta.y > 0 ? 1.f : -1.f);
    float now = shownVolume(s);
    float next = now <= kSilentDb ? (delta.y > 0 ? -60.f : kSilentDb) : std::clamp(now + step, kSilentDb, kMaxFaderDb);
    if (s.lane >= 0)
        setLane(s, {next, shownPan(s)}, laneName(s) + " volume " + audioDraw::dbText(next));
    else if (s.master)
        m->perform(setMasterVolume(m->song(), next));
    else
        m->perform(setChannel(m->song(), s.track, {next, shownPan(s)}, "Volume " + audioDraw::dbText(next)));
}

CursorShape MixerPane::hoverCursor(Vec pos, Modifiers mods) {
    switch (hitTest(pos).part) {
    case Part::SendKnob:
        return sendDots ? CursorShape::ResizeV : CursorShape::ResizeH;
    case Part::Fader:
    case Part::Pan:
        return CursorShape::ResizeV;
    case Part::Receives:
    case Part::AddSend:
    case Part::Destination:
    case Part::Fold:
        return CursorShape::Hand;
    case Part::Mute:
    case Part::Solo:
    case Part::Name:
    case Part::Chip:
    case Part::ClipLight:
    case Part::Connect:
        return CursorShape::Hand;
    default:
        return CursorShape::Arrow;
    }
}

float MixerPane::heldPeak(uint64_t key, float level) {
    Peak& p = peaks[key];
    double now = EditorContext::now();
    if (level >= p.level || now - p.at > kPeakHoldSeconds) {
        p.level = level;
        p.at = now;
    }
    return p.level;
}

void MixerPane::drawMeter(NVGcontext* vg, Rect r, float left, float right) {
    const theme::Palette& c = theme::colors();
    draw::roundedRect(vg, r, 1, theme::recessed(c, c.bg0, 0.5f));
    float barW = (r.size.x - 1) / 2;
    float levels[2] = {left, right};
    for (int side = 0; side < 2; side++) {
        float db = voltsDb(levels[side]);
        float pos = meterPos(db);
        float x = r.pos.x + side * (barW + 1);
        float y = r.getBottom() - pos * r.size.y;
        if (pos > 0) {
            nvgBeginPath(vg);
            nvgRect(vg, x, y, barW, r.getBottom() - y);
            nvgFillColor(vg, db > 0 ? c.record : db > -6 ? c.queue : c.play);
            nvgFill(vg);
        }
    }
    float zero = r.getBottom() - meterPos(0) * r.size.y;
    nvgBeginPath(vg);
    nvgRect(vg, r.pos.x - 1, zero, r.size.x + 2, 1);
    nvgFillColor(vg, c.text2);
    nvgFill(vg);
}

void MixerPane::drawTab(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    TesseraModule* m = ctx.module();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, tab.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, 1);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);
    float cy = tab.size.y / 2;
    if (!m || !hasSound())
        return;
    // A thin live meter per track in its colour: the header still shows what plays.
    float x = tabLeft + 8;
    const AudioPlayer& player = m->audioPlayer();
    for (const Strip& s : strips) {
        if (s.master || s.lane >= 0)
            continue;
        const Track* t = m->song().findTrack(s.track);
        float level = std::max(player.level(s.playerIndex, 0), player.level(s.playerIndex, 1));
        float w = 40 * meterPos(voltsDb(level));
        nvgBeginPath(vg);
        nvgRect(vg, x, cy - 1.5f, 40, 3);
        nvgFillColor(vg, theme::recessed(c, c.bg0, 0.5f));
        nvgFill(vg);
        if (w > 0) {
            nvgBeginPath(vg);
            nvgRect(vg, x, cy - 1.5f, w, 3);
            nvgFillColor(vg, theme::track(t ? t->color : 0x888888));
            nvgFill(vg);
        }
        x += 46;
        if (x > box.size.x - 260 - tabRight)
            break;
    }
    float right = maximizeButton->box.pos.x - 8 - tabRight;
    draw::TextStyle small{theme::fonts::mono(), theme::metrics::kTextRuler, c.text1,
                          NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    // The master's level lives on its strip (its meter and max light); the header keeps only the warning.
    if (m->masterSounding() && !m->mainPatched()) {
        small.color = c.queue;
        draw::text(vg, small, right, cy, "MAIN not patched");
    }
}

void MixerPane::drawGroupStrip(NVGcontext* vg, const Strip& s, const TrackGroup& g) {
    const theme::Palette& c = theme::colors();
    TesseraModule* m = ctx.module();
    // A step darker than a track's strip, so it reads as the sum of the ones before it.
    draw::roundedRect(vg, s.box, theme::metrics::kRadiusControl, theme::recessed(c, c.bg1, 0.25f));
    NVGcolor color = theme::track(g.color);
    nvgBeginPath(vg);
    nvgRect(vg, s.box.pos.x, s.box.pos.y, s.box.size.x, 3);
    nvgFillColor(vg, color);
    nvgFill(vg);
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text0,
                         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, name, s.name.getCenter().x, s.name.getCenter().y + 1,
               draw::ellipsize(vg, name, g.name.empty() ? "Group" : g.name, s.name.size.x - 4));
    draw::TextStyle small{theme::fonts::ui(), theme::metrics::kTextRuler - 1, c.text1,
                          NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    if (s.chip.size.x > 0) {
        draw::roundedRect(vg, s.chip, theme::metrics::kRadiusControl, c.bg2);
        draw::text(vg, small, s.chip.getCenter().x, s.chip.getCenter().y, "Group · " + std::to_string(g.tracks.size()));
    }
    if (s.pan.size.x > 0) {
        float pan = shownPan(s);
        Vec cc = s.pan.getCenter();
        float r = s.pan.size.y / 2 - 2;
        float at = -float(M_PI) / 2 + pan * 0.75f * float(M_PI);
        nvgBeginPath(vg);
        nvgArc(vg, cc.x, cc.y, r, 0.75f * float(M_PI), 2.25f * float(M_PI), NVG_CW);
        nvgStrokeColor(vg, theme::recessed(c, c.bg0, 0.6f));
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cc.x, cc.y);
        nvgLineTo(vg, cc.x + std::cos(at) * r, cc.y + std::sin(at) * r);
        nvgStrokeColor(vg, c.text0);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
    }
    draw::roundedRect(vg, s.mute, theme::metrics::kRadiusControl, g.mute ? c.queue : c.bg2);
    small.color = g.mute ? c.ink : c.text1;
    draw::text(vg, small, s.mute.getCenter().x, s.mute.getCenter().y, "M");
    small.color = c.text1;
    float volume = shownVolume(s);
    Rect groove(s.fader.getCenter().x - 1.5f, s.fader.pos.y, 3, s.fader.size.y);
    draw::roundedRect(vg, groove, 1.5f, theme::recessed(c, c.bg0, 0.6f));
    float capY = s.fader.getBottom() - faderPos(volume) * s.fader.size.y;
    bool heldNow = held.count(s.group.value) > 0;
    draw::roundedRect(vg, Rect(s.fader.pos.x - 2, capY - 5, s.fader.size.x + 4, 10), 2, heldNow ? c.accent : c.text1);
    const AudioPlayer& player = m->audioPlayer();
    drawMeter(vg, s.meter, player.groupLevel(s.playerIndex, 0), player.groupLevel(s.playerIndex, 1));
    draw::TextStyle mono{theme::fonts::mono(), theme::metrics::kTextRuler, c.text0,
                         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, mono, s.readout.getCenter().x, s.readout.getCenter().y, audioDraw::dbText(volume));
    small.color = c.text2;
    draw::text(vg, small, s.box.getCenter().x, s.readout.getBottom() + 8, "\xE2\x86\x92 Master");
}

void MixerPane::drawLaneStrip(NVGcontext* vg, const Strip& s, const Track* t) {
    const theme::Palette& c = theme::colors();
    // Set against its track's strip, a step darker, so the lanes read as part of the kit.
    draw::roundedRect(vg, s.box, theme::metrics::kRadiusControl, theme::recessed(c, c.bg1, 0.35f));
    const DrumLane* first = nullptr;
    if (t && !s.isInput())
        for (const DrumLane& l : t->drumLanes)
            if (l.outputIndex == s.lane) {
                first = &l;
                break;
            }
    NVGcolor color = theme::track(first && first->color ? first->color : t ? t->color : theme::kMasterColor);
    nvgBeginPath(vg);
    nvgRect(vg, s.box.pos.x, s.box.pos.y, s.box.size.x, 3);
    nvgFillColor(vg, color);
    nvgFill(vg);
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextRuler - 1, c.text0,
                         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    std::string label = s.isInput() ? s.label : first ? first->name : std::to_string(s.lane + 1);
    draw::text(vg, name, s.name.getCenter().x, s.name.getCenter().y + 1,
               draw::ellipsize(vg, name, label, s.name.size.x - 2));
    // Pan, as on a track's strip.
    if (s.pan.size.x > 0) {
        float pan = shownPan(s);
        Vec cc = s.pan.getCenter();
        float r = s.pan.size.y / 2 - 3;
        nvgBeginPath(vg);
        nvgArc(vg, cc.x, cc.y, r, float(M_PI) * 0.75f, float(M_PI) * 2.25f, NVG_CW);
        nvgStrokeColor(vg, theme::recessed(c, c.bg0, 0.6f));
        nvgStrokeWidth(vg, 2.5f);
        nvgStroke(vg);
        float at = float(M_PI) * 1.5f + pan * float(M_PI) * 0.75f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, cc.x, cc.y);
        nvgLineTo(vg, cc.x + std::cos(at) * r, cc.y + std::sin(at) * r);
        nvgStrokeColor(vg, c.text0);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
    }
    // Fader and meter: the lane's level after its fader and pan, measured by its Drum Out. An effect loop's fader is
    // its Mix, straight up the travel.
    float volume = shownVolume(s);
    float mix = s.loop ? sources::mixOf(s.inputModule) : 0;
    bool heldNow = heldLanes.count({s.track.value, s.lane}) > 0;
    if (s.loop) {
        // Filled from dry at the bottom up to the wet amount, with a thin line at the setting.
        Rect bar(s.fader.getCenter().x - 4, s.fader.pos.y, 8, s.fader.size.y);
        draw::roundedRect(vg, bar, 2, theme::recessed(c, c.bg0, 0.6f));
        float fillY = bar.getBottom() - mix * bar.size.y;
        if (mix > 0)
            draw::roundedRect(vg, Rect(bar.pos.x, fillY, bar.size.x, bar.getBottom() - fillY), 2,
                              nvgTransRGBAf(c.accent, heldNow ? 0.8f : 0.5f));
        nvgBeginPath(vg);
        nvgRect(vg, bar.pos.x - 2, std::round(fillY) - 1, bar.size.x + 4, 2);
        nvgFillColor(vg, heldNow ? c.accent : c.text0);
        nvgFill(vg);
        if (s.fader.pos.y - kMixBarLabel >= s.mute.getBottom()) {
            draw::TextStyle end{theme::fonts::ui(), theme::metrics::kTextRuler - 2, c.text2,
                                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            draw::text(vg, end, bar.getCenter().x, bar.pos.y - kMixBarLabel / 2 - 1, "wet");
            draw::text(vg, end, bar.getCenter().x, bar.getBottom() + kMixBarLabel / 2 + 1, "dry");
        }
    } else {
        Rect groove(s.fader.getCenter().x - 1.5f, s.fader.pos.y, 3, s.fader.size.y);
        draw::roundedRect(vg, groove, 1.5f, theme::recessed(c, c.bg0, 0.6f));
        float capY = s.fader.getBottom() - faderPos(volume) * s.fader.size.y;
        draw::roundedRect(vg, Rect(s.fader.pos.x - 2, capY - 5, s.fader.size.x + 4, 10), 2,
                          heldNow ? c.accent : c.text1);
    }
    float level = stripLevel(s);
    if (s.isInput()) {
        SourceLevel input = sources::inputLevel(ctx, s.track, s.inputModule);
        auto toggle = [&](const Rect& r, const char* text, bool on, NVGcolor lit) {
            if (r.size.x <= 0)
                return;
            draw::roundedRect(vg, r, 2, on ? lit : theme::recessed(c, c.bg0, 0.5f));
            draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kTextRuler - 2, on ? c.bg0 : c.text1,
                                  NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            draw::text(vg, style, r.getCenter().x, r.getCenter().y, text);
        };
        toggle(s.mute, "M", !input.use, c.queue);
        toggle(s.solo, "S", input.solo, c.accent);
    }
    drawMeter(vg, s.meter, level, level);
    draw::TextStyle mono{theme::fonts::mono(), theme::metrics::kTextRuler - 1, c.text0,
                         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    char percent[16];
    snprintf(percent, sizeof percent, "%.0f%%", double(mix * 100));
    draw::text(vg, mono, s.readout.getCenter().x, s.readout.getCenter().y,
               s.loop ? std::string(percent) : audioDraw::dbText(volume));
    // "mix" under an effect loop's percentage, so it never reads as a level.
    if (s.loop) {
        draw::TextStyle small{theme::fonts::ui(), theme::metrics::kTextRuler - 2, c.text2,
                              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, small, s.box.getCenter().x, s.readout.getBottom() + 8, "mix");
    }
    // Dragging this strip's name: a marker where it would land.
    if (reorderStrip >= 0 && reorderStrip < int(strips.size()) && &strips[size_t(reorderStrip)] == &s) {
        draw::roundedRectStroke(vg, s.box, theme::metrics::kRadiusControl, c.accent, 1);
        int64_t before = reorderTarget(reorderStrip, reorderX);
        float x = -1;
        for (const Strip& o : strips)
            if (o.isInput() && o.track == s.track && o.inputModule >= 0) {
                if (o.inputModule == before) {
                    x = o.box.pos.x - 1;
                    break;
                }
                x = o.box.getRight() + 1;
            }
        if (x >= 0) {
            nvgBeginPath(vg);
            nvgRect(vg, x - 1, s.box.pos.y, 2, s.box.size.y);
            nvgFillColor(vg, c.accent);
            nvgFill(vg);
        }
    }
}

float MixerPane::stripLevel(const Strip& s) const {
    TesseraModule* m = ctx.module();
    const AudioPlayer& player = m->audioPlayer();
    if (s.master)
        return std::max(player.masterLevel(0), player.masterLevel(1));
    if (s.group.valid())
        return std::max(player.groupLevel(s.playerIndex, 0), player.groupLevel(s.playerIndex, 1));
    if (s.lane < 0)
        return std::max(player.level(s.playerIndex, 0), player.level(s.playerIndex, 1));
    float level = 0;
    if (s.isInput() && s.inputModule < 0) {
        // The sound before the chain, which the clips' (or the mix's) Level already scaled.
        if (s.track == m->song().master.id)
            level = player.masterOutLevel();
        else if (int index = m->soundIndex(s.track); index >= 0)
            level = player.outLevel(index);
    } else if (s.isInput()) {
        for (OutputModuleBase* o : audioActions::outputsFor(m, s.track))
            if (o->id == s.inputModule)
                level = o->inputLevel();
    } else {
        for (OutputModuleBase* o : Registry::get().outputsOf(m->id))
            if (o->boundTrack() == s.track)
                level = std::max(level, o->sourceLevel(s.lane));
    }
    return level;
}

void MixerPane::drawKnobStrip(NVGcontext* vg, const Strip& s) {
    const theme::Palette& c = theme::colors();
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    const Track* t = s.master ? nullptr : song.findTrack(s.track);
    const TrackGroup* group = s.group.valid() ? song.findGroup(s.group) : nullptr;
    if (!s.master && !t && !group && !s.isInput())
        return;
    bool selected = t && s.lane < 0 && !group && ctx.state().selectedTrack == t->id;
    draw::roundedRect(vg, s.box, theme::metrics::kRadiusControl,
                      s.lane >= 0 || group ? theme::recessed(c, c.bg1, 0.35f)
                      : selected           ? nvgLerpRGBA(c.bg1, c.bg2, 0.6f)
                                           : c.bg1);
    NVGcolor color = group ? theme::track(group->color) : t ? theme::track(t->color) : c.text1;
    nvgBeginPath(vg);
    nvgRect(vg, s.box.pos.x, s.box.pos.y, s.box.size.x, 2);
    nvgFillColor(vg, color);
    nvgFill(vg);
    std::string label = s.master ? "Master" : group ? group->name : s.isInput() ? s.label : t->name;
    if (s.lane >= 0 && !s.isInput()) {
        label = std::to_string(s.lane + 1);
        for (const DrumLane& l : t->drumLanes)
            if (l.outputIndex == s.lane) {
                label = l.name;
                break;
            }
    }
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextRuler - 1, c.text0,
                         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, name, s.name.getCenter().x, s.name.getCenter().y,
               draw::ellipsize(vg, name, label, s.name.size.x - 4));

    // The level knob: its position on the outer ring, what is sounding on the ring inside it.
    float position = s.loop ? sources::mixOf(s.inputModule) : faderPos(shownVolume(s));
    float low = float(M_PI) * 0.75f, sweep = float(M_PI) * 1.5f;
    Vec center = s.fader.getCenter();
    float radius = s.fader.size.y / 2 - 1.5f;
    auto arc = [&](float r, float from, float to, NVGcolor stroke, float width) {
        nvgBeginPath(vg);
        nvgArc(vg, center.x, center.y, r, from, to, NVG_CW);
        nvgStrokeColor(vg, stroke);
        nvgStrokeWidth(vg, width);
        nvgStroke(vg);
    };
    arc(radius, low, low + sweep, theme::recessed(c, c.bg0, 0.6f), 2.5f);
    arc(radius, low, low + sweep * position, c.text1, 2.5f);
    float db = voltsDb(stripLevel(s)), meter = meterPos(db);
    arc(radius - 3.5f, low, low + sweep, theme::recessed(c, c.bg0, 0.4f), 2);
    if (meter > 0)
        arc(radius - 3.5f, low, low + sweep * meter, db > 0 ? c.record : db > -6 ? c.queue : c.play, 2);
    float at = low + sweep * position;
    nvgBeginPath(vg);
    nvgMoveTo(vg, center.x + std::cos(at) * (radius - 6), center.y + std::sin(at) * (radius - 6));
    nvgLineTo(vg, center.x + std::cos(at) * radius, center.y + std::sin(at) * radius);
    nvgStrokeColor(vg, c.text0);
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);

    if (s.pan.size.x > 0) {
        Vec cc = s.pan.getCenter();
        float r = s.pan.size.y / 2 - 2;
        nvgBeginPath(vg);
        nvgArc(vg, cc.x, cc.y, r, low, low + sweep, NVG_CW);
        nvgStrokeColor(vg, theme::recessed(c, c.bg0, 0.6f));
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);
        float pointer = float(M_PI) * 1.5f + shownPan(s) * float(M_PI) * 0.75f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, cc.x, cc.y);
        nvgLineTo(vg, cc.x + std::cos(pointer) * r, cc.y + std::sin(pointer) * r);
        nvgStrokeColor(vg, c.text0);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
    }

    if (s.readout.size.x > 0) {
        draw::TextStyle mono{theme::fonts::mono(), theme::metrics::kTextRuler - 2, c.text1,
                             NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        char percent[16];
        snprintf(percent, sizeof percent, "%.0f%%", double(position * 100));
        // The setting, as the tall strip's readout shows it; the inner ring already shows what is sounding.
        float setting = shownVolume(s);
        draw::text(vg, mono, s.readout.getCenter().x, s.readout.getCenter().y,
                   s.loop ? std::string(percent) : audioDraw::dbText(setting));
    }

    auto toggle = [&](const Rect& r, const char* text, bool on, NVGcolor lit) {
        if (r.size.x <= 0)
            return;
        draw::roundedRect(vg, r, 2, on ? lit : theme::recessed(c, c.bg0, 0.5f));
        draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kTextRuler - 2, on ? c.bg0 : c.text1,
                              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, style, r.getCenter().x, r.getCenter().y, text);
    };
    if (s.isInput()) {
        SourceLevel input = sources::inputLevel(ctx, s.track, s.inputModule);
        toggle(s.mute, "M", !input.use, c.queue);
        toggle(s.solo, "S", input.solo, c.accent);
    } else if (group) {
        toggle(s.mute, "M", group->mute, c.queue);
    } else if (t) {
        toggle(s.mute, "M", t->mute, c.queue);
        toggle(s.solo, "S", t->solo, c.accent);
    }
}

void MixerPane::drawStrip(NVGcontext* vg, const Strip& s) {
    const theme::Palette& c = theme::colors();
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    if (s.knob) {
        drawKnobStrip(vg, s);
        return;
    }
    if (s.group.valid()) {
        if (const TrackGroup* g = song.findGroup(s.group))
            drawGroupStrip(vg, s, *g);
        return;
    }
    if (s.isInput()) {
        drawLaneStrip(vg, s, song.findTrack(s.track));
        return;
    }
    const Track* t = s.master ? nullptr : song.findTrack(s.track);
    // Show sends: other strips' faders are their sends to the target (§7.6).
    if (!s.master && !t)
        return;
    bool selected = t && ctx.state().selectedTrack == t->id;
    if (s.lane >= 0) {
        drawLaneStrip(vg, s, t);
        return;
    }
    draw::roundedRect(vg, s.box, theme::metrics::kRadiusControl, selected ? nvgLerpRGBA(c.bg1, c.bg2, 0.6f) : c.bg1);
    // Colour bar and name.
    NVGcolor color = t ? theme::track(t->color) : c.text1;
    nvgBeginPath(vg);
    nvgRect(vg, s.box.pos.x, s.box.pos.y, s.box.size.x, 3);
    nvgFillColor(vg, color);
    nvgFill(vg);
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text0,
                         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    std::string label = t ? t->name : "Master";
    draw::text(vg, name, s.name.getCenter().x, s.name.getCenter().y + 1,
               draw::ellipsize(vg, name, label, s.name.size.x - 4));
    draw::TextStyle small{theme::fonts::ui(), theme::metrics::kTextRuler - 1, c.text1,
                          NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};

    // Source chip: what feeds the track, in words (§7.2).
    if (s.chip.size.x > 0) {
        ChipText ct = chipText(*m, t);
        std::string chip = ct.text;
        NVGcolor chipColor = ct.warn ? c.queue : ct.dim ? c.text2 : c.text1;
        if (s.master) {
            chip = m->mainPatched() ? "MAIN" : "MAIN not patched";
            chipColor = m->mainPatched() ? c.text1 : c.queue;
        }
        draw::roundedRect(vg, s.chip, theme::metrics::kRadiusControl, c.bg2);
        small.color = chipColor;
        draw::text(vg, small, s.chip.getCenter().x, s.chip.getCenter().y,
                   draw::ellipsize(vg, small, chip, s.chip.size.x - 4));
        small.color = c.text1;
    }
    // "← 3": this track gets sends from 3 tracks; a click lists them, with their levels.
    if (s.receives.size.x > 0 && !s.master) {
        size_t senders = sendersTo(song, s.track).size();
        if (senders > 0) {
            small.color = c.text1;
            draw::text(vg, small, s.receives.getCenter().x, s.receives.getCenter().y,
                       "\xE2\x86\x90 " + std::to_string(senders));
            small.color = c.text1;
        }
    }
    // Sends: a row each with the receiving track's full name in its colour over a thin level bar ("pre" when before
    // the fader), or in a short pane a dot each, brighter the louder. "+ Send" adds one.
    if (t) {
        if (!sendDots && !s.sendKnobs.empty()) {
            draw::TextStyle head{theme::fonts::uiStrong(), theme::metrics::kTextRuler - 3, c.text2,
                                 NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
            draw::text(vg, head, s.sendKnobs[0].pos.x, s.sendKnobs[0].pos.y - kSendsLabel / 2, "SENDS");
        }
        for (size_t k = 0; k < s.sendKnobs.size() && k < t->sends.size(); k++) {
            const Send& send = t->sends[k];
            const Track* to = song.findTrack(send.to);
            NVGcolor color = to ? theme::track(to->color) : c.text2;
            Rect r = s.sendKnobs[k];
            float db = shownSend(s, send.to);
            float pos = faderPos(db);
            bool lane = std::any_of(t->automation.begin(), t->automation.end(), [&](const AutomationEnvelope& l) {
                return l.drive == LaneDrive::Send && l.sendTo == send.to && !l.mute;
            });
            if (sendDots) {
                nvgBeginPath(vg);
                nvgCircle(vg, r.getCenter().x, r.getCenter().y, r.size.x / 2);
                nvgFillColor(
                    vg, nvgTransRGBAf(color, db <= kSilentDb ? 0.15f : 0.3f + 0.7f * std::min(1.f, pos / kZeroDbPos)));
                nvgFill(vg);
                if (lane) {
                    nvgStrokeColor(vg, color);
                    nvgStrokeWidth(vg, 1);
                    nvgStroke(vg);
                }
                continue;
            }
            draw::TextStyle name{theme::fonts::ui(), theme::metrics::kTextRuler - 2, color,
                                 NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
            float preWidth = send.preFader ? 16 : 0;
            draw::text(vg, name, r.pos.x, r.pos.y + 5,
                       draw::ellipsize(vg, name, to ? to->name : std::string("?"), r.size.x - preWidth));
            if (send.preFader) {
                draw::TextStyle pre{theme::fonts::ui(), theme::metrics::kTextRuler - 3, c.text1,
                                    NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
                draw::text(vg, pre, r.getRight(), r.pos.y + 5, "pre");
            }
            Rect bar(r.pos.x, r.getBottom() - 4, r.size.x, 3);
            draw::roundedRect(vg, bar, 1.5f, theme::recessed(c, c.bg0, 0.6f));
            if (db > kSilentDb)
                draw::roundedRect(vg, Rect(bar.pos.x, bar.pos.y, bar.size.x * pos, bar.size.y), 1.5f, color);
            // 0 dB, where a double-click on the fader would put it.
            nvgBeginPath(vg);
            nvgRect(vg, std::round(bar.pos.x + bar.size.x * kZeroDbPos), bar.pos.y - 1, 1, bar.size.y + 2);
            nvgFillColor(vg, c.text2);
            nvgFill(vg);
            if (lane)
                draw::roundedRectStroke(vg, bar.grow(Vec(1, 1)), 2, color, 1);
        }
        if (s.addSend.size.x > 0) {
            draw::TextStyle add{theme::fonts::ui(), theme::metrics::kTextRuler - 2, c.text2,
                                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            draw::text(vg, add, s.addSend.getCenter().x, s.addSend.getCenter().y, "+ Send");
        }
    }
    // Pan knob: an arc from the centre to where it points.
    if (s.pan.size.x > 0 && !s.master) {
        float pan = shownPan(s);
        Vec cc = s.pan.getCenter();
        float r = s.pan.size.y / 2 - 2;
        nvgBeginPath(vg);
        nvgArc(vg, cc.x, cc.y, r, float(M_PI) * 0.75f, float(M_PI) * 2.25f, NVG_CW);
        nvgStrokeColor(vg, theme::recessed(c, c.bg0, 0.6f));
        nvgStrokeWidth(vg, 3);
        nvgStroke(vg);
        float mid = float(M_PI) * 1.5f, at = mid + pan * float(M_PI) * 0.75f;
        if (pan != 0) {
            nvgBeginPath(vg);
            nvgArc(vg, cc.x, cc.y, r, std::min(mid, at), std::max(mid, at), NVG_CW);
            nvgStrokeColor(vg, color);
            nvgStroke(vg);
        }
        nvgBeginPath(vg);
        nvgMoveTo(vg, cc.x, cc.y);
        nvgLineTo(vg, cc.x + std::cos(at) * r, cc.y + std::sin(at) * r);
        nvgStrokeColor(vg, c.text0);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
        if (std::optional<NVGcolor> ring = laneColor(s, true)) {
            nvgBeginPath(vg);
            nvgCircle(vg, cc.x, cc.y, r + 3);
            nvgStrokeColor(vg, *ring);
            nvgStrokeWidth(vg, 1.5f);
            nvgStroke(vg);
        }
    }
    // Mute and solo, the same switches as the track rows.
    if (t) {
        auto button = [&](Rect r, const char* text, bool on, NVGcolor onColor) {
            draw::roundedRect(vg, r, theme::metrics::kRadiusControl, on ? onColor : c.bg2);
            small.color = on ? c.ink : c.text1;
            draw::text(vg, small, r.getCenter().x, r.getCenter().y, text);
        };
        button(s.mute, "M", t->mute, c.queue);
        button(s.solo, "S", t->solo, c.accent);
        small.color = c.text1;
    }
    // The master's clip light: the highest peak since it was last clicked.
    if (s.master && s.clipLight.size.x > 0) {
        float peak = m->audioPlayer().masterClipPeak();
        bool over = peak > kFullScaleVolts;
        draw::roundedRect(vg, s.clipLight, theme::metrics::kRadiusControl, over ? c.record : c.bg2);
        // "max", not "clip": in Tessera a clip is a piece of music.
        char text[24];
        if (over)
            snprintf(text, sizeof text, "over %+.1f", double(voltsDb(peak)));
        else
            snprintf(text, sizeof text, peak > 1e-5f ? "max %.1f" : "max -inf", double(voltsDb(peak)));
        small.color = over ? c.ink : c.text2;
        draw::text(vg, small, s.clipLight.getCenter().x, s.clipLight.getCenter().y, text);
        small.color = c.text1;
    }
    // Fader: a groove with the 0 dB mark, and the cap.
    float volume = shownVolume(s);
    Rect groove(s.fader.getCenter().x - 1.5f, s.fader.pos.y, 3, s.fader.size.y);
    draw::roundedRect(vg, groove, 1.5f, theme::recessed(c, c.bg0, 0.6f));
    float zeroY = s.fader.getBottom() - kZeroDbPos * s.fader.size.y;
    nvgBeginPath(vg);
    nvgRect(vg, s.fader.pos.x - 2, zeroY, s.fader.size.x + 4, 1);
    nvgFillColor(vg, volume == 0 ? c.text0 : c.text2);
    nvgFill(vg);
    float capY = s.fader.getBottom() - capPos(s) * s.fader.size.y;
    bool heldNow = held.count(s.master ? song.master.id.value : s.track.value) > 0;
    Rect cap(s.fader.pos.x - 2, capY - 5, s.fader.size.x + 4, 10);
    draw::roundedRect(vg, cap, 2, heldNow ? c.accent : c.text1);
    if (std::optional<NVGcolor> ring = laneColor(s, false))
        draw::roundedRectStroke(vg, cap.grow(Vec(2, 2)), 3, *ring, 1.5f);
    // The OUT meter of a track that receives sends: what leaves for its effect, dim while OUT has no cable.
    if (s.outMeter.size.x > 0) {
        float out = m->audioPlayer().outLevel(s.playerIndex);
        bool patched = false;
        for (OutputModuleBase* o : audioActions::outputsFor(m, s.track))
            patched |= o->outPatched();
        draw::roundedRect(vg, s.outMeter, 1, theme::recessed(c, c.bg0, 0.5f));
        float db = voltsDb(out), p = meterPos(db);
        if (p > 0) {
            NVGcolor fill = db > 0 ? c.record : db > -6 ? c.queue : c.play;
            if (!patched)
                fill = nvgTransRGBAf(fill, 0.35f);
            nvgBeginPath(vg);
            nvgRect(vg, s.outMeter.pos.x, s.outMeter.getBottom() - p * s.outMeter.size.y, s.outMeter.size.x,
                    p * s.outMeter.size.y);
            nvgFillColor(vg, fill);
            nvgFill(vg);
        }
    }
    // Meter beside it, with peak-hold ticks.
    const AudioPlayer& player = m->audioPlayer();
    float l = s.master ? player.masterLevel(0) : player.level(s.playerIndex, 0);
    float r = s.master ? player.masterLevel(1) : player.level(s.playerIndex, 1);
    drawMeter(vg, s.meter, l, r);
    uint64_t key = s.master ? 0 : s.track.value;
    float hold = heldPeak(key, std::max(l, r));
    if (hold > 1e-5f) {
        float y = s.meter.getBottom() - meterPos(voltsDb(hold)) * s.meter.size.y;
        nvgBeginPath(vg);
        nvgRect(vg, s.meter.pos.x, y, s.meter.size.x, 1);
        nvgFillColor(vg, c.text0);
        nvgFill(vg);
    }
    // The level as text; under it where the strip goes.
    draw::TextStyle mono{theme::fonts::mono(), theme::metrics::kTextRuler, c.text0,
                         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, mono, s.readout.getCenter().x, s.readout.getCenter().y, audioDraw::dbText(volume));
    small.color = c.text2;
    if (!s.master) {
        const TrackGroup* g = groupOf(song, s.track);
        bool viaGroup = g && m->groupIndex(g->id) >= 0;
        // Where it goes: a click chooses between the Master (or its group) and only its sends.
        bool toMaster = !t || t->toMaster;
        small.color = toMaster ? c.text2 : c.queue;
        draw::text(vg, small, s.box.getCenter().x, s.destination.getCenter().y,
                   draw::ellipsize(vg, small,
                                   toMaster ? "\xE2\x86\x92 " + (viaGroup ? g->name : std::string("Master"))
                                            : std::string("sends only"),
                                   s.box.size.x - 4));
    } else if (s.connect.size.x > 0) {
        draw::roundedRect(vg, s.connect, theme::metrics::kRadiusControl, c.bg2);
        small.color = c.text0;
        draw::text(vg, small, s.connect.getCenter().x, s.connect.getCenter().y,
                   audioActions::audioModuleInPatch() ? "Connect to Audio" : "Add Audio module");
    } else {
        draw::text(vg, small, s.box.getCenter().x, s.readout.getBottom() + 8, "\xE2\x86\x92 MAIN");
    }
}

void MixerPane::drawFamilies(NVGcontext* vg, bool pinned) {
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    for (size_t i = 0; i < strips.size(); i++) {
        const Strip& s = strips[i];
        if (s.pinned != pinned || s.familyEnd <= int(i) || s.familyEnd >= int(strips.size()))
            continue;
        NVGcolor color = theme::track(theme::kMasterColor);
        if (s.group.valid()) {
            for (const TrackGroup& g : song.groups)
                if (g.id == s.group)
                    color = theme::track(g.color);
        } else if (const Track* t = s.master ? nullptr : song.findTrack(s.track)) {
            color = theme::track(t->color);
        }
        const Strip& last = strips[size_t(s.familyEnd)];
        Rect frame(s.box.pos.x - 2, s.box.pos.y - 2, last.box.getRight() - s.box.pos.x + 4, s.box.size.y + 4);
        if (s.group.valid()) {
            // A group holds whole tracks: an outline round them, so their own frames still read inside it.
            draw::roundedRectStroke(vg, frame.grow(Vec(-0.5f, -0.5f)), theme::metrics::kRadiusControl + 2,
                                    nvgTransRGBAf(color, 0.7f), 1);
        } else {
            draw::roundedRect(vg, frame, theme::metrics::kRadiusControl + 2, nvgTransRGBAf(color, 0.35f));
        }
    }
}

void MixerPane::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);
    drawTab(vg);
    if (!ctx.module() || box.size.y <= tab.size.y + 20)
        return;
    nvgSave(vg);
    nvgScissor(vg, 0, tab.size.y, box.size.x, box.size.y - tab.size.y);
    if (!hasSound()) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, 12, tab.size.y + 20, "Nothing makes sound yet. Drop audio files to add audio tracks.");
        nvgRestore(vg);
        return;
    }
    float stripsTop = tab.size.y;
    nvgSave(vg);
    nvgIntersectScissor(vg, 0, stripsTop, scrollRight, box.size.y - stripsTop);
    drawFamilies(vg, false);
    for (const Strip& s : strips)
        if (!s.pinned)
            drawStrip(vg, s);
    nvgRestore(vg);
    drawFamilies(vg, true);
    for (const Strip& s : strips)
        if (s.pinned)
            drawStrip(vg, s);
    // The fold arrows: pointing down while the strips under them show, right while folded.
    for (const Strip& s : strips) {
        if (s.fold.size.x <= 0 || (!s.pinned && s.fold.getRight() > scrollRight))
            continue;
        bool open = s.group.valid() ? !ctx.state().foldedMixerGroups.count(s.group.value)
                    : s.master      ? !ctx.state().foldedInputs.count(ctx.module()->song().master.id.value)
                                    : !ctx.state().foldedInputs.count(s.track.value);
        Vec cc = s.fold.getCenter();
        nvgBeginPath(vg);
        if (open) {
            nvgMoveTo(vg, cc.x - 3.5f, cc.y - 2);
            nvgLineTo(vg, cc.x + 3.5f, cc.y - 2);
            nvgLineTo(vg, cc.x, cc.y + 2.5f);
        } else {
            nvgMoveTo(vg, cc.x - 2, cc.y - 3.5f);
            nvgLineTo(vg, cc.x + 2.5f, cc.y);
            nvgLineTo(vg, cc.x - 2, cc.y + 3.5f);
        }
        nvgClosePath(vg);
        nvgFillColor(vg, c.text1);
        nvgFill(vg);
    }
    // While the tracks don't fit, a thin bar along the bottom shows which part of them is in view.
    if (contentWidth > scrollRight + 1 && scrollRight > 40) {
        Rect track(kGap, box.size.y - 4, scrollRight - 2 * kGap, 3);
        draw::roundedRect(vg, track, 1.5f, theme::recessed(c, c.bg0, 0.5f));
        float w = track.size.x * scrollRight / contentWidth;
        float x0 = track.pos.x + (track.size.x - w) * scrollX / std::max(1.f, contentWidth - scrollRight);
        draw::roundedRect(vg, Rect(x0, track.pos.y, w, track.size.y), 1.5f, c.text2);
    }
    if (notesOnly > 0) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2,
                             NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        float x = hintX;
        std::string text =
            std::to_string(notesOnly) + (notesOnly == 1 ? " track plays" : " tracks play") + " notes only";
        draw::text(vg, hint, x, tab.size.y + 14, text);
    }
    nvgRestore(vg);
}

} // namespace tessera
