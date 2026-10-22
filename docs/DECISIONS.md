# Design decisions

Short records of choices that differ from, or add detail to, DESIGN.md. Newest last.

## 1. Transport state lives in module params, not in `Song`

Tempo, loop on/off (and later play mode) are Rack params on the Tessera module. `Song` holds musical
content only (tracks, clips, loop region, time signature, swing).

Why: params get Rack's undo, saving, MIDI mapping and CV mapping for free. That matters in a modular
patch, where people want to drive tempo from other modules. The editor's BPM field and loop strip will
set the same params.

Impact: MIDI export reads the tempo from the module, not from `Song`.

## 2. Output bus: stable slots and a track tag on each frame

DESIGN.md §6 sketches a `trackId -> slot` table that is swapped atomically. Instead:

- Each track gets a bus slot when it is created and keeps it for its whole life (`BusSlots`). Reordering
  tracks never moves a slot.
- Every `TrackFrame` carries the `trackId` that wrote it. A reader only uses a frame whose `trackId`
  matches its binding (`OutputBus::read`).

Why: no table to swap, no table lifetime to manage, and no window in which a reader can pick up the
wrong track. If a slot is freed and reused by a new track, old readers see a mismatch and stay silent.

## 3. Each buffer records which sample it holds

`OutputBus` stores, for each of its two buffers, the sample number last written into it. A reader at
sample n uses buffer (n − 1) & 1 only if that buffer holds exactly sample n − 1.

Why: comparing against a single "latest sample" counter would hand a reader a stale frame right after
Tessera comes back from bypass. This check also covers bypass, removal and stalls in one rule.

## 4. Bus lifetime: `AudioHandoff`

Output modules hold the source's bus through a `shared_ptr` on the UI thread and give `process()` a raw
pointer. When the binding changes, the old bus is kept alive until `process()` reports a later sample.
So a Tessera can be deleted at any moment without a reader touching freed memory.

## 5. Panels are drawn in code from `Theme`

Panels are NanoVG drawings driven by `ui/Theme.hpp` (cached in a `FramebufferWidget`), not SVG files.
`PanelBuilder` places a control and its label in one call, so labels can't drift from their jacks.
Panels use Rack's dark jacks and screws until the light theme exists.

## 6. Timing rule shared by notes and transport events

An event at tick t belongs to the sample whose window `[start, end)` contains t. Note-ons, note-offs,
bar/beat triggers and clock edges all follow this rule, so they line up to the sample. The tests use
93.75 BPM at 48 kHz (exactly 1/32 tick per sample) to check exact sample numbers.

## 7. Note IDs are saved, at the end of the compact note array

DESIGN.md §17 gives notes as `[start, length, pitch, vel, relVel, micro, flags]`. We append the note ID:
`[start, length, pitch, vel, relVel, micro, flags, id, {extras}?]`.

Why: Rack's own undo steps (rename, preset load, Initialize) restore the module from JSON. If IDs were
regenerated on load, any Tessera edit further back in the undo history would point at notes that no longer
exist. Saving IDs costs a few bytes per note and keeps every undo step valid.

## 8. Pasted output modules follow the pasted Tessera

Rack gives pasted modules new IDs but does not rewrite module data, so a pasted Track Out still names the
original Tessera. Tessera saves its own ID as `savedId`. When a Tessera is added with a different ID, it is
a copy, and it records "old ID → new ID" for the current UI frame (`PasteRemaps`). An output module added
from JSON in the same frame follows the copy. One paste adds all its modules in one frame; a Track Out
pasted on its own finds no record and keeps following the original.

## 9. Snapshot swaps keep sounding notes

DESIGN.md §7 asks that snapshot swaps release all gates. Instead, each track's voices carry over to the new
snapshot by `TrackId`. Voices store their own end tick, so no note can hang. Gates are still released when a
track is muted, silenced by a solo, deleted, or loses voices to a lower polyphony.

Why: edits happen while playing. Cutting every note on every edit would make live editing sound broken.

Snapshots share unchanged tracks (`shared_ptr<const TrackPlayback>`), so an edit rebuilds only the tracks it
touched. Old snapshots are freed on the UI thread once the engine has acknowledged a newer one
(`SnapshotExchange`).

## 10. Tests link jansson from the system

`model/Serialize` uses jansson, the JSON library Rack ships. Tests are a plain native build, so they link a
system copy of jansson (Arch `jansson`, Debian/Ubuntu `libjansson-dev`, Homebrew `jansson`).

## 11. Clip files wait for the launcher

`.tessera-clip` import/export (DESIGN.md §17) needs a clip to right-click, which arrives with the clip
launcher in milestone 6. `clipToJson` / `clipFromJson` already exist for it.

## 12. Keymap conflicts in EDITOR_UX resolved

EDITOR_UX §7 binds a few chords twice. The shortcut table (`ui/interaction/Shortcuts.cpp`) resolves them,
and a unit test fails if any chord ever does two things in the same pane:

- **Alt+Left/Right** was both "nudge fine" and "cycle selection". Alt+arrows now cycle the selection
  (all four directions); fine nudge moves to **Ctrl/Cmd+Alt+Left/Right**, next to
  Ctrl/Cmd+Alt+Up/Down for velocity.
- **Ctrl/Cmd+Shift+T** was both "add drum track" and "transform box". Bindings are now scoped by pane: in
  the arranger it adds a drum track, in the detail editor it toggles the transform box. Scale fold
  (Ctrl/Cmd+Shift+F) and fit-all-tracks (Shift+F) are scoped the same way.
- **Z** is tap = zoom to selection, hold = zoom tool, as the spec intends; the table marks the hold binding.

## 13. Ruler double-click and the zoom rectangle

- EDITOR_UX §3 gives "double-click the ruler" to both *zoom to fit* and *move and play*. The time ruler
  keeps *move and play*; **double-clicking the overview bar** zooms to fit.
- §2 gives Ctrl/Cmd+Alt+drag to rubber-band selection, §3 to zoom-to-rectangle. Selection keeps it (it is
  the editing escape hatch); zoom-to-rectangle is the **zoom tool: hold Z and drag** (Z+click zooms in,
  Z+Alt+click out), as §4.3 also describes.

## 14. The editor overlay draws at Rack's UI scale

"Auto" editor size means 100% of Rack's own UI scale (menus and the overlay match). The Tessera context
menu offers 75–200%. The setting is saved per Tessera in the patch.

## 15. An editor inside the panel, as well as the overlay

DESIGN.md §6 has a compact panel plus a full-window overlay. Tessera's panel can also be **widened** (drag
its right edge, or pick 48 / 80 / 120 HP from the menu) to show a compact editor beside the controls: the
arranger above the selected track's piano roll, like the overlay. A strip on top switches between
**Arranger**, **Both** and **Piano roll**, since the 380 px panel often suits one pane at a time. The overlay
stays for big work and opens from the strip's "Full editor" button.

- Both surfaces lay their panes out with the same `PaneStack` (splitter, maximize, Tab / Shift+Tab). The
  panel starts from compact defaults: narrower track headers and 32 px track rows.

- Both surfaces run the same views and editing code through `EditorHost`; only layout differs. Edits
  in one show up in the other at once. Each keeps its own zoom and scroll (`ViewState`); the selected
  track and follow mode are shared.
- Rack modules are always 380 px tall at 100% zoom, so the panel editor is compact; zooming the rack in
  is how you give it more room.
- Rack sends keys to whatever the mouse is over. While the mouse is on the panel editor, its shortcuts win
  over Rack's module shortcuts (Delete never deletes the module); keys it doesn't use still reach Rack. It
  takes the mouse wheel too, so the rack scrolls only when the pointer is elsewhere.
- Widening pushes neighbouring modules aside like a Rack module drag. The width change and the neighbour
  moves are one undo step. The width is saved under `ui.panelHp`.
- The panel editor draws on Rack's self-lit layer, so it stays readable when room brightness is lowered.

## 16. Piano roll editing details (milestone 4)

- **Selection is shared** by the overlay and the panel editor and lives in `EditorState` (not saved). Every
  edit records the selection before and after, and undo/redo restore it (EDITOR_UX §2).
- **A press on a note starts the move at once**; releasing without moving counts as a click (select just
  that note, or Ctrl/Cmd-toggle it). `ClickOrDrag` in `NoteGrid.cpp` does this for any gesture.
- **Draw tool clicks place notes on the grid line at or before the pointer**, not the nearest one, so a
  note never appears to the right of where you clicked.
- **Velocity line drawing** lives in the velocity lane (Alt+drag ramps, Shift+drag freehand). In the grid,
  the Velocity tool drags velocity up and down; EDITOR_UX §4.3's Shift+drag line there would be ambiguous
  (grid height is pitch, not velocity).
- **Slice, Shift+click** offers 2/3/4/6/8 equal parts from a small menu instead of a typed prompt.
- **Zoom to fit/selection** never zooms tighter than one bar and one octave.
- **Tap keys vs hold keys:** a key release only counts when that key's own press started the hold, so
  releasing Z after Ctrl+Z never triggers "zoom to selection".
- **Cursors** use the operating system's standard shapes (crosshair for drawing, I-beam for slicing,
  "not allowed" for erasing). Custom pencil/eraser/scissors cursors are part of the polish milestone.
- **Audition** goes through a lock-free queue to the engine and plays on the track's own voices, even while
  stopped. New and moved notes play for 0.25 s when the speaker toggle (Alt+A) is on; keyboard clicks always
  play while held.

## 17. Track management details (milestone 5)

- **Folding a group is a view setting** (`ViewState::foldedGroups`), not song data: folding never lands in
  undo, and the panel and overlay fold independently. The `collapsed` field left `TrackGroup`.
- **One layout command** (`ChangeTrackLayout`) covers reordering, grouping, ungrouping and moving tracks into
  or out of groups. It stores the track order and groups before and after (at most 64 IDs each).
- **Groups don't nest.** Dragging a group moves its tracks as a block; dropped inside another group, it lands
  just before it. A group's tracks always follow the track order.
- **Deleting a track** removes the clips only it uses; clips shared with other tracks stay. Undo restores the
  track with its ID, clips and group position, so output modules find it again. An emptied group stays
  (delete it with Ungroup).
- **Duplicating a track** copies its clips as independent clips (new IDs).
- **A song always keeps one track**, and holds at most 64.
- **Delete and Ctrl/Cmd+D act on tracks** while the arranger has focus. When arranger clips arrive (milestone
  7), they act on selected clips first.
- **Mute, solo, colour and voices apply to every selected track** when the clicked track is selected.

