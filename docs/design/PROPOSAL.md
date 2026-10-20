# Tessera redesign: the experience

This is the "what changes and why" for the editor, the panel and the output modules. The look (colours, type,
icons) is in [DESIGN_LANGUAGE.md](DESIGN_LANGUAGE.md). The problems it answers are in [AUDIT.md](AUDIT.md).

Mockups: [E1 editor, Timeline plays](mockups/E1-editor-dark.png) ([light](mockups/E1-editor-light.png)),
[E4 editor, Clips play](mockups/E4-editor-plays-clips-dark.png), [E2 perform](mockups/E2-perform-dark.png),
[E3 drums, notes maximised](mockups/E3-drums-dark.png), [P1 panels](mockups/P1-panels-light.png),
[P2 widened panel](mockups/P2-panel-widened-dark.png), [P5 panel screen options](mockups/P5-screen-options-dark.png),
[D2 colour schemes](mockups/D2-colour-schemes.png).
Wireframes: [W1](wireframes/W1-editor-song-and-notes.png) – [W6](wireframes/W6-outputs.png).

### Revision 2 (12 Oct 2026): review feedback

| Feedback | Change | Where |
|---|---|---|
| The default-width panel should preview the arranger / piano roll | The screen shows the song (the timeline, or the clip grid when clips play) above the selected track's notes as a tiny piano roll | §3.9, [P5](mockups/P5-screen-options-dark.png) |
| The dark theme is too deep and too contrasty | New dark default **Slate**: canvas #292B30 instead of #15181E, text about 10:1 instead of 15:1, panel screens lifted too | §3.11 |
| Add colour schemes | Four schemes in one menu: Plaster, Sand, Slate, Walnut, plus "Follow Rack" | §3.11, [D2](mockups/D2-colour-schemes.png) |
| Keep the light theme | Plaster is unchanged and stays the light default | §3.11 |
| Clips and timeline in one view, with a clear switch for what plays | A **Plays: Clips \| Timeline** switch in the transport (and PLAYS on the panel). The playing section gets a jade frame and a "Playing" tag; the other one steps back | §2.2, [E1](mockups/E1-editor-dark.png), [E4](mockups/E4-editor-plays-clips-dark.png) |
| No Song / Both / Notes switch; maximise / restore instead | Every pane (Song, Notes) has a maximise / restore button in its top-right corner, in the full editor and the in-panel editor | §2.1, [P2](mockups/P2-panel-widened-dark.png), [E3](mockups/E3-drums-dark.png) |

---

## 1. Principles for this redesign

1. **One concept, one control, one place.** If two controls read the same, one goes. Every removed control is
   listed in §5 with where its job went.
2. **The song never rotates.** Tracks are rows everywhere: in the clip grid, the timeline, the panel's screen and
   the in-panel editor.
3. **Say what plays.** Each track says in words what it is playing right now. There is no hidden mode to know.
4. **Quiet chrome, loud music.** The interface is neutral; colour belongs to tracks, clips and notes. The accent
   is rare, so focus is obvious.
5. **The panel is the product shot.** The default-width panel must say "piano roll + clips" at thumbnail size,
   in its light variant (that is what the VCV Library shows).
6. **Fit, then hide.** Every view lays out from its height budget: the main area keeps its room, extras shrink,
   then hide. Popovers stay inside their pane.

---

## 2. Information architecture

### 2.1 Before and after

**Today:** a transport bar; a top pane that is *either* the launcher grid (tracks as columns) *or* the arranger
(tracks as rows, optional side launcher); a detail pane (piano roll / drum steps, optional device strip, lower
lanes); an inspector. A separate play mode (Launcher / Arranger) decides what sounds. The panel editor has a
four-way switch (Launcher / Arranger / Both / Piano roll).

**Proposed:** two panes and a column, always in the same places.

