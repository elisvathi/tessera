#pragma once

#include "../engine/ArrangementCapture.hpp"
#include "../engine/ClockSync.hpp"
#include "../engine/Engine.hpp"
#include "../engine/Recorder.hpp"
#include "../engine/SnapshotExchange.hpp"
#include "../engine/SpscQueue.hpp"
#include "../engine/TapTempo.hpp"
#include "../model/AutomationTake.hpp"
#include "../model/Commands.hpp"
#include "../model/TrackCommands.hpp"
#include "../ui/EditorState.hpp"
#include "OutputBus.hpp"

#include <rack.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace tessera {

enum class RunInputMode { Toggle, Gate };
/// How the SCENE input picks a scene: 0.1 V apart, or one semitone (1/12 V) apart like a keyboard.
enum class SceneCvMode { TenthVolt, Semitone };

/// What a track's launcher is doing, as the UI sees it.
struct LauncherStatus {
    int playing = kNoSlot;
    int queued = kNothingQueued; // kNoSlot means a stop is queued
    double position = NAN;       // clip ticks of the playing clip
    bool overridden = false;     // Arranger mode: the launcher has taken the track over
    double elapsed = NAN;        // clip ticks since the playing clip started, for lanes with their own loop
};

/// Transport requests from the UI thread, carried out on the next sample.
enum TransportRequest : int {
    kRequestPlay = 1,
    kRequestStop = 2,
    kRequestToggle = 4,
    kRequestReset = 8,
    kRequestRecord = 16,
};

/// The sequencer module: owns the song, runs the engine and publishes track frames on its OutputBus.
///
/// Thread ownership: the song, bus slots, snapshot builder and instance name belong to the UI thread.
/// `engine` belongs to the audio thread. Values the panel shows travel through atomics.
class TesseraModule : public rack::engine::Module {
  public:
    enum ParamId {
        PLAY_PARAM,
        RESET_PARAM,
        LOOP_PARAM,
        TAP_PARAM,
        BPM_PARAM,
        SCENE_PREV_PARAM,
        SCENE_NEXT_PARAM,
        SCENE_LAUNCH_PARAM,
        STOP_ALL_PARAM,
        MODE_PARAM,
        REC_PARAM,
        FILL_PARAM,
        PARAMS_LEN
    };
    // New IDs go last, so cables in saved patches stay on their jacks.
    enum InputId {
        RESET_INPUT,
        RUN_INPUT,
        SCENE_INPUT,
        LAUNCH_INPUT,
        REC_VOCT_INPUT,
        REC_GATE_INPUT,
        REC_VEL_INPUT,
        AUTO_REC_INPUT,
        FILL_INPUT,
        CLOCK_INPUT,
        INPUTS_LEN
    };
    enum OutputId { CLOCK_OUTPUT, RESET_OUTPUT, RUN_OUTPUT, EOC_OUTPUT, BAR_OUTPUT, CLICK_OUTPUT, OUTPUTS_LEN };
    enum LightId { PLAY_LIGHT, LOOP_LIGHT, BEAT_LIGHT, LAUNCH_LIGHT, MODE_LIGHT, REC_LIGHT, FILL_LIGHT, LIGHTS_LEN };

    TesseraModule();
    ~TesseraModule() override;

    void process(const ProcessArgs& args) override;
    void onAdd(const AddEvent& e) override;
    void onRemove(const RemoveEvent& e) override;
    void onReset(const ResetEvent& e) override;
    json_t* dataToJson() override;
    void dataFromJson(json_t* root) override;

    /// A project file (.tessera): the song with this Tessera's settings, transport (tempo, loop, Plays) and editor
    /// layout, without what belongs to the patch (the module's ID, the MIDI device, the panel's width).
    json_t* projectToJson();
    /// Sets tempo, loop and Plays from a project file's transport; older files without it leave them as they are.
    void applyProjectTransport(const json_t* project);
    /// The module data with `project` opened in it, saved as `path`; nullptr when it isn't a project file. Applied
    /// with dataFromJson, so the caller can record it as one undo step.
    json_t* withProject(const json_t* project, const std::string& path);
    /// The file "Save project" writes to; empty until the project is saved or opened. Saved with the patch.
    std::string projectPath;

