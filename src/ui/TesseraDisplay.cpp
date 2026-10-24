#include "TesseraDisplay.hpp"

#include "../model/Arrangement.hpp"
#include "../module/TesseraModule.hpp"
#include "Theme.hpp"
#include "widgets/Draw.hpp"

#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kPad = 2 * theme::metrics::kSpacing;
constexpr float kHeaderHeight = 17;
constexpr float kFooterHeight = 18;
constexpr float kSongShare = 0.52f; // of the room between header and footer; the notes get the rest
constexpr float kGap = 5;
constexpr float kTrackChip = 5;      // the track's colour at the start of its row
constexpr float kMinRow = 1.5f;      // thinner rows than this aren't drawn (many tracks)
constexpr float kKeysWidth = 6;      // the mini piano roll's keys
constexpr int kMinPitchSpan = 12;    // the mini piano roll shows at least an octave
constexpr int kMosaicScenes = 8;     // scenes shown at once in the clip grid
constexpr float kQuietAlpha = 0.45f; // clips and notes away from the playhead
constexpr double kBlinkHz = 2;
constexpr size_t kMaxNoteDashes = 4000; // a huge song draws its clips without their notes

const Song& previewSong() {
    static const Song song = makeDefaultSong();
    return song;
}

bool blackKey(int pitch) {
    int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

/// A small pill on the screen's top line, right-aligned at `right`. Returns its width.
float pill(NVGcontext* vg, float right, float cy, const std::string& text, NVGcolor color, bool triangle) {
    using namespace theme;
    draw::TextStyle style{fonts::uiStrong(), metrics::kPanelLabel, color, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                          metrics::kPanelLabelSpacing};
    float tw = draw::textWidth(vg, style, text);
    float w = tw + 10 + (triangle ? 9 : 0);
    Rect r(right - w, cy - 6, w, 12);
    draw::roundedRect(vg, r, 6, nvgTransRGBAf(color, 0.2f));
    float x = r.pos.x + 5;
    if (triangle) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x, cy - 3);
        nvgLineTo(vg, x + 5, cy);
        nvgLineTo(vg, x, cy + 3);
        nvgClosePath(vg);
        nvgFillColor(vg, color);
        nvgFill(vg);
        x += 9;
    }
    draw::text(vg, style, x, cy + 0.5f, text);
    return w;
}

} // namespace

const Song& TesseraDisplay::song() const { return module ? module->song() : previewSong(); }

