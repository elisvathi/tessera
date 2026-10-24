# EDITOR_UX.md — Piano Roll and Arranger Specification

This is the authoritative spec for Tessera's editors. DESIGN.md §8 (Piano roll), §11 (Arranger) and §16 (Shortcuts) defer to this file. Read it in full before working on anything under `src/ui/`.

The bar: someone who knows a modern DAW should sit down and be productive in five minutes without reading anything. Where DAWs disagree, this doc picks one and says why.

---

## 0. Principles

1. **Direct manipulation first.** Anything you can see, you can grab: notes, note edges, velocity stems, automation points, curve segments, clip edges, loop braces, the playhead, the zoom.
2. **Instant feedback, zero lag.** Edits preview on the same frame as the mouse moves. No animation on editing feedback. Commit (one undo step) on mouse-up.
3. **One modifier vocabulary everywhere.** The same modifier means the same thing in the piano roll, drum editor, automation lanes and arranger (§2).
4. **Never lose the user's place.** Zoom anchors at the cursor. Undo restores selection and scroll position. Opening a clip scrolls to its notes, not to C0.
5. **Discoverable, then fast.** Every tool is in a visible toolbar with a tooltip that names its shortcut. Power users never need the toolbar.
6. **Values are always visible while changing.** Every drag shows a readout near the cursor: position (bars.beats.ticks), length, pitch name, velocity, volts, percent.

---

## 1. Rack-specific foundations

Rack is not a DAW host, so a few things need deliberate handling:

- **The editor is an overlay in screen space**, added to `APP->scene`, unaffected by the rack's own zoom. It has its own UI scale setting (75–200%, default follows Rack's window scale).
- **Event capture.** While the overlay is open it consumes all hover, scroll, button and key events inside its bounds, so the rack behind never pans or zooms. Use `e.consume(this)` consistently. Keys pass through to Rack only for global shortcuts the editor doesn't use.
- **Scroll input.** Rack delivers `HoverScrollEvent::scrollDelta` (x and y). Trackpads produce fine-grained x/y deltas; mice produce coarse y deltas. Detect fine-grained input (small, frequent deltas) and treat it as trackpad: two-finger scroll pans in both axes naturally. Pinch is not available in Rack, so zoom is always Ctrl/Cmd+scroll or the zoom gestures in §3.
- **Text entry** (renaming, numeric fields) uses Rack's `ui::TextField`, styled with the theme. Enter commits, Esc cancels, Tab moves to the next field.
- **Rendering.** NanoVG. Static layers (grid, keyboard, clip previews) are cached in `FramebufferWidget`s and redrawn only when zoom, scroll or content changes. The playhead, hover highlights, drag previews and readouts live on a light top layer drawn every frame.

---

## 2. Global interaction vocabulary

### Modifiers (identical in every editor)

| While dragging / clicking | Meaning |
|---|---|
| **Alt** (Option) | Bypass snap (free placement) |
| **Shift** | Constrain to one axis, decided by the first 4 px of movement. In value lanes: fine adjust (×0.1) |
| **Ctrl/Cmd + drag** | Duplicate the dragged items |
| **Ctrl/Cmd + click** | Toggle item in selection |
| **Shift + click** | Add to selection (range-extend where order is meaningful, e.g. tracks) |
| **Alt + drag on a note or clip body** | Edit its primary value (note velocity, clip instance velocity scale) |
| **Double-click** | Open / edit (note → inspector focus, clip → detail editor, empty space → create) |
| **Right-click** | Context menu. In the Draw tool, right-click (and right-drag) deletes. A setting switches this to "always context menu" |
| **Esc during a drag** | Cancel the gesture and revert to the pre-drag state |

On macOS, Cmd replaces Ctrl everywhere; Ctrl+click is treated as right-click.

### Selection model

- Single selection set per editor. Selecting in the arranger and piano roll are independent, but clicking a clip in the arranger makes it the piano roll's target.
- Rubber band from empty space with the Select tool, or with any tool by holding Ctrl/Cmd+Alt and dragging (escape hatch so you never need to switch tools to select).
- Rubber band selects items it **touches**, not only items fully inside (configurable).
- Selection survives undo/redo: each command records the selection before and after.
- Ctrl/Cmd+A select all, Ctrl/Cmd+Shift+A deselect, Ctrl/Cmd+I invert selection.

### Cursor feedback

Hovering changes the cursor and draws a subtle hover highlight, so the user always knows what a click will do before clicking:
move (4-way), resize left/right (edge hot zones ≥ 6 px, extending outside narrow notes), pencil, eraser, slice (scissors with a vertical guide line across the editor), velocity (vertical double arrow), hand (pan), zoom (magnifier).

### Gesture engine

Implement interactions as small gesture objects (`DragGesture` subclasses: `MoveNotes`, `ResizeNoteEnd`, `RubberBand`, `PaintNotes`, `DrawAutomation`, `TrimClip`, …).