## 18. Clip launcher details (milestone 6)

- **Launch quantize follows the song position** (the bar numbers you see), not the time since play was
  pressed. Clips have their own setting that overrides the song's ("Song setting" by default).
- **Launching while stopped** starts the clip at once and starts the transport. Pressing play again later
  resumes whatever clips were playing.
- **Launching an empty slot stops the track**, also as part of a scene. A scene launch that starts no clip
  doesn't start the transport.
- **Pending launches happen at once when the transport stops or resets**, so a queued clip is never lost.
- **A scene's tempo** takes effect on the same quantize line as its clips and moves the BPM knob.
- **Switching clips releases the old clip's notes at the switch.** A clip that doesn't loop stops at its end.
- **The engine follows the playing clip by its ID**, so inserting a scene above it or dragging it to another
  slot never changes what plays. When the clip is deleted, the track stops.
- **Which clip each track plays is saved with the patch** (`"launcher": [[trackId, slot], ...]`). Patches from
  before the launcher play each track's first clip, as they did.
- ~~The top pane shows the launcher or the arranger~~ (replaced by §42: the clips and the timeline are always
  shown together). What *plays* is the play mode (§19), a separate choice.
- **The piano roll shows the selected clip** when it is on the selected track; otherwise the clip the
  launcher plays on that track, otherwise its first clip.
- **Delete and Ctrl/Cmd+D in the launcher** act on the last clicked clip; after clicking a track header they
  act on tracks, as in the arranger. Duplicate copies the clip into the slot below, inserting a scene when that
  slot is taken.
- **Dragging a clip onto a taken slot replaces that clip.** Alt or Ctrl/Cmd while dragging copies.
- **A folded group shows as one launcher column.** Its triangles launch every clip of the group in that scene
  and stop the group's other tracks.
- **The panel's scene controls** (PREV, NEXT, LAUNCH, STOP ALL) and the SCENE / LAUNCH inputs act on the
  "selected scene", which is also shown on the display and outlined in the editor's scene column. The SCENE
  input picks 0.1 V per scene, or one semitone per scene (menu).
- **"Record arrangement from launcher"** arrived with milestone 7 (§19).

## 19. Arranger details (milestone 7)

- **Play mode is a Rack param** (`MODE_PARAM`, the panel's ARRANGE switch and the transport's play-mode
  button), like tempo and loop. Launcher mode: tracks play launched clips and the arrangement is silent.
  Arranger mode: tracks play the timeline.
