#include "Sources.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/ArrangementEdits.hpp"

#include "../../module/TesseraModule.hpp"

#include <cmath>

namespace tessera {

namespace {

constexpr int kSongMarginBars = 8;
constexpr int kMinSongBars = 32;
constexpr int kMinFitBars = 16;
constexpr int kClipMarginBars = 2;

/// Sets loop start and end as one undo step. Either part may be unchanged.
CommandPtr setLoopCommand(CommandPtr start, CommandPtr end, const std::string& name) {
    auto compound = std::make_unique<CompoundCommand>(name);
    if (start)
        compound->add(std::move(start));
    if (end)
        compound->add(std::move(end));
    return compound->empty() ? nullptr : std::move(compound);
}

} // namespace

// ---- Arranger -----------------------------------------------------------------------------------------------

TimeSig ArrangerSource::timeSig() { return ctx.module()->song().timeSig; }

Tick ArrangerSource::songEnd() {
    const Song& song = ctx.module()->song();
    Tick end = song.loopEnd;
    for (const Marker& m : song.markers)
        end = std::max(end, m.position);
    for (const Track& t : song.tracks)
        for (const ArrangerClip& a : t.arrangement)
            end = std::max(end, a.position + a.length);
    return end;
}

Tick ArrangerSource::fitEnd() { return std::max(songEnd(), kMinFitBars * ticksPerBar(timeSig())); }

double ArrangerSource::contentEnd() {
    Tick bar = ticksPerBar(timeSig());
    return double(std::max(songEnd() + kSongMarginBars * bar, kMinSongBars * bar));
}

Tick ArrangerSource::loopStart() { return ctx.module()->song().loopStart; }
Tick ArrangerSource::loopEnd() { return ctx.module()->song().loopEnd; }
bool ArrangerSource::loopOn() { return ctx.module()->loopEnabled(); }

void ArrangerSource::setLoop(Tick start, Tick end) {
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    m->perform(setLoopCommand(setSongField(song, &Song::loopStart, start, "Set loop start"),
                              setSongField(song, &Song::loopEnd, end, "Set loop end"), "Set loop"));
}

void ArrangerSource::toggleLoop() {
    TesseraModule* m = ctx.module();
    bool on = !m->loopEnabled();
    m->setParamWithUndo(TesseraModule::LOOP_PARAM, on ? 1.f : 0.f, on ? "Loop on" : "Loop off");
}

double ArrangerSource::playhead() { return ctx.module()->playheadTicks(); }

void ArrangerSource::seek(double tick, bool play) {
    TesseraModule* m = ctx.module();
    m->requestSeek(tick);
    if (play)
        m->requestTransport(kRequestPlay);
}

std::vector<Marker> ArrangerSource::markers() { return ctx.module()->song().markers; }

void ArrangerSource::setMarkers(std::vector<Marker> markers, const std::string& undoName) {
    TesseraModule* m = ctx.module();
    m->perform(setSongField(m->song(), &Song::markers, std::move(markers), undoName));
}

MarkerId ArrangerSource::newMarkerId() { return ctx.module()->newId<MarkerId>(); }

void ArrangerSource::addMarker(Tick position) {
    std::vector<Marker> list = markers();
    Marker marker;
    marker.id = newMarkerId();
    marker.position = std::max<Tick>(0, position);
    marker.name = "Marker " + std::to_string(list.size() + 1);
    list.push_back(marker);
    std::stable_sort(list.begin(), list.end(),
                     [](const Marker& a, const Marker& b) { return a.position < b.position; });
    setMarkers(std::move(list), "Add marker");
}

// ---- Clip ---------------------------------------------------------------------------------------------------

const Clip* ClipSource::current() { return ctx.module()->song().findClip(clip); }

const std::vector<DrumLane>* ClipSource::lanes() {
    const Track* t = ctx.module()->song().findTrack(track);
    return t && t->type == TrackType::Drum ? &t->drumLanes : nullptr;
}

TimeSig ClipSource::timeSig() { return ctx.module()->song().timeSig; }

double ClipSource::contentEnd() {
    const Clip* c = current();
    Tick bar = ticksPerBar(timeSig());
    if (!c)
        return double(4 * bar);
    Tick end = std::max(c->length, loopEnd());
    for (const Note& n : c->notes)
        end = std::max(end, n.end());
    // Opened from a timeline clip: far enough to see where it stops.
    double stops = instanceEnd();
    if (std::isfinite(stops))
        end = std::max(end, Tick(std::ceil(stops)));
    return double(end + kClipMarginBars * bar);
}

Tick ClipSource::loopStart() {
    const Clip* c = current();
    return c ? c->loopStart : 0;
}

Tick ClipSource::loopEnd() {
    const Clip* c = current();
    if (!c)
        return ticksPerBar(timeSig());
    return loopLocked() ? clipSpan(*c, lanes()).end : c->loopEnd;
}

bool ClipSource::loopOn() {
    const Clip* c = current();
    return c && c->loopMode == LoopMode::Loop;
}

bool ClipSource::loopLocked() {
    const Clip* c = current();
    return c && clipSpan(*c, lanes()).cycles();
}

void ClipSource::setLoop(Tick start, Tick end) {
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    if (!current() || loopLocked())
        return;
    m->perform(setLoopCommand(setClipField(song, clip, &Clip::loopStart, start, "Set clip loop start"),
                              setClipField(song, clip, &Clip::loopEnd, end, "Set clip loop end"), "Set clip loop"));
}

void ClipSource::toggleLoop() {
    TesseraModule* m = ctx.module();
    const Clip* c = current();
    if (!c)
        return;
    bool on = c->loopMode != LoopMode::Loop;
    m->perform(setClipField(m->song(), clip, &Clip::loopMode, on ? LoopMode::Loop : LoopMode::Off,
                            on ? "Clip loop on" : "Clip loop off"));
}

double ClipSource::playhead() {
    TesseraModule* m = ctx.module();
    const Track* t = m->song().findTrack(track);
    if (!t || !current())
        return NAN;
    LauncherStatus status = m->launcherStatus(track);
    if (status.playing >= 0 && status.playing < int(t->slots.size()) && t->slots[size_t(status.playing)].clip == clip)
        return status.position;
    // Arranger mode: where the timeline plays this clip right now.
    if (m->playMode() == PlayMode::Arranger && !status.overridden) {
        double song = m->playheadTicks();
        if (const ArrangerClip* a = instanceAt(*t, Tick(song)); a && a->clip == clip)
            if (std::optional<double> u = clipTimeAt(*current(), *a, song, lanes()))
                return *u;
    }
    return NAN;
}

int ClipSource::seekSlot() {
    TesseraModule* m = ctx.module();
    const Track* t = m->song().findTrack(track);
    if (!t || !current())
        return -1;
    LauncherStatus status = m->launcherStatus(track);
    bool launched =
        status.playing >= 0 && status.playing < int(t->slots.size()) && t->slots[size_t(status.playing)].clip == clip;
    if (launched)
        return status.playing;
    if (m->playMode() != PlayMode::Launcher)
        return -1;
    for (size_t i = 0; i < t->slots.size(); i++)
        if (t->slots[i].clip == clip)
            return int(i);
    return -1;
}

const ArrangerClip* ClipSource::seekInstance() {
    TesseraModule* m = ctx.module();
    const Track* t = m->song().findTrack(track);
    if (!t || !current() || m->playMode() != PlayMode::Arranger)
        return nullptr;
    if (const ArrangerClip* a = instance())
        return a;
    if (const ArrangerClip* a = instanceAt(*t, Tick(m->playheadTicks())); a && a->clip == clip)
        return a;
    for (const ArrangerClip& a : t->arrangement)
        if (a.clip == clip)
            return &a;
    return nullptr;
}

bool ClipSource::canSeek() { return ctx.module() && (seekSlot() >= 0 || seekInstance()); }

void ClipSource::seek(double tick, bool play) {
    TesseraModule* m = ctx.module();
    const Clip* c = current();
    if (!m || !c)
        return;
    int slot = seekSlot();
    if (slot >= 0) {
        LaunchEvent e;
        e.kind = LaunchEvent::Kind::Slot;
        e.track = track.value;
        e.index = slot;
        e.immediate = true;
        e.start = play;
        ClipSpan span = clipSpan(*c, lanes());
        Tick start = span.looping ? span.start : 0;
        Tick end = span.looping ? span.end : c->playEnd();
        e.clipTime = std::clamp(tick, double(start), std::max(double(start), double(end) - 1));
        m->launch(e);
        return;
    }
    if (const ArrangerClip* a = seekInstance())
        if (std::optional<double> song = songTickAt(*c, *a, tick, m->playheadTicks(), lanes())) {
            m->requestSeek(*song);
            if (play)
                m->requestTransport(kRequestPlay);
        }
}

const ArrangerClip* ClipSource::instance() {
    const arrange::IdSet& selected = ctx.state().clipSelection;
    if (selected.size() != 1 || !current())
        return nullptr;
    for (const Track& t : ctx.module()->song().tracks)
        for (const ArrangerClip& a : t.arrangement)
            if (a.id == *selected.begin() && a.clip == clip)
                return &a;
    return nullptr;
}

double ClipSource::instanceEnd() {
    const ArrangerClip* a = instance();
    if (!a)
        return NAN;
    if (std::isfinite(endPreview))
        return endPreview;
    double stretch = a->stretch > 0 ? double(a->stretch) : 1.0;
    return double(a->offset) + double(a->length) / stretch;
}

double ClipSource::instanceStart() {
    const ArrangerClip* a = instance();
    return a ? double(a->offset) : NAN;
}

void ClipSource::setInstanceEnd(double end) {
    endPreview = NAN;
    const ArrangerClip* a = instance();
    if (!a)
        return;
    TesseraModule* m = ctx.module();
    double stretch = a->stretch > 0 ? double(a->stretch) : 1.0;
    Tick length = std::max<Tick>(1, Tick(std::llround((end - double(a->offset)) * stretch)));
    arrange::Layout before = arrange::layoutOf(m->song());
    arrange::Layout after = before;
    for (arrange::Placed& p : after)
        if (p.clip.id == a->id)
            p.clip.length = length;
    m->perform(arrange::replaceArrangement(m->song(), "Timeline clip length", before, after));
}

} // namespace tessera
