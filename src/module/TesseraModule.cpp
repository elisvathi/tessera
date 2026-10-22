#include "TesseraModule.hpp"

#include "../engine/ClockDivisions.hpp"
#include "../model/DeviceCommands.hpp"
#include "../model/DeviceLookup.hpp"
#include "../model/Json.hpp"
#include "../model/Serialize.hpp"
#include "CommandAction.hpp"
#include "Registry.hpp"

#include <algorithm>
#include <set>

namespace tessera {

namespace {

constexpr float kTriggerSeconds = 1e-3f;
constexpr float kBeatFlashSeconds = 0.05f;
constexpr float kGateLow = 0.1f;
constexpr float kGateHigh = 1.f;
// Launcher state for the UI doesn't need every sample.
constexpr uint32_t kLauncherStatusDivision = 32;
constexpr uint32_t kMapDivision = 16; // mapped knobs follow automation at a 16th of the sample rate
// Rack knob mappings of modulators are told apart from lanes' by this bit (IDs never reach it).
constexpr uint64_t kModulatorKey = uint64_t(1) << 62;
constexpr uint32_t kSongModulatorColor = 0xFF8A3D; // the accent: song modulators belong to no track

} // namespace

TesseraModule::TesseraModule() : clockDivisionIndex(kDefaultClockDivision) {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

    configButton(PLAY_PARAM, "Play / stop");
    configButton(RESET_PARAM, "Return to start");
    configSwitch(LOOP_PARAM, 0.f, 1.f, 0.f, "Loop", {"Off", "On"});
    configButton(TAP_PARAM, "Tap tempo");
    configParam(BPM_PARAM, float(Engine::kMinBpm), float(Engine::kMaxBpm), 120.f, "Tempo", " BPM");
    paramQuantities[BPM_PARAM]->displayPrecision = 4;
    configButton(SCENE_PREV_PARAM, "Previous scene");
    configButton(SCENE_NEXT_PARAM, "Next scene");
    configButton(SCENE_LAUNCH_PARAM, "Launch the selected scene");
    configButton(STOP_ALL_PARAM, "Stop all clips");
    configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Plays", {"Clips", "Timeline"})->description =
        "Clips: tracks play the clips you launch. Timeline: tracks play the timeline until a launched clip takes "
        "them over";

    configInput(RESET_INPUT, "Reset")->description = "Trigger: return to the start (or loop start)";
    configInput(RUN_INPUT, "Run")->description = "Starts and stops the transport. Toggle or gate mode in the menu";
    configInput(SCENE_INPUT, "Scene select")->description = "Picks the scene LAUNCH starts: 0.1 V per scene, or "
                                                            "1 semitone per scene (set in the menu)";
    configInput(LAUNCH_INPUT, "Scene launch")->description = "Trigger: launches the selected scene";

    configOutput(CLOCK_OUTPUT, "Clock")->description = "Division set in the menu";
    configOutput(RESET_OUTPUT, "Reset")->description = "Trigger when the transport resets";
    configOutput(RUN_OUTPUT, "Run")->description = "High while playing";
    configOutput(EOC_OUTPUT, "Loop end")->description = "Trigger each time the loop wraps";
    configOutput(BAR_OUTPUT, "Bar")->description = "Trigger on every bar";
    configOutput(CLICK_OUTPUT, "Click")->description = "Trigger on every beat: 10 V on the downbeat, 5 V otherwise";

    configLight(PLAY_LIGHT, "Playing");
    configLight(BEAT_LIGHT, "Beat");
    configLight(LAUNCH_LIGHT, "Scene launch queued");
    configLight(MODE_LIGHT, "Timeline plays");
    configButton(REC_PARAM, "Record")->description =
        "Stopped: count in (menu) and record. Playing: punch in. Recording: punch out";
    configInput(REC_VOCT_INPUT, "Record V/OCT")->description = "Pitch of the notes to record, per poly channel";
    configInput(REC_GATE_INPUT, "Record gate")->description = "A note per poly channel while high (above 1 V)";
    configInput(REC_VEL_INPUT, "Record velocity")->description = "0-10 V per poly channel; 8 V without a cable";
    configInput(AUTO_REC_INPUT, "Automation record")->description =
        "Poly: channel N is recorded into armed lanes on AUTO channel N whose source is this input";
    configLight(REC_LIGHT, "Recording");
    configButton(FILL_PARAM, "Fill (hold)")->description = "Notes set to play on Fill (or Not fill) follow it";
    configInput(FILL_INPUT, "Fill")->description = "Gate: on while high, together with the FILL button";
    configInput(CLOCK_INPUT, "Clock")->description =
        "External clock (24 PPQN by default, see the menu): while patched, the tempo and the song position follow it";
    configLight(FILL_LIGHT, "Fill");
    launcherStatusDivider.setDivision(kLauncherStatusDivision);
    mapDivider.setDivision(kMapDivision);
    engine.setRecordQueue(&recordEvents);

    songData = makeDefaultSong();
    publish(ChangeSet::all());
    launchFirstClips();
}

void TesseraModule::launchFirstClips() {
    launch({LaunchEvent::Kind::Reset, 0, 0, true, false});
    // In Arranger mode tracks start on the arrangement instead.
    if (playMode() == PlayMode::Arranger)
        return;
    for (const Track& t : songData.tracks)
        for (size_t i = 0; i < t.slots.size(); i++)
            if (t.slots[i].clip) {
                launch({LaunchEvent::Kind::Slot, t.id.value, int(i), true, false});
                break;
            }
}

void TesseraModule::restoreLauncher(const json_t* launcherJ) {
    launch({LaunchEvent::Kind::Reset, 0, 0, true, false});
    size_t i;
    const json_t* entry;
    json_array_foreach(launcherJ, i, entry) {
        uint64_t track = uint64_t(json_integer_value(json_array_get(entry, 0)));
        int slot = int(json_integer_value(json_array_get(entry, 1)));
        launch({LaunchEvent::Kind::Slot, track, slot, true, false});
    }
}

LauncherStatus TesseraModule::launcherStatus(TrackId track) const {
    LauncherStatus status;
    int slot = slots.slotOf(track);
    if (slot < 0)
        return status;
    const LauncherSlot& s = launcherSlots[size_t(slot)];
    if (s.track.load(std::memory_order_relaxed) != track.value)
        return status;
    status.playing = s.playing.load(std::memory_order_relaxed);
    status.queued = s.queued.load(std::memory_order_relaxed);
    status.position = s.position.load(std::memory_order_relaxed);
    status.overridden = s.overridden.load(std::memory_order_relaxed);
    status.elapsed = s.elapsed.load(std::memory_order_relaxed);
    return status;
}

bool TesseraModule::anyTrackOverridden() const {
    for (const Track& t : songData.tracks)
        if (launcherStatus(t.id).overridden)
            return true;
    return false;
}

void TesseraModule::setPlayMode(PlayMode mode) {
    float value = mode == PlayMode::Arranger ? 1.f : 0.f;
    setParamWithUndo(MODE_PARAM, value, mode == PlayMode::Arranger ? "Play the timeline" : "Play the clips");
}

void TesseraModule::setRecordArrangement(bool on) {
    if (on == captureArmed.load())
        return;
    captureArmed.store(on);
    captureSawPlaying = false;
    if (!on)
        commitCapture();
}

void TesseraModule::setRecordSettings(const RecordSettings& settings) {
    recorder.settings = settings;
    countInBars.store(settings.countInBars);
    liveToKey.store(settings.toKey);
    newControlLanes.store(settings.newControlLanes);
    mpeInput.store(settings.mpe);
}

void TesseraModule::Host::launchAt(TrackId track, int slot, double playTick) {
    LaunchEvent e{LaunchEvent::Kind::Slot, track.value, slot, true, false};
    e.at = playTick;
    m.launch(e);
}

void TesseraModule::readRecordInputs(const ProcessArgs& args) {
    // CV: a rising gate starts a note at the V/OCT of that moment; the note keeps that pitch until it ends.
    int channels = inputs[REC_GATE_INPUT].getChannels();
    // Nothing patched and no CV note held from before: no note can start or end.
    bool anyHigh = false;
    for (int c = 0; c < kMaxVoices && (channels > 0 || recGateHeld); c++) {
        bool wasHigh = recGates[size_t(c)].isHigh();
        float gate = c < channels ? inputs[REC_GATE_INPUT].getVoltage(c) : 0.f;
        recGates[size_t(c)].process(gate, kGateLow, kGateHigh);
        bool high = recGates[size_t(c)].isHigh();
        anyHigh |= high;
        if (high == wasHigh)
            continue;
        LiveNote n;
        n.source = LiveSource::Cv;
        n.channel = uint8_t(c);
        n.on = high;
        if (high) {
            float v = inputs[REC_VOCT_INPUT].getPolyVoltage(c);
            recPitches[size_t(c)] = int8_t(std::clamp(int(std::lround(v * 12.f)) + 60, 0, 127));
            n.velocity = inputs[REC_VEL_INPUT].isConnected()
                             ? std::clamp(inputs[REC_VEL_INPUT].getPolyVoltage(c) / 10.f, 0.f, 1.f)
                             : 0.8f;
        } else {
            n.velocity = 0.5f;
        }
        n.pitch = recPitches[size_t(c)];
        engine.liveNote(n);
    }
    recGateHeld = anyHigh;

    bool mpe = mpeInput.load(std::memory_order_relaxed);
    if (mpe != midiReaderMpe) {
        midiReader.setMpe(mpe);
        midiReaderMpe = mpe;
    }
    rack::midi::Message& msg = midiMessage;
    std::array<MidiEvent, MidiReader::kMaxEvents> events;
    while (midiIn.tryPop(&msg, args.frame)) {
        int n = midiReader.read(msg.bytes.data(), int(msg.bytes.size()), events);
        for (int i = 0; i < n; i++)
            playMidiEvent(events[size_t(i)]);
    }
}

void TesseraModule::playMidiEvent(const MidiEvent& e) {
    switch (e.kind) {
    case MidiEvent::Kind::NoteOn:
    case MidiEvent::Kind::NoteOff: {
        LiveNote n;
        n.source = LiveSource::Midi;
        n.channel = e.channel;
        n.partChannel = int8_t(e.part);
        n.pitch = e.pitch;
        n.velocity = e.value;
        n.on = e.kind == MidiEvent::Kind::NoteOn;
        engine.liveNote(n);
        return;
    }
    case MidiEvent::Kind::NoteValue:
    case MidiEvent::Kind::ChannelValue: {
        LiveValue v;
        v.source = LiveSource::Midi;
        v.channel = e.channel;
        v.partChannel = int8_t(e.part);
        v.perNote = e.kind == MidiEvent::Kind::NoteValue;
        v.pitch = e.pitch;
        v.curve = e.curve;
        v.value = e.value;
        switch (e.control) {
        case ChannelControl::PitchBend:
            v.control = {MidiControl::Kind::PitchBend, 0};
            break;
        case ChannelControl::Pressure:
            v.control = {MidiControl::Kind::Pressure, 0};
            break;
        case ChannelControl::Cc:
            v.control = {MidiControl::Kind::Cc, e.cc};
            break;
        }
        if (!v.perNote) {
            lastMidiControl.store(uint32_t(v.control.kind) << 8 | v.control.cc, std::memory_order_relaxed);
            midiMoves.fetch_add(1, std::memory_order_release);
        }
        engine.liveValue(v);
        return;
    }
    }
}

void TesseraModule::uiStep() {
    snapshots.collect();
    syncMappings();
    if (learning()) {
        learnMovedControl();
        if (learning())
            learnTouchedParam();
    }
    recordAutomation();
    RecordEvent recordEvent;
    while (recordEvents.pop(recordEvent)) {
        // Every live note also comes once without a track (the capture copy): that one is what step input hears.
        constexpr size_t kMaxLiveInput = 256;
        if (recordEvent.kind == RecordEvent::Kind::Note && recordEvent.track == 0 && liveInput.size() < kMaxLiveInput)
            liveInput.push_back(recordEvent);
        recorder.handle(recordEvent);
    }
    recorder.update(playTicks());
    if (!captureArmed.load() && captureBuilder.empty())
        return;
    drainCapture();
    if (captureArmed.load()) {
        // Recording ends with the transport.
        if (playing())
            captureSawPlaying = true;
        else if (captureSawPlaying)
            setRecordArrangement(false);
    }
}

void TesseraModule::drainCapture() {
    CaptureEvent e;
    while (captureEvents.pop(e))
        captureBuilder.add(e);
}

void TesseraModule::commitCapture() {
    drainCapture();
    captureBuilder.closeAll(playheadTicks());
    std::vector<CapturedSegment> segments = captureBuilder.take();
    perform(recordArrangement(songData, segments, [this] { return songData.newId<ArrangementId>(); }));
}

TesseraModule::~TesseraModule() {
    Registry::get().removeSource(this);
    for (OwnedHandle& h : handles)
        APP->engine->removeParamHandle(h.handle.get());
}

// ---- Parameter mapping ----------------------------------------------------------------------------------------

void TesseraModule::startLearn(ModulatorId modulator) {
    learnLane = AutomationId();
    learnModulator = modulator;
    if (APP->scene && APP->scene->rack)
        APP->scene->rack->touchedParam = nullptr;
}

void TesseraModule::startLearn(LaneOwner owner, AutomationId lane) {
    learnModulator = ModulatorId();
    learnOwner = owner;
    learnLane = lane;
    learnMidiMoves = midiMoves.load(std::memory_order_relaxed);
    // Only a knob touched from now on counts.
    if (APP->scene && APP->scene->rack)
        APP->scene->rack->touchedParam = nullptr;
}

std::string TesseraModule::targetName(const ParamTarget& t) {
    rack::engine::Module* m = APP->engine->getModule(t.moduleId);
    if (!m || t.paramId < 0 || t.paramId >= int(m->paramQuantities.size()))
        return "(missing module)";
    std::string module = m->model ? m->model->name : "Module";
    return module + " · " + m->paramQuantities[size_t(t.paramId)]->getLabel();
}

bool TesseraModule::targetTakenOver(AutomationId lane, int index) const {
    uint64_t key = lane.value * 4 + uint64_t(index);
    for (const OwnedHandle& h : handles)
        if (h.key == key)
            return h.handle->moduleId < 0;
    return false;
}

void TesseraModule::learnTouchedParam() {
    rack::app::ParamWidget* touched = APP->scene->rack->getTouchedParam();
    rack::engine::ParamQuantity* pq = touched ? touched->getParamQuantity() : nullptr;
    if (!pq || !pq->module)
        return;
    APP->scene->rack->touchedParam = nullptr;
    CommandPtr command;
    ParamTarget target;
    target.moduleId = pq->module->id;
    target.paramId = pq->paramId;
    if (learnModulator.valid()) {
        ModulatorId modulator = learnModulator;
        learnModulator = ModulatorId();
        if (pq->module == this)
            return;
        Modulation route;
        route.kind = Modulation::Kind::RackKnob;
        route.knob = target;
        route.amount = 1;
        command = addRoute(songData, modulator, route);
    } else {
        command = learnLaneCommand(target);
    }
    if (!command)
        return;
    // Rack allows one mapping per knob: say so before taking it from another one (DESIGN.md §6).
    rack::engine::ParamHandle* other = APP->engine->getParamHandle(target.moduleId, target.paramId);
    bool ours = false;
    for (const OwnedHandle& h : handles)
        ours |= h.handle.get() == other;
    if (!other || ours) {
        perform(std::move(command));
        return;
    }
    auto shared = std::make_shared<CommandPtr>(std::move(command));
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("This knob is already mapped" +
                                         (other->text.empty() ? std::string() : " (" + other->text + ")")));
    menu->addChild(rack::createMenuItem("Take it over", "", [this, shared] { perform(std::move(*shared)); }));
    menu->addChild(rack::createMenuItem("Cancel", "", [] {}));
}