- **In Arranger mode the launcher takes tracks over.** Launching a clip, or stopping a track, takes
  that track off the arrangement until "Back to arrangement" (transport, or the arranger's corner). Its lane is
  dimmed meanwhile. Switching to Arranger mode brings every track back. "Stop all" only stops launched clips;
  tracks playing the arrangement carry on. DESIGN.md §7's per-track "launcher overrides arranger" option is
  not built: the override is always on.
- **How an instance plays** (`model/Arrangement`): it starts at `offset` in the clip, plays to the end of the
  clip's loop (or the clip's end when it doesn't loop), then repeats the loop until the instance ends. Notes are
  cut at loop seams and at the instance end. `stretch` scales time; transpose and velocity scale apply per
  instance. The engine, the drawing, glue and (later) MIDI export all use this one function.
- **Arrangement notes never sound past the song loop end** while looping, and seeking cuts sounding notes. A
  note that started before the playhead was moved there doesn't start mid-way (no note chasing).
- **Overlaps:** a moved, resized, pasted or recorded clip wins; clips under it are trimmed, removed, or split
  around it. Deleting the last use of a clip removes the clip from the pool (undo brings it back).
- **Linked by default:** Ctrl/Cmd+drag and Ctrl/Cmd+D make linked instances (same clip). Ctrl/Cmd+Alt+drag
  makes independent copies. Ctrl/Cmd+drag on an unselected clip copies only that clip.
- **Alt on a clip** follows EDITOR_UX §6.3, not the generic §2 rule: Alt+drag the body slips the content,
  Alt+drag the right edge time-stretches. Velocity scale is the header strip (drag up and down); a sideways
  drag there moves the clip. Rows shorter than 36 px have no header strip.
- **Magnetic snap:** clip edges, markers, the loop edges and the playhead pull within 8 px, before the grid.
  Alt turns all snapping off (except while stretching, where Alt is the gesture itself).
- **Delete, Ctrl/Cmd+D and Ctrl/Cmd+A act on clips** after a click in the lanes and on tracks after a click in
  the headers.
- **Right-click on a clip always opens the menu**, in every tool (the piano roll's Draw tool deletes on right-
  click instead).
- **Glue** bakes loops, offset, stretch, transpose and velocity scale into a new looping clip covering the glued
  span. **Make unique** gives each selected instance whose clip is used elsewhere its own copy.
- **Clips go from the launcher to the arranger** with "Place in arrangement at playhead" (launcher clip menu)
  or "Place launcher clip" (lane menu), because the launcher and arranger share the top pane and are not on
  screen together for a drag.
- **Record to arrangement** (launcher toolbar) writes what the launcher plays onto the timeline as one undo
  step when it is switched off or the transport stops. The engine reports each clip start and stop with its
  song position and clip phase; when the song loop wraps, the next pass is recorded over the previous one.
- **The transport bar collapses** on narrow windows: the instance name goes first, then the key placeholder,
  then undo and redo (Ctrl/Cmd+Z still works).
- Not in this milestone: the range tool, ripple mode and automation lanes (milestone 11), arm/monitor in the
  headers (milestone 10).

## 20. Drum tracks (milestone 8)

- **A lane is a note number.** A drum clip is an ordinary clip; each lane plays the notes of its pitch. The
  piano roll edits the same notes (G switches between the step grid and the piano roll). Drum tracks ignore
  the track transpose, so notes never move onto another lane.
- **Lane output:** a 1 ms trigger, or a gate of a fixed length in ms (the lane's "Output length"). A lane hit
  again while its output is high drops low for the 1 ms retrigger gap first. The note's length doesn't matter.
- **Outputs:** each lane has an output number (1-16). Drum Out has jacks for outputs 1-8 and poly TRIGS / VEL
  for all 16. Lanes sharing an output merge. On a Track Out, a drum track gives one channel per output with the
  lane's note as V/OCT.
- **Choke groups 1-8:** a hit cuts every other lane of its group at once (hats share group 1 in the kits).
- **Polymeter:** a lane's "Steps" sets its own loop length (in steps of its rate). Its pattern lives in the first
  steps of the clip loop and repeats on its own, across clip loops, in the launcher and the arranger alike. The
  step grid shows the repeats faded. "Follow the clip" goes back to the clip's loop.
- **Rate** is per lane (1/4 to 1/32, triplets too); a lane with its own loop keeps its step count when the rate
  changes.
- **Step grid gestures:** click or drag across steps paints them on (or off, when the first step was on), one
  undo step per drag; Alt+drag a step changes its velocity (Shift: fine); a click on a lane name plays it.
  New steps use the last velocity set.
- **Pattern tools** (lane menu): Euclidean (hits over the lane's steps; the first step always hits), rotate,
  reverse, fill every N, randomize (25/50/75% of steps), clear, copy and paste. Each is one undo step on the
  shown clip.
- **Kits:** new drum tracks get "Standard 8"; the Kit button loads "Standard 8" or "16 GM-ish" (lanes only;
  notes stay, so lanes with the same notes pick them up again).
- **Not yet:** the step menu's note operators (chance, repeats, occurrence…), per-lane swing and Alt+wheel
  repeats arrive with the note operators (milestone 12). The per-lane free-time grid editor (piano roll with lane
  rows) arrives with the row folding of scale fold (milestone 9); until then G opens the full piano roll.
  *(Done in milestone 9: fold in the piano roll of a drum track shows one row per lane, named.)*
- **Glue** on drum tracks bakes lanes with their own loop as they play, but the glued clip's lanes then loop
  inside it again; avoid gluing polymetric lanes until this is reworked.

## 21. Bug fixes after milestone 8

- **Auto voices (replaces "output channel count = track polyphony" in DESIGN.md §6):** a track's Voices setting
  is "Auto" by default. Auto sends as many poly channels as the track's clips ever play at once (unmuted notes
  inside the loop, at least 1), so a part with 3-note chords sends 3 channels, not 8. A fixed count (1-16) is
  still available in the Voices menu. Schema 2 turns tracks saved with the old default of 8 into Auto.
- **Keys reach the editor first:** Rack 2.6 runs some shortcuts before any widget's hover key handler (the
  scene pans the rack with the arrow keys; a module panel deletes or clones itself with Delete or Ctrl+D). The
  full editor therefore takes the keyboard as Rack's "selected widget" while it is open, and the panel editor
  does so after a click inside it while the mouse stays over it. Text fields keep the keys while typing.
- **Grid lines follow the snap:** with a fixed snap (1/8, 1/8T…), grid lines are drawn at that step, unless
  they would be closer than 4 px; then the zoom picks them as before.
- **Ctrl/Cmd+drag from empty space selects (replaces "Ctrl/Cmd+Alt+drag selects" in EDITOR_UX §2):** with any
  tool, in the piano roll and the arranger. Ctrl/Cmd+drag that starts on a note or clip still copies it, and
  Ctrl/Cmd+Alt+drag still selects from anywhere, except on an arranger clip (§41). A selection box replaces the selection; Shift+drag adds to it.
- **Erase button with a selection** deletes the selection and keeps the current tool; without one it picks
  the Erase tool. The arranger has the same tool buttons as the piano roll (Pointer, Draw, Erase, Slice, Mute).
- **Which clip the piano roll opens** for a track: the selected clip, else the launcher's playing clip, else
  (while the arranger shows) the timeline clip under the playhead or the earliest one, else the first launcher
  clip. Selecting a track scrolls the launcher and arranger to it.
- **Drum Out velocity per lane:** each of the 8 lane jacks has a velocity jack beside it ("V1"-"V8", 0-10 V, held
  from one hit to the next). The panel stays 10 HP so existing patches don't overlap, and the new jacks take the
  last output IDs so saved cables stay on their jacks. Lanes on outputs 9-16 (custom lanes added past 8) reach
  only the poly TRIGS and VEL jacks. Each lane row in the step grid shows its output ("→3", "→12 poly"); the
  lane menu's Output changes it.

## 22. Scales (milestone 9)

- **No key by default.** A new song's key is "Chromatic", shown as "No key": nothing is tinted, snapped or
  folded until a key is chosen in the transport (song key) or the piano roll (the clip's key). Before schema 3
  every song saved "C major" without anyone choosing it, so schema 3 drops that saved key.
- **Song key and clip key:** one key picker (Trello, 13 Oct): a Song | This clip switch (the transport's key
  button has Song only), the 12 roots, the scales, a one-octave keyboard of the key. Clicks preview in the piano
  roll; Enter or closing the picker applies, as one undo step; Esc cancels. "Notes: Move with the key" (same
  degrees, the root moves the short way round) or "Keep pitches" is a remembered editor setting, not a question
  per change. Drum clips never move.
- **Scale tools are editor settings** (saved with the editor, not undoable): Highlight (on by default), Snap and
  Fold, as three toggles beside the key button. They do nothing without a key, and nothing on drum tracks except
  Fold.
- **Highlight:** key rows are light, other rows darker, root rows tinted with a line under them; the keyboard
  dims keys outside the key and marks each root. Notes outside the key get an amber corner.
- **Snap:** Draw and Paint place notes on the nearest key note (ties go down); dragging notes moves them by key
  steps, each note on its own, so chords stay in key; Up/Down step through the key; the transform box's pitch
  handles land notes on the key. Alt allows any note while dragging, drawing or painting.
- **Fold:** only the key's rows are shown, plus rows of any pitch a note uses, so folding never hides a note.
  With fold on, a row is a step: dragging and Up/Down move through the shown rows. On a drum track Fold shows
  one row per lane, named, which is the drum "grid editor" (free-time editing per lane).
- **Custom scales:** "Custom scales" in either key menu. "New scale from this key" copies the current key's notes
  and makes the new scale the key; each custom scale has a name field, a 12-key toggle row (the root stays on)
  and Delete. Each toggle is one undo step. A key using a deleted scale acts as no key until undo restores it.
- **Chord stamp:** a toolbar menu. Shapes "in the key" (triad, 7th, 9th) stack every other key note from the
  clicked note, so the chord follows the degree; fixed shapes (major, minor, sus, 7ths, add 9, power, octave)
  don't. Inversions move bottom notes up an octave; Open raises the second note an octave, Drop 2 lowers the
  second from the top. Without a key, "in the key" chords use a major scale. Draw and double-click place the
  chord; dragging sets its length and root.
- **Fit to key** in the note menu moves selected notes outside the key to the nearest key note.
- **Not yet:** quantizing recorded notes to the key arrives with recording (milestone 10); scale-aware
  transforms (invert, connect) with the transforms (milestone 12).

## 23. Bug fixes after milestone 9

- **Moves snap absolutely (replaces "relative snap" in EDITOR_UX §4.3):** a dragged note's (or clip's) start
  lands on the grid even when it started off it. It never moves against the drag: an off-grid note stays put
  until the pointer is past the middle of the way to the next grid line.
- **Ruler drags scroll the view** at the edges like every other drag, so a clip's loop can be dragged longer
  than what is on screen.
- **Launcher takeover is explained:** in Arranger mode a track playing a launched clip says so in its header
  ("Launcher clip playing"), and the "Back to arr." tooltips say why the button is there.
- **Drum velocity lane:** a velocity strip under the step grid shows the steps of the lane last clicked. Dragging
  across it sets each step it crosses to the pointer's height; one undo step per drag.
  Alt+drag on a step still works.

## 24. Recording (milestone 10)

- **Inputs:** REC V/OCT, GATE and VEL on the panel (poly; a rising gate above 1 V starts a note at that moment's
  V/OCT, `round(v * 12) + 60`; it ends below 0.1 V), and one MIDI input per Tessera (module menu or the record
  button's menu). Velocity is 0-10 V, or 0.8 without a cable. The new jack and button IDs come last, so saved
  cables stay on their jacks.
- **Per track:** an arm button (track headers and launcher column headers; exclusive, Ctrl/Cmd+click adds), the
  input (Any / REC CV / MIDI), a MIDI channel (or all) and "Monitor while armed" (on by default: armed tracks play
  what comes in through their outputs). Schema 4 turns monitoring on for older songs, where it did nothing.
- **Record button** (transport, panel REC, R): stopped, it counts in (Off / 1 / 2 bars, clicking on CLICK OUT and
  blinking) and then plays and records; playing, it punches in at once; recording, it punches out. Stop also
  punches out. Reset and jumping the playhead end the take and start a new one.
- **Where a take goes**, per armed track, by what the track does when its first note arrives:
  - a track playing a launcher clip overdubs it: every loop pass is merged when it ends (one undo step each), so
    the next pass plays it back;
  - Launcher mode otherwise: a new clip in an empty slot (the selected scene's slot, else the next empty one),
    starting at the bar recording started in. "New launcher clips" sets it to a fixed 1/2/4/8 bars (committed when
    full, then it loops and takes overdubs) or "as long as played" (rounded up to the bar at punch-out). The new
    clip keeps playing in time, unless the take ended with the transport stopping;
  - Arranger mode otherwise: a new clip on whole bars from punch-in to punch-out, one per loop pass. Overdub (the
    default) merges what the track played there; Replace drops it. Either way old clips are cut around the new one.
- **Nothing played, nothing made:** a take with no notes leaves no empty clip and no undo step.
- **Input quantize** (Off, 1/4-1/32, strength 50-100%) is applied as a second undo step, so Ctrl/Cmd+Z brings back
  the played timing. **Snap to the key** applies to monitoring and recording alike (the song key).
- **Capture** (launcher toolbar, record menu, Ctrl/Cmd+Shift+R): the last 64 bars of notes played while not
  recording are kept; Capture turns the last phrase (back to a silence of 4 bars) into a clip on the selected
  track. Played along with the song, it keeps the notes' place in the bar; played while stopped, it starts on the
  beat of the first note.
- **Seen live:** the target slot turns red ("Recording"); an overdubbed clip has a red outline and its new notes
  show in red in the piano roll; arranger takes show as a red region with their notes so far.
- **Not yet:** recording into a launcher slot doesn't wait for the launch quantize line (it starts at once and lines
  the clip up with the bar instead); step input and computer-keyboard play come with milestone 12.

## 25. Clip length buttons

- The piano roll and the drum step grid have a **Length** button ("2 bars") for the shown clip: 1, 2, 4, 8 or 16
  bars, **Double** (the loop doubles and its notes are copied into the new half) and
  **Halve**. Each is one undo step. Dragging the loop strip in the ruler still works; opening a clip now leaves a
  quarter of the view free after its end, so the loop end isn't stuck at the edge.

## 26. Automation (milestone 11)

- **Lanes:** a track's lanes open under it in the arranger (the "A" button on its header, or the track menu);
  "+ Add automation lane" adds one on the first free AUTO channel. A clip's lanes are in the piano roll's lower
  pane (the selector where "Velocity" was). Up to 8 per track or clip. Lane headers show name, AUTO channel, the
  value now in volts, mapped knobs, arm and mute; the lane menu has the rest (name, output, voltage range presets,
  bipolar, slew 0-100 ms, mute, for clip lanes Override / Modulate, functions, shape stamps, mapping, recording,
  delete).
- **Playback:** track lanes play while the track follows the arrangement; clip lanes while their clip plays (in the
  launcher or as an arranger instance, in clip time, looping with it). A clip lane on the same AUTO channel as a
  track lane overrides it, or with Modulate adds to it around the middle. Muted lanes are left out. A channel with
  nothing playing holds its last value. Slew is a one-pole smoother of that time.
- **Curves:** Hold, Linear, Exponential and S-curve, each bent by a tension -1..1 (positive rises early). Values
  are 0..1 in the model and become volts through the lane's range.
- **Editing (EDITOR_UX §6.6):** click adds a point (Alt: on the curve), drag moves (snapped; Alt free; Shift one
  axis, fine values), double-click deletes, drag a segment's middle handle to bend it, Ctrl/Cmd+drag a segment
  moves both its points, right-click a point for its curve, exact value (volts) and delete. Draw tool: freehand,
  thinned on release (Alt: a straight line); Paint: held steps on the grid; Erase: wipe points. Drag from empty
  space selects; 2+ selected points get a transform box (top/bottom scale toward the opposite edge, Alt around the
  middle; sides stretch time; top corners tilt). Keys: Delete, Ctrl/Cmd+A, C, X, V (at the playhead), Up/Down 1%
  (Shift 0.1%), Left/Right by the grid. Lane functions act on the selected points or all: thin, quantize to the
  beat, invert, reverse, randomize ±10%, clear. "Stamp a shape" arms one of nine shapes; the next drag across the
  lane stamps it, one cycle per grid step, the drag up and down setting its depth.
- **Mapping (DESIGN.md §6):** "Map a knob" in a track lane's menu; the editor shrinks to a banner ("click to
  cancel") and the next knob touched in the rack is mapped (up to 4 per lane, each with its own range: full, halves,
  middle, reversed). A knob already mapped elsewhere asks before taking it over. Mapped knobs follow the lane's
  channel (track and clip lanes combined) about every 16 samples, only when the value changes, so a lane holding
  still never fights the hand; nothing moves until the channel has points. Mappings live on track lanes only.
- **Recording:** arm a track lane (dot in its header). It records while the transport records, from its first
  mapped knob or its channel of the new AUTO REC input (poly, channel = AUTO channel), in Touch (while the source
  moves, 0.5 s release), Latch (from the first move) or Write (all the time) mode. While a mapped knob is being
  recorded its playback pauses so the hand wins. Each pass is drawn live in red, thinned and committed as one undo
  step when recording stops or the loop wraps.
- **Range (EDITOR_UX §6.4):** Alt+drag in empty track space, or the Range tool (corner button, Alt+R), selects time
  across tracks. Delete empties it (in Ripple mode it deletes time), Ctrl/Cmd+Shift+Delete deletes time,
  Ctrl/Cmd+Shift+I inserts silence, Ctrl/Cmd+D duplicates it after itself (the range moves onto the copy),
  Ctrl/Cmd+L loops it. Range edits act on clips and track automation together.
- **Ripple** (corner button or the arranger menu): deleting clips closes the gap on their track. Moving clips in
  ripple mode doesn't push later clips yet.
- **Automation follows clips** (on by default; arranger menu or corner): moving or copying a clip carries the track
  automation under it, to the lanes on the same AUTO channels of the track it lands on. Trims don't move anything.
- **Not yet:** the maximized automation view (A), "show used lanes" (Shift+A), the dashed line of the combined
  result over track lanes, the stamp popover (rate, phase, skew), copy/paste with "keep volts", "move clip
  automation to track", rubber band across several lanes, and recording into clip lanes.

## 27. Bug fixes after milestone 11

- **Chords follow the key's steps when the key shapes editing** (scale fold or scale snap on, with a key): a chord
  shape's intervals are read as steps of the key, so a major triad stacks two rows and two more on any degree, as
  the folded rows show it (D in C major gives D-F-A, not D-F#-A with F# on a hidden row). Without fold or snap the
  shapes stay exact.
- **Drum view switch:** the step grid and a drum track's piano roll both show "Steps | Piano roll" as a pair of
  buttons in the same place, the current one lit, so the way back is where you left (G still switches).

## 28. Piano roll v2 (milestone 12)

- **Operators are on/off switches.** The inspector has one switch per operator: Chance, Repeats, Spread (velocity
  and timing), Occurrence, Recurrence. Off means the note plays as written. Switching one on gives a value you hear
  at once (chance 50%, 2 repeats, ±20% velocity and ±10% timing, "first pass", every second pass) and shows only
  that operator's controls; switching it off puts its fields back to their defaults. A switch is half lit when only
  some selected notes have it. Recurrence is a row of 16 pass bars: click to play or skip a pass, click a grey bar
  to lengthen the cycle, right-click to end it there.
- **Inspector fields are sliders.** Each field fills to its value (from zero for values that can go negative); with
  several notes a band shows their range and a tick marks each note. Drag sideways or up and down to move all
  values together, Alt+drag to spread them, click to type a value or an edit (+10, -5%, *0.8, =64), double-click
  to reset. The dice and ramp buttons sit beside each field when several notes are selected.
- **Lower lanes are tabs**, one click each: Velocity, Release, Chance, Vel. spread, Repeats, Micro, Pitch, Timbre,
  Pressure, and Automation (a menu of the clip's lanes). One lane shows at a time; stacking is not built.
- **Expression curves** (pitch ±24 st, timbre, pressure) are edited in their lane, a curve per note across its
  span: click adds a point (the first one also adds a resting point at the note start), drag moves, the middle
  handle bends, double-click deletes, Shift snaps pitch to semitones. The pitch lane shows ±2, ±7, ±12 (default)
  or ±24 semitones. The active curve is also drawn small inside each note.
- **Badges:** repeat ticks inside the note, chance and occurrence text at its right end, recurrence dots along its
  bottom; on rows too small for them, a dot. A toolbar switch hides them.
- **Transforms** are in the toolbar's ⋯ menu and the notes' right-click menu (EDITOR_UX §4.8). Each with settings
  opens a small panel; the grid previews the result while settings change, Apply/Enter is one undo step, Cancel/Esc
  leaves the notes alone. Settings are remembered for the session. Glue, Reverse and Retrograde apply at once.
  Select by velocity, on/off the grid, every Nth, muted, with operators. Q quantizes to the snap grid with the last
  quantize settings, Shift+Q opens them. The velocity shaper uses preset shapes (ramps, sine, triangle, random)
  instead of a drawn curve.
- **Keyboard play (Ctrl+K)** uses a piano layout: A row white keys from C4, W row black keys, Z/X octave, C/V
  velocity. (EDITOR_UX §4.10 also mentions a Z-row octave, which would clash with Z/X/C/V; this layout wins.)
  Notes go through the engine as a third live source, like MIDI: armed tracks monitor and record them and the
  capture buffer keeps them; an unarmed selected track still sounds them.
- **Step input (Ctrl+R)** takes notes from MIDI, the REC CV inputs and keyboard play. Notes held together are one
  step; the step is the snap grid. Right arrow rests, Shift+Right ties, Backspace removes the last step, Left steps
  back; Esc or Ctrl+R stops. Each note is its own undo step.
- **Ghost notes** (toolbar): another track, all instrument tracks, or this track's other clips, as faint outlines in
  clip time. Alt+Shift+click one to open its clip.
- **Multi-clip editing** is a layered view, one clip editable at a time: "Edit this scene's clips together"
  (launcher clip menu) or "Edit together" (arranger, several clips selected). The others show dimmed in their
  colours; clicking one of their notes, or their chip in the strip at the bottom, edits that clip; × ends it. Notes
  are shown in clip time, also when opened from the arranger. Editing notes of several clips in one gesture is not
  built.
- **Not yet:** stacked lanes, the curved Alt+Shift ramp in stem lanes, swing at playback (the song's swing is
  stored but the engine doesn't apply it).

## 29. Bug fixes after milestone 12

- **Track automation plays in Launcher mode too.** Track lanes run along the song position in both modes. In Arranger mode they still stop while
  the launcher has taken the track over, until "Back to arrangement". Before, they only played in Arranger mode, so
  mapped knobs and AUTO outputs stood still in the default Launcher mode.
- **Tessera Auto Out** (8 HP): a track's automation lanes on eight mono jacks, each labelled on the panel with its
  lane's name and a live value bar. It binds like Track Out, to instrument or drum tracks, with the same source /
  track menus and arrows. The poly AUTO jacks on Track Out and Drum Out stay, so older patches keep working.
- A mapped knob also counts as active when its channel has points in a clip placed on the timeline.

## 30. MIDI export (milestone 13)

- **Where:** the transport bar's Export button or Ctrl/Cmd+Shift+E (the arrangement, or the tracks selected in the
  headers), and "Export as MIDI…" in the clip menus of the launcher and the arranger (one clip, or the selected
  clips: one file each, or one file with a track per clip, each track named after its clip). The old clip-file
  items are now "Save as Tessera clip file…" / "Load a Tessera clip file…" so the two exports can't be confused.
- **Dialog:** Range (whole song, loop, the arranger's range selection when there is one), Files (one, or one per
  track), Timing, Note operators, Automation, Expressions, Include muted, a CC number per AUTO channel in use, and
  a MIDI channel per track (listed when there are at most 12 tracks; longer songs keep the automatic channels).
  Settings are saved with the patch. A toast says what was written; an empty arrangement says so instead of
  writing an empty file.
- **File:** SMF format 1 at 960 PPQ. Track 0 has the instance name, the tempo (the module's BPM; scene tempo
  changes are not exported), the time signature, markers, and the song loop as "Loop start" / "Loop end" cue
  points. Then one track per Tessera track in display order, named after it, empty tracks included. "Whole song"
  ends at the last clip on the timeline. Instrument tracks take channels 1, 2, 3… skipping 10; drum tracks take
  10. Muted notes are never exported; muted tracks and clip instances only with "Include muted".
- **Timing "as played"** applies swing and micro-timing. Swing now also plays: the snapshot warps note times
  (every second 1/16 up to half a 1/16 later), so playback and export agree. The model stays unswung.
- **Rendered operators** run the engine's own operator code with the song's seed, clip and pass, so a loop with
  chance or recurrence exports its actual variations. Fill is taken as off.
- **Automation** samples each AUTO channel every 1/64 plus at every track-lane point, combining track and clip
  lanes like the engine (override / modulate), and writes a value only when it changes. 14-bit writes the MSB on
  the chosen CC and the LSB on CC+32 (for CCs below 32).
- **Expressions:** MPE uses a lower zone (channel 1 master, 2–16 members, zone and ±48 bend range set by RPN at the
  start); each note takes the free member channel used longest ago and sets its bend, CC74 and channel pressure
  at its start, then follows its curves. Flatten writes timbre as CC74 and pressure as polyphonic aftertouch on
  the track's channel; pitch curves are dropped.
- **Speed:** 64 tracks × 200 bars render and write in about 130 ms in an optimized build.

## 31. Polish, external clock and performance (milestone 14)

- **External clock:** a new CLOCK input (first jack of the input row). While patched, the tempo and the song
  position follow it: pulse k after the transport starts is tick k × 960 / PPQN, the time between pulses is filled
  at the averaged pulse rate (8 pulses, so jitter is smoothed), the song never runs past the next pulse's tick and
  jumps forward when a pulse comes early, so it can't drift. Starting the transport starts the song on the next
  pulse; RESET restarts the count. PPQN is in the module menu (1–96, default 24) and saved. The tempo shows
  "EXT 120.3" in the editor and "EXT CLOCK" on the panel, and can't be dragged while a clock is patched.
- **Follow selected track:** Track Out, Drum Out and Auto Out can follow the track selected in their Tessera's
  editor (Track menu, first item; saved with the module). A selected track of the wrong kind leaves the last one
  playing; picking a track or the arrows turns following off. The display shows "· follows editor".
- **Help (?, Shift+/):** a full-editor overlay generated from the shortcut table, grouped as in the table, plus the
  mouse modifiers. Any key or click closes it.
- **Piano roll polish (EDITOR_UX §8):** notes glow for 150 ms as they start playing; muted notes are hatched; a
  faint band marks the hovered row and grid column; a hovered note shows its velocity when it is at least 44 px
  wide and 12 px tall; the ruler marks where a dragged note snaps; empty states say "Draw with D, or press
  Ctrl/Cmd+R for step input" and "Drag a clip here from the launcher, or draw one with D".
- **Performance (DESIGN.md §18):** measured in an optimized build, 48 kHz, every track playing 4-note chords on
  every 1/16: 16 tracks ≈ 1.0–1.4 % of a core; 64 tracks ≈ 2.5 % without automation and ≈ 3.5–4.5 % with an
  automation ramp on every track (the budget is 1 % and 3 %). What made it fast: tracks with nothing due skip
  scheduling until their next note start or end, pending hit or queued launch (launcher clips; the arrangement is
  still scheduled every sample); a track's bus frame is rewritten only when its voices, drum lanes or automation
  changed (each bus buffer remembers which version it holds) or while a note plays an expression curve;
  automation and the activity lights run every 32 samples (0.67 ms at 48 kHz; slew is scaled to match); voice
  timers are timestamps instead of per-sample countdowns. What is left is a fixed ~8 ns per track per sample.
- **Not yet:** the hover band toggle, scene tempo changes under an external clock, and skipping quiet tracks in
  Arranger mode.

## 32. Stacked clips (roadmap: "multiple lanes in the same track playing in parallel")

- Clips on one track may overlap on the timeline: they stack and all play, into the track's voices and outputs.
  Each instance takes the lowest sub-row free at its start; the track's row is split between the sub-rows and
  grows (at least 28 px per sub-row, for as long as they overlap) without changing the height you set.
- **Stack** (corner button, on by default, saved): moving, copying, pasting, drawing and placing clips keep what
  they overlap. Off, the old rule applies: the changed clip trims or replaces what it covers. Recording in
  Replace mode, "record from launcher" and range duplicates still replace on purpose.
- Automatic voices add up the clips that overlap in time, so stacked chords don't steal each other's voices.
- Where stacked clips both have clip automation on the same AUTO channel, the earlier one plays it.
- The corner tools wrap onto a second line when the header is too narrow for one (Automation follows clips was
  hidden before).

## 33. The launcher beside the arranger (roadmap: "Launcher should be on the left")

- The arranger shows the clip launcher between the track headers and the timeline:
  tracks are rows, lined up with the arranger's, scenes are columns (76 px each) with their launch buttons where
  the ruler is. It is on by default in the full editor and off in the panel editor; the corner button with three
  slots shows or hides it, its right edge resizes it (it never takes more than half the room), Shift+wheel
  scrolls the scenes and the wheel scrolls the tracks with the arranger.
- Slots work as in the launcher grid (the drawing is shared): the triangle launches, an empty slot's square
  stops the track, a click edits the clip, double-click creates one, right-click opens the clip, slot or scene
  menu.
- **Dragging:** a slot's clip dragged onto the timeline is placed where it lands (snapped, linked to the slot's
  clip, one undo step, previewed as you drag); onto another slot it moves, or copies with Ctrl/Cmd or Alt. A clip
  dragged from the timeline onto an empty slot is linked into that slot (the timeline keeps it).
- ~~The full launcher grid (F2) stays for performing~~ (§42: it is gone; F2 widens these clips instead).

## 34. Track colours and Alt+drag in the panel editor

- **Clips always show their track's colour** (launcher slots, arranger clips, notes, lanes, ghost notes, the
  clip strip). The clip colour menus are gone; a recoloured track recolours its clips at once. The colour stored
  with a clip is kept in the patch but no longer used.
- **Alt+drag in the panel editor:** Rack scrolls the rack on Alt+drag, and its scroll view takes the press before
  modules see it. A small layer on top of Rack's scene hands Alt+clicks that land on a Tessera panel editor to the
  editor (Rack then drags the editor's view as usual), so velocity drags, free placement and Alt+spread work
  there. Alt+drag anywhere else still scrolls the rack.

## 35. The inspector's chart (Trello: "two velocity sliders", "make operators simpler")

- **What you see is what plays.** Velocity and micro-timing no longer vary each pass: the inspector's chart changes
  the notes themselves. The user found per-play variation complex and of little musical value, and it read as a
  second velocity slider. Its switch, its "Vel. spread" lane tab and its rows are gone from the editor; the model
  and engine still read it, so older songs that used it play as before.
- **A histogram for several notes.** Clicking a field opens its chart under it: how many notes sit at each
  value, a mean marker, and Mean, Spread and Chaos to drag. Spread works around the mean (not the middle of the
  range). Spread and Chaos spring back on release: they are tools applied to the notes, each drag
  one undo step. A short hint on the chart says what the control under the mouse does.
- **No dice or ramp buttons.** Chaos replaces the dice; ramps are the "Shape velocity…" transform. Alt+drag on a
  field no longer spreads (the chart does that visibly).

## 36. The device strip (docs/DEVICES.md §1.1)

- **Devices sit above the notes, not in a separate view.** Agreed with the user on a design canvas: a strip of
  fixed height above the piano roll (and the drum grid), opened and closed with the header's Devices button or
  F3, so notes and what shapes them are seen together and the pane never shows empty space. Closed, a thin bar
  names the chain. The in-panel editor starts with it closed.
- **One modulator list**, whatever the level: each row says where the modulator lives (song, track, or its
  device), and a device card shows a dot per modulator of its own. Settings open in a popover over the notes.
- **A searchable browser** adds devices and modulators: type to filter by name or purpose, arrows and Enter.

## 37. Selecting steps in the drum grid (Trello: "Drum select notes to add operators")

- **The step grid has the piano roll's tools**, the ones that make sense on steps: Pointer, Draw, Erase, Mute,
  Velocity, with the same keys and the same shared choice (Paint and Slice draw here). Asked for by the user
  ("toolbox as the simple piano roll").
- **Pointer selects; a click on an empty step still turns it on.** A step grid is for toggling steps, so a plain
  click keeps doing that (and selects the new step, as Draw does for notes in the piano roll). A drag from empty
  space draws a selection box; a drag on a selected step moves the selection by whole steps and lanes. Draw keeps
  the old click-and-drag painting.
- **One selection for both drum views and the inspector.** The inspector edits the selection of whichever note
  editor is showing (`NoteTarget`), so operators, charts and fields work on steps as on notes.
- **The step menu acts on the selection** and carries the operators, because the in-panel editor has no
  inspector.

## 38. Automating device knobs (docs/DEVICES.md §1.3, design canvas frame 7)

- **Entry point is the knob**: right-click → Automate in the arranger / in this clip, as agreed on the canvas.
  The lane appears where the user will draw it, next to the CV lanes or as the clip's lower lane.
- **Lanes act only while playing.** Stopped, the knob is free to turn and audition; otherwise every hand turn
  would snap back with no way to hear it.
- **Lane values set the base; modulation adds on top**, so an LFO keeps wobbling around an automated value.
- **Knob lanes are not CV lanes**: no AUTO channel, range or slew in their menu, their own limit (16), and they
  are never exported as CC; with devices applied, MIDI export already plays what they do.

## 39. Project files and DAWproject (Trello: "Project save and load in its own format")

- **The editor's Export button became a File menu** (Open, Save, Save as, Export MIDI, Export DAWproject), and the
  module's right-click menu has the same items, so a project can be saved without opening the editor.
- **A .tessera file is the module's own data** (the same JSON a patch stores for Tessera: song, settings, editor
  layout) marked `"format": "tessera-project"`, minus what belongs to the patch: the module ID, the MIDI device,
  the panel's width and editor size, and the project's own path. Opening it is one Rack undo step (module
  change), and track IDs are kept, so output modules of the same patch stay bound.
- **DAWproject over other DAW formats**: it is open (MIT), documented with an XML schema, and read by
  several major DAWs. Tessera writes it without libraries: the XML by hand and the zip uncompressed.
  Tests check it against the official schema.
- **One clip per arranger instance, with the notes as MIDI export plays them** (the same Timing, Note operators
  and Devices choices). Loops inside instances are written out, so what the DAW shows is what Tessera played.
  Launcher clips become scenes, one pass long and looping. AUTO lanes become CC automation. Per-note expressions
  and device chains themselves don't go across (DAWproject has no place for Tessera's MIDI devices).

