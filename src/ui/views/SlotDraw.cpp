#include "SlotDraw.hpp"

#include "DeviceTiles.hpp"

#include "AudioClipDraw.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/DeviceLookup.hpp"
#include "../../model/DrumKit.hpp"
#include "../../model/Playheads.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../widgets/Draw.hpp"

#include <algorithm>
#include <cmath>
#include <string>

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

namespace {

/// A cell tall enough for a title bar over its notes.
bool hasTitleBar(const Rect& cell) { return cell.size.y >= theme::metrics::kSlotTitleBarFrom; }

/// The line with the name and the marks, right of the launch button: the title bar in a tall cell, else the cell.
Rect nameRow(const Rect& cell) {
    Rect launch = launchRect(cell);
    float h = hasTitleBar(cell) ? theme::metrics::kSlotTitleBar : cell.size.y;
    return Rect(cell.pos.x + launch.size.x, cell.pos.y, cell.size.x - launch.size.x, h);
}

/// The room the own-devices card takes at the name row's right end, with the playhead stripes right of it; 0 for a
/// clip without own devices. The full card where the row is wide, its small form elsewhere.
float ownMarkRoom(const Rect& row, float stripes, bool hasOwn) {
    namespace m = theme::metrics;
    if (!hasOwn)
        return 0;
    float card = row.size.x - stripes >= m::kOwnMarkCardRoom ? m::kOwnMarkWidth : m::kOwnMarkSmallWidth;
    return card + 4 + stripes;
}

} // namespace

Rect launchRect(const Rect& cell) {
    float size = hasTitleBar(cell) ? theme::metrics::kSlotTitleBar : cell.size.y;
    return Rect(cell.pos, Vec(size, size));
}

Rect ownMarkRect(Rect cell, const Track& track, const Clip& clip) {
    namespace m = theme::metrics;
    Rect row = nameRow(cell);
    float stripes = activePlayheads(track, &clip) ? m::kPlayheadMarkWidth + 4 : 0;
    float room = ownMarkRoom(row, stripes, clip.hasOwnChain);
    if (room <= 0)
        return Rect();
    bool full = room - 4 - stripes >= m::kOwnMarkWidth;
    Vec size = full ? Vec(m::kOwnMarkWidth, m::kOwnMarkHeight) : Vec(m::kOwnMarkSmallWidth, m::kOwnMarkSmallHeight);
    return Rect(Vec(row.getRight() - room, row.getCenter().y - size.y / 2), size);
}