CommandPtr TesseraModule::learnLaneCommand(const ParamTarget& target) {
    LaneOwner owner = learnOwner;
    AutomationId laneId = learnLane;
    learnLane = AutomationId();
    const AutomationEnvelope* lane = findLane(songData, owner, laneId);
    // Knobs are mapped on track lanes only; a clip lane plays its track lane's knobs.
    if (!lane || !owner.track.valid() || target.moduleId == id || lane->targets.size() >= 4)
        return nullptr;
    for (const ParamTarget& t : lane->targets)
        if (t.moduleId == target.moduleId && t.paramId == target.paramId)
            return nullptr;
    AutomationEnvelope next = *lane;
    // A lane still named by its channel takes its first knob's name.
    if (next.targets.empty() && next.name == "Auto " + std::to_string(next.outputIndex + 1))
        next.name = targetName(target);
    next.targets.push_back(target);
    // Touching a knob makes it the source: it is what the lane records from now.
    next.midi = MidiControl();
    next.source = AutoSource::Knob;
    return replaceLane(songData, owner, next, "Map " + targetName(target));
}

void TesseraModule::learnMovedControl() {
    if (!learnLane.valid() || midiMoves.load(std::memory_order_acquire) == learnMidiMoves)
        return;
    uint32_t key = lastMidiControl.load(std::memory_order_relaxed);
    MidiControl control{MidiControl::Kind(key >> 8), uint8_t(key & 127)};
    const AutomationEnvelope* lane = findLane(songData, learnOwner, learnLane);
    LaneOwner owner = learnOwner;
    learnLane = AutomationId();
    if (!lane || !control.valid())
        return;
    AutomationEnvelope next = *lane;
    next.midi = control;
    perform(replaceLane(songData, owner, next, "Record " + midiControlName(control)));
}