- `begin` captures an immutable copy of the affected items.
- `update(mousePos, modifiers)` recomputes the full preview from that copy every frame (never incrementally — this makes Shift/Alt toggling mid-drag and Esc-cancel trivial and keeps snapping stable).
- `commit` emits exactly one command. `cancel` discards the preview.
- **Auto-scroll:** while dragging near an editor edge (24 px band), scroll in that direction at a speed proportional to proximity. The gesture keeps updating as content scrolls under the cursor.

---

## 3. Navigation (shared by piano roll, drum editor, arranger, automation)

### Scroll and zoom

| Input | Action |
|---|---|
| Wheel / two-finger vertical | Scroll vertically |
| Shift + wheel, or two-finger horizontal | Scroll horizontally |
| Ctrl/Cmd + wheel | Zoom horizontally, anchored at the mouse X |
| Alt + wheel | Zoom vertically (row / track height), anchored at mouse Y |
| Middle-drag, or Space + drag | Pan (hand) |
| Drag vertically in the ruler | Zoom (down = in, up = out), anchored at the click point. Drag horizontally while doing so to pan. |
| Double-click the ruler | Zoom to fit |
| Ctrl/Cmd + Alt + drag rectangle | Zoom to rectangle |
| `F` | Zoom to fit: selection if any, else all content |
| `Z` | Zoom to selection (both axes in piano roll) |
| `+` / `-` (or `=` / `-`) | Zoom in / out around the playhead (or the mouse if it is over the editor) |
| `Ctrl/Cmd + [` / `]` | Zoom history back / forward (every zoom-to-* pushes onto the history) |
| Home / End | Jump view and playhead to song (or clip) start / end |
| Page Up / Page Down | Scroll one screen horizontally |

- Zoom is smoothed over 80 ms for wheel and keyboard input (eased, still anchored), immediate for ruler drags.
- Horizontal zoom range: 1 bar fills the screen at max zoom-in → whole song with 8 px per bar at max zoom-out. Grid and labels adapt so lines never get closer than 6 px.
- Vertical piano roll row height: 6–40 px. Arranger track height: 24–240 px per track, plus "fit all tracks" (`Shift+F`).

### Overview bar (minimap)

A thin strip under the ruler in both the arranger and piano roll shows the whole clip/song in miniature with the current view as a draggable window. Drag the window to scroll, drag its edges to zoom, click anywhere to jump. In the piano roll, a vertical overview on the right of the keyboard does the same for pitch.

### Follow playhead

Toggle `Ctrl/Cmd+F` (button in transport). Modes: **Page** (jumps when the playhead leaves the view) or **Continuous** (playhead fixed at 1/3 of the width, content scrolls). Following pauses automatically while the user is dragging or has scrolled in the last 2 seconds, then resumes. A "playhead off-screen" arrow at the edge clicks to jump back.

### Playhead and transport in the ruler

- Click in the ruler's lower half: move the playhead (snapped). Double-click: move and play from there.
- Drag in the ruler's upper half (the loop strip): draw the loop region. Drag its body to move it, edges to resize. Double-click it to toggle loop on/off.
- `Ctrl/Cmd+L`: set loop to selection.
- Markers row above the ruler: double-click to add, drag to move, click a marker to jump.

---

## 4. Piano roll