void TesseraDisplay::refresh() {
    const Song& s = song();
    uint64_t rev = module ? module->songRevision() : 0;
    if (rev != revision) {
        revision = rev;
        lanes.clear();
        songEnd = 4 * ticksPerBar(s.timeSig);
        songEnd = std::max(songEnd, s.loopEnd);
        size_t dashes = 0;
        std::vector<TimedNote> played;
        for (const Track& t : s.tracks) {
            Lane lane{t.color, s.isAudible(t), {}, {}, 127, 0};
            for (const ArrangerClip& a : t.arrangement) {
                songEnd = std::max(songEnd, instanceEnd(a));
                lane.clips.push_back({a.position, instanceEnd(a)});
                const Clip* clip = s.findClip(a.clip);
                if (!clip || dashes > kMaxNoteDashes)
                    continue;
                played.clear();
                renderInstance(*clip, a, played, 0, 1, t.type == TrackType::Drum ? &t.drumLanes : nullptr);
                for (const TimedNote& n : played) {
                    lane.notes.push_back({n.start, n.end, n.pitch});
                    lane.low = std::min(lane.low, int(n.pitch));
                    lane.high = std::max(lane.high, int(n.pitch));
                }
                dashes += played.size();
            }
            lanes.push_back(std::move(lane));
        }
        roll.clip = ClipId(); // rebuilt below
    }

    // The selected track's clip: the one being edited, else what its launcher plays, else its first.
    TrackId selected = module ? module->editor().selectedTrack : TrackId();
    const Track* track = s.findTrack(selected);
    if (!track)
        for (const Track& t : s.tracks)
            if (s.activeClip(t)) {
                track = &t;
                break;
            }
    const Clip* clip = nullptr;
    if (track) {
        ClipId editing = module ? module->editor().selectedClip : ClipId();
        for (const ClipSlot& slot : track->slots)
            if (slot.clip && *slot.clip == editing)
                clip = s.findClip(editing);
        for (const ArrangerClip& a : track->arrangement)
            if (!clip && a.clip == editing)
                clip = s.findClip(editing);
        if (!clip)
            clip = s.activeClip(*track);
    }
    if (!clip) {
        roll = Roll();
        if (track) {
            roll.color = track->color;
            roll.title = track->name;
        }
        return;
    }
    // A lane cycle shows a clip length of it at a time, the page the playhead is on, as the editor shows its bars.
    const std::vector<DrumLane>* drumLanes = track->type == TrackType::Drum ? &track->drumLanes : nullptr;
    ClipSpan span = clipSpan(*clip, drumLanes);
    int64_t page = 0;
    if (span.cycles()) {
        double now = roll.clip == clip->id ? clipPlayhead() : NAN;
        if (std::isfinite(now))
            page = int64_t(std::floor((now - double(span.start)) / double(span.pattern)));
    }
    if (roll.clip == clip->id && roll.page == page)
        return;
    roll = Roll();
    roll.clip = clip->id;
    roll.track = track->id;
    roll.color = track->color;
    roll.title = track->name + " · " + clip->name;
    roll.page = page;
    int low = 127, high = 0;
    auto add = [&](Tick start, Tick end, int pitch) {
        roll.notes.push_back({start, end, pitch});
        low = std::min(low, pitch);
        high = std::max(high, pitch);
    };
    if (span.cycles()) {
        roll.start = span.start + Tick(page) * span.pattern;
        roll.end = roll.start + span.pattern;
        // The page as the clip plays it: one timeline pass over just that stretch of the cycle.
        ArrangerClip window;
        window.position = roll.start;
        window.offset = roll.start;
        window.length = span.pattern;
        std::vector<TimedNote> played;
        renderInstance(*clip, window, played, 0, 1, drumLanes);
        for (const TimedNote& n : played)
            add(n.start, n.end, n.pitch);
    } else {
        roll.start = clip->playStart();
        roll.end = std::max(clip->playEnd(), roll.start + ticksPerBar(s.timeSig));
        for (const Note& n : clip->notes)
            if (!n.muted)
                add(n.start, n.end(), n.pitch);
    }
    if (low > high)
        low = high = 60;
    int pad = std::max(1, (kMinPitchSpan - (high - low)) / 2);
    roll.low = std::max(0, low - pad);
    roll.high = std::min(127, std::max(high + pad, roll.low + kMinPitchSpan));
}

double TesseraDisplay::clipPlayhead() const {
    if (!module || !module->playing() || !roll.clip.valid())
        return NAN;
    const Song& s = song();
    const Track* track = s.findTrack(roll.track);
    if (!track)
        return NAN;
    LauncherStatus status = module->launcherStatus(track->id);
    if (status.playing >= 0 && std::isfinite(status.position))
        return status.position;
    if (module->playMode() != PlayMode::Arranger)
        return NAN;
    const ArrangerClip* a = instanceAt(*track, Tick(module->playheadTicks()));
    const Clip* clip = a && a->clip == roll.clip ? s.findClip(a->clip) : nullptr;
    if (!clip)
        return NAN;
    std::optional<double> t =
        clipTimeAt(*clip, *a, module->playheadTicks(), track->type == TrackType::Drum ? &track->drumLanes : nullptr);
    return t ? *t : NAN;
}

void TesseraDisplay::draw(const DrawArgs& args) {
    const theme::Palette& c = theme::screenColors();
    Rect r(Vec(), box.size);
    draw::roundedRect(args.vg, r, theme::metrics::kPanelDisplayRadius, c.bg0);
    draw::roundedRectStroke(args.vg, r, theme::metrics::kPanelDisplayRadius, c.line0, 1.f);
}

