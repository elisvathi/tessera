# Tessera design audit

What works today, what doesn't, and how bad it is. Every finding has a fix; the fixes are worked out in
[PROPOSAL.md](PROPOSAL.md) (experience) and [DESIGN_LANGUAGE.md](DESIGN_LANGUAGE.md) (look).

Checked on the build of 6 Oct 2026 (branch `design-refresh`), in a private Rack: the user's patch, a fresh Tessera
and the module browser. Screenshots are in [current/](current/).

**Severity**
- **S1 – blocks or misleads.** A new user gets stuck, or the screen says something false.
- **S2 – slows everyone down.** Clutter, duplicates, hidden features, inconsistent gestures.
- **S3 – polish.** Looks dated, uneven spacing, weak contrast in a corner.

---

## 1. What works and must stay

These are real strengths. The redesign keeps them as they are, or makes them easier to find.

- **The editing engine.** One gesture = one undo step, Esc cancels, live previews, absolute snap, drag readouts,
  edge auto-scroll. It feels like a real DAW.
- **The shortcut table and the help overlay** (`?`): generated from one table, grouped, with mouse modifiers
  ([12](current/12-help-overlay.png)). Rare even in commercial DAWs.
- **The inspector chart** (mean / spread / chaos, "what you see is what plays") — agreed with the user, clear.
- **Transforms with live preview** ([06](current/06-transform-menu.png)), grouped Timing / Pitch / Values.
- **Output routing from anywhere** with clear "No source" / "Track missing" states, follow-selected mode,
  paste remapping.
- **The in-panel editor** ([18](current/18-panel-editor-both.png)): nothing else in Rack does this. It is the
  module's best selling point and today nobody sees it until they drag the edge.
- **Auto Out's lane screen** ([19](current/19-module-browser-previews.png)): lane names next to their jacks. The
  best panel idea in the family; the other output modules should borrow it.
- **Plain-language menus** ("Map a knob: touch it in the rack", "Recording (while the transport records)").
- **Track colours flow into clips** (DECISIONS §34): one colour per track everywhere.
- **Empty-state hints** exist ("Click to add a point, or draw with D").

---

## 2. The big problems (cross-cutting)

### 2.1 Two "launcher vs arranger" switches side by side — S1
[02](current/02-full-editor-default.png), [08](current/08-launcher-grid.png)