```
┌ Transport: Main ▾ │ ▶ ■ ●▾ ⤓ ⟳ │ 9.3.1  120.00  Swing 12%  4/4  C minor ▾ │ tools │ ↶ ↷ │ follow  inspector  ?  ✕ ┐
├ SONG ────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ Tracks          │ Clips (scenes →)        ║│ Timeline ▾                                                     │
│ ▸ ① Drums  M S  │ [Beat A▶][Beat B][    ] ║│ [Beat A│Beat A][Beat B   ]           ↺ Back to timeline         │
│   ▶ Beat A · →  │                         ║│                                                                │
│ ▾ ② Bass   M S  │ [Root  ][Walk ⋯][    ] ║│ [Root        ][Walk        ][Root   ]                            │
│   Cutoff lane   │                         ║│  ~~~~~~~~~ automation ~~~~~~~~~                                 │
├ NOTES ─────────────────────────────────────────────────────────────────────┬ INSPECTOR ──────────────────────┤
│ ‹ › [■ Keys · Verse A  2 bars ▾] [Devices 2] │ Snap ▾  C minor ▾ Snap Fold Chord ▾ │ View ▾ Notes ▾ 🔈 │ 3 notes (Eb maj) │
│ keys │ piano roll / drum steps                                             │ fields, chart, Plays           │
│      │ Velocity  Release  Chance  Repeats  Micro │ Pitch Timbre Pressure │ … │ 12 notes · used 2×          │
└──────┴──────────────────────────────────────────────────────────────────────┴────────────────────────────────┘
```

- **Song pane** = track headers + the **clip shelf** (the launcher, scenes as columns) + the **timeline**. Both
  are always on screen, lined up row by row. This is today's "launcher beside the arranger" (DECISIONS §33)
  made the only launcher.
- **Perform** = the same shelf, widened. Drag the divider between clips and timeline (double-click it, or F2,
  to jump) and the clips take the width; the timeline folds to a thin strip on the right
  ([E2](mockups/E2-perform-dark.png), [W2](wireframes/W2-perform-view.png)). Nothing rotates and the track headers
  don't move. Widening the clips does **not** change what plays; only the Plays switch does (§2.2).
- **Maximise / restore**: the Song pane and the Notes pane each have a ⤢ button in their top-right corner. Maximise
  hides the other pane; the same button (now ⤡, lit) restores it. F11 does the same for the focused pane. This is
  the only way to change the layout besides dragging splitters ([E3](mockups/E3-drums-dark.png) shows the Notes
  pane maximised, [E2](mockups/E2-perform-dark.png) the Song pane).
- **Notes pane** = piano roll or drum steps for the selected clip, with the device strip on demand and one lower
  lane.
- **Inspector** = the selection only (notes or steps). Collapsible with I.
- **Panel editor** = the same two panes, clips beside the timeline, each pane with its maximise / restore button.
  Its strip keeps only the tool palette and "Full editor"; the Launcher / Arranger / Both / Piano roll switch is
  gone ([P2](mockups/P2-panel-widened-dark.png)).

### 2.2 What plays: one clear switch

Clips and timeline are always on screen together, so *seeing* them is never a choice. *Hearing* them is, and it
gets one big, clear control:

- **Plays: [Clips | Timeline]** in the transport, right after the transport buttons
  ([E1](mockups/E1-editor-dark.png), [E4](mockups/E4-editor-plays-clips-dark.png)). The lit half is what you hear;
  it is jade with ▶ while the transport runs. On the panel it is the **PLAYS** switch (same Rack param
  `MODE_PARAM`, so CV/MIDI mapping and old patches keep working).
- **The playing section is highlighted.** It gets a jade frame, a tinted header and a "▶ PLAYING" tag. The other
  section steps back (its content is drawn at about half strength) but stays fully editable: you can still draw in
  the timeline while clips play, and drag clips while the timeline plays.
- **Timeline plays** (Arranger mode today): tracks follow the timeline. Launching a clip still takes that one
  track over (DECISIONS §19). That slot stays lit inside the dimmed clips section, the track's lane
  dims and shows **↺ Back to timeline**.
- **Clips play** (Launcher mode today): only launched clips play; the timeline is dimmed.
- Each track header's second line still says what it plays: "Timeline", "▶ Verse A", "Stopped", "● Recording".
- **Default for new songs: Timeline.** A new song's timeline is empty, so a clips-only user flips the switch once
  (or just launches clips: the launched tracks play either way).

