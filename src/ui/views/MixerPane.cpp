#include "MixerPane.hpp"

#include "../../model/AudioEdits.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/AudioActions.hpp"
#include "../editing/TrackActions.hpp"
#include "../interaction/ClickOrDrag.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/MenuTextField.hpp"
#include "AudioClipDraw.hpp"

#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kStripWidth = 64;
constexpr float kMasterWidth = 88;
constexpr float kGap = 4;
constexpr float kFaderSnapPx = 6;    // the soft detent at 0 dB
constexpr float kZeroDbPos = 0.75f;  // 0 dB sits at 75% of the fader's travel
constexpr double kPeakHoldSeconds = 1.5;
constexpr float kWheelDb = 0.5f;
constexpr float kFine = 0.1f;

/// A level in volts as dB of full scale (10 V).
float voltsDb(float volts) { return gainToDb(volts / kFullScaleVolts); }

/// Where a level sits on a meter running from -60 to +6 dB (0..1).
float meterPos(float db) { return std::clamp((db + 60) / 66, 0.f, 1.f); }

/// The menu for typing a level: "Type a value", then Reset.
void typingMenu(EditorContext& ctx, const std::string& title, float now, std::function<void(float)> set) {
    rack::ui::Menu* menu = rack::createMenu();
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
class FaderGesture : public DragGesture {
  public:
    FaderGesture(MixerPane& pane, EditorContext& ctx, int strip, Vec press)
        : pane(pane), ctx(ctx), press(press) {
        const MixerPane::Strip& s = pane.layoutStrips()[size_t(strip)];
        master = s.master;
        track = s.track;
        travel = std::max(10.f, s.fader.size.y);
        startDb = pane.shownVolume(s);
        TesseraModule* m = ctx.module();
        if (master) {
            track = m->song().master.id;
            return;
        }
        const std::vector<TrackId>& sel = ctx.state().selectedTracks;
        bool inSelection = std::find(sel.begin(), sel.end(), s.track) != sel.end() && sel.size() > 1;
        for (const MixerPane::Strip& o : pane.layoutStrips()) {
            if (o.master || (o.track != s.track && (!inSelection || std::find(sel.begin(), sel.end(), o.track) == sel.end())))
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
        if (master) {
            m->holdChannel(track, now, 0);
            pane.showHeld(track, now, 0);
            return;
        }
        for (const SetChannels::Change& g : group) {
            float v = g.track == track ? now : afterDelta(g.before.volumeDb);
            m->holdChannel(g.track, v, g.before.pan);
            pane.showHeld(g.track, v, g.before.pan);
        }
    }
    void commit() override {
        TesseraModule* m = ctx.module();
        if (now != startDb) {
            if (master)
                m->perform(setMasterVolume(m->song(), now));
            else if (group.size() == 1)
                m->perform(setChannel(m->song(), track, {now, group[0].before.pan}, "Volume " + audioDraw::dbText(now)));
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
    float travel = 100;
    float startDb = 0, now = 0;
    std::vector<SetChannels::Change> group;
};

/// Drags a pan knob up/down or sideways: relative, Shift or Ctrl/Cmd for fine.
class PanGesture : public DragGesture {
  public:
    PanGesture(MixerPane& pane, EditorContext& ctx, int strip, Vec press) : pane(pane), ctx(ctx), press(press) {
        const MixerPane::Strip& s = pane.layoutStrips()[size_t(strip)];
        track = s.track;
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
        ctx.module()->holdChannel(track, volume, now);
        pane.showHeld(track, volume, now);
    }
    void commit() override {
        TesseraModule* m = ctx.module();
        if (now != start)
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
    float start = 0, now = 0, volume = 0;
};

/// Drags the tab: the mixer's height, from the bottom of the editor up to the pointer.
class TabGesture : public DragGesture {
  public:
    TabGesture(MixerPane& pane, EditorContext& ctx, Vec press) : pane(pane), ctx(ctx), grab(press.y) {}
    void update(Vec pos, Modifiers) override {
        ViewState& v = ctx.view();
        v.mixerOpen = true;
        v.mixerHeight = std::max(pane.tabHeight() + 60, pane.box.size.y - (pos.y - grab));
    }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    MixerPane& pane;
    EditorContext& ctx;
    float grab;
};

} // namespace

float MixerPane::faderPos(float db) {
    if (db <= kSilentDb)
        return 0;
    if (db >= 0)
        return kZeroDbPos + (1 - kZeroDbPos) * std::min(db, kMaxFaderDb) / kMaxFaderDb;
    // Below 0 dB the travel follows the fourth root of the gain: -12 dB sits at about half way, -48 dB near the
    // bottom, so the useful range gets most of the fader.
    return kZeroDbPos * std::pow(dbToGain(db), 0.25f);
}

float MixerPane::faderDb(float pos) {
    if (pos <= 0.001f)
        return kSilentDb;
    if (pos >= kZeroDbPos)
        return (pos - kZeroDbPos) / (1 - kZeroDbPos) * kMaxFaderDb;
    return gainToDb(std::pow(pos / kZeroDbPos, 4.f));
}

float MixerPane::tabHeight() const { return panel ? 16.f : 22.f; }

bool MixerPane::hasSound() const {
    TesseraModule* m = ctx.module();
    return m && hasAudio(m->song());
}

float MixerPane::shownVolume(const Strip& s) const {
    TesseraModule* m = ctx.module();
    uint64_t key = s.master ? m->song().master.id.value : s.track.value;
    auto it = held.find(key);
    if (it != held.end())
        return it->second.first;
    if (s.master)
        return m->song().master.volumeDb;
    const Track* t = m->song().findTrack(s.track);
    return t ? t->channel.volumeDb : 0;
}

float MixerPane::shownPan(const Strip& s) const {
    auto it = held.find(s.track.value);
    if (it != held.end())
        return it->second.second;
    const Track* t = ctx.module()->song().findTrack(s.track);
    return t ? t->channel.pan : 0;
}

void MixerPane::layout() {
    strips.clear();
    notesOnly = 0;
    TesseraModule* m = ctx.module();
    float tabH = tabHeight();
    tab = Rect(0, 0, box.size.x, tabH);
    maximize = Rect(box.size.x - tabH - 2, 1, tabH, tabH - 2);
    if (!m)
        return;
    float top = tabH + kGap, bottom = box.size.y - kGap;
    float h = bottom - top;
    bool roomy = h >= 190; // short panes drop the source chip and pan, keeping the fader usable
    auto partsOf = [&](Strip& s, float x, float w) {
        s.box = Rect(x, top, w, h);
        float y = top + 3;
        s.name = Rect(x, y, w, 16);
        y += 18;
        if (roomy) {
            s.chip = Rect(x + 3, y, w - 6, 14);
            y += 18;
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
    };
    float x = kGap;
    int index = 0;
    for (const Track& t : m->song().tracks) {
        if (t.type != TrackType::Audio) {
            notesOnly++;
            continue;
        }
        Strip s;
        s.track = t.id;
        s.playerIndex = index++;
        partsOf(s, x, kStripWidth);
        strips.push_back(s);
        x += kStripWidth + kGap;
    }
    Strip master;
    master.master = true;
    partsOf(master, box.size.x - kMasterWidth - kGap, kMasterWidth);
    if (!m->mainPatched() && audioActions::audioModuleInPatch())
        master.connect = Rect(master.box.pos.x + 4, master.readout.pos.y + 16, kMasterWidth - 8, 14);
    strips.push_back(master);
}

void MixerPane::step() {
    layout();
    InteractiveView::step();
}

MixerPane::Hit MixerPane::hitTest(Vec pos) const {
    Hit hit;
    if (maximize.contains(pos)) {
        hit.part = Part::Maximize;
        return hit;
    }
    if (tab.contains(pos)) {
        hit.part = Part::Tab;
        return hit;
    }
    for (size_t i = 0; i < strips.size(); i++) {
        const Strip& s = strips[i];
        if (!s.box.contains(pos) && !s.connect.contains(pos))
            continue;
        hit.strip = int(i);
        Rect faderZone(s.fader.pos.x - 6, s.fader.pos.y - 6, s.fader.size.x + s.meter.size.x + 18, s.fader.size.y + 12);
        if (s.connect.size.x > 0 && s.connect.contains(pos))
            hit.part = Part::Connect;
        else if (s.name.contains(pos))
            hit.part = Part::Name;
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
    if (button != GLFW_MOUSE_BUTTON_LEFT || !ctx.module())
        return nullptr;
    Hit h = hitTest(pos);
    switch (h.part) {
    case Part::Tab: {
        // A click opens or folds the mixer; only a real drag sets its height.
        EditorContext* c = &ctx;
        return std::make_unique<ClickOrDrag>(std::make_unique<TabGesture>(*this, ctx, pos), pos, [c] {
            c->view().mixerOpen = !c->view().mixerOpen;
            c->view().mixerMax = false;
        });
    }
    case Part::Fader:
        return std::make_unique<FaderGesture>(*this, ctx, h.strip, pos);
    case Part::Pan:
        return std::make_unique<PanGesture>(*this, ctx, h.strip, pos);
    default:
        return nullptr;
    }
}

void MixerPane::openTyping(const Strip& s) {
    TesseraModule* m = ctx.module();
    TrackId track = s.track;
    bool master = s.master;
    float pan = master ? 0 : shownPan(s);
    typingMenu(ctx, master ? "Master volume (dB)" : "Volume (dB)", shownVolume(s), [m, track, master, pan](float db) {
        if (master)
            m->perform(setMasterVolume(m->song(), db));
        else
            m->perform(setChannel(m->song(), track, {db, pan}, "Volume " + audioDraw::dbText(db)));
    });
}

void MixerPane::click(Vec pos, int button, Modifiers mods) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    Hit h = hitTest(pos);
    const Strip* s = h.strip >= 0 ? &strips[size_t(h.strip)] : nullptr;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (s && (h.part == Part::Fader || h.part == Part::Readout))
            openTyping(*s);
        else if (s && h.part == Part::Pan) {
            TrackId track = s->track;
            float volume = shownVolume(*s);
            rack::ui::Menu* menu = rack::createMenu();
            menu->addChild(rack::createMenuLabel("Pan"));
            menu->addChild(rack::createMenuItem("Centre", "double-click", [m, track, volume] {
                m->perform(setChannel(m->song(), track, {volume, 0}, "Pan centre"));
            }));
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    ViewState& v = ctx.view();
    switch (h.part) {
    case Part::Maximize:
        v.mixerMax = !v.mixerMax;
        v.mixerOpen = true;
        break;
    case Part::Name:
        if (s && !s->master)
            ctx.state().selectedTrack = s->track;
        break;
    case Part::Mute:
        trackActions::toggleMute(ctx, s->track);
        break;
    case Part::Solo:
        trackActions::toggleSolo(ctx, s->track, mods.ctrl || mods.shift);
        break;
    case Part::ClipLight:
        m->audioPlayer().clearClip();
        break;
    case Part::Connect:
        ctx.toast(audioActions::connectMainToAudio(m));
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
    if (h.part == Part::Tab) {
        ctx.view().mixerMax = !ctx.view().mixerMax;
        ctx.view().mixerOpen = true;
        return;
    }
    if (h.strip < 0)
        return;
    const Strip& s = strips[size_t(h.strip)];
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
    if (!m || h.part != Part::Fader || delta.y == 0)
        return;
    const Strip& s = strips[size_t(h.strip)];
    float step = (mods.shift ? kFine * 1 : kWheelDb) * (delta.y > 0 ? 1.f : -1.f);
    float now = shownVolume(s);
    float next = now <= kSilentDb ? (delta.y > 0 ? -60.f : kSilentDb) : std::clamp(now + step, kSilentDb, kMaxFaderDb);
    if (s.master)
        m->perform(setMasterVolume(m->song(), next));
    else
        m->perform(setChannel(m->song(), s.track, {next, shownPan(s)}, "Volume " + audioDraw::dbText(next)));
}

CursorShape MixerPane::hoverCursor(Vec pos, Modifiers mods) {
    switch (hitTest(pos).part) {
    case Part::Tab:
        return CursorShape::ResizeV;
    case Part::Fader:
    case Part::Pan:
        return CursorShape::ResizeV;
    case Part::Mute:
    case Part::Solo:
    case Part::Name:
    case Part::ClipLight:
    case Part::Connect:
    case Part::Maximize:
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
    draw::TextStyle label{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    float cy = tab.size.y / 2;
    draw::text(vg, label, 8, cy, "Mixer");
    if (!m || !hasSound())
        return;
    // A thin live meter per track in its colour, so the folded tab still shows what plays.
    float x = 60;
    const AudioPlayer& player = m->audioPlayer();
    for (const Strip& s : strips) {
        if (s.master)
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
        if (x > box.size.x - 260)
            break;
    }
    float right = maximize.pos.x - 8;
    draw::TextStyle small{theme::fonts::mono(), theme::metrics::kTextRuler, c.text1, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    float peak = std::max(player.masterLevel(0), player.masterLevel(1));
    char text[32];
    snprintf(text, sizeof text, peak > 1e-5f ? "peak %.1f" : "peak -inf", double(voltsDb(peak)));
    draw::text(vg, small, right, cy, text);
    right -= draw::textWidth(vg, small, text) + 12;
    if (m->masterSounding() && !m->mainPatched()) {
        small.color = c.queue;
        draw::text(vg, small, right, cy, "MAIN not patched");
    }
    // Maximise / restore: a square, or two when maximised.
    nvgBeginPath(vg);
    Rect b = maximize.shrink(Vec(5, 4));
    nvgRect(vg, b.pos.x, b.pos.y, b.size.x, b.size.y);
    if (ctx.view().mixerMax)
        nvgRect(vg, b.pos.x + 2, b.pos.y - 2, b.size.x, b.size.y);
    nvgStrokeColor(vg, c.text1);
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
}

void MixerPane::drawStrip(NVGcontext* vg, const Strip& s) {
    const theme::Palette& c = theme::colors();
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    const Track* t = s.master ? nullptr : song.findTrack(s.track);
    if (!s.master && !t)
        return;
    bool selected = t && ctx.state().selectedTrack == t->id;
    draw::roundedRect(vg, s.box, theme::metrics::kRadiusControl, selected ? nvgLerpRGBA(c.bg1, c.bg2, 0.6f) : c.bg1);
    // Colour bar and name.
    NVGcolor color = t ? theme::track(t->color) : c.text1;
    nvgBeginPath(vg);
    nvgRect(vg, s.box.pos.x, s.box.pos.y, s.box.size.x, 3);
    nvgFillColor(vg, color);
    nvgFill(vg);
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text0, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    std::string label = t ? t->name : "Master";
    draw::text(vg, name, s.name.getCenter().x, s.name.getCenter().y + 1,
               draw::ellipsize(vg, name, label, s.name.size.x - 4));
    draw::TextStyle small{theme::fonts::ui(), theme::metrics::kTextRuler - 1, c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};

    // Source chip: what feeds the track. In this phase that is its clips.
    if (s.chip.size.x > 0) {
        std::string chip = "Clips";
        NVGcolor chipText = c.text1;
        if (t)
            for (const auto& [id, clip] : song.clips)
                if (clip.audio && song.clipUses(id) > 0) {
                    audioDraw::ClipState st = audioDraw::stateOf(*m, clip);
                    if (st.status.state == SourceState::Missing &&
                        (std::any_of(t->arrangement.begin(), t->arrangement.end(), [&](const ArrangerClip& a) { return a.clip == id; }) ||
                         std::any_of(t->slots.begin(), t->slots.end(), [&](const ClipSlot& sl) { return sl.clip == id; }))) {
                        chip = "Missing file";
                        chipText = c.queue;
                    }
                }
        if (s.master)
            chip = m->mainPatched() ? "MAIN" : "MAIN not patched";
        if (s.master && !m->mainPatched())
            chipText = c.queue;
        draw::roundedRect(vg, s.chip, theme::metrics::kRadiusControl, c.bg2);
        small.color = chipText;
        draw::text(vg, small, s.chip.getCenter().x, s.chip.getCenter().y, draw::ellipsize(vg, small, chip, s.chip.size.x - 4));
        small.color = c.text1;
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
        char text[24];
        if (over)
            snprintf(text, sizeof text, "%+.1f", double(voltsDb(peak)));
        small.color = over ? c.ink : c.text2;
        draw::text(vg, small, s.clipLight.getCenter().x, s.clipLight.getCenter().y, over ? text : "no clip");
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
    float capY = s.fader.getBottom() - faderPos(volume) * s.fader.size.y;
    bool heldNow = held.count(s.master ? song.master.id.value : s.track.value) > 0;
    draw::roundedRect(vg, Rect(s.fader.pos.x - 2, capY - 5, s.fader.size.x + 4, 10), 2, heldNow ? c.accent : c.text1);
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
    draw::TextStyle mono{theme::fonts::mono(), theme::metrics::kTextRuler, c.text0, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, mono, s.readout.getCenter().x, s.readout.getCenter().y, audioDraw::dbText(volume));
    small.color = c.text2;
    if (!s.master)
        draw::text(vg, small, s.box.getCenter().x, s.readout.getBottom() + 8, "\xE2\x86\x92 Master");
    else if (s.connect.size.x > 0) {
        draw::roundedRect(vg, s.connect, theme::metrics::kRadiusControl, c.bg2);
        small.color = c.text0;
        draw::text(vg, small, s.connect.getCenter().x, s.connect.getCenter().y, "Connect to Audio");
    } else if (AudioStore* store = m->audioStore()) {
        char mem[32];
        snprintf(mem, sizeof mem, "audio %.0f MB", double(store->memoryBytes()) / (1 << 20));
        draw::text(vg, small, s.box.getCenter().x, s.readout.getBottom() + 8, mem);
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
    for (const Strip& s : strips)
        drawStrip(vg, s);
    if (notesOnly > 0) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        float x = strips.size() > 1 ? strips[strips.size() - 2].box.getRight() + 12 : 12;
        std::string text = std::to_string(notesOnly) + (notesOnly == 1 ? " track plays" : " tracks play") +
                           " notes only";
        draw::text(vg, hint, x, tab.size.y + 14, text);
    }
    nvgRestore(vg);
}

} // namespace tessera
