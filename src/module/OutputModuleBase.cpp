#include "OutputModuleBase.hpp"

#include "Registry.hpp"
#include "TesseraModule.hpp"

#include <algorithm>

namespace tessera {

namespace {
// Reporting is a full memory fence; every 32nd sample is enough, it only delays freeing a replaced bus a little.
constexpr int64_t kReportEvery = 32;
} // namespace

OutputModuleBase::~OutputModuleBase() { Registry::get().removeOutput(this); }

void OutputModuleBase::process(const ProcessArgs& args) {
    const OutputBus* b = bus.get();
    TrackId track(boundTrack.load(std::memory_order_relaxed));
    const TrackFrame* frame = b ? b->read(args.frame, busSlot.load(std::memory_order_relaxed), track) : nullptr;
    if (frame)
        writeFrame(*frame);
    else
        silenceGates();
    if (args.frame % kReportEvery == 0)
        bus.markProcessed(args.frame);
}

void OutputModuleBase::processBypass(const ProcessArgs& args) {
    silenceGates();
    if (args.frame % kReportEvery == 0)
        bus.markProcessed(args.frame);
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
    auto free =
        std::find_if(tracks.begin(), tracks.end(), [&](TrackId t) { return !registry.hasOutputFor(src->id, t); });
    bind(*src, free != tracks.end() ? *free : tracks.front());
}

void OutputModuleBase::onRemove(const RemoveEvent& e) { Registry::get().removeOutput(this); }

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
        // A selected track this module can't play (a drum track for Track Out) leaves the last one playing.
        const Track* selected = src->song().findTrack(src->editor().selectedTrack);
        if (selected && accepts(selected->type))
            bindingData.track = selected->id;
    }

    busSlot.store(src ? src->busSlotOf(bindingData.track) : -1);
    boundTrack.store(bindingData.track.value);
    bus.set(src ? src->outputBus() : nullptr);
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

TesseraModule* OutputModuleBase::source() const { return Registry::get().findSource(bindingData.sourceId); }

BindingStatus OutputModuleBase::status() const {
    TesseraModule* src = source();
    if (!src)
        return BindingStatus::NoSource;
    if (!src->song().findTrack(bindingData.track) || src->busSlotOf(bindingData.track) < 0)
        return BindingStatus::TrackMissing;
    return BindingStatus::Bound;
}

std::vector<TrackId> OutputModuleBase::candidateTracks(const TesseraModule& src) const {
    std::vector<TrackId> out;
    for (const Track& t : src.song().tracks)
        if (accepts(t.type))
            out.push_back(t.id);
    return out;
}

json_t* OutputModuleBase::dataToJson() {
    json_t* root = json_object();
    json_object_set_new(root, "source", json_integer(bindingData.sourceId));
    json_object_set_new(root, "sourceName", json_string(bindingData.sourceName.c_str()));
    json_object_set_new(root, "track", json_integer(json_int_t(bindingData.track.value)));
    json_object_set_new(root, "followSelected", json_boolean(bindingData.followSelected));
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
}

} // namespace tessera