## 40. Solo adds by default (Trello: "Multiple solo tracks")

- **A click on S toggles that track's solo and keeps the others**, like drum lanes and the panel's Tracks
  menu. EDITOR_UX §6.2 had it the other way (exclusive solo), and soloing a second track silently
  dropped the first.
- **Shift+click on S un-soloes every other track and toggles this one**: on if it was off, off if it was on (then
  nothing is soloed). It replaced Ctrl/Cmd+click, which only ever turned the track on; one modifier is enough.
- **Alt+S adds the selected tracks to the solo** the same way. Arm stays exclusive: recording usually goes to one
  track.

## 41. Ctrl/Cmd+Alt+drag on an arranger clip makes an independent copy (Trello QA)

- **§19 and §21 disagreed**: §19 gave Ctrl/Cmd+Alt+drag on a clip to independent copies, §21 to selection
  from anywhere, and the code followed §21, so no drag made an unlinked copy.
- **On a clip it copies (§19); from empty space it selects (§21).** Selecting already has Ctrl/Cmd+drag from
  empty space, so nothing is lost, and the modifier keeps one meaning per place: Ctrl/Cmd = copy, Alt = the
  independent / free variant (as Alt frees snapping and slips elsewhere).

## 42. One song view, a Plays switch, and maximize / restore (design review, docs/design/PROPOSAL.md §2)