bool TesseraModule::readAutomationSource(const AutomationEnvelope& lane, float& value) const {
    if (lane.paramTarget.valid()) {
        // A device knob: where the mouse holds it; let go, it stays where the hand left it. Write mode also
        // records a knob nobody has touched yet, at the value it plays.
        if (holds(lane.paramTarget)) {
            value = heldKnobValue.load(std::memory_order_relaxed);
            return true;
        }
        auto it = laneValues.find(lane.id.value);
        if (it != laneValues.end()) {
            value = it->second;
            return true;
        }
        if (lane.recordMode != AutoRecordMode::Write)
            return false;
        value = liveKnob(lane.paramTarget);
        return true;
    }
    if (lane.source == AutoSource::Cv) {
        int channel = lane.recInput >= 0 ? lane.recInput : lane.outputIndex;
        if (channel >= autoRecChannels.load(std::memory_order_relaxed))
            return false;
        value = std::clamp(fromVolts(lane, autoRecVolts[size_t(channel)].load()), 0.f, 1.f);
        return true;
    }
    if (lane.targets.empty())
        return false;
    const ParamTarget& t = lane.targets.front();
    rack::engine::Module* m = APP->engine->getModule(t.moduleId);
    if (!m || t.paramId < 0 || t.paramId >= int(m->paramQuantities.size()) || t.max == t.min)
        return false;
    // Back through the target's own range, so playback turns the knob to where it was recorded.
    float scaled = m->paramQuantities[size_t(t.paramId)]->getScaledValue();
    value = std::clamp((scaled - t.min) / (t.max - t.min), 0.f, 1.f);
    return true;
}