This replaces the earlier idea of hiding the mode in a "Timeline ▾" menu (decision **D1**, revised): the review
asked for a switch you can't miss. What goes is the transport's "Show launcher / Show arranger" view button.

---

## 3. Area by area

### 3.1 Transport ([E1](mockups/E1-editor-dark.png), top row)

Left to right, in groups:

| Group | Contents | Notes |
|---|---|---|
| Instance | **Main ▾** (logo + instance name) | Menu: Open / Save / Save as / Export MIDI / Export DAWproject (was "File"), Rename, Editor size, Colour scheme (§3.11) |
| Transport | **▶** (filled jade while playing), **■**, **● ▾** (record; the caret opens count-in, input quantize, new clip length, over-clip mode, MIDI input, snap to key), **⤓ Capture** (a red dot when something was played that can be kept), **⟳ Loop** | Record settings move from a hidden right-click to a visible caret. Capture leaves the launcher toolbar |
| What plays | **Plays: [Clips \| Timeline]** | §2.2. The one control for what you hear |
| Song | **9.3.1** (click to type a bar to jump to), **120.00 BPM** (drag; "EXT 120.3" with a clock), **Swing 12%** (new: the engine already plays it), **4/4**, **C minor ▾** | On narrow windows these fold into one "Song ▾" button instead of disappearing |
| Tools | Pointer, Draw, Paint, Erase, Slice, Mute, Velocity, Range | **One palette for the focused pane**, now on the right next to undo. Tools that don't apply to the focused pane are greyed (Range in the notes pane, Paint/Velocity in the timeline) |
| Right | ↶ ↷ │ Follow (icon toggle; right-click or hold for Page / Scroll), Inspector, **?** (help overlay, now visible), ✕ | Follow is no longer the biggest button on screen |

### 3.2 Track headers

`▸ ① Name ……… [●] M S` on line one, `▶ Verse A · ◉ Track Out` on line two ([E1](mockups/E1-editor-dark.png)).

- **▸ / ▾ chevron** opens the track's automation lanes (was the "A" button).
- **① tile**: the track's colour square with its number. The same tile is on its Track Out display and in the
  panel's screen, so editor and rack visibly match.
- **Arm ●** shows only while armed or hovered (arm is also in the record caret and the track menu). M and S stay.
- **Line two** says what plays and where it goes: the output jack glyph is filled when an output module plays
  this track, hollow when nothing does. **Clicking a hollow one offers "Add Track Out beside Tessera"** (or Drum
  Out for drum tracks), one undo step.
- Voices ("4 voices (auto)") moves to the track menu's Voices item and the Track Out display.
- Drum tracks show a small drum glyph after the name (was an unreadable "|||" / oval).

### 3.3 Clip shelf (launcher)

- Scenes are columns, named in the ruler row ("▶ 2 Verse", tempo badge "128"). "+ Scene" at the end.
- **Slot states use shape and words, not only colour** (see DESIGN_LANGUAGE §6): empty = recessed well (the stop
  square appears on hover); stopped = tinted tile with ▶; playing = solid tile, ▶ and a progress bar; queued =
  dashed gold outline and "queued"; recording = red tab with ● and "recording".
- Header (narrow): launch quantize ▾, Stop all ■, Record into timeline ●. Header (wide, Perform): the same with
  words, quantize as a segmented control, plus "Back to timeline (all)" when needed.
- The shelf's right edge has a grip: drag to widen into Perform, double-click to toggle (F2 does the same).
- Gestures are today's (DECISIONS §18, §33): triangle launches, click edits, double-click creates, drag to the
  timeline places a linked instance, Alt/Ctrl copies.

### 3.4 Timeline

- **One 28 px ruler** with the loop band and marker tags inside it; the overview bar becomes the horizontal
  scrollbar (drag to scroll, drag its ends to zoom, double-click to fit). Same in the piano roll. Saves ~30 px in
  every pane.
- **Clips are tiles; each loop repeat is its own tile** joined by a grout seam, so repeats are obvious without
  notch marks. The first pass is stronger, repeats softer. Name inside the tile, link badge top right.
