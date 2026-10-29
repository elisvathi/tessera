# PLAYHEADS.md — Several playheads reading one clip (design, for review)

Status: design proposed, not agreed or built. Branch `feature/multi-playhead`.
Mockups: claude.ai canvas "Tessera Playheads proposals" (boards A–E and the final board "F · Final").

## 0. The idea in one paragraph

A track's clip is read by several **playheads** at once. Each playhead moves through the clip in its own way:
slower, faster, backwards, bouncing, repeating one beat a few times, or along a path you draw. What each playhead
plays goes through its own short chain of devices (an Echo on one, a Transpose on another), then all of them
meet in the track's usual chain. Each playhead plays on a **channel**, and the track's Track Out has a **channel
picker**: All, or one channel. So one Track Out can play everything, or only the playheads on its channel. A short
phrase becomes a canon, a cloud, or a slowly shifting texture, without copying a single note.

---

## 1. The shape: one choice per question

The five mockups answered three separate questions. This design takes one answer for each:

| Question | Answer | From |
|---|---|---|
| Where do playheads live? | In a **Playheads device**, first in the track's chain | A |
| What happens to their notes? | Each playhead has its own **branch** of devices; branches merge into the track's chain | B |
| How do you edit them? | **Rows** in the device, **markers** on the ruler, and a drawn **time map** in the lower pane | A, C, D |

Why the device chain is the home (the one choice that is hard to change later):
- It reuses what exists: the device strip, knobs, modulators, automation lanes, the device browser, bypass, and
  MIDI export through the chain.
- Clip-by-clip differences come for free: a clip's automation lane on a playhead knob changes it for that clip
  only (the knob lanes from DEVICES.md §1.3). That was the main reason to put playheads in the clip (option C).
- It grows without changing the save format again: a branch, a drawn path and a channel are all fields of one
  playhead.

"Follower tracks" (option E) are not built: a playhead's channel (§2.2) gives each one its own sound without a
new kind of track, and its branch gives it its own devices.

---

## 2. Setting it up (the easy path)

The goal: from "I have a melody" to "three playheads, two sounds" in under a minute, without reading the manual.

### 2.1 One sound: nothing to patch

1. On a track that already plays through a Track Out, open **Devices** and pick **Playheads** (first in the
   browser, group "Source"). The track's right-click menu has the same item: **Add playheads**.
2. The device arrives with **two playheads**: "1" plays the clip as before; "2" starts one bar later, so you hear
   a canon at once. **+ Add playhead** adds more, each starting a beat after the last.
3. Every playhead plays through the track's Track Out, on the same poly cable. Nothing to patch.

### 2.2 Its own sound: channels

Every playhead plays on a **channel** (1–16), shown as a chip in its row in the playhead's colour. New playheads
take the next channel: Lead on 1, Half back on 2, Bells on 3. Two playheads can share a channel, so they share a
sound.

The **Track Out** gets a **channel picker**: **All** (the default, so nothing changes for existing patches) or one
channel. It sits on the module's display, under the track name: click it, or scroll on it, to step through
All, 1, 2…; its right-click menu has the same list. The display names who plays there: "Ch 2 · ● Half back".

To give Half back its own sound:
1. Add a second Track Out for the same track (as today: module browser, pick the track).
2. Set its picker to **Ch 2**. Set the first Track Out's picker to **Ch 1** if Half back should leave it.
3. Patch it to another voice.

One Track Out per sound, each picking its channel. The playheads never point at modules, so moving, deleting or
copying modules never breaks a playhead, and a playhead never needs to know where it is heard.

Clear states, never silence by surprise:
- A playhead whose channel no Track Out plays (none on All, none on its channel) shows its channel chip crossed out,
  with the tooltip "Nothing plays channel 2. Set a Track Out to Ch 2 or All."
- A Track Out on a channel nobody uses says "Ch 3 · nothing plays". It keeps the channel; it never changes it
  by itself.
- The inspector's Output line lists them: "Track Out · All" or "Track Out · Ch 1 · Track Out · Ch 2".

**It works without playheads too.** Notes already carry a channel (a clip can hold several parts, DEVICES.md §2).
With no Playheads device, a note plays on its own channel, so a clip with a bass part on 1 and chords on 2 can
now go to two sounds as well. The playhead's channel setting has **As written** for the same reason: it keeps each
note's own channel.

