# DEVICES.md — Device Chains and Modulators (draft for review)

This spec adds a **device chain** to every track: MIDI effects that change the notes on their way from the clips
to the track's outputs. It also adds **modulators** (LFOs, envelopes, steps…) that turn device knobs, and lets
**automation** turn the same knobs. Modulators live at three levels: the **song** (global, for every track),
the **track**, and the **device**.

Status: design agreed, not built yet. Decisions taken: all nine devices in v1; modulators can also send CV out;
modulators are monophonic; recording keeps the notes as played (baking a chain's result onto another track is
a later feature).

---

## 0. Principles

1. **Same rules as the rest of Tessera.** Every edit is a command with undo. The audio thread never allocates,
   locks or touches widgets. The device and modulator code is plain C++ in `src/model/`, unit tested without
   Rack, and shared by the engine and MIDI export, so a rendered file plays exactly what you hear.
2. **A simple device-and-modulator model.** Devices sit in a row; each knob shows its base value plus a ring for each
   modulation on it. Modulators are dragged (or "mapped") onto knobs. Automation moves the base value;
   modulation adds on top of it.
3. **Notes keep their length.** Tessera always knows how long a note lasts (it plays clips, not a keyboard), so
   devices work on whole notes — start, length, pitch, velocity, release, expression curves — not on separate
   note-on and note-off messages. That makes the arpeggiator, echo and chord devices simple and exact.
   Live-played notes (MIDI, REC CV, keyboard play) enter as "open" notes whose length is known when the key
   is released (§3.4).
4. **Repeatable.** Anything random (random pitch, random modulator) uses the song seed, the track and the pass,
   like the note operators, so a loop plays the same each time it is exported and a "Reseed" gives a new one.

---

## 1. What the user sees

### 1.1 The device strip

Agreed with the user on a design canvas (claude.ai artifact "Tessera Devices view"). The devices sit in a strip of
fixed height (about 128 px) **above the piano roll** (and above the drum step grid), so notes and what shapes them
are seen together and the pane never shows empty space. The **Devices** button in the detail pane's header opens
and closes it (F3). Closed, a track with devices shows a 24 px bar with its chain: "Chord → Arpeggiator →
Velocity · 3 modulators"; clicking it opens the strip. The in-panel editor starts with the strip closed.

```
[‹][›] [Devices 3]  ■ Track 1 · Demo                                           (piano roll tools…)
┌ MODULATORS   + Add ┐    ┌⏻ Chord      ⋯┐   ┌⏻ Arpeggiator •  ⋯┐   ┌⏻ Velocity   ⋯┐   ┌ + ┐
│● Build-up  song ~ ↗│ in │ Min7▾ ⏽ ◔ Open▾│ + │Up-dn▾ 1/16▾ ◔ ◔ ◔ │ + │ ◔  ◔  ◔  ◔  │ + │Add│ out
│● LFO 1    track ~ ↗│ →  │ Chord In Inv …│   │Pattern Rate Gate …│   │Curve Min Max │   │dev│ →
│● LFO 2     Arp  ~ ↗│    └───────────────┘   └───────────────────┘   └──────────────┘   └───┘
└────────────────────┘
(piano roll below)
```

- **Device cards** sit in one row, left to right in the order notes flow ("in" … "out"). A card has a header
  (power button for bypass, name, a coloured dot per modulator of its own, ⋯ menu) and its knobs in a single
  line. Choices (pattern, rate) are small dropdowns and on/off settings are switches, so nothing pretends to be
  a knob. The strip scrolls sideways when the chain is wider than the pane.
- **⋯ menu:** rename, duplicate, delete, reset knobs, "affect drum lanes" (pitch devices on drum tracks), add a
  modulator to this device.
- **+ between cards** (faint until hovered) and the **Add device** tile at the end open the **device browser**:
  a searchable list. Typing filters by name and by purpose ("delay" finds Echo, "loose" finds Humanize); ↑ ↓
  choose, Enter adds, Esc closes; each entry has a one-line description; groups Pitch (Transpose, Scale, Chord),
  Rhythm (Arpeggiator, Echo, Note Length), Feel (Velocity, Humanize, Random, Note Filter); the footer says where the device
  will go. Drag a card by its header to reorder; Ctrl/Cmd+drag copies; Delete removes the selected card.