    // UI thread

    /// Applies an edit, publishes it to the engine and records it as one Rack undo step.
    /// `selectionAfter` replaces the note selection; undo and redo restore the selection of their moment.
    /// A null command (nothing to change) is ignored.
    void perform(CommandPtr command, std::optional<NoteSelection> selectionAfter = std::nullopt);
    /// Called by undo / redo of a recorded command.
    void revertCommand(Command& command);
    void reapplyCommand(Command& command);
    /// Housekeeping each UI frame: frees snapshots the engine has moved past, collects recorded arrangement.
    void uiStep();

    const Song& song() const { return songData; }
    /// A fresh ID for something a command is about to add. IDs are never reused, even after undo.
    template <typename IdT>
    IdT newId() {
        return songData.newId<IdT>();
    }
    /// Fresh ID values for commands that create several things at once (tracks with their clips).
    NextId ids() {
        return [this] { return songData.nextId++; };
    }
    /// Changes whenever the song changes, so views can tell when to redraw cached layers.
    uint64_t songRevision() const { return revision; }

    /// The editor's layout and views, saved with the patch.
    EditorState& editor() { return editorState; }

    void requestTransport(int request) { transportRequests.fetch_or(request); }
    /// Moves the song position on the next sample.
    void requestSeek(double tick) {
        seekTarget.store(tick);
        seekPending.store(true);
    }
    bool loopEnabled() { return params[LOOP_PARAM].getValue() > 0.5f; }
    /// Plays (or stops) a note on a track's outputs from the editor, e.g. a keyboard click.
    /// `seconds` > 0 releases it by itself; otherwise send a matching note-off.
    void audition(TrackId track, int pitch, float velocity, bool on, float seconds = 0) {
        auditions.push({track.value, int8_t(pitch), velocity, seconds, on});
    }
    /// A note played on the computer keyboard in the editor: it reaches armed tracks like a MIDI note (monitor,
    /// recording, capture) and is heard by step input.
    void playLive(int pitch, float velocity, bool on) {
        keyboardNotes.push({LiveSource::Keyboard, 0, int8_t(pitch), velocity, on});
    }
    /// Notes played on any input (MIDI, REC CV, computer keyboard) since the last call, for step input.
    std::vector<RecordEvent> takeLiveInput() { return std::exchange(liveInput, {}); }
    /// Launches or stops clips (DESIGN.md §10). Carried out on the next sample.
    void launch(const LaunchEvent& event) { launches.push(event); }
    void launchSlot(TrackId track, int slot) { launch({LaunchEvent::Kind::Slot, track.value, slot}); }
    /// `fromSlot`: the slot whose stop square was clicked, so that square blinks until the stop happens.
    void stopTrack(TrackId track, int fromSlot = -1) {
        stopScene.store(fromSlot, std::memory_order_relaxed);
        launch({LaunchEvent::Kind::StopTrack, track.value});
    }
    void launchScene(int scene) {
        stopScene.store(scene, std::memory_order_relaxed);
        launch({LaunchEvent::Kind::Scene, 0, scene});
    }
    /// The scene column whose stop squares blink while a stop waits for the launch quantize: the scene launched or
    /// the slot clicked last in the editor, else the selected scene (the panel's LAUNCH and SCENE input).
    int queuedStopScene() const {
        int s = stopScene.load(std::memory_order_relaxed);
        return s >= 0 ? s : selectedScene();
    }
    void stopAllClips() { launch({LaunchEvent::Kind::StopAll}); }
    /// Arranger mode: every track the launcher took over plays the arrangement again.
    void backToArrangement() { launch({LaunchEvent::Kind::BackToArrangement}); }
    PlayMode playMode() { return params[MODE_PARAM].getValue() > 0.5f ? PlayMode::Arranger : PlayMode::Launcher; }
    /// Switches Launcher / Arranger as one undo step.
    void setPlayMode(PlayMode mode);
    /// "Record to arrangement": what the launcher plays is written onto the timeline, as one undo step when
    /// it is switched off or the transport stops.
    bool recordingArrangement() const { return captureArmed.load(); }
    void setRecordArrangement(bool on);
    /// Clips being recorded right now, for drawing them live.
    const CaptureBuilder& capture() const { return captureBuilder; }
    LauncherStatus launcherStatus(TrackId track) const;