- **"Timeline ▾" menu** holds Ripple, Stack, Automation follows clips, Record into timeline. (What plays is the
  Plays switch, not a menu item.) A small badge next to the title shows modes that change edits: "Ripple" (accent) and
  "Stack" (neutral).
- Snap ▾ on the right of the timeline header (was in the corner tools), then the pane's maximise button.
- A track that left the timeline dims its lane and shows **↺ Back to timeline** in it.

### 3.5 Notes pane

Header, left to right ([E1](mockups/E1-editor-dark.png)):

| Control | Replaces |
|---|---|
| ‹ › previous / next track | same |
| **Clip chip** `■ Keys · Verse A  2 bars ▾`: menu with rename, length (1/2/4/8/16 bars, Double, Halve), loop on/off, fit loop to notes, clip key (song or own), launch quantize override, export, save as clip file | the "8 bars" button, clip key from the key button, clip items scattered in menus |
| **Devices 2** chip: opens the device strip (F3); hover names the chain | the Devices button *and* the closed 24 px chain bar (two controls for one thing) |
| **Snap: 1/16 ▾** | "Adaptive" button + its ≡ grid-density icon |
| **C minor ▾** then **Snap** and **Fold** as words | key button + three look-alike icons; Highlight moves into the key menu (on by default) |
| **Chord ▾** | chord stamp icon |
| **View ▾**: ghost notes (source), operator badges, hover band | ghost icon, badges icon |
| **Notes ▾**: transforms (quantize, humanize, strum, arpeggiate…) and Select ▸ | ⋯ |
| 🔈 hear notes while editing | same |
| *(tools leave: one palette in the transport)* | the seven tool icons |

- **Opening a clip always scrolls to its notes** (fixes the S1 in AUDIT 3.5).
- **Device strip**: opens by itself only when the track has devices; a new song never shows an empty device box.
- **Lower lane**: tabs grouped *Note values* (Velocity, Release, Chance, Repeats, Micro) │ *Expression* (Pitch,
  Timbre, Pressure) │ *Automation ▾*. When the pane is too narrow, the tabs collapse into one "Velocity ▾"
  picker in the header. When the pane is too short, the lane hides first (height budget).
- **Keyboard** ([E5](mockups/E5-keyboard-dark.png)): a real piano lined up with the note rows. Behind the black
  keys a white key is one row tall; in front of them it widens to meet its neighbours halfway, as on a real piano.
  Black keys are raised (soft shadow, light front edge); white keys have a front lip. Keys being played light up
  in the **track colour** (was the accent). Root keys get a small accent mark at the front. C keys are always
  named; from 15 px rows every white key is named. With **Fold** on, the piano shape would lie, so each row
  becomes a named key (light for naturals, dark for sharps and flats).

### 3.6 Inspector

- Shows **only the selection**: notes, or steps in the drum grid ([E1](mockups/E1-editor-dark.png),
  [E3](mockups/E3-drums-dark.png)). Title "3 notes" with a chord badge when the notes form one ("Eb maj").
- Fields: Start and Length in **bar.beat.16th, matching the ruler** (today "0 beats – 28 beats"), Pitch, Velocity
  (its chart open by default), Release, Micro-timing.
- **Plays** section (things that differ from pass to pass), in words: **Chance**, **Repeats**, **Only when…**
  (was Occurrence), **Every … passes** (was Recurrence). A lit chip carries its value; drag it sideways to change.
- Footer: one quiet line, "12 notes · C3–G4 · used 2×". The read-only TRACK and CLIP blocks are removed.
- Nothing selected: "Select notes to edit them · Ctrl+A selects all".

### 3.7 Drum editor ([E3](mockups/E3-drums-dark.png), [W3](wireframes/W3-drum-editor.png))

- Same header pattern as the piano roll: clip chip, Devices, **Kit ▾**, **Steps | Piano roll**, Snap ▾.
- Lane row: colour bar, name, **output chip "→3"** (tooltip: "plays on Drum Out jack 3"), M, S. Lanes on outputs
  9–16 have a dashed chip "→10" and a divider "Lanes 9–16 play on a second Drum Out (bank 9–16)" (see
  [OUTPUTS.md](OUTPUTS.md)). A polymeter lane shows its own step count as a tag ("12") and its repeats faded.