- **Knobs:** drag up/down (Shift fine), double-click resets, click the value to type it, right-click for a menu
  (reset, type a value, modulate with ▸, remove modulation ▸, and automation once it is built). The knob draws
  an orange arc for its value (what you or automation set), a ring per modulation in the modulator's colour
  showing how far it can push the knob, and a white dot for where it is right now. Hovering a modulated knob
  names its modulations ("LFO 1 · +35%").
- **Modulators** are one list at the left of the strip, whatever their level. Each row: colour, name, where it
  lives ("song", "track", or its device's name), a live mini graph, and the ↗ map arrow. "+ Add" asks for the
  level (the whole song, this track, or one of its devices) and the type. Clicking a row (or a device's dot)
  opens the modulator's **popover** over the notes: its settings, the Steps editor for Steps, and its "Moves"
  list (target, amount, ×), with "Map to knobs" and "CV out ▾" (an AUTO output, or a Rack knob by learn).
  Right-click a row: rename, remove.
- **Mapping:** click a modulator's ↗; the pane header shows "Mapping LFO 1 · drag a lit knob up
  or down" in its colour, the knobs it may reach light up and the others dim (a device modulator lights only its
  own device). Drag a lit knob to set the amount (up positive, down negative); a click adds +50%. Esc or a click
  on empty strip space ends mapping. A knob takes up to 4 modulations.

Elsewhere:
- The inspector stays about notes.

As built (step 3):
- The strip is `ui/views/DeviceStrip`, placed by the detail pane between the editor's header and its notes (the
  editors leave room for it through `EditorPane::topInset`). The modulator popover is `ui/views/ModulatorPopover`,
  the searchable list is `ui/widgets/SearchList` (matching in `ui/interaction/Search`, unit tested), and the menus
  are `ui/editing/DeviceActions`. The device tables carry each type's group, one-line description and search words.
- While mapping, the message floats in a pill just under the strip (the header has no room left on narrow panes).
- An LFO's popover shows the one rate that applies (the synced length while Sync is on, else hertz), named "Rate".
- Delete and Ctrl+D act on the selected card only right after a click in the strip; otherwise they edit notes.
- Each modulator's colour comes from `theme::modulatorColor` by its ID, so it never changes.
- A modulator can learn a Rack knob ("CV out" › "A knob in the rack"), with the same take-over question as lanes.

As built (step 4):
- All nine devices and all seven modulators work. Chord builds chords the way the piano roll's chord stamp does
  (same shapes, in the key or not), allocation free. The arpeggiator steps on the song's grid while playing and on
  its own clock while stopped; Hold latches until new notes come. Echo makes its repeats one at a time, so long
  echoes never fill the chain. Humanize only ever delays notes, and never moves a held key.
- A track's automatic voice count follows its devices: a chord multiplies the voices, echoes and Note Length can
  overlap notes, an arpeggiator plays one step at a time (all at once in its Chord pattern). A track with a fixed
  voice count keeps it; a chord on a 1-voice track plays one note of each chord.

### 1.2 The three levels of modulators

| Level | Where it lives | What it can modulate | Typical use |
|---|---|---|---|
| **Song** (global) | the song; shown in every track's Devices view | any knob of any device on any track, and any track's or device's modulator settings | one LFO that sweeps several tracks together; a slow "build-up" ramp over 16 bars |
| **Track** | the track | any knob of the track's devices and of their modulators | an envelope on each note of the track driving arp gate and velocity |
| **Device** | inside one device | only that device's knobs (and its own modulators) | an LFO that wobbles one arpeggiator's rate |

A modulator can only map to what its level allows; the lit knobs during mapping show exactly that. Moving a
device to another track takes its device modulators along; track modulations onto it are removed (undoable).

### 1.3 Automation

Any device knob and any modulator setting can be automated, like the AUTO lanes today:

- Right-click a knob → **Add automation lane** creates a lane bound to that knob, on the track (arranger time)
  or in the clip (clip time, loops with the clip; picked in the menu). Its header reads "Arpeggiator · Rate".
- Automation sets the knob's **base** value while it plays; modulation is added on top. A knob that is
  automated draws its base arc in the automation colour.
- Recording works as for mapped Rack knobs: arm the lane, turn the knob while recording (Touch, Latch, Write).
- These lanes don't use an AUTO output channel.

As built:

- The knob's right-click menu has **Automate in the arranger** and **Automate in this clip** (or **Show its lane**
  when it has one). A new lane starts with one point at the knob's value, so adding it changes nothing.
- Knob lanes play **only while the transport runs**; stopped, the knob is free and plays its saved value. A
  knob held by the mouse wins while it is held, then the lane takes it back.