std::vector<const AutomationEnvelope*> TesseraModule::recordableLanes(const Track& t) const {
    std::vector<const AutomationEnvelope*> out;
    if (!t.armed)
        return out;
    // MIDI lanes record what the engine hears; these are read here, each UI frame.
    auto recordable = [](const AutomationEnvelope& l) { return !l.noRecord && !l.midi.valid(); };
    for (const AutomationEnvelope& l : t.automation)
        if (recordable(l))
            out.push_back(&l);
    // The playing clip's device knob lanes record too (they turn knobs only that clip turns).
    int playing = launcherStatus(t.id).playing;
    if (playing >= 0 && playing < int(t.slots.size()) && t.slots[size_t(playing)].clip)
        if (const Clip* c = songData.findClip(*t.slots[size_t(playing)].clip))
            for (const AutomationEnvelope& l : c->automation)
                if (recordable(l) && l.paramTarget.valid())
                    out.push_back(&l);
    return out;
}

void TesseraModule::recordAutomation() {
    bool rec = recording() && playing();
    if (!rec) {
        if (automationRecording) {
            // A knob recorded in Latch or Write mode stayed held after the mouse let go; the pass is over.
            for (const ParamRef& ref : recordedKnobs)
                if (holds(ref))
                    releaseKnob();
            laneValues.clear();
            recordedKnobs.clear();
            automationRecording = false;
        }
    } else {
        automationRecording = true;
        double seconds = rack::system::getTime();
        for (const Track& t : songData.tracks) {
            for (const AutomationEnvelope* lane : recordableLanes(t)) {
                float value;
                if (!readAutomationSource(*lane, value))
                    continue;
                laneValues[lane->id.value] = value;
                if (lane->paramTarget.valid() &&
                    std::find(recordedKnobs.begin(), recordedKnobs.end(), lane->paramTarget) == recordedKnobs.end())
                    recordedKnobs.push_back(lane->paramTarget);
                RecordEvent e;
                e.kind = RecordEvent::Kind::LaneValue;
                e.track = t.id.value;
                e.lane = lane->id.value;
                e.value = value;
                e.seconds = seconds;
                e.recording = e.playing = true;
                e.playTick = playTicks();
                e.songTick = playheadTicks();
                LauncherStatus status = launcherStatus(t.id);
                if (status.playing >= 0 && !std::isnan(status.elapsed)) {
                    e.slot = status.playing;
                    e.clipStart = e.playTick - status.elapsed;
                    e.clipTick = status.position;
                }
                recorder.handle(e);
            }
        }
    }
    int n = mapCount.load(std::memory_order_acquire);
    for (int i = 0; i < n; i++) {
        AutomationId lane(maps[size_t(i)].lane);
        // A mapped knob turned by hand while its lane records follows the hand, not the lane.
        maps[size_t(i)].paused.store(rec && recorder.laneWriting(lane), std::memory_order_relaxed);
    }
}

void TesseraModule::syncMappings() {
    retiredHandles.clear(); // process() stopped using these at least a frame ago
    if (mappedRevision == revision)
        return;
    mappedRevision = revision;

    struct Want {
        uint64_t key;
        ParamTarget target;
        int busSlot, output;
        bool active;
        uint32_t color;
        std::string text;
        int modSlot = -1;
        float amount = 0;
        bool bipolar = false;
    };
    std::vector<Want> wanted;
    for (const Track& t : songData.tracks) {
        for (const AutomationEnvelope& lane : t.automation) {
            // Without points anywhere on its channel, a fresh mapping mustn't yank the knob to the lane's rest.
            bool active = !lane.points.empty();
            auto clipHasPoints = [&](ClipId id) {
                if (const Clip* c = songData.findClip(id))
                    for (const AutomationEnvelope& cl : c->automation)
                        active |= drivesOutput(cl) && cl.outputIndex == lane.outputIndex && !cl.points.empty();
            };
            for (const ClipSlot& slot : t.slots)
                if (slot.clip)
                    clipHasPoints(*slot.clip);
            for (const ArrangerClip& a : t.arrangement)
                clipHasPoints(a.clip);
            for (size_t k = 0; k < lane.targets.size() && k < 4; k++)
                wanted.push_back({lane.id.value * 4 + k, lane.targets[k], slots.slotOf(t.id), lane.outputIndex,
                                  active && !lane.mute, t.color, t.name + " · " + lane.name});
        }
    }

    // Modulators turning Rack knobs (docs/DEVICES.md §3 "CV out").
    forEachModulator(songData, [&](const Modulator& m, const ModulatorPlace& place) {
        int slot = snapshotBuilder.modulatorSlots().slotOf(m.id);
        const Track* track = place.level == ModLevel::Song ? nullptr : songData.findTrack(place.track);
        for (size_t k = 0; k < m.routes.size(); k++) {
            const Modulation& r = m.routes[k];
            if (r.kind != Modulation::Kind::RackKnob || slot < 0)
                continue;
            Want w{kModulatorKey | (m.id.value * 8 + k),
                   r.knob,
                   -1,
                   0,
                   true,
                   track ? track->color : kSongModulatorColor,
                   (track ? track->name : std::string("Song")) + " · " + m.name};
            w.modSlot = slot;
            w.amount = r.amount;
            w.bipolar = mods::bipolar(m.type, m.params.data());
            wanted.push_back(w);
        }
    });

    // Stop process() using the old table before handles change.
    mapCount.store(0, std::memory_order_release);
    std::vector<OwnedHandle> kept;
    for (OwnedHandle& h : handles) {
        bool still = std::any_of(wanted.begin(), wanted.end(), [&](const Want& w) { return w.key == h.key; });
        if (still) {
            kept.push_back(std::move(h));
        } else {
            APP->engine->removeParamHandle(h.handle.get());
            retiredHandles.push_back(std::move(h.handle));
        }
    }
    handles = std::move(kept);

    int n = 0;
    for (const Want& w : wanted) {
        OwnedHandle* owned = nullptr;
        for (OwnedHandle& h : handles)
            if (h.key == w.key)
                owned = &h;
        if (!owned) {
            handles.push_back({w.key, -2, -1, std::make_unique<rack::engine::ParamHandle>()});
            owned = &handles.back();
            APP->engine->addParamHandle(owned->handle.get());
        }
        owned->handle->color = nvgRGB((w.color >> 16) & 0xFF, (w.color >> 8) & 0xFF, w.color & 0xFF);
        owned->handle->text = w.text;
        // Only when the target itself changed, so a mapping taken over by someone else stays theirs.
        if (owned->moduleId != w.target.moduleId || owned->paramId != w.target.paramId) {
            owned->moduleId = w.target.moduleId;
            owned->paramId = w.target.paramId;
            APP->engine->updateParamHandle(owned->handle.get(), w.target.moduleId, w.target.paramId, true);
        }
        if (n >= kMaxMaps || !w.active)
            continue;
        MapSlot& slot = maps[size_t(n++)];
        slot.handle.store(owned->handle.get());
        slot.busSlot.store(w.busSlot);
        slot.output.store(w.output);
        slot.min.store(w.target.min);
        slot.max.store(w.target.max);
        slot.paused.store(false);
        slot.modSlot.store(w.modSlot);
        slot.amount.store(w.amount);
        slot.bipolar.store(w.bipolar);
        slot.lane = w.modSlot >= 0 ? 0 : w.key / 4;
    }
    mapCount.store(n, std::memory_order_release);
}