- Lane tools stay in the lane's ⋯ menu (Euclidean, rotate, randomize, copy…), not as more row buttons.
- Steps: filled to their velocity; selection outline in the accent; repeats drawn as split cells; chance as a
  corner notch; muted lane hatched.
- The velocity strip stays under the grid.

### 3.8 Automation

- Lane header (two lines): name and value in volts on line one; "AUTO 1 · ↗ VCF Freq" on line two; arm and mute
  on hover; the lane menu as today. The menu's 20 items stay but group under headers Output / Recording / Points
  / Knobs.
- "Map a knob" stays the entry point (it works well); the mapped knob's name appears in the lane header.
- Later: the maximised automation view (A), "show used lanes" (Shift+A), dashed combined curve.

### 3.9 Tessera panel ([P1](mockups/P1-panels-light.png), [P5](mockups/P5-screen-options-dark.png), [W4](wireframes/W4-panel.png))

Detailed options in [PANEL_AND_LIBRARY.md](PANEL_AND_LIBRARY.md). The recommended panel, 24 HP:

- **Preview screen** (option C in [P5](mockups/P5-screen-options-dark.png)), three bands:
  - top line: instance name, LOOP and what plays ("▶ TIMELINE" or "▶ CLIPS");
  - **the song**, following the PLAYS switch: when the timeline plays, a mini timeline (tracks down, clips as tiles
    with their notes, loop band, playhead); when clips play, the clip mosaic (tracks down, scenes across, lit when
    playing, dashed gold when queued, red when recording, the scene LAUNCH would start framed);
  - **the selected track's notes** as a tiny piano roll; notes under the playhead light up;
  - bottom line: position, the selected track and clip, BPM.
  With no module (Library, browser) it draws the demo song. So at thumbnail size the panel already says
  "arranger + clips + piano roll".
- Rejected: song only (A, no piano roll) and notes only (B, hides the song).
- **Transport**: PLAY, REC and LOOP larger and lit; RESET and TAP smaller; TEMPO knob with an accent arc.
- **Open editor**: the one accent button. **Edge tab** on the right edge: drag it (or click) to open the editor
  inside the panel.
- **Scene group** on a recessed plate: PREV, NEXT, LAUNCH, STOP ALL, FILL; then the **PLAYS** switch (two lit
  halves, CLIPS / TIMELINE; was ARRANGE) and **CAPTURE**.
- **Jacks**: inputs on the face (CLOCK RESET RUN SCENE LAUNCH FILL — FILL gets its label); the record inputs in a
  red-outlined **REC** group (V/OCT GATE VEL AUTO); outputs on the inverted plate (CLOCK RESET RUN / EOC BAR
  CLICK).
- Param and jack IDs don't change (only positions and drawing), so saved cables and mappings stay.

### 3.10 Output modules ([P1](mockups/P1-panels-light.png), [OUTPUTS.md](OUTPUTS.md))

- One display style for all three: dark screen, a strip in the track colour, the **track tile** (number), name,
  source instance.
- **Drum Out** gets Auto Out's best idea: lane names on a screen beside their jacks, TRIG and VEL as two clear
  columns, poly TRIGS / VELS / AUTO at the bottom, and a **bank switch (Lanes 1–8 / 9–16)**.
- Track Out: "TIMBRE" spelled out.

### 3.11 Colour schemes ([D2](mockups/D2-colour-schemes.png))

The review found the dark theme too deep and too contrasty, liked the light one, and asked for schemes.

- **One menu**: Main ▾ › Colour scheme: **Follow Rack** (default), Plaster, Sand, Slate, Walnut. Follow Rack
  uses Plaster when Rack prefers light panels and Slate when it prefers dark ones.
- Every scheme is a full set of the same tokens (ground, canvas, surface, raised, lines, text, accent, states), so
  every view follows it with no special cases. Values come from `mockups/gen/colors.py`, which also prints the
  contrast table.