- **Clips and timeline are always on screen together**: the clips (the launcher panel of §33) sit between the
  track headers and the timeline in the full editor and in the panel editor. The full launcher grid (tracks as
  columns), the "Show launcher / Show arranger" button and the panel editor's Launcher / Arranger / Both / Piano
  roll strip are gone. The corner button that hid the clips is gone too.
- **What plays is one clear switch: Plays [Clips | Timeline]** in the transport. It is the play mode of §19 (the
  panel's MODE_PARAM, with its undo and CV mapping), so nothing changes in the engine. The half that plays is lit,
  jade while the transport runs.
- **The playing section is framed in jade; the other steps back** (drawn at about half strength, still fully
  editable). While the timeline plays, a clip that took its track over (§19) stays lit among the dimmed clips.
  The transport's "Back to arr." button is gone; the arranger's own button reads "Back to timeline".
- **Layout changes are maximize / restore**: the song pane and the notes pane each have the button in their
  top-right corner (same as Shift+Tab or double-clicking the splitter). The panel editor's top-right corner,
  left of the top pane's maximize button, holds play, stop, record and loop and a "Full editor" icon (a window
  with an arrow leaving it, unlike maximize's corners), so they stay in view whichever pane is maximized (Trello,
  13 Oct). They are the same buttons as the full editor's transport bar.
- **F2 widens the clips** (performing): they take the width but a strip of timeline; again restores them. The
  clips' edge still resizes them up to half the room.
- **The clips' toolbar** (above their scene buttons): launch quantize, Stop all, Record into timeline and Capture,
  which used to live on the launcher grid. A "+" after the last scene adds one.
- Lost with the grid: a folded group's single column that launched every clip of the group in a scene.

## 43. Colour schemes (design review, docs/design/PROPOSAL.md §3.11)

