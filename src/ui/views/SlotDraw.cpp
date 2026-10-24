#include "SlotDraw.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/DrumKit.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../widgets/Draw.hpp"

#include <cmath>

namespace tessera::slotDraw {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kTextPad = 5;
constexpr float kPreviewAlpha = 0.45f;
constexpr float kMinPreviewWidth = 16;
constexpr size_t kMaxPreviewNotes = 256;
constexpr double kBlinkHz = 2;

} // namespace

bool blinkOn() { return std::fmod(EditorContext::now() * kBlinkHz, 1.0) < 0.5; }

void drawTriangle(NVGcontext* vg, Vec c, float size, NVGcolor color) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, c.x - size * 0.4f, c.y - size * 0.5f);
    nvgLineTo(vg, c.x + size * 0.5f, c.y);
    nvgLineTo(vg, c.x - size * 0.4f, c.y + size * 0.5f);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawSquare(NVGcontext* vg, Vec c, float size, NVGcolor color) {
    nvgBeginPath(vg);
    nvgRect(vg, c.x - size / 2, c.y - size / 2, size, size);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

/// Progress through the playing clip: a ring that fills clockwise from the top.
void drawRing(NVGcontext* vg, Vec c, float radius, float progress, NVGcolor color) {
    nvgBeginPath(vg);
    nvgCircle(vg, c.x, c.y, radius);
    nvgStrokeColor(vg, nvgTransRGBAf(color, 0.25f));
    nvgStrokeWidth(vg, 1.5f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    float start = -float(M_PI) / 2;
    nvgArc(vg, c.x, c.y, radius, start, start + 2 * float(M_PI) * std::clamp(progress, 0.001f, 1.f), NVG_CW);
    nvgStrokeColor(vg, color);
    nvgStroke(vg);
}

/// The clip's notes in miniature, behind its name. With a drum track's `lanes` a looping clip shows its whole lane
/// cycle, each lane repeating as it plays.
void drawNotePreview(NVGcontext* vg, const Clip& clip, const std::vector<DrumLane>* lanes, Rect r, NVGcolor color) {
    ClipSpan span = clipSpan(clip, lanes);
    Tick length = span.cycles() ? span.end : clip.playEnd();
    if (clip.notes.empty() || length <= 0)
        return;
    int lo = 127, hi = 0;
    size_t stride = std::max<size_t>(1, clip.notes.size() / kMaxPreviewNotes);
    for (size_t i = 0; i < clip.notes.size(); i += stride) {
        lo = std::min<int>(lo, clip.notes[i].pitch);
        hi = std::max<int>(hi, clip.notes[i].pitch);
    }
    float rowH = std::min(3.f, r.size.y / float(hi - lo + 1));
    float height = rowH * float(hi - lo + 1);
    float top = r.pos.y + (r.size.y - height) / 2;
    float perTick = r.size.x / float(length);
    auto bar = [&](int pitch, Tick from, Tick to) {
        float x0 = r.pos.x + perTick * float(from);
        float x1 = r.pos.x + perTick * float(std::min(to, length));
        nvgRect(vg, x0, top + float(hi - pitch) * rowH, std::max(1.f, x1 - x0), std::max(1.f, rowH - 0.5f));
    };
    nvgBeginPath(vg);
    for (size_t i = 0; i < clip.notes.size(); i += stride) {
        const Note& n = clip.notes[i];
        if (n.start >= length)
            continue;
        if (!span.cycles()) {
            bar(n.pitch, n.start, n.start + n.length);
            continue;
        }
        int lane = laneOfPitch(*lanes, n.pitch);
        LaneRegion region = lane >= 0 && hasOwnLoop((*lanes)[size_t(lane)], clip)
                                ? laneRegion((*lanes)[size_t(lane)], clip)
                                : LaneRegion{span.start, span.pattern};
        if (n.start < region.start || n.start >= region.start + region.length)
            continue;
        Tick end = std::min(n.end(), region.start + region.length);
        // Repeats closer than a couple of pixels read as one band, so a long cycle draws no more than fits.
        if (perTick * float(region.length) < 2) {
            bar(n.pitch, n.start, length);
            continue;
        }
        for (Tick shift = 0; n.start + shift < length; shift += region.length)
            bar(n.pitch, n.start + shift, end + shift);
    }
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawSlot(NVGcontext* vg, EditorContext& ctx, Rect cell, TrackId trackId, int scene, const LauncherStatus& st,
              bool hovered, bool hotLaunch) {
    const LauncherStatus* status = &st;
    const theme::Palette& c = theme::colors();
    const Song& song = ctx.module()->song();
    const Track* track = song.findTrack(trackId);
    if (!track)
        return;
    Rect launch = launchRect(cell);
    Vec center = launch.getCenter();
    const Clip* clip = scene >= 0 && scene < int(track->slots.size()) && track->slots[size_t(scene)].clip
                           ? song.findClip(*track->slots[size_t(scene)].clip)
                           : nullptr;
    bool playing = status->playing == scene;
    bool queued = status->queued == scene;
    bool stopQueued = playing && status->queued == kNoSlot;

    // A take recording into this slot (DESIGN.md §10: recording slots are red).
    const Recorder::Session* take = nullptr;
    for (const Recorder::Session& s : ctx.module()->takes().sessions())
        if (s.track == trackId && s.slot == scene)
            take = &s;
    if (!clip && take) {
        draw::roundedRect(vg, cell, theme::metrics::kRadiusItem, nvgLerpRGBA(c.bg1, c.record, 0.35f));
        nvgBeginPath(vg);
        nvgCircle(vg, center.x, center.y, 4.5f);
        nvgFillColor(vg, blinkOn() ? c.record : nvgTransRGBAf(c.record, 0.5f));
        nvgFill(vg);
        Rect body(cell.pos.x + launch.size.x, cell.pos.y, cell.size.x - launch.size.x, cell.size.y);
        draw::TextStyle text{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text0,
                             NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, text, body.pos.x + 3, body.getCenter().y,
                   draw::ellipsize(vg, text, "Recording", body.size.x - kTextPad));
        return;
    }
    if (!clip) {
        // A recessed well, so an empty slot never reads as content.
        NVGcolor fill = hovered ? nvgLerpRGBA(c.bg0, c.bg1, 1.f) : nvgLerpRGBA(c.bg0, c.ground, 0.45f);
        draw::roundedRect(vg, cell, theme::metrics::kRadiusItem, fill);
        // The stop square is faint until stopping matters: while the track plays a clip, or under the mouse.
        // A stop waiting for the launch quantize blinks, so a scene launch never looks ignored (Trello, 13 Oct).
        bool stopWaiting = status->queued == kNoSlot && scene == ctx.module()->queuedStopScene();
        bool trackPlaying = status->playing != kNoSlot;
        NVGcolor square = hotLaunch ? c.text1 : trackPlaying ? c.text2 : nvgTransRGBAf(c.text2, 0.45f);
        if (stopWaiting && blinkOn())
            square = c.queue;
        drawSquare(vg, center, trackPlaying || hotLaunch ? 6 : 5, square);
        return;
    }

    uint32_t color = track->color;
    bool selected = ctx.state().selectedTrack == trackId && clipActions::shownClip(ctx, *track) == clip->id;
    draw::roundedRect(vg, cell, theme::metrics::kRadiusItem, theme::track(color, selected ? 1.f : 0.82f));

    Rect body(cell.pos.x + launch.size.x, cell.pos.y, cell.size.x - launch.size.x, cell.size.y);
    nvgSave(vg);
    nvgIntersectScissor(vg, body.pos.x, body.pos.y, body.size.x, body.size.y);
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.onTile,
                         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::string label = draw::ellipsize(vg, name, clip->name.empty() ? "Clip" : clip->name, body.size.x - kTextPad);
    draw::text(vg, name, body.pos.x + 3, body.getCenter().y, label);
    // The notes in miniature, in the room the name leaves.
    float previewLeft = body.pos.x + 3 + draw::textWidth(vg, name, label) + kTextPad;
    if (body.getRight() - previewLeft >= kMinPreviewWidth)
        drawNotePreview(vg, *clip, track->type == TrackType::Drum ? &track->drumLanes : nullptr,
                        Rect(previewLeft, body.pos.y + 3, body.getRight() - previewLeft - 3, body.size.y - 6),
                        nvgTransRGBAf(c.onTile, kPreviewAlpha));
    nvgRestore(vg);

    // The launch button: a darker square with the triangle, a ring while playing.
    draw::roundedRect(vg, launch, theme::metrics::kRadiusItem, nvgTransRGBAf(c.onTile, hotLaunch ? 0.55f : 0.4f));
    NVGcolor triangle = c.text0;
    if (queued)
        triangle = blinkOn() ? c.queue : c.text0;
    else if (playing)
        triangle = c.play;
    drawTriangle(vg, center, 7, triangle);
    if (playing && std::isfinite(status->position)) {
        ClipSpan span = clipSpan(*clip, track->type == TrackType::Drum ? &track->drumLanes : nullptr);
        Tick start = span.looping ? span.start : clip->playStart();
        Tick end = span.looping ? span.end : clip->playEnd();
        float progress = end > start ? float((status->position - double(start)) / double(end - start)) : 0;
        NVGcolor ring = stopQueued ? (blinkOn() ? c.queue : c.play) : c.play;
        drawRing(vg, center, launch.size.y * 0.5f - 2.5f, progress, ring);
    }
    if (take) // overdubbing this clip
        draw::roundedRectStroke(vg, cell, theme::metrics::kRadiusItem, c.record, 2);
    else if (selected)
        draw::roundedRectStroke(vg, cell, theme::metrics::kRadiusItem, c.select, 1.5f);
}

} // namespace tessera::slotDraw