| Scheme | Canvas | Text on canvas | Accent | For |
|---|---|---|---|---|
| **Plaster** | #FDFBF9 | 16.7:1 | lapis | light default, unchanged |
| **Sand** | #F3E6D4 | 13:1 | blue | light, warmer, less glare |
| **Slate** | #292B30 | 10.2:1 | lapis | **new dark default**: soft grey, not black (was #15181E at 15.6:1) |
| **Walnut** | #362B24 | 10.2:1 | blue | warm dark, brown-grey; same softness as Slate, different temperature |

- Two light and two dark schemes. The pairs differ in temperature, not in depth: the review rejected a mid grey
  (Stone) and a darker copy of Slate (Night) as not meaningful.
- Secondary text stays at 4.5:1 or more in every scheme; accent hues avoid the state hues (jade play, gold queue,
  red record), so "playing" never looks like "selected".
- **Track colours** are the same 16 hues everywhere, tuned once for dark and once for light canvases.
- **Module panels** follow the scheme too (changed after review): with Follow Rack they follow Rack's light/dark
  panel setting like other modules; a picked scheme colours the Tessera panels, jacks and screws included. The
  **screens** stay dark in every scheme, lifted from #080B10 to #191C21.

---

## 4. Gestures and shortcuts

The vocabulary is sound (EDITOR_UX §2, DECISIONS §12, §21, §41). Changes:

| Topic | Today | Proposed |
|---|---|---|
| F2 | Launcher grid ↔ arranger | Widen / fold the clip shelf (Perform); does not change what plays |
| F11 / double-click splitter | Maximise a pane | Same, and now also a visible maximise / restore button in each pane's corner |
| Tools | Two palettes, same state | One palette; keys unchanged (S, D, P, E, C, T, Y, Z, Alt+R) |
| Alt | Free placement; on clips: slip and stretch; in empty arranger space: range; on drum steps: velocity; on automation segments: insert point | Keep (each is established), but **show a hint while a modifier is held**: a one-line label at the bottom-left of the pane ("Alt: no snap · on a clip edge: stretch"). Later |
| Right-click in Draw | Deletes in the piano roll, opens a menu in the arranger | Keep (a common habit), and say it in the Draw tool tooltip; the setting "Right-click in Draw: delete / menu" moves to the instance menu |
| Help | Shift+/ only | Visible **?** in the transport; tooltips keep showing shortcuts |
| Record settings | Right-click on ● | Caret ▾ beside ● |

No shortcut changes meaning except F2. The help overlay is restyled with the new tokens; its content is generated
from the table as today.

---

## 5. Simplification table: today → proposed → where the function went