    // Recording (DESIGN.md §14)

    /// The record button: count-in or punch in, or punch out.
    void toggleRecord() { requestTransport(kRequestRecord); }
    /// The editor's FILL key (held): OR-ed with the panel button and the FILL gate.
    void setEditorFill(bool on) { editorFill.store(on); }
    bool recording() const { return uiRecording.load(std::memory_order_relaxed); }
    bool countingIn() const { return uiCountingIn.load(std::memory_order_relaxed); }
    const RecordSettings& recordSettings() const { return recorder.settings; }
    void setRecordSettings(const RecordSettings& settings);
    /// Takes in progress, for drawing them live.
    const Recorder& takes() const { return recorder; }
    bool canCapture() const { return recorder.canCapture(); }
    /// Turns the notes last played while not recording into a new clip on `track`. One undo step.
    ClipId capture(TrackId track) { return recorder.capture(track); }
    /// The CCs the last take moved but did not record: no lane (new lanes off), or no free AUTO channel
    /// (`channelsFull`). `serial` grows with each report, so every editor shows it once.
    struct UnlinkedControls {
        uint64_t serial = 0;
        TrackId track;
        std::vector<MidiControl> controls;
        bool channelsFull = false;
    };
    const UnlinkedControls& unlinkedControls() const { return unlinked; }
    rack::midi::InputQueue& midiInput() { return midiIn; }

    // Parameter mapping (DESIGN.md §6): track lanes drive knobs on other modules, no cable needed.

    /// The next MIDI control moved, or (for a track lane) knob touched in the rack, becomes what this lane records
    /// from: the control is linked, the knob mapped. cancelLearn() stops waiting.
    void startLearn(LaneOwner owner, AutomationId lane);
    void startLearn(TrackId track, AutomationId lane) { startLearn(LaneOwner::ofTrack(track), lane); }
    /// How many channels the AUTO REC input has patched now.
    int autoRecInputs() const { return autoRecChannels.load(std::memory_order_relaxed); }
    /// The next knob touched in the rack is turned by this modulator (docs/DEVICES.md §3 "CV out").
    void startLearn(ModulatorId modulator);
    void cancelLearn() {
        learnLane = AutomationId();
        learnModulator = ModulatorId();
    }
    bool learning() const { return learnLane.valid() || learnModulator.valid(); }
    AutomationId learningLane() const { return learnLane; }
    /// "VCF · Frequency", or "(missing module)".
    static std::string targetName(const ParamTarget& target);
    /// True when another mapping (a MIDI-Map, another Tessera) took this target over.
    bool targetTakenOver(AutomationId lane, int index) const;
    // Devices and modulators (docs/DEVICES.md)

    /// While a device knob or modulator setting is dragged, the engine plays `value` (normalized) before the edit
    /// is committed. releaseKnob() hands the knob back to the song.
    void holdKnob(const ParamRef& ref, float value);
    void releaseKnob() { heldKnobKey.store(0, std::memory_order_release); }
    /// A knob's value as it plays now (normalized): with modulation, or as held. Otherwise its saved value.
    float liveKnob(const ParamRef& ref) const;
    /// A knob's base as it plays now: what its lane sets while the song plays, else its saved value.
    float knobBase(const ParamRef& ref) const;
    /// Whether a lane turns this knob (playing or not).
    bool automated(const ParamRef& ref) const;
    /// The lane that records this knob now (its track is armed, the song records and plays), or nullptr. Turning
    /// the knob then writes the lane instead of changing the knob.
    const AutomationEnvelope* recordingKnobLane(const ParamRef& ref) const;
    /// A modulator's output now: -1..1 when bipolar, else 0..1.
    float modulatorOutput(ModulatorId id) const;