The transport has an orange **"Arranger"** button (what *plays*: the play mode) right next to **"Show launcher"**
(what you *see*: the top pane). Both name the same two words. Pressing the wrong one silences the song or does
nothing audible. The panel adds a third (ARRANGE button) and the panel editor a fourth ("Launcher | Arranger |
Both | Piano roll").

**Fix:** one Song view where both always show (clips beside the timeline), and one clear **Plays: Clips |
Timeline** switch that also highlights the section you hear. Each track still says what it plays ("Timeline",
"▶ Verse", "Stopped"). See PROPOSAL §2.2 (revised 12 Oct after review).

### 2.2 Three clip launchers — S2
[08](current/08-launcher-grid.png), [13](current/13-arranger-with-side-launcher.png),
[18](current/18-panel-editor-both.png)

The same slots appear in the full launcher grid (tracks are **columns**, scenes are rows), the launcher panel
beside the arranger (tracks are **rows**, scenes are columns) and the panel editor's launcher. The grid turns the
song 90° when you switch, so your eye has to find every track again.

**Fix:** one clip grid, tracks always rows. Widen it for performing (drag its edge or F2); it never rotates.

### 2.3 Two tool palettes, and modes that look like tools — S2
[14](current/14-arranger-corner-tooltips.png), [04](current/04-piano-roll-maximized.png)

The arranger corner has its own tools (Pointer, Draw, Erase, Slice, Mute, Range) *and* three orange toggles that
look like tools but are settings (Ripple, Stack, Automation follows clips), plus "Launcher panel" and "+". The
piano roll header repeats the tools. They share one state (DECISIONS §21), so the user sees the same thing twice
and can't tell a tool from a mode until they hover.

**Fix:** one tool palette, in the transport, acting on the focused pane. Ripple / Stack / Automation follows
clips move into a "Timeline ▾" menu; Ripple shows a badge while on.

### 2.4 Accent orange everywhere, so nothing stands out — S2 (and it reads as another DAW)

Orange marks: play mode, loop, Follow (a big orange button for a minor setting), the active tool, scale toggles,
the speaker, the inspector toggle, the clip loop bar, the velocity tab underline, the panel LOOP light. With a
dark grey ground and flat colour blocks, the whole editor reads as a copy of another DAW.

**Fix:** a new palette where the accent is rare (focus, selection, the one primary button). See
DESIGN_LANGUAGE §2.

### 2.5 No light theme; the VCV Library will show the panel in its light variant — S1 for release
`Theme.cpp`: "Dark only for now". Rack's own module screenshots (the Library page) are rendered with
`preferDarkPanels` **forced off** and with no module (`createModuleWidget(NULL)`, Rack `Window::screenshotModules`).
So the Library shows the *light* panel, without the in-panel editor. Tessera has no light panel, so it will show
its dark one, which is fine today but breaks the moment light panels exist unless they are designed as the hero.

**Fix:** design light panels as the Library image. See PANEL_AND_LIBRARY.md.

### 2.6 Hidden or missing features — S2
- **Song swing has no control.** The engine plays it and MIDI export uses it (DECISIONS §30), but nothing in the
  UI sets `Song::swing`. Only the quantize dialog has a swing value. *(S1: a feature nobody can reach.)*
- **Capture** (keep what you just played) lives only in the launcher toolbar, the record button's right-click
  menu and Ctrl+Shift+R. It disappears with the launcher.
- **Record settings** (count-in, input quantize, new clip length) are a right-click menu on the record button
  ([17](current/17-record-menu.png)). Right-click on a transport button is not discoverable.
- **Track settings** (voices, input, MIDI channel, monitor) only in the track's right-click menu
  ([11](current/11-track-menu.png)).
- **The in-panel editor** is reached only by dragging the panel's right edge, or a module-menu item ("Panel
  editor: Off / 48 / 80 / 120 HP"). Nothing on the panel says it exists.

### 2.7 Inconsistent words — S2
The same thing has several names: *arranger / arrangement / timeline / "arr."*; *launcher / clip launcher /
launcher panel / session*; *lane* (drum lane, automation lane, lower lane); *output* (output module, AUTO output,
lane output number); *Auto* (Auto voices vs AUTO output vs automation). "Back to arr." is truncated jargon.

**Fix:** a short word list (PROPOSAL §7).

---

## 3. Surface by surface

### 3.1 Transport bar — [02](current/02-full-editor-default.png)

| What | Problem | Sev | Fix |
|---|---|---|---|
| Play mode "Arranger" + "Show launcher" | Same words, different meaning (2.1) | S1 | Remove both; per-track state + one Song view |
| "Follow: Page" | Biggest, brightest button for a minor setting | S2 | Icon toggle with a menu (Off / Page / Scroll) |
| "File" text button among icons | Odd one out; rename, theme, size live elsewhere | S3 | Instance menu "Main ▾" holds File, rename, editor size, theme |
| No swing | 2.6 | S1 | Swing field beside tempo |
| Position "1.1.1" in an empty pill | Looks like a text field; no click action | S3 | Clickable position (click to type a bar to jump to) |
| Record settings on right-click | 2.6 | S2 | Small ▾ beside record, plus Capture button |
| Collapses on narrow windows by hiding the name, key, undo | Hides things silently | S3 | Fold song settings into "Song ▾" instead of hiding them |

### 3.2 Track headers — [02](current/02-full-editor-default.png), [13](current/13-arranger-with-side-launcher.png)

| What | Problem | Sev | Fix |
|---|---|---|---|
| Name truncated ("1 CHOR…") | Five buttons on every row eat the name | S2 | Name gets the room; arm on hover/when armed |
| "A" button | Cryptic letter for "show automation lanes" | S2 | Disclosure chevron ▸ left of the name |
| "|||" / oval glyph | Track type icon nobody can read | S3 | Drum tracks get a small drum glyph; instrument none |
| "4 voices (auto)" | Useful but uses a whole line | S3 | Second line says what plays + where it goes ("▶ Verse · → Track Out") |
| Green dot | Means "an output module plays this" but looks like "playing" | S2 | Jack-ring glyph: filled = connected, hollow = none; click a hollow one to add a Track Out |

### 3.3 Clip launcher grid — [08](current/08-launcher-grid.png)

| What | Problem | Sev | Fix |
|---|---|---|---|
| Tracks as columns | Rotates the song vs the arranger (2.2) | S2 | Tracks as rows everywhere |
| Toolbar "Launch: 1 bar · Stop all · ● To arrangement · Capture" | Four unrelated things as flat text; "To arrangement" sounds like navigation, it is recording | S2 | Shelf header: Launch quantize ▾, Stop all, "● Record into timeline"; Capture moves to transport |
| Empty slot = tiny square | Reads as "stop" (it is a stop button) — empty and stop look the same | S2 | Empty slot is a recessed well; the stop square shows on hover |
| Scenes "1…8" | Scene names hidden unless renamed | S3 | Scene row shows names, tempo badge |
| Arm circle and stop square per column header | Duplicate of track header controls | S3 | Removed (rows share the track header) |

### 3.4 Arranger — [13](current/13-arranger-with-side-launcher.png), [15](current/15-arranger-automation-lane.png)

| What | Problem | Sev | Fix |
|---|---|---|---|
| Corner tools | 2.3 | S2 | One palette; "Timeline ▾" menu |
| Two rulers' worth of strips (marker row, loop strip, ruler, overview: 54 px) | Heavy top; repeated in the piano roll | S3 | One 28 px ruler: loop band and markers inside it, overview becomes the scrollbar |
| Clip header bar in full track colour | A borrowed look; competes with the notes | S3 | Name inside a tinted tile; loop repeats drawn as separate tiles with a grout gap |
| "Back to arr." | Truncated jargon | S2 | "Back to timeline", only while a track left it |
| Automation lane header ("Auto 1 · AUTO 1 · 0.00 V", arm, M) | OK but cramped | S3 | Same content, two lines, value in mono |

### 3.5 Piano roll — [04](current/04-piano-roll-maximized.png), [03](current/03-full-editor-notes-out-of-view.png)

| What | Problem | Sev | Fix |
|---|---|---|---|
| Opens away from the notes ([03](current/03-full-editor-notes-out-of-view.png): melody opens at F#2, every note off screen) | Breaks "opening a clip scrolls to its notes" (EDITOR_UX §0.4) | S1 | Scroll to the notes on open, always |
| Header: ‹ › Devices ■ 8 bars, 7 tools, Adaptive, ≡, key, 3 scale icons, ⋯, 2 more icons, speaker | 19 controls, half of them unlabeled icons; scale toggles are three look-alike glyphs | S2 | Clip chip ▾ (name, length, loop), Devices chip, Snap ▾, Key ▾ with "Snap" and "Fold" as words, Chord ▾, View ▾ (ghost, badges), Notes ▾ (transforms), speaker. Tools leave (one palette) |
| Lower lane: 9 tabs + Automation ▾ | Wide; overflows in the panel | S2 | Tabs while they fit, one "Velocity ▾" picker when they don't |
| Keyboard keys are grey slabs, white keys look disabled | Hard to read pitch; C rows unmarked | S3 | Real key drawing, C labels, root tint |
| Note labels on every note | Busy at normal zoom | S3 | Labels only when the note is wide enough (already the rule; tune thresholds) |

### 3.6 Inspector — [05](current/05-inspector-notes.png), [02](current/02-full-editor-default.png)

| What | Problem | Sev | Fix |
|---|---|---|---|
| TRACK and CLIP blocks of read-only text (Name, Type, Voices, State, Output, Loop, Key, Notes, Range, Used) | Repeats the header; can't edit anything; takes half the column | S2 | Inspector shows the selection only. Clip facts become a one-line footer ("12 notes · C3–G4 · used 2×") |
| Operators as four equal buttons (Chance, Repeats, Occurrence, Recurrence) | "Occurrence" / "Recurrence" are jargon | S2 | Words: "Chance", "Repeats", "Only when…", "Every … passes" (plays heading) |
| Start/Length show "0 beats – 28 beats" | Beats count from 0 while the ruler counts bars from 1 | S3 | Bar.beat positions matching the ruler (1.2.1 – 3.4.3) |
| Nothing selected | Shows the track/clip blocks | S3 | Empty state: "Select notes to edit them · Ctrl+A selects all" |

### 3.7 Device strip — [21](current/21-first-run-editor.png)

| What | Problem | Sev | Fix |
|---|---|---|---|
| Open by default on a new song, as an empty "Add device" box | Eats ~130 px above the first notes a new user sees | S2 | Open only when the track has devices; otherwise just the "Devices" chip |

### 3.8 Drum editor — [09](current/09-drum-steps.png)

| What | Problem | Sev | Fix |
|---|---|---|---|
| Lane rows "Kick +1 M S" | "+1" is the output number, unexplained | S2 | Output chip "→1" with tooltip "plays on Drum Out jack 1"; "→12 · lanes 9–16" for the second bank (OUTPUTS.md) |
| Steps are flat pink blocks; velocity as height only | OK, but on/off steps hard to tell at low velocity | S3 | Step tile: filled to velocity, full outline when on |
| Velocity strip under the grid | Good. Keep | — | — |
| "Kit" and "Steps | Piano roll" in the middle of the header | Fine; align with the piano roll header pattern | S3 | Same header pattern as piano roll |

### 3.9 Tessera panel (24 HP) — [20](current/20-first-run-panel.png), [22](current/22-tessera-and-drum-out.png)

| What | Problem | Sev | Fix |
|---|---|---|---|
| 11 identical round buttons, labels 7.5 px | No hierarchy: PLAY looks like STOP ALL | S2 | Group: transport, scenes, mode; PLAY and REC larger and lit |
| Record inputs labelled V/OCT GATE VEL AUTO | On a sequencer they read as outputs; nothing says "record" | S2 | "REC" group title on the plate; AUTO → "AUTO REC" |
| CLOCK/RESET/RUN as inputs and outputs with the same labels | Only a slightly lighter plate tells them apart | S2 | Outputs on an inverted (light-on-dark / dark-on-light) plate, Rack convention |
| FILL jack has no label | — | S3 | Label "FILL" |
| "EDITOR" small grey button | The module's main feature looks secondary; the in-panel editor is invisible | S1 (for adoption) | Big "Open editor" + a visible edge tab that widens the panel |
| Display: name, 1.1.1, scene number, 120.0, numbered track cells | Reads as a clock module, not a sequencer | S2 | Mosaic display: a live tile per clip (tracks × scenes), playing/queued visible (PANEL_AND_LIBRARY.md) |
| Browser/Library preview | A dark transport panel with four grey squares; says nothing about piano rolls or clips | S1 (for adoption) | Library preview draws the demo song's mosaic; light panel is the hero |

### 3.10 Output modules — [19](current/19-module-browser-previews.png), [drum-out](current/drum-out.png)

| What | Problem | Sev | Fix |
|---|---|---|---|
| Track Out display: source, big number, name | Clear. Keep; use the track's tile (colour square with number) | S3 | Track tile + name, same tile as in the editor |
| "TIMB" truncated label | — | S3 | "TIMBRE" fits at 6 HP with a smaller tracking |
| Drum Out: 16 tiny jacks labelled 1 V1 2 V2 … | No lane names; trigger and velocity jacks look the same; lanes 9–16 only on poly | S2 | Lane-name screen like Auto Out; TRIG and VEL columns; lane bank 1–8 / 9–16 (OUTPUTS.md) |
| Three modules with AUTO poly jacks + Auto Out | Fine, but AUTO on Track Out / Drum Out is rarely used and costs a jack | S3 | Keep for compatibility (patches use it) |

### 3.11 Menus — [07](current/07-note-menu.png), [11](current/11-track-menu.png), [16](current/16-automation-lane-menu.png)

Menus are Rack's own. Wording is good. Problems: long track menu mixes structure (add, delete, group) with
settings (voices, record from); the automation lane menu has 20 items. **Fix (later):** section headers already
exist; keep, but move per-track settings into the track inspector (PROPOSAL §3.4) so menus hold actions.

---

## 4. Main workflows

Each walk-through starts in a fresh patch (fresh.vcv) unless noted.

| Workflow | What happens today | Friction | Sev |
|---|---|---|---|
| **First run** | Add Tessera from the browser: dark 24 HP transport panel. Press PLAY: nothing (no output module). Open editor: launcher grid, empty device box, demo clip, inspector of read-only text ([21](current/21-first-run-editor.png)) | Silence with no explanation; the first screen is mostly empty boxes | S1 |
| **Make a beat** | Select Drums column, step grid shows. Click steps; good. Patch: Drum Out has 16 unlabeled jacks | Which jack is the snare? | S2 |
| **Write a melody** | Click a track; piano roll may open away from notes (03). Draw with D; good | Scroll hunt; tool choice duplicated | S1/S2 |
| **Arrange** | F2 / "Show arranger", then drag clips from the side launcher; switch play mode to Arranger (else silence) | Must know the mode; launcher rotates | S1 |
| **Launch scenes live** | Launcher grid; scene ▶. Panel PREV/NEXT/LAUNCH work | Panel shows only a scene number; can't see what will play | S2 |
| **Record CV/MIDI** | Arm (red dot), REC; settings on right-click of REC | Settings hidden; inputs on panel unlabeled as REC | S2 |
| **Automate a knob** | Track header "A", "+ Add automation lane", lane menu → "Map a knob: touch it in the rack" | Works; "A" cryptic; lane menu long | S2 |
| **Add devices** | Device strip (open by default), "+ Add device", searchable list | Good once found; empty strip on every new track | S2 |
| **Export** | File ▾ → Export MIDI… / DAWproject… | Good. Keep | — |
| **Use the panel only** | PLAY, scene buttons, track strip | OK; can't see clips | S3 |

---

## 5. Visual check (look and feel)

| Area | Finding | Sev |
|---|---|---|
| Identity | Dark neutral grey + orange + flat colour blocks + coloured clip header bars = a borrowed look. Launcher grid with tracks as columns = also borrowed | S2 |
| Hierarchy | Everything is 11 px Inter on bg1; pane titles, tools and values all the same weight | S2 |
| Contrast | text2 #646B75 on bg1 #1A1C20 = 3.2:1 (fails AA for hint text it is used for); line0 grid lines nearly invisible | S2 |
| Radii | 3 px notes/clips, 4 px controls, but panel displays 3, plates 4, buttons on the panel round (VCV) — mixed | S3 |
| Spacing | 4 px grid mostly respected; transport items float at uneven gaps | S3 |
| Icons | Custom glyphs mostly fine; scale highlight / snap / fold are three hard-to-tell icons; "A", "|||" are letters/marks posing as icons | S2 |
| Type | Inter + JetBrains Mono: safe and readable, but generic (the type of every web app) | S3 |
| State colours | Play green, record red, queue yellow: good, but state is mostly colour only (queued slot vs playing slot) | S2 |
| Light theme | None | S1 for release |
| Panels | Drawn in code from Theme (good: one source of truth); dark only; VCV default jacks and buttons | S2 |