void TesseraDisplay::drawLayer(const DrawArgs& args, int layer) {
    // Layer 1 is self-lit: it stays readable when the room lights are dimmed.
    if (layer == 1) {
        refresh();
        NVGcontext* vg = args.vg;
        float w = box.size.x - 2 * kPad;
        Rect header(kPad, 0, w, kHeaderHeight);
        Rect footer(kPad, box.size.y - kFooterHeight, w, kFooterHeight);
        float room = footer.pos.y - header.getBottom() - 2 * kGap;
        Rect songArea(kPad, header.getBottom() + 2, w, room * kSongShare);
        Rect notesArea(kPad, songArea.getBottom() + kGap, w, footer.pos.y - songArea.getBottom() - 2 * kGap + 2);
        drawHeader(vg, header);
        nvgSave(vg);
        nvgIntersectScissor(vg, songArea.pos.x, songArea.pos.y, songArea.size.x, songArea.size.y);
        if (module && module->playMode() == PlayMode::Launcher)
            drawClipGrid(vg, songArea);
        else
            drawTimeline(vg, songArea);
        nvgRestore(vg);
        nvgBeginPath(vg);
        nvgRect(vg, kPad, songArea.getBottom() + kGap / 2, w, 0.75f);
        nvgFillColor(vg, theme::screenColors().line1);
        nvgFill(vg);
        nvgSave(vg);
        nvgIntersectScissor(vg, notesArea.pos.x, notesArea.pos.y, notesArea.size.x, notesArea.size.y);
        drawRoll(vg, notesArea);
        nvgRestore(vg);
        drawFooter(vg, footer);
    }
    Widget::drawLayer(args, layer);
}