    /// Arranger mode: some track plays a launched clip (or was stopped) instead of the arrangement.
    bool anyTrackOverridden() const;
    /// The scene the panel's LAUNCH button and the LAUNCH input start.
    int selectedScene() const { return sceneSelected.load(std::memory_order_relaxed); }
    void selectScene(int scene) { sceneSelected.store(std::max(0, scene), std::memory_order_relaxed); }
    SceneCvMode sceneCvMode() const { return sceneCv.load(); }
    void setSceneCvMode(SceneCvMode mode) { sceneCv.store(mode); }

    /// Sets a param from the UI with an undo step, like turning the knob by hand.
    void setParamWithUndo(int paramId, float value, const std::string& name);
    /// Records an undo step for a param the UI already changed live (e.g. at the end of a BPM drag).
    void pushParamUndo(int paramId, float oldValue, float newValue, const std::string& name);
    const std::string& instanceName() const { return name; }
    void setInstanceName(const std::string& newName) { name = newName; }
    const std::shared_ptr<OutputBus>& outputBus() const { return bus; }
    int busSlotOf(TrackId track) const { return slots.slotOf(track); }

    int clockDivision() const { return clockDivisionIndex.load(); }
    void setClockDivision(int index);
    RunInputMode runInputMode() const { return runMode.load(); }
    void setRunInputMode(RunInputMode mode) { runMode.store(mode); }

    // Live transport state for the panel, safe to read from any thread
    double playheadTicks() const { return uiPlayhead.load(std::memory_order_relaxed); }
    double playTicks() const { return uiPlayTick.load(std::memory_order_relaxed); }
    bool playing() const { return uiPlaying.load(std::memory_order_relaxed); }
    double tempo() const { return uiTempo.load(std::memory_order_relaxed); }
    /// A cable on CLOCK: the tempo is the clock's, and the song follows its pulses.
    bool externalClock() const { return uiExternalClock.load(std::memory_order_relaxed); }
    int clockInputPpqn() const { return clockInPpqn.load(); }
    void setClockInputPpqn(int ppqn) { clockInPpqn.store(ppqn); }
    uint64_t activeTracks() const { return uiActiveTracks.load(std::memory_order_relaxed); }

  private:
    void handleTransportControls(const ProcessArgs& args);
    /// CLOCK input: its tempo and pulses drive the song while a cable is patched.
    void followClock(const ProcessArgs& args);
    void writeTransportOutputs(const ProcessArgs& args);
    void handleUiRequests();
    void handleSceneControls();
    void publishLauncherStatus();
    /// Queues the launcher state of a fresh song: each track plays its first clip.
    void launchFirstClips();
    void drainCapture();
    void commitCapture();
    void restoreLauncher(const json_t* launcherJ);
    void publish(const ChangeSet& changes);
    void readRecordInputs(const ProcessArgs& args);
    /// One thing a MIDI message meant, played into the engine: a note, or a value for a note or the channel.
    void playMidiEvent(const MidiEvent& event);
    void syncMappings();
    void writeMappings();
    void learnTouchedParam();
    /// The edit that adds the touched knob to the lane being learned, or null.
    CommandPtr learnLaneCommand(const ParamTarget& target);
    /// While learning a lane: a MIDI control moved since learning started becomes the lane's source.
    void learnMovedControl();
    /// Armed lanes record their source while the transport records; a loop wrap or stop ends the pass.
    void recordAutomation();
    bool readAutomationSource(const AutomationEnvelope& lane, float& value) const;
    bool holds(const ParamRef& ref) const; // the mouse holds this knob (holdKnob)
    /// The knob, device knob and AUTO REC lanes an armed track records: its own, and the playing clip's device
    /// knob lanes. None when the track isn't armed or a lane is marked "don't record".
    std::vector<const AutomationEnvelope*> recordableLanes(const Track& track) const;