| Today | Proposed | Where the function went |
|---|---|---|
| Transport "Arranger / Launcher" (play mode) | **Plays: Clips \| Timeline** switch | Same param as the panel's PLAYS; the playing section is highlighted (§2.2) |
| Transport "Show launcher / Show arranger" | removed | Clips and timeline always visible; F2 or the shelf edge widens the clips (Perform) |
| Full launcher grid (tracks as columns) | removed | The shelf, widened (Perform). Scenes, launch, stop, record-into-slot, folded group (now a row) all there |
| Launcher panel toggle (corner icon) | removed | The shelf is always present; drag its edge to resize or fold it to zero |
| Launcher toolbar "Launch: 1 bar · Stop all · ● To arrangement · Capture" | shelf header | Launch ▾, Stop all, "● Record into timeline" (renamed); Capture → transport |
| "Back to arr." | "↺ Back to timeline" | In the lane of each track that left; "(all)" in the shelf header |
| Arranger corner tools (Pointer…Range) | removed | Transport tool palette |
| Corner Ripple / Stack / Automation follows clips | "Timeline ▾" menu | With a badge while Ripple or Stack is on |
| Corner "+" (add track) | "+" in the Tracks header and "+ Add track" under the last track | — |
| Piano-roll tool icons | removed | Transport tool palette |
| "8 bars" length button | Clip chip ▾ | Length, Double, Halve, loop, fit, clip key |
| Devices button + closed chain bar | Devices chip | Chain names on hover; strip opens with F3 or the chip |
| "Adaptive" + ≡ density icon | Snap ▾ | Grid density lock inside the Snap menu |
| Key button + Highlight / Snap / Fold icons | Key ▾ + "Snap" "Fold" words | Highlight in the key menu (on by default) |
| Ghost icon, badges icon | View ▾ | Ghost source, badges, hover band |
| ⋯ transforms | Notes ▾ | Same list |
| 9 lane tabs + Automation ▾ | Grouped tabs; one picker when narrow | All lanes kept |
| Inspector TRACK block (Name, Type, Voices, State, Output) | removed | Header name, drum glyph, track menu (Voices), state line, output jack glyph |
| Inspector CLIP block (Name, Length, Loop, Key, Notes, Range, Used) | footer line + clip chip | Clip chip (editable), footer (facts) |
| Operators "Occurrence", "Recurrence" | "Only when…", "Every … passes" | Same settings |
| Track header "A" | ▸ chevron | Opens automation lanes |
| Track header "|||" / oval | drum glyph for drum tracks | — |
| Track header green dot | jack glyph (filled / hollow) | Click hollow: add an output module |
| "4 voices (auto)" line | track menu, Track Out display | — |
| Launcher column headers' arm circle and stop square | removed | Track header arm; shelf stop squares per slot / Stop all |
| "Follow: Page" big button | icon toggle | Page / Scroll in its menu |
| "File" button | Main ▾ instance menu | Same items + rename, size, theme |
| Record right-click menu | ● ▾ caret | Same items |
| (no control) Song swing | Swing field in the transport | Engine and export already use it |
| Panel ARRANGE | Panel PLAYS (CLIPS / TIMELINE) | Same param |
| Panel EDITOR (small) | "Open editor" (accent) + edge tab | Edge tab opens the in-panel editor |
| Panel record inputs V/OCT GATE VEL AUTO | same jacks in a "REC" group | AUTO labelled inside REC |
| Panel editor strip Launcher / Arranger / Both / Piano roll | removed | Clips beside the timeline in the Song pane; maximise / restore button on each pane |
| Theme: Follow Rack / Dark / Light | Colour scheme: Follow Rack / Plaster / Sand / Slate / Walnut | Main ▾ menu (§3.11) |
| Module menu "Panel editor: Off / 48 / 80 / 120 HP" | stays | Plus the edge tab on the panel |

Net: about 25 visible controls fewer in the full editor at rest, no function removed.

---

## 6. Onboarding and empty states

The first five minutes ([W8](wireframes/W8-first-run.png)):

1. **Library / browser**: the light panel shows a colourful mosaic and "Open editor" (PANEL_AND_LIBRARY.md).
2. **Added to the rack, no output module**: the panel screen's bottom line reads "No output yet · add a Track
   Out". PLAY still runs the clock.
3. **First editor open**: the empty timeline holds a card, not a pop-up: *"Make a sound in three steps: 1. Give
   Track 1 an output [Add Track Out] 2. Patch V/OCT and GATE into a voice 3. Press Space"*. It goes away by itself
   once any track has an output module, and has "Hide".
4. Every **hollow output glyph** offers the same "Add Track Out / Drum Out" action.
5. Empty states say what to do, where the eye already is:
   - empty clip: "Draw with D · play notes with Ctrl+K · step input Ctrl+R"
   - empty timeline lane: "Drag a clip here from the shelf, or draw one with D"
   - empty slot on hover: "Double-click: new clip"
   - inspector: "Select notes to edit them · Ctrl+A selects all"
   - device strip (when opened empty): "Devices change notes on their way out: arpeggiator, chord, echo… [Add]"
   - automation lane: "Click to add a point, or draw with D" (exists)
6. **? in the transport** opens the help overlay (exists, now discoverable).

---

## 7. Words

One word per thing, used everywhere (UI, tooltips, help, manual):

