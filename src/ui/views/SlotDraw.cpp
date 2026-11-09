#include "SlotDraw.hpp"

#include "AudioClipDraw.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/DeviceLookup.hpp"
#include "../../model/DrumKit.hpp"
#include "../../model/Playheads.hpp"
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

void drawPlayheadMark(NVGcontext* vg, const Track& track, Vec topRight, float maxHeight) {
    const Device* device = activePlayheads(track);
    if (!device)
        return;
    const float w = theme::metrics::kPlayheadMarkWidth, h = theme::metrics::kPlayheadMarkHeight, gap = 2;
    int fits = std::max(1, int((maxHeight + gap) / (h + gap)));
    int shown = std::min(int(device->playheads.size()), fits);
    for (int i = 0; i < shown; i++) {
        const Playhead& p = device->playheads[size_t(i)];
        Rect stripe(topRight.x - w, topRight.y + float(i) * (h + gap), w, h);
        NVGcolor color = theme::playheadColor(p.color);
        if (!playheads::isOn(p))
            draw::roundedRectStroke(vg, stripe, 1, color, 1);
        else
            draw::roundedRect(vg, stripe, 1, color);
    }
}

void drawHatch(NVGcontext* vg, Rect r, float right, NVGcolor color) {
    namespace m = theme::metrics;
    nvgSave(vg);
    nvgIntersectScissor(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
    nvgBeginPath(vg);
    // From the clip's own left edge, so the lines move with it; only those in view.
    float origin = r.pos.x - r.size.y;
    float first = origin + std::max(0.f, std::floor((-r.size.y - origin) / m::kPlayheadHatch)) * m::kPlayheadHatch;
    for (float x = first; x < std::min(right, r.getRight()); x += m::kPlayheadHatch) {
        nvgMoveTo(vg, x, r.getBottom());
        nvgLineTo(vg, x + r.size.y, r.pos.y);
    }
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
    nvgRestore(vg);
}

/// The note under clip position `pos`, as its index in `notes` (sorted by start); -1 when there is none.
static int noteUnder(const std::vector<Note>& notes, double pos) {
    auto after =
        std::upper_bound(notes.begin(), notes.end(), pos, [](double t, const Note& n) { return t < double(n.start); });
    int index = int(after - notes.begin()) - 1;
    // A long note can start before several short ones, so look back a little.
    for (int i = index; i >= 0 && i > index - 8; i--)
        if (!notes[size_t(i)].muted && double(notes[size_t(i)].end()) > pos)
            return i;
    return -1;
}

/// One playhead's band on a clip: a tint of its colour, with a block wherever it is over a note. It shows how many
/// playheads there are and the rhythm of each at a glance.
static void drawPlayheadBand(NVGcontext* vg, const Clip& clip, const playheads::Motion& motion, NVGcolor color,
                             Rect band, float x0, float x1, int samples, double e0, double de) {
    NVGcolor tint = color;
    tint.a *= 0.14f;
    nvgBeginPath(vg);
    nvgRect(vg, x0, band.pos.y, x1 - x0, band.size.y);
    nvgFillColor(vg, tint);
    nvgFill(vg);
    float inset = std::min(2.f, band.size.y / 4);
    nvgBeginPath(vg);
    int open = -1;
    float openX = x0;
    auto closeBlock = [&](float x) {
        if (open >= 0)
            nvgRoundedRect(vg, openX, band.pos.y + inset, std::max(1.f, x - openX - 1), band.size.y - 2 * inset, 1);
    };
    for (int i = 0; i <= samples; i++) {
        float x = x0 + (x1 - x0) * float(i) / float(samples);
        double pos = playheads::positionAt(motion, e0 + double(i) * de);
        int under = std::isfinite(pos) ? noteUnder(clip.notes, pos) : -1;
        if (under != open) {
            closeBlock(x);
            open = under;
            openX = x;
        }
    }
    closeBlock(x1);
    color.a *= 0.85f;
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawPlayheadPaths(NVGcontext* vg, const Song& song, const Track& track, const Device& device, const Clip& clip,
                       Rect area, float x0, float x1, double e0, double e1, NVGcolor line, float alpha) {
    namespace m = theme::metrics;
    int count = int(device.playheads.size());
    if (count == 0 || x1 <= x0 || e1 <= e0)
        return;
    nvgSave(vg);
    nvgIntersectScissor(vg, area.pos.x, area.pos.y, area.size.x, area.size.y);
    const std::vector<DrumLane>* lanes = track.type == TrackType::Drum ? &track.drumLanes : nullptr;
    std::pair<Tick, Tick> segment = playheadSpan(clip, lanes);
    bool bands = area.size.y / float(count) >= m::kPlayheadBand;
    float bandHeight = bands ? area.size.y / float(count) : area.size.y;
    int samples = std::max(2, int(std::ceil((x1 - x0) / m::kPlayheadPathStep)));
    double de = (e1 - e0) / double(samples);
    for (int k = 0; k < count; k++) {
        const Playhead& p = device.playheads[size_t(k)];
        playheads::Motion motion = playheads::motionFrom(p.params.data(), device.params.data(), double(segment.first),
                                                         double(segment.second), song.timeSig);
        if (playheads::wantsDrawnPath(p.params.data()))
            motion.path = &p.path;
        if (motion.regionLength <= 0)
            continue;
        float top = area.pos.y + (bands ? float(k) * bandHeight : 0) + 1, h = std::max(1.f, bandHeight - 2);
        if (bands && k > 0) {
            nvgBeginPath(vg);
            nvgRect(vg, area.pos.x, top - 1, area.size.x, 1);
            nvgFillColor(vg, line);
            nvgFill(vg);
        }
        if (bands) {
            NVGcolor bandColor = theme::playheadColor(p.color);
            bandColor.a = alpha * (playheads::heardAsSet(device.playheads, p) ? 1.f : 0.3f);
            drawPlayheadBand(vg, clip, motion, bandColor, Rect(area.pos.x, top, area.size.x, h), x0, x1, samples, e0,
                             de);
            continue;
        }
        // How far the playhead can move between two samples (0..1 of its segment): more is a jump, left undrawn.
        double reach = 3 * std::abs(motion.rate) * de / motion.regionLength + 0.02;
        nvgBeginPath(vg);
        double last = NAN;
        for (int i = 0; i <= samples; i++) {
            double e = e0 + double(i) * de;
            double pos = playheads::positionAt(motion, e);
            if (!std::isfinite(pos)) {
                last = NAN;
                continue;
            }
            double f = std::clamp((pos - motion.regionStart) / motion.regionLength, 0.0, 1.0);
            float x = x0 + (x1 - x0) * float(i) / float(samples), y = top + h - float(f) * h;
            if (!std::isfinite(last) || std::abs(f - last) > reach)
                nvgMoveTo(vg, x, y);
            else
                nvgLineTo(vg, x, y);
            last = f;
        }
        NVGcolor color = theme::playheadColor(p.color);
        color.a = alpha * (playheads::heardAsSet(device.playheads, p) ? 1.f : 0.3f);
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, m::kPlayheadPathWidth);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }
    nvgRestore(vg);
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
        if (track && scene >= 0 && scene < int(track->slots.size()) && !track->slots[size_t(scene)].stop)
            return; // no stop button: its scene leaves the track playing
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

    uint32_t color = clip->shownColor(track->color);
    bool selected = ctx.state().selectedTrack == trackId && clipActions::shownClip(ctx, *track) == clip->id;
    draw::roundedRect(vg, cell, theme::metrics::kRadiusItem, theme::track(color, selected ? 1.f : 0.82f));

    Rect body(cell.pos.x + launch.size.x, cell.pos.y, cell.size.x - launch.size.x, cell.size.y);
    nvgSave(vg);
    nvgIntersectScissor(vg, body.pos.x, body.pos.y, body.size.x, body.size.y);
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.onTile,
                         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::string label = draw::ellipsize(vg, name, clip->name.empty() ? "Clip" : clip->name, body.size.x - kTextPad);
    draw::text(vg, name, body.pos.x + 3, body.getCenter().y, label);
    // The notes (or the waveform) in miniature, in the room the name leaves; with playheads, where they go over one
    // pass of the clip instead (its notes as written aren't what plays).
    float previewLeft = body.pos.x + 3 + draw::textWidth(vg, name, label) + kTextPad;
    const std::vector<DrumLane>* lanes = track->type == TrackType::Drum ? &track->drumLanes : nullptr;
    Rect preview(previewLeft, body.pos.y + 3, body.getRight() - previewLeft - 3, body.size.y - 6);
    const Device* readers = clip->audio ? nullptr : activePlayheads(*track);
    if (clip->audio) {
        audioDraw::ClipState state = audioDraw::stateOf(*ctx.module(), *clip);
        std::string mark = audioDraw::stateText(state);
        if (!mark.empty() && body.getRight() - previewLeft >= kMinPreviewWidth) {
            draw::TextStyle small{theme::fonts::mono(), theme::metrics::kTextRuler - 1,
                                  audioDraw::stateWarns(state) ? c.queue : c.onTile, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
            draw::text(vg, small, body.getRight() - 3, body.getCenter().y, mark);
            preview.size.x -= draw::textWidth(vg, small, mark) + 4;
        }
        if (preview.size.x >= kMinPreviewWidth && state.source) {
            const AudioContent& a = *clip->audio;
            double from = double(clip->playStart()), span = double(clip->playEnd() - clip->playStart());
            audioDraw::drawWaveform(vg, preview, preview.pos.x, preview.getRight(), state, a.gainDb,
                                    nvgTransRGBAf(c.onTile, kPreviewAlpha), [&](float x, double& f0, double& f1, float& extraDb) {
                                        double u0 = from + span * double(x - preview.pos.x) / double(preview.size.x);
                                        double u1 = u0 + span / double(preview.size.x);
                                        return audioDraw::framesOfTicks(*clip, *state.source, u0, u1, f0, f1,
                                                                        extraDb);
                                    });
        }
    } else if (readers) {
        drawHatch(vg, body, body.getRight(), nvgTransRGBAf(c.onTile, 0.12f));
        // Clear of the playhead mark at the right.
        preview.size.x -= theme::metrics::kPlayheadMarkWidth + 4;
        if (preview.size.x >= kMinPreviewWidth)
            drawPlayheadPaths(vg, song, *track, *readers, *clip, preview, preview.pos.x, preview.getRight(), 0,
                              double(clipSpan(*clip, lanes).length()), nvgTransRGBAf(c.onTile, 0.2f), 1);
    } else if (body.getRight() - previewLeft >= kMinPreviewWidth) {
        drawNotePreview(vg, *clip, lanes, preview, nvgTransRGBAf(c.onTile, kPreviewAlpha));
    }
    nvgRestore(vg);
    drawPlayheadMark(vg, *track, Vec(body.getRight() - 4, body.pos.y + 4), body.size.y - 8);

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