### 2.3 Drum tracks

Drum Out gets the same picker. A Drum Out on Ch 2 sends only what channel 2's playheads trigger, lane by lane. So
the reversed half-speed hats can go to a different voice than the straight ones.

### 2.4 MIDI export

MIDI export writes each note on its channel. With playheads, playhead channels become MIDI channels, so in another
program each playhead (or each group sharing a channel) can play its own instrument.

---

## 3. What the user sees

### 3.1 The Playheads card in the device strip

The device strip (DEVICES.md §1.1) shows the Playheads device as a wide card, always first, before "in". It has
one row per playhead. To the right of each row sits that playhead's branch: small device cards, in the order notes
flow. The branches meet at a merge point, then the track's chain goes on as now.

```
┌⏻ Playheads · 3                                 ⋯┐
│● Lead       1×  →   1.1   Written  [Ch 1]        │ → [Velocity]             ─┐
│● Half back  ½×  ←   2.1   Follows  [Ch 2]        │ → [Transpose −12] [Echo] ─┼─ merge → [Scale] → out
│● Bells      2×  ⇄   1.3   1/16     [Ch 1]        │ → [Transpose +12]        ─┘
│+ Add playhead                                    │
└──────────────────────────────────────────────────┘
```

The chip at the end of each row is the playhead's channel (§2.2). Click it for a list of 1–16 and As written;
each entry says which Track Outs play it ("Ch 2 · Track Out 2").

- **A row** shows the settings you change most: colour, name, rate, direction, start, note length. A click on the
  row selects the playhead and opens its **settings popover** (all settings in §4, as knobs and choices), like a
  modulator's popover.
- **Each setting is a knob or a choice** with the usual gestures: drag, double-click to reset, right-click to
  modulate or automate.
- **A row's ⋯ menu:** rename, colour, duplicate, delete, mute, solo, "Channel ›", "Edit path in time map".
- **Branches:** the faint **+** at the end of a branch opens the device browser; the new device goes on that
  branch. Drag a card between branches or onto the shared chain. A branch holds up to 4 devices.
- **The Playheads card can't be moved** out of first place, and only one is allowed per track. The device
  browser shows it in its own group "Source" with the line "Read the clip with several playheads".
- **Bypass** (the power button) plays the clip as if there were no Playheads device. A muted playhead is dimmed.
- **Narrow panes** (the in-panel editor): rows show colour, name and rate only; branches fold into a count
  ("2 devices"); a click opens the popover.

### 3.2 In the piano roll and drum editor

- Each playhead is drawn as a line in its colour, moving through the notes. The clip's own playhead is hidden
  while a Playheads device is on.
- **Start markers** sit on the ruler: a small triangle per playhead in its colour. Drag one to change that
  playhead's start (one undo step, snapped to the grid). The selected playhead's **region** shows as a faint band
  on the ruler and in the notes; drag its edges to change it.
- A note lights up briefly in the colour of the playhead that played it, so you can follow who plays what.
- The launcher and timeline clip previews show the lines too, and the clip chip shows "×3" on tracks with
  playheads.

### 3.3 The time map (lower pane)

The piano roll's lower pane (where Velocity and Pitch show today) gets a new choice: **Time map**. One lane per
playhead. Across: song time. Up: where in the clip the playhead reads.

- A playhead's path is drawn from its settings: a rising line is normal; steeper is faster; falling is reverse;
  flat is freeze; a staircase is a repeat; a curve is bend. Changing a setting redraws the path at once.
- **Draw:** right-click a lane → "Draw this path". The playhead switches to **Drawn** motion (§4.2) and its
  path becomes points you move, add and delete, with curves between them, using the automation lane editor.
  "Back to settings" in the same menu returns to the knobs (the drawn path is kept until you clear it).
- A vertical line shows "now".

### 3.4 Keys, help and manual

- New actions go in the one action table (`src/ui/interaction/Shortcuts.cpp`), so tooltips and the help overlay
  list them: "Add playhead", "Next / previous playhead", "Mute playhead", "Show the time map". Keys are picked
  when building, from the free ones in the table.