void drawActionIcon(NVGcontext* vg, NextAction action, Vec c, float size, NVGcolor color) {
    if (action == NextAction::None)
        return;
    // Drawn on a 12 by 12 grid, scaled to `size`.
    float k = size / 12;
    auto at = [&](float x, float y) { return Vec(c.x + (x - 6) * k, c.y + (y - 6) * k); };
    auto move = [&](float x, float y) {
        Vec p = at(x, y);
        nvgMoveTo(vg, p.x, p.y);
    };
    auto line = [&](float x, float y) {
        Vec p = at(x, y);
        nvgLineTo(vg, p.x, p.y);
    };
    nvgBeginPath(vg);
    nvgStrokeColor(vg, color);
    nvgFillColor(vg, color);
    nvgStrokeWidth(vg, theme::metrics::kNextMarkStroke);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    auto dot = [&](float x, float y) {
        Vec p = at(x, y);
        nvgBeginPath(vg);
        nvgCircle(vg, p.x, p.y, 0.95f * k);
        nvgFill(vg);
    };
    switch (action) {
    case NextAction::Next:
        move(2, 6), line(9.5f, 6), move(6.5f, 3), line(9.5f, 6), line(6.5f, 9);
        break;
    case NextAction::Previous:
        move(10, 6), line(2.5f, 6), move(5.5f, 3), line(2.5f, 6), line(5.5f, 9);
        break;
    case NextAction::First:
        move(2.5f, 2.5f), line(2.5f, 9.5f), move(10, 6), line(4.5f, 6), move(7, 3.5f), line(4.5f, 6), line(7, 8.5f);
        break;
    case NextAction::Last:
        move(9.5f, 2.5f), line(9.5f, 9.5f), move(2, 6), line(7.5f, 6), move(5, 3.5f), line(7.5f, 6), line(5, 8.5f);
        break;
    case NextAction::Random: {
        Vec p = at(1.5f, 1.5f);
        nvgRoundedRect(vg, p.x, p.y, 9 * k, 9 * k, 2 * k);
        nvgStroke(vg);
        dot(4.2f, 4.2f), dot(6, 6), dot(7.8f, 7.8f);
        return;
    }
    case NextAction::Other: {
        // Two dice: a different one.
        Vec p = at(1.5f, 4);
        nvgRect(vg, p.x, p.y, 5.5f * k, 6.5f * k);
        move(5, 4), line(5, 1.5f), line(10.5f, 1.5f), line(10.5f, 8), line(7, 8);
        break;
    }
    case NextAction::RoundRobin: {
        Vec o = at(6, 6.5f);
        nvgArc(vg, o.x, o.y, 4 * k, -0.28f * float(M_PI), 1.55f * float(M_PI), NVG_CW);
        move(9.6f, 1.6f), line(9.6f, 4.2f), line(7, 4.2f);
        break;
    }
    case NextAction::Back: {
        move(4.5f, 2.5f), line(2, 5), line(4.5f, 7.5f), move(2, 5), line(7.5f, 5);
        Vec o = at(7.5f, 7.5f);
        nvgArc(vg, o.x, o.y, 2.5f * k, -0.5f * float(M_PI), 0.5f * float(M_PI), NVG_CW);
        line(6, 10);
        break;
    }
    case NextAction::Timeline: {
        // A return arrow: down from the right, round the corner and back to the left.
        move(10, 2.5f), line(10, 5.5f);
        Vec o = at(7, 5.5f);
        nvgArc(vg, o.x, o.y, 3 * k, 0, 0.5f * float(M_PI), NVG_CW);
        line(2.5f, 8.5f), move(5, 6), line(2.5f, 8.5f), line(5, 11);
        break;
    }
    case NextAction::Stop: {
        Vec p = at(3, 3);
        nvgRect(vg, p.x, p.y, 6 * k, 6 * k);
        nvgFill(vg);
        return;
    }
    case NextAction::None:
        return;
    }
    nvgStroke(vg);
}

std::string nextActionCount(const Clip& clip, Tick loopLength, TimeSig ts, double elapsed) {
    if (clip.loopMode != LoopMode::Loop)
        return "";
    int count = std::max<int>(clip.next.count, 1);
    bool bars = clip.next.unit == NextActionUnit::Bars;
    std::string unit = bars ? (count == 1 ? " bar" : " bars") : "";
    double per = bars ? double(ticksPerBar(ts)) : double(loopLength);
    if (!std::isfinite(elapsed) || per <= 0)
        return std::to_string(count) + unit;
    int done = std::clamp(int(std::floor(elapsed / per)) + 1, 1, count);
    return std::to_string(done) + "/" + std::to_string(count) + unit;
}

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