### 4.1 Layout

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Toolbar: tools │ snap ▾ │ chord stamp ▾ │ scale ▾ fold │ ghost ▾ │ ⋯     │
├──────┬───────────────────────────────────────────────────────────┬───────┤
│      │ markers / loop strip / ruler / overview                    │       │
│ keys ├───────────────────────────────────────────────────────────┤ Insp- │
│      │                                                            │ ector │
│      │                      note grid                             │       │
│      │                                                            │       │
├──────┼───────────────────────────────────────────────────────────┤       │
│ lane │ Expression lanes (tabs): Velocity │ Release │ Chance │     │       │
│ name │ Repeats │ Timbre │ Pressure │ Pitch │ Automation 1..8      │       │
└──────┴───────────────────────────────────────────────────────────┴───────┘
```

- **Keyboard** (left): click/drag to audition (glissando across keys while dragging). Scale notes tinted, root marked, active (playing) notes light up. Ctrl/Cmd+click a key selects all notes of that pitch. Drum tracks show lane names instead of keys.
- **Grid**: bar / beat / subdivision lines at three strengths; black-key rows shaded; out-of-scale rows dimmed when a scale is active; clip loop region shown, content beyond the loop end dimmed.
- **Expression lanes** (bottom, resizable, multiple visible at once stacked, or tabbed): see §4.6.
- **Inspector** (right, collapsible with `I`): see §4.7.
- **Ghost notes**: notes from other tracks or other clips on the same track, drawn as outlines at 30% opacity. Source picker in the toolbar (none / one track / all tracks / other clips in this track). Ghost notes are not editable but can be clicked with Alt+Shift to jump to their clip.

### 4.2 Creating notes

| Tool | Key | Behavior |
|---|---|---|
| **Draw** | `D` (or `B`) | Click: create note at snapped position with the **last-used length and velocity**. Click-and-drag right: set length while creating. Click-and-drag vertically right after creating: set pitch (preview sounds each pitch). |
| **Paint** | `P` | Drag horizontally: create a note at each grid step along the path. Drag vertically too: paints a melodic line following the mouse. Hold Shift: keep pitch fixed. |
| **Chord stamp** | toolbar | When a chord type is active, Draw places the whole chord. Scale-aware option builds diatonic chords from the clicked root. Inversion and spread (close / open / drop-2) selectable. |
| **Double-click** empty space | any tool | Create a note (same as Draw click) |
| **Step input** | `Ctrl/Cmd+R` toggle | With the editor focused, play notes on MIDI/REC CV or the computer keyboard (see §4.10): each note is inserted at a step cursor that advances by the grid. Chords are inserted together. Right arrow inserts a rest, Backspace deletes the last step, Shift+right extends the last note (tie). |

New notes are auditioned (short preview on the track's outputs) unless audition is muted (speaker toggle in the toolbar, `Alt+A`).

### 4.3 Selecting and editing

| Tool | Key | Behavior |
|---|---|---|
| **Select / pointer** | `S` (or `V`) | Click selects, drag moves, edges resize, rubber band from empty space |
| **Erase** | `E` | Click or drag across notes to delete |
| **Slice** | `C` | Click splits the note under the cursor at the snapped position. Drag a line across notes to slice all of them where the line crosses. Shift+click: split into N equal parts (prompt). |
| **Mute** | `T` | Click or drag to toggle mute |
| **Velocity** | `Y` | Drag vertically on any note: changes velocity of the selection (relative). Hold Shift+drag across notes: draw a velocity line through them. |
| **Zoom** | `Z` (hold) | Click to zoom in, Alt+click to zoom out, drag a rectangle to zoom to it |

Pointer behaviors in detail:

- **Move**: drag the body. The note's start snaps to the grid, also when it started off it (docs/DECISIONS.md §23).
- **Resize**: drag right edge for end, left edge for start. Resizing a selection resizes all selected notes by the same delta. **Alt+Shift drag on edge**: proportional resize of all selected notes from the selection's left edge (scales timing).
- **Stretch the selection**: when more than one note is selected, a thin selection frame with handles appears around them (toggle `Ctrl/Cmd+Shift+T`). Drag side handles to time-stretch, top/bottom handles to scale pitch range (scale-aware), corners for both.
- **Duplicate**: Ctrl/Cmd+drag, or `Ctrl/Cmd+D` (duplicates the selection immediately after itself in time; repeat to keep stamping).
- **Transpose**: Up/Down = 1 semitone (1 scale step when scale snap is on), Shift+Up/Down = octave.
- **Nudge**: Left/Right = 1 grid step, Alt+Left/Right = 1/10 of a grid step (fine), Shift+Left/Right = 1 bar.
- **Length**: Shift+Alt+Left/Right shortens/lengthens by one grid step.
- **Velocity**: Ctrl/Cmd+Alt+Up/Down ±5%, with Shift ±1%.
- **Cycle selection**: Alt+Up/Down/Left/Right moves the selection to the next note in that direction (keyboard-only editing of individual notes).
- **Clipboard**: Copy, Cut, Paste at playhead (or at the mouse with `Ctrl/Cmd+Shift+V`). Pasting into a different clip keeps relative timing; pasting into a drum track maps pitches to lanes by lane pitch.
- **Overlap rule**: when a moved or created note fully overlaps another note of the same pitch, the old note is trimmed (shortened) rather than stacked. When fully covered, it is replaced. Configurable.

### 4.4 Snap and grid

- Snap dropdown: Off, Adaptive (default; grid follows zoom), Bar, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64, each with straight / triplet / dotted variants, plus "Events" (snap to other notes' starts and ends — magnetic snap).
- Keyboard: `Ctrl/Cmd+1` narrows the grid, `Ctrl/Cmd+2` widens it, `Ctrl/Cmd+3` toggles triplet, `Ctrl/Cmd+4` toggles snap on/off.
- The grid's visual density and the snap value are linked by default; a lock icon decouples them.
- Swing preview: when the global swing is above zero, grid sub-lines that are swung are drawn at their swung positions (thin) to show where notes will actually fall.

### 4.5 Note operators (per note)

Every note carries operators that change how it plays back each time, without changing the written note. They are edited in the Inspector (for the selection) and in their own expression lanes (per note).

| Operator | Values | Playback behavior |
|---|---|---|
| **Chance** | 0–100% | Probability the note plays on each pass |
| **Velocity spread** | 0–100% | Each play, velocity is randomized by up to ± this amount around the written velocity (played if a song has it, but no longer offered in the editor: DECISIONS §35) |
| **Repeats** | count −16 … +16, curve −1 … +1, velocity end 0–100%, gate 1–100% | Ratchets: the note is split into N evenly spaced (curve 0) hits within its length. Positive curve accelerates, negative decelerates. Velocity ramps from the note's velocity to "velocity end". Negative counts mean "repeat at this many per beat" instead of "N in the note's length". Repeats are drawn inside the note as tick marks |
| **Occurrence** | Always, First, Not first, Previous played, Previous not played, Fill, Not fill | Conditional play. "Previous" refers to the nearest earlier note in this clip (any pitch) on this pass. "Fill" reads the FILL state (FILL button / FILL gate input on Tessera) |
| **Recurrence** | length 1–16 passes, per-pass on/off bitmask | Note plays only on selected passes of the clip loop (e.g. length 4, passes 1 and 3). Edited as a row of toggles in the inspector and drawn as small dots under the note |
| **Timing spread** | 0–50% of a 1/16 | Random humanize applied on each play (played if a song has it, but no longer offered in the editor: DECISIONS §35) |
| **Micro-timing** | −50 … +50% of a 1/16 | Fixed offset applied at playback (keeps the written note on the grid for editing) |

Notes with a non-default operator show a small badge (chance %, repeat count, recurrence dots) when zoomed in enough. A toolbar toggle hides badges.

Randomness uses a per-track RNG seeded from (song seed, clip id, pass number), so a pattern is repeatable within a session and a "Reseed" button gives a new variation. The engine evaluates operators when scheduling each pass, never in the model.

### 4.6 Expression lanes

The lower pane holds per-note value lanes. Several can be shown stacked (click lane names with Ctrl/Cmd to add to the stack) or one at a time as tabs.

Lanes: **Velocity**, **Release velocity**, **Chance**, **Velocity spread**, **Repeats** (count), **Micro-timing**, and per-note expression curves **Pitch** (bend, ±48 semitones), **Timbre**, **Pressure** (MPE-style), plus the clip's **automation lanes** (continuous, not per note).

Editing in stem lanes (velocity, chance, release, spread, repeats, micro-timing):

- Each note is a vertical stem with a handle; selected notes' stems are highlighted, others dimmed (only selected notes are editable if anything is selected — prevents accidental edits).
- **Drag a stem**: set that note's value. With multiple selected: relative change for all.
- **Draw (Pencil or Shift+drag)**: freehand across stems sets values along the path.
- **Line (Alt+drag)**: straight line ramp from press to release point across the notes in between.
- **Curve (Alt+Shift+drag)**: ramp with a bendable curve (drag the midpoint handle that appears after release, until the next click).
- Values for chords: stems at the same time are fanned slightly so each is grabbable; hover shows the note's pitch name.

Per-note expression curves (Pitch, Timbre, Pressure):

- Shown as small envelopes **inside each note** in the grid when the lane is active, and as editable curves in the lane.
- Click to add points, drag to move, drag segment midpoints to curve, double-click a point to delete. The curve spans the note's duration and moves with the note.
- Pitch curves snap to semitones with Shift (and to scale degrees when scale snap is on).
- **On the notes:** pitch curves are also drawn in the note grid at the pitch they really play, so a glide visibly
  lands on the row it heads for. While the lower pane shows Pitch, pressing a note in the grid bends it (Select and
  Draw alike); timbre and pressure are shaped in a strip above the selected notes while the lower pane shows them
  (docs/DECISIONS.md §63–66).
- Output: per voice, the Pitch curve is added to the V/OCT output; Timbre and Pressure go to Tessera Track Out's **TIMBRE** and **PRESS** poly outputs (see DESIGN.md §6).

### 4.7 Inspector (selection editing panel)

The inspector shows the current note selection and is the main place for multi-note edits.

- Fields: start, length, pitch, velocity, release velocity, plus every operator from §4.5.
- **Single note**: values shown and editable directly (drag the field vertically, or click and type).
- **Multiple notes**: each field shows the **range** (e.g. `velocity 54–98%`) as a small distribution bar with every note's value as a tick. Dragging the field shifts all values together (clamped, keeping their differences). Clicking a field on a fixed scale (pitch, velocity, release, micro-timing, chance…) opens its **chart** under it, a histogram (one open at a time, velocity by default):
  - the chart counts how many notes sit at each value, with a marker at the mean;
  - **Mean** (drag): moves every note; click it to type a value or an edit;
  - **Spread** (drag): right pulls the values apart, left gathers them toward the mean; it springs back after release;
  - **Chaos** (drag): scatters the values at random, further the further you drag; it springs back after release.
- Values change the notes themselves: what you see is what plays, every pass. There is no per-play variation of velocity or timing in the editor (the old velocity and timing spread); chance, repeats, occurrence and recurrence are the only things that differ from pass to pass.
- Ramps across the selection are the "Shape velocity…" transform (§4.8), not an inspector button.
- Fields also accept typed expressions: `+10`, `-5%`, `*0.8`, `=64`.

### 4.8 Transform functions

Menu: `Notes` in the toolbar's `⋯`, and the right-click menu on selected notes. Each opens a small popover with live preview (the result is previewed on the grid while you adjust; OK commits one command, Esc reverts). Applies to the selection, or the whole clip if nothing is selected.

| Function | Parameters |
|---|---|
| **Quantize** | grid, strength %, swing %, start / end / both, "only notes off by more than X" (Q-range) |
| **Humanize** | timing ± ms or %, velocity ± %, length ± % |
| **Velocity shaper** | draw a curve over the selection's time span; multiply/add/replace velocities by it |
| **Legato** | extend each note to the next one; "only same pitch" option; overlap amount |
| **Strum** | direction up/down/alternating, time per note, curve; for chords |
| **Flam** | add a grace note before each note: offset, velocity |
| **Chop** | split each note into N parts or at a grid; optional velocity ramp across parts |
| **Glue** | join touching notes of the same pitch |
| **Arpeggiate** | render chords into an arpeggio: pattern (up, down, up-down, random, as played), rate, gate, octaves |
| **Strum / Span / Connect** | fill gaps between notes with passing notes in scale |
| **Reverse / Invert / Retrograde** | time-reverse; pitch-invert around a pivot (scale-aware) |
| **Transpose** | semitones or scale degrees; "fit to scale" (moves out-of-scale notes to nearest scale note) |
| **Scale velocity / length** | percent, or set to fixed |
| **Randomize** | which properties, amounts, seed |
| **Euclidean fill** | hits / steps / rotation at the cursor's pitch (also in drum editor) |
| **Select by…** | pitch, velocity range, position in bar (e.g. all off-beats), every Nth, same as clicked note, muted, with operators set |

### 4.9 Clip-level controls (top of the piano roll)

- Clip name and color (click to edit).
- Clip loop start/end and loop on/off (also draggable as the loop brace in the ruler).
- Clip length; `Ctrl/Cmd+Shift+D` doubles the loop and duplicates content into the new half.
- "Fit loop to notes".
- Clip-level transpose and velocity scale (non-destructive, applied at playback).
- Clip scale override.
- Multi-clip editing: select several clips in the arranger or launcher and open them together. Their notes are shown in the same grid, each clip's notes in its own color, with a clip filter strip to choose which are editable ("layered editing"). Time is shown in arranger time when opened from the arranger.

### 4.10 Computer keyboard as instrument

`Ctrl/Cmd+K` toggles "keyboard play" (only while the editor is focused): two rows of the QWERTY keyboard play notes on the selected track (Z-row lower octave, Q-row upper, `Z`/`X` octave down/up, `C`/`V` velocity down/up). While active, the tool shortcuts are disabled and a banner shows "Keyboard play — Ctrl/Cmd+K to exit". Works with recording and step input.

### 4.11 Scale tools in the roll

- Scale highlight and **fold** (show only scale rows; `Ctrl/Cmd+Shift+F`), see DESIGN.md §13.
- When scale snap is on, dragging notes vertically moves in scale steps; holding Alt temporarily allows chromatic notes.
- Out-of-scale notes get a small warning marker when highlight is on.

---

## 5. Drum editor specifics

The drum editor is the piano roll of a drum track (docs/DECISIONS.md §81): everything above applies, with lane rows
instead of pitch rows (DESIGN.md §9). Differences:

- Lane headers replace the keys: color, name, output, mute, solo, then "+ Add lane". Click a name to audition the
  lane (and show its hits in the velocity lane), Ctrl/Cmd+click to select its hits, double-click to rename,
  right-click for the lane menu. Rows keep the lanes' order; notes on no lane get a "(no lane)" row below.
- Draw (and Paint): click an empty step to add a hit one lane step long, click a hit to remove it, drag to paint or
  remove along the path. Alt+drag a hit sets its velocity. With adaptive snap, hits use the lane's own rate.
- Every step of a lane is drawn as a cell at the lane's rate, beats shaded in turn, bar lines in the gaps (beat bands
  when cells would be under 5 px). A hit fills its cell to its velocity. Muted and solo-silenced lanes are dimmed.
- Lanes with their own loop (polymeter) show a line where it ends, then faded repeats until the clip loop ends.
- The velocity lane edits the lane last clicked when nothing is selected; the others are faded.
- The header has Kit where the key chip is. The note menu adds the hit operators (velocity, chance, repeats,
  occurrence, recurrence).
- Lane operations in the header's menu: rotate, reverse, Euclidean, randomize (density, velocity range, seed), clear, copy/paste lane.

---

## 6. Arranger

### 6.1 Layout

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Toolbar: tools │ snap ▾ │ edit mode (normal/ripple) │ follow │ ⋯           │
├────────────────┬─────────────────────────────────────────────────────────┤
│                │ markers / loop strip / ruler / overview                  │
│ track headers  ├─────────────────────────────────────────────────────────┤
│ (groups fold)  │ clips on tracks                                          │
│                │   automation lanes (expand per track)                    │
├────────────────┴─────────────────────────────────────────────────────────┤
│ detail editor (piano roll / drum editor for the selected clip)            │
└──────────────────────────────────────────────────────────────────────────┘
```