- The manual gets a "Playheads" page: what they are, a 3-voice canon in five steps, the settings, the time map,
  and giving a playhead its own sound. QA gets the manual test steps (§8).

---

## 4. A playhead's settings

All settings are knobs or choices in the playhead parameter table: append-only, normalized, like a device's.

### 4.1 Motion: Settings (default)

| Setting | Range | Default | What it does |
|---|---|---|---|
| Rate | 1/16× … 16×, with dotted and triplet steps | 1× | speed against the song |
| Direction | Forward, Reverse, Ping-pong, Freeze | Forward | ping-pong turns at the region's edges |
| Start | any grid position in the clip | 1.1 | where the playhead is when the clip starts |
| Region | start and end in the clip | the clip's loop | what it loops over |
| Repeat segment | Off, 1/16 … 1 bar | Off | the size of the part that repeats |
| Repeat times | 1–16 | 2 | play each segment this many times, then move on one segment: 1, 1, 2, 2, 3, 3… |
| Bend shape | Straight, Ease in, Ease out, Ease in-out, Swing, Bounce, Elastic, Stairs | Straight | speeds up and slows down inside each bend cycle, without moving the cycle's ends |
| Bend amount | 0–100 % | 0 | how strong the shape is |
| Bend cycle | 1/16 … 4 bars | 1 beat | the length of one bend cycle |
| Offset | ± 1 bar | 0 | moves the playhead's position; made to be automated |

### 4.2 Motion: Drawn

The path drawn in the time map (§3.3): points over a **cycle** (1–16 bars of song time), each giving a position in
the clip, with curves between them. The path repeats every cycle. Offset still applies; rate, direction, repeat
and bend are greyed out.

### 4.3 Notes

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Length | As written, Follows playhead, Fixed | As written | §5.3 |
| Fixed length | 1/64 … 1 bar | 1/16 | for Fixed |

Pitch, velocity and the rest are **not** playhead settings. They are devices in the playhead's branch
(Transpose, Velocity, Random…). One concept, one control.

### 4.4 Output

| Setting | Choices | Default |
|---|---|---|
| Channel | As written, 1–16 | the next free channel (1, 2, 3…) |
| Mute, Solo | on / off | off |

The channel is put on the playhead's notes when it plays them, before its branch. Devices can read it (Note Filter
already filters by channel), and the Track Out picker (§2.2) decides which module they come out of.

The channel is a setting, not a knob: it can't be modulated or automated (switching channel mid-note would move a
playing note to another cable).

Playing on another *track* was considered and dropped: a channel already gives a playhead its own sound, its
branch gives it its own devices, and routing between tracks would add a second way to do the same thing.

### 4.5 Device settings (the whole Playheads device)

| Setting | Choices | Default | What it does |
|---|---|---|---|
| Restart | On clip start, Every N bars, Never | On clip start | when every playhead goes back to its start |
| Restart bars | 1–64 | 4 | for "Every N bars" |
| Same note | Play each, Play once | Play once | when two playheads on this track play the same pitch at the same moment |

### 4.6 Limits

One Playheads device per track. 16 playheads per device. 4 devices per branch. Branch devices count toward the
track's 16 devices.

---

## 5. Rules (how it plays)

### 5.1 Position is worked out from time

A playhead's place in the clip is a **pure function** of the time since its last restart and its settings. Rate,
direction, region, repeat, bend, offset and drawn paths all work this way. That keeps MIDI export identical to
playback, puts every playhead in the right place after a seek, and stops edits from making playheads drift.

The one exception is a **modulated or automated rate**. The position is then the sum of the speed over time. The
engine adds it up once per 32-sample block (when it updates modulators); export does the same with the same step;
a seek adds it up again from the last restart (a few thousand steps at most, once, on the UI thread).

### 5.2 When a note plays

A note plays when a playhead **crosses into it**:
- moving forward: at the note's start;
- moving backward: at the note's **end** (the playhead enters it from the right). The note still sounds normally;
  only the order of the notes is reversed;
- frozen: nothing new plays.

A playhead that jumps (restart, seek, repeat step, offset change, drawn jump) into the middle of a note doesn't start
it. There's no note chasing, the same as today. Note operators (chance, repeats…) run per playhead, with the
playhead's ID mixed into the seed, so two playheads don't make identical choices.