- The engine reads knob lanes at the start of each 32-sample block for the time at the block's end, so a note
  that sits on a lane point already plays with the point's value. Order per block: saved value → lanes → held
  knob → modulation.
- A clip lane overrides a track lane on the same knob, or modulates around it (as AUTO lanes do).
- Lanes on choices, switches and whole numbers hold each point until the next (new points are Hold); readouts,
  the scale and the point menu use the knob's own units.
- Recording reads the knob the mouse holds. In Latch and Write the knob stays where the hand left it until the
  pass ends; a recorded drag doesn't change the knob's saved value.
- Up to 16 knob lanes per track and per clip, apart from the 8 AUTO lanes. MIDI export with devices applied
  follows them; they are never written as CC.

---

## 2. Devices (MIDI effects)

Each device type has a fixed list of parameters (name, range, unit, default, steps or choices). The list is
append-only so saved patches keep working. Values are stored normalized (0..1) and shown in their units.

**First set** (all in v1, built in this order):

| Device | What it does | Main knobs |
|---|---|---|
| **Transpose** | moves pitch | semitones, octaves, key steps (in the clip's key) |
| **Scale** | forces notes into a key | key (song / clip / fixed), mode (nearest / up / down), drop out-of-key notes |
| **Velocity** | shapes velocity | curve (soft…hard), min, max, random ±, fixed value |
| **Note Length** | shapes gate | scale %, fixed length (sync), legato (to next note) |
| **Chord** | adds notes | chord type (or intervals), inversion, spread (close / open / drop-2), in key |
| **Arpeggiator** | plays held notes one at a time | pattern (up, down, up-down, random, as played, chord), rate (sync, triplet, dotted), gate, octaves, steps, swing, velocity (as played / accent pattern), hold |
| **Echo** | repeats notes (MIDI delay) | time (sync), repeats, velocity decay, pitch per repeat, gate |
| **Humanize** | loosens the feel each pass | timing (later only, up to 1/32), velocity ±, length ± |
| **Random** | varies notes each pass | pitch ± (in key), chance to drop |
| **Note Filter** | lets some notes through | pitch range, velocity range, every Nth / skip pattern, channel (Any or 1–16: one part of a clip) |

Later candidates: Strum, Harmonizer (adds a diatonic voice), Note Latch, Ratchet, Pitch-to-drum-lane mapper.

**Order matters:** Chord → Arpeggiator arpeggiates the chord; Arpeggiator → Chord makes a chord of every arp
step.

**Note operators and expressions:** clips' per-note operators (chance, repeats…) run first, as now. Devices
see the notes that operators let through. A note's expression curves travel with it; notes a device creates
(arp steps, echoes, chord notes) take the curves of the note they came from, stretched to their own length.

**Drum tracks:** devices work there too. Pitch-changing devices (Transpose, Scale, Chord, Random pitch) are
skipped on drum tracks by default (a pitch is a lane there); a per-device switch "affect drum lanes" turns them
on.

**Limits:** 16 devices per track, 16 knobs per device.

---

## 3. Modulators

**First set:**

| Modulator | Output | Settings |
|---|---|---|
| **LFO** | -1..1 or 0..1 | shape (sine, triangle, saw up/down, square, S&H, smooth random), rate (Hz, or synced 1/64…16 bars, dotted, triplet), phase, retrigger (free / each bar / each note) |
| **Envelope** | 0..1 | attack, decay, sustain, release; triggered by the track's notes (or the device's input notes for a device modulator) |
| **Steps** | 0..1 | 1–16 steps drawn as bars, rate (synced), smooth |
| **Random** | 0..1 | rate, smooth, seeded per pass |
| **Ramp** | 0..1 | rises (or falls) over N bars, then holds or loops: builds and fades |
| **Macro** | 0..1 | one knob you turn or automate; drives many knobs at once |
| **Note** | 0..1 | follows the last note: velocity, pitch (key tracking), release velocity, timbre, pressure |

- Song-level modulators run on the song position, so every track sees the same LFO phase; they restart with
  the transport.
- Modulators are **monophonic**: one value per modulator, not one per note. (Per-note modulation can come
  later.)
- Modulators update every 32 samples (0.7 ms at 48 kHz), like automation; device knobs follow smoothly.
- **Limits:** 16 song modulators, 8 per track, 4 per device; 8 targets per modulator; 4 modulations per knob.