    /// The recorder's view of this module.
    class Host : public RecorderHost {
      public:
        explicit Host(TesseraModule& m) : m(m) {}
        const Song& song() override { return m.songData; }
        void perform(CommandPtr command) override { m.perform(std::move(command)); }
        uint64_t nextId() override { return m.songData.nextId++; }
        void launchAt(TrackId track, int slot, double playTick) override;
        PlayMode playMode() override { return m.playMode(); }
        int targetScene() override { return m.selectedScene(); }
        void controlsNotRecorded(TrackId track, const std::vector<MidiControl>& controls, bool channelsFull) override {
            m.unlinked = {m.unlinked.serial + 1, track, controls, channelsFull};
        }

      private:
        TesseraModule& m;
    };

    // UI thread
    Song songData;
    BusSlots slots;
    SnapshotBuilder snapshotBuilder;
    std::string name;
    int64_t savedId = -1; // this module's ID when its JSON was written; differs after paste or duplicate
    EditorState editorState;
    uint64_t revision = 0;
    std::shared_ptr<OutputBus> bus = std::make_shared<OutputBus>();

    // UI thread -> audio thread
    SnapshotExchange snapshots;
    SpscQueue<AuditionEvent, 256> auditions;
    SpscQueue<LiveNote, 256> keyboardNotes;
    std::vector<RecordEvent> liveInput;
    SpscQueue<LaunchEvent, 256> launches;
    std::atomic<bool> captureArmed{false};

    // Audio thread -> UI
    CaptureQueue captureEvents;

    // UI thread
    CaptureBuilder captureBuilder;
    bool captureSawPlaying = false;
    Host recorderHost{*this};
    Recorder recorder{recorderHost};
    UnlinkedControls unlinked;
    // A knob held by a drag: kind, parameter and owner packed into one word (0 = none), then its value.
    std::atomic<uint64_t> heldKnobKey{0};
    std::atomic<float> heldKnobValue{0};
    struct OwnedHandle {
        uint64_t key; // lane ID * 4 + target index, or kModulatorKey | modulator ID * 8 + route index
        int64_t moduleId;
        int paramId;
        std::unique_ptr<rack::engine::ParamHandle> handle;
    };
    std::vector<OwnedHandle> handles;
    // Handles leave the engine at once but are freed a frame later, when process() can no longer be using them.
    std::vector<std::unique_ptr<rack::engine::ParamHandle>> retiredHandles;
    uint64_t mappedRevision = ~uint64_t(0);
    LaneOwner learnOwner;
    AutomationId learnLane;
    uint32_t learnMidiMoves = 0; // midiMoves when learning started: a later count is a control moved since
    // Audio thread -> UI: the last MIDI control that moved, as (kind << 8 | cc), and how many moves so far.
    std::atomic<uint32_t> lastMidiControl{0};
    std::atomic<uint32_t> midiMoves{0};
    ModulatorId learnModulator;
    // Knob and AUTO REC lanes read this pass: each one's last value (a device knob let go keeps it), and the
    // device knobs among them, which the pass holds until it ends.
    std::map<uint64_t, float> laneValues;
    std::vector<ParamRef> recordedKnobs;
    bool automationRecording = false;

    // Audio thread -> UI
    RecordQueue recordEvents;