### 5.3 How long a note lasts

- **As written:** the note's own length, in song time.
- **Follows playhead:** the note lasts while the playhead is inside it. Half speed doubles it, and reverse ends it
  when the playhead leaves the note's start.
- **Fixed:** the set length.

A note never lasts longer than 4 bars, so a frozen playhead can't hang a note. Stop, loop wrap, mute, bypass,
deleting a playhead and snapshot swaps end every note, as now.

### 5.4 Drum tracks and lane lengths

A playhead reads every lane. A lane with its own length (polymeter) is read at the playhead's position wrapped
to that lane's length, so the lane keeps its own cycle under every playhead. The region's default is the clip's
whole lane cycle (decision 83).

### 5.5 Voices

Each channel in use gets its own **group of voices**, sized like a track's automatic voice count for what plays on
it. The track's 16 voices are shared out in channel order. A Track Out on All sends every group; a Track Out on a
channel sends only that channel's group, starting at its poly channel 1. The groups only change when playheads or
channels are added, removed or changed, so a voice never jumps to another cable while it plays. When the groups
need more than 16 voices, the highest channels get fewer, and their chips show "2 of 4 voices" as a warning. The
track's fixed voice count (when set) is the total.

Notes made by devices after the merge (an Echo or Chord in the shared chain) keep the channel of the note they came
from, so they come out of the same Track Out.

### 5.6 Launcher, timeline, recording

- Launcher: a launched clip restarts the playheads (with Restart "On clip start").
- Timeline: each timeline clip restarts them at its start.
- Recording records notes **as played**, before the Playheads device, the same rule as devices. Live notes pass the
  Playheads device untouched and go on through the shared chain.