| Use | Not |
|---|---|
| **Timeline** | arranger, arrangement, "arr." (the File menu keeps "Export … arrangement" where the DAW term helps) |
| **Plays** (the switch: Clips or Timeline) | play mode, mode |
| **Maximise / Restore** (a pane) | zoom, focus mode |
| **Clips** (the shelf), **clip slot** | launcher, launcher panel, session |
| **Scene** (a column of clips launched together; explained in its tooltip) | — |
| **Perform** (the widened shelf) | launcher grid |
| **Notes pane** | detail pane, detail editor |
| **Output module** (Track Out, Drum Out, Auto Out) | output, expander |
| **AUTO 1–8** (an automation output channel) | auto, lane output |
| **Lane** only for drum lanes and automation lanes; the lower area is the **lower lane** | — |
| **Back to timeline** | Back to arr. |
| **Record into timeline** | To arrangement |
| **Capture** ("keep what you just played") | — |
| **Only when…**, **Every … passes** | Occurrence, Recurrence (the model's names) |
| **Voices** (Auto or 1–16) | polyphony |

Rules: sentence case for labels ("Open editor"), caps only for panel labels and captions; verbs for actions;
no abbreviations that need a manual ("REL" stays on the jack because it is the Rack convention, and its
tooltip says "Release velocity").

---

## 8. Must-do for release vs later

**For release** (they fix S1 issues or the first impression):

1. Theme tokens as colour schemes (Plaster, Sand, Slate, Walnut; Follow Rack by default), new type, lifted
   dark panels and screens.
2. Tessera panel redesign + Library preview (song + notes preview screen, PLAYS switch, grouped controls, REC
   group, inverted output plate, edge tab, Open editor); Track Out / Drum Out / Auto Out restyle; Drum Out lane
   names.
3. Transport: Plays switch with the playing section highlighted; remove the view button; per-track state line +
   Back to timeline; Swing; Main ▾ menu; record caret; Capture; ? button; Follow as an icon. Maximise / restore
   buttons on the Song and Notes panes (full and in-panel editor); remove the panel editor's view switch.
4. One launcher: the shelf (with Perform by widening); remove the tracks-as-columns grid.
5. One tool palette in the transport; Timeline ▾ menu.
6. Notes header regrouped (clip chip, Snap/Fold words, View ▾, Notes ▾); scroll-to-notes fix; device strip closed
   unless the track has devices.
7. Inspector: selection only, bar.beat positions, "Plays" wording.
8. First-run card and "Add Track Out" from hollow output glyphs.
9. Words (§7) across UI, tooltips and help.

**Soon after release:**

- Drum Out bank switch (lanes 9–16) — small; could go into the release if time allows.
- Single 28 px ruler with the overview as scrollbar.
- Loop repeats as separate tiles; clip tile restyle.
- Lower-lane tabs collapsing into a picker.
- Modifier hint line.
- Automation lane header two-line layout and grouped lane menu.

**Later:**

- Clickable mosaic on the panel (launch clips from the panel screen when cells are big enough).
- Maximised automation view, used-lanes filter.
- Theme-aware custom cursors (pencil, eraser, scissors).

---

## 9. What I kept from the earlier, interrupted work

**UX wireframes** (now in [wireframes/](wireframes/)):

| File | Verdict | Why |
|---|---|---|
| W1 editor | **Adopt, with two changes** | Its structure is this proposal's. Changed: the play mode is kept as the Plays switch instead of removed (no lost function, CV mapping survives); Capture is an icon with a "something to keep" dot rather than a text button |
| W2 perform | **Adopt** | Exactly the "widen the shelf" idea |
| W3 drum editor | **Adapt** | Added the second-bank lanes (9–16) and the polymeter tag |
| W8 first run | **Adopt** | The card in the empty timeline and the hollow output dots |
| W4–W7 (planned, never made) | **Made new** | [W4 panel and Library](wireframes/W4-panel.png), [W5 in-panel editor height budget](wireframes/W5-panel-editor-budget.png), [W6 output modules](wireframes/W6-outputs.png) |

**UI mockups** (moved to [archive/first-ui-pass/](archive/first-ui-pass/)): **adapted, then replaced.** Kept the
ideas that work: panes as tiles on a grout background, outputs on an inverted plate, the "mosaic" name and a
cool-tinted dark neutral. Replaced: their layout followed today's structure (Launcher/Arranger switch, two tool
palettes, launcher grid with tracks as columns), the indigo accent was used for many things at once, and the
display face (Unbounded) isn't bundled. The new mockups follow this proposal's layout and the token set in
DESIGN_LANGUAGE.md. Their generator was the starting point for [mockups/gen/](mockups/gen/).