    // Audio thread
    Engine engine;
    float engineSampleRate = 0;
    TapTempo tapTempo;
    TransportEvents lastEvents; // written to the outputs one sample late, to line up with the output bus
    rack::dsp::BooleanTrigger playButton, resetButton, tapButton;
    rack::dsp::BooleanTrigger scenePrevButton, sceneNextButton, sceneLaunchButton, stopAllButton;
    rack::dsp::SchmittTrigger resetInput, runInput, launchInput, clockInput;
    ClockSync clockSync;
    bool clockWasPlaying = false;
    std::atomic<int> clockInPpqn{24};
    std::atomic<bool> uiExternalClock{false};
    rack::dsp::ClockDivider launcherStatusDivider;
    rack::dsp::PulseGenerator resetPulse, eocPulse, barPulse, clickPulse;
    float clickLevel = 0;
    rack::dsp::PulseGenerator beatFlash;
    rack::dsp::BooleanTrigger recButton;
    rack::midi::InputQueue midiIn;
    rack::midi::Message midiMessage; // reused: a Message holds a vector, so a new one per sample would allocate
    MidiReader midiReader;           // audio thread
    bool midiReaderMpe = false;      // the MPE setting the reader was last given
    std::array<rack::dsp::SchmittTrigger, kMaxVoices> recGates;
    std::array<int8_t, kMaxVoices> recPitches{};
    bool recGateHeld = false; // a REC GATE channel is high
    float recBlink = 0;

    // Settings written on the UI thread, read on the audio thread
    std::atomic<int> clockDivisionIndex;
    std::atomic<RunInputMode> runMode{RunInputMode::Toggle};
    std::atomic<SceneCvMode> sceneCv{SceneCvMode::TenthVolt};
    std::atomic<int> sceneSelected{0}; // written by both threads; the last write wins
    std::atomic<int> stopScene{-1};    // UI thread: see queuedStopScene()
    std::atomic<int> transportRequests{0};
    std::atomic<double> seekTarget{0};
    std::atomic<bool> seekPending{false};
    std::atomic<int> countInBars{0};
    std::atomic<bool> editorFill{false};
    std::array<std::atomic<float>, kMaxAutomationLanes> autoRecVolts{};
    std::atomic<int> autoRecChannels{0};
    std::atomic<bool> liveToKey{false};
    std::atomic<bool> newControlLanes{true};
    std::atomic<bool> replaceTakes{false};
    std::atomic<bool> mpeInput{false};

    // Audio thread -> UI
    std::atomic<double> uiPlayhead{0};
    std::atomic<double> uiPlayTick{0};
    std::atomic<bool> uiPlaying{false};
    std::atomic<double> uiTempo{120};
    std::atomic<uint64_t> uiActiveTracks{0};
    std::atomic<bool> uiRecording{false};
    std::atomic<bool> uiCountingIn{false};
    std::atomic<int> uiSceneCount{1};
    struct LauncherSlot {
        std::atomic<uint64_t> track{0};
        std::atomic<int> playing{kNoSlot};
        std::atomic<int> queued{kNothingQueued};
        std::atomic<double> position{NAN};
        std::atomic<bool> overridden{false};
        std::atomic<double> elapsed{NAN};
    };
    std::array<LauncherSlot, kMaxTracks> launcherSlots; // by bus slot

    // UI thread -> audio thread: the mapped knobs process() writes.
    struct MapSlot {
        std::atomic<rack::engine::ParamHandle*> handle{nullptr};
        std::atomic<int> busSlot{-1};
        std::atomic<int> output{0};
        std::atomic<float> min{0}, max{1};
        std::atomic<bool> paused{false}; // the knob is being recorded: let the hand move it
        // A modulator's Rack knob route instead of a lane: its slot, amount and whether it swings both ways.
        std::atomic<int> modSlot{-1};
        std::atomic<float> amount{0};
        std::atomic<bool> bipolar{false};
        uint64_t lane = 0;   // UI thread only
        float written = NAN; // audio thread only
    };
    static constexpr int kMaxMaps = 128;
    std::array<MapSlot, kMaxMaps> maps;
    std::atomic<int> mapCount{0};
    rack::dsp::ClockDivider mapDivider;
};

} // namespace tessera