### 6.2 Track headers

- Name (double-click to rename), color chip, type icon, output status dot (DESIGN.md §11), mute `M`, solo `S` (click), arm, monitor, polyphony, automation expand arrow.
- Resize height by dragging the bottom edge; Alt+drag resizes all tracks. Double-click the bottom edge toggles between compact and expanded.
- Drag a header to reorder (shows insertion line); drag onto a group header to move into the group.
- Select multiple headers (Shift range, Ctrl/Cmd toggle) to mute/solo/arm/color them together.
- Groups: fold arrow; folded groups show a summary lane with all members' clips flattened.
- Solo behavior: click = toggle, keeping other solos; Shift+click = toggle and un-solo all others (docs/DECISIONS.md §40).

### 6.3 Clips on the timeline

- Clip shows a header strip (name, link badge for shared clips, loop icon) and a content preview (notes for instrument tracks, lane hits for drums), cached as an image.
- Loop seams are drawn as notches where the content repeats.

| Gesture | Result |
|---|---|
| Drag body | Move (snaps; can move across tracks — vertical move shows the target track highlight) |
| Ctrl/Cmd + drag | Duplicate (as a linked instance; Alt+Ctrl/Cmd+drag makes an independent copy) |
| Drag right edge | Lengthen/shorten. Looping clips repeat their content; non-looping clips reveal empty space |
| Drag left edge | Trim the start (adjusts offset); content stays in place on the timeline |
| **Alt + drag right edge** | Time-stretch the instance's content: notes are scaled in time; the clip's stretch factor is stored on the instance |
| **Alt + drag body** (slip) | Slide the content inside the clip without moving the clip (adjusts offset) |
| Double-click | Open in the detail editor; double-click again maximizes the detail editor |
| Pencil drag in empty space | Create an empty clip of that length |
| Click in the clip's top header strip and drag down | Fade-like "velocity scale" handle for the instance (instances can scale velocity non-destructively) |