void TesseraModule::writeMappings() {
    int n = mapCount.load(std::memory_order_acquire);
    for (int i = 0; i < n; i++) {
        MapSlot& slot = maps[size_t(i)];
        if (slot.paused.load(std::memory_order_relaxed))
            continue;
        rack::engine::ParamHandle* h = slot.handle.load(std::memory_order_relaxed);
        rack::engine::Module* target = h ? h->module : nullptr;
        if (!target || h->paramId < 0 || h->paramId >= int(target->paramQuantities.size()))
            continue;
        int modSlot = slot.modSlot.load(std::memory_order_relaxed);
        float v;
        if (modSlot >= 0) {
            // Full amount sweeps the whole min..max range: from its bottom (or top, when negative), or from the
            // middle for a bipolar modulator.
            float out = engine.modulatorOutput(modSlot), amount = slot.amount.load(std::memory_order_relaxed);
            if (slot.bipolar.load(std::memory_order_relaxed))
                v = 0.5f + 0.5f * amount * out;
            else
                v = amount >= 0 ? amount * out : 1 + amount * out;
            v = std::clamp(v, 0.f, 1.f);
        } else {
            v = engine.automationValueOfSlot(slot.busSlot.load(std::memory_order_relaxed),
                                             slot.output.load(std::memory_order_relaxed));
        }
        if (v < 0)
            continue;
        float lo = slot.min.load(std::memory_order_relaxed), hi = slot.max.load(std::memory_order_relaxed);
        float value = lo + v * (hi - lo);
        // Only on change, so a knob the lane holds still can still be turned by hand.
        if (value == slot.written)
            continue;
        slot.written = value;
        target->paramQuantities[size_t(h->paramId)]->setScaledValue(value);
    }
}

void TesseraModule::holdKnob(const ParamRef& ref, float value) {
    heldKnobValue.store(value, std::memory_order_relaxed);
    uint64_t key = (ref.kind == ParamRef::Kind::Modulator ? uint64_t(1) << 63 : 0) |
                   (uint64_t(ref.param & 0x7FFF) << 48) | (ref.owner & 0xFFFFFFFFFFFFull);
    heldKnobKey.store(key, std::memory_order_release);
}

bool TesseraModule::holds(const ParamRef& ref) const {
    uint64_t held = heldKnobKey.load(std::memory_order_relaxed);
    return held != 0 && (held >> 63 == 1) == (ref.kind == ParamRef::Kind::Modulator) &&
           (held & 0xFFFFFFFFFFFFull) == ref.owner && int((held >> 48) & 0x7FFF) == ref.param;
}

const AutomationEnvelope* TesseraModule::recordingKnobLane(const ParamRef& ref) const {
    if (!recording() || !playing())
        return nullptr;
    for (const Track& t : songData.tracks)
        for (const AutomationEnvelope* lane : recordableLanes(t))
            if (lane->paramTarget == ref)
                return lane;
    return nullptr;
}

bool TesseraModule::automated(const ParamRef& ref) const {
    const Snapshot* snap = snapshots.latest();
    int k = snap ? snap->knobIndex(ref) : -1;
    return k >= 0 && std::binary_search(snap->laneKnobs.begin(), snap->laneKnobs.end(), k);
}

float TesseraModule::knobBase(const ParamRef& ref) const {
    const Snapshot* snap = snapshots.latest();
    int k = snap ? snap->knobIndex(ref) : -1;
    if (k < 0)
        return 0;
    if (playing() && std::binary_search(snap->laneKnobs.begin(), snap->laneKnobs.end(), k))
        return engine.liveBase(k);
    return snap->knobs[size_t(k)];
}

float TesseraModule::liveKnob(const ParamRef& ref) const {
    const Snapshot* snap = snapshots.latest();
    int k = snap ? snap->knobIndex(ref) : -1;
    if (k < 0)
        return 0;
    bool moving = holds(ref);
    for (const KnobModulation& km : snap->modulated)
        moving |= km.knob == k;
    moving |= playing() && std::binary_search(snap->laneKnobs.begin(), snap->laneKnobs.end(), k);
    return moving ? engine.liveKnob(k) : snap->knobs[size_t(k)];
}

float TesseraModule::modulatorOutput(ModulatorId id) const {
    return engine.modulatorOutput(snapshotBuilder.modulatorSlots().slotOf(id));
}

void TesseraModule::publish(const ChangeSet& changes) {
    revision++;
    slots.sync(songData);
    snapshots.publish(snapshotBuilder.build(songData, slots, changes));
}

void TesseraModule::perform(CommandPtr command, std::optional<NoteSelection> selectionAfter) {
    if (!command)
        return;
    NoteSelection before = editorState.selection;
    command->apply(songData);
    if (selectionAfter)
        editorState.selection = std::move(*selectionAfter);
    publish(command->changes());
    APP->history->push(new CommandAction(id, std::move(command), std::move(before), editorState.selection));
}

void TesseraModule::revertCommand(Command& command) {
    command.revert(songData);
    publish(command.changes());
}

void TesseraModule::reapplyCommand(Command& command) {
    command.apply(songData);
    publish(command.changes());
}

void TesseraModule::setClockDivision(int index) {
    clockDivisionIndex.store(rack::math::clamp(index, 0, kClockDivisionCount - 1));
}