void drawPlayheadMark(NVGcontext* vg, const Device* device, Vec topRight, float maxHeight) {
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
    // One band per playhead, however short the clip: thin bands while each keeps kPlayheadThinBand, and below that
    // every playhead's blocks share the clip, laid over each other. Lines between bands only where there is room.
    bool bands = area.size.y / float(count) >= m::kPlayheadThinBand;
    bool roomy = area.size.y / float(count) >= m::kPlayheadBand;
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
        float gap = roomy ? 1 : 0;
        float top = area.pos.y + (bands ? float(k) * bandHeight : 0) + gap, h = std::max(1.f, bandHeight - 2 * gap);
        if (roomy && k > 0) {
            nvgBeginPath(vg);
            nvgRect(vg, area.pos.x, top - 1, area.size.x, 1);
            nvgFillColor(vg, line);
            nvgFill(vg);
        }
        NVGcolor bandColor = theme::playheadColor(p.color);
        bandColor.a = alpha * (playheads::heardAsSet(device.playheads, p) ? 1.f : 0.3f) * (bands ? 1.f : 0.7f);
        drawPlayheadBand(vg, clip, motion, bandColor, Rect(area.pos.x, top, area.size.x, h), x0, x1, samples, e0, de);
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
        Rect body = nameRow(cell);
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
    bool titleBar = hasTitleBar(cell);
    if (titleBar) {
        // A title bar over the notes, as timeline clips have.
        float r = theme::metrics::kRadiusItem;
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, cell.pos.x, cell.pos.y, cell.size.x, theme::metrics::kSlotTitleBar, r, r, 0, 0);
        nvgFillColor(vg, nvgTransRGBAf(c.onTile, theme::metrics::kSlotTitleBarShade));
        nvgFill(vg);
    }

    Rect body = nameRow(cell);
    nvgSave(vg);
    if (titleBar)
        nvgIntersectScissor(vg, cell.pos.x, cell.pos.y, cell.size.x, cell.size.y);
    else
        nvgIntersectScissor(vg, body.pos.x, body.pos.y, body.size.x, body.size.y);
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.onTile,
                         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    // A clip with its own devices wears their card at the right end, left of the playhead stripes when there are
    // any: white while it plays through them. A narrow row shows the same card, smaller.
    namespace m = theme::metrics;
    float stripes = activePlayheads(*track, clip) ? m::kPlayheadMarkWidth + 4 : 0;
    float markRoom = ownMarkRoom(body, stripes, clip->hasOwnChain);
    if (markRoom > 0) {
        Rect mark = ownMarkRect(cell, *track, *clip);
        if (mark.size.x >= m::kOwnMarkWidth)
            tiles::drawOwnMark(vg, mark.pos.x, body.getCenter().y, clip->usesOwnChain());
        else
            tiles::drawOwnMarkSmall(vg, mark.pos.x, body.getCenter().y, clip->usesOwnChain());
    }
    // The next action's tag, left of those marks: icon and count while the name keeps some room, else the icon alone,
    // small and bare, which most slots have room for.
    float marksRight = markRoom > 0 ? markRoom : stripes;
    float actionRoom = 0;
    bool bareIcon = false;
    std::string actionText;
    draw::TextStyle actionStyle{theme::fonts::mono(), m::kTextRuler - 1, c.onTile, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    NextAction shownAction = clip->next.action != NextAction::None ? clip->next.action : clip->next.otherwise;
    if (clip->next.any()) {
        ClipSpan span = clipSpan(*clip, track->type == TrackType::Drum ? &track->drumLanes : nullptr);
        actionText = nextActionCount(*clip, span.length(), song.timeSig, playing ? status->actionElapsed : NAN);
        float free = body.size.x - marksRight - m::kNextMarkNameRoom;
        float iconOnly = m::kNextMarkIcon + 2 * m::kNextMarkPad;
        float full = actionText.empty() ? iconOnly : iconOnly + 3 + draw::textWidth(vg, actionStyle, actionText);
        if (free >= full) {
            actionRoom = full;
        } else if (body.size.x - marksRight - m::kNextMarkBareNameRoom >= m::kNextMarkIcon) {
            actionRoom = m::kNextMarkIcon;
            bareIcon = true;
            actionText.clear();
        }
    }
    // Held (the Next actions switch is off): the tag fades, as nothing will happen.
    NVGcolor ink = ctx.module()->nextActionsOn() ? c.onTile : nvgTransRGBAf(c.onTile, m::kNextMarkHeldAlpha);
    if (bareIcon) {
        Vec icon(body.getRight() - marksRight - 3 - m::kNextMarkIcon / 2, body.getCenter().y);
        drawActionIcon(vg, shownAction, icon, m::kNextMarkIcon, ink);
    } else if (actionRoom > 0) {
        float h = std::min(m::kNextMarkHeight, body.size.y - 4);
        Rect tag(body.getRight() - marksRight - 3 - actionRoom, body.getCenter().y - h / 2, actionRoom, h);
        draw::roundedRect(vg, tag, theme::metrics::kRadiusItem,
                          nvgTransRGBAf(c.onTile, playing ? m::kNextMarkFillPlaying : m::kNextMarkFill));
        Vec icon(tag.pos.x + m::kNextMarkPad + m::kNextMarkIcon / 2, tag.getCenter().y);
        actionStyle.color = ink;
        drawActionIcon(vg, shownAction, icon, m::kNextMarkIcon, ink);
        if (!actionText.empty())
            draw::text(vg, actionStyle, icon.x + m::kNextMarkIcon / 2 + 3, tag.getCenter().y, actionText);
    }
    // What the tag takes from the name and the preview: it sits left of the playhead stripes too.
    float tagRoom = actionRoom > 0 ? actionRoom + 3 + (markRoom > 0 ? 0 : stripes) : 0;
    std::string label = draw::ellipsize(vg, name, clip->name.empty() ? "Clip" : clip->name,
                                        body.size.x - kTextPad - markRoom - tagRoom);
    draw::text(vg, name, body.pos.x + 3, body.getCenter().y, label);
    // The notes (or the waveform) in miniature, in the room the name leaves; with playheads, where they go over one
    // pass of the clip instead (its notes as written aren't what plays).
    // Under a title bar the notes take the whole width below it; else the room right of the name.
    float previewLeft = titleBar ? cell.pos.x + 3 : body.pos.x + 3 + draw::textWidth(vg, name, label) + kTextPad;
    const std::vector<DrumLane>* lanes = track->type == TrackType::Drum ? &track->drumLanes : nullptr;
    Rect preview = titleBar ? Rect(previewLeft, body.getBottom() + 2, cell.size.x - 6,
                                   cell.getBottom() - body.getBottom() - 4)
                            : Rect(previewLeft, body.pos.y + 3, body.getRight() - previewLeft - 3 - markRoom - tagRoom,
                                   body.size.y - 6);
    float previewRoom = titleBar ? preview.size.x : body.getRight() - previewLeft;
    const Device* readers = clip->audio ? nullptr : activePlayheads(*track, clip);
    if (clip->audio) {
        audioDraw::ClipState state = audioDraw::stateOf(*ctx.module(), *clip);
        std::string mark = audioDraw::stateText(state);
        if (!mark.empty() && previewRoom >= kMinPreviewWidth) {
            draw::TextStyle small{theme::fonts::mono(), theme::metrics::kTextRuler - 1,
                                  audioDraw::stateWarns(state) ? c.queue : c.onTile, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
            draw::text(vg, small, body.getRight() - 3, body.getCenter().y, mark);
            if (!titleBar)
                preview.size.x -= draw::textWidth(vg, small, mark) + 4;
        }
        if (preview.size.x >= kMinPreviewWidth && state.source) {
            const AudioContent& a = *clip->audio;
            double from = double(clip->playStart()), span = double(clip->playEnd() - clip->playStart());
            audioDraw::drawWaveform(vg, preview, preview.pos.x, preview.getRight(), state, a.gainDb,
                                    nvgTransRGBAf(c.onTile, kPreviewAlpha), [&](float x, audioDraw::Column& column) {
                                        double u0 = from + span * double(x - preview.pos.x) / double(preview.size.x);
                                        double u1 = u0 + span / double(preview.size.x);
                                        return audioDraw::framesOfTicks(*clip, state, u0, u1, column);
                                    });
        }
        if (audioDraw::rendering(state))
            audioDraw::drawRenderBar(vg,
                                     Rect(body.pos.x, body.getBottom() - theme::metrics::kStatusBar, body.size.x,
                                          theme::metrics::kStatusBar),
                                     state, c.text0);
    } else if (readers) {
        // Clear of the playhead mark at the right (the own mark's room already is).
        if (!titleBar && markRoom == 0 && tagRoom == 0)
            preview.size.x -= stripes;
        if (preview.size.x >= kMinPreviewWidth)
            drawPlayheadPaths(vg, song, *track, *readers, *clip, preview, preview.pos.x, preview.getRight(), 0,
                              double(clipSpan(*clip, lanes).length()), nvgTransRGBAf(c.onTile, 0.2f), 1);
    } else if (previewRoom >= kMinPreviewWidth) {
        drawNotePreview(vg, *clip, lanes, preview, nvgTransRGBAf(c.onTile, kPreviewAlpha));
    }
    nvgRestore(vg);
    drawPlayheadMark(vg, activePlayheads(*track, clip), Vec(body.getRight() - 4, body.pos.y + 4), body.size.y - 8);

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
