#include "AudioEditor.hpp"

#include "../../engine/AudioPlayer.hpp"
#include "../../model/ArrangementEdits.hpp"
#include "../../model/AudioEdits.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../interaction/Snap.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/MenuTextField.hpp"
#include "../editing/AudioActions.hpp"
#include "AudioClipDraw.hpp"

#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kRulerHeight = 22;
constexpr float kTopStrip = 12;   // the strip along a part's top: gain, fade corners, seam handles
constexpr float kEdgeHit = 6;     // px either side of a part's edge
constexpr float kCornerHit = 10;  // px of a top corner that grabs a fade with no length yet
constexpr float kBeatHit = 4;     // px either side of a beat line (§6.4)
constexpr float kSnapPx = 8;      // an edge catches a beat line this close; Alt frees it
constexpr float kMinGrip = 12;    // a part narrower than this shows no grips
constexpr double kMinWindow = 64; // frames
constexpr double kFine = 0.1;
constexpr double kGainDbPerPx = 0.1;
constexpr float kGainSnapDb = 0.6f;
constexpr double kCurvePerPx = 1.0 / 60;
constexpr float kTakeRowHeight = 44;   // a take's row, when there is room for them all
constexpr float kMinTakeRowHeight = 22;
constexpr float kRowsShare = 0.6f;     // of the pane, at most, so the comp stays visible
constexpr float kHearWidth = 38, kHearHeight = 16;
constexpr float kBeatBand = 9;         // the ruler's lower strip, where beat lines are grabbed; above it a drag picks a range
constexpr float kFlagHeight = 14;
constexpr double kDriftQuietMs = 5, kDriftFarMs = 15; // a hit this far off its grid line is dim / amber

/// "0:01.250": a place in the file.
std::string timeText(double frames, double rate) {
    double s = rate > 0 ? std::max(0.0, frames / rate) : 0;
    int minutes = int(s / 60);
    char text[32];
    snprintf(text, sizeof text, "%d:%06.3f", minutes, s - minutes * 60.0);
    return text;
}

/// IDs for parts that exist only while a drag previews them.
regions::NewId previewIds() {
    auto next = std::make_shared<uint64_t>(uint64_t(1) << 60);
    return [next] { return AudioRegionId((*next)++); };
}