void TesseraDisplay::drawHeader(NVGcontext* vg, Rect r) {
    using namespace theme;
    const Palette& c = screenColors();
    float cy = r.getCenter().y + 1;
    float right = r.getRight();
    bool playing = module && module->playing();
    bool clips = module && module->playMode() == PlayMode::Launcher;
    right -= pill(vg, right, cy, clips ? "CLIPS" : "TIMELINE", playing ? c.play : c.text2, true) + 4;
    if (module && module->loopEnabled())
        right -= pill(vg, right, cy, "LOOP", c.accent, false) + 4;

    // The scene PREV / NEXT pick and LAUNCH starts.
    const Song& s = song();
    int scene = std::clamp(module ? module->selectedScene() : 0, 0, std::max(0, int(s.scenes.size()) - 1));
    if (!s.scenes.empty()) {
        std::string label = "SCENE " + std::to_string(scene + 1);
        const std::string& name = s.scenes[size_t(scene)].name;
        if (!name.empty())
            label += " " + rack::string::uppercase(name);
        draw::TextStyle caption{fonts::uiStrong(), metrics::kPanelLabel, c.text2, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
                                metrics::kPanelLabelSpacing};
        float room = right - r.pos.x - 70;
        if (room > 30) {
            label = draw::ellipsize(vg, caption, label, room);
            draw::text(vg, caption, right - 4, cy + 0.5f, label);
            right -= draw::textWidth(vg, caption, label) + 8;
        }
    }
    draw::TextStyle title{fonts::uiStrong(), metrics::kTextUi, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::string name = module ? module->instanceName() : "Tessera";
    draw::text(vg, title, r.pos.x, cy, draw::ellipsize(vg, title, name, right - r.pos.x - 4));
}

void TesseraDisplay::drawTimeline(NVGcontext* vg, Rect r) {
    using namespace theme;
    const Palette& c = screenColors();
    const Song& s = song();
    if (lanes.empty() || songEnd <= 0)
        return;
    float x0 = r.pos.x + kTrackChip + 3, w = r.getRight() - x0;
    float loopBand = 2.5f;
    float top = r.pos.y + loopBand + 2;
    float rowGap = 1;
    float row = std::max(kMinRow, (r.getBottom() - top - rowGap * float(lanes.size() - 1)) / float(lanes.size()));
    auto xOf = [&](double tick) { return x0 + float(tick / double(songEnd)) * w; };
    if (module && module->loopEnabled() && s.loopEnd > s.loopStart)
        draw::roundedRect(vg, Rect(xOf(s.loopStart), r.pos.y, xOf(s.loopEnd) - xOf(s.loopStart), loopBand), 1,
                          nvgTransRGBAf(c.accent, 0.8f));
    for (size_t i = 0; i < lanes.size(); i++) {
        float y = top + float(i) * (row + rowGap);
        if (y > r.getBottom())
            break;
        const Lane& lane = lanes[i];
        LauncherStatus status =
            module && i < s.tracks.size() ? module->launcherStatus(s.tracks[i].id) : LauncherStatus();
        // A track a launched clip took over is quiet on the timeline.
        float alpha = (lane.audible ? 1.f : 0.4f) * (status.overridden ? kQuietAlpha : 1.f);
        NVGcolor color = rgb(lane.color, alpha);
        draw::roundedRect(vg, Rect(r.pos.x, y, kTrackChip, row), 1, color);
        nvgBeginPath(vg);
        nvgRect(vg, x0, y, w, row);
        nvgFillColor(vg, nvgTransRGBAf(c.line0, 0.6f));
        nvgFill(vg);
        for (const auto& [from, to] : lane.clips)
            draw::roundedRect(vg, Rect(xOf(from) + 0.4f, y, std::max(1.f, xOf(to) - xOf(from) - 0.8f), row), 1,
                              rgb(lane.color, 0.42f * alpha));
        if (row < 3)
            continue;
        int span = std::max(1, lane.high - lane.low);
        float dash = std::max(0.8f, row * 0.18f);
        nvgBeginPath(vg);
        for (const MiniNote& n : lane.notes) {
            float ny = y + 0.5f + (1 - float(n.pitch - lane.low) / float(span)) * (row - 1 - dash);
            nvgRect(vg, xOf(n.start), ny, std::max(0.8f, xOf(n.end) - xOf(n.start) - 0.4f), dash);
        }
        nvgFillColor(vg, color);
        nvgFill(vg);
    }
    if (module) {
        float px = xOf(std::fmod(module->playheadTicks(), double(songEnd)));
        nvgBeginPath(vg);
        nvgRect(vg, px - 0.5f, r.pos.y, 1, r.size.y);
        nvgFillColor(vg, nvgTransRGBAf(c.text0, 0.9f));
        nvgFill(vg);
    }
}

void TesseraDisplay::drawClipGrid(NVGcontext* vg, Rect r) {
    using namespace theme;
    const Palette& c = screenColors();
    const Song& s = song();
    int scenes = int(s.scenes.size());
    if (scenes == 0 || s.tracks.empty())
        return;
    int selected = std::clamp(module ? module->selectedScene() : 0, 0, scenes - 1);
    int shown = std::min(scenes, kMosaicScenes);
    int first = std::clamp(selected - shown / 2, 0, scenes - shown);
    float x0 = r.pos.x + kTrackChip + 3, gap = 1.5f;
    float cell = (r.getRight() - x0 - gap * float(shown - 1)) / float(shown);
    float rowGap = 1;
    float row = std::max(kMinRow, (r.size.y - rowGap * float(s.tracks.size() - 1)) / float(s.tracks.size()));
    bool blinkOn = std::fmod(rack::system::getTime() * kBlinkHz, 1.0) < 0.5;
    for (size_t i = 0; i < s.tracks.size(); i++) {
        float y = r.pos.y + float(i) * (row + rowGap);
        if (y > r.getBottom())
            break;
        const Track& t = s.tracks[i];
        float alpha = s.isAudible(t) ? 1.f : 0.4f;
        draw::roundedRect(vg, Rect(r.pos.x, y, kTrackChip, row), 1, rgb(t.color, alpha));
        LauncherStatus status = module ? module->launcherStatus(t.id) : LauncherStatus();
        for (int k = 0; k < shown; k++) {
            int scene = first + k;
            Rect cellRect(x0 + float(k) * (cell + gap), y, cell, row);
            bool filled = scene < int(t.slots.size()) && t.slots[size_t(scene)].clip;
            if (!filled) {
                draw::roundedRect(vg, cellRect, 1, c.line1);
                continue;
            }
            bool playing = status.playing == scene;
            bool queued = status.queued == scene;
            draw::roundedRect(vg, cellRect, 1, rgb(t.color, (playing ? 1.f : 0.35f) * alpha));
            if (queued && blinkOn)
                draw::roundedRectStroke(vg, cellRect, 1, c.queue, 1);
        }
    }
    // The scene LAUNCH starts.
    float fx = x0 + float(selected - first) * (cell + gap);
    draw::roundedRectStroke(vg, Rect(fx - 1, r.pos.y - 1, cell + 2, r.size.y + 2), 2, c.text0, 1);
}

void TesseraDisplay::drawRoll(NVGcontext* vg, Rect r) {
    using namespace theme;
    const Palette& c = screenColors();
    int rows = roll.high - roll.low + 1;
    float row = r.size.y / float(rows);
    float x0 = r.pos.x + kKeysWidth + 2, w = r.getRight() - x0;
    double now = clipPlayhead();
    for (int p = roll.high; p >= roll.low; p--) {
        float y = r.pos.y + float(roll.high - p) * row;
        bool black = blackKey(p);
        nvgBeginPath(vg);
        nvgRect(vg, r.pos.x, y, kKeysWidth, row - 0.3f);
        nvgFillColor(vg, black ? rgb(0x3A3E46) : rgb(0x9EA3AB));
        nvgFill(vg);
        if (black) {
            nvgBeginPath(vg);
            nvgRect(vg, x0, y, w, row);
            nvgFillColor(vg, nvgTransRGBAf(c.line0, 0.45f));
            nvgFill(vg);
        }
    }
    double length = double(roll.end - roll.start);
    if (length <= 0)
        return;
    Tick bar = ticksPerBar(song().timeSig);
    auto xOf = [&](double tick) { return x0 + float((tick - double(roll.start)) / length) * w; };
    nvgBeginPath(vg);
    for (Tick t = roll.start - roll.start % bar; t <= roll.end; t += bar / 4)
        if (t >= roll.start)
            nvgRect(vg, xOf(double(t)), r.pos.y, 0.6f, r.size.y);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);
    NVGcolor color = rgb(roll.color ? roll.color : 0x9AA1AB);
    for (const MiniNote& n : roll.notes) {
        if (n.end <= roll.start || n.start >= roll.end || n.pitch < roll.low || n.pitch > roll.high)
            continue;
        float y = r.pos.y + float(roll.high - n.pitch) * row;
        bool lit = std::isfinite(now) && double(n.start) <= now && now < double(n.end);
        float x = xOf(double(std::max(n.start, roll.start)));
        float xe = xOf(double(std::min(n.end, roll.end)));
        draw::roundedRect(vg, Rect(x + 0.3f, y + 0.2f, std::max(1.f, xe - x - 0.6f), std::max(1.f, row - 0.4f)), 0.8f,
                          lit ? color : nvgTransRGBAf(color, kQuietAlpha + 0.1f));
        if (lit) {
            draw::roundedRectStroke(vg, Rect(x + 0.3f, y + 0.2f, std::max(1.f, xe - x - 0.6f), row - 0.4f), 0.8f,
                                    nvgRGBAf(1, 1, 1, 0.8f), 0.7f);
            nvgBeginPath(vg);
            nvgRect(vg, r.pos.x, y, kKeysWidth, row - 0.3f);
            nvgFillColor(vg, color);
            nvgFill(vg);
        }
    }
    if (std::isfinite(now)) {
        nvgBeginPath(vg);
        nvgRect(vg, xOf(now) - 0.5f, r.pos.y, 1, r.size.y);
        nvgFillColor(vg, nvgTransRGBAf(c.text0, 0.9f));
        nvgFill(vg);
    }
}