void TesseraModule::process(const ProcessArgs& args) {
    if (args.sampleRate != engineSampleRate) {
        engineSampleRate = args.sampleRate;
        engine.setSampleRate(engineSampleRate);
    }
    if (const Snapshot* next = snapshots.poll())
        engine.setSnapshot(next);
    if (uint64_t held = heldKnobKey.load(std::memory_order_acquire)) {
        ParamRef ref{held >> 63 ? ParamRef::Kind::Modulator : ParamRef::Kind::Device, held & 0xFFFFFFFFFFFFull,
                     uint16_t((held >> 48) & 0x7FFF)};
        engine.setKnobOverride(ref, heldKnobValue.load(std::memory_order_relaxed));
    } else {
        engine.clearKnobOverride();
    }
    // Before launches, so launches restored with a patch apply to the mode it was saved in.
    engine.setPlayMode(params[MODE_PARAM].getValue() > 0.5f ? PlayMode::Arranger : PlayMode::Launcher);
    engine.setCapture(&captureEvents, captureArmed.load(std::memory_order_relaxed));
    writeTransportOutputs(args);
    handleTransportControls(args);
    handleUiRequests();
    handleSceneControls();
    engine.setLiveToKey(liveToKey.load(std::memory_order_relaxed));
    engine.setNewControlLanes(newControlLanes.load(std::memory_order_relaxed));
    bool fill = params[FILL_PARAM].getValue() > 0.5f || inputs[FILL_INPUT].getVoltage() > kGateHigh ||
                editorFill.load(std::memory_order_relaxed);
    engine.setFill(fill);
    lights[FILL_LIGHT].setBrightness(fill);
    readRecordInputs(args);

    followClock(args);
    engine.setLoopEnabled(params[LOOP_PARAM].getValue() > 0.5f);
    engine.setClockDivision(kClockDivisions[clockDivisionIndex.load(std::memory_order_relaxed)].ticks);
    engine.process(bus->beginWrite(args.frame), lastEvents);
    bus->publish(args.frame);
    if (mapDivider.process()) {
        writeMappings();
        int channels = inputs[AUTO_REC_INPUT].getChannels();
        autoRecChannels.store(channels, std::memory_order_relaxed);
        for (int c = 0; c < channels && c < kMaxAutomationLanes; c++)
            autoRecVolts[size_t(c)].store(inputs[AUTO_REC_INPUT].getVoltage(c), std::memory_order_relaxed);
    }
    if (lastEvents.tempo > 0)
        params[BPM_PARAM].setValue(float(std::clamp(lastEvents.tempo, Engine::kMinBpm, Engine::kMaxBpm)));
    if (launcherStatusDivider.process())
        publishLauncherStatus();

    uiPlayhead.store(engine.position(), std::memory_order_relaxed);
    uiPlayTick.store(engine.playPosition(), std::memory_order_relaxed);
    uiPlaying.store(engine.isPlaying(), std::memory_order_relaxed);
    uiTempo.store(engine.tempo(), std::memory_order_relaxed);
    uiActiveTracks.store(engine.activeTracks(), std::memory_order_relaxed);
    uiRecording.store(engine.recording(), std::memory_order_relaxed);
    uiCountingIn.store(engine.countingIn(), std::memory_order_relaxed);

    lights[PLAY_LIGHT].setBrightness(engine.isPlaying());
    lights[LOOP_LIGHT].setBrightness(params[LOOP_PARAM].getValue());
    lights[MODE_LIGHT].setBrightness(params[MODE_PARAM].getValue());
    lights[BEAT_LIGHT].setBrightness(beatFlash.process(args.sampleTime));
    // Steady while recording, blinking through the count-in.
    recBlink = std::fmod(recBlink + args.sampleTime * 4.f, 1.f);
    lights[REC_LIGHT].setBrightness(engine.recording() ? 1.f : engine.countingIn() ? (recBlink < 0.5f) : 0.f);
}

void TesseraModule::handleTransportControls(const ProcessArgs& args) {
    auto toggle = [this] { engine.isPlaying() ? engine.stop() : engine.play(); };

    if (playButton.process(params[PLAY_PARAM].getValue() > 0.f))
        toggle();
    if (recButton.process(params[REC_PARAM].getValue() > 0.f))
        engine.toggleRecord(countInBars.load(std::memory_order_relaxed));

    bool resetNow = resetButton.process(params[RESET_PARAM].getValue() > 0.f);
    resetNow |= resetInput.process(inputs[RESET_INPUT].getVoltage(), kGateLow, kGateHigh);
    if (resetNow) {
        engine.reset();
        clockSync.resetPhase();
    }

    if (inputs[RUN_INPUT].isConnected()) {
        bool wasHigh = runInput.isHigh();
        bool rose = runInput.process(inputs[RUN_INPUT].getVoltage(), kGateLow, kGateHigh);
        if (runMode.load(std::memory_order_relaxed) == RunInputMode::Toggle) {
            if (rose)
                toggle();
        } else if (runInput.isHigh() != wasHigh) {
            // Gate mode reacts to edges only, so the play button still works while a cable is patched.
            runInput.isHigh() ? engine.play() : engine.stop();
        }
    }

    if (tapButton.process(params[TAP_PARAM].getValue() > 0.f)) {
        double bpm = tapTempo.tap(double(args.frame) / args.sampleRate);
        if (bpm > 0)
            params[BPM_PARAM].setValue(float(std::clamp(bpm, Engine::kMinBpm, Engine::kMaxBpm)));
    }
}

void TesseraModule::followClock(const ProcessArgs& args) {
    bool external = inputs[CLOCK_INPUT].isConnected();
    uiExternalClock.store(external, std::memory_order_relaxed);
    if (!external) {
        engine.setTempo(params[BPM_PARAM].getValue());
        clockWasPlaying = engine.isPlaying();
        return;
    }
    clockSync.setPpqn(clockInPpqn.load(std::memory_order_relaxed));
    bool pulse = clockInput.process(inputs[CLOCK_INPUT].getVoltage(), kGateLow, kGateHigh);
    // Starting the transport starts the song on the next pulse.
    bool playing = engine.isPlaying();
    if (playing && !clockWasPlaying)
        clockSync.resetPhase();
    clockWasPlaying = playing;
    double ticks = clockSync.step(pulse);
    double bpm = clockSync.bpm(args.sampleRate);
    if (bpm > 0)
        engine.setTempo(std::clamp(bpm, Engine::kMinBpm, Engine::kMaxBpm));
    engine.setStep(ticks);
}

void TesseraModule::handleUiRequests() {
    // A plain load first: an exchange on every sample costs a locked instruction even when nothing was asked.
    int requests = transportRequests.load(std::memory_order_relaxed)
                       ? transportRequests.exchange(0, std::memory_order_acquire)
                       : 0;
    if (requests & kRequestReset)
        engine.reset();
    if (requests & kRequestToggle)
        engine.isPlaying() ? engine.stop() : engine.play();
    if (requests & kRequestStop)
        engine.stop();
    if (requests & kRequestPlay)
        engine.play();
    if (requests & kRequestRecord)
        engine.toggleRecord(countInBars.load(std::memory_order_relaxed));
    if (seekPending.load(std::memory_order_relaxed) && seekPending.exchange(false, std::memory_order_acquire))
        engine.seek(seekTarget.load(std::memory_order_relaxed));
    AuditionEvent event;
    while (auditions.pop(event))
        engine.audition(event);
    LiveNote played;
    while (keyboardNotes.pop(played))
        engine.liveNote(played);
    // A launch can follow an edit that was published a moment ago (a freshly recorded clip): take that
    // snapshot first, so the launch finds its clip.
    if (const Snapshot* next = snapshots.poll())
        engine.setSnapshot(next);
    LaunchEvent launchEvent;
    while (launches.pop(launchEvent))
        engine.launch(launchEvent);
}