**CV out:** a modulator's targets can also include
- a track's **AUTO output channel**: the modulator's value, scaled to the channel's voltage range, is added to
  that channel (the track's Track Out, Drum Out and Auto Out send it), and
- a **Rack knob** on any module, mapped with the same learn flow as automation lanes (click Map, touch the
  knob), with its own min/max range. Mapped knobs show the modulator's colour.

So one song LFO can sweep arpeggiator gates in Tessera and a filter module in the rack together. Song
modulators can reach any track's AUTO channels; track and device modulators only their own track's.

---

## 4. Data model (src/model)

```cpp
enum class DeviceType : uint8_t { Transpose, Scale, Velocity, NoteLength, Chord, Arpeggiator, Echo, Random, NoteFilter,
                                  Humanize };
enum class ModulatorType : uint8_t { Lfo, Envelope, Steps, Random, Ramp, Macro, Note };

/// What a modulation or an automation lane turns: a knob of a device, or a setting of a modulator.
struct ParamRef {
    enum class Kind : uint8_t { Device, Modulator } kind;
    uint64_t owner;   // DeviceId or ModulatorId value
    uint16_t param;   // index into the type's parameter list
};

struct Modulation {   // one route
    enum class Kind : uint8_t { Param, AutoChannel, RackKnob } kind;
    ParamRef target;          // Param
    TrackId track; int8_t channel = 0;   // AutoChannel: AUTO 1..8 of a track
    ParamTarget knob;         // RackKnob: module, param, min/max (as automation lanes use)
    float amount;             // -1..1 of the target's full range
};

struct Modulator {
    ModulatorId id;
    ModulatorType type;
    std::string name;
    std::vector<float> params;          // normalized, the type's list
    std::vector<float> steps;           // Steps: the drawn values
    std::vector<Modulation> routes;     // up to 8
};

struct Device {
    DeviceId id;
    DeviceType type;
    std::string name;
    bool on = true;
    bool affectDrumLanes = false;
    std::vector<float> params;          // normalized, the type's list
    std::vector<Modulator> modulators;  // device level, up to 4
};

// Track: + std::vector<Device> devices; std::vector<Modulator> modulators;   (track level)
// Song:  + std::vector<Modulator> modulators;                                (song level)
// AutomationEnvelope: + std::optional<ParamRef> paramTarget;  (a knob lane instead of an AUTO channel)
```

- `model/DeviceTypes.hpp`: the parameter tables (name, range, unit, curve, default, choices) for each type.
- `model/Devices.hpp/.cpp`: the note processing of every device, pure functions on fixed-size buffers.
- `model/Modulators.hpp/.cpp`: modulator evaluation at a song position (pure, seeded).
- Commands: add / remove / move / duplicate device, set knob (one undo per drag), bypass, add / remove /
  edit modulator, add / remove / change modulation, bind an automation lane to a knob. Each tested for
  apply/revert symmetry.
- Schema 5: `migrate` adds empty chains to older patches.
- Validation (`normalize`): a route that points outside its modulator's level, or at something deleted, is
  dropped.
- Commands drop routes the same way: removing a device or modulator, moving a device to another track, or
  deleting a track removes the routes that can no longer reach their target. Undo brings them back.
- Duplicating a device or a track gives the copy new IDs. Routes, AUTO outputs and knob lanes inside the copy
  point at the copy, not the original.
- An automation lane whose knob was deleted stays, and shows "Target missing".

---

## 5. Engine (audio thread)

```
clip playback ─▶ note operators ─▶ device 1 ─▶ device 2 ─▶ … ─▶ voice allocator ─▶ Track Out
                                      ▲            ▲
                     knob value = automation base (or the knob) + Σ modulations, clamped
```

- **The note stream.** Today the engine starts a note straight into the voice allocator. With a chain, notes
  go into a small per-track buffer of whole notes (start in play ticks, length, pitch, velocity, release,
  expression pointer). Each device reads its input buffer and writes its output buffer. Devices that wait
  (arpeggiator, echo, note length legato) keep their pending notes in fixed arrays in the track's state. The
  last buffer feeds the voice allocator. No allocation: buffer sizes are fixed (e.g. 128 notes in flight per
  device, 64 pending), and overflow drops the oldest and counts it for a warning.
- **Timing.** The engine already finds note starts sample by sample; a device that delays or creates a note
  gives it a start tick, and the note is started on the sample that tick falls in, as pending note hits are
  today. Quiet tracks still skip work: a track's next wake-up time includes its devices' next pending note.
- **Knob values** live in a lock-free parameter bank (one float per knob, written by the UI, read by the
  engine), so turning a knob never rebuilds the snapshot. Adding, removing or reordering devices and changing
  routes rebuilds that track's snapshot, as other edits do.
- **Modulation** is evaluated every 32 samples: song modulators once, then each track's and device's. Each
  knob's value = clamp(base + Σ amount × modulator value). Devices read knob values when they process notes,
  so a note is shaped by the knob values at its start.
- **Live input** (MIDI, REC CV, keyboard play) goes through the chain while monitoring, so you hear the
  arpeggiator as you play. Recording records the notes **as played**, before the chain; the chain is applied again
  on playback.
- **Stop / mute / loop / snapshot swap** clear every device's pending notes and release voices, as now (no
  hanging notes).

