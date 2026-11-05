#include "OutputModuleBase.hpp"

#include "../engine/AudioPlayer.hpp"
#include "../model/AudioSource.hpp"
#include "Registry.hpp"
#include "TesseraModule.hpp"

#include <algorithm>

namespace tessera {

namespace {
// Reporting is a full memory fence; every 32nd sample is enough, it only delays freeing a replaced bus a little.
constexpr int64_t kReportEvery = 32;
} // namespace

OutputModuleBase::~OutputModuleBase() {
    Registry::get().removeOutput(this);
    releaseReturn();
}

void OutputModuleBase::process(const ProcessArgs& args) {
    const OutputBus* b = bus.get();
    TrackId track(boundTrackId.load(std::memory_order_relaxed));
    const TrackFrame* frame = b ? b->read(args.frame, busSlot.load(std::memory_order_relaxed), track) : nullptr;
    if (frame)
        writeFrame(*frame);
    else
        silenceGates();
    if (audioPorts.outL >= 0 || audioPorts.inL >= 0)
        processAudio(args, b, track);
    if (args.frame % kReportEvery == 0)
        bus.markProcessed(args.frame);
}

void OutputModuleBase::processAudio(const ProcessArgs& args, const OutputBus* b, TrackId track) {
    bool on = b && audioOn.load(std::memory_order_relaxed);
    // The source Level and Use, ramped over about 5 ms so a change never clicks.
    float target = inputTarget.load(std::memory_order_relaxed);
    inputGain += (target - inputGain) * std::min(1.f, args.sampleTime / 0.005f);
    if (args.frame % 32 == 0) {
        float fall = float(std::pow(10.0, -20.0 / 20.0 * 32 * args.sampleTime));
        inputMeter.store(std::max(inputPeak, inputMeter.load(std::memory_order_relaxed) * fall),
                         std::memory_order_relaxed);
        inputPeak = 0;
    }
    if (audioPorts.outL >= 0) {
        const AudioOutFrame* f =
            on ? b->readAudio(args.frame, audioSlot.load(std::memory_order_relaxed), track) : nullptr;
        outputs[size_t(audioPorts.outL)].setVoltage(f ? f->l : 0.f);
        outputs[size_t(audioPorts.outR)].setVoltage(f ? f->r : 0.f);
    }
    if (audioPorts.sourceCount > 0) {
        // Each source at its own level and pan (a lane strip in the mixer), ramped so a move never clicks.
        float sumL = 0, sumR = 0;
        bool any = false;
        float follow = sourcesFresh ? 1.f : 1.f - std::exp(-args.sampleTime / 0.005f);
        sourcesFresh = false;
        for (int k = 0; k < audioPorts.sourceCount; k++) {
            size_t i = size_t(k);
            sourceGainL[i] += (sourceTargetL[i].load(std::memory_order_relaxed) - sourceGainL[i]) * follow;
            sourceGainR[i] += (sourceTargetR[i].load(std::memory_order_relaxed) - sourceGainR[i]) * follow;
            rack::engine::Input& in = inputs[size_t(audioPorts.sourceFirst + k)];
            if (!in.isConnected())
                continue;
            float x = in.getVoltageSum();
            float l = x * sourceGainL[i], r = x * sourceGainR[i];
            sumL += l;
            sumR += r;
            sourcePeak[i] = std::max(sourcePeak[i], std::max(std::fabs(l), std::fabs(r)));
            any = true;
        }
        if (++meterCount >= 32) {
            meterCount = 0;
            float fall = float(std::pow(10.0, -20.0 / 20.0 * 32 * args.sampleTime));
            for (int k = 0; k < audioPorts.sourceCount; k++) {
                size_t i = size_t(k);
                sourceMeter[i].store(std::max(sourcePeak[i], sourceMeter[i].load(std::memory_order_relaxed) * fall),
                                     std::memory_order_relaxed);
                sourcePeak[i] = 0;
            }
        }
        int slot = sourceSlot.load(std::memory_order_relaxed);
        if (on && slot >= 0 && any) {
            sumL *= inputGain;
            sumR *= inputGain;
            inputPeak = std::max(inputPeak, std::max(std::fabs(sumL), std::fabs(sumR)));
            b->returns().write(args.frame, slot, track, sumL, sumR);
        }
    }
    if (audioPorts.inL < 0)
        return;
    rack::engine::Input& inL = inputs[size_t(audioPorts.inL)];
    rack::engine::Input& inR = inputs[size_t(audioPorts.inR)];
    float l = 0, r = 0;
    // A poly cable is summed to stereo; R follows L when only L is patched, and the other way round.
    bool hasL = inL.isConnected(), hasR = inR.isConnected();
    if (hasL)
        l = inL.getVoltageSum();
    if (hasR)
        r = inR.getVoltageSum();
    if (hasL && !hasR)
        r = l;
    if (hasR && !hasL)
        l = r;
    int slot = returnSlot.load(std::memory_order_relaxed);
    if (on && slot >= 0 && (hasL || hasR)) {
        float gl = l * inputGain, gr = r * inputGain;
        inputPeak = std::max(inputPeak, std::max(std::fabs(gl), std::fabs(gr)));
        b->returns().write(args.frame, slot, track, gl, gr);
    }
    if (audioPorts.inLight >= 0) {
        // Green while audio arrives, red above 0 dB (10 V); the peak falls so short hits still show.
        float peak = std::max(std::fabs(l), std::fabs(r));
        inPeak = std::max(peak, inPeak * 0.9995f);
        bool arriving = on && (hasL || hasR) && inPeak > 0.01f;
        // Amber (half green, full red) while a cable still brings audio to a track that was deleted.
        bool gone = !on && trackGone.load(std::memory_order_relaxed) && (hasL || hasR);
        float green = arriving ? 1.f : gone ? 0.5f : 0.f;
        float red = (arriving && inPeak > kFullScaleVolts) || gone ? 1.f : 0.f;
        lights[size_t(audioPorts.inLight)].setBrightnessSmooth(green, args.sampleTime);
        lights[size_t(audioPorts.inLight) + 1].setBrightnessSmooth(red, args.sampleTime);
    }
}

void OutputModuleBase::processBypass(const ProcessArgs& args) {
    // Bypassed, IN never reaches OUT (in an effect loop that would feed back) and nothing goes back to Tessera.
    silenceGates();
    if (audioPorts.outL >= 0) {
        outputs[size_t(audioPorts.outL)].setVoltage(0.f);
        outputs[size_t(audioPorts.outR)].setVoltage(0.f);
    }
    if (args.frame % kReportEvery == 0)
        bus.markProcessed(args.frame);
}

void OutputModuleBase::releaseReturn() {
    for (std::atomic<int>* held : {&returnSlot, &sourceSlot}) {
        int slot = held->exchange(-1);
        if (returnOwner && slot >= 0)
            returnOwner->returns().release(slot);
    }
    returnOwner.reset();
}

bool OutputModuleBase::sourcePatched(int k) const {
    return k >= 0 && k < audioPorts.sourceCount && inputs[size_t(audioPorts.sourceFirst + k)].channels > 0;
}

float OutputModuleBase::sourceLevel(int k) const {
    return k >= 0 && k < audioPorts.sourceCount ? sourceMeter[size_t(k)].load(std::memory_order_relaxed) : 0.f;
}

int OutputModuleBase::activeSource() const {
    if (audioPorts.sourceCount <= 0 || !audioOn.load(std::memory_order_relaxed))
        return -1;
    for (int k = 0; k < audioPorts.sourceCount; k++)
        if (inputs[size_t(audioPorts.sourceFirst + k)].channels > 0)
            return sourceSlot.load(std::memory_order_relaxed);
    return -1;
}

BoundKind OutputModuleBase::boundKind() const {
    TesseraModule* src = source();
    if (!src || status() != BindingStatus::Bound)
        return BoundKind::None;
    if (acceptsMaster && bindingData.track == src->song().master.id)
        return BoundKind::Master;
    const Track* t = src->song().findTrack(bindingData.track);
    if (!t)
        return BoundKind::None;
    return t->type == TrackType::Audio  ? BoundKind::Audio
           : t->type == TrackType::Drum ? BoundKind::Drum
                                        : BoundKind::Instrument;
}

int OutputModuleBase::activeReturn() const {
    if (audioPorts.inL < 0 || !audioOn.load(std::memory_order_relaxed))
        return -1;
    // Rack's isConnected() isn't const; it only reads the channel count.
    bool patched = inputs[size_t(audioPorts.inL)].channels > 0 || inputs[size_t(audioPorts.inR)].channels > 0;
    return patched ? returnSlot.load(std::memory_order_relaxed) : -1;
}

void OutputModuleBase::onAdd(const AddEvent& e) {
    Registry& registry = Registry::get();
    registry.addOutput(this);
    if (!needsDefaultBinding) {
        checkPaste = true;
        addedFrameTime = Registry::frameTime();
        return;
    }
    needsDefaultBinding = false;

    // A fresh module follows the Tessera used last, on its first track that has no output module yet.
    TesseraModule* src = registry.mostRecentSource();
    if (!src)
        return;
    std::vector<TrackId> tracks = candidateTracks(*src);
    if (tracks.empty())
        return;
    if (acceptsMaster) {
        // Track Out (§8.1): the selected track if nothing plays it yet, else the first note track without one.
        // Never an audio track or the master unless it is the selected one.
        TrackId selected = src->editor().selectedTrack;
        if (src->song().findTrack(selected) && !registry.hasOutputFor(src->id, selected)) {
            bind(*src, selected);
            return;
        }
        for (const Track& t : src->song().tracks)
            if (t.type != TrackType::Audio && !registry.hasOutputFor(src->id, t.id)) {
                bind(*src, t.id);
                return;
            }
        for (const Track& t : src->song().tracks)
            if (t.type != TrackType::Audio) {
                bind(*src, t.id);
                return;
            }
    }
    auto free =
        std::find_if(tracks.begin(), tracks.end(), [&](TrackId t) { return !registry.hasOutputFor(src->id, t); });
    bind(*src, free != tracks.end() ? *free : tracks.front());
}

void OutputModuleBase::onRemove(const RemoveEvent& e) {
    Registry::get().removeOutput(this);
    releaseReturn();
}

void OutputModuleBase::refreshBinding() {
    bus.collect();

    // By the first refresh every module of a paste has been added, whatever order Rack added them in.
    if (checkPaste) {
        checkPaste = false;
        if (std::optional<int64_t> copy = Registry::get().findPaste(bindingData.sourceId, addedFrameTime))
            bindingData.sourceId = *copy;
    }

    TesseraModule* src = source();
    if (!src && !bindingData.sourceName.empty()) {
        src = Registry::get().findSourceByName(bindingData.sourceName);
        if (src)
            bindingData.sourceId = src->id;
    }
    if (src)
        bindingData.sourceName = src->instanceName();
    if (src && bindingData.followSelected) {
        // A selected track this module can't play (an audio track for Drum Out) leaves the last one playing.
        const Track* selected = src->song().findTrack(src->editor().selectedTrack);
        if (selected && accepts(selected->type))
            bindingData.track = selected->id;
    }

    busSlot.store(src ? src->busSlotOf(bindingData.track) : -1);
    boundTrackId.store(bindingData.track.value);
    bus.set(src ? src->outputBus() : nullptr);

    // Audio: OUT reads the track's (or the master's) slot; IN writes a return slot of this source's own bus. In
    // "follow the selected track" mode both do nothing (§7.8).
    bool master = src && acceptsMaster && bindingData.track == src->song().master.id;
    audioSlot.store(master ? OutputBus::kMasterSlot : busSlot.load());
    audioOn.store(src && !bindingData.followSelected && status() == BindingStatus::Bound);
    trackGone.store(src && status() == BindingStatus::TrackMissing);
    float levelDb = heldLevelDb ? *heldLevelDb : setting.db;
    inputTarget.store(setting.use && !countedOnce ? dbToGain(levelDb) : 0.f, std::memory_order_relaxed);
    // The sources' levels and pans: the drum track's lane IN settings, or what a drag in the mixer holds.
    if (audioPorts.sourceCount > 0) {
        const Track* t = src ? src->song().findTrack(bindingData.track) : nullptr;
        for (int k = 0; k < audioPorts.sourceCount && k < kDrumInputs; k++) {
            Channel c = t ? t->drumInputs[size_t(k)] : Channel{};
            if (std::optional<Channel> held = src ? src->drumInputHold(bindingData.track, k) : std::nullopt)
                c = *held;
            float l, r;
            balanceGains(c.pan, l, r);
            float g = dbToGain(c.volumeDb);
            sourceTargetL[size_t(k)].store(l * g, std::memory_order_relaxed);
            sourceTargetR[size_t(k)].store(r * g, std::memory_order_relaxed);
        }
    }
    if (audioPorts.inL >= 0 || audioPorts.sourceCount > 0) {
        std::shared_ptr<OutputBus> owner = src ? src->outputBus() : nullptr;
        if (owner != returnOwner) {
            releaseReturn();
            if (owner) {
                returnOwner = owner;
                if (audioPorts.inL >= 0)
                    returnSlot.store(owner->returns().claim());
                if (audioPorts.sourceCount > 0)
                    sourceSlot.store(owner->returns().claim());
            }
        }
    }
}

void OutputModuleBase::bind(const TesseraModule& src, TrackId track) {
    bindingData.sourceId = src.id;
    bindingData.sourceName = src.instanceName();
    bindingData.track = track;
    refreshBinding();
}

void OutputModuleBase::stepTrack(int delta) {
    bindingData.followSelected = false;
    TesseraModule* src = source();
    if (!src)
        return;
    std::vector<TrackId> tracks = candidateTracks(*src);
    if (tracks.empty())
        return;
    int n = int(tracks.size());
    auto it = std::find(tracks.begin(), tracks.end(), bindingData.track);
    int index = it == tracks.end() ? (delta > 0 ? 0 : n - 1) : (int(it - tracks.begin()) + delta % n + n) % n;
    bind(*src, tracks[index]);
}

void OutputModuleBase::setSourceSetting(SourceLevel level) {
    level.db = std::clamp(level.db, kSilentDb, kMaxFaderDb);
    setting = level;
    refreshBinding();
}

float OutputModuleBase::flashAmount() const {
    double left = flashUntil - rack::system::getTime();
    if (left <= 0)
        return 0;
    // Three pulses that never fade below 60%, then a quick fade out over the last 0.3 s.
    double t = 1.2 - left;
    return float((0.8 + 0.2 * std::cos(t * 2 * M_PI / 0.4)) * std::min(1.0, left / 0.3));
}

TesseraModule* OutputModuleBase::source() const { return Registry::get().findSource(bindingData.sourceId); }

BindingStatus OutputModuleBase::status() const {
    TesseraModule* src = source();
    if (!src)
        return BindingStatus::NoSource;
    if (acceptsMaster && bindingData.track == src->song().master.id)
        return BindingStatus::Bound;
    if (!src->song().findTrack(bindingData.track) || src->busSlotOf(bindingData.track) < 0)
        return BindingStatus::TrackMissing;
    return BindingStatus::Bound;
}

std::vector<TrackId> OutputModuleBase::candidateTracks(const TesseraModule& src) const {
    std::vector<TrackId> out;
    for (const Track& t : src.song().tracks)
        if (accepts(t.type))
            out.push_back(t.id);
    if (acceptsMaster)
        out.push_back(src.song().master.id);
    return out;
}

json_t* OutputModuleBase::dataToJson() {
    json_t* root = json_object();
    json_object_set_new(root, "source", json_integer(bindingData.sourceId));
    json_object_set_new(root, "sourceName", json_string(bindingData.sourceName.c_str()));
    json_object_set_new(root, "track", json_integer(static_cast<json_int_t>(bindingData.track.value)));
    json_object_set_new(root, "followSelected", json_boolean(bindingData.followSelected));
    if (part() > 0)
        json_object_set_new(root, "channel", json_integer(part()));
    if (setting.db != 0)
        json_object_set_new(root, "level", json_real(setting.db));
    if (!setting.use)
        json_object_set_new(root, "use", json_false());
    return root;
}

void OutputModuleBase::dataFromJson(json_t* root) {
    needsDefaultBinding = false;
    if (json_t* j = json_object_get(root, "source"))
        bindingData.sourceId = json_integer_value(j);
    if (const char* s = json_string_value(json_object_get(root, "sourceName")))
        bindingData.sourceName = s;
    if (json_t* j = json_object_get(root, "track"))
        bindingData.track = TrackId(uint64_t(json_integer_value(j)));
    bindingData.followSelected = json_is_true(json_object_get(root, "followSelected"));
    setPart(int(json_integer_value(json_object_get(root, "channel"))));
    if (json_t* j = json_object_get(root, "level"); json_is_number(j))
        setting.db = std::clamp(float(json_number_value(j)), kSilentDb, kMaxFaderDb);
    setting.use = !json_is_false(json_object_get(root, "use"));
}

} // namespace tessera