- **Bake** (later, with the device chain's Bake): render what the playheads play into new clips.

---

## 6. Model (`src/model`)

```cpp
enum class DeviceType : uint8_t { /* existing… */ Playheads };   // appended
enum class PlayheadMotion : uint8_t { Settings, Drawn };

/// One reader of the clip inside a Playheads device (docs/design/PLAYHEADS.md).
struct Playhead {
    PlayheadId id;
    std::string name;
    uint8_t color = 0;              // index into the playhead colours in Theme
    std::vector<float> params;      // normalized, the playhead parameter table (§4)
    std::vector<AutoPoint> path;      // Drawn motion: points over the cycle
    std::vector<Device> branch;     // up to 4; never a Playheads device
    uint8_t channel = 1;            // 1..16, or 0 = as written
    bool mute = false, solo = false;
};

// Device: + std::vector<Playhead> playheads;   (Playheads devices only)
// ParamRef::Kind: + Playhead                    (owner = PlayheadId; modulators and lanes turn its settings)
```

Track Out and Drum Out save one more field next to their track: the **channel** they play (0 = All, the default
for old patches). It is module data in the patch, like the track binding, and pasted modules keep it. Because it
is a number, not a playhead's ID, no edit in the song can leave it pointing at something deleted.

- `model/PlayheadTypes`: the parameter table (§4) and the device's own table (§4.5).
- `model/Playheads`: the pure position function and "which notes does this playhead cross between times a and b,
  in which direction". Used by the engine, MIDI export and the editor's drawing. Heavily unit tested.
- Device lookup (`DeviceLookup`) learns to find devices inside branches, so knobs, routes and lanes on branch
  devices work like any other.
- Commands, each with apply/revert tests: add, remove, duplicate, reorder playhead; set setting (one undo per drag);
  set channel; mute/solo; set the drawn path (one undo per drag); switch motion; add, remove and move a device
  in a branch (including to and from the shared chain).
- Schema 6: `migrate` adds nothing (no Playheads devices yet); `normalize` drops a second Playheads device, moves it
  first, and drops a Playheads device found inside a branch.
- Duplicating a track or device gives every playhead and branch device new IDs, and fixes routes and lanes
  inside the copy to point at the copy.

## 7. Engine (`src/engine`)

```
             ┌▶ playhead 1 (ch 1) ─▶ branch 1 ─┐                      ┌ ch 1 voices ┐─▶ Track Out · Ch 1
clip notes ──┼▶ playhead 2 (ch 2) ─▶ branch 2 ─┼─▶ shared chain ─▶ voices ┤             │
             └▶ playhead 3 (ch 1) ─▶ branch 3 ─┘   (notes keep         └ ch 2 voices ┘─▶ Track Out · Ch 2
                                                    their channel)       (a Track Out on All gets both)
```

- `ClipPlayback` stays as it is. `TrackState` gets, per playhead slot (16, fixed arrays): its clock, its last
  position, a cursor into the clip's notes in each direction, and its branch's device states. Nothing allocates.
- Per sample the engine asks each playhead for its window [from, to) in clip time (it may run backwards, or not
  move), and fires the notes it crosses (§5.2) into that playhead's branch. The branch's output enters the
  shared chain's message queue, each note carrying its channel.
- **Output bus:** the track's frame stays one frame of 16 voices. The snapshot carries where each channel's group
  starts and how many voices it has. An output module still reads one atomic pointer and copies one frame: all of
  it (All), or its channel's slice. Its poly channel count is the slice's size.
- Playhead settings live in the knob table like device knobs, so turning one never rebuilds the snapshot.
  Adding or removing playheads or branch devices rebuilds that track's snapshot, as device edits do.
- Quiet tracks still sleep: a track's next wake-up is the soonest note any playhead reaches.
- Budget: 16 playheads × 64 tracks is the worst case; each playhead does a little maths per sample and a cursor
  step per note. Measured against DESIGN.md §18 in step 2.
- MIDI export with devices applied runs the same code through `ChainRender`, with one MIDI channel per playhead
  when that option is on (§2.4).

## 8. Manual tests (QA.md, added when built)

Canon in three playheads; reverse at ½× with Follows playhead; repeat 1 beat × 3; ping-pong at the region's edges;
freeze then stop (no hanging gate); two Track Outs on Ch 1 and Ch 2, each to its own voice; change a playhead's
channel while it plays (no hanging gate); a channel no Track Out plays (crossed-out chip); an old patch's Track Out
plays All; a clip with two parts on two channels, no playheads; add playheads until the voices run out; LFO on
rate, then seek; drum
clip with a 3-step lane under a reverse playhead; drawn path; export with devices applied and compare by ear;
in-panel editor with the divider moved.

## 9. What we decided, and what is still open

Decided in this design (can be revisited before building):
- Reverse plays a note when the playhead enters it from its end (§5.2).
- Note length defaults to As written (§5.3).
- Repeat means "each segment N times, then move on one segment" (§4.1).
- Bend uses eight built-in shapes; the free-form option is the drawn path (§4.2).
- No per-playhead transpose, velocity or chance: those are branch devices (§4.3).

Still open:
1. **Per-note motion** (notes whose pitch, time or velocity drift each pass, as notes can in the app this was
   inspired by). It changes what plays from pass to pass, which you earlier found had little value for random
   variation. If wanted, it would be a separate design: note operators that move a note by a set amount each pass
   (not at random), shown in the inspector.
2. Should the clip's own playhead show as a faint line while playheads play, for reference?
3. Should "Add a Track Out for channel 2" exist as a one-click action (Tessera places the module, already set),
   or is adding it from the module browser enough?

## 10. Build order

Each step builds, passes tests and can be used before the next.

1. **Model:** Playheads device type, playhead table, commands with undo, save/load (schema 6), normalize rules;
   the pure position and crossing functions. Tests.
2. **Engine:** playheads on this track with Settings motion (rate, direction, start, region, repeat), the three
   length modes, Restart, Same note; no hanging notes; MIDI export. Tests, including the performance budget.
3. **UI 1:** Playheads card with rows and popover, playhead lines and ruler markers in the piano roll and drum
   editor, the browser's Source group. Agreed mockup first.
4. **Channels and branches:** voice groups per channel, the Track Out and Drum Out channel picker (this works for
   clips with several parts even before playheads ship), the channel chip; then devices inside branches (lookup,
   strip layout, drag between branches).
5. **Bend, offset, modulation and automation** of playhead settings, including the summed-rate rule.
6. **Time map** lower pane and Drawn motion.
7. **Finish:** shortcuts, help overlay, tooltips, manual page and screenshots, QA steps, a DECISIONS.md entry.