void TesseraDisplay::drawFooter(NVGcontext* vg, Rect r) {
    using namespace theme;
    const Palette& c = screenColors();
    float base = r.getBottom() - 5;
    const Song& s = song();
    double position = module ? module->playheadTicks() : 0;
    BarBeatTick bbt = toBarBeatTick(Tick(position), s.timeSig);
    char positionText[48];
    snprintf(positionText, sizeof(positionText), "%lld.%d.%d", (long long)bbt.bar, bbt.beat,
             int(bbt.tick / (kPpq / 4)) + 1);
    draw::TextStyle number{fonts::mono(), 13, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE};
    draw::text(vg, number, r.pos.x, base, positionText);
    float left = r.pos.x + draw::textWidth(vg, number, positionText) + 8;

    bool external = module && module->externalClock();
    char bpmText[24];
    snprintf(bpmText, sizeof(bpmText), external ? "EXT %.1f" : "%.1f", module ? module->tempo() : 120.0);
    draw::TextStyle unit{fonts::uiStrong(), metrics::kPanelLabel, c.text2, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE,
                         metrics::kPanelLabelSpacing};
    draw::text(vg, unit, r.getRight(), base, "BPM");
    number.align = NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE;
    float right = r.getRight() - draw::textWidth(vg, unit, "BPM") - 4;
    draw::text(vg, number, right, base, bpmText);
    right -= draw::textWidth(vg, number, bpmText) + 8;

    if (roll.title.empty() || right - left < 30)
        return;
    draw::TextStyle title{fonts::uiStrong(), metrics::kTextRuler, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE};
    std::string text = draw::ellipsize(vg, title, roll.title, right - left - 12);
    float tw = draw::textWidth(vg, title, text) + 10;
    float x = left + std::max(0.f, (right - left - tw) / 2);
    draw::roundedRect(vg, Rect(x, base - 7, 6, 6), 1.5f, rgb(roll.color ? roll.color : 0x9AA1AB));
    draw::text(vg, title, x + 10, base, text);
}

} // namespace tessera
