# Tessera design

The full design of **Tessera**, a VCV Rack 2 plugin that brings a DAW-grade sequencing environment into the rack. It has a dynamic number of polyphonic tracks (1–64) routed to free-standing output modules, a full-featured piano roll, a drum step/grid editor, a clip launcher (session view), a linear arranger, scale-aware editing, live recording from external CV/MIDI, and an automation editor that outputs CV.

This file describes the original design. **The editors (piano roll, drum editor, arranger, automation editing, navigation, keymap) are specified in [`EDITOR_UX.md`](EDITOR_UX.md), which is authoritative for anything under `src/ui/`.** Choices that refine or replace parts of this file are recorded in [`DECISIONS.md`](DECISIONS.md); where they disagree, DECISIONS.md wins. When a decision isn't covered here, choose the option that (1) keeps the audio thread real-time safe, (2) matches what a user of a modern DAW would expect, and (3) keeps the model testable without Rack.

---

## 1. Goals and non-goals

### Goals
- Feel like a real DAW editor: fast, precise, keyboard-driven, mouse-friendly, with undo for everything.
- Dynamic track count (1–64), each track up to 16-voice polyphonic (Rack's cable limit).
- Track outputs live on separate **output modules** placed anywhere in the patch (no adjacency, no chaining), selected by Tessera instance + track. See §6.
- Automation can drive other modules' knobs directly via parameter mapping, no cable needed.
- Clips are editable, reusable, loopable, and can be placed in both a **Clip Launcher** (session view) and a **linear Arranger** (timeline).
- Drum tracks with per-lane trigger/gate outputs and a dedicated drum editor.
- Scale/key system that constrains and highlights editing and recording.
- Record notes and automation from external poly CV/gate and MIDI.
- Automation lanes per track and per clip, output as CV.
- Export songs, ranges, tracks and clips as Standard MIDI Files, including automation as CC and expressions as MPE (§17).
- Beautiful, consistent, dark-first design. Sample-accurate timing.

### Non-goals (v1)
- No audio recording, no audio clips, no plugin hosting.
- No MIDI file import in v1 (export is in scope, §17; the shared SMF code makes import a small v1.1 addition).
- No networked collaboration.

---

## 2. Hard constraints

- **Rack SDK 2.x**, C++17 (Rack's default toolchain). Build with the standard plugin `Makefile` (`RACK_DIR`).
- **No external dependencies** beyond what Rack ships (NanoVG, jansson, Rack's `midi::`, `dsp::`, `history::`, `ui::`, `event::`).
- Must build on macOS (arm64 + x64), Windows, Linux via the Rack plugin toolchain.
- **The audio thread (`process()`) must never**: allocate, free, lock a blocking mutex, log, do file I/O, or touch widgets.
- **Widgets must never** mutate engine state directly. All edits go through the command layer (§5).
- All edits must be undoable via Rack's `APP->history` (Cmd/Ctrl+Z works globally).
- Patch state saves/loads completely through `dataToJson` / `dataFromJson`, with a schema version.

---

## 3. Repository layout

```
tessera/
  Makefile
  plugin.json
  res/
    Tessera.svg                # module panel
    fonts/                     # Inter (UI), JetBrains Mono (numbers) — OFL licensed
  src/
    plugin.cpp / plugin.hpp
    model/                     # PURE C++. No Rack includes. Unit-testable.
      Types.hpp                # Tick, Pitch, Velocity, IDs
      Note.hpp  Clip.hpp  Track.hpp  Song.hpp
      Automation.hpp           # envelopes, curves, evaluation
      Scale.hpp  Scale.cpp     # scale definitions, quantize, snap
      Quantize.hpp             # grid, swing, humanize, strength
      Transforms.hpp/.cpp      # pure note transforms (EDITOR_UX §4.8)
      Commands.hpp/.cpp        # every edit as a reversible command
      Serialize.hpp/.cpp       # model <-> JSON (via thin jansson wrapper)
      NoteOperators.hpp        # chance, repeats, occurrence, recurrence, spreads (shared by engine and export)
      Render.hpp/.cpp          # Song -> flat timed event list (notes, CCs, tempo), used by MIDI export
      MidiFile.hpp/.cpp        # Standard MIDI File writer (and a minimal reader for tests)
    engine/                    # Audio-thread side
      Engine.hpp/.cpp          # transport, playhead, scheduling
      VoiceAllocator.hpp       # per-track poly channel assignment
      Snapshot.hpp             # immutable, render-ready song snapshot
      Recorder.hpp/.cpp        # CV/MIDI capture -> lock-free FIFO
      ClockSync.hpp            # external clock / PPQN / tempo estimation
    module/
      TesseraModule.hpp/.cpp   # rack::Module: transport, global I/O, process()
      TrackOutModule.hpp/.cpp  # "Tessera Track Out": instrument track outputs
      DrumOutModule.hpp/.cpp   # "Tessera Drum Out": per-lane drum outputs
      OutputBus.hpp            # double-buffered frame bus (see §6)
      Registry.hpp/.cpp        # instance registry keyed by module ID
      ParamMapper.hpp/.cpp     # automation lane -> other modules' params
    ui/
      Theme.hpp                # colors, metrics, fonts — single source of truth
      TesseraWidget.cpp        # panel widget
      EditorWindow.hpp/.cpp    # full-screen overlay editor host
      views/
        TransportBar.cpp
        TrackHeaders.cpp
        ClipLauncher.cpp
        Arranger.cpp
        PianoRoll.cpp
        DrumEditor.cpp
        AutomationLane.cpp
        VelocityLane.cpp
        Inspector.cpp
        ScaleBar.cpp
      widgets/                 # buttons, knobs, dropdowns, scrollbars, tooltips
      interaction/
        Axes.hpp               # TimeAxis, PitchAxis, TrackAxis (zoom/scroll math)
        Gesture.hpp            # gesture engine base (EDITOR_UX §2)
        Tools.hpp              # draw/select/erase/slice/paint/mute/velocity/zoom/range tools
        DragHandlers.cpp
        Shortcuts.cpp
  tests/
    model_tests.cpp            # builds without Rack (plain g++ + doctest single header in tests/)
```

`model/` must compile standalone. That's how we test the hard logic (quantize, scales, commands, serialization, automation evaluation) quickly.

---

## 4. Data model

Time unit: **ticks, 960 PPQ** (`using Tick = int64_t`). All positions and lengths are ticks. Never store seconds or samples in the model.

```cpp
struct NoteOperators {    // per-note playback rules, evaluated by the engine each pass (EDITOR_UX §4.5)
  float    chance = 1;          // 0..1
  float    velocitySpread = 0;  // 0..1, random ± around velocity
  int8_t   repeats = 0;         // -16..16 (negative = per beat), 0 = off
  float    repeatCurve = 0;     // -1..1
  float    repeatVelEnd = 1;    // 0..1
  float    repeatGate = 0.5;    // 0..1
  Occurrence occurrence = Occurrence::Always;  // First, NotFirst, PrevPlayed, PrevNotPlayed, Fill, NotFill
  uint8_t  recurrenceLength = 1;  // 1..16 passes
  uint16_t recurrenceMask = 0xFFFF;
  float    timingSpread = 0;    // 0..0.5 of a 1/16
};

struct ExprPoint { float pos; float value; float tension; };   // pos 0..1 across the note

struct Note {
  NoteId   id;
  Tick     start;
  Tick     length;        // >= 1
  int8_t   pitch;         // 0..127 (MIDI). V/OCT = (pitch - 60) / 12
  float    velocity;      // 0..1
  float    releaseVelocity;   // 0..1, default 0.5
  float    microOffset;   // fraction of a 1/16, -0.5..0.5, applied at playback
  bool     muted;
  uint8_t  colorTag;      // 0 = inherit
  NoteOperators ops;
  // Per-note expression curves (empty = flat). Pitch in semitones ±24, others 0..1.
  std::vector<ExprPoint> pitchExpr, timbreExpr, pressureExpr;
};

enum class LoopMode { Off, Loop };

struct Clip {
  ClipId   id;
  std::string name;
  uint32_t color;
  Tick     length;        // clip content length
  Tick     loopStart, loopEnd;
  LoopMode loopMode;
  std::vector<Note> notes;            // kept sorted by (start, pitch)
  std::vector<AutomationEnvelope> automation;  // clip-level automation
  ScaleRef scaleOverride;             // optional
};

enum class TrackType { Instrument, Drum };

struct DrumLane {
  std::string name;       // "Kick", "Snare", ...
  int8_t   pitch;         // the note number this lane uses inside clips
  int      outputIndex;   // which trigger output channel
  bool     mute, solo;
  float    gateLengthMs;  // 0 = trigger, >0 = gate
  float    chokeGroup;    // 0 = none
};

struct Track {
  TrackId  id;            // stable for the track's lifetime; output modules bind to this, not to the index
  std::string name;
  uint32_t color;
  TrackType type;
  int      polyphony;     // 1..16, instrument tracks
  bool     mute, solo, armed, monitor;
  float    gateLengthScale;   // 0..1, legato vs staccato at output
  int8_t   transpose;
  std::vector<DrumLane> drumLanes;     // drum tracks only, up to 16
  std::vector<ClipSlot> slots;         // launcher slots (scenes)
  std::vector<ArrangerClip> arrangement;
  std::vector<AutomationEnvelope> automation;   // track-level (arranger time)
};

struct ClipSlot { std::optional<ClipId> clip; };

struct ArrangerClip {     // an instance on the timeline referencing a Clip
  ArrangementId id;
  ClipId   clip;          // shared reference -> edit once, all instances change
  Tick     position;
  Tick     length;        // may differ from clip length (loops or truncates)
  Tick     offset;        // start offset into the clip
  bool     muted;
  int8_t   transpose = 0;       // non-destructive, per instance
  float    velocityScale = 1;   // non-destructive, per instance
  float    stretch = 1;         // time-stretch factor (Alt+drag edge)
};

struct Scene { std::string name; uint32_t color; std::optional<double> tempo; };

struct Song {
  int      schemaVersion;
  double   tempo;         // BPM, internal clock
  TimeSig  timeSig;       // num/den
  float    swing;         // 0..1
  ScaleRef scale;         // global root + scale id
  Tick     loopStart, loopEnd; bool loopOn;
  std::vector<Track> tracks;   // 1..kMaxTracks (64). Order = display order only.
  std::vector<TrackGroup> groups;   // optional folders for organizing many tracks (collapse/expand in UI)
  std::vector<Scene> scenes;
  ClipPool clips;         // all clips by ClipId
  std::vector<Marker> markers;
  PlayMode playMode;      // Launcher | Arranger
};
```

**Clip sharing:** Arranger instances and launcher slots reference clips by ID. Provide "Make unique" to fork a clip. Instances of the same clip show a link badge.

**Tracks are dynamic.** Commands: `AddTrack(type, index)`, `DeleteTrack`, `DuplicateTrack`, `MoveTrack`, `GroupTracks`. Deleting a track that an output module is bound to leaves that module showing "Track missing" (it never silently rebinds to another track). Undoing the delete restores the binding, because bindings use `TrackId`, not position. A new song starts with 4 tracks (3 instrument, 1 drum).

### Automation

```cpp
enum class CurveType { Hold, Linear, Exponential, SCurve };

struct AutoPoint { Tick t; float value; CurveType curve; float tension; };  // value 0..1 normalized

struct ParamTarget {        // cable-free automation target (see §6 "Parameter mapping")
  int64_t moduleId;         // Rack module ID of the target
  int     paramId;
  float   min, max;         // sub-range of the target param, normalized 0..1
};

struct AutomationEnvelope {
  AutomationId id;
  std::string name;
  int   outputIndex;      // which automation CV channel on the track's output module (0..7)
  float rangeMin, rangeMax;   // output voltage mapping, e.g. 0..10V or -5..5V
  bool  bipolar;
  std::vector<ParamTarget> targets;   // up to 4 mapped params, driven directly
  std::vector<AutoPoint> points;   // sorted by t
};
```

Evaluate with `float evaluate(const AutomationEnvelope&, Tick)` — binary search the segment, interpolate per curve. Tension controls curvature (drag a curve to bend it). Unit test every curve type at boundaries.

---

## 5. Commands, undo, and threading

### Command layer
Every user edit is a `Command` with `apply(Song&)` and `revert(Song&)`. Examples: `AddNotes`, `DeleteNotes`, `MoveNotes(dt, dpitch)`, `ResizeNotes`, `SetVelocity`, `QuantizeNotes`, `SplitClip`, `PlaceArrangerClip`, `EditAutomationPoints`, `SetTrackProperty`, `RecordTake`.

- Commands store the minimal diff, not whole-song copies.
- Wrap each command in a `rack::history::Action` subclass so Rack's global undo/redo drives it. Name actions clearly ("Move 12 notes", "Quantize to 1/16").
- Continuous gestures (dragging notes, drawing automation) produce **one** command on mouse-up. During the drag, preview in UI-local state.

### Threading model (critical)
- **UI thread** owns the editable `Song`.
- After any committed edit, the UI builds an immutable **render `Snapshot`** (flattened, time-sorted events per track, evaluated automation breakpoints) and publishes it to the engine through a lock-free pointer swap (`std::atomic<Snapshot*>`).
- Old snapshots are freed **on the UI thread** via a deferred-free list once the engine acknowledges it moved on (engine writes an atomic "current generation" counter).
- Snapshot building must be incremental for large songs: rebuild only changed tracks.
- **Engine → UI** data (playhead, active notes, meter, recorded events) flows through single-producer/single-consumer ring buffers or atomics. Never share mutable containers.
- Recording: engine pushes raw `RecordedEvent {tick, pitch, velocity, on/off, track}` to an SPSC FIFO; the UI drains it each frame, shows it live, and commits a single `RecordTake` command when recording stops.

---

## 6. Modules, I/O, and output routing

Tessera is a small family of modules:

| Module | HP | Role |
|---|---|---|
| **Tessera** | 24 | The sequencer: transport, editor, global I/O. Owns the song. Several can exist in one patch. |
| **Tessera Track Out** | 6 | Outputs for one instrument track. Place anywhere, as many as you like. |
| **Tessera Drum Out** | 10 | Per-lane outputs for one drum track. Place anywhere. |

There are **no expanders and no chaining**. Output modules find their sequencer through a registry (below). This design is informed by Biset Tracker, whose Tracker Synth / Tracker Drum modules select a synth by number and read its outputs from anywhere in the patch. Tessera keeps that UX but fixes its two weaknesses: Biset allows only one global Tracker instance, and its output modules read shared arrays with no synchronization.

The main module is a compact panel with a mini-overview and an **"Open Editor"** button. The panel can also be widened to show a compact in-panel editor (docs/DECISIONS.md §15). The full editor opens as an **overlay that fills the Rack window** (add a widget to `APP->scene`, close with Esc or the close button). The module must stay usable from the panel alone for transport and scene launching.

### Panel controls (Tessera)
- Play/Stop, Record, Loop, Tap Tempo, BPM knob (or display + drag), Mode switch (Launcher/Arranger), a small track-activity strip (one LED per track, scrolls/condenses past 16), scene-launch buttons (prev/next/launch), Open Editor.
- Instance name display (editable, e.g. "Main", "Drums B") so output modules can show which Tessera they follow.

### Inputs
| Input | Type | Purpose |
|---|---|---|
| CLOCK | trig | External clock. PPQN selectable (1, 2, 4, 8, 12, 24, 48, 96) |
| RESET | trig | Return to start (or loop start) |
| RUN | gate/trig | Start/stop (toggle or gate mode, configurable) |
| REC V/OCT | poly | Recording pitch input |
| REC GATE | poly | Recording gate input |
| REC VEL | poly | Optional velocity (0–10V) |
| SCENE | CV | Select scene by voltage (0.1V per scene or 1V/oct steps, configurable) |
| LAUNCH | trig | Launch selected scene |
| FILL | gate | Fill state for notes with Fill / Not-fill occurrence (OR-ed with the panel FILL button) |
| AUTO REC | poly up to 8 | Automation recording source, channel = lane |
| MIDI IN | menu | Rack MIDI driver/device/channel for recording and note input |

### Global outputs (Tessera)
CLOCK OUT (selectable division), RESET OUT, RUN OUT, EOC/loop trigger, BAR trigger, CLICK (count-in/metronome).

### Tessera Track Out
| Port / control | Type | Notes |
|---|---|---|
| V/OCT | poly up to 16 | Pitch per voice, including per-note pitch expression |
| GATE | poly | Gate per voice (note repeats appear here as separate gates) |
| VEL | poly | 0–10V |
| REL | poly | Release velocity, 0–10V, held after gate falls |
| TIMBRE | poly | Per-note timbre expression, 0–10V |
| PRESS | poly | Per-note pressure expression, 0–10V |
| AUTO | poly up to 8 | The track's automation lanes (channel = lane), already scaled to each lane's voltage range |
| Display | — | Source instance name + track number, name and color. "No source" / "Track missing" states are clearly shown |
| Prev/Next buttons | — | Step through tracks of the bound Tessera |

Context menu: choose source Tessera (by instance name), choose track (list with names and colors), "Follow selected track" mode (outputs whatever track is selected in the editor, useful for auditioning one synth across tracks).

### Tessera Drum Out
| Port | Type | Notes |
|---|---|---|
| TRIG 1–8 | mono ×8 | One jack per lane for lanes 1–8 (the common case: patch kick, snare… to separate voices) |
| TRIG POLY | poly up to 16 | All lanes on one cable, channel = lane |
| VEL POLY | poly up to 16 | Velocity per lane |
| AUTO | poly up to 8 | The drum track's automation lanes |

Same display, selectors and context menu as Track Out. Lanes 9–16 are available on the poly outputs.

### Routing: registry and output bus

**Registry** (`Registry.hpp`): a plugin-global map `moduleId -> TesseraModule*`, guarded by a mutex that is only ever taken on the UI thread or in `onAdd`/`onRemove`, never in `process()`.

- Each output module stores its binding as `{ sourceModuleId, trackId }` and saves it in its own JSON.
- **Binding resolution happens off the audio thread.** The output module's widget `step()` (UI thread) looks up the source in the registry, then hands the audio thread a pointer to that Tessera's `OutputBus` via an atomic. `process()` only ever reads that atomic pointer.
- When a Tessera is removed, it first clears the bus pointers of all bound output modules (via the registry) before it is destroyed. Use Rack's `onRemove` for this, and keep the bus alive until every reader has dropped it (reference count or generation counter checked on the UI thread).
- **Default binding on add:** a new output module binds to the most recently focused Tessera and its first track that has no output module yet. If there is only one Tessera, the user never needs to pick it.
- **Patch load / module ID remapping:** when a patch is loaded or modules are pasted/duplicated, module IDs can change. Store the source by ID, and on failure to resolve, fall back to matching the stored instance name. If neither matches, show "No source" and wait (the Tessera may be added later in the same load).
- Multiple output modules may bind to the same track (e.g. one next to the VCO, one next to the filter). This must just work.

**OutputBus** (`OutputBus.hpp`): the per-sample handoff from Tessera to its output modules.

```cpp
struct TrackFrame {
  uint8_t  channels;            // voices in use
  float    voct[16], gate[16], vel[16], rel[16], timbre[16], press[16];
  float    automation[8];       // already in volts
};

struct OutputBus {
  // Double buffer indexed by sample parity. Tessera writes frame[n & 1] during sample n;
  // readers read frame[(n - 1) & 1] — the complete frame from the previous sample.
  std::array<std::array<TrackFrame, kMaxTracks>, 2> frames;
  std::atomic<uint64_t> sampleIndex;   // published by Tessera after writing a frame
  // trackId -> slot index table, rebuilt on the UI thread and swapped atomically
  std::atomic<const TrackSlotTable*> slots;
};
```

- As built, the slot table above is replaced by stable per-track slots plus a `trackId` tag on each frame (docs/DECISIONS.md §2–3).
- **Fixed one-sample latency for every track,** regardless of module processing order or Rack's thread count. This is the key guarantee Biset's approach lacks. All output modules output the frame for sample n−1, so all tracks stay sample-aligned with each other.
- The global CLOCK/RESET/RUN outputs on Tessera are delayed by the same one sample so the clock stays aligned with notes.
- If a reader sees that `sampleIndex` hasn't advanced (Tessera bypassed, removed, or engine stalled), it holds the last frame with gates forced low. Never output a stale high gate.
- Bypassing Tessera (Rack's bypass) sets all gates low on every output module.
- Gate retrigger gaps (see Voice allocation) are written into frames by Tessera, so output modules stay dumb: copy frame to ports, set channel counts, done.

### Parameter mapping (cable-free automation)

Borrowed from Biset Tracker Synth's MIDI-learn-style CV mapping. Each automation lane can target up to 4 parameters on any module in the patch.

- Implemented with Rack's `ParamHandle` (`APP->engine->addParamHandle` / `updateParamHandle`). Mapped knobs show a colored handle indicator in the track's color.
- **Learn flow:** in the lane header, click "Map", then touch any knob in the rack. Esc cancels. Each target has its own min/max sub-range (set in the lane header).
- Mapped values are written on the audio thread via `ParamQuantity::setValue` (smoothed with the lane's slew setting) by **the Tessera module itself**, so mapping works even when no output module exists for that track.
- Rack allows only one handle per param; if a param is already mapped elsewhere, show a warning and offer to take it over.
- Mapping changes are undoable commands. Mappings are saved by module ID + param ID and survive patch reload.

### Voice allocation
- Per track, round-robin with voice stealing (oldest note) when exceeding polyphony.
- Retrigger behavior: when a voice is reused while its gate is high, drop the gate low for **1 sample-block (≥1 ms)** so downstream envelopes retrigger.
- Output channel count = track polyphony setting (output modules call `setChannels` from `TrackFrame::channels`).
- Tessera computes all tracks every sample, whether or not an output module is bound, so binding/unbinding never changes timing and parameter mapping keeps working.

---

## 7. Engine and timing

- **Internal clock:** BPM 20–300, double-precision tick accumulator advanced per sample (`ticksPerSample = bpm * 960 / 60 / sampleRate`). Never round per sample; accumulate in double.
- **External clock:** measure interval between pulses, smooth tempo estimate (moving average over last N pulses), and lock tick position to pulses so it never drifts. Show "EXT 120.3" in the transport.
- **Swing:** applied at playback to even 1/16 (or user-chosen 1/8) positions. Model stays unswung.
- **Scheduling:** each `process()`, find events in `[prevTick, curTick)` across snapshot tracks. Handle loop wraparound by splitting the window. Events must fire on the correct sample.
- **Note-off safety:** stopping, muting, looping, or swapping snapshots must release all active gates. Never leave hanging notes.
- **Launcher mode:** per-track playing clip, queued clip, quantized launch (Off, 1/16, 1/4, 1 bar, 2, 4 bars). Scene launch queues all tracks. Stop-clip per track, stop-all.
- **Arranger mode:** play timeline with loop region. Option: "Launcher overrides arranger" per track, with a "Back to arrangement" button.
- **Probability:** evaluate with a seeded RNG per note per loop iteration so behavior is reproducible within a session.

---

## 8. Piano roll

**Full spec: `docs/EDITOR_UX.md` §1–4.** Summary of what it requires:

- Layout: audition keyboard, ruler with loop strip, markers and overview bar, note grid, stacked expression lanes, collapsible inspector, ghost notes.
- Tools: Pointer, Draw, Paint, Erase, Slice, Mute, Velocity, Zoom, Chord stamp, step input, computer-keyboard play.
- Industry-standard navigation: cursor-anchored zoom, ruler-drag zoom, zoom history, minimap, smart follow, edge auto-scroll while dragging.
- Note operators per note (chance, velocity spread, repeats with curve, occurrence, recurrence, timing spread) and per-note expression curves (pitch, timbre, pressure).
- Inspector with offset / **spread** / randomize / ramp for multi-note value distributions; transform box for stretching and scaling a selection.
- Transform functions with live preview (quantize, humanize, velocity shaper, strum, flam, chop, arpeggiate, connect, invert…).
- One shared modifier vocabulary and a gesture engine where every drag is one undo step and Esc cancels.

---

## 9. Drum editor

Opens automatically for Drum tracks (toggle to piano roll for advanced editing).

- Rows = drum lanes with name, mute/solo, color, output channel, and audition button.
- Two view modes:
  - **Step grid**: big clickable steps (default 16 per bar, configurable), click to toggle, drag to paint/erase, velocity shown as step fill height. Per-step context menu: velocity, chance, repeats, occurrence, recurrence, micro-offset (the same note operators as the piano roll).
  - **Grid editor**: same as piano roll but per-lane rows, for free-time placement.
- Per-lane step count and rate (polymeter: e.g. hats at 12 steps while kick is 16). Store as lane-level loop length override.
- Lane presets: "Standard 8 (Kick, Snare, Clap, CH, OH, Tom L, Tom H, Rim)" and "16 GM-ish". Users can rename and add lanes up to 16.
- Choke groups (e.g. closed hat chokes open hat): when a lane fires, release gates of others in the same group.
- Pattern tools: rotate lane left/right, randomize lane (density + seed), copy/paste lane, fill (every N steps), Euclidean generator per lane (hits, steps, rotation).

---

## 10. Clip launcher (session view)

- Grid: one column per track × N scene rows (start with 8, grow on demand, max 64). Columns scroll horizontally past the visible width; track groups collapse to a single column showing the group's activity.
- Each slot: empty / clip (colored, named, with a mini note preview) / playing (animated progress ring) / queued (blinking) / recording (red).
- Click play triangle on a slot to launch; click empty slot's stop square to stop the track.
- Double-click empty slot: create clip (default 1 bar) and open it in the editor.
- Drag clips between slots (Alt/Ctrl duplicates); drag a clip from the launcher into the arranger.
- Scene column on the right: launch whole scene, rename, color, optional tempo change.
- Launch quantization dropdown in the header. Per-clip override in the clip inspector.
- "Record arrangement from launcher": while launching clips live, capture what plays onto the arranger timeline. Single undoable command.

---

## 11. Arranger (timeline)

**Full spec: `docs/EDITOR_UX.md` §6.** Summary of what it requires:

- Track headers with groups, multi-select, reorder, output status dot, height resize.
- Clips with cached previews, loop seams, link badges; move across tracks, loop-extend, trim, slip (Alt+drag body), time-stretch (Alt+drag edge), linked/unique duplicates, split, consolidate.
- Per-instance transpose, velocity scale and stretch (non-destructive).
- Magnetic snap to clip edges, markers, loop and playhead.
- Range tool with delete-time, insert-silence, duplicate-range; Normal and Ripple edit modes.
- Expanded automation lanes per track with the full automation editor (§12), and "automation follows clips".

---

## 12. Automation editor

- Each track has expandable automation lanes in the arranger below the track (track-level, arranger time). Each clip has its own automation lanes in the piano roll lower pane (clip time, loops with the clip).
- If both exist for the same output, combine with a per-lane mode: **Override** (clip wins) or **Modulate** (clip value is added as an offset around 0.5).
- Up to **8 automation lanes per track**, mapped to the AUTO poly output channels 1–8 of that track's output modules, and optionally to up to 4 mapped parameters each (§6 "Parameter mapping").
- Lane header: name, output channel, voltage range preset (0–10V, ±5V, 0–5V, ±10V, custom), bipolar flag, mute, "show in arranger" toggle, color, and a **Map** button with the list of mapped targets (module name + param name, each with min/max and a remove button).
- **Editing UX is specified in `docs/EDITOR_UX.md` §6.6** (point editing, drawing, shape stamps, transform box with tilt, lane management, recording feedback). Minimum feature set:
  - Click to add point; drag point (snaps to grid on time, Alt bypass); Shift for fine value.
  - Drag a segment's midpoint vertically to adjust curve tension (live curve preview).
  - Right-click point: curve type (Hold, Linear, Exp, S-curve), delete.
  - Pencil mode: freehand draw, then thin to a minimal point set with a tolerance-based simplification (Ramer–Douglas–Peucker) on mouse-up.
  - Shape stamps: draw a sine/triangle/saw/square/random LFO shape over a time selection at a chosen rate and depth.
  - Rubber band select points; move/scale selection; Ctrl/Cmd+C/V.
- Value readout tooltip shows the actual output voltage, not just 0–1.
- **Automation recording:** Arm a lane and choose a source: a CV input (the REC inputs on Tessera, or an AUTO REC poly input on Tessera where channel = lane), or **a mapped parameter** (turning the mapped knob by hand while armed records its movement, like touching a fader in a DAW). Modes: Latch, Touch, Write. Captured data is thinned before commit.
- Output smoothing: configurable slew (0–100 ms) per lane to avoid zipper steps on Hold transitions when desired.

---

## 13. Scales and key

- Global key (root + scale) in the transport bar; per-clip override.
- Built-in scales: Major, Natural Minor, Harmonic Minor, Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major/Minor Pentatonic, Blues, Whole Tone, Chromatic, Hungarian Minor, Phrygian Dominant, Japanese (In), Hirajoshi, Double Harmonic, Bebop Dominant.
- Custom scales: 12-key toggle editor, saved per patch.
- Behaviors (each toggleable in the Scale Bar):
  - **Highlight**: tint in-scale rows, mark root.
  - **Snap**: new/moved notes snap to the nearest scale degree; Up/Down moves by scale step.
  - **Fold**: hide non-scale rows entirely (compact view, "scale fold").
  - **Quantize recording**: incoming CV/MIDI quantized to scale.
  - **Transpose diatonically**: when changing the key, offer "move notes to new key" (degree-preserving) vs "keep pitches".
- Scale.cpp exposes: `bool inScale(pitch)`, `int snap(pitch, direction)`, `int stepBy(pitch, steps)`, `int degreeOf(pitch)`, `int pitchOf(degree, octave)`. Unit test them all, including wraparound across octaves.

---

## 14. Recording

- Arm one or more tracks. Sources per track: **REC CV inputs** (poly V/OCT + GATE + VEL) or **MIDI** (device + channel), or "Any".
- When armed and monitoring is on, incoming notes are passed through to the track's outputs (live play-through) with the track's voice allocator.
- Count-in: Off / 1 bar / 2 bars, with click output on CLOCK OUT or a dedicated CLICK output.
- Modes:
  - **Launcher**: record into the selected empty slot; clip length either fixed (1/2/4/8 bars) or set by stopping record (rounded up to the bar).
  - **Arranger**: record onto the timeline, creating a new clip from punch-in to punch-out; overlapping existing clips: **Replace** or **Overdub (merge)**.
  - **Loop overdub** inside a looping clip: each pass merges.
- Input quantize: Off or to grid on record, with strength. Keep raw timing stored so "Undo quantize" is possible.
- CV pitch → MIDI pitch: `round(v * 12) + 60`, with an optional scale quantize. Gate threshold: rising edge > 1V, falling < 0.1V (Schmitt trigger via `dsp::SchmittTrigger`).
- Retrospective capture ("capture MIDI"): always buffer the last 64 bars of incoming notes while stopped or playing un-armed; a "Capture" button turns the buffer into a clip.
- Commit as one undoable `RecordTake` command per recording pass.

---

## 15. UX and visual design

The design must look intentional and premium, not like a default Rack module. All values live in `ui/Theme.hpp`. Never hard-code a color or size in a view.

### Palette (dark theme, default)
| Token | Hex | Use |
|---|---|---|
| bg0 | `#121316` | Editor background |
| bg1 | `#1A1C20` | Panels, headers |
| bg2 | `#23262B` | Raised controls, hover |
| line0 | `#2C3036` | Sub-grid lines |
| line1 | `#3A3F47` | Beat lines |
| line2 | `#525965` | Bar lines |
| text0 | `#E8EAED` | Primary text |
| text1 | `#9AA1AB` | Secondary text |
| text2 | `#646B75` | Disabled, hints |
| accent | `#FF8A3D` | Playhead, focus, primary actions |
| record | `#FF4D5E` | Record states |
| play | `#4BD18A` | Playing states |
| queue | `#F5C451` | Queued states |
| select | `#7AA7FF` | Selection outlines, rubber band (with 15% fill) |

Track colors: a curated 16-color set with even perceived lightness (pick from an OKLCH ring, L≈0.72, C≈0.14). Notes use the track or clip color; selected notes get a 1.5 px `select` outline and slightly raised brightness.

Provide a light theme via the same tokens, following Rack's "Prefer dark panels" setting where sensible.

### Typography
- UI: Inter (11 px default, 12 px headers, 10 px ruler labels). Numbers: JetBrains Mono, tabular.
- Load fonts with `APP->window->loadFont(asset::plugin(...))` and cache handles.

### Metrics and feel
- 4 px base spacing grid. Corner radius 4 px for controls, 3 px for notes and clips.
- Minimum hit targets: 6 px resize edges, 16 px buttons, note bodies clickable even when narrower than 4 px (expand the hit area, not the drawing).
- Hover states on everything interactive. Cursor changes: resize, move, pencil, eraser, slice.
- Tooltips after 500 ms, with the shortcut shown.
- Every drag shows a live value readout near the cursor (position, length, pitch name, velocity, volts).
- Animations: 120 ms ease-out for panels/expand, none for editing feedback (editing must feel instant).
- Selected track/clip is always visually obvious. Focus ring on the active editor pane.
- Empty states are helpful: "Double-click to create a clip", "Arm a track and press R to record".

### Editor window layout
```
┌──────────────────────────────────────────────────────────────┐
│ Transport: ▶ ■ ● ⟳ | 120.00 BPM | 4/4 | C Minor ▾ | Launcher/Arranger │
├────────────┬─────────────────────────────────────────────────┤
│ Track      │  Arranger timeline  or  Clip launcher grid      │
│ headers    │                                                 │
│ (scroll)   │                                                 │
├────────────┴─────────────────────────────────────────────────┤ ← draggable splitter
│ Detail editor: Piano roll / Drum editor  + lower lanes │ Insp │
└──────────────────────────────────────────────────────────────┘
```
- Tab switches focus between top and detail panes; F11/double-click on a splitter maximizes a pane.
- Remember layout (splitter positions, zoom levels, open lanes) in patch JSON under `ui`.

---

## 16. Keyboard shortcuts (editor focused)

**Full keymap: `docs/EDITOR_UX.md` §7.** Shortcuts act only while the editor overlay has focus, are defined in a single action table (`Shortcuts.cpp`), and the "?" help overlay and tooltips are generated from that table. Design the table so user remapping can be added in v1.1.

---

## 17. Persistence and MIDI export

- `dataToJson`: `{ "schema": 1, "song": {...}, "ui": {...} }`.
- Notes stored compactly as arrays (`[start, length, pitch, vel, relVel, micro, flags]`) to keep patches small; operators and expression curves are written only when they differ from defaults (as an optional trailing object).
- `dataFromJson` must tolerate missing fields (defaults) and older schemas (`migrate(json, fromVersion)`).
- Round-trip test: random song → JSON → song must be deep-equal.
- Module presets (Rack's preset system) save/restore the whole song.
- Add clip import/export as `.tessera-clip` JSON via context menu.
- Project files (`.tessera`) and DAWproject export from the editor's File menu (docs/DECISIONS.md §39).

### MIDI export

Export to Standard MIDI Files (`.mid`) so work made in Rack can move into any DAW.

**What can be exported** (File menu in the editor, and right-click menus):
- **Arrangement**: the whole timeline, or the loop region, or the current range selection.
- **Tracks**: selected tracks only (arrangement or range).
- **Clips**: a single clip, or the selected clips (one file each, or one multi-track file). Available from the clip's right-click menu in the launcher and arranger.
- **Launcher performance**: whatever "Record arrangement from launcher" captured is just arrangement, so it exports like any arrangement.

**File structure**
- SMF **format 1**, 960 PPQ (same as the model, so no rounding of positions).
- Track 0 (conductor): song name, tempo map (including scene tempo changes captured into the arrangement), time signature, markers as Marker meta events, loop region as cue points.
- One MIDI track per Tessera track, named after the track, in display order. Track groups are flattened.
- Channel per track: 1–16 assigned in order by default, user-overridable per track in the export dialog. Drum tracks default to channel 10, each lane's notes on its lane pitch.
- Note-on velocity from velocity; note-off velocity from release velocity.
- Clip instance transpose, velocity scale, stretch, offsets, trims and loop repeats are applied — the file contains what plays, laid out on the timeline.
- Muted tracks, muted clips and muted notes: excluded by default, option to include.

**Export options** (export dialog, remembered per patch)

| Option | Choices | Notes |
|---|---|---|
| Timing | **As written** / As played | As played applies swing, micro-timing and timing spread |
| Note operators | **As written** / Rendered | As written ignores chance, repeats, occurrence, recurrence and spreads (no MIDI equivalent). Rendered bakes them by running the same `NoteOperators` code the engine uses, with the current seed, pass by pass — so loops of a clip with recurrence export as their actual variations |
| Automation | **Off** / As CC / As CC + 14-bit | Each lane maps to a CC number chosen per lane (default CC 1, 2, 11, 16–19…, listed in the dialog). Curves are sampled at a chosen resolution (default every 1/64, plus at every point) and then thinned, so files stay small. Values map lane range → 0–127 (or 0–16383 for 14-bit MSB/LSB pairs) |
| Expressions | **Off** / MPE / Flatten | MPE: notes are spread across member channels (zone of up to 15) with per-note pitch bend (pitch expression, bend range written as RPN, default ±48), CC74 (timbre) and channel pressure (pressure). Flatten: per-note pitch drops; timbre → CC74 and pressure → polyphonic aftertouch on the track's channel |
| Range | Whole song / Loop / Selection | |
| Split | One file / One file per track / One file per clip | |

**Implementation**
- `model/Render.cpp` turns `(Song, ExportOptions)` into a sorted list of timed events. It is pure and fully testable; it is also the right place for future features like freezing or bouncing.
- `model/MidiFile.cpp` writes SMF bytes (variable-length quantities, meta events, running status optional) with no external dependency. Include a minimal SMF reader used only by tests.
- File dialog via Rack's bundled `osdialog` (`osdialog_file(OSDIALOG_SAVE, …)`), called on the UI thread. Writing happens on the UI thread too; never from `process()`.
- Large songs: rendering and writing must take < 200 ms for a 64-track, 200-bar song; show a brief "Exported 12 tracks to song.mid" toast with the path.
- Export uses the current UI-thread `Song`, never the engine snapshot.

---

## 18. Performance budgets

- `process()` cost: < 1% of one core at 48 kHz with 16 busy tracks, < 3% with 64. Each output module's `process()` is a plain copy of one `TrackFrame` and must stay trivial. Profile both.
- Editor redraw: 60 fps with 10,000 notes in a clip and 500 arranger instances.
  - Cull to the visible rect before drawing.
  - Cache static layers (grid, clip previews, keyboard) in `FramebufferWidget`s; redraw them only on zoom/scroll/edit (`setDirty()`).
  - Playhead and active-note highlights live on a separate lightweight top layer.
- Use spatial lookup for hit-testing notes (notes sorted by start + binary search on visible window; then pitch filter).
- No per-frame heap allocations in draw code paths; reuse vectors.

---

## 19. Testing

- `tests/model_tests.cpp` (doctest, single header, no Rack): MIDI export (write → read back with the test reader; check note timing, velocities, release velocities, tempo map, CC sampling and thinning, MPE channel allocation, rendered operators match the engine for the same seed, clip instance transforms applied), every transform in `Transforms.cpp`, note-operator determinism per seed (chance, repeats timing/velocity, occurrence, recurrence), `TimeAxis`/`PitchAxis` zoom anchoring, scales, quantize, swing math, automation evaluation, every Command's apply/revert symmetry (apply then revert must equal original), serialization round-trip, voice allocator stealing and retrigger, loop window splitting, external clock lock.
- Add a `make test` target that builds and runs these.
- Engine test harness: drive `Engine` with a fake sample loop and assert gate edges land on exact sample indices.
- Output bus tests: simulate Tessera and several readers processed in shuffled order across threads; every reader must output exactly the frame for sample n−1, all tracks aligned. Also test: Tessera removed mid-stream (readers go silent, gates low, no crash), track deleted then undone (binding restored), two Tesseras in one patch with readers bound to each.
- Manual QA checklist in `docs/QA.md` (create it): hanging notes on stop/loop/mute, undo after every operation, patch reload fidelity (including output bindings and param mappings), copy/paste of Tessera + output modules together, two Tesseras in one patch, 64 tracks, external clock jitter, 16-voice chords, drum choke groups, recording from MIDI and CV.

---

## 20. Build order (milestones)

Do these in order. Each milestone must build, pass tests, and be usable before moving on. Commit at the end of each.

1. **Skeleton**: plugin builds, Tessera panel, internal clock, transport, one hard-coded clip on one track. **Registry + OutputBus + Tessera Track Out** with poly V/OCT/GATE/VEL, binding via context menu. Output routing is foundational, so it is built first.
2. **Model + commands + undo + persistence** with tests. Snapshot publishing to engine. Output module bindings saved and restored, including ID remapping on paste/duplicate.
3. **Editor overlay shell**: transport bar, track headers, splitters, theme, fonts, and EDITOR_UX §10 step 1 (axes, navigation, gesture engine).
4. **Piano roll v1**: EDITOR_UX §10 steps 2–3.
5. **Dynamic tracks**: add/delete/duplicate/reorder/group (all undoable), voice allocation, mute/solo, output status dots, scrolling headers.
6. **Clip launcher**: slots, launch quantize, scenes, panel scene controls.
7. **Arranger**: EDITOR_UX §10 step 4.
8. **Drum tracks + Tessera Drum Out**: lanes, step grid, per-lane outputs, choke, polymeter, Euclidean.
9. **Scales**: highlight, snap, fold, chord stamp.
10. **Recording**: CV + MIDI, launcher/arranger modes, count-in, overdub, capture buffer.
11. **Automation**: lanes, curves, CV output, **parameter mapping with learn**, recording (including from mapped knobs), and EDITOR_UX §10 step 7.
12. **Piano roll v2**: EDITOR_UX §10 steps 5, 6 and 8 (inspector, note operators, transforms, step input, multi-clip editing, expression curves).
13. **MIDI export**: Render, SMF writer, export dialog, all options in §17, tests.
14. **EDITOR_UX §10 step 9 (polish, help overlay), external clock, multi-instance polish ("Follow selected track", instance names), polish pass, performance pass with 64 tracks, help overlay.**

---

## 21. Coding conventions

- `clang-format` (LLVM base, 4-space indent, 120 cols). Run before committing.
- Names: `PascalCase` types, `camelCase` functions/vars, `kConstant`. Member fields without prefixes.
- Prefer value types and `std::vector`; IDs over raw pointers across the model.
- No exceptions or RTTI in engine code. `assert` liberally in model code.
- Every new feature: command(s) + tests + shortcut (if applicable) + tooltip + entry in the help overlay + user manual (§23).

## 22. Things to never do

- Never lock a mutex in `process()`.
- Never let a widget hold a pointer into `Song` across frames (IDs only; data can move).
- Never mutate the model outside a Command.
- Never draw the whole grid every frame without caching.
- Never steal keyboard input when the editor isn't focused.
- Never leave gates high after stop, loop wrap, track mute, or snapshot swap.
- Never touch the registry, take a lock, or resolve a binding inside any module's `process()`. Bindings resolve on the UI thread; `process()` reads one atomic pointer.
- Never let an output module read a frame Tessera is still writing. Always read the previous sample's frame.
- Never silently rebind an output module to a different track or instance when its target disappears.
- Never ship a feature without undo.

## 23. User manual

[`docs/manual/`](manual/README.md) is the user manual: plain-language chapters with screenshots, for musicians.
It describes what Tessera does today. **Every behaviour change updates it in the same commit**: a new feature, a
changed gesture, key, label, menu or default, a removed feature, a moved control. A change is done when every
passage and screenshot that shows the behaviour matches the new one. How to find them, retake screenshots and redraw
the numbered pictures: [`docs/manual/tools/README.md`](manual/tools/README.md).

---

When unsure about UX, ask: "What is fastest to edit, what is clearest to read, and what feels most polished?" — then pick the one that serves editing speed first.