/// Drags a warp marker (§3.5): the beat it pins its hit to moves, snapping to 1/16 notes unless Alt is held. The audio
/// between the markers either side stretches to fit.
class WarpGesture : public DragGesture {
  public:
    WarpGesture(AudioEditor& view, Vec press, int index, int take) : view(view), press(press), index(index), take(take) {
        if (const Clip* c = view.shownClip())
            warps = regions::warpsOf(*c->audio, take);
    }
    void update(Vec pos, Modifiers mods) override {
        if (index < 0 || index >= int(warps.size()))
            return;
        double start = warps[size_t(index)].tick;
        double tick = start + (pos.x - press.x) * view.pixelTicks();
        if (!mods.alt)
            tick = std::round(tick / double(kPpq / 4)) * double(kPpq / 4);
        double lo = index > 0 ? warps[size_t(index - 1)].tick + 1 : -1e12;
        double hi = index + 1 < int(warps.size()) ? warps[size_t(index + 1)].tick - 1 : 1e12;
        std::vector<WarpMarker> next = warps;
        next[size_t(index)].tick = std::clamp(tick, lo, hi);
        view.preview.warps = next;
        view.preview.warpsTake = take;
        shown = next[size_t(index)].tick;
    }
    void commit() override {
        TesseraModule* m = view.context().module();
        if (view.preview.warps && m)
            m->perform(setWarps(m->song(), view.clip, *view.preview.warps, "Move warp marker", take));
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
    int index, take;
    std::vector<WarpMarker> warps;
    double shown = 0;
};

/// Slips a plain clip: its window stays put and the audio slides under it, so the clip plays another part of the file.
class SlipGesture : public DragGesture {
  public:
    SlipGesture(AudioEditor& view, Vec press) : view(view), press(press) {
        start = double(view.shownAudio().sourceStart);
        if (const Clip* c = view.shownClip())
            span = double(c->length) * view.framesPerClipTick();
        if (const AudioSource* src = view.source())
            frames = double(src->frames);
        now = int64_t(start);
    }
    void update(Vec pos, Modifiers mods) override {
        double d = (pos.x - press.x) * view.pixelTicks() * view.framesPerClipTick() * (mods.shift ? kFine : 1);
        double most = std::max(0.0, frames - span);
        now = int64_t(std::llround(std::clamp(start - d, 0.0, most)));
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

/// Drags an edge of a plain clip: the clip starts or ends elsewhere in the file. Edges catch the clip's beat lines.
class EdgeGesture : public DragGesture {
  public:
    EdgeGesture(AudioEditor& view, Vec press, bool leftEdge) : view(view), press(press), leftEdge(leftEdge) {
        perTick = view.framesPerClipTick();
        origin = view.shownAudio().sourceStart;
        if (const Clip* c = view.shownClip())
            length = double(c->length);
        if (const AudioSource* src = view.source())
            frames = double(src->frames);
        if (TesseraModule* m = view.context().module())
            beat = double(ticksPerBeat(m->song().timeSig));
        first = origin;
        last = origin + int64_t(std::llround(length * perTick));
    }
    void update(Vec pos, Modifiers mods) override {
        double tick = (leftEdge ? 0 : length) + (pos.x - press.x) * view.pixelTicks() * (mods.shift ? kFine : 1);
        if (!mods.alt && beat > 0) {
            double nearest = std::round(tick / beat) * beat;
            if (std::fabs(view.tickToX(nearest) - view.tickToX(tick)) <= kSnapPx)
                tick = nearest;
        }
        double frame = double(origin) + tick * perTick;
        if (leftEdge)
            first = int64_t(std::llround(std::clamp(frame, 0.0, double(last) - kMinWindow)));
        else
            last = int64_t(std::llround(std::clamp(frame, double(first) + kMinWindow, frames)));
        AudioRegion r;
        r.start = Tick(std::llround(double(first - origin) / perTick));
        r.length = std::max<Tick>(1, Tick(std::llround(double(last - first) / perTick)));
        r.sourceFrame = first;
        view.preview.regions = regions::List{r};
    }
    void commit() override {
        view.preview = {};
        TesseraModule* m = view.context().module();
        if (first != origin || last != origin + int64_t(std::llround(length * perTick))) {
            m->perform(setAudioWindow(m->song(), view.clip, first, last));
            view.shiftAxis(double(first - origin) / perTick);
        }
    }
    void cancel() override { view.preview = {}; }
    std::string readout() const override {
        const AudioSource* src = view.source();
        double rate = src ? src->sampleRate : 0;
        return leftEdge ? "Start in file " + timeText(double(first), rate)
                        : "End in file " + timeText(double(last), rate);
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    AudioEditor& view;
    Vec press;
    bool leftEdge;
    double perTick = 1, length = 0, frames = 0, beat = 0;
    int64_t origin = 0, first = 0, last = 0;
};

/// Drags a beat line of the ruler: the clip tempo changes so that beat lands where it is dropped, the grid stretching
/// from beat 1 (§6.4). Like every tempo edit, it changes all copies of the clip.
class BeatGesture : public DragGesture {
  public:
    BeatGesture(AudioEditor& view, Vec press, int beat) : view(view), press(press), beat(beat) {
        from = view.shownAudio().sourceTempo;
        now = from;
        beatTicks = double(ticksPerBeat(timeSig()));
    }
    void update(Vec pos, Modifiers mods) override {
        if (beat < 1)
            return;
        double tick = beat * beatTicks + (pos.x - press.x) * view.pixelTicks() * (mods.shift ? kFine : 1);
        double perBeat = std::max(1.0, tick / beat);
        now = std::clamp(from * beatTicks / perBeat, kMinClipTempo, kMaxClipTempo);
        if (!mods.alt)
            now = std::round(now * 10) / 10;
        view.preview.tempo = now;
    }
    void commit() override {
        view.preview = {};
        TesseraModule* m = view.context().module();
        if (std::fabs(now - from) <= 1e-6)
            return;
        bool scales = view.shownAudio().follow != TempoFollow::Off;
        m->perform(setClipTempo(m->song(), view.clip, now));
        if (scales)
            view.scaleAxis(now / from);
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
    double from = 120, now = 120, beatTicks = 960;
};

/// What the part gestures share: the list as the drag began, and how a result is stored.
class PartsGesture : public DragGesture {
  public:
    PartsGesture(AudioEditor& view, Vec press, std::string name)
        : view(view), press(press), name(std::move(name)), before(view.parts()) {
        files = view.files();
        audio = view.shownAudio();
    }
    void cancel() override { view.preview = {}; }

  protected:
    /// Clip ticks the pointer has moved; Shift is fine.
    double dragged(Vec pos, Modifiers mods) const {
        return (pos.x - press.x) * view.pixelTicks() * (mods.shift ? kFine : 1);
    }
    /// `tick` on the grid, unless Alt frees it.
    Tick snapped(double tick, Modifiers mods) const {
        Tick step = mods.alt ? 0 : view.gridStep();
        return step > 0 ? snapPosition(tick, step) : Tick(std::llround(tick));
    }
    /// Shows `list` until the drag ends.
    void show(regions::List list) { view.preview.regions = std::move(list); }
    /// Stores what the drag shows, as one undo step. Nothing happened if it never moved.
    void store(regions::List list) {
        view.preview = {};
        TesseraModule* m = view.context().module();
        if (m && list != before)
            m->perform(regions::setRegions(m->song(), view.clip, std::move(list), name, m->ids()));
    }
    /// IDs for parts a commit really makes.
    regions::NewId realIds() const {
        TesseraModule* m = view.context().module();
        return [m] { return m->newId<AudioRegionId>(); };
    }

    AudioEditor& view;
    Vec press;
    std::string name;
    regions::List before;
    AudioContent audio;
    regions::Files files;
};

/// Moves the selected parts along the clip. Where they land follows the overlap rule, shown while dragging.
class MoveGesture : public PartsGesture {
  public:
    MoveGesture(AudioEditor& view, Vec press, AudioRegionId grabbed) : PartsGesture(view, press, "Move part"), ids(view.selection()) {
        for (const AudioRegion& r : before)
            if (r.id == grabbed)
                anchor = r.start;
        if (const Clip* c = view.shownClip())
            clipLength = c->length;
    }
    void update(Vec pos, Modifiers mods) override {
        double d = dragged(pos, mods);
        Tick step = mods.alt ? 0 : view.gridStep();
        delta = step > 0 ? snapMoveDelta(anchor, d, step) : Tick(std::llround(d));
        show(regions::settle(regions::move(before, ids, delta, clipLength), ids, audio, files, previewIds()));
    }
    void commit() override {
        if (delta == 0) {
            view.preview = {};
            return;
        }
        store(regions::settle(regions::move(before, ids, delta, clipLength), ids, audio, files, realIds()));
    }
    std::string readout() const override {
        char text[32];
        snprintf(text, sizeof text, "%+.2f beats", double(delta) / double(kPpq));
        return text;
    }
    CursorShape cursor() const override { return CursorShape::Move; }

  private:
    regions::Ids ids;
    Tick anchor = 0, clipLength = 0, delta = 0;
};

/// Slides the audio inside one part: it stays where it is and plays another stretch of its file.
class PartSlipGesture : public PartsGesture {
  public:
    PartSlipGesture(AudioEditor& view, Vec press, AudioRegionId id) : PartsGesture(view, press, "Slip part"), id(id) {}
    void update(Vec pos, Modifiers mods) override {
        const AudioRegion* part = nullptr;
        for (const AudioRegion& r : before)
            if (r.id == id)
                part = &r;
        frames = -std::llround(dragged(pos, mods) * framesPerTick(audio, files.rateOf(part ? part->take : -1)));
        result = regions::slip(before, id, frames, audio, files);
        show(result);
    }
    void commit() override { store(result); }
    std::string readout() const override {
        for (const AudioRegion& r : result)
            if (r.id == id)
                return "Start in file " + timeText(double(r.sourceFrame), files.rateOf(r.take));
        return "";
    }
    CursorShape cursor() const override { return CursorShape::Move; }

  private:
    AudioRegionId id;
    int64_t frames = 0;
    regions::List result;
};

/// Trims one part from an edge, bringing back hidden audio or hiding it. An edge dragged into a neighbour makes a
/// crossfade.
class PartEdgeGesture : public PartsGesture {
  public:
    PartEdgeGesture(AudioEditor& view, Vec press, AudioRegionId id, bool leftEdge)
        : PartsGesture(view, press, "Trim part"), id(id), leftEdge(leftEdge) {
        for (const AudioRegion& r : before)
            if (r.id == id)
                from = leftEdge ? r.start : r.end();
    }
    void update(Vec pos, Modifiers mods) override {
        Tick to = snapped(double(from) + dragged(pos, mods), mods);
        moved = leftEdge ? regions::trimStart(before, id, to, audio, files)
                         : regions::trimEnd(before, id, to, audio, files);
        show(regions::settle(moved, {id}, audio, files, previewIds()));
    }
    void commit() override {
        if (moved.empty()) {
            view.preview = {};
            return;
        }
        store(regions::settle(moved, {id}, audio, files, realIds()));
    }
    std::string readout() const override {
        for (const AudioRegion& r : moved)
            if (r.id == id) {
                char text[32];
                snprintf(text, sizeof text, "%.2f beats long", double(r.length) / double(kPpq));
                return text;
            }
        return "";
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    AudioRegionId id;
    bool leftEdge;
    Tick from = 0;
    regions::List moved;
};

/// Drags the top strip: the gain of the selected parts, or of the clip while it has no edits.
class PartGainGesture : public PartsGesture {
  public:
    PartGainGesture(AudioEditor& view, Vec press, AudioRegionId id) : PartsGesture(view, press, "Part gain"), id(id) {
        wholeClip = audio.regions.empty();
        for (const AudioRegion& r : before)
            if (r.id == id)
                anchor = r.gainDb;
        if (wholeClip)
            anchor = audio.gainDb;
    }
    void update(Vec pos, Modifiers mods) override {
        delta = float(-(pos.y - press.y) * kGainDbPerPx * (mods.shift || mods.ctrl ? kFine : 1));
        if (!mods.alt && std::abs(anchor + delta) < kGainSnapDb)
            delta = -anchor;
        if (wholeClip) {
            view.preview.gainDb = shownGain();
            return;
        }
        regions::List next = before;
        for (AudioRegion& r : next)
            if (r.id == id || view.selection().count(r.id))
                r.gainDb = std::clamp(r.gainDb + delta, kSilentDb, kMaxClipGainDb);
        result = next;
        show(next);
    }
    void commit() override {
        if (wholeClip) {
            view.preview = {};
            TesseraModule* m = view.context().module();
            if (m && delta != 0)
                m->perform(editAudio(m->song(), view.clip, [g = shownGain()](AudioContent& a) { a.gainDb = g; }, "Gain"));
            return;
        }
        store(result.empty() ? before : result);
    }
    std::string readout() const override { return "Gain " + audioDraw::dbText(shownGain()); }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    float shownGain() const { return std::clamp(anchor + delta, kSilentDb, kMaxClipGainDb); }
    AudioRegionId id;
    bool wholeClip = false;
    float anchor = 0, delta = 0;
    regions::List result;
};

/// Drags a top corner: a part's fade. It belongs to the clip, so linked copies follow.
class PartFadeGesture : public PartsGesture {
  public:
    PartFadeGesture(AudioEditor& view, Vec press, AudioRegionId id, bool in) : PartsGesture(view, press, "Part fade"), id(id), in(in) {}
    void update(Vec pos, Modifiers mods) override {
        Tick tick = snapped(view.xToTick(pos.x), mods);
        result = before;
        for (AudioRegion& r : result)
            if (r.id == id) {
                Fade& f = in ? r.fadeIn : r.fadeOut;
                f.length = std::clamp<Tick>(in ? tick - r.start : r.end() - tick, 0, r.length);
                shown = f.length;
            }
        show(result);
    }
    void commit() override { store(result.empty() ? before : result); }
    std::string readout() const override {
        char text[32];
        snprintf(text, sizeof text, "Fade %.2f beats", double(shown) / double(kPpq));
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    AudioRegionId id;
    bool in;
    Tick shown = 0;
    regions::List result;
};

/// Drags the dot in a fade's middle: it bends the fade, catching at straight unless Alt is held.
class PartCurveGesture : public PartsGesture {
  public:
    PartCurveGesture(AudioEditor& view, Vec press, AudioRegionId id, bool in) : PartsGesture(view, press, "Fade curve"), id(id), in(in) {
        for (const AudioRegion& r : before)
            if (r.id == id)
                anchor = (in ? r.fadeIn : r.fadeOut).curve;
    }
    void update(Vec pos, Modifiers mods) override {
        float curve = std::clamp(anchor - float((pos.y - press.y) * kCurvePerPx), -1.f, 1.f);
        if (!mods.alt && std::abs(curve) < 0.08f)
            curve = 0;
        result = before;
        for (AudioRegion& r : result)
            if (r.id == id)
                (in ? r.fadeIn : r.fadeOut).curve = curve;
        show(result);
    }
    void commit() override { store(result.empty() ? before : result); }
    std::string readout() const override { return "Fade curve"; }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    AudioRegionId id;
    bool in;
    float anchor = 0;
    regions::List result;
};

/// Drags the handle where two parts meet: right makes their crossfade longer, left shorter, both parts moving evenly.
class SeamGesture : public PartsGesture {
  public:
    SeamGesture(AudioEditor& view, Vec press, AudioRegionId first, AudioRegionId second)
        : PartsGesture(view, press, "Crossfade parts"), first(first), second(second) {
        for (const AudioRegion& r : before) {
            if (r.id == first)
                firstEnd = r.end();
            if (r.id == second)
                secondStart = r.start;
        }
    }
    void update(Vec pos, Modifiers mods) override {
        Tick overlap = firstEnd - secondStart;
        double d = std::max(dragged(pos, mods), -double(overlap) / 2);
        Tick tick = mods.alt ? Tick(std::llround(d)) : std::max(snapped(d, mods), -overlap / 2);
        result = regions::trimEnd(before, first, firstEnd + tick, audio, files);
        result = regions::trimStart(result, second, secondStart - tick, audio, files);
        show(result);
    }
    void commit() override { store(result.empty() ? before : result); }
    std::string readout() const override {
        Tick end = firstEnd, start = secondStart;
        for (const AudioRegion& r : result) {
            if (r.id == first)
                end = r.end();
            if (r.id == second)
                start = r.start;
        }
        char text[40];
        snprintf(text, sizeof text, "Crossfade %.2f beats", double(std::max<Tick>(0, end - start)) / double(kPpq));
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    AudioRegionId first, second;
    Tick firstEnd = 0, secondStart = 0;
    regions::List result;
};

/// Drags along the ruler's upper strip: picks a time range for Delete, Copy and Crop. Without moving, it is a click,
/// which plays from there.
class RangeGesture : public DragGesture {
  public:
    RangeGesture(AudioEditor& view, Vec press, Modifiers mods) : view(view), press(press), pressMods(mods) {}
    void update(Vec pos, Modifiers mods) override {
        moved = moved || std::fabs(pos.x - press.x) > 3;
        if (!moved)
            return;
        Tick step = mods.alt ? 0 : view.gridStep();
        auto snap = [&](double t) { return step > 0 ? snapPosition(t, step) : Tick(std::llround(t)); };
        Tick a = snap(view.xToTick(press.x)), b = snap(view.xToTick(pos.x));
        Tick length = 0;
        if (const Clip* c = view.shownClip())
            length = c->length;
        from = std::clamp(std::min(a, b), Tick(0), length);
        to = std::clamp(std::max(a, b), Tick(0), length);
        view.setRange(to > from ? std::make_optional(std::make_pair(from, to)) : std::nullopt);
    }
    void commit() override {
        if (!moved) {
            view.setRange(std::nullopt);
            view.rulerClick(press, pressMods);
        }
    }
    void cancel() override { view.setRange(std::nullopt); }
    std::string readout() const override {
        char text[32];
        snprintf(text, sizeof text, "%.2f beats", double(to - from) / double(kPpq));
        return text;
    }
    CursorShape cursor() const override { return CursorShape::IBeam; }

  private:
    AudioEditor& view;
    Vec press;
    Modifiers pressMods;
    bool moved = false;
    Tick from = 0, to = 0;
};

/// Drags across a take's row: that stretch of the clip now plays that take.
class CompGesture : public PartsGesture {
  public:
    CompGesture(AudioEditor& view, Vec press, int take) : PartsGesture(view, press, "Comp"), take(take) {}
    void update(Vec pos, Modifiers mods) override {
        moved = moved || std::fabs(pos.x - press.x) > 2;
        const Clip* c = view.shownClip();
        if (!c)
            return;
        Tick a = snapped(view.xToTick(press.x), mods), b = snapped(view.xToTick(pos.x), mods);
        auto [first, last] = regions::takeSpan(*c, take, files);
        from = std::clamp(std::min(a, b), first, last);
        to = std::clamp(std::max(a, b), first, last);
        if (to > from)
            show(regions::comp(before, audio, take, from, to, files, previewIds()));
        else
            view.preview = {};
    }
    void commit() override {
        view.preview = {};
        if (to > from) {
            name = "Comp: " + regions::takeName(audio, take);
            store(regions::comp(before, audio, take, from, to, files, realIds()));
        } else if (moved) {
            view.context().toast(regions::takeName(audio, take) + " has no audio here");
        }
    }
    std::string readout() const override {
        char text[48];
        snprintf(text, sizeof text, "%.2f beats", double(to - from) / double(kPpq));
        return regions::takeName(audio, take) + " · " + text;
    }
    CursorShape cursor() const override { return CursorShape::IBeam; }

  private:
    int take;
    Tick from = 0, to = 0;
    bool moved = false;
};

} // namespace

AudioEditor::~AudioEditor() { stopHearing(); }

void AudioEditor::stopHearing() {
    TesseraModule* m = ctx.module();
    if (m && m->heardTakeClip().valid())
        m->setHeardTake(ClipId(), -1);
    if (m && m->audioPlayer().previewing())
        m->audioPlayer().stopPreview();
}

void AudioEditor::toggleTakes() {
    takesOpen = !takesOpen;
    rowsScroll = 0;
    if (!takesOpen)
        stopHearing();
}

void AudioEditor::makeClipUnique() {
    TesseraModule* m = ctx.module();
    if (const ArrangerClip* a = instance(); m && a)
        m->perform(arrange::makeUnique(m->song(), {a->id}, m->ids()));
}

std::optional<std::pair<Tick, Tick>> AudioEditor::activeRange() {
    if (range)
        return range;
    Tick from = std::numeric_limits<Tick>::max(), to = 0;
    for (const AudioRegion& r : parts())
        if (selected.count(r.id)) {
            from = std::min(from, r.start);
            to = std::max(to, r.end());
        }
    if (to > from)
        return std::make_pair(from, to);
    return std::nullopt;
}

namespace {

/// Parts copied in the audio editor. Session only, and shared by both editors.
regions::Copied clipboard;

} // namespace

bool AudioEditor::copyActive() {
    std::optional<std::pair<Tick, Tick>> active = activeRange();
    if (!active || !source())
        return false;
    TesseraModule* m = ctx.module();
    regions::NewId newId = [m] { return m->newId<AudioRegionId>(); };
    regions::Copied copied = regions::copyRange(parts(), shownAudio(), active->first, active->second, files(), newId);
    if (copied.parts.empty())
        return false;
    clipboard = std::move(copied);
    return true;
}

void AudioEditor::pasteAt(Tick at) {
    TesseraModule* m = ctx.module();
    if (!m || !source() || clipboard.parts.empty())
        return;
    regions::NewId newId = [m] { return m->newId<AudioRegionId>(); };
    regions::List list = parts();
    regions::Ids pasted;
    if (!regions::paste(list, shownAudio(), clipboard, at, files(), newId, &pasted)) {
        ctx.toast("These parts play a file this clip doesn't have, or its tempo is different");
        return;
    }
    store(list, "Paste parts");
    selected = pasted;
    range.reset();
}

void AudioEditor::appendRangeItems(rack::ui::Menu* menu) {
    bool has = activeRange().has_value();
    menu->addChild(rack::createMenuItem("Copy", shortcutLabel(Action::Copy), [this] { copyActive(); }, !has));
    Tick step = gridStep();
    double tick = xToTick(localMouse().x);
    Tick at = step > 0 ? snapPosition(tick, step) : Tick(std::llround(tick));
    menu->addChild(rack::createMenuItem("Paste", shortcutLabel(Action::Paste), [this, at] { pasteAt(at); },
                                        clipboard.parts.empty()));
    menu->addChild(rack::createMenuItem("Duplicate", shortcutLabel(Action::Duplicate), [this] {
        if (std::optional<std::pair<Tick, Tick>> active = activeRange(); active && copyActive())
            pasteAt(active->second);
    }, !has));
    menu->addChild(rack::createMenuItem("Crop to selection", "", [this] {
        TesseraModule* m = ctx.module();
        if (std::optional<std::pair<Tick, Tick>> active = activeRange(); active && m) {
            m->perform(regions::cropToRange(m->song(), clip, active->first, active->second, m->ids()));
            range.reset();
            selected.clear();
            fit();
        }
    }, !has));
}

regions::Files AudioEditor::files() {
    TesseraModule* m = ctx.module();
    const Clip* c = shownClip();
    return m && c ? regions::filesOf(m->song(), *c->audio) : regions::Files();
}

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
    if (preview.warps) {
        if (preview.warpsTake >= 0 && size_t(preview.warpsTake) < a.takes.size())
            a.takes[size_t(preview.warpsTake)].warps = *preview.warps;
        else
            a.warps = *preview.warps;
    }
    if (preview.gainDb)
        a.gainDb = *preview.gainDb;
    return a;
}

const AudioSource* AudioEditor::source() {
    const Clip* c = shownClip();
    TesseraModule* m = ctx.module();
    return c && m ? m->song().sourceOf(*c) : nullptr;
}

regions::List AudioEditor::parts() {
    if (preview.regions)
        return *preview.regions;
    const Clip* c = shownClip();
    const AudioSource* src = source();
    if (!c || !src)
        return {};
    Clip shown = *c;
    shown.audio = shownAudio();
    return regions::of(shown, files());
}

bool AudioEditor::plainClip() {
    const Clip* c = shownClip();
    return c && c->audio->regions.empty() && !c->audio->reverse && c->audio->warps.empty();
}

double AudioEditor::framesPerClipTick() {
    const Clip* c = shownClip();
    const AudioSource* src = source();
    return c && src ? framesPerTick(*c->audio, src->sampleRate) : 1;
}

Tick AudioEditor::gridStep() {
    TesseraModule* m = ctx.module();
    return m ? tessera::snapStep(ctx.state().snap, 1 / ticksPerPx, m->song().timeSig) : 0;
}

const AudioRegion* AudioEditor::find(const regions::List& list, AudioRegionId id) {
    for (const AudioRegion& r : list)
        if (r.id == id)
            return &r;
    return nullptr;
}

std::pair<double, double> AudioEditor::reach(const regions::List& list) {
    if (list.empty() || !source())
        return {0, 1};
    AudioContent a = shownAudio();
    regions::Files f = files();
    const AudioRegion& first = list.front();
    const AudioRegion& last = list.back();
    // Clip tick of frame `frame`, seen from part `r`.
    auto tickOf = [&](const AudioRegion& r, double frame) {
        return double(r.start) + regions::takeTick(a, r.take, frame, f) - regions::takeTick(a, r.take, double(r.sourceFrame), f);
    };
    double perFirst = std::max(framesPerTick(a, f.rateOf(first.take)), 1e-9);
    double perLast = std::max(framesPerTick(a, f.rateOf(last.take)), 1e-9);
    double from = first.reverse ? double(first.end()) - (double(f.framesOf(first.take)) - double(first.sourceFrame)) / perFirst
                                : tickOf(first, 0);
    double to = last.reverse ? double(last.end()) + double(last.sourceFrame) / perLast
                             : tickOf(last, double(f.framesOf(last.take)));
    return {std::min(from, double(first.start)), std::max(to, double(last.end()))};
}

int AudioEditor::takeRowCount() {
    const Clip* c = shownClip();
    return takesOpen && c && !c->audio->takes.empty() ? int(c->audio->takes.size()) + 1 : 0;
}

float AudioEditor::rowsHeight() {
    int n = takeRowCount();
    if (n == 0)
        return 0;
    float body = std::max(0.f, box.size.y - kRulerHeight);
    return std::min(float(n) * kTakeRowHeight, body * kRowsShare);
}

float AudioEditor::rowHeight() {
    int n = takeRowCount();
    return n > 0 ? std::max(kMinTakeRowHeight, rowsHeight() / float(n)) : 0;
}

Rect AudioEditor::rowRect(int index) {
    float h = rowHeight();
    return Rect(0, box.size.y - rowsHeight() + float(index) * h - rowsScroll, box.size.x, h);
}

int AudioEditor::takeOfRow(int index) { return takeRowCount() - 2 - index; }

int AudioEditor::rowAt(Vec pos) {
    int n = takeRowCount();
    if (n == 0 || pos.y < box.size.y - rowsHeight())
        return -1;
    for (int i = 0; i < n; i++)
        if (rowRect(i).contains(pos))
            return i;
    return -1;
}

Rect AudioEditor::hearRect(Rect row) const {
    return Rect(6, row.pos.y + (row.size.y - kHearHeight) / 2, kHearWidth, kHearHeight);
}

Rect AudioEditor::waveRect() {
    return Rect(0, kRulerHeight, box.size.x, std::max(0.f, box.size.y - kRulerHeight - rowsHeight()));
}

void AudioEditor::fit() {
    auto [from, to] = reach(parts());
    double span = std::max(1.0, to - from);
    double width = std::max(1.f, box.size.x);
    // A little room either side, so a part's edges can be grabbed at the file's ends.
    ticksPerPx = span * 1.04 / width;
    left = from - span * 0.02;
    fitted = clip;
    fittedWidth = width;
}

void AudioEditor::step() {
    float width = box.size.x;
    if (clip != fitted || (fittedWidth <= 1 && width > 1)) {
        if (clip != fitted)
            stopHearing();
        selected.clear();
        range.reset();
        rowsScroll = 0;
        fit();
    } else if (width > 1 && fittedWidth > 1 && width != fittedWidth) {
        // A resize (a pane maximised or restored, the inspector opened) keeps the same part of the clip in view.
        ticksPerPx *= fittedWidth / width;
        fittedWidth = width;
    }
    InteractiveView::step();
}

AudioEditor::Hit AudioEditor::hitTest(Vec pos) {
    Hit hit;
    const Clip* c = shownClip();
    const AudioSource* src = source();
    if (!c || !src)
        return hit;
    if (int row = rowAt(pos); row >= 0) {
        hit.take = takeOfRow(row);
        hit.part = hearRect(rowRect(row)).contains(pos) ? Part::TakeHear : Part::TakeRow;
        return hit;
    }
    AudioContent a = shownAudio();
    regions::Files f = files();
    regions::List list = parts();
    if (pos.y < kRulerHeight) {
        bool markers = false;
        double best = 6;
        for (const AudioRegion& r : list) {
            const std::vector<WarpMarker>& warps = regions::warpsOf(a, r.take);
            markers = markers || !warps.empty();
            if (r.reverse)
                continue;
            double shift = regions::shift(a, r, f);
            for (size_t i = 0; i < warps.size(); i++) {
                double t = warps[i].tick - shift;
                if (t < double(r.start) || t > double(r.end()))
                    continue;
                double d = std::fabs(tickToX(t) - pos.x);
                if (d <= best) {
                    best = d;
                    hit.part = Part::Warp;
                    hit.warp = int(i);
                    hit.take = r.take;
                }
            }
        }
        if (hit.part == Part::Warp)
            return hit;
        hit.part = Part::Range;
        if (markers)
            return hit;
        TesseraModule* m = ctx.module();
        double perBeat = m ? double(ticksPerBeat(m->song().timeSig)) : 0;
        if (perBeat <= 0)
            return hit;
        int k = int(std::lround(xToTick(pos.x) / perBeat));
        if (k >= 1 && pos.y >= kRulerHeight - kBeatBand && std::fabs(tickToX(k * perBeat) - pos.x) <= kBeatHit) {
            hit.part = Part::Beat;
            hit.beat = k;
        } else if (k == 0 && plainClip() && pos.y <= kFlagHeight && std::fabs(tickToX(0) - pos.x) <= kEdgeHit) {
            hit.part = Part::Flag;
        }
        return hit;
    }
    Rect wave = waveRect().shrink(Vec(0, 4));
    bool top = pos.y < wave.pos.y + kTopStrip;
    float topY = wave.pos.y, h = wave.size.y;
    // Where two parts meet, the handle comes first: it is the smallest thing to catch.
    if (top)
        for (size_t i = 0; i + 1 < list.size(); i++) {
            if (list[i].end() < list[i + 1].start)
                continue;
            double middle = (double(list[i].end()) + double(list[i + 1].start)) / 2;
            if (std::fabs(tickToX(middle) - pos.x) <= kEdgeHit) {
                hit.part = Part::Seam;
                hit.region = list[i].id;
                hit.next = list[i + 1].id;
                return hit;
            }
        }
    for (const AudioRegion& r : list) {
        float xs = float(tickToX(double(r.start))), xe = float(tickToX(double(r.end())));
        if (pos.x < xs - kEdgeHit || pos.x > xe + kEdgeHit)
            continue;
        hit.region = r.id;
        // The dots in the fades' middles.
        auto dot = [&](const Fade& f, double middle) {
            float y = topY + h - fadeShape(0.5f, f.curve, false) * h;
            return f.length > 0 && std::fabs(tickToX(middle) - pos.x) <= theme::metrics::kAudioHandle &&
                   std::fabs(y - pos.y) <= theme::metrics::kAudioHandle;
        };
        if (dot(r.fadeIn, double(r.start) + double(r.fadeIn.length) / 2)) {
            hit.part = Part::FadeInCurve;
            return hit;
        }
        if (dot(r.fadeOut, double(r.end()) - double(r.fadeOut.length) / 2)) {
            hit.part = Part::FadeOutCurve;
            return hit;
        }
        if (top && pos.x >= xs && pos.x <= xe) {
            float fadeInX = float(tickToX(double(r.start + r.fadeIn.length)));
            float fadeOutX = float(tickToX(double(r.end() - r.fadeOut.length)));
            if (pos.x <= xs + kCornerHit || std::fabs(pos.x - fadeInX) <= kEdgeHit)
                hit.part = Part::FadeIn;
            else if (pos.x >= xe - kCornerHit || std::fabs(pos.x - fadeOutX) <= kEdgeHit)
                hit.part = Part::FadeOut;
            else
                hit.part = Part::Gain;
            return hit;
        }
        if (std::fabs(pos.x - xs) <= kEdgeHit && std::fabs(pos.x - xs) <= std::fabs(pos.x - xe)) {
            // Two parts meeting: each side of the line trims its own part.
            if (pos.x >= xs) {
                hit.part = Part::LeftEdge;
                return hit;
            }
            if (const AudioRegion* other = regions::at(list, double(r.start) - 1, true); !other || other == &r) {
                hit.part = Part::LeftEdge;
                return hit;
            }
            continue;
        }
        if (std::fabs(pos.x - xe) <= kEdgeHit) {
            hit.part = Part::RightEdge;
            return hit;
        }
        if (pos.x > xs && pos.x < xe) {
            hit.part = Part::Body;
            return hit;
        }
    }
    hit.region = AudioRegionId();
    return hit;
}

std::unique_ptr<DragGesture> AudioEditor::startGesture(Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return nullptr;
    Hit hit = hitTest(pos);
    Tool tool = ctx.state().tool;
    bool toolClicks = tool == Tool::Slice || tool == Tool::Erase || tool == Tool::Mute;
    switch (hit.part) {
    case Part::Beat:
        return std::make_unique<BeatGesture>(*this, pos, hit.beat);
    case Part::Warp:
        return std::make_unique<WarpGesture>(*this, pos, hit.warp, hit.take);
    case Part::TakeRow:
        return std::make_unique<CompGesture>(*this, pos, hit.take);
    case Part::Range:
        return std::make_unique<RangeGesture>(*this, pos, mods);
    case Part::Flag:
        return std::make_unique<SlipGesture>(*this, pos);
    default:
        break;
    }
    if (toolClicks || hit.part == Part::None || hit.part == Part::TakeHear)
        return nullptr;
    range.reset();
    bool plain = plainClip();
    switch (hit.part) {
    case Part::LeftEdge:
    case Part::RightEdge:
        if (plain)
            return std::make_unique<EdgeGesture>(*this, pos, hit.part == Part::LeftEdge);
        return std::make_unique<PartEdgeGesture>(*this, pos, hit.region, hit.part == Part::LeftEdge);
    case Part::Gain:
        if (!selected.count(hit.region))
            selected = {hit.region};
        return std::make_unique<PartGainGesture>(*this, pos, hit.region);
    case Part::FadeIn:
    case Part::FadeOut:
        return std::make_unique<PartFadeGesture>(*this, pos, hit.region, hit.part == Part::FadeIn);
    case Part::FadeInCurve:
    case Part::FadeOutCurve:
        return std::make_unique<PartCurveGesture>(*this, pos, hit.region, hit.part == Part::FadeInCurve);
    case Part::Seam:
        return std::make_unique<SeamGesture>(*this, pos, hit.region, hit.next);
    case Part::Body: {
        if (mods.shift) {
            if (!selected.erase(hit.region))
                selected.insert(hit.region);
            return nullptr;
        }
        if (!selected.count(hit.region))
            selected = {hit.region};
        const Clip* c = shownClip();
        regions::List list = parts();
        bool fills = c && list.size() == 1 && list[0].start == 0 && list[0].length >= c->length;
        if (plain && !mods.alt)
            return std::make_unique<SlipGesture>(*this, pos);
        if (mods.alt || fills)
            return std::make_unique<PartSlipGesture>(*this, pos, hit.region);
        return std::make_unique<MoveGesture>(*this, pos, hit.region);
    }
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

void AudioEditor::store(const regions::List& list, const std::string& name) {
    TesseraModule* m = ctx.module();
    if (m)
        m->perform(regions::setRegions(m->song(), clip, list, name, m->ids()));
}

void AudioEditor::cutManyAt(const std::vector<Tick>& ticks) {
    TesseraModule* m = ctx.module();
    if (!m || !source())
        return;
    AudioContent a = shownAudio();
    regions::Files f = files();
    regions::List list = parts();
    regions::NewId newId = [m] { return m->newId<AudioRegionId>(); };
    for (AudioRegion& r : list)
        if (!r.id.valid())
            r.id = newId();
    for (Tick t : ticks)
        list = regions::cut(list, a, t, f, newId);
    store(list, "Cut audio");
}

void AudioEditor::cutAt(Tick at) { cutManyAt({at}); }

void AudioEditor::cutAtHits(AudioRegionId id) {
    TesseraModule* m = ctx.module();
    AudioStore* store = m ? m->audioStore() : nullptr;
    if (!store || !source())
        return;
    regions::List list = parts();
    const AudioRegion* r = find(list, id);
    AudioContent a = shownAudio();
    if (!r || r->reverse) {
        ctx.toast("No hits found in this part yet");
        return;
    }
    SourceStatus st = store->status(takeContent(a, r->take).source);
    if (!st.transients) {
        ctx.toast("No hits found in this part yet");
        return;
    }
    regions::Files f = files();
    double shift = regions::shift(a, *r, f);
    std::vector<Tick> ticks;
    for (int64_t frame : *st.transients) {
        double t = regions::takeTick(a, r->take, double(frame), f) - shift;
        if (t > double(r->start) && t < double(r->end()))
            ticks.push_back(Tick(std::llround(t)));
    }
    if (ticks.empty()) {
        ctx.toast("No hits found in this part");
        return;
    }
    cutManyAt(ticks);
}

void AudioEditor::cutEvery(AudioRegionId id, Tick step) {
    regions::List list = parts();
    const AudioRegion* r = find(list, id);
    if (!r || step <= 0)
        return;
    std::vector<Tick> ticks;
    for (Tick t = (r->start / step + 1) * step; t < r->end(); t += step)
        ticks.push_back(t);
    cutManyAt(ticks);
}

void AudioEditor::removeParts(const regions::Ids& ids, bool close) {
    TesseraModule* m = ctx.module();
    if (!m || ids.empty())
        return;
    regions::List list = parts(), rest;
    for (const AudioRegion& r : list)
        if (!ids.count(r.id))
            rest.push_back(r);
    if (rest.empty()) {
        ctx.toast("A clip keeps at least one part. To remove it, delete the clip on the timeline");
        return;
    }
    if (close)
        m->perform(regions::removeClosingGaps(m->song(), clip, ids, m->ids()));
    else
        store(rest, "Delete part");
    selected.clear();
}

void AudioEditor::takeMenu(int take) {
    TesseraModule* m = ctx.module();
    const Clip* c = shownClip();
    if (!m || !c)
        return;
    ClipId id = clip;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(regions::takeName(*c->audio, take)));
    menu->addChild(rack::createMenuItem("Use for the whole clip", "",
                                        [m, id, take] { m->perform(regions::useTake(m->song(), id, take, m->ids())); }));
    if (take < 0)
        return;
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Name"));
    menu->addChild(new MenuTextField(c->audio->takes[size_t(take)].name, [m, id, take](const std::string& text) {
        m->perform(regions::renameTake(m->song(), id, take, text));
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Delete this take", "",
                                        [m, id, take] { m->perform(regions::removeTake(m->song(), id, take)); }));
}

void AudioEditor::partMenu(const Hit& hit, Tick at) {
    AudioRegionId id = hit.region;
    regions::List list = parts();
    const AudioRegion* r = find(list, id);
    if (!r)
        return;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Part of this clip"));
    menu->addChild(rack::createMenuItem("Cut here", shortcutLabel(Action::Split), [this, at] { cutAt(at); }));
    menu->addChild(rack::createSubmenuItem("Cut at…", "", [this, id](rack::ui::Menu* sub) {
        sub->addChild(rack::createMenuItem("Every hit", "", [this, id] { cutAtHits(id); }));
        TesseraModule* m = ctx.module();
        Tick beat = m ? ticksPerBeat(m->song().timeSig) : kPpq;
        sub->addChild(rack::createMenuItem("Every 1/16", "", [this, id] { cutEvery(id, kPpq / 4); }));
        sub->addChild(rack::createMenuItem("Every 1/8", "", [this, id] { cutEvery(id, kPpq / 2); }));
        sub->addChild(rack::createMenuItem("Every beat", "", [this, id, beat] { cutEvery(id, beat); }));
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    bool muted = r->muted, backwards = r->reverse;
    menu->addChild(rack::createMenuItem(muted ? "Unmute" : "Mute", "", [this, id, muted] {
        regions::List next = parts();
        for (AudioRegion& p : next)
            if (p.id == id)
                p.muted = !muted;
        store(next, muted ? "Unmute part" : "Mute part");
    }));
    menu->addChild(rack::createMenuItem(backwards ? "Play forwards" : "Reverse", "", [this, id, backwards] {
        regions::List next = parts();
        for (AudioRegion& p : next)
            if (p.id == id) {
                p.reverse = !backwards;
                std::swap(p.fadeIn, p.fadeOut);
            }
        store(next, backwards ? "Play part forwards" : "Reverse part");
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    regions::Ids ids = selected.count(id) ? selected : regions::Ids{id};
    appendRangeItems(menu);
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Delete", shortcutLabel(Action::Delete),
                                        [this, ids] { removeParts(ids, false); }));
    menu->addChild(rack::createMenuItem("Delete and close the gap", shortcutLabel(Action::DeleteClosingGap),
                                        [this, ids] { removeParts(ids, true); }));
}

void AudioEditor::click(Vec pos, int button, Modifiers mods) {
    TesseraModule* m = ctx.module();
    if (!m || !source())
        return;
    Hit hit = hitTest(pos);
    Tick step = mods.alt ? 0 : gridStep();
    Tick at = step > 0 ? snapPosition(xToTick(pos.x), step) : Tick(std::llround(xToTick(pos.x)));
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        const Clip* c = shownClip();
        if (hit.part == Part::TakeRow || hit.part == Part::TakeHear) {
            takeMenu(hit.take);
        } else if (hit.part == Part::Warp && c) {
            int take = hit.take;
            std::vector<WarpMarker> warps = regions::warpsOf(*c->audio, take);
            int index = hit.warp;
            ClipId id = clip;
            rack::ui::Menu* menu = rack::createMenu();
            menu->addChild(rack::createMenuLabel("Warp marker"));
            menu->addChild(rack::createMenuItem("Remove this marker", "", [m, id, warps, index, take] {
                std::vector<WarpMarker> rest = warps;
                rest.erase(rest.begin() + index);
                m->perform(setWarps(m->song(), id, rest, "Remove warp marker", take));
            }));
            menu->addChild(rack::createMenuItem("Remove all markers", "", [m, id, take] {
                m->perform(setWarps(m->song(), id, {}, "Remove warp markers", take));
            }));
        } else if (hit.part == Part::Range) {
            appendRangeItems(rack::createMenu());
        } else if (pos.y >= kRulerHeight && hit.part != Part::None) {
            partMenu(hit, at);
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    if (hit.part == Part::TakeHear) {
        bool same = m->heardTakeClip() == clip && m->heardTakeIndex() == hit.take;
        m->setHeardTake(same ? ClipId() : clip, hit.take);
        return;
    }
    if (pos.y >= kRulerHeight) {
        Tool tool = ctx.state().tool;
        if (hit.part == Part::None) {
            selected.clear();
            return;
        }
        if (tool == Tool::Slice) {
            if (mods.shift)
                partMenu(hit, at);
            else
                cutAt(at);
        } else if (tool == Tool::Erase) {
            removeParts({hit.region}, false);
        } else if (tool == Tool::Mute) {
            regions::List next = parts();
            for (AudioRegion& p : next)
                if (p.id == hit.region)
                    p.muted = !p.muted;
            store(next, "Mute part");
        }
        return;
    }
}

void AudioEditor::rulerClick(Vec pos, Modifiers mods) {
    TesseraModule* m = ctx.module();
    if (!m || !source())
        return;
    AudioPlayer& player = m->audioPlayer();
    if (player.previewing()) {
        player.stopPreview();
        return;
    }
    const ArrangerClip* a = instance();
    // Stopped, a click on a clip that isn't on the timeline, or Alt+click on any, plays the clip alone.
    if (!m->playing() && (mods.alt || !a)) {
        const Clip* c = shownClip();
        TrackId owner;
        for (const Track& t : m->song().tracks) {
            bool plays = std::any_of(t.slots.begin(), t.slots.end(), [&](const ClipSlot& s) { return s.clip == clip; }) ||
                         std::any_of(t.arrangement.begin(), t.arrangement.end(),
                                     [&](const ArrangerClip& x) { return x.clip == clip; });
            if (plays && !owner.valid())
                owner = t.id;
        }
        if (owner.valid() && c)
            player.startPreview(clip.value, owner.value, std::clamp(xToTick(pos.x), 0.0, double(c->length)));
        return;
    }
    if (!a) {
        ctx.toast("This clip isn't on the timeline: launch it to hear it");
        return;
    }
    // A click in the ruler plays from there, on the timeline copy of the clip (§6.4).
    double perClip = songTicksPerClipTick(shownAudio(), m->tempo() > 0 ? m->tempo() : 120);
    double tick = double(a->position) + std::max(0.0, xToTick(pos.x) - double(a->offset)) * perClip;
    tick = std::min(tick, double(instanceEnd(*a)));
    m->requestSeek(tick);
    m->requestTransport(kRequestPlay);
}

bool AudioEditor::handleAction(Action action) {
    TesseraModule* m = ctx.module();
    if (!m || !shownClip())
        return false;
    switch (action) {
    case Action::Delete:
    case Action::Cut: {
        if (action == Action::Cut)
            copyActive();
        if (range) {
            regions::NewId newId = [m] { return m->newId<AudioRegionId>(); };
            regions::List rest = regions::clearRange(parts(), shownAudio(), range->first, range->second, files(), newId);
            if (rest.empty()) {
                ctx.toast("A clip keeps at least one part. To remove it, delete the clip on the timeline");
                return true;
            }
            store(rest, "Delete range");
            range.reset();
            selected.clear();
        } else {
            removeParts(selected, false);
        }
        return true;
    }
    case Action::DeleteClosingGap:
        if (range) {
            m->perform(regions::closeRange(m->song(), clip, range->first, range->second, m->ids()));
            range.reset();
            selected.clear();
        } else {
            removeParts(selected, true);
        }
        return true;
    case Action::Copy:
        if (!copyActive())
            ctx.toast("Pick a range in the ruler, or select a part, to copy it");
        return true;
    case Action::Paste:
    case Action::PasteAtMouse: {
        Vec mouse = localMouse();
        Tick step = gridStep();
        std::optional<std::pair<Tick, Tick>> active = activeRange();
        double tick = box.zeroPos().contains(mouse) || !active ? xToTick(mouse.x) : double(active->second);
        pasteAt(step > 0 ? snapPosition(tick, step) : Tick(std::llround(tick)));
        return true;
    }
    case Action::Duplicate: {
        std::optional<std::pair<Tick, Tick>> active = activeRange();
        if (active && copyActive())
            pasteAt(active->second);
        return true;
    }
    case Action::Quantize:
        audioActions::quantizeClip(ctx, clip);
        return true;
    case Action::QuantizeDialog:
        audioActions::appendWarpItems(rack::createMenu(), ctx, clip);
        return true;
    case Action::ToggleTakes:
        if (shownClip()->audio->takes.empty()) {
            ctx.toast("This clip has one take. Record over it to add more");
            return true;
        }
        toggleTakes();
        return true;
    case Action::TransposeUp:
    case Action::TransposeDown: {
        // The take above or below in the rows, for the selected parts.
        const Clip* c = shownClip();
        regions::List list = parts();
        if (c->audio->takes.empty() || selected.empty())
            return false;
        const AudioRegion* first = nullptr;
        for (const AudioRegion& r : list)
            if (selected.count(r.id) && !first)
                first = &r;
        if (!first)
            return false;
        int newest = int(c->audio->takes.size()) - 1;
        int take = std::clamp(first->take + (action == Action::TransposeUp ? 1 : -1), -1, newest);
        if (take == first->take)
            return true;
        auto [from, to] = regions::takeSpan(*c, take, files());
        if (first->end() <= from || first->start >= to) {
            ctx.toast(regions::takeName(*c->audio, take) + " has no audio here");
            return true;
        }
        store(regions::retake(list, selected, take, shownAudio(), files()), "Use " + regions::takeName(*c->audio, take));
        return true;
    }
    case Action::SelectAll:
        selected.clear();
        for (const AudioRegion& r : parts())
            selected.insert(r.id);
        return true;
    case Action::Split: {
        Tick step = gridStep();
        double tick = xToTick(localMouse().x);
        cutAt(step > 0 ? snapPosition(tick, step) : Tick(std::llround(tick)));
        return true;
    }
    default:
        return false;
    }
}

void AudioEditor::doubleClick(Vec pos, Modifiers mods) {
    Hit hit = hitTest(pos);
    if (hit.part != Part::None && hit.part != Part::Body)
        return;
    // In the ruler: show the whole file. On the waveform: pin the nearest hit to the nearest 1/16 (§3.5).
    if (pos.y < kRulerHeight) {
        fit();
        return;
    }
    TesseraModule* m = ctx.module();
    const Clip* c = shownClip();
    if (!m || !c || !source() || hit.part != Part::Body)
        return;
    regions::List list = parts();
    const AudioRegion* r = find(list, hit.region);
    if (!r || r->reverse)
        return;
    AudioContent a = *c->audio;
    regions::Files f = files();
    double frame = regions::frameAt(a, *r, xToTick(pos.x), f);
    if (AudioStore* store = m->audioStore())
        if (SourceStatus st = store->status(takeContent(a, r->take).source); st.transients) {
            double best = 12 * ticksPerPx * framesPerTick(a, f.rateOf(r->take));
            for (int64_t frameHit : *st.transients)
                if (std::fabs(double(frameHit) - frame) < best) {
                    best = std::fabs(double(frameHit) - frame);
                    frame = double(frameHit);
                }
        }
    double shift = regions::shift(a, *r, f);
    double tick = regions::takeTick(a, r->take, frame, f) - shift;
    if (!mods.alt)
        tick = std::round(tick / double(kPpq / 4)) * double(kPpq / 4);
    std::vector<WarpMarker> warps = regions::warpsOf(a, r->take);
    // Without markers yet, the part's start is pinned where it is, so only the hit moves.
    if (warps.empty() && tick + shift > 0)
        warps.push_back({0, int64_t(std::llround(regions::takeFrame(a, r->take, 0, f)))});
    warps.push_back({tick + shift, int64_t(std::llround(frame))});
    m->perform(setWarps(m->song(), clip, warps, "Add warp marker", r->take));
}

CursorShape AudioEditor::hoverCursor(Vec pos, Modifiers mods) {
    Part part = hitTest(pos).part;
    Tool tool = ctx.state().tool;
    if (pos.y >= kRulerHeight && part != Part::None && tool == Tool::Slice)
        return CursorShape::Crosshair;
    switch (part) {
    case Part::LeftEdge:
    case Part::RightEdge:
    case Part::FadeIn:
    case Part::FadeOut:
    case Part::Seam:
    case Part::Beat:
        return CursorShape::ResizeH;
    case Part::Gain:
    case Part::FadeInCurve:
    case Part::FadeOutCurve:
        return CursorShape::ResizeV;
    case Part::Body:
    case Part::Flag:
        return CursorShape::Move;
    case Part::TakeHear:
        return CursorShape::Hand;
    case Part::TakeRow:
    case Part::Range:
        return CursorShape::IBeam;
    default:
        return CursorShape::Arrow;
    }
}

void AudioEditor::wheel(Vec pos, Vec delta, Modifiers mods) {
    // Over rows that don't all fit, the wheel moves through them.
    if (rowAt(pos) >= 0 && !mods.ctrl && !mods.alt) {
        float spare = float(takeRowCount()) * rowHeight() - rowsHeight();
        if (spare > 0) {
            rowsScroll = std::clamp(rowsScroll - float(delta.y), 0.f, spare);
            return;
        }
    }
    auto [from, to] = reach(parts());
    double span = std::max(1.0, to - from);
    if (mods.ctrl || mods.alt) {
        // Zoom about the pointer, from a few samples per pixel to the whole file.
        double at = xToTick(pos.x);
        double factor = std::pow(0.85, double(delta.y) / 50);
        double finest = 0.25 / std::max(framesPerClipTick(), 1e-9);
        ticksPerPx = std::clamp(ticksPerPx * factor, finest, span * 1.5 / std::max(1.f, box.size.x));
        left = at - pos.x * ticksPerPx;
        return;
    }
    double d = double(std::fabs(delta.x) > std::fabs(delta.y) ? delta.x : delta.y);
    left -= d * ticksPerPx;
    double width = box.size.x * ticksPerPx;
    left = std::clamp(left, from - width * 0.5, to - width * 0.5);
}

void AudioEditor::drawRuler(NVGcontext* vg, Rect r) {
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    TesseraModule* m = ctx.module();
    const Clip* cl = shownClip();
    if (!m || !cl)
        return;
    // While a beat line is dragged the audio stays and the grid stretches from beat 1.
    double scale = preview.tempo && cl->audio->sourceTempo > 0 ? cl->audio->sourceTempo / *preview.tempo : 1;
    double perBeat = double(ticksPerBeat(m->song().timeSig)) * scale;
    int beatsPerBar = std::max(1, int(m->song().timeSig.num));
    int first = int(std::floor(xToTick(0) / perBeat)) - 1;
    int last = int(std::ceil(xToTick(box.size.x) / perBeat)) + 1;
    // Too many lines to read: only the bars.
    bool beats = tickToX(perBeat) - tickToX(0) >= 6;
    draw::TextStyle label{theme::fonts::mono(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    double length = double(cl->length);
    for (int k = std::max(first, -100000); k <= last && k < first + 4000; k++) {
        bool bar = ((k % beatsPerBar) + beatsPerBar) % beatsPerBar == 0;
        if (!bar && !beats)
            continue;
        double t = k * perBeat;
        float x = float(tickToX(t));
        bool inside = t >= -0.5 && t <= length + 0.5;
        nvgBeginPath(vg);
        nvgRect(vg, x, bar ? r.pos.y + 6 : r.getBottom() - 6, 1, bar ? r.size.y - 6 : 6);
        nvgFillColor(vg, inside ? (bar ? c.text1 : c.text2) : c.line1);
        nvgFill(vg);
        if (bar && k != 0 && tickToX(beatsPerBar * perBeat) - tickToX(0) >= 24)
            draw::text(vg, label, x + 3, r.pos.y + 8, std::to_string(k / beatsPerBar + (k >= 0 ? 1 : 0)));
    }
    // Beat 1's flag, where the clip starts.
    float xs = float(tickToX(0));
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

void AudioEditor::drawPart(NVGcontext* vg, const AudioRegion& r, Rect wave, uint32_t color) {
    const theme::Palette& c = theme::colors();
    TesseraModule* m = ctx.module();
    const Clip* cl = shownClip();
    if (!m || !cl || !source())
        return;
    float xs = float(tickToX(double(r.start))), xe = float(tickToX(double(r.end())));
    if (xe < 0 || xs > box.size.x)
        return;
    bool isSelected = selected.count(r.id) > 0;
    Rect inside(xs, wave.pos.y, xe - xs, wave.size.y);
    AudioContent a = shownAudio();
    bool hasTakes = !a.takes.empty();
    NVGcolor tint = hasTakes ? theme::takeColor(r.take) : theme::track(color);
    draw::roundedRect(vg, inside, theme::metrics::kRadiusItem, nvgTransRGBAf(tint, r.muted ? 0.07f : 0.18f));
    audioDraw::ClipState state = audioDraw::stateOf(*m, *cl);
    state.status = state.statusOf(r.take);
    regions::Files f = state.files;
    NVGcolor bright = r.muted ? nvgTransRGBAf(c.text2, 0.4f) : nvgLerpRGBA(tint, c.text0, 0.35f);
    double perTick = framesPerTick(a, f.rateOf(r.take));
    double fileFrames = double(f.framesOf(r.take));
    audioDraw::drawWaveform(vg, wave, std::max(0.f, xs), std::min(box.size.x, xe), state, a.gainDb + r.gainDb, bright,
                            [&](float x, audioDraw::Column& column) {
                                double f0 = regions::frameAt(a, r, xToTick(x), f);
                                double f1 = f0 + (r.reverse ? -1 : 1) * ticksPerPx * perTick;
                                column.from = std::min(f0, f1);
                                column.to = std::max(f0, f1);
                                return column.to > 0 && column.from < fileFrames;
                            });
    // Fades shade what they hide, as on the timeline.
    auto shade = [&](double fromTick, double length, bool in, float curve) {
        if (length <= 0)
            return;
        float x0 = float(tickToX(fromTick)), x1 = float(tickToX(fromTick + length));
        nvgBeginPath(vg);
        nvgMoveTo(vg, x0, wave.pos.y);
        int steps = std::clamp(int((x1 - x0) / 4), 2, 64);
        for (int i = 0; i <= steps; i++) {
            float f = float(i) / float(steps);
            float g = fadeShape(in ? f : 1 - f, curve, false);
            nvgLineTo(vg, x0 + (x1 - x0) * f, wave.getBottom() - g * wave.size.y);
        }
        nvgLineTo(vg, x1, wave.pos.y);
        nvgClosePath(vg);
        nvgFillColor(vg, nvgTransRGBAf(c.bg0, 0.55f));
        nvgFill(vg);
    };
    shade(double(r.start), double(r.fadeIn.length), true, r.fadeIn.curve);
    shade(double(r.end() - r.fadeOut.length), double(r.fadeOut.length), false, r.fadeOut.curve);
    auto dot = [&](double tick, float curve) {
        float y = wave.getBottom() - fadeShape(0.5f, curve, false) * wave.size.y;
        nvgBeginPath(vg);
        nvgCircle(vg, float(tickToX(tick)), y, theme::metrics::kAudioHandle / 2);
        nvgFillColor(vg, c.select);
        nvgFill(vg);
    };
    if (r.fadeIn.length > 0)
        dot(double(r.start) + double(r.fadeIn.length) / 2, r.fadeIn.curve);
    if (r.fadeOut.length > 0)
        dot(double(r.end()) - double(r.fadeOut.length) / 2, r.fadeOut.curve);

    // The part's edges, with grips to show they move.
    if (xe - xs >= kMinGrip)
        for (float x : {xs, xe}) {
            nvgBeginPath(vg);
            nvgRect(vg, x - 1, wave.pos.y, 2, wave.size.y);
            nvgFillColor(vg, tint);
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRect(vg, x - 3, wave.getCenter().y - 10, 6, 20);
            nvgFillColor(vg, tint);
            nvgFill(vg);
        }
    if (isSelected) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, xs + 0.5f, wave.pos.y + 0.5f, xe - xs - 1, wave.size.y - 1, theme::metrics::kRadiusItem);
        nvgStrokeWidth(vg, 1.5f);
        nvgStrokeColor(vg, c.select);
        nvgStroke(vg);
    }
    std::string words;
    if (hasTakes)
        words = "T" + std::to_string(r.take + 2);
    if (r.gainDb != 0)
        words += (words.empty() ? "" : "  ") + audioDraw::dbText(r.gainDb);
    if (r.muted)
        words += words.empty() ? "Muted" : "  Muted";
    if (r.reverse)
        words += words.empty() ? "◀" : "  ◀";
    if (!words.empty() && xe - xs > 44) {
        draw::TextStyle small{theme::fonts::mono(), theme::metrics::kTextRuler, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
        draw::text(vg, small, xs + 12, wave.pos.y + 16, words);
    }
}

void AudioEditor::drawDrift(NVGcontext* vg, double tick, Rect wave) {
    TesseraModule* m = ctx.module();
    double grid = double(ctx.state().audioQuantize.grid);
    if (!m || grid <= 0)
        return;
    double target = std::round(tick / grid) * grid;
    float x0 = float(tickToX(tick)), x1 = float(tickToX(target));
    double ms = std::fabs(tick - target) * 60000.0 / ((m->tempo() > 0 ? m->tempo() : 120) * double(kPpq));
    if (std::fabs(x1 - x0) < 1 || ms < 0.5)
        return;
    const theme::Palette& c = theme::colors();
    NVGcolor color = ms > kDriftFarMs ? c.queue : ms < kDriftQuietMs ? nvgTransRGBAf(c.text2, 0.5f) : c.text1;
    float y = wave.getBottom() - 8;
    nvgBeginPath(vg);
    nvgRect(vg, std::min(x0, x1), y, std::fabs(x1 - x0), 1.5f);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void AudioEditor::drawTakeRows(NVGcontext* vg) {
    int n = takeRowCount();
    TesseraModule* m = ctx.module();
    const Clip* cl = shownClip();
    if (n == 0 || !m || !cl)
        return;
    const theme::Palette& c = theme::colors();
    AudioContent a = shownAudio();
    regions::List list = parts();
    audioDraw::ClipState state = audioDraw::stateOf(*m, *cl);
    regions::Files f = state.files;
    float top = box.size.y - rowsHeight();
    nvgSave(vg);
    nvgIntersectScissor(vg, 0, top, box.size.x, rowsHeight());
    for (int i = 0; i < n; i++) {
        Rect row = rowRect(i);
        if (row.getBottom() < top || row.pos.y > box.size.y)
            continue;
        int take = takeOfRow(i);
        NVGcolor tint = theme::takeColor(take);
        nvgBeginPath(vg);
        nvgRect(vg, 0, row.pos.y + row.size.y - 1, box.size.x, 1);
        nvgFillColor(vg, c.line1);
        nvgFill(vg);
        Rect body = row.shrink(Vec(0, 1));
        audioDraw::ClipState rowState = state;
        rowState.status = state.statusOf(take);
        double perTick = framesPerTick(a, f.rateOf(take));
        double fileFrames = double(f.framesOf(take));
        auto columns = [&](float x, audioDraw::Column& column) {
            double u = xToTick(x);
            if (u < 0 || u >= double(cl->length))
                return false;
            double f0 = regions::takeFrame(a, take, u, f);
            column.from = f0;
            column.to = f0 + ticksPerPx * perTick;
            return column.to > 0 && column.from < fileFrames;
        };
        // Bright where the comp plays this take.
        std::vector<std::pair<float, float>> used;
        for (const AudioRegion& r : list)
            if (r.take == take && !r.muted)
                used.push_back({float(tickToX(double(r.start))), float(tickToX(double(r.end())))});
        for (const auto& [x0, x1] : used)
            draw::roundedRect(vg, Rect(x0, body.pos.y, x1 - x0, body.size.y), 2, nvgTransRGBAf(tint, 0.22f));
        float x0 = std::max(0.f, float(tickToX(0))), x1 = std::min(box.size.x, float(tickToX(double(cl->length))));
        audioDraw::drawWaveform(vg, body, x0, x1, rowState, a.gainDb, nvgTransRGBAf(c.text2, 0.45f), columns);
        NVGcolor bright = nvgLerpRGBA(tint, c.text0, 0.35f);
        for (const auto& [ux0, ux1] : used)
            audioDraw::drawWaveform(vg, body, std::max(x0, ux0), std::min(x1, ux1), rowState, a.gainDb, bright, columns);
        // No audio here: the pass stopped early, or started late.
        auto [first, last] = regions::takeSpan(*cl, take, f);
        auto hatch = [&](double fromTick, double toTick) {
            float hx0 = std::max(0.f, float(tickToX(fromTick))), hx1 = std::min(box.size.x, float(tickToX(toTick)));
            if (hx1 <= hx0)
                return;
            nvgSave(vg);
            nvgIntersectScissor(vg, hx0, body.pos.y, hx1 - hx0, body.size.y);
            nvgBeginPath(vg);
            for (float s = hx0 - body.size.y; s < hx1; s += 8) {
                nvgMoveTo(vg, s, body.getBottom());
                nvgLineTo(vg, s + body.size.y, body.pos.y);
            }
            nvgStrokeWidth(vg, 1);
            nvgStrokeColor(vg, c.line2);
            nvgStroke(vg);
            nvgRestore(vg);
        };
        if (last <= first) {
            hatch(0, double(cl->length));
        } else {
            hatch(0, double(first));
            hatch(double(last), double(cl->length));
        }
        Rect hear = hearRect(row);
        bool lit = m->heardTakeClip() == clip && m->heardTakeIndex() == take;
        draw::roundedRect(vg, hear, theme::metrics::kRadiusItem, lit ? c.play : c.bg2);
        draw::TextStyle hearText{theme::fonts::ui(), theme::metrics::kTextRuler, lit ? c.ink : c.text0,
                                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, hearText, hear.getCenter().x, hear.getCenter().y, "Hear");
        draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, name, hear.getRight() + 6, hear.getCenter().y, regions::takeName(a, take));
    }
    nvgRestore(vg);
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
    regions::List list = parts();
    regions::Files f = state.files;

    // The rest of the file, dim, before the first part and after the last.
    if (!list.empty()) {
        NVGcolor dim = nvgTransRGBAf(c.text2, 0.35f);
        auto ghost = [&](const AudioRegion& r, float from, float to) {
            audioDraw::ClipState ghostState = state;
            ghostState.status = state.statusOf(r.take);
            double perTick = framesPerTick(a, f.rateOf(r.take));
            double fileFrames = double(f.framesOf(r.take));
            audioDraw::drawWaveform(vg, wave, std::max(0.f, from), std::min(box.size.x, to), ghostState, a.gainDb, dim,
                                    [&](float x, audioDraw::Column& column) {
                                        double first = regions::frameAt(a, r, xToTick(x), f);
                                        double last = first + (r.reverse ? -1 : 1) * ticksPerPx * perTick;
                                        column.from = std::min(first, last);
                                        column.to = std::max(first, last);
                                        return column.to > 0 && column.from < fileFrames;
                                    });
        };
        ghost(list.front(), 0, float(tickToX(double(list.front().start))));
        ghost(list.back(), float(tickToX(double(list.back().end()))), box.size.x);
    }
    for (const AudioRegion& r : list)
        drawPart(vg, r, wave, color);

    // Where two parts meet: an X where they overlap, and the handle that makes it longer.
    for (size_t i = 0; i + 1 < list.size(); i++) {
        double end = double(list[i].end()), start = double(list[i + 1].start);
        if (end < start)
            continue;
        float x0 = float(tickToX(start)), x1 = float(tickToX(end));
        if (x1 > x0) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, x0, wave.getBottom());
            nvgLineTo(vg, x1, wave.pos.y);
            nvgMoveTo(vg, x0, wave.pos.y);
            nvgLineTo(vg, x1, wave.getBottom());
            nvgStrokeWidth(vg, 1);
            nvgStrokeColor(vg, c.text1);
            nvgStroke(vg);
        }
        float middle = (x0 + x1) / 2;
        Rect handle(middle - 5, wave.pos.y, 10, 7);
        draw::roundedRect(vg, handle, 2, c.select);
    }

    // The loop, when it covers less than the whole clip.
    if (cl->loopMode == LoopMode::Loop && !(cl->loopStart == 0 && cl->loopEnd >= cl->length)) {
        for (Tick t : {cl->loopStart, cl->loopEnd}) {
            float x = float(tickToX(double(t)));
            nvgBeginPath(vg);
            nvgRect(vg, x - 0.5f, wave.pos.y, 1, wave.size.y);
            nvgFillColor(vg, c.queue);
            nvgFill(vg);
        }
    }
    // Transients: a small mark under the waveform where each hit starts (Split at transients cuts there).
    std::vector<double> hits;
    nvgBeginPath(vg);
    for (const AudioRegion& r : list) {
        const SourceStatus& st = state.statusOf(r.take);
        if (r.muted || !st.transients)
            continue;
        double perTick = framesPerTick(a, f.rateOf(r.take));
        double first = regions::frameAt(a, r, double(r.start), f), last = regions::frameAt(a, r, double(r.end()), f);
        double low = std::min(first, last), high = std::max(first, last);
        double shift = r.reverse ? 0 : regions::shift(a, r, f);
        for (int64_t frame : *st.transients) {
            if (double(frame) < low || double(frame) >= high)
                continue;
            double tick = r.reverse ? double(r.end()) - (double(frame) - double(r.sourceFrame)) / perTick
                                    : regions::takeTick(a, r.take, double(frame), f) - shift;
            float x = float(tickToX(tick));
            if (x < 0 || x > box.size.x)
                continue;
            float y = wave.getBottom();
            nvgMoveTo(vg, x, y - 6);
            nvgLineTo(vg, x - 3, y);
            nvgLineTo(vg, x + 3, y);
            nvgClosePath(vg);
            hits.push_back(tick);
        }
    }
    nvgFillColor(vg, nvgTransRGBAf(c.text0, 0.6f));
    nvgFill(vg);
    // How far each hit is from the grid, before Quantize moves it.
    for (double tick : hits)
        drawDrift(vg, tick, wave);
    // Warp markers: a line through the hit they pin, with a handle in the ruler. Each shows with the part that plays it.
    for (const AudioRegion& r : list) {
        if (r.reverse)
            continue;
        double shift = regions::shift(a, r, f);
        for (const WarpMarker& w : regions::warpsOf(a, r.take)) {
            double t = w.tick - shift;
            if (t < double(r.start) || t > double(r.end()))
                continue;
            float x = float(tickToX(t));
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
    }
    // The Slice tool shows where a click would cut.
    if (ctx.state().tool == Tool::Slice && !hasGesture()) {
        Vec mouse = localMouse();
        if (box.zeroPos().contains(mouse) && mouse.y >= kRulerHeight && hitTest(mouse).part != Part::None) {
            Tick step = gridStep();
            double tick = step > 0 ? double(snapPosition(xToTick(mouse.x), step)) : xToTick(mouse.x);
            nvgBeginPath(vg);
            nvgRect(vg, float(tickToX(tick)) - 0.5f, wave.pos.y, 1, wave.size.y);
            nvgFillColor(vg, c.select);
            nvgFill(vg);
        }
    }
    drawTakeRows(vg);
    // The time range picked in the ruler.
    if (range) {
        float x0 = float(tickToX(double(range->first))), x1 = float(tickToX(double(range->second)));
        nvgBeginPath(vg);
        nvgRect(vg, x0, 0, x1 - x0, wave.getBottom());
        nvgFillColor(vg, nvgTransRGBAf(c.select, 0.2f));
        nvgFill(vg);
    }
    // What the clip does, in words.
    std::string note = a.takes.empty() ? src->name : "Comp";
    std::string st = audioDraw::stateText(state);
    if (!st.empty())
        note += "  " + st;
    draw::TextStyle small{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
    draw::text(vg, small, 6, wave.pos.y + 2, note);
    // Where a preview has got to.
    if (m->audioPlayer().previewing()) {
        float x = float(tickToX(m->audioPlayer().previewTick()));
        nvgBeginPath(vg);
        nvgRect(vg, x - 0.5f, 0, 1, box.size.y);
        nvgFillColor(vg, c.play);
        nvgFill(vg);
    }
    // Where playback is in the clip, while the timeline copy plays.
    if (const ArrangerClip* inst = instance(); inst && m->playing() && m->playMode() == PlayMode::Arranger) {
        double t = m->playheadTicks();
        double perClip = songTicksPerClipTick(a, m->tempo() > 0 ? m->tempo() : 120);
        if (t >= double(inst->position) && t < double(inst->position) + double(inst->length) * perClip) {
            double u = double(inst->offset) + (t - double(inst->position)) / perClip;
            if (cl->loopMode == LoopMode::Loop && cl->loopEnd > cl->loopStart && u >= double(cl->loopEnd))
                u = double(cl->loopStart) + std::fmod(u - double(cl->loopStart), double(cl->loopEnd - cl->loopStart));
            float x = float(tickToX(u));
            nvgBeginPath(vg);
            nvgRect(vg, x - 0.5f, 0, 1, box.size.y);
            nvgFillColor(vg, c.play);
            nvgFill(vg);
        }
    }
    nvgRestore(vg);
}

} // namespace tessera