void TesseraModule::handleSceneControls() {
    int count = std::max(1, engine.sceneCount());
    int scene = std::min(sceneSelected.load(std::memory_order_relaxed), count - 1);
    if (scenePrevButton.process(params[SCENE_PREV_PARAM].getValue() > 0.f))
        scene = std::max(0, scene - 1);
    if (sceneNextButton.process(params[SCENE_NEXT_PARAM].getValue() > 0.f))
        scene = std::min(count - 1, scene + 1);
    if (inputs[SCENE_INPUT].isConnected()) {
        float v = inputs[SCENE_INPUT].getVoltage();
        float perScene = sceneCv.load(std::memory_order_relaxed) == SceneCvMode::Semitone ? 1.f / 12.f : 0.1f;
        scene = rack::math::clamp(int(std::round(v / perScene)), 0, count - 1);
    }
    sceneSelected.store(scene, std::memory_order_relaxed);
    uiSceneCount.store(count, std::memory_order_relaxed);

    bool launchNow = sceneLaunchButton.process(params[SCENE_LAUNCH_PARAM].getValue() > 0.f);
    launchNow |= launchInput.process(inputs[LAUNCH_INPUT].getVoltage(), kGateLow, kGateHigh);
    if (launchNow)
        engine.launch({LaunchEvent::Kind::Scene, 0, scene});
    if (stopAllButton.process(params[STOP_ALL_PARAM].getValue() > 0.f))
        engine.launch({LaunchEvent::Kind::StopAll});
}

void TesseraModule::publishLauncherStatus() {
    bool anyQueued = false;
    for (size_t i = 0; i < engine.trackCountNow(); i++) {
        int bus = engine.busSlotAt(i);
        if (bus < 0 || bus >= kMaxTracks)
            continue;
        LauncherSlot& s = launcherSlots[size_t(bus)];
        s.track.store(engine.trackAt(i).value, std::memory_order_relaxed);
        s.playing.store(engine.playingSlot(i), std::memory_order_relaxed);
        s.queued.store(engine.queuedSlot(i), std::memory_order_relaxed);
        s.position.store(engine.clipPosition(i), std::memory_order_relaxed);
        s.overridden.store(engine.overridden(i), std::memory_order_relaxed);
        s.elapsed.store(engine.clipElapsed(i), std::memory_order_relaxed);
        anyQueued |= engine.queuedSlot(i) != kNothingQueued;
    }
    lights[LAUNCH_LIGHT].setBrightness(anyQueued);
}

void TesseraModule::setParamWithUndo(int paramId, float value, const std::string& name) {
    float old = params[paramId].getValue();
    if (old == value)
        return;
    params[paramId].setValue(value);
    pushParamUndo(paramId, old, value, name);
}

void TesseraModule::pushParamUndo(int paramId, float old, float value, const std::string& name) {
    if (old == value)
        return;
    auto* action = new rack::history::ParamChange;
    action->name = name;
    action->moduleId = id;
    action->paramId = paramId;
    action->oldValue = old;
    action->newValue = value;
    APP->history->push(action);
}

void TesseraModule::writeTransportOutputs(const ProcessArgs& args) {
    // These are the previous sample's events, so clock and notes leave Tessera with the same one-sample delay.
    const TransportEvents& ev = lastEvents;
    if (ev.reset)
        resetPulse.trigger(kTriggerSeconds);
    if (ev.loop)
        eocPulse.trigger(kTriggerSeconds);
    if (ev.bar)
        barPulse.trigger(kTriggerSeconds);
    if (ev.beat) {
        clickPulse.trigger(kTriggerSeconds);
        clickLevel = ev.bar ? 10.f : 5.f;
        beatFlash.trigger(kBeatFlashSeconds);
    }

    outputs[CLOCK_OUTPUT].setVoltage(ev.clock ? 10.f : 0.f);
    outputs[RUN_OUTPUT].setVoltage(ev.running ? 10.f : 0.f);
    outputs[RESET_OUTPUT].setVoltage(resetPulse.process(args.sampleTime) ? 10.f : 0.f);
    outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.f : 0.f);
    outputs[BAR_OUTPUT].setVoltage(barPulse.process(args.sampleTime) ? 10.f : 0.f);
    outputs[CLICK_OUTPUT].setVoltage(clickPulse.process(args.sampleTime) ? clickLevel : 0.f);
}

void TesseraModule::onAdd(const AddEvent& e) {
    Registry& registry = Registry::get();
    // A duplicated module arrives with its original's name; give it its own so name lookups stay unambiguous.
    TesseraModule* sameName = registry.findSourceByName(name);
    if (name.empty() || (sameName && sameName != this))
        name = registry.uniqueSourceName(name.empty() ? "Tessera" : name);
    registry.addSource(this);
    registry.touch(this);
    // A different saved ID means this is a pasted or duplicated copy. Output modules pasted along with it
    // use this record to follow the copy instead of the original.
    if (savedId >= 0 && savedId != id)
        registry.recordPaste(savedId, id);
}

void TesseraModule::onRemove(const RemoveEvent& e) { Registry::get().removeSource(this); }

void TesseraModule::onReset(const ResetEvent& e) {
    Module::onReset(e);
    engine.stop();
    engine.reset();
    clockDivisionIndex.store(kDefaultClockDivision);
    runMode.store(RunInputMode::Toggle);
    sceneCv.store(SceneCvMode::TenthVolt);
    sceneSelected.store(0);
    setRecordSettings(RecordSettings());
    // IDs keep counting up from the old song's, so nothing that still points at it (an editor's selection, an open
    // menu, a device card) can match something in the new one.
    uint64_t used = songData.nextId;
    songData = makeDefaultSong();
    songData.nextId = std::max(songData.nextId, used);
    editorState.forgetSong();
    if (!songData.tracks.empty())
        editorState.selectedTrack = songData.tracks.front().id;
    snapshotBuilder = SnapshotBuilder();
    publish(ChangeSet::all());
    launchFirstClips();
}