- **Eight schemes in one menu** (the transport's instance menu and the module menu): Plaster and Sand (light),
  Slate (dark, soft grey), and Gruvbox, Nord, Catppuccin (Frappé), One Dark and Monokai, which take those editor
  themes' published palettes. Walnut was dropped on 13 Oct (Trello); a saved Walnut opens as Gruvbox. **Follow
  Rack** is the default: Plaster with light panels, Slate with dark ones. The choice is plugin-wide, saved in
  Rack's settings by name.
- **The panels follow the scheme too.** With Follow Rack they follow Rack's panel setting like every other module;
  a picked scheme colours every Tessera panel, with Rack's light or dark jacks and screws to match. Module screens
  stay dark in every scheme.
- Track colours are stored once; light schemes draw them deeper so tiles keep their contrast.

## 44. Renaming clips (Trello: "No clear way to rename a clip")

- **A clip is renamed from its right-click menu** in the clips panel and in the arranger (Rename), or by
  **double-clicking its name in the notes pane header**, which lights on hover. All three
  open the same name field; Enter or clicking away commits, as one undo step. A linked clip has one name, so every
  use changes.
- No keyboard shortcut yet: F2 widens the clips and Ctrl/Cmd+R is step input.

## 45. Arpeggiate follows the held notes (Trello: "Arpeggiate for these notes doesn't look ok")

- **Each step plays one of the notes held at that moment**, like an arpeggiator played live. A voice that moves
  inside a chord (F to E under a held A and D) joins the running arpeggio instead of starting a second one on top.
- **A new chord restarts the pattern** when most of the held notes have just begun; a moving inner voice doesn't.
  Notes up to a 1/128 late (strummed, humanized) count as starting on the step; after a rest the steps start
  again with the next note.

## 46. A new Tessera starts without clips (Trello: "Let's not initialize with a clip")

- A fresh module has its 3 instrument tracks and 1 drum track but no clips; the editors' empty-state hints say how
  to make one. The old demo clip is kept as `addDemoClip` for tests.

## 47. Clip Length ×2 / ×½ / custom (Trello: "Clip: Scale by 200%, 50%")

- **Length ▸** in the clip's right-click menu, in the clips panel and on the timeline: ×2 longer (half speed),
  ×½ shorter (double speed), or a typed percentage. It rewrites the clip itself: note positions and lengths, its
  loop and its automation, and every timeline clip that uses it grows or shrinks with it, so none is cut. One undo
  step. Per-instance stretch on the timeline (Alt+drag an edge) stays the way to change one timeline clip only.

## 48. The timeline clip's length in the piano roll (Trello, 13 Oct)

- Opened from a timeline clip (the arranger selection is that one clip), the piano roll draws the loop's repeats
  faded up to where that timeline clip stops, with a yellow line there and, in the ruler's loop strip, a thin
  yellow bracket under the loop bar for the span it plays, with a handle at its end (a text tag was rejected:
  brackets like the loop bar read better). Dragging the handle sets the timeline clip's length (snapped; Alt
  free), one undo step. The header adds
  "N bars on the timeline". Repeats are not editable: they are the same notes. A clip opened from the clips panel
  clears the timeline selection, so none of this shows.

## 49. Ghost notes follow how the other clips play (Trello, 13 Oct)

- **Opened from a timeline clip**, ghosts show what the ghosted tracks play on the timeline under it (their loops,
  stretch and transpose included), placed in this clip's time. "Other clips of this track" then means the
  track's other timeline clips under this one (stacked clips).
- **Otherwise** a looping ghost clip repeats its loop across the view, lined up as if launched together with the
  clip being edited (both start at their loop start). Clips opened together (layered editing) are shown as they
  are, since their notes are editable.

## 50. Humanize is its own device (Trello, 13 Oct)

- **Humanize** (Feel group) loosens timing (later only, up to half a 1/16), velocity ± and length ±, a little
  differently on each pass but repeatably (the song seed, the device, the pass). **Random** keeps pitch ± and the
  chance to drop. Each device does one job.
- Songs saved with Random's old velocity and timing knobs load as Random followed by a Humanize with the same
  values; automation lanes and modulations aimed at those knobs move to Humanize's (Serialize.cpp).

## 51. Calmer track rows, clips open on their notes (design review 13 Oct, step 1)

The review sorted every control by how often a producer uses it (artifact "Tessera Editor Overhaul"). Step 1:

- **Track rows lay out the name first.** A colour tile carries the track number; drum tracks add a small drum
  mark; the name gets the rest. Then arm, the lanes arrow, M and S at a fixed size, smaller below 180 px (the
  in-panel editor, where names used to vanish). The type icon, the green output dot and the "1 voice (auto) ·
  no output module" line are gone; voices and output stay in the inspector.
- **Nothing hides until hover** (the user rejected arm-on-hover as not intuitive). Arm is a dim ring until armed
  (then red); the lanes arrow is dim until the track has lanes. Each part has a tooltip.
- **A missing output module shows only when missing**: an amber jack left of arm, whose tooltip says to add a
  Track Out or Drum Out.
- **Opening a clip never shows empty rows.** A restored scroll (saved with the patch, or from another clip) that
  shows none of the clip's notes is replaced by zoom-to-fit.
- **The device strip stays closed on a track with no devices or modulators**, unless it was opened by hand
  (Devices, F3, the bar) while that track was selected. Adding a device opens it as before.
- **Empty clip slots are recessed wells** with a faint stop square, full strength while the track plays a clip.

## 52. The in-panel editor's layout and one tool switch per pane (design review 13 Oct, step 2)

- **One tool switch per pane** (the user's choice: a switch in each pane, not one in the transport). The song pane's
  two rows of tool and mode icons are now Select, Draw and a caret. The caret's menu holds Erase, Slice, Mute, Range
  and the timeline's settings (Ripple, Stack, Automation follows clips). While Erase, Slice, Mute or Range is on, the
  caret shows its icon, lit. A RIPPLE tag shows in the corner while ripple is on, where there is room.
- **The clips' toolbar is gone.** "Launch 1 bar ▾" and Stop all sit in the corner above the track names; the
  launch menu also holds "Record into timeline", and the chip is red while it records. Capture stays on the record
  button's right-click menu and Ctrl+Shift+R. Where the corner has no room (the in-panel editor), the tool caret's
  menu holds Launch quantize, Stop all clips and Record into timeline. The corner's "+" and track count are gone:
  "+ Add track" under the last track does that job. "Back to timeline" sits on the timeline's ruler.
- **In the in-panel editor**, the transport (play, stop, record, loop) sits top-left above the track names, beside
  the song pane's tool switch; Full editor and maximize stay top-right. With the notes maximized both move into the
  notes header as before. The ruler's strips are thinner there (27 px instead of 40), and new track rows start at
  24 px (the shortest a row gets).
- **A song pane narrower than 600 px folds the clips to a CLIPS tab while the timeline plays**; the tab opens and
  closes them. While the clips play they show as usual.
- **Timeline clips keep their full-colour title bar** in both editors. The review's option B (no bar, the name in
  8.5 px over the notes) was built and then rolled back at the user's request after trying it.

## 53. The notes header: chips with words (design review 13 Oct, step 3)

- **Left:** a clip chip ("BASS · Verse · 8 bars ▾", in the track colour) whose menu renames the clip, loops it, sets
  its length (lengths, Double, Halve), saves it as a Tessera clip file or exports it as MIDI; then Devices. The
  track arrows are gone (click a track; Ctrl+Up / Ctrl+Down still step). The selection count and "N bars on the
  timeline" follow as plain text where there is room.
- **Right:** the pane's Select / Draw / ▾ switch (Paint, Erase, Slice, Mute, Velocity, and "Draw chords" with the
  chord stamp's shapes), "Snap 1/4 ▾", the key chip, "Notes ▾" (the transforms), "View ▾" (ghost notes, chance
  and repeats on notes, hearing notes while editing, follow the playhead), maximize.
- **The key chip** opens the key picker with three switches under it: highlight the key's rows, snap notes to the
  key, fold to the key's rows. The chip is lit while snap or fold is on. The three icons are gone.
- **Narrow headers use short forms**: Notes and View merge into "⋯" (which also offers the key when its chip has no
  room), the key reads "F#m", snap drops its "Snap" word, and the clip chip shows the track's name only.
- **The lower lane's label is its picker** ("Velocity ▾" in the keyboard column); the row of nine tabs is gone.
- The drum editor's header loses its track arrows and its five tool icons and speaker for the same Select / Draw /
  ▾ switch (Erase, Mute, Velocity, and "Hear steps while editing").

## 54. The transport: the song's controls only (design review 13 Oct, step 4)

- **The key leaves the transport.** It was shown twice; the notes header's key chip edits the song's key or the
  clip's own (§53).
- **"4/4 ▾" holds the time signature and swing.** Swing finally has a control (Off, 10–100%); the chip reads
  "4/4 · swing 30%" while swing is on. Setting it is one undo step.
- **Follow is a small on/off icon.** Turning it back on restores the last mode; Page or Scroll is chosen in the
  notes header's View menu.