- `Ctrl/Cmd+D`: duplicate selection after itself. `Ctrl/Cmd+E`: split at the playhead or mouse. `Ctrl/Cmd+J`: consolidate (glue) the selected clips on a track into one clip. `Ctrl/Cmd+U`: make unique.
- Magnetic snap: in addition to the grid, clip edges snap to other clips' edges, markers, the loop edges and the playhead when within 8 px (Alt bypasses).
- Instance properties (inspector when a clip is selected): name, color, loop on/off, start offset, transpose, velocity scale, time-stretch factor, mute, link status, "Show in launcher slot".

### 6.4 Time selection and range editing

- Range tool (`R`, or Alt+drag in empty track space): select a time range across one or more tracks (the range is drawn as a translucent band).
- With a range: Delete removes content inside the range on those tracks (splitting clips at the edges); `Ctrl/Cmd+Shift+Delete` deletes time (everything after closes the gap); `Ctrl/Cmd+Shift+I` inserts silence of the range length; `Ctrl/Cmd+D` duplicates the range after itself; `Ctrl/Cmd+L` loops it.
- **Edit modes** (toolbar): Normal, or **Ripple** (moving, deleting or inserting shifts everything later on the same tracks). Ripple is clearly highlighted in the toolbar when active, in the accent color.