json_t* TesseraModule::dataToJson() {
    json_t* root = json_object();
    json_object_set_new(root, "schema", json_integer(Song::kSchemaVersion));
    json_object_set_new(root, "name", json_string(name.c_str()));
    json_object_set_new(root, "clockDivision", json_integer(clockDivisionIndex.load()));
    json_object_set_new(root, "clockInPpqn", json_integer(clockInPpqn.load()));
    json_object_set_new(root, "runMode", json_string(runMode.load() == RunInputMode::Gate ? "gate" : "toggle"));
    json_object_set_new(root, "savedId", json_integer(id));
    json_object_set_new(root, "sceneCv", json_string(sceneCv.load() == SceneCvMode::Semitone ? "semitone" : "0.1v"));
    json_object_set_new(root, "scene", json_integer(selectedScene()));
    // Which clip each track plays, as [trackId, slot] pairs, so a reloaded patch carries on where it was.
    json_t* launcherJ = json_array();
    for (const Track& t : songData.tracks) {
        int playing = launcherStatus(t.id).playing;
        if (playing >= 0)
            json_array_append_new(launcherJ, json_pack("[I, i]", static_cast<json_int_t>(t.id.value), playing));
    }
    json_object_set_new(root, "launcher", launcherJ);
    json_object_set_new(root, "song", songToJson(songData));
    json_object_set_new(root, "ui", editorState.toJson());
    json_object_set_new(root, "record", recorder.settings.toJson());
    json_object_set_new(root, "midiInput", midiIn.toJson());
    if (!projectPath.empty())
        json_object_set_new(root, "projectPath", json_string(projectPath.c_str()));
    return root;
}

namespace {
constexpr const char* kProjectFormat = "tessera-project";
// Keys of the module data that belong to the patch, not the project.
const char* const kPatchKeys[] = {"savedId", "midiInput", "projectPath"};
} // namespace

json_t* TesseraModule::projectToJson() {
    json_t* root = dataToJson();
    for (const char* key : kPatchKeys)
        json_object_del(root, key);
    if (json_t* ui = json_object_get(root, "ui")) {
        json_object_del(ui, "panelHp");
        json_object_del(ui, "uiScale");
    }
    json_object_set_new(root, "format", json_string(kProjectFormat));
    json_object_set_new(root, "formatVersion", json_integer(1));
    // Tempo, loop and Plays are params, which the patch saves outside the module data.
    json_t* transport = json_object();
    json_object_set_new(transport, "bpm", json_real(params[BPM_PARAM].getValue()));
    json_object_set_new(transport, "loop", json_boolean(loopEnabled()));
    json_object_set_new(transport, "plays", json_string(playMode() == PlayMode::Arranger ? "timeline" : "clips"));
    json_object_set_new(root, "transport", transport);
    return root;
}

void TesseraModule::applyProjectTransport(const json_t* project) {
    const json_t* transport = json_object_get(project, "transport");
    if (!transport)
        return;
    if (const json_t* bpm = json_object_get(transport, "bpm"); json_is_number(bpm))
        params[BPM_PARAM].setValue(float(std::clamp(json_number_value(bpm), Engine::kMinBpm, Engine::kMaxBpm)));
    if (const json_t* loop = json_object_get(transport, "loop"); json_is_boolean(loop))
        params[LOOP_PARAM].setValue(json_is_true(loop) ? 1.f : 0.f);
    if (const char* plays = json_string_value(json_object_get(transport, "plays")))
        params[MODE_PARAM].setValue(std::string(plays) == "timeline" ? 1.f : 0.f);
}

json_t* TesseraModule::withProject(const json_t* project, const std::string& path) {
    const char* format = json_string_value(json_object_get(project, "format"));
    if (!format || std::string(format) != kProjectFormat || !json_object_get(project, "song"))
        return nullptr;
    json_t* merged = dataToJson();
    const char* key;
    json_t* value;
    json_object_foreach(const_cast<json_t*>(project), key, value) {
        std::string k = key;
        if (k == "format" || k == "formatVersion" || k == "transport" ||
            std::any_of(std::begin(kPatchKeys), std::end(kPatchKeys), [&](const char* p) { return k == p; }))
            continue;
        if (k == "ui") {
            // The panel's width and the editor's size stay as this patch has them.
            json_t* ui = json_deep_copy(value);
            json_t* current = json_object_get(merged, "ui");
            for (const char* keep : {"panelHp", "uiScale"})
                if (json_t* v = json_object_get(current, keep))
                    json_object_set(ui, keep, v);
            json_object_set_new(merged, "ui", ui);
            continue;
        }
        json_object_set(merged, key, value);
    }
    json_object_set_new(merged, "projectPath", json_string(path.c_str()));
    return merged;
}

void TesseraModule::dataFromJson(json_t* root) {
    if (const char* s = json_string_value(json_object_get(root, "name")))
        name = s;
    if (json_t* j = json_object_get(root, "clockDivision"))
        setClockDivision(int(json_integer_value(j)));
    clockInPpqn.store(int(std::clamp<int64_t>(json::getInt(root, "clockInPpqn", 24), 1, 96)));
    if (const char* mode = json_string_value(json_object_get(root, "runMode")))
        runMode.store(std::string(mode) == "gate" ? RunInputMode::Gate : RunInputMode::Toggle);
    savedId = json::getInt(root, "savedId", -1);
    if (const char* mode = json_string_value(json_object_get(root, "sceneCv")))
        sceneCv.store(std::string(mode) == "semitone" ? SceneCvMode::Semitone : SceneCvMode::TenthVolt);
    selectScene(int(json::getInt(root, "scene", 0)));

    // Older patches without a song keep the current one.
    if (const json_t* songJ = json_object_get(root, "song")) {
        int schema = int(json::getInt(root, "schema", Song::kSchemaVersion));
        if (schema > Song::kSchemaVersion)
            WARN("Tessera: patch uses song schema %d, newer than this build (%d); loading what it can", schema,
                 Song::kSchemaVersion);
        songData = songFromJson(songJ, schema);
        publish(ChangeSet::all());
        // Patches saved before the launcher existed played each track's first clip.
        if (const json_t* launcherJ = json_object_get(root, "launcher"))
            restoreLauncher(launcherJ);
        else
            launchFirstClips();
    }
    if (const json_t* uiJ = json_object_get(root, "ui"))
        editorState = EditorState::fromJson(uiJ);
    setRecordSettings(RecordSettings::fromJson(json_object_get(root, "record")));
    if (json_t* midiJ = json_object_get(root, "midiInput"))
        midiIn.fromJson(midiJ);
    const char* path = json_string_value(json_object_get(root, "projectPath"));
    projectPath = path ? path : "";
}

} // namespace tessera