- **Record has a ▾** for its settings (count-in, snapping played notes, new clip length, MIDI input, Capture), which
  were only on its right-click menu. Undo and redo stay, as small icons (the user's choice).

## 55. The inspector: settings you can change (design review 13 Oct, step 5)

- It stays **open by default** in the full editor (the user's choice: people would miss it).
- With no notes selected, the TRACK section holds **Voices** and **Recording** (armed, input, MIDI channel,
  monitor) as chips that open their menus, which were only in the track's right-click menu. **Output** says
  "None" with a hint to add a Track Out or Drum Out when nothing plays the track.
- The read-only lines that repeated the headers (Type, State, Notes, Range, Used) are gone. The clip's name, length,
  loop and key stay as facts; the notes header's clip and key chips change them.

## 56. Nothing lost from the header rework (13 Oct, after review)

- **Hear notes while editing is a button again** (a speaker at the right of the notes and drum headers): the user
  switches it often. It left the View menu.
- **Erase from a tool menu with a selection deletes the selection** and keeps the tool, as the Erase button did.
- **The clip chip's menu has a Track submenu** (previous, next, every track), so tracks can be switched with the
  mouse even when the notes are maximized.
- **A narrow notes header's "⋯" holds whatever has no room** (Snap, Key) besides Notes and View, and it stays
  longest, so every control is reachable at any width.

## 57. A track can play another track's notes, or the external inputs, without arming (13 Oct)

- **Input** (inspector TRACK section and the track's right-click menu): what a track plays besides its clips, all
  the time: *Nothing* (the default), *External* (any input, the REC CV inputs, or MIDI on the Recording menu's
  channel), or *another track's notes*, either **after its devices** (what it plays: arpeggios, echoes) or
  **before its devices** (its clips as written). The notes go through this track's own devices, so one melody can
  drive a harmony track with its own chord or arp. Its own clips keep playing alongside (the user picked "no
  arming needed"; the other two choices took the recommended defaults: both after and before, and clips keep
  playing).
- **Arming is for recording.** An armed track still takes the external inputs and plays them while monitoring, as
  before; the computer keyboard still plays only armed tracks. Recording only records armed tracks.
- **A muted (or un-soloed) track still feeds the tracks that listen to it**: it runs its clips and devices but
  sounds nothing itself, like a MIDI send.
- **Loops can't happen**: the menu greys out tracks that already listen to this one, and the engine stops a note
  after four listening tracks in a row.
- Engine: a note entering a track (`deliver`) is copied to the tracks listening before its devices, a note leaving
  the chain (`sound`) to those listening after; open notes (held keys, legato) keep a forwarded ID so their end
  follows. Listening tracks wake at once when a note arrives. Nothing allocates.
- Saved as `listen` / `listenTrack` on the track, left out when nothing is listened to. A deleted source shows
  "Missing track" and plays nothing; undoing the delete brings it back.

## 58. Ctrl+L loops what is selected (13 Oct)

- Ctrl+L (Set loop to selection) now also works on **selected timeline clips** (the song's loop covers them) and on
  **selected notes or drum steps** (the clip's loop covers them, out to the grid or the beat). It turns the loop on.
  A time range keeps working as before.

## 59. Note names with flats, and "Fold: only the scale's rows" (13 Oct)

- **View ▾ → "Name notes with flats (Db, not C#)"**: every note, key and pitch name in Tessera uses flats. It is a
  display preference saved with the plugin's settings (like the colour scheme), not part of the song. The device
  knobs that list roots keep their sharp names for now.
- The key picker's fold switch reads "Fold: only the scale's rows".

## 60. MIDI file import (14 Oct, ahead of v1.1)

- **Three ways in**, each one undo step:
  - an empty launcher slot's menu → "Import MIDI file…": one clip in that slot;
  - empty timeline space's menu → "Import MIDI file here…": one clip placed where you clicked, over what is there;
  - Tessera ▾ (and the module menu's File items) → "Import MIDI file as tracks…": one new track per part, each with
    its clip on the timeline at bar 1 and in its first slot, and the file's time signature. The file's tempo is set
    too, as its own undo step (tempo is the module's knob, not part of the song).
- **Parts**: each MIDI track's channels are parts (a type 0 file splits by channel). A file with several parts asks
  which one to put in a slot or on the timeline, or all of them as one clip. Channel 10 becomes a drum track with a
  lane per note it uses (General MIDI names where known, at most 16 lanes).
- **Notes** keep their timing (converted to 960 PPQ), velocity and release velocity; notes still held at the end of
  a track stop there. Clips are whole bars long and loop. Files timed in SMPTE frames are refused, as is anything
  that isn't a MIDI file; a file without notes says so.
- `model/MidiImport` is pure and tested; `ui/editing/ImportMidi` reads the file on the UI thread.

## 61. Seeking from the piano roll and the drum editor (14 Oct)

- The notes pane's ruler seeks like the song's: a click moves the playhead there, a double-click plays from there
  (the drum editor's bar ruler too, on the 1/16s).
- **While the clip plays from its slot** (or the clips play and the clip is in a slot), the clip restarts from that
  point at once. The launch event carries the clip time and the audio thread works out the start, so it lands on the
  sample it is handled on.
- **While the timeline plays**, the song jumps to where the timeline plays that point of the clip: in the selected
  timeline clip, else the one under the playhead, else the first on the track; in the loop pass the playhead is in,
  else the first pass that plays it (`songTickAt`, the inverse of `clipTimeAt`).
- Nothing happens when the clip is in neither place for what plays.

## 62. New song (14 Oct)

- Tessera ▾ and the module menu's File items start with **New song**: the song a new Tessera starts with, the knobs
  (tempo, loop, Plays) at their defaults, and no project file, as one undo step. It runs the module's reset through
  Rack's engine, as Rack's own Initialize does, so the audio thread is paused meanwhile.
- **It forgets the old song in the editor too** (also on Rack's Initialize): selections, the open clip, layered and
  ghost clips, the step cursor, and each view's zoom, scroll and per-track sizes. Tools, snap, scale switches,
  export settings, the layout and the panel's width stay. Track 1 is selected.
- **IDs keep counting up** from the old song's, so nothing that still points at it can match the new one (a
  re-imported file used to get the same note IDs, and the old selection showed on them again).

## 63. Bending notes on the piano roll (14 Oct)

- **Every note's pitch curve is drawn in the note grid at the pitch it plays.** A glide from C4 to E4 is a line that
  leaves the C4 note and ends on the E4 row, so you can see which note it heads for. Where the curve rests on the
  note's own pitch nothing extra is drawn; the note already says so. Folded rows are crossed smoothly.
- **A new Bend tool (N, in the tool menu) edits the curves there.** Drag a curve point, or press anywhere on a
  note's curve to add a point there (on the grid; Alt anywhere). A click without a drag adds a point that holds the
  pitch, so "hold, then glide" is a click and a drag. Right-click a point to delete it; when only the resting point
  is left, the curve goes and the note plays straight again. Empty space starts a rubber band.
- **Up and down snaps to whole semitones; with scale snap on, to steps of the key; Alt is free.** This is the other
  way round from the lane (Shift snaps there): in the grid the point of a bend is to land on a row.
- **Sideways moves the point in time** on the grid; a few pixels of wobble are ignored, and Shift keeps the time.
- **Bending a selected note bends the whole selection**, each at its point nearest the grabbed spot (a new one if it
  has none), by the same amount, or by the same number of key steps with scale snap. So C-E-G glides to D-F-A in
  C major. One undo step: "Bend 3 notes".
- The readout names the note the grabbed point lands on and the offset ("D4 +2 semitones").
- In the drum editor the Bend tool acts as Select: drum hits have no pitch to bend.
- **Arching a glide:** each glide has a hollow handle in its middle (as in the lower lane); drag it up or down to
  arch the glide from a straight line to a slow or fast start. Alt+drag anywhere on a glide does the same. A
  selected chord arches together. Shift is fine adjustment.
- **Curves stay in time when a note's length or start changes** (edge drags, typed lengths, slice, chop, legato,
  strum, glue, quantized ends, Length ×): a glide at beat 3 stays at beat 3. A part that now falls outside the note
  is cut off, with the value the curve had at the new edge, so what still plays inside the note sounds the same.
  Time-stretching (Alt+Shift drag, the transform box) still scales curves, because it scales everything.
  (`expr::keepInTime`.)

## 64. Timbre and pressure shaped on the notes (14 Oct)

- **The Bend tool edits three curves:** the tool menu lists them under "Curves on the notes": Bend pitch, Shape
  timbre, Shape pressure. N picks the tool; N again moves on to the next curve.
- **Timbre and pressure have no row to aim at, so they get a strip:** a panel four rows tall just above the selected
  notes (below them when there is no room above), as long as they are, 0% at the bottom and 100% at the top. Notes
  that overlap in time share one strip; the highest is the lead note, drawn bold, the others faint. Each note's
  curve is drawn over its own time span.
- **Editing in the strip works as bending does:** press to add a point (on the grid; Alt anywhere), drag a point,
  drag the hollow middle handle or Alt+drag a segment to arch it, right-click a point to delete it. The selection
  follows the grabbed note: every note gets its point at the same beat (not at the same fraction of its length,
  so notes of different lengths swell together). Up and down is free; there are no rows to snap to.
- **The strip shows while the Bend tool shapes timbre or pressure, or while the lower pane shows that curve**, with
  any tool, so what you see can be edited. With the Bend tool on timbre or pressure, a press on a note selects it,
  which opens its strip.
- **The lower pane stays a second way to edit,** and it follows the same choice: picking Pressure below makes the
  Bend tool and the strip work on pressure, and picking a curve in the tool menu switches the lower pane when it
  shows a curve. Whichever was picked last wins (`PianoRollView::syncCurveTool`).
- Mono values (channel pressure, mod wheel) need nothing new: they are clip automation lanes on the AUTO jacks.
- Chord bends also put the other notes' points at the grabbed beat now, instead of at the same fraction.

## 65. Picking curve points, and bending without switching tools (14 Oct)

- **Curve points can be picked and moved together.** With the Bend tool, a drag from empty space draws a box that
  picks the curve points inside it (and selects the notes it touches and the picked points' notes, so a chord can
  then be bent together). In a timbre or pressure strip, a drag away from the curves does the same within the
  strip. A click on a point picks just it; Shift+click adds or removes one. Picked points are drawn in the
  selection colour.
- **Dragging a picked point moves all of them** by the same time and the same amount (whole semitones, or key
  steps with scale snap, as one point would move). Delete deletes the picked points, not the notes; a curve left
  flat is dropped. Picked points are let go when the tool or clip changes, on Select all / none, and when Select
  clicks anything other than a point, so Delete is for notes again.
- **No tool switching for the common mix of edits:** with the Bend tool, a note's edges resize it as with Select
  (curve points and arch handles come first where they overlap). With Select, the bend points and arch handles of
  selected notes can be dragged, picked and deleted; adding new points still needs the Bend tool.
- In a strip, a press only adds a point near a curve now (it used to anywhere over the lead note), so the rest of
  the strip is free for picking.

## 66. No Bend tool: the lower pane's curve is the switch (14 Oct, replaces the tool in §63–65)

- The extra tool in the tool menu was confusing next to Select, so it is gone (and its N key). **What the lower
  pane shows decides what a press on a note does in the grid:**
  - **Pitch:** Select and Draw bend notes instead of moving them. A press on a note adds a bend point there and
    drags it; points, arch handles and edges work as before (edges still resize). Empty space: Select draws a box
    that picks points, Draw still draws notes. Pick another lane (Velocity…) to move notes again.
  - **Timbre / Pressure:** the strip above the selected notes, as in §64.
- **Picking points works everywhere a curve is drawn:** a box from empty space in the grid (Select), in a strip,
  or in the lower lane picks the points inside; a click picks one, Shift+click adds or removes. Dragging a picked
  point, in the grid or the lane, moves all of them. In the lower lane a press now adds a point only near a curve
  (within 8 px); farther away it draws the box.
- **Delete** deletes picked points while the lower pane shows their curve (or the last press was in the lane);
  otherwise it deletes notes.
- Even without Pitch shown, Select can still drag the bend points of selected notes; adding points needs Pitch.
- In the lower lane, pitch points now snap to whole semitones as in the grid (Alt is free), instead of Shift
  snapping.
- **Where a click would add a curve point, the mouse shows an add-point cursor** (a point ring with a plus, drawn by
  Tessera since the system has none): on a note while Pitch is shown, near a curve in a strip, and near a curve in
  the lower lane. Over a point it is the move cursor, over an arch handle up/down.

## 67. Curves are drawn with counted steps (14 Oct, fixes a freeze)

- **The bug:** the bend lines on the notes were drawn by stepping x until it reached the end of the note. Rack
  builds with unsafe float math, and the compiler rearranged that test so it could stay false forever. The loop
  then added line points without end, about 250 MB a second, until the computer ran out of memory and froze
  (no swap). Found by replaying random editing in a memory-capped Rack under gdb: the stack was in
  `NoteGrid::drawBends`, inside `nvgLineTo`.
- **The rule now:** every curve drawn across a note samples with `draw::sampleSpan`, which counts its steps and
  covers only the part on screen. The muted-note hatching counts too. A note far wider than the view (zoomed in)
  no longer costs hundreds of thousands of points per frame either.
- Never write a drawing loop that runs "until x reaches the end" when x is clamped to that end.

## 68. Curves play across the note as written (14 Oct, fixes "stretched bends play as before")

- **The bug:** the engine spread a note's curves across the part of the note that sounds. A note cut short (by the
  song loop end, a clip loop, or the gate length) squeezed its whole curve into that part. After Length ×2 the
  note grew past the 4-bar song loop, the cut part stayed 4 bars, and the bend played at the old place while the
  editor drew it stretched.
- **Now** the curve spans the note as written (its length times the timeline clip's stretch), from its start, as the
  editor draws it; the part after the cut is simply not heard.
- **Edits no longer flatten sounding bends.** Every edit hands the engine a new snapshot; sounding notes carried
  over but dropped their curves (they lived in the old snapshot), so a long bent note played flat until it started
  again, up to a whole loop later. Each voice now remembers its note's clip and note ID and finds the curves again
  in the new snapshot, so it keeps bending, along the edited curve if that changed (`Engine::reattachCurves`).

## 69. Keys go to the pane under the mouse; touching edges belong to their own side (19 Oct)

- **Keys act on the pane under the mouse**, in the full editor and in the panel editor. A key press moves the focus
  ring to that pane first. With the mouse outside both panes (on the transport or the inspector), the pane last
  clicked keeps the keys, and Tab still switches it. Before, the full editor sent keys to the pane last clicked:
  after launching a clip, Ctrl+A then Ctrl+D over the notes selected and copied every track.
- **Where two notes touch, each side of the seam grabs its own note's edge.** Edge zones reach a few pixels out of
  a note, so at a seam both notes' zones overlap. The edge of the note the mouse is inside wins, then the nearer
  edge. Before, the earlier note always won, so the later note's start could not be dragged from the seam.
  Arranger clips follow the same rule.

## 70. First-run fixes: new clips play, Ctrl+D repeats on the bar, clips grow to hold new notes (QA 14 Oct, 19 Oct)

- **A new clip on a silent track starts launched.** In Clips mode, a clip made on a track that plays nothing
  (and has nothing queued) is launched at once: while stopped it waits for Play, while playing it starts on the
  launch grid. This is the same rule a song follows when it opens on each track's first clip. Before, drawing a
  clip and pressing Play played nothing until you found the slot's triangle. A track that already plays a clip
  keeps it.
- **Ctrl+D rounds the copy's shift to the bar or the beat.** The step is the bar when the selection is longer than
  half a bar, the beat when it is longer than half a beat, otherwise the snap grid (and never less than the grid).
  A one-bar phrase whose last note is a 1/16 now repeats on bar 2, not at 1.4.2 where that note ends.
- **Notes put past the end of a clip grow it, in whole bars.** When the clip's loop runs to its end, notes drawn,
  pasted, duplicated or moved past that end extend the loop and the clip to the bar that holds them, in the same
  undo step. A note that only rings past the end, or hangs a 1/32 past a bar line, adds no
  bar. Notes that were already past the end stay out when edited, and a loop that ends inside the clip is left
  alone: both were set on purpose. Clips without a loop already follow their notes.

## 71. First-run polish: clip names, plain operator words, bar.beat in the inspector, quieter warnings (QA 14 Oct, 19 Oct)

- **New clips are called "Clip N", numbered across the song.** "Track 1 1" read like a track name (and like
  "Track 11"). N is one past the highest "Clip N" in the song, so names stay unique across tracks. Recording and
  drawing on the timeline name clips the same way. Old songs keep their names.
- **Occurrence and Recurrence are "Only when…" and "Every … passes"** in the inspector and the step menu. The
  model and the file keep the old names.
- **The inspector writes a note's start as the ruler does (1.4.1), and its length in bars and beats** ("1 bar 2
  beats"). A start off the 1/16 grid gets a fourth number, the leftover ticks (1.2.1.120).
  Typing a start takes a position ("2.1.1", "2.3", "2"); +1 / -1 still move by beats. Typed numbers for start and
  length count beats of the time signature (eighths in 6/8), as the ruler does.
- **"Show the whole song" on the timeline reaches at least 16 bars.** A new song opened on bars 1–5 with labels
  every 1/8; now it opens on bars 1–17 and looks like a song. Zoom to fit with nothing selected does the same.
- **The amber "no output" mark shows only on tracks that have notes.** A fresh song showed it on all four tracks
  before anything was made. The inspector's Output row still says "None" with the hint for the selected track.
- **Zoom to fit in the piano roll keeps rows at least 12 px tall**, the height where note names show. In the
  in-panel editor an octave squeezed rows to 6–7 px lines. When the notes don't fit at that height, the view
  centres on their median pitch, where most of them are; the rest is a scroll away. In the full editor this only
  matters for clips spanning about three octaves or more.
- **The drum editor's header starts with the same clip chip as the piano roll** (name, length, and the menu with
  track, rename, loop, length, save and export), then Devices. Its own length button and plain title are gone,
  and with them double-click-to-rename on the title: Rename… is in the chip's menu, as in the piano roll.

## 72. A MIDI channel on every note, coloured, and a channel filter (Trello, 19 Oct)

- **Each note has a channel, 1–16, starting at 1.** One clip can hold several parts (bass on 2, chords on 1),
  and another track can pick one of them out: listen to the track, add a Note Filter set to that channel.
- **Setting it, for one note or many:**
  - right-click a note: **Channel ▸** lists 1–16 with their colours and a check on the selection's channel;
  - the inspector's **Channel** row (drag, or click and type; `+1` shifts each note by one channel);
  - drawing (Draw, Paint, step input) uses the selected notes' channel when they share one, else the channel of
    the last note clicked or drawn, the way length and velocity already follow the last note touched. So after
    putting a note on channel 2, drawing goes on in channel 2. There is no channel picker in the toolbar: the
    header is full, and this rule does its job.
  - "Select all notes on this channel" sits next to "Select all notes of this pitch".
- **Colours:** channel 1 is the track's colour, so songs that don't use channels look as before. Channel N steps
  around the 16-colour track ring from it, halving the gap each time (opposite, then the quarters, the eighths), so
  the first channels used are easy to tell apart. Velocity still sets brightness; the velocity lane's stems take
  the same colours; the inspector's Channel row shows a swatch.
- **Playback:** the channel rides with the note through the device chain (chords, arpeggios and echoes keep it)
  and into tracks that listen. The Note Filter gained **Channel: Any / 1–16**.
- **Recording:** notes from MIDI keep the channel they came in on. CV and the computer keyboard record on
  channel 1 (CV's poly channels are voices, not parts).
- **Export:** MIDI and DAWproject export gain **Use note channels** (off by default: notes go out on the track's
  channel as before). With MPE on, MPE picks the channels.
- Saved only when not 1 (`"ch"` in the note's extras), so older patches load unchanged.

## 73. Expression from MIDI input, and one switch to record (22 Oct)

Plan: `docs/design/EXPRESSION_INPUT.md`. Replaces the per-lane arm and the knob/CV "track lanes only" rule of §26.

- **MIDI input reads everything a keyboard sends** (`MidiReader`): poly aftertouch and MPE member channels go to the
  note's pressure, timbre and pitch curves; pitch bend, channel pressure and CCs are per-channel values. A
  controller that sends the MPE Configuration Message switches the input to MPE by itself.
- **Arming the track is the only switch.** Notes, expression, moved CCs and the track's knob and CV lanes all record
  into one take, where the notes land (launcher clip or timeline), as one undo step. Lanes lose their own arm.
- **No setup for controls:** bend becomes a "Pitch bend" clip lane added to V/OCT (±2 semitones); channel pressure
  a "Pressure" lane played on PRESS; any CC moved during the take its own lane on the next free AUTO channel.
  Untouched controls make no lane.
- **PRESS plays the larger** of the note's pressure and the channel pressure, so a keyboard that sends both is not
  doubled. Both stay stored apart.
- **Rest values:** bend, pressure and sustain return to 0 when no clip lane plays them.
- **Sustain is only a lane;** it does not lengthen notes.
- The note pitch curve grows to ±48 semitones (MPE's default). MPE notes record on channel 1.

## 74. Lanes show whether they record; the track arm is the switch (22 Oct)

Builds on §73. Mockup agreed 22 Oct.

- **The lane arm button is gone.** In its place a mark: red dot (records), amber "!" (its source is missing, with the
  reason on the header's second line), crossed circle ("Don't record this lane"), nothing (no source, or the track
  isn't armed). A lane you only draw has no source and shows nothing, so arming never fills headers with warnings.
- **Lane menu, "Recording":** Records from (a MIDI control / the mapped knob / AUTO REC input), Learn (the first MIDI
  control moved or knob touched), AUTO REC channel (free, default the lane's own), Record mode (knob and CV; choosing
  AUTO REC switches to Write), Don't record this lane. Clip lanes: a MIDI link and Don't record; knobs and AUTO REC
  are set on the track lane of the same output.
- **Where knob and CV takes land:** a launcher take writes the clip's lane on the same output (made from the track
  lane, without its knob mappings); a timeline take writes the track lane. All in the take's one undo step
  (`Recorder`, `RecordEvent::Kind::LaneValue`).
- **Starting from a knob:** the track menu's "Automate a knob: touch it in the rack" makes the lane, maps the knob and
  names the lane after it. Rack has no supported way to add items to other modules' knob menus, so not there.

## 75. Overdub takes stack; lanes for MIDI controls by name (22 Oct)

- **Timeline Overdub records a new clip stacked over what was there** (§32), which stays untouched: a take can be
  moved, muted or deleted on its own. Before, the old notes were copied into the take and the old clips cut around
  it. Replace still cuts. Launcher recording into a playing clip still adds to it pass by pass (a slot holds one
  clip).
- **Stacked clips' lanes: the clip on top wins, per AUTO channel** (replaces "the earlier one plays it" in §32).
  The engine reads every clip under the playhead, bottom to top (`Engine::lanesClips`), so a take with only a mod
  wheel lane doesn't silence the cutoff lane of the clip below. Modulate still moves around the track lane.
- **"Add automation lane" is one menu** (track header row, track menu, the lower pane's "Add clip lane"): Empty lane,
  Automate a knob (track lanes), MIDI control (pitch bend, pressure, common CCs by name, any CC by number). Records
  from offers the same list, so no controller is needed to set a lane up.