### 6.5 Automation in the arranger

- Each track's automation lanes expand below it. The lane header shows the lane name, current value (volts), range, mapped targets, its source, a record mark (no arm: the track arm records lanes, DECISIONS §73–74), and a mini menu.
- Same point editing as DESIGN.md §12, using the shared modifier vocabulary: click adds a point, drag moves, Alt bypasses snap, Shift fine-adjusts value, drag a segment midpoint to bend the curve.
- **Moving clips can carry automation** ("Automation follows clips" toggle): when on, moving or duplicating a clip moves the track automation within its time range too.
- Range selection includes automation: deleting/duplicating a range affects points in that range on the selected tracks.

### 6.6 Automation editor (shared by arranger lanes and clip lanes)

The same automation editing component is used in three places: track lanes in the arranger (arranger time), clip automation lanes in the piano roll's lower pane (clip time, loops with the clip), and a **maximized automation view** (double-click a lane name, or `A` with a lane selected) that fills the detail pane for precise work. DESIGN.md §12 defines the data and outputs; this section defines the editing.

**Lane display**
- The curve is drawn with its lane color, the area under it filled at 12% (from the lane's zero point: bottom for unipolar, center for bipolar).
- Points are 7 px circles (hit area 12 px); selected points filled, others outlined.
- The value axis on the lane's left edge shows the actual output voltage, with the lane's unit (V, or %, or the mapped param's own display value when the lane drives a mapped knob, e.g. "2.4 kHz").
- Where clip automation and track automation combine (Override / Modulate), the effective result is drawn as a thin dashed line over the track lane so the user sees what will actually be output.
- Lanes with no points show a flat line at the lane's default value and the hint "Click to add a point, or draw with D".

**Point editing**
| Gesture | Result |
|---|---|
| Click empty lane (Pointer) | Add a point on the curve at the click time, value from the click height |
| Double-click a point | Delete it |
| Drag point | Move in time and value (time snaps; Alt bypasses snap; Shift constrains axis then fine-adjusts value) |
| Drag segment midpoint (the small handle shown on hover) | Bend the curve (tension), live preview |
| Alt + click a segment | Insert a point exactly on the existing curve (does not change the shape) |
| Right-click point | Curve type for the following segment (Hold, Linear, Exponential, S-curve), set value exactly, delete |
| Drag a whole segment vertically (Ctrl/Cmd+drag on the line between points) | Move both its points' values together |

**Drawing**
- **Draw tool** (`D`): freehand drawing; points are simplified (Ramer–Douglas–Peucker, tolerance tied to zoom) on mouse-up.
- **Line** (Alt+drag with Draw): straight ramp replacing everything under it.
- **Shape stamps** (toolbar dropdown when a lane is focused): sine, triangle, saw up/down, square, random steps, smooth random, exponential rise/fall, ADSR. Drag across a time range to stamp; while dragging, the vertical drag sets depth and the wheel sets the cycle rate (snapped to grid divisions). After release, a popover allows adjusting rate, depth, phase, offset and skew with live preview before committing.
- **Paint steps** (Paint tool): drag to paint Hold-curve steps at each grid division (stepped modulation, sequencer style).

**Selection and transforms**
- Rubber band selects points (also across stacked lanes). Range selection (`Alt+R`) selects all points in the time range, including on multiple lanes and tracks.
- **Automation transform box** (appears around 2+ selected points): drag top edge to scale values toward the bottom, bottom edge toward the top, side edges to stretch time; **corner handles tilt** the top-left/top-right independently (fade in/out a modulation's depth). Alt+drag the top edge compresses/expands around the center.
- Keyboard: Up/Down nudges values by 1% (Shift 0.1%), Left/Right nudges time by grid (Alt fine).
- **Clipboard**: copy/paste points between lanes and between tracks; pasting into a lane with a different voltage range keeps normalized values (with an option to keep volts).
- Functions in the lane menu: Thin (reduce points with tolerance), Quantize point times to grid, Smooth, Invert (flip around the center), Reverse, Randomize values, Set all to value, Clear.

**Lane management**
- Lane header: name, color, value readout, output (AUTO channel, V/OCT or PRESS), source, voltage range, bipolar flag, mute, record mark, mapped targets, lane menu.
- `+` at the bottom of a track's lanes adds a lane; drag lane headers to reorder (changes nothing in output routing — the output channel is a separate setting).
- Show only lanes that have data: "Show used lanes" toggle per track (`Shift+A`).
- Clip vs track automation: a small "clip / track" toggle in the clip's lower-pane lane header, and "Move clip automation to track" / "Move track automation into clip" in the lane menu.

**Recording automation** (writes into the lane being edited)
- Arm the track; each lane with a source records (MIDI control, mapped knob or AUTO REC channel; Learn picks one), with Latch / Touch / Write for knobs and CV. While recording, the curve draws live in red ahead of the existing curve. Recorded data is thinned on commit, one undoable command per pass.
- Punch: if the loop is on, recording respects the loop region; Touch mode returns to the existing curve over 50 ms when the knob is released.

**Playback feedback**
- A dot rides the curve at the playhead showing the current output value; the lane header readout updates live.
- Mapped targets' knobs in the rack visibly move (via `ParamHandle`), and the lane header flags any target that the user is currently overriding by hand.

---

## 7. Keymap reference

Shortcuts are active only while the editor overlay is focused. They are defined in one table (`Shortcuts.cpp`) with action IDs, so the help overlay (`?`) and tooltips are generated from the same data. User remapping is a v1.1 item; design the table to allow it.

| Group | Action | Shortcut |
|---|---|---|
| Transport | Play / stop | Space |
| | Play from mouse / selection start | Shift+Space |
| | Record | R (Shift+R when Range tool shortcut conflicts — Range is `Alt+R`) |
| | Loop on/off | L |
| | Set loop to selection | Ctrl/Cmd+L |
| | Follow playhead | Ctrl/Cmd+F |
| | Fill (momentary) | Hold F1 |
| Tools | Pointer / Draw / Paint / Erase / Slice / Mute / Velocity / Zoom | S, D, P, E, C, T, Y, Z(hold) |
| | Range (arranger) | Alt+R |
| | Toggle step input | Ctrl/Cmd+R |
| | Keyboard play | Ctrl/Cmd+K |
| Edit | Undo / redo | Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z |
| | Cut / copy / paste / paste at mouse | Ctrl/Cmd+X / C / V, Ctrl/Cmd+Shift+V |
| | Duplicate | Ctrl/Cmd+D |
| | Delete | Delete / Backspace |
| | Select all / none / invert | Ctrl/Cmd+A, Ctrl/Cmd+Shift+A, Ctrl/Cmd+I |
| | Split / glue / make unique | Ctrl/Cmd+E, Ctrl/Cmd+J, Ctrl/Cmd+U |
| | Quantize / quantize dialog | Q, Shift+Q |
| | Transpose ±1 / ±octave | Up/Down, Shift+Up/Down |
| | Nudge grid / fine / bar | Left/Right, Alt+Left/Right, Shift+Left/Right |
| | Length ± grid | Shift+Alt+Left/Right |
| | Velocity ± 5% / 1% | Ctrl/Cmd+Alt+Up/Down (+Shift) |
| | Cycle selection to next note | Alt+arrows |
| | Transform box | Ctrl/Cmd+Shift+T |
| View | Zoom to fit / selection | F, Z |
| | Zoom in / out | + / − |
| | Zoom history | Ctrl/Cmd+[ / ] |
| | Fit all tracks | Shift+F |
| | Show used automation lanes | Shift+A |
| | Maximize automation lane in detail pane | A |
| | Narrow / widen grid, triplet, snap | Ctrl/Cmd+1 / 2 / 3 / 4 |
| | Scale fold | Ctrl/Cmd+Shift+F |
| | Inspector | I |
| | Switch focus arranger ↔ detail | Tab |
| | Maximize focused pane | Shift+Tab or double-click splitter |
| | Widen / narrow the clips beside the timeline | F2 |
| Tracks | Add instrument / drum track | Ctrl/Cmd+T, Ctrl/Cmd+Shift+T |
| | Select next/prev track | Ctrl/Cmd+Up/Down |
| | Mute / solo selected tracks | M, Alt+S |
| | Add marker / jump to marker 1–9 | Ctrl/Cmd+M, Ctrl/Cmd+1–9 in arranger with Alt (Alt+1–9) |
| General | Help overlay | ? |
| | Export MIDI (opens the export dialog, DESIGN.md §17) | Ctrl/Cmd+Shift+E |
| | Close editor | Esc (first Esc clears selection / cancels drag, second closes) |

Rule: when two DAWs disagree, prefer the binding that doesn't need a modifier for the most frequent action, then the most common convention among DAWs.

---

## 8. Visual polish checklist

- Notes: rounded 3 px, filled with clip color at brightness mapped from velocity (40–100%), 1 px darker outline; selected = 1.5 px `select` outline and +10% brightness; muted = hatched at 35% opacity; playing = brief glow pulse on note-on.
- Note labels (pitch name) appear when a note is ≥ 28 px wide and row ≥ 12 px tall.
- Velocity readout appears inside notes on hover when zoomed in.
- Rubber band: `select` color at 15% fill, 1 px outline.
- Hover highlight: +6% brightness on the hovered item; hovered row and column get a faint crosshair band in the grid (toggleable).
- Snapping indicator: a short accent-colored tick on the ruler at the snapped position while dragging.
- Drag readouts: rounded pill near the cursor, mono font, offset so it never covers the item being dragged.
- Empty states: an empty clip shows "Draw with D, or press Ctrl/Cmd+R for step input"; an empty arranger track shows "Drag a clip here or draw one with D".
- Everything stays crisp at 75–200% UI scale (use the theme's metric functions, never raw pixels).

---

## 9. Implementation notes

- Coordinate transforms live in one `TimeAxis` (ticks ↔ x, with zoom and scroll) and one `PitchAxis` / `TrackAxis` (pitch or track ↔ y). Every view and gesture uses them. Unit test both, including zoom anchoring math.
- Hit testing: notes sorted by start, binary-search the visible window, then test rows. Expand hit rects for tiny notes. Priority order when items overlap: resize edge > selected note body > unselected note body > empty space.
- Grid drawing chooses its subdivision from zoom so lines are ≥ 6 px apart; labels ≥ 40 px apart.
- All transform functions in §4.8 are pure functions in `src/model/Transforms.cpp` (`std::vector<Note> -> std::vector<Note>` with parameters), each unit tested, wrapped by a generic `ReplaceNotes` command.
- Note operator evaluation (chance, recurrence, occurrence, repeats, spreads) lives in `src/engine/NoteOperators.hpp`, called when the engine schedules a clip pass; unit test determinism for a given seed.
- Performance targets from DESIGN.md §18 apply. Any gesture's `update()` must run in < 1 ms with 2,000 selected notes.

---

## 10. Build order for the editors

Fold these into DESIGN.md §20 milestones 3, 4, 7, 11 and 12:

1. Axes, navigation (§3 complete: scroll, zoom, ruler zoom, overview, follow), gesture engine, cursor feedback, drag readouts.
2. Piano roll core: Pointer and Draw tools, move/resize/duplicate, snap menu, keyboard audition, velocity lane, keyboard nudge/transpose.
3. Remaining tools (Paint, Erase, Slice, Mute, Velocity, Zoom), clipboard, overlap rules, transform box.
4. Arranger core: headers, clips, move/trim/loop-extend, split/duplicate/glue, magnetic snap, markers, loop region.
5. Inspector with offset/spread/randomize/ramp; note operators in the model and engine; their lanes and badges.
6. Transform functions with live preview.
7. Range editing, ripple mode, the full automation editor (§6.6: point editing, drawing, shape stamps, transform box, lane management, recording), automation-follows-clips.
8. Step input, keyboard play, multi-clip editing, per-note expression curves, ghost notes.
9. Polish checklist (§8), help overlay generated from the keymap.