### 5.1 As built (step 2)

- **Messages, not buffers.** `model/Devices.cpp` runs a chain as one small time-ordered queue of messages
  (128 per track): a note entering a device, or an "end" for an open note. Each device handles a message and
  posts new ones for the next device. A note whose end isn't known yet (a held key, a legato note) is *open*:
  its end comes later as an end message with the note's ID. Voices remember note IDs for this. When the queue
  is full, new notes are dropped and counted; an end is never dropped (it goes straight to the voices).
- **One clock for chains and voices.** Chains and voice ends run on the engine clock (`freeTick`), which also
  runs while the transport is stopped. So live notes go through the chain when stopped too, and a Note Length
  device still ends a held key on time.
- **Knobs.** Each snapshot carries a table of every device knob and modulator setting (normalized). Modulators
  have a stable slot (like bus slots), so an LFO keeps its phase across edits. Every 32 samples, at the start
  of the block, the engine copies the table, adds modulation (using the modulators' outputs from the previous
  block, so modulators that turn each other never loop), then runs the modulators. Devices read the result
  when a note reaches them.
- **Dragging a knob** before the edit is committed: the module holds that one knob at the dragged value
  (`holdKnob` / `releaseKnob`), and the engine plays it at once. The edit itself is one command on mouse-up.
- **Rack knobs:** a modulator's Rack knob routes use the same parameter handles as automation lanes. Full
  amount sweeps the knob's whole min..max range: from the bottom (or the top, for a negative amount), or from
  the middle for a bipolar modulator.
- **Changing devices** (add, remove, reorder, bypass) drops the notes the chain held and ends notes that were
  waiting on it. Turning a knob changes nothing in flight.
- Note Length's legato ends a note when the next one starts (plus up to a 1/16 of overlap), and after 4 bars
  at the latest, so a last note can't hang. Its Overlap knob is therefore 0–100% of a 1/16.

---

## 6. MIDI export and other features

- MIDI export gets a **Devices** option: *applied* (the file has what the chain played; arp steps become
  notes) or *bypassed* (the clips as written). Applied runs the same device code with the song seed, pass by
  pass.
- As built: the export dialog's **Devices** choice (Applied by default). `model/ChainRender` runs each track's
  chain over its exported notes with the same device and modulator code as playback (knobs plus modulation, the
  same seed; modulators step every 16 ticks). Echoes and arpeggios stop at the end of the exported range.
- Automation lanes bound to knobs export as CC only when you choose "knobs as CC" (a CC per lane, picked in the
  export dialog).
- Copy/paste of devices between tracks, and saving a chain or a single device as a preset file, come after v1.
- **Bake (later):** render what a track's chain plays into new clips on another track (a "bounce in place"
  for notes), using the same offline code as MIDI export. Not in v1.

---

## 7. Build order

1. **Model:** types, parameter tables, devices and modulators in Track/Song, commands with undo,
   save/load (schema 5), level validation. Tests.
2. **Engine:** the note stream through the chain, the parameter bank, modulation; devices Transpose, Scale,
   Velocity, Note Length; modulators LFO and Macro. Tests: sample-exact timing, no hanging notes, the same
   seed gives the same result, no allocation in process.
3. **Devices view:** cards, knobs with modulation rings, add / remove / reorder / bypass, the three modulator
   columns, mapping, modulator settings panel. Undo for everything.
4. **More devices and modulators:** Chord, Arpeggiator, Echo, Random, Note Filter; Envelope, Steps, Random,
   Ramp, Note.
5. **Automation of knobs** (lanes, recording), MIDI export through the chain, the panel editor view, help
   overlay entries, docs and QA.

Each step builds, passes tests and is usable before the next.
