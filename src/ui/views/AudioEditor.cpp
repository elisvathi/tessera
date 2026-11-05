#include "AudioEditor.hpp"

#include "../../model/AudioEdits.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../widgets/Draw.hpp"
#include "AudioClipDraw.hpp"

#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kRulerHeight = 22;
constexpr float kEdgeHit = 6;     // px either side of a window edge
constexpr float kBeatHit = 4;     // px either side of a beat line (§6.4)
constexpr float kSnapPx = 8;      // an edge catches a beat line this close; Alt frees it
constexpr double kMinWindow = 64; // frames
constexpr double kFine = 0.1;

/// "0:01.250": a place in the file.
std::string timeText(double frames, double rate) {
    double s = rate > 0 ? std::max(0.0, frames / rate) : 0;
    int minutes = int(s / 60);
    char text[32];
    snprintf(text, sizeof text, "%d:%06.3f", minutes, s - minutes * 60.0);
    return text;
}

/// Drags a warp marker (§3.5): the beat it pins its hit to moves, snapping to 1/16 notes unless Alt is held. The
/// beat grid slides around the hit; the audio between the markers either side stretches to fit.
class WarpGesture : public DragGesture {
  public:
    WarpGesture(AudioEditor& view, Vec press, int index) : view(view), press(press), index(index) {
        if (const Clip* c = view.shownClip())
            warps = c->audio->warps;
    }
    void update(Vec pos, Modifiers mods) override {
        const AudioSource* src = view.source();
        if (!src || index < 0 || index >= int(warps.size()))
            return;
        double perTick = framesPerTick(view.shownAudio(), src->sampleRate);
        double start = warps[size_t(index)].tick;
        double tick = start + (pos.x - press.x) * view.pixelFrames() / perTick;
        if (!mods.alt)
            tick = std::round(tick / double(kPpq / 4)) * double(kPpq / 4);
        double lo = index > 0 ? warps[size_t(index - 1)].tick + 1 : -1e12;
        double hi = index + 1 < int(warps.size()) ? warps[size_t(index + 1)].tick - 1 : 1e12;
        std::vector<WarpMarker> next = warps;
        next[size_t(index)].tick = std::clamp(tick, lo, hi);
        view.preview.warps = next;
        shown = next[size_t(index)].tick;
    }
    void commit() override {
        TesseraModule* m = view.context().module();
        if (view.preview.warps && m)
            m->perform(setWarps(m->song(), view.clip, *view.preview.warps, "Move warp marker"));
        view.preview = {};
    }
    void cancel() override { view.preview = {}; }
    std::string readout() const override {
        TesseraModule* m = view.context().module();
        TimeSig ts = m ? m->song().timeSig : TimeSig();
        Tick beat = ticksPerBeat(ts);
        int b = int(std::floor(shown / double(beat)));
        int bar = b / std::max(1, int(ts.num)) + 1, inBar = b % std::max(1, int(ts.num)) + 1;
        double sixteenth = std::fmod(shown, double(beat)) / double(kPpq / 4);
        char text[48];
        snprintf(text, sizeof text, "Pinned to %d.%d.%.0f", bar, inBar, std::floor(sixteenth) + 1);
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    AudioEditor& view;
    Vec press;
    int index;
    std::vector<WarpMarker> warps;
    double shown = 0;
};

/// Drags the window's body: the clip plays another part of the file, as long as before.
class SlipGesture : public DragGesture {
  public:
    SlipGesture(AudioEditor& view, Vec press) : view(view), press(press) {
        start = double(view.shownAudio().sourceStart);
        auto [s, e] = view.window();
        span = e - s;
        if (const AudioSource* src = view.source())
            frames = double(src->frames);
        now = int64_t(start);
    }
    void update(Vec pos, Modifiers mods) override {
        double d = (pos.x - press.x) * view.pixelFrames() * (mods.shift ? kFine : 1);
        double most = std::max(0.0, frames - span);
        now = int64_t(std::llround(std::clamp(start + d, 0.0, most)));
        view.preview.sourceStart = now;
    }
    void commit() override {
        view.preview = {};
        TesseraModule* m = view.context().module();
        if (now != int64_t(start))
            m->perform(
                editAudio(m->song(), view.clip, [n = now](AudioContent& a) { a.sourceStart = n; }, "Slip audio"));
    }
    void cancel() override { view.preview = {}; }
    std::string readout() const override {
        const AudioSource* src = view.source();
        return "Start in file " + timeText(double(now), src ? src->sampleRate : 0);
    }
    CursorShape cursor() const override { return CursorShape::Move; }

  private:
    AudioEditor& view;
    Vec press;
    double start = 0, span = 0, frames = 0;
    int64_t now = 0;
};

/// Drags an edge of the window: the clip starts or ends elsewhere in the file. Edges catch the clip's beat lines.
class EdgeGesture : public DragGesture {
  public:
    EdgeGesture(AudioEditor& view, Vec press, bool leftEdge) : view(view), press(press), leftEdge(leftEdge) {
        auto [s, e] = view.window();
        start = s;
        end = e;
        perBeat = view.framesPerBeat();
        if (const AudioSource* src = view.source())
            frames = double(src->frames);
        now = {int64_t(s), int64_t(e)};
    }
    void update(Vec pos, Modifiers mods) override {
        double from = leftEdge ? start : end;
        double f = from + (pos.x - press.x) * view.pixelFrames() * (mods.shift ? kFine : 1);
        if (!mods.alt && perBeat > 0) {
            double beat = start + std::round((f - start) / perBeat) * perBeat;
            if (std::fabs(view.frameToX(beat) - view.frameToX(f)) <= kSnapPx)
                f = beat;
        }
        if (leftEdge)
            now.first = int64_t(std::llround(std::clamp(f, 0.0, end - kMinWindow)));
        else
            now.second = int64_t(std::llround(std::clamp(f, start + kMinWindow, frames)));
        view.preview.window = now;
    }
    void commit() override {
        view.preview = {};
        TesseraModule* m = view.context().module();
        if (now.first != int64_t(start) || now.second != int64_t(end))
            m->perform(setAudioWindow(m->song(), view.clip, now.first, now.second));
    }
    void cancel() override { view.preview = {}; }
    std::string readout() const override {
        const AudioSource* src = view.source();
        double rate = src ? src->sampleRate : 0;
        return leftEdge ? "Start in file " + timeText(double(now.first), rate)
                        : "End in file " + timeText(double(now.second), rate);
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    AudioEditor& view;
    Vec press;
    bool leftEdge;
    double start = 0, end = 0, perBeat = 0, frames = 0;
    std::pair<int64_t, int64_t> now;
};

/// Drags a beat line of the ruler: the clip tempo changes so that beat lands where it is dropped, the grid
/// stretching from beat 1 (§6.4). Like every tempo edit, it changes all copies of the clip.
class BeatGesture : public DragGesture {
  public:
    BeatGesture(AudioEditor& view, Vec press, int beat) : view(view), press(press), beat(beat) {
        from = view.shownAudio().sourceTempo;
        now = from;
        start = view.window().first;
        at = start + beat * view.framesPerBeat();
    }
    void update(Vec pos, Modifiers mods) override {
        const AudioSource* src = view.source();
        if (!src || beat < 1)
            return;
        double f = at + (pos.x - press.x) * view.pixelFrames() * (mods.shift ? kFine : 1);
        double perBeat = std::max(1.0, (f - start) / beat);
        double ticksPerBeatNow = double(ticksPerBeat(timeSig()));
        // Frames per beat = 60 / bpm * rate * (beat ticks / kPpq).
        now = std::clamp(60.0 * src->sampleRate * ticksPerBeatNow / double(kPpq) / perBeat, 20.0, 999.0);
        if (!mods.alt)
            now = std::round(now * 10) / 10;
        view.preview.tempo = now;
    }
    void commit() override {
        view.preview = {};
        TesseraModule* m = view.context().module();
        if (std::fabs(now - from) > 1e-6)
            m->perform(setClipTempo(m->song(), view.clip, now));
    }
    void cancel() override { view.preview = {}; }
    std::string readout() const override {
        char text[48];
        snprintf(text, sizeof text, "%.4g → %.4g BPM", from, now);
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    TimeSig timeSig() {
        TesseraModule* m = view.context().module();
        return m ? m->song().timeSig : TimeSig{};
    }
    AudioEditor& view;
    Vec press;
    int beat;
    double from = 120, now = 120, start = 0, at = 0;
};

} // namespace

const Clip* AudioEditor::shownClip() {
    TesseraModule* m = ctx.module();
    const Clip* c = m ? m->song().findClip(clip) : nullptr;
    return c && c->audio ? c : nullptr;
}

AudioContent AudioEditor::shownAudio() {
    const Clip* c = shownClip();
    AudioContent a = c ? *c->audio : AudioContent();
    if (preview.sourceStart)
        a.sourceStart = *preview.sourceStart;
    if (preview.window)
        a.sourceStart = preview.window->first;
    if (preview.tempo)
        a.sourceTempo = *preview.tempo;
    if (preview.warps)
        a.warps = *preview.warps;
    return a;
}

const AudioSource* AudioEditor::source() {
    const Clip* c = shownClip();
    TesseraModule* m = ctx.module();
    return c && m ? m->song().sourceOf(*c) : nullptr;
}

std::pair<double, double> AudioEditor::window() {
    if (preview.window)
        return {double(preview.window->first), double(preview.window->second)};
    const Clip* c = shownClip();
    const AudioSource* src = source();
    if (!c || !src)
        return {0, 0};
    // With warp markers, they say where the clip starts and ends in the file.
    if (!c->audio->warps.empty() && !c->audio->reverse) {
        AudioContent w = shownAudio();
        return {warpedFrame(w, 0, src->sampleRate), warpedFrame(w, double(c->length), src->sampleRate)};
    }
    // A tempo preview leaves the window where it is: the clip keeps the same audio (§5.1).
    const AudioContent& a = *c->audio;
    double start = double(preview.sourceStart ? *preview.sourceStart : a.sourceStart);
    return {start, start + double(c->length) * framesPerTick(a, src->sampleRate)};
}

double AudioEditor::framesPerBeat() {
    const AudioSource* src = source();
    TesseraModule* m = ctx.module();
    if (!src || !m)
        return 0;
    return framesPerTick(shownAudio(), src->sampleRate) * double(ticksPerBeat(m->song().timeSig));
}

Rect AudioEditor::waveRect() const {
    return Rect(0, kRulerHeight, box.size.x, std::max(0.f, box.size.y - kRulerHeight));
}

void AudioEditor::fit() {
    const AudioSource* src = source();
    double frames = src ? double(std::max<int64_t>(1, src->frames)) : 48000;
    double width = std::max(1.f, box.size.x);
    // A little room either side, so the window's edges can be grabbed at the file's ends.
    framesPerPx = frames * 1.04 / width;
    left = -frames * 0.02;
    fitted = clip;
    fittedWidth = width;
}

void AudioEditor::step() {
    float width = box.size.x;
    if (clip != fitted || (fittedWidth <= 1 && width > 1)) {
        fit();
    } else if (width > 1 && fittedWidth > 1 && width != fittedWidth) {
        // A resize (a pane maximised or restored, the inspector opened) keeps the same part of the file in view.
        framesPerPx *= fittedWidth / width;
        fittedWidth = width;
    }
    InteractiveView::step();
}

AudioEditor::Hit AudioEditor::hitTest(Vec pos) {
    Hit hit;
    if (!shownClip() || !source())
        return hit;
    auto [s, e] = window();
    float xs = float(frameToX(s)), xe = float(frameToX(e));
    // A warped clip: its markers are what moves; window, edges and tempo wait until they are gone.
    const AudioContent& a = *shownClip()->audio;
    if (!a.warps.empty() && !a.reverse) {
        AudioContent w = shownAudio();
        double best = 6;
        for (size_t i = 0; i < w.warps.size(); i++) {
            double d = std::fabs(frameToX(double(w.warps[i].frame)) - pos.x);
            if (d <= best) {
                best = d;
                hit.part = Part::Warp;
                hit.warp = int(i);
            }
        }
        return hit;
    }
    if (pos.y < kRulerHeight) {
        double perBeat = framesPerBeat();
        if (perBeat <= 0)
            return hit;
        int k = int(std::lround((xToFrame(pos.x) - s) / perBeat));
        if (k >= 1 && std::fabs(frameToX(s + k * perBeat) - pos.x) <= kBeatHit) {
            hit.part = Part::Beat;
            hit.beat = k;
        } else if (k == 0 && std::fabs(xs - pos.x) <= kEdgeHit) {
            hit.part = Part::Window; // beat 1's flag: dragging it slips the clip through the file
        }
        return hit;
    }
    if (std::fabs(pos.x - xs) <= kEdgeHit && std::fabs(pos.x - xs) <= std::fabs(pos.x - xe))
        hit.part = Part::LeftEdge;
    else if (std::fabs(pos.x - xe) <= kEdgeHit)
        hit.part = Part::RightEdge;
    else if (pos.x > xs && pos.x < xe)
        hit.part = Part::Window;
    return hit;
}

std::unique_ptr<DragGesture> AudioEditor::startGesture(Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return nullptr;
    Hit hit = hitTest(pos);
    switch (hit.part) {
    case Part::Window:
        return std::make_unique<SlipGesture>(*this, pos);
    case Part::LeftEdge:
    case Part::RightEdge:
        return std::make_unique<EdgeGesture>(*this, pos, hit.part == Part::LeftEdge);
    case Part::Beat:
        return std::make_unique<BeatGesture>(*this, pos, hit.beat);
    case Part::Warp:
        return std::make_unique<WarpGesture>(*this, pos, hit.warp);
    default:
        return nullptr;
    }
}

const ArrangerClip* AudioEditor::instance() {
    TesseraModule* m = ctx.module();
    if (!m)
        return nullptr;
    const ArrangerClip* first = nullptr;
    for (const Track& t : m->song().tracks)
        for (const ArrangerClip& a : t.arrangement) {
            if (a.clip != clip)
                continue;
            if (ctx.state().clipSelection.count(a.id))
                return &a;
            if (!first || a.position < first->position)
                first = &a;
        }
    return first;
}

void AudioEditor::click(Vec pos, int button, Modifiers mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        Hit hit = hitTest(pos);
        TesseraModule* m = ctx.module();
        const Clip* c = shownClip();
        if (hit.part == Part::Warp && m && c) {
            std::vector<WarpMarker> warps = c->audio->warps;
            int index = hit.warp;
            ClipId id = clip;
            rack::ui::Menu* menu = rack::createMenu();
            menu->addChild(rack::createMenuLabel("Warp marker"));
            menu->addChild(rack::createMenuItem("Remove this marker", "", [m, id, warps, index] {
                std::vector<WarpMarker> rest = warps;
                rest.erase(rest.begin() + index);
                m->perform(setWarps(m->song(), id, rest, "Remove warp marker"));
            }));
            menu->addChild(rack::createMenuItem("Remove all markers", "", [m, id] {
                m->perform(setWarps(m->song(), id, {}, "Remove warp markers"));
            }));
        }
        return;
    }
    // A click in the ruler plays from there, on the timeline copy of the clip (§6.4).
    TesseraModule* m = ctx.module();
    const AudioSource* src = source();
    if (!m || !src || button != GLFW_MOUSE_BUTTON_LEFT || pos.y >= kRulerHeight || hitTest(pos).part != Part::None)
        return;
    const ArrangerClip* a = instance();
    if (!a) {
        ctx.toast("This clip isn't on the timeline: launch it to hear it");
        return;
    }
    AudioContent audio = shownAudio();
    double u = (xToFrame(pos.x) - window().first) / framesPerTick(audio, src->sampleRate);
    double perClip = songTicksPerClipTick(audio, m->tempo() > 0 ? m->tempo() : 120);
    double tick = double(a->position) + std::max(0.0, u - double(a->offset)) * perClip;
    tick = std::min(tick, double(instanceEnd(*a)));
    m->requestSeek(tick);
    m->requestTransport(kRequestPlay);
}

void AudioEditor::doubleClick(Vec pos, Modifiers mods) {
    Hit hit = hitTest(pos);
    if (hit.part != Part::None && hit.part != Part::Window)
        return;
    // In the ruler: show the whole file. On the waveform: pin the nearest hit to the nearest 1/16 (§3.5).
    if (pos.y < kRulerHeight) {
        fit();
        return;
    }
    TesseraModule* m = ctx.module();
    const Clip* c = shownClip();
    const AudioSource* src = source();
    if (!m || !c || !src || c->audio->reverse)
        return;
    double frame = xToFrame(pos.x);
    if (AudioStore* store = m->audioStore())
        if (SourceStatus st = store->status(c->audio->source); st.transients) {
            double best = 12 * framesPerPx;
            for (int64_t f : *st.transients)
                if (std::fabs(double(f) - frame) < best) {
                    best = std::fabs(double(f) - frame);
                    frame = double(f);
                }
        }
    AudioContent a = *c->audio;
    double tick = warpedTick(a, frame, src->sampleRate);
    if (!mods.alt)
        tick = std::round(tick / double(kPpq / 4)) * double(kPpq / 4);
    std::vector<WarpMarker> warps = a.warps;
    // Without markers yet, the clip's start is pinned where it is, so only the hit moves.
    if (warps.empty() && tick > 0)
        warps.push_back({0, a.sourceStart});
    warps.push_back({tick, int64_t(std::llround(frame))});
    m->perform(setWarps(m->song(), clip, warps, "Add warp marker"));
}

CursorShape AudioEditor::hoverCursor(Vec pos, Modifiers mods) {
    switch (hitTest(pos).part) {
    case Part::LeftEdge:
    case Part::RightEdge:
    case Part::Beat:
        return CursorShape::ResizeH;
    case Part::Window:
        return CursorShape::Move;
    default:
        return CursorShape::Arrow;
    }
}

void AudioEditor::wheel(Vec pos, Vec delta, Modifiers mods) {
    const AudioSource* src = source();
    double frames = src ? double(std::max<int64_t>(1, src->frames)) : 48000;
    if (mods.ctrl || mods.alt) {
        // Zoom about the pointer, from a few samples per pixel to the whole file.
        double at = xToFrame(pos.x);
        double factor = std::pow(0.85, double(delta.y) / 50);
        framesPerPx = std::clamp(framesPerPx * factor, 0.25, frames * 1.5 / std::max(1.f, box.size.x));
        left = at - pos.x * framesPerPx;
        return;
    }
    double d = double(std::fabs(delta.x) > std::fabs(delta.y) ? delta.x : delta.y);
    left -= d * framesPerPx;
    double width = box.size.x * framesPerPx;
    left = std::clamp(left, -width * 0.5, frames - width * 0.5);
}

void AudioEditor::drawRuler(NVGcontext* vg, Rect r) {
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    TesseraModule* m = ctx.module();
    double perBeat = framesPerBeat();
    if (!m || perBeat <= 0)
        return;
    auto [s, e] = window();
    // A warped clip's beats sit where its markers put them.
    AudioContent audio = shownAudio();
    const AudioSource* src = source();
    bool warped = src && !audio.warps.empty() && !audio.reverse;
    int beatsPerBar = std::max(1, int(m->song().timeSig.num));
    int first = int(std::floor((xToFrame(0) - s) / perBeat)) - 1;
    int last = int(std::ceil((xToFrame(box.size.x) - s) / perBeat)) + 1;
    // Too many lines to read: only the bars.
    bool beats = frameToX(s + perBeat) - frameToX(s) >= 6;
    draw::TextStyle label{theme::fonts::mono(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    for (int k = std::max(first, -100000); k <= last && k < first + 4000; k++) {
        bool bar = ((k % beatsPerBar) + beatsPerBar) % beatsPerBar == 0;
        if (!bar && !beats)
            continue;
        double f = s + k * perBeat;
        if (warped)
            f = warpedFrame(audio, double(k) * double(ticksPerBeat(m->song().timeSig)), src->sampleRate);
        float x = float(frameToX(f));
        bool inside = f >= s - 0.5 && f <= e + 0.5;
        nvgBeginPath(vg);
        nvgRect(vg, x, bar ? r.pos.y + 6 : r.getBottom() - 6, 1, bar ? r.size.y - 6 : 6);
        nvgFillColor(vg, inside ? (bar ? c.text1 : c.text2) : c.line1);
        nvgFill(vg);
        if (bar && k != 0 && frameToX(s + beatsPerBar * perBeat) - frameToX(s) >= 24)
            draw::text(vg, label, x + 3, r.pos.y + 8, std::to_string(k / beatsPerBar + (k >= 0 ? 1 : 0)));
    }
    // Beat 1's flag, where the clip starts in the file.
    float xs = float(frameToX(s));
    Rect flag(xs, r.pos.y + 1, 12, 12);
    draw::roundedRect(vg, flag, 2, c.select);
    draw::TextStyle one{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.ink,
                        NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, one, flag.getCenter().x, flag.getCenter().y + 0.5f, "1");
    nvgBeginPath(vg);
    nvgRect(vg, r.pos.x, r.getBottom() - 1, r.size.x, 1);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);
}

void AudioEditor::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg0);
    nvgFill(vg);
    TesseraModule* m = ctx.module();
    const Clip* cl = shownClip();
    const AudioSource* src = source();
    if (!m || !cl || !src) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, box.size.x / 2, box.size.y / 2, "Select an audio clip to edit it here");
        return;
    }
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    drawRuler(vg, Rect(0, 0, box.size.x, kRulerHeight));

    Rect wave = waveRect().shrink(Vec(0, 4));
    const Track* track = m->song().findTrack(ctx.state().selectedTrack);
    uint32_t color = cl->shownColor(track ? track->color : 0);
    AudioContent a = shownAudio();
    audioDraw::ClipState state = audioDraw::stateOf(*m, *cl);
    auto [s, e] = window();
    float xs = float(frameToX(s)), xe = float(frameToX(e));

    // The window: a lighter ground, so the part the clip plays stands out.
    Rect inside(xs, wave.pos.y, xe - xs, wave.size.y);
    draw::roundedRect(vg, inside, theme::metrics::kRadiusItem, theme::track(color, 0.18f));
    auto framesAt = [&](float x, double& from, double& to) {
        from = xToFrame(x);
        to = xToFrame(x + 1);
        return to > 0 && from < double(src->frames);
    };
    NVGcolor dim = nvgTransRGBAf(c.text2, 0.35f);
    NVGcolor bright = nvgLerpRGBA(theme::track(color), c.text0, 0.35f);
    audioDraw::drawWaveform(vg, wave, 0, xs, state, a.gainDb, dim, framesAt);
    audioDraw::drawWaveform(vg, wave, xe, box.size.x, state, a.gainDb, dim, framesAt);
    audioDraw::drawWaveform(vg, wave, xs, xe, state, a.gainDb, bright, framesAt);

    // The loop, when it covers less than the whole window.
    if (cl->loopMode == LoopMode::Loop && !(cl->loopStart == 0 && cl->loopEnd >= cl->length)) {
        double perTick = framesPerTick(a, src->sampleRate);
        for (Tick t : {cl->loopStart, cl->loopEnd}) {
            float x = float(frameToX(s + double(t) * perTick));
            nvgBeginPath(vg);
            nvgRect(vg, x - 0.5f, wave.pos.y, 1, wave.size.y);
            nvgFillColor(vg, c.queue);
            nvgFill(vg);
        }
    }
    // Transients: a small mark under the waveform where each hit starts (Split at transients cuts there).
    if (AudioStore* store = m->audioStore())
        if (SourceStatus st = store->status(cl->audio->source); st.transients) {
            nvgBeginPath(vg);
            for (int64_t f : *st.transients) {
                float x = float(frameToX(double(f)));
                if (x < 0 || x > box.size.x)
                    continue;
                float y = wave.getBottom();
                nvgMoveTo(vg, x, y - 6);
                nvgLineTo(vg, x - 3, y);
                nvgLineTo(vg, x + 3, y);
                nvgClosePath(vg);
            }
            nvgFillColor(vg, nvgTransRGBAf(c.text0, 0.6f));
            nvgFill(vg);
        }
    // Warp markers: a line through the hit they pin, with a handle in the ruler.
    if (!a.reverse)
        for (const WarpMarker& w : a.warps) {
            float x = float(frameToX(double(w.frame)));
            if (x < -4 || x > box.size.x + 4)
                continue;
            nvgBeginPath(vg);
            nvgRect(vg, x - 0.75f, 2, 1.5f, wave.getBottom() - 2);
            nvgFillColor(vg, c.select);
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x, kRulerHeight - 2);
            nvgLineTo(vg, x - 5, kRulerHeight - 9);
            nvgLineTo(vg, x, kRulerHeight - 16);
            nvgLineTo(vg, x + 5, kRulerHeight - 9);
            nvgClosePath(vg);
            nvgFillColor(vg, c.select);
            nvgFill(vg);
        }
    // The window's edges, with grips to show they move.
    for (float x : {xs, xe}) {
        nvgBeginPath(vg);
        nvgRect(vg, x - 1, wave.pos.y, 2, wave.size.y);
        nvgFillColor(vg, theme::track(color));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, x - 3, wave.getCenter().y - 10, 6, 20);
        nvgFillColor(vg, theme::track(color));
        nvgFill(vg);
    }
    // What the clip does with its window, in words.
    std::string note = src->name;
    if (a.reverse)
        note += "  ◀ plays reversed";
    std::string st = audioDraw::stateText(state);
    if (!st.empty())
        note += "  " + st;
    draw::TextStyle small{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
    draw::text(vg, small, 6, wave.pos.y + 2, note);
    // Where playback is in the file, while the timeline copy plays.
    if (const ArrangerClip* inst = instance(); inst && m->playing() && m->playMode() == PlayMode::Arranger) {
        double t = m->playheadTicks();
        double perClip = songTicksPerClipTick(a, m->tempo() > 0 ? m->tempo() : 120);
        if (t >= double(inst->position) && t < double(inst->position) + double(inst->length) * perClip) {
            double u = double(inst->offset) + (t - double(inst->position)) / perClip;
            if (cl->loopMode == LoopMode::Loop && cl->loopEnd > cl->loopStart && u >= double(cl->loopEnd))
                u = double(cl->loopStart) + std::fmod(u - double(cl->loopStart), double(cl->loopEnd - cl->loopStart));
            double perTick = framesPerTick(a, src->sampleRate);
            double f = a.reverse ? e - u * perTick : s + u * perTick;
            float x = float(frameToX(f));
            nvgBeginPath(vg);
            nvgRect(vg, x - 0.5f, 0, 1, box.size.y);
            nvgFillColor(vg, c.play);
            nvgFill(vg);
        }
    }
    nvgRestore(vg);
}

} // namespace tessera
