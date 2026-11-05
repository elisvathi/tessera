# Audio, mixer and performance: the musician's-chair review

Status: **proposal, 5 Nov 2026. Nothing in this document is built** unless a line says "today". It reviews the audio
and mixer work of `AUDIO_MIXER.md` (phases 0–4, DECISIONS §94–130) from a producer's and a live performer's chair,
across the whole module system, and proposes the overhaul that DECISIONS §118 promised ("a full UX overhaul comes
later").

Mockups: `docs/design/mockups/audio-ux/index.html` (one self-contained page, Slate dark and Plaster light), published
privately at https://claude.ai/artifact/ESVC1oxbxuxvfxGanJraHh.

Where this document and DECISIONS.md disagree, DECISIONS.md wins until the user answers the open questions (§7).

---

## 1. Summary: the ten changes that matter most

Ranked by how much they help a producer and a performer, for the effort. Each links to its proposals (§4), and the
phase it belongs to (§6). Effort: **S** a few days, **M** one to two weeks, **L** more.

| # | Change | Proposals | Who gains | Effort | Phase |
|---|---|---|---|---|---|
| 1 | **A first minute that just works.** A loop dropped on the panel plays when you press PLAY. With the editor closed, Tessera's display says what happened ("Song set to 92 BPM"). When the Audio module's inputs are already taken by a synth, one helper puts Tessera's mixer in front of it instead of refusing. | A1, L4, P5 | Everyone | S–M | 1 |
| 2 | **A mixer that fits any height, and scrolls.** Four layouts picked by the pane's height (strips, compact strips, tiles, meters), strips that scroll sideways under a pinned Master, a level bar in every track row (the panel editor's 24 px rows too), and keys in the mixer. | M1, L2, M3 | Everyone who mixes; the panel editor most | M | 1 |
| 3 | **Audio slots that loop like a looper.** On an armed track, ● in an empty slot records; a second press closes the loop on the launch line and it plays on without a gap; ● on the playing loop overdubs a layer; undo peels it back. The panel's REC and the Perform module drive the same steps, so a foot switch can run it. | S1 | Performers, jamming producers | L | 2 |
| 4 | **The Perform module.** Eight strips of launch, stop, send, level, mute, solo and arm, every one a Rack parameter (so MIDI-Map maps a controller in a minute), poly CV inputs for launch, stop, mute and level, and an EOC jack per strip. | P3 | Performers | L | 3 |
| 5 | **Keep every pass, comp by swiping.** Loop recording on an audio track always keeps its passes as takes. The audio editor shows the takes as rows; drag across a take to use it there. An **IN** button takes the microphone out of the mix after recording. | R1, R2, R4 | Producers who record | M | 2 |
| 6 | **One view across both editors.** Open lanes, folded groups, Notes or Mixer, drum lanes or keys and the lower lane follow you between the panel editor and the full editor. Only size, zoom and scroll stay per editor. | L1 | Everyone who uses both | S–M | 1 |
| 7 | **Automate every mixer control.** Group strips, drum lane strips and source levels get lanes; **Mute** and **Record** switch lanes, also for another track; a **Tempo** lane on the Master row. | M4, S6 | Producers; loopers who script takes | M–L | 4 |
| 8 | **An audio editor you can work in.** The clip's settings in its header, **Q** pins hits to the grid with a strength (after showing how far each hit drifts), a Slice tool, fades, and preview while stopped. | A2–A5 | Producers | M | 2 |
| 9 | **Safe on stage.** A perform view with bars-left counts and record buttons in slots, **Lock editing**, **Stop now**, LAUNCH that walks to the next scene, sections with **Next song** for a set, and **Perform keys** that work anywhere in the rack. | S2–S5, P2 | Performers | M | 3 |
| 10 | **Modules that say more.** An **EOC** jack beside AUTO on Track Out (and on Drum Out), each output module's screen shows mute, solo, arm and what plays, and a small **FX Loop** module gives the agreed effect chain (AUDIO_MIXER.md §12) a home, with a WET knob and a BYPASS button you can map. | P1, P4 | Rack patchers, performers | S (P1), L (P4) | 1, 4 |

What stays as agreed: the mixer shares the lower pane with the notes (DECISIONS §118); every source is mixed within
its track (§96); no effect track kind (§97); audio tracks never layer (§98); tracks that make no sound show no level
controls (§99); 0 dB = 10 V (§100). Five recorded choices are put back to the user as questions, each with a
recommendation, because the review found them in the way: Q2 (DECISIONS §17), Q3 (§77 with §119), Q4 (§119, audio
takes in slots), Q7 (the open half of AUDIO_MIXER.md §12) and Q16 (§128, warp markers lock the window).

## 2. How this review was done

Rack can't run in this container, so the review walked each journey through the manual (chapters 1–5, 11, 15–18),
the screenshots in `docs/manual/images/`, and the code where a picture didn't answer a question: the views in
`src/ui/views/`, the module widgets in `src/ui/`, the action table in `src/ui/interaction/Shortcuts.cpp`, the module
headers in `src/module/`, and the capture path in `src/module/TesseraCapture.cpp`. Each journey was taken twice: as a
producer at a desk with the full editor, and as a performer on a dark stage with the editor closed or in the panel,
hands on a controller.

The journeys (AUDIO_MIXER.md §1, widened): first sound; a beat from audio; recording through IN; arranging audio
next to notes; mixing; finishing; performing live. Then every module's panel, menu and setup helper, and what a
performer can reach without the editor.

Every finding below points at its evidence: a manual section, a screenshot, or `file:line`. "Today" means the
`feature/audio-mixer` branch at commit `c795aea`.

## 3. Findings: what is hard today

Each finding: what happens today, the evidence, who it hurts (**P** producer, **L** live performer, **R** rack
patcher), and how much (**high** blocks or loses work, **mid** slows or confuses, **low** a rough edge).

### 3.1 First sound: drop a loop, get it in time, hear it

- **F1. A loop dropped on the panel doesn't play.** A new Tessera plays Clips (`TesseraModule.cpp:68`). The drop
  puts the loop in the new track's first slot (`src/model/AudioEdits.cpp:535`) but doesn't launch it, so PLAY plays
  nothing until you open the editor and click ▶ in the slot. Note clips got exactly this fix after a Trello report
  ("a track that plays nothing takes its new clip", `src/ui/editing/ClipActions.cpp:157-161`); audio drops didn't.
  *P, L · high.*
- **F2. With the editor closed, a drop says nothing.** The "Imported…" and "Song set to 92 BPM · Keep 120 BPM"
  messages are toasts of the full editor only (`src/ui/TesseraWidget.cpp:338-343`, `EditorWindow.cpp:54`). With the
  editor closed you see the tempo change on the display and nothing else; only a failure comes up, as an
  operating-system dialog. *P, L · high.*
- **F3. The first patch the manual teaches blocks the first loop.** Chapter 1 patches the voice's VCA straight into
  the Audio module (`docs/manual/01-getting-started.md`, step 3; `first-patch.png`). Drop a loop afterwards and
  **Connect MAIN to Audio module** refuses: "The Audio module's inputs 1 and 2 are in use", and changes nothing
  (`src/ui/editing/AudioActions.cpp:466`). The musician must work out the IN round trip by hand, at the moment they
  only wanted to hear a loop. *P, R · high.*
- **F4. A loop dropped on the timeline plays once.** The timeline copy is one pass long (`AudioEdits.cpp:524-531`);
  the song then plays on into silence. You hear it cycle only after drawing a loop or dragging its right edge.
  *P · mid.*
- **F5. "Add audio track" has no key.** The spec gives it Ctrl/Cmd+Alt+T (AUDIO_MIXER.md §9.3); the action table
  has none (`src/ui/interaction/Shortcuts.cpp:116-123`). Only **+ Add track ▸ Audio track**. *P · low.*
- **F6. The manual still says 24 HP** for Tessera (chapter 1's module table, chapter 3's "folds back to 24 HP"); it
  is 26 HP since DECISIONS §103. *Everyone · low.*

### 3.2 A beat from audio: chop a break, warp a loose loop, drive rack drums

- **F7. Chopping lives only on the timeline.** Split at transients and Make triggers are timeline clip-menu items
  (DECISIONS §123: "Only from the timeline for now"). Each slice becomes its own clip, so a chopped break can be
  rearranged by dragging, but never replayed from a pattern or launched as a loop. *P · mid.*
- **F8. Make triggers puts every hit on one lane.** One "Hits" lane at pitch 36 on output 1 (§123): kick, snare and
  hats all fire the same rack drum. Doubling a break with rack drums means sorting the hits by hand. The new drum
  track has no Drum Out until you add one. *P · mid.*
- **F9. Warping is one hit at a time, or all at once, blind.** Double-click a hit to pin it, or **Pin every hit to
  the nearest 1/16** with no other grid and no strength (§128, manual 17 "Warp markers"). Nothing shows how far a
  hit is off the grid before you commit. While a clip has markers, its window, edges and tempo can't be dragged.
  *P · mid.*
- **F10. The audio editor shows none of the clip's settings and few tools.** Tempo, follow tempo, sound type, pitch,
  gain and reverse live in the clip menu and the inspector (`audio-clip-menu.png`, `audio-editor.png`). The Slice
  tool, fades and gain in the editor were left for later (§107). *P · mid.*
- **F11. You can't hear a launcher clip in the audio editor.** A click in its ruler says "This clip isn't on the
  timeline: launch it to hear it" (`src/ui/views/AudioEditor.cpp:410`). *P · mid.*

### 3.3 Record: a vocal or guitar through IN, several passes, comp, fix latency

- **F12. Loop recording loses passes in the default mode.** Recording over arranger clips defaults to **Merge**
  (manual 11). On an audio track each loop pass becomes a new clip and "the overlap rules settle them" (§119); a clip
  that covers another replaces it (manual 17 "Overlaps are crossfades"). After four passes, only the last is on the
  timeline. Takes collect only in **Stack** mode (§129), which a first-time recorder has no reason to pick. Undo
  still holds the passes, one by one. *P · high.*
- **F13. Comping is blind.** Split the clip, then right-click each part ▸ Take ▸ pick (manual 17 "Takes and
  comping"). You can't see or hear takes side by side, and the audio editor shows take 1 only (§129). *P · mid.*
- **F14. Getting ready to record takes five places, and nothing says what will happen.** + Add track (menu), the
  strip chip or track menu to add a Track Out, a cable into IN, the arm ring, and Record ▾ for the count-in and the
  metronome, which is Off by default (`record-menu.png`). No line says "Vox records from Track Out IN, with a one-bar
  count-in" before you press record, and no warning comes when IN has no cable. *P · mid.*
- **F15. After recording, the microphone stays in the mix.** Every source is always mixed (DECISIONS §96), so a live
  mic on IN plays under the takes. Turning it off means the strip's source list (it appears only with two or more
  sources) or the Track Out's right-click menu (§116). *P · mid.*
- **F16. Latency is typed in samples, in a hidden menu.** Right-click a strip's name ▸ Latency (samples)
  (`src/ui/views/MixerPane.cpp:895`). There is no way to measure what a rack effect delays, though the effect loop
  (OUT → effect → IN) is exactly a path Tessera could time. *P · low.*

### 3.4 Arrange and edit audio next to note tracks

- **F17. Rows under 40 px show no level, and the panel editor's rows are 24 px.** `kLevelLineMinHeight = 40`
  (`src/ui/views/TrackHeaders.cpp:55`, used at `:512`); the panel's rows start at 24 px
  (`src/ui/EditorState.hpp:126`). In the panel editor, no track row shows its level unless you make it taller. The
  spec's 3 px bar for short rows (AUDIO_MIXER.md §7.5) wasn't built. *P, L · mid.*
- Audio clips otherwise arrange well: the same gestures as note clips, crossfades by overlap, gain on the title
  bar, fades on the corners (`audio-timeline.png`, `audio-crossfade.png`). The review found nothing else that
  needs changing there.

### 3.5 Mix: levels, pan, an effect return, groups, automation, meters, the master

- **F18. The mixer can't scroll.** Strips are laid out left to right and the Master is pinned to the right edge
  (`MixerPane.cpp:585-652`, `:649`). Past about twelve strips in the full editor, or six in an 80 HP panel editor,
  strips slide under the Master and can't be reached. *P, L · high.*
- **F19. The mixer is unusable when short and empty when tall** (Trello). Under 190 px a strip drops its source
  chip, sends and pan (`MixerPane.cpp:534`); faders shrink to 20 px (`:575`); under about 42 px only the header is
  drawn (`:1519`). The panel editor's lower pane is 70–200 px, so there the mixer is a row of slivers. In the full
  editor it is the opposite: one strip, a Master and a wide empty field (`mixer.png`). *P, L · high.*
- **F20. The mixer has no keys.** The spec's Up/Down for level and Left/Right between strips (AUDIO_MIXER.md §7.3)
  were not built; with the mixer showing, only keys that work everywhere act (§118). The key scopes are Everywhere,
  TopPane and DetailPane (`src/ui/interaction/Shortcuts.hpp:123`). *P · mid.*
- **F21. Only some mixer controls can be automated** (Trello). Lanes can drive a track's Volume, Pan and Sends and
  the Master's Volume (`src/model/Automation.hpp:51`). Group strips (§127 "No group automation lanes yet"), drum
  lane strips (§112 "No mute, solo or automation per lane yet"), source levels (AUDIO_MIXER.md §7.8 "Not automatable
  for now") and mute can't. *P · mid.*
- **F22. The agreed effect chain has no home.** The user agreed on 3 Nov to an ordered chain of rack effect loops
  inside a track (AUDIO_MIXER.md §12, last section). It isn't built and has no DECISIONS entry. Today a second effect
  loop is one more source, so two effects on one track run side by side, not one after the other. *P · mid.*
- **F23. Drum lane strips have no mute.** (§112.) Muting the snare's IN means pulling its cable. *P, L · low.*

### 3.6 Finish: bounce, stems, export

- **F24. Export cuts the tail.** With Timeline it ends at the bar after the last clip (§121): a reverb or delay tail
  is cut off. Bounce has **Until silent**; export doesn't. *P · mid.*
- **F25. No loudness to finish against.** The Master shows peak and a clip light. The spec's short-term loudness on
  the Master (AUDIO_MIXER.md §7.2) wasn't built. *P · mid.*
- **F26. Stems skip the group strips.** "Stems in an export are each track on its own, before the group strip"
  (manual 18 "Group strips"): stems don't add up to the master when a group fader isn't at 0 dB, and a drum bus
  can't be one stem. *P · low.*

### 3.7 Perform live

- **F27. Audio can't live-loop in slots.** A take recorded while the clips play lands in the first empty slot as a
  clip that doesn't loop, isn't launched and isn't cut to the bar (`src/module/TesseraCapture.cpp:126`, `:723`,
  `:727-733`). There is no record button on a slot, no fixed length, no overdub. Notes have all of these
  (`src/engine/Recorder.hpp:33-38`). *L · high.*
- **F28. A MIDI controller reaches only the transport and scenes.** Rack's MIDI-Map maps module parameters. Tessera
  has twelve: PLAY, RESET, LOOP, TAP, TEMPO, PREV, NEXT, LAUNCH, STOP ALL, PLAYS, REC, FILL
  (`src/module/TesseraModule.hpp:64-78`). Track Out, Drum Out and Auto Out have none (`TrackOutModule.hpp:13`,
  `DrumOutModule.hpp:17`, `AutoOutModule.hpp:13`). No track's level, mute, solo, arm, send or clip can be mapped.
  *L · high.*
- **F29. CV reaches scenes, not tracks.** SCENE and LAUNCH pick and launch a scene; RUN, RESET, FILL and CLOCK drive
  the transport (manual 02 "Inputs"). Nothing launches or stops one track's clip, mutes a track or rides a level.
  *L, R · high.*
- **F30. No set of songs** (Trello: "CV selector for source and track on each output module… or maybe a project
  playlist"). One song per Tessera; output modules bind to one Tessera's tracks by ID (manual 02 "How output
  modules find Tessera"). Several songs means several Tesseras and every output module re-bound by hand between
  songs. *L · mid.*
- **F31. Keys reach Tessera only under the mouse.** With the editor closed there is no key for play or launch;
  in the panel editor "keys only reach Tessera while the mouse is over the panel" (manual 16 "A key does nothing").
  *L · mid.*
- **F32. Nothing guards a running set from an edit.** Delete with a track header clicked deletes the track (manual
  04), and its Track Out goes quiet ("Track deleted"); a stray drag moves a clip. Undo brings it back, after the
  silence. *L · mid.*
- **F33. The panel's scene buttons are slow and STOP ALL can't stop now.** Two presses per scene (NEXT, then LAUNCH);
  STOP ALL waits for the launch line; the only immediate stop is stopping the transport (manual 02 "Scene buttons").
  *L · mid.*
- **F34. The perform view is small and says little.** The F2 key widens the clips, but slots stay small, a queued launch
  shows no "bars left", armed tracks show no record buttons, and the panel editor fits two scenes
  (`EditorState.hpp:129`, `clips-playing.png`). *L · mid.*
- **F35. Output modules show only their binding** (Trello: "EOC on the track device"). The screen shows the Tessera,
  the number and the name (`src/ui/TrackOutDisplay.cpp`): not mute, solo, arm, record or what plays. EOC exists only
  on Tessera, for the song loop (manual 02 "Outputs"). *L, R · low.*

### 3.8 Across the two editors

- **F36. The panel editor and the full editor keep separate views** (Trello). Each keeps its own `ViewState`
  (`EditorState.hpp:202-203`): which tracks show lanes, folded groups (DECISIONS §17 made folding independent on
  purpose), Notes or Mixer, Show sends, drum lanes or keys, the lower lane, the device strip, the perform view. Open a
  lane in one and it is closed in the other. *P, L · mid.*
- **F37. Messages belong to the full editor.** Toasts from module events (a drop, the first-loop tempo, a missing
  file) reach the editor that is open, or nobody (F2). *Everyone · mid.*

## 4. Proposals

Each proposal says what it fixes, the design, its gestures and keys (checked in §5), states and edge cases, what it
changes in today's behaviour, where it lands in the code, its effort, and the mockup that shows it. Mockup names are
the section titles of `docs/design/mockups/audio-ux/index.html`.

Rules every proposal keeps: every edit is a Command with one undo step per gesture; widgets keep IDs; nothing new on
the audio thread allocates, locks or touches the registry; no gate is left high; output modules never rebind on
their own; colours and sizes go in `Theme.hpp`; keys go in the one action table; no song that makes no sound costs
more than today (AUDIO_MIXER.md §8.8).

### 4.1 Editor layout and the two editors

#### L1. One view across both editors

**Fixes** F36 (Trello: "State of the view between the panel view and the large editor should be consistent").

**Design.** Split today's per-editor `ViewState` in two:

| Shared by both editors: what you look at | Kept per editor: how big it is |
|---|---|
| Notes or Mixer in the lower pane; Show sends | Zoom and scroll of every view |
| Which tracks show their lanes; folded groups | Track, lane and row heights |
| Drum lanes or keys; the lower lane (velocity, a clip lane) | The splitter, header width, launcher width |
| The perform view (F2); the mixer's layout choice (M1); the Takes rows (R2) | Inspector open; the device strip; the maximised pane |

The selection, the selected track and clip, the tool and follow mode are already shared (DECISIONS §15). When an
editor appears (the full editor opens, or the panel is widened), it scrolls so the selected track and the playhead
are in view, keeping its own zoom.

**States and edge cases.** The device strip and the inspector stay per editor: they take room the panel doesn't have.
A group folded in one editor is folded in the other. View state stays out of undo and out of the song, as today.

**Changes today.** Opening lanes, folding a group, switching to the mixer or to drum keys in one editor now shows in
the other. This overturns DECISIONS §17's "the panel and overlay fold independently": question **Q2**.

**Touches.** `src/ui/EditorState.hpp` (a shared view block beside `overlay` and `panel`), its `toJson`/`fromJson`
(old patches: take the full editor's values), the views that read these fields through `ctx.view()`.
**Effort** S–M. **Mockup** "One view, two editors".

#### L2. A level bar in every track row

**Fixes** F17.

**Design.** Rows of 40 px and more keep today's level line. Shorter rows get the spec's 3 px bar (AUDIO_MIXER.md
§7.5): the fader and the meter in one, along the row's bottom, just above the 4 px resize edge, with a 6 px hit
area. The first 4 px of a drag decide: sideways sets the level, up or down resizes the row. Double-click in the
bar's own zone resets to 0 dB; double-click on the edge still toggles short and tall. The panel editor's 24 px rows
show it, so the panel can be mixed from the song pane.

**Changes today.** Short rows of tracks that make sound get a thin live bar. **Touches** `src/ui/views/TrackHeaders.cpp`,
`Theme.hpp` (bar height and hit size). **Effort** S. **Mockup** "The editor inside the panel".

#### L3. The channel in the inspector

**Fixes** the round trip between notes and mixer (the F4 key, back and forth) while you edit one track.

**Design.** For a track that makes sound, the inspector's TRACK section gains a CHANNEL block: a level bar (fader and
meter, the row widget), pan, **M S ● IN**, the sources (inline when there are two or more), the FX loops in order
(P4), the sends (exist today), Latency in ms with **Measure** (R5) and the output ("→ Master" or the group). Same
gestures and undo as the mixer.

**Touches** `src/ui/views/Inspector.cpp`, the shared mixer controls. **Effort** M. **Mockup** "The full editor".

#### L4. Messages wherever you are looking

**Fixes** F2, F37.

**Design.** Module events (a drop, the first loop setting the tempo, a missing file, a take stopped early, a new
source) go to one module-level message line. Every editor that is on screen shows it as today's toast. When neither
editor is on screen, Tessera's display shows it on its bottom line for four seconds, in amber for warnings:
"Loop: song set to 92 BPM", "MAIN not patched", "Recording stopped: disk full". A message's action (Keep 120 BPM,
Find…) moves into the module's right-click menu for 30 seconds, at the top.

**States.** The newest message replaces the older one. Bounce and export progress stay toasts in the editors, and the
display shows "Bouncing Bass · bar 3 of 8" when no editor is open.

**Touches** `src/module/TesseraModule.*` (a message queue), `src/ui/EditorHost.cpp` (`showNotices`),
`src/ui/TesseraDisplay.cpp`, `src/ui/TesseraWidget.cpp` (drop and menu). **Effort** S. **Mockup** "The rack" and "The
first minute".

### 4.2 The mixer

#### M1. A mixer that fits any height

**Fixes** F18, F19 (Trello: "Mixer when the pane is very small looks very bad and unusable… an alternative view if
too short"). This is the overhaul DECISIONS §118 deferred; it keeps §118's choice (the mixer shares the lower pane,
the F4 key switches).

**Design.** The mixer picks one of four layouts from the height its strips get. **View ▾** in the mixer header can pin
one (Auto by default); the choice is per editor.

| Layout | Strip area height | What a track shows |
|---|---|---|
| **Strips** | 280 px and more | Today's strip, completed (M2): name, source, FX loops, sends, pan, **M S ● IN**, fader and meter, dB, output. 64 px wide. |
| **Compact** | 150–279 px | 44 px strips (the spec's narrow width): colour bar, name, pan as a thin arc, **M S**, fader and meter, dB. Sends become one dot each in the target's colour (brighter = louder); click a dot for its knob. FX loops become a count, "FX 2". |
| **Tiles** | 36–149 px | One tile per track, 132 × 28 px, flowing left to right in as many rows as fit (one row at the panel editor's smallest pane): colour and number, name, a small pan knob, **M**, **S** on top; the level bar (the track rows' widget) and dB below. |
| **Meters** | under 36 px | One live meter per track in its colour, the Master's peak and "MAIN not patched" (today's header line). Click a meter to select its track; drag the splitter up for more. |

- **Strips scroll sideways.** Shift+wheel anywhere in the mixer, the plain wheel over names and empty space (over a
  fader or knob it still sets the value), two-finger sideways, or the 4 px scroll bar under the strips. The Master
  stays pinned at the right with a soft shadow where strips pass under it. Selecting a track anywhere scrolls its
  strip into view.
- In the panel editor (lower pane 150–200 px) the default is **Compact**; maximised (about 330 px) it is **Strips**.
  In the full editor it is **Strips**.
- **Strips keep at least 140 px of fader travel.** When the FX rows (M2) and the send knobs would leave less, they fold
  first, into an "FX 2" count and a dot per send as in Compact; the strip stays a Strip.
- Show sends works in every layout: tiles show "send −12.0" and a hollow bar in the receiver's colour. Drum lane and
  group strips follow their track's layout (narrow strips, small tiles).

**Changes today.** Short panes keep pan, M and S and a usable fader; tall panes are unchanged except for scrolling.

**Touches** `src/ui/views/MixerPane.cpp` (one layout function per layout), `Theme.hpp` (strip and tile sizes, the
height thresholds), `ViewState` (scroll position, layout choice). **Effort** M. **Mockup** "The mixer at four
heights".

#### M2. The strip, completed

**Fixes** F22 (no place for the chain), the strip's missing arm (the spec's M · S · ●, AUDIO_MIXER.md §7.2, isn't
drawn today: `mixer.png`), and F15.

**Design** (Strips layout, top to bottom): colour bar, number and name, with a hairline under the bar that fills as
the playing clip goes round · **← 3** (unchanged) · the source chip (unchanged) · **FX loops** in order, one row each
("1 Delay", "2 Drive") with a bypass dot, then a dim **+** (P4) · sends (unchanged) · pan · **M S ● IN** · fader and
meter (with the OUT meter on receivers, unchanged) · dB · "→ Master". The Master strip shows the finish meter (M5);
the audio memory moves to its tooltip.

**Gestures.** FX rows: click the dot to bypass that loop (its module's BYPASS); drag a row up or down to reorder the
chain (one Rack undo step); right-click for Show in the rack, Move up, Move down, Take out of the chain. **IN** is R4.

**Touches** `MixerPane.cpp`, `src/ui/editing/Sources.cpp`. **Effort** S–M. **Mockup** "The full editor".

#### M3. Keys in the mixer

**Fixes** F20.

**Design.** A new key scope, **Mixer**, active while the mouse is over the mixer (or it has the focus ring). A scoped
binding wins over an Everywhere one with the same chord; `findAction` checks the pane's scope first. Bindings:
Up / Down: level of the selected strips ±0.5 dB (Shift: ±0.1 dB). Left / Right: select the strip before or after.
Alt+Left / Alt+Right: pan ±5% (Shift: ±1%). **0**: selected strips to 0 dB. M and Alt+S work as everywhere. Delete
still does nothing over the mixer (spec §7.3). Each key press is one undo step; held keys repeat into one step until
released.

**Touches** `src/ui/interaction/Shortcuts.*` (scope, table, `findAction` order), `MixerPane.cpp`, the help overlay
(generated). **Effort** S. **Mockup** "Keys".

#### M4. Automate every mixer control

**Fixes** F21, F23 (Trello: "Automation for all mixer elements (even children level, panning etc, in drums)").

**Design.** **Right-click ▸ Automate in the timeline** on every mixer control, with the lane opening where you would
look for it:

| Control | Lane | Where it opens |
|---|---|---|
| Group fader, pan, M | Volume, Pan, Mute of the group | Under the group's header row |
| Drum lane strip fader, pan, new **M** | "Kick level", "Kick pan", "Kick mute", in the lane's colour | Under the drum track |
| A source's level (Clips, Mix, Track Out 2) | "Track Out 2 level" | Under its track |
| A track's M | Mute (a switch lane, S6) | Under the track |
| FX loop WET and BYPASS (P4) | Through the existing knob lanes ("Automate a knob: touch it in the rack") | Under the track |

Drum lane strips also get the **M** they lack (F23). Lanes behave like today's Volume lanes: a ring on the control,
the hand wins while it holds, a muted lane gives the control back. Groups have no arm, so their lanes are drawn,
never recorded (like the Master's, DECISIONS §115); drum lane lanes record while the drum track is armed.

**Engine.** The player already reads lanes per 32-sample block for tracks and the Master; groups are player strips,
so they read the same way. Drum lane gains are applied in the Drum Out (§112): the player publishes each drum track's
eight lane gains on the output bus once per block and the Drum Out ramps toward them, as it does today with the UI's
values. A source's lane targets the source by its module's ID, saved with the patch.

**Touches** `src/model/Automation.hpp` (`LaneDrive` gains DrumLaneVolume, DrumLanePan, SourceLevel, Mute; a group
lane owner), `AutomationEdits`, `AudioPlayer`, `DrumOutModule`, `ArrangerLanes.cpp`, `MixerPane.cpp`.
**Effort** M (groups, drum lanes, mute), plus M for source levels. **Mockup** "Automation".

#### M5. A finish meter on the Master

**Fixes** F25.

**Design.** Under the Master's dB readout: short-term loudness, "−14.2 LUFS"; a click cycles **Short-term (3 s)**,
**Integrated** (since play started; a click on the label resets it) and **Peak**. The same three in the Master row's
tooltip, and in the export result (E1).

**Engine.** K-weighting on the master only (two biquads per channel) and a mean square per 100 ms block, handed to
the UI through a lock-free ring; the UI does the gating and integration. A song with no sound skips it with the
rest of the mixer.

**Touches** `AudioPlayer`, `MixerPane.cpp`, `TrackHeaders.cpp` (Master row tooltip). **Effort** M. **Mockup** "The mixer at four
heights".

### 4.3 Audio editing

#### A1. A first loop that plays

**Fixes** F1, F4, F5.

**Design.**
- **Plays = Clips.** A dropped loop that lands on a track that plays nothing is launched the way a new note clip is
  (`ClipActions.cpp:157-161`): while stopped it is cued, so PLAY starts it. While the transport runs it is not
  launched (a drop mid-set must never start a sound by surprise); the message says "Loop is ready in its first slot:
  click ▶ to launch". One-shots are never launched.
- **Plays = Timeline, first loop in an empty song.** In the same undo step as the drop and the tempo change, the song
  loop is set around the clip and turned on, so it cycles at once: "Looping 2 bars at 92 BPM · Keep 120 BPM". Later
  drops don't touch the loop. Question **Q15**.
- **Ctrl/Cmd+Alt+T** adds an audio track after the selected one (AUDIO_MIXER.md §9.3).

**Touches** `TesseraModule::importAudioFiles`, `src/model/AudioEdits.cpp` (loop region in the drop's command),
`Shortcuts.cpp`, manual 15 and 17. **Effort** S. **Mockup** "The first minute".

#### A2. The audio editor's header holds the clip

**Fixes** F10.

**Design.** The header the audio editor shares with the piano roll gets, left to right: the clip chip (today) · the
tempo badge **112 ▾** (the clip badge's menu) · **Stretch ▾** (Follow tempo) · **Drums ▾** (Sound type) · **Pitch 0 st**
(Stretch clips) · **Gain −3.0 dB** · **◀** (reverse, lit when on) · **Warp ▾** (A3) · **Takes 3** (R2) · the tool
switch **Select | Slice** · the speaker · maximise. Pitch and Gain drag like the inspector's values and take a typed
value on double-click. In a narrow pane (the panel editor) the chips fold into one **Clip ▾** menu with the same items.
Tooltips say "all 3 copies" when the clip has several uses.

**Touches** `src/ui/views/AudioEditor.cpp`, `DetailPane.cpp`, the clip actions it already calls. **Effort** S–M.
**Mockup** "The audio editor".

#### A3. Quantize audio with Q

**Fixes** F9.

**Design.**
- **Drift ticks.** Every hit Tessera found shows a short tick from where it is to its nearest grid line, like the
  swing ticks of DECISIONS §92: dim under 5 ms off, amber over 15 ms. You see the loop's drift before changing it.
- **Q** (the Quantize key, the same verb as for notes) pins every hit of the clip to the grid at the set strength: a
  hit 18 ms late at 75% moves 13.5 ms. **Shift+Q** opens the settings, also in **Warp ▾**: Grid (1/8, 1/16, 1/16
  triplet, 1/32), Strength (50–100%), and **Hits louder than** (a threshold; the hits that count light up as you drag
  it). Q with audio clips selected on the timeline does the same per clip.
- With markers, the window and its edges can be dragged again (the markers move with the window); dragging a beat
  line to set the tempo stays off, and its tooltip says why.

**Changes today.** "Pin every hit to the nearest 1/16" becomes Q with a grid and a strength. DECISIONS §128 turned
window and edge drags off with markers; this turns them back on: question **Q16**.

**Touches** `src/model/AudioEdits.cpp` (`quantizeAudio` with grid, strength, threshold), `AudioEditor.cpp`,
`Shortcuts.cpp` (Quantize in the audio editor). **Effort** M. **Mockup** "The audio editor".

#### A4. Slice, fades and gain in the audio editor

**Fixes** F10 (the parts of AUDIO_MIXER.md §6.4 not built).

**Design.**
- **Slice** (C): a click splits the selected timeline copy at that point of the file; **Shift+click** offers Every hit,
  Every 1/16, Every 1/8, Every beat (as the piano roll's Slice offers equal parts, DECISIONS §16). A launcher-only clip
  can't be split into slots: the cursor says "Slice works on timeline copies".
- **Fades**: handles on the window's top corners edit the selected timeline copy's fades, with curve dots, as on the
  timeline. Launcher clips have no fades (spec §4.3), so no handles.
- **Gain**: drag the window's top strip up or down, as the title bar on the timeline; the waveform scales live.

**Touches** `AudioEditor.cpp`, the timeline's fade and split commands. **Effort** M. **Mockup** "The audio editor".

#### A5. Hear a clip in the editor while stopped

**Fixes** F11.

**Design.** While the transport is stopped, a click in the ruler of a launcher-only clip, or **Alt+click** in any
clip's ruler, plays that clip alone from there, through its track's fader to MAIN, looping if it loops; a second
click, Space or Esc stops it. While the transport plays, preview is off and the tooltip says so: a preview must never
sound in a set. (A cue output for headphones is a later idea, not proposed here.)

**Engine.** A preview voice in the audio player fed through the lock-free queue that note audition already uses
(DECISIONS §16); it reads the clip's prepared or rendered buffer. **Touches** `AudioPlayer`, `AudioEditor.cpp`.
**Effort** M. **Mockup** "The audio editor".

#### A6. Make triggers by sound, from slots too

**Fixes** F7, F8.

**Design.** **Make triggers from audio** (clip menu, on the timeline and now in slots) asks one thing: **Kick, snare
and hats (3 lanes)** (default) or **One lane (Hits)**. With three lanes, each hit goes to Kick (36, output 1), Snare
(38, output 2) or Hats (42, output 3) by where its energy sits in the 20 ms after it (low, middle with noise, high),
louder hits louder, as today. If the new drum track has no Drum Out, the same Rack undo step adds one right of
Tessera (as **Add a Track Out for this track** does). From a slot, the triggers clip goes in the same scene's slot of
the new drum track, so launching the scene plays both.

**Touches** `src/model/AudioAnalysis.cpp` (band energy at each transient), the make-triggers command, `ClipActions.cpp`,
`AudioActions.cpp`. **Effort** M. **Mockup** "The audio editor" (menu).

#### A7. Slice to a drum track (a question)

**Fixes** F7 for the classic way of making a beat from a break: slices replayed from a pattern.

**Design.** **Slice to a drum track** (clip menu) makes a drum track whose lanes each play one slice of the clip (at
its hits, or in equal parts, up to 16), with a clip that plays them in the original order. Re-sequence the slices
in the drum editor. Each lane plays its slice through Tessera's own audio (the drum track's Clips source, mixed and
automatable like any other), and still sends its trigger to the Drum Out. A new slice cuts the one before on the
same track by default (lane menu: "Let slices ring").

This would be the one place where notes play audio inside Tessera, which DESIGN.md never planned: question **Q12**.
**Effort** L.

### 4.4 Recording and comping

#### R1. Every pass is a take on an audio track

**Fixes** F12.

**Design.** Loop recording on an audio track keeps every pass as a take of one clip, whatever **Recording over arranger
clips** says (Merge, Stack and Replace keep their meaning for notes). The first pass makes a clip over the loop; each
later pass becomes take 2, 3…; the clip plays the newest. When recording starts inside the song loop, the first
pass's clip still spans the whole loop (silent before the punch-in), so later passes line up as takes. Record ▾ gets
one line for audio: **Audio passes: Keep as takes** (default) / **Replace**.

**Changes today.** In Merge (the default) and Replace, audio passes used to replace each other (DECISIONS §119); in
Stack they already became takes (§129). Question **Q3**.

**Touches** `TesseraCapture.cpp` (`importTakes`), `RecordSettings`, the record menu, manual 11 and 17. **Effort** S.
**Mockup** "Recording and comping".

#### R2. Takes as rows, comp by swiping

**Fixes** F13.

**Design.** **Takes 3** in the audio editor's header (or **Alt+T**) shows the clip's takes as rows under the waveform,
newest first, each in file time and lined up with the clip's window. The top row, **Comp**, shows what plays: each
part tinted by the take it uses, with the take number on it.
- **Drag across a take row**: that stretch of the comp now plays that take. Underneath it is today's model, split plus
  a take per part (DECISIONS §129): the selected timeline copy is split at the drag's ends (snapped; Alt free) and the
  parts get their take; neighbouring parts that end up on the same take join again. One undo step per drag.
- **Hear** on a take row's name plays that take instead of the comp while it is lit, in time with the loop, so you
  can compare passes; one at a time; Esc turns it off. It is a hold: nothing changes in the song.
- **Up / Down** with a part selected: use the take above or below for that part.
- Each seam gets the 2 ms safety fades; drag the seam's top handle to crossfade it, as any overlap (spec §6.3).
- A take that stopped early is hatched where it has no audio; a drag there says "Take 2 has no audio here".

**Touches** `AudioEditor.cpp` (rows, swipe gesture), `AudioEdits` (split and pick in one command),
`AudioPlayer` (the Hear hold), `Shortcuts.cpp`. **Effort** M. **Mockup** "Recording and comping".

#### R3. Ready to record

**Fixes** F14.

**Design.**
- **The record plan.** While a track with audio is armed and the transport is stopped, a line beside the record button
  says what will happen: "● Vox · from Track Out IN · count-in 1 bar · click". A click on it opens Record ▾. Problems
  show in amber in the same place: "Vox has nothing on IN", "MAIN not patched: you won't hear the click".
- **The armed track** shows its IN level in red in its row and strip, so you set the input before you record.
- **The first time an audio track is armed** in a song with the metronome Off, the metronome switches to **Count-in
  only** and the message says so ("Metronome: count-in only · Change"). Question **Q10**.
- **+ Add track ▸ Audio track with a Track Out**: the track and its module, bound and placed right of Tessera, in one
  undo step: the shortest way from nothing to a cable into IN.

**Touches** `TransportBar.cpp`, `TrackHeaders.cpp`, `MixerPane.cpp`, `TrackActions.cpp`, `AudioActions.cpp`.
**Effort** S–M. **Mockup** "Recording and comping".

#### R4. The IN switch

**Fixes** F15.

**Design.** On a track that has clips and an IN (an audio track with a Track Out IN patched), strips, tiles and the
inspector channel get an **IN** button beside M and S: lit while the track's IN sources are in use. A click turns
every IN source of the track off or on; Shift+click opens the sources list. It sets the same **Use** values as the
sources list and the Track Out menu (DECISIONS §96, §116), so nothing switches by itself: the musician turns the mic
off after recording and on before the next pass. Note tracks have no IN button (IN is their only source).

**Touches** `MixerPane.cpp`, `Sources.cpp`, `Inspector.cpp`. **Effort** S. **Mockup** "The full editor".

#### R5. Measure a track's effect loop

**Fixes** F16.

**Design.** Right-click a strip's name ▸ **Latency…** shows the track's latency in ms and in samples (type either) and
**Measure through OUT and IN**: while stopped, Tessera sends four clicks out of the track's OUT and times them coming
back on IN (as the recording offset measurement does, DECISIONS §122), then sets the latency to what the patched
effect adds. Offered only when OUT and IN are patched.

**Touches** `TesseraCapture.cpp` (measurement), `MixerPane.cpp`, `Inspector.cpp`. **Effort** S–M. **Mockup** "The full
editor" (inspector).

### 4.5 Modules and panels

#### P1. Track Out and Drum Out: EOC, and the track's state on the screen

**Fixes** F35 (Trello: "EOC on the track device, for every time a clip on that track loops").

**Design.**
- **EOC** (an output, appended to the port list so saved cables stay on their jacks). On Track Out, AUTO moves from
  the middle of the fourth note row to its left column (9 mm) and EOC takes the right (21.48 mm), on the same plate.
  On Drum Out the poly row is spaced for seven jacks: TRIGS, VEL, PITCH, TIMBRE, PRESS, AUTO, EOC.
- EOC sends a 1 ms 10 V trigger each time the track's clip comes round to its start: every loop of a launched clip,
  and on the timeline at each loop seam. The module menu adds **EOC also at each new clip** (off by default). It fires
  while the clip plays, muted or not, so a muted loop still keeps time. Never dim, for every track kind.
- **The screen** gains two small corners: top left "▶ 3/4" (playing, and the bar of its loop) or "■"; top right **M**
  (amber), **S**, **●** (red while recording, an outline while armed). A muted track's big number dims. The status
  strip at the top shows the track colour while it plays and a dim version when stopped.

**Touches** `src/module/TrackOutModule.*`, `DrumOutModule.*`, `src/ui/TrackOutWidget.cpp`, `DrumOutWidget.cpp`,
`TrackOutDisplay.cpp`, the engine's per-track frame (one more flag), manual 02. **Effort** S. **Mockup** "The rack".
Question **Q13**.

#### P2. The Tessera panel does more with what it has

**Fixes** F2, F33, part of F31. No new jacks, no new HP.

**Design.**
- The display shows messages when no editor is open (L4), the queued launch with its bars left ("LAUNCH IN 2"), the
  next section ("NEXT 2 GLASS", S4) and a **KEYS** pill while Perform keys are on (S5).
- **Module menu ▸ Scene buttons ▸ After LAUNCH, pick the next scene** (off by default): one button walks a set.
- **STOP ALL held one second** stops every clip now, not at the launch line; its light flashes while held. A short press
  is unchanged. A mapped MIDI button held does the same.
- **REC** with an armed audio track while the clips play drives the loop steps (S1): record into the picked scene's
  slot, close the loop, overdub, stop overdubbing.

**Touches** `src/module/TesseraModule.cpp` (button timing, auto-advance), `TesseraDisplay.cpp`, `TesseraWidget.cpp`.
**Effort** S. **Mockup** "The rack".

#### P3. A new module: Perform

**Fixes** F28, F29 (the brief: "what should be a mappable Rack parameter or a CV input but isn't").

**Design.** **Perform** (30 HP) gives eight tracks a channel strip made of Rack parameters, so Rack's MIDI-Map (or any
mapping module) maps a controller in a minute, and poly CV inputs for the patch. It finds its Tessera the way output
modules do (Source menu; it follows the Tessera clicked last).

- **Eight strips**, showing tracks 1–8; **BANK ◀ ▶** moves them to 9–16 and on. The module menu can instead pin each
  strip to a track by its ID. Per strip, top to bottom: a screen cell (colour, number, short name, the playing clip's
  progress, M S ● state); **▶** launches the picked scene's clip on that track (on an armed track's empty slot it
  records, S1); **■** stops it at the launch line (held one second: now); **SEND** sets the send to the module's send
  target (menu: "Sends go to: Reverb"; turning it up from −inf adds the send); **LEVEL** is the track's fader (0 dB at
  75% of the travel, as in the mixer, with its meter in the slider's lights); **M**, **S**, **●** with lights; an
  **EOC** output (P1's trigger, per strip).
- **The left column**: a screen (Tessera, bank, tracks), BANK ◀ ▶, **STOP ALL**, **NEXT SONG** (S4), **MASTER**
  (the Master fader), **UNDO** (the last loop layer or take, when Rack's last undo step is one), and four poly inputs:
  **LAUNCH** (a trigger per channel launches that strip's track), **STOP**, **MUTE** (a gate per channel; the menu
  picks "muted while high" or "each trigger toggles"), **LEVEL** (0–10 V scales the strip's level: 10 V as set, 0 V
  silent, for swells and ducking from an envelope).
- **Two ways, one value.** Sliders and buttons always show the song: editor drags, undo and automation move them (an
  automated fader moves its slider). A move on the module (mouse, MIDI-Map, a CV-mapping module) acts at once through
  the same hold the mixer's drags use, and becomes **one undo step when it rests for one second** (or on mouse-up).
  Question **Q5**.
- **Rack rules.** The parameter list is fixed (8 strips × 7 + 6 = 62); banks change which track a slider drives, so eight
  hardware faders reach 64 tracks without re-mapping. A deleted track's strip shows "deleted" and does nothing; a
  strip never jumps to another track by itself. Lights are Rack lights, lit in a dark room.

**Threading.** The module reads its parameters on the UI thread and makes Commands or holds. Its CV inputs are read
on the audio thread and written as `{sample, flags, gains}` into one return slot on Tessera's bus, read by the player
per 32-sample block and ramped, like a return (DECISIONS §108): no allocation, no locks. LAUNCH and STOP triggers
become launch events in Tessera's own `process()`, as the panel's LAUNCH input does.

**Touches** new `src/module/PerformModule.*`, `src/ui/PerformWidget.cpp`, `AudioBus.hpp` (a control slot),
`AudioPlayer` (level and mute multipliers), the registry (a new client kind), manual 02 and a new manual page.
**Effort** L. **Mockup** "The rack", "A live setup". Question **Q6** on its shape.

#### P4. A new module: FX Loop (the agreed effect chain)

**Fixes** F22. Settles the open part of AUDIO_MIXER.md §12 ("whether a slot is a Track Out in an effect-slot role or a
small module of its own") with a recommendation: question **Q7**.

**Design.** **FX Loop** (5 HP) is one place in a track's chain, in the order the user sets: sources mixed → FX 1 →
FX 2 → … → fader and pan → sends (the agreed shape).
- **Screen**: the track's colour and number, "FX 1 of 2", the track name. Arrows step the track, as on Track Out.
- **SEND L/R** (outputs, on a plate): the track's sound so far. **RETURN L/R** (inputs): the effect's output, with an
  IN light between them.
- **WET** (knob, default 100%: the effect replaces the sound, a true insert) and **BYPASS** (button with a light):
  both Rack parameters, so a performer can map a delay throw or a filter kill.
- **Order**: module menu ▸ Place in chain (1, 2, 3…), or drag the rows in the strip's FX list (M2). Each slot adds about
  two samples; Tessera adds them to the track's known latency, so delay compensation (§126) keeps the track in line.
- **States**: BYPASS fades WET to 0 over 5 ms and never routes RETURN to SEND; with nothing on RETURN the slot passes the
  sound through and the strip's row says "nothing on RETURN"; a removed module drops out with a 5 ms fade and undo
  brings it back; "Track deleted" and "No source" as on Track Out.
- Track Out's IN stays what it is today, a source mixed beside the others (§96): parallel effects keep working.

**Why a module of its own**: its panel says what it does (SEND, RETURN, WET, BYPASS), its controls can be mapped, and
it doesn't carry a Track Out's note jacks that would do nothing in a slot.

**Touches** new `FxLoopModule.*` and widget, `AudioBus.hpp` (one return slot per loop, as today), `MixPlan` (slots in
order), `AudioPlayer`, `MixerPane.cpp` (M2's rows). **Effort** L. **Mockup** "The rack", "The full editor".

#### P5. Put the mixer in front of the Audio module

**Fixes** F3.

**Design.** When **Connect MAIN to Audio module** finds inputs 1 and 2 taken, it offers instead **Put Tessera's mixer
in front of the Audio module**, one Rack undo step that:
1. adds a Track Out bound to the **Master**, right of Tessera;
2. moves the cables now in the Audio module's inputs 1 and 2 to that Track Out's IN L and IN R;
3. patches MAIN L and MAIN R into inputs 1 and 2.

What you heard before still plays, at the same level (the Master's IN is mixed at 0 dB, and 0 dB is 10 V on both
sides), now with Tessera's audio. The message says: "Your rack now plays through the Master's Track Out IN. Give each
voice its own track later: patch it into that track's Track Out IN." Inputs 3–8 are left alone. With only input 1
used, IN R follows IN L, so a left-only voice becomes centred: the message says so. Chapter 1 of the manual teaches
the round trip right after the first sound.

**Touches** `src/ui/editing/AudioActions.cpp`, `TesseraWidget.cpp` (menu), `MixerPane.cpp` (Master's note), manual
01, 17. **Effort** S–M. **Mockup** "The first minute".

### 4.6 Live performance

#### S1. Audio slots that loop

**Fixes** F27 (and the Trello wish to colour-code rec, play, overdub and erase clips, already possible with Colour,
DECISIONS §89).

**Design.** An armed track's empty slots show **●** where the stop square was, for audio and note tracks alike (notes
already record into slots; this gives them the button). On a playing slot of an armed track, a small ● appears too.

| Press | What happens | The slot shows |
|---|---|---|
| ● on an empty slot (or REC on the panel, or ▶ on Perform for that track) | Recording starts at the next launch line | Blinking red "● bar 9", then red with the live waveform and a bar count |
| The slot again | The loop closes at the next launch line (or by itself when **New launcher clips** is 1, 2, 4 or 8 bars). The take becomes the slot's clip, looping, and plays on from the same sample (as notes do through `LaunchEvent::at`). | "closes at bar 13", then the clip, playing |
| ● on the playing loop | Overdub from the next loop start: what comes in on IN is added to the loop. When the pass ends, the writer thread mixes it with the loop into the clip's next take, which plays from the next pass. | Red outline, "layer 2" |
| The loop again | Overdubbing stops after the pass | "layer 3" |
| Ctrl+Z, or **UNDO** on Perform | The last layer comes off: the clip goes back to the take before (the file stays until Remove unused audio) | "layer 2" |

- Each close and each layer is one undo step.
- Audio tracks still never layer clips (§98): a layer is mixed into one new take; nothing plays on top of anything.
- A track keeps playing its other clip while you record a new loop in another slot; the new loop takes over when it
  closes (one clip per track, as today).
- Overdub needs the song at the loop's tempo, as takes do; otherwise the ● is dim: "Set the tempo back to 92 to
  overdub".
- Stop while recording: the take closes at the stop, rounded up to the bar, and is kept, not playing. A full disk or a
  slow writer stops it and keeps what was recorded, as today.
- New loops take the track colour; Colour (DECISIONS §89) marks their role.

**Changes today.** Audio takes in Clips mode stop landing as one-shot clips in the first empty slot (DECISIONS §119).
Question **Q4**.

**Touches** `TesseraCapture.cpp` (slot takes, close at the launch line, layers), `CaptureWriter` (mix the previous take
with the new pass), `AudioStore`, `LauncherPanel.cpp` and `SlotDraw.cpp` (● button and states), `TesseraModule.cpp`
(REC), `Recorder` settings. **Effort** L. **Mockup** "Loops in slots".

#### S2. The perform view (the F2 key), made for a stage

**Fixes** F34.

**Design.** In the perform view (clips wide), in both editors:
- Rows are at least 40 px (the perform view keeps its own row height, 44 px by default), so every row shows its level
  line and slots are big targets.
- A slot's progress ring becomes a bar along its bottom; a queued launch shows **bars left** in its corner ("2") and
  pulses amber (a steady amber outline with reduced motion).
- Armed tracks show ● in empty slots (S1). A **stop column** after the last scene holds a ■ per track.
- Scene headers show their tempo ("92") and time signature when they set one; sections (S4) group them.
- In the panel editor the perform view drops the timeline strip and uses the full width: four scenes at 80 HP.

**Touches** `LauncherPanel.cpp`, `SlotDraw.cpp`, `TrackHeaders.cpp`, `ViewState`. **Effort** M. **Mockup** "A live
setup".

#### S3. Lock editing

**Fixes** F32.

**Design.** A padlock in both editors' transport, **Ctrl/Cmd+Shift+L**, and a module-menu item. While locked, what a
performance needs still works: transport, launching and stopping, scenes and sections, mute, solo, arm, the mixer,
sends, tempo, recording takes and loops, selecting and scrolling. What changes the song's content doesn't: notes,
clips, tracks, lanes, devices, drops, deletes, reorders. A refused edit shows a readout, "Locked for performance ·
Ctrl+Shift+L unlocks", and nothing happens. The song pane's frame turns amber while locked. The lock is saved with the
patch, so a set opens locked.

**Touches** `EditorContext` / `EditorHost::perform` (commands carry a kind; locked refuses the editing kinds), the
gestures' begin, `TransportBar.cpp`. **Effort** S–M. **Mockup** "A live setup". Question **Q14**.

#### S4. Sections: a set list inside one song

**Fixes** F30 (Trello: "…a project playlist feature / module… something that could be used for live performance").

**Design.**
- **Sections** group scenes under a song name: "1 Night Drive · 92", "2 Glass · 124 · 7/8". A section header spans its
  scenes in the clips panel; right-click a scene header ▸ Start a section here; double-click a section to rename.
- **Next song** (the perform view's button, Shift+] in the editor, NEXT SONG on Perform, Shift+Page Down with Perform
  keys) launches the next section's first scene at the launch line; the display says "NEXT 2 GLASS" until it starts.
- **Scene follow** (right-click a scene ▸ Then): **Stay** (default), **Launch the next scene after N bars**, **Launch
  scene… after N bars**. A song can walk its parts, or a whole set can play itself.
- **A scene can set the time signature** as it sets the tempo today: it applies from its launch line, and bars count
  from there. Clips keep their length in ticks.

**Why inside one song.** Output modules stay bound (no re-binding between songs), all audio sits in one patch, and the
panel, Perform and CV controls work unchanged. Several Tesseras with a CV-selected source (the request as written)
stays possible as an option: question **Q9**.

**Touches** `Song` (sections, scene time signature and follow), `Serialize`, `Engine` (follow actions, meter changes at
the launch line), `LauncherPanel.cpp`, `TesseraDisplay.cpp`, MIDI export (meter changes). **Effort** M (sections,
follow), M (time signature per scene). **Mockup** "A live setup".

#### S5. Perform keys anywhere in the rack

**Fixes** F31. Our reading of the Trello card "Global keyboard shortcuts": keys that reach Tessera from anywhere in
Rack, editor closed, mouse over another module. Other readings are in question **Q8**.

**Design.** Module menu ▸ **Perform keys** (off by default; one Tessera owns them; turning them on in a second Tessera
moves them). While on, and only when nothing under the mouse uses the key:

| Key | Does |
|---|---|
| Space | Play / stop |
| Enter | Launch the picked scene |
| Shift+Enter | Stop all clips (at the launch line) |
| Page Up / Page Down | Pick the scene before / after |
| Shift+Page Up / Shift+Page Down | Previous / next song (section) |

The display shows **KEYS**. Inside the editors, the editors' own keys still rule. Text fields, modules that use keys
and Rack's own shortcuts keep theirs: the key catcher sits at the bottom of Rack's scene, under the rack (the other
end from the existing Alt-click router, `src/ui/PanelEditor.cpp:104-137`), so it only sees keys nobody else took.
Before building, check this list against Rack's own bindings and its computer-keyboard MIDI driver in the hidden Rack.

**Touches** a scene-level key catcher in `src/ui/`, `TesseraModule` (owner flag, saved), the menu, manual 15.
**Effort** S–M. **Mockup** "Keys".

#### S6. Switch lanes, and a Tempo lane

**Fixes** the Trello cards "Automation lanes to control the other track's armed status… or really any other track's
internal settings" and, at lower priority, "Tempo automation".

**Design.**
- **Switch lanes** hold steps, on or off, drawn with Draw or Paint: **Mute** (mutes with the usual fade; gates drop) and
  **Record** (while on and the transport plays, the track records, punching in and out with the lane, as if armed
  with record pressed; each pass commits as one undo step, as today).
- **Another track**: a switch lane's menu has **Switches: This track / Track ▸**. Its header then names the target,
  "Record · Vox". One control track (or the Master row) can carry the plan for a whole set of hands-free takes.
- **Song lanes on the Master row**: **Tempo** (BPM over time, ramps allowed). The TEMPO knob follows it like a mapped
  knob; Stretch clips follow through the varispeed reader while it moves, as with any moving tempo (spec §5.2); an
  external CLOCK overrides it (the lane dims and says why). MIDI export writes tempo events; DAWproject a tempo lane.
- Arming stays the user's switch for recording by hand (DECISIONS §73); a Record lane is a timed punch, not an arm.

**Touches** `Automation.hpp` (`LaneDrive::Mute`, `Record`, `Tempo`; a target track), `Engine` (switch lanes per
block; tempo map), `Recorder` (lane punch), `ArrangerLanes.cpp` (step drawing), `MidiFile`, `DawProject`.
**Effort** M (switch lanes), L (Tempo lane). **Mockup** "Automation". Question **Q11**.

### 4.7 Finishing

#### E1. Export with its tail, groups as stems, and a result you can read

**Fixes** F24, F26, F25 (with M5).

**Design.** Export audio gains two choices and a clearer result:
- **Tail: Until silent** (default for the song, at most 8 bars, the bounce rule of DECISIONS §104) or **None** (default
  for the loop, so a loop exports exactly).
- **Stems: Every track** or **Groups as one stem** (each group strip's sum after its fader is one stem; tracks outside
  groups stay their own), so stems add up to the master.
- **The result**: "Exported Song.wav · 3:42 · −11.8 LUFS integrated · peak −0.4 dB · 9 stems (Lead muted, left out)".

**Touches** `TesseraCapture.cpp` (`exportStep`), the export menu, `AudioPlayer` (group stem taps). **Effort** S–M.
**Mockup** "The mixer at four heights" (Master meter).

## 5. Keys: every new binding, checked

Checked against `src/ui/interaction/Shortcuts.cpp` (the one action table), EDITOR_UX §7 and the manual's chapter 15.
Every binding goes in the table, so the help overlay and tooltips list it.

**One rule change.** `findAction` takes the first match in table order, and the arrows' Everywhere bindings come
first. The mixer and the audio editor need their own meaning for arrows, so: **a binding scoped to the pane under the
mouse wins over an Everywhere binding with the same chord.** Two scopes are added: **Mixer** (the lower pane shows the
mixer) and **Audio editor** (it shows the audio editor). Today no Everywhere chord collides with a TopPane or
DetailPane one, so nothing that works now changes.

| Action | Key | Scope | Check |
|---|---|---|---|
| Add audio track | Ctrl/Cmd+Alt+T | Everywhere | Free; AUDIO_MIXER.md §9.3 named it |
| Launch the picked scene | Enter | Everywhere | Free; text fields and the Tap popover keep their Enter |
| Stop all clips (at the launch line) | Shift+Enter | Everywhere | Free |
| Pick the scene before / after | [ / ] | Everywhere | Free; Ctrl+[ ] stay zoom history; keyboard play doesn't use them |
| Previous / next song (section) | Shift+[ / Shift+] | Everywhere | Free |
| Lock editing | Ctrl/Cmd+Shift+L | Everywhere | Free; L is loop, Ctrl+L loop to selection |
| Level of the selected strips ±0.5 dB / ±0.1 dB | Up / Down, Shift+Up / Down | Mixer | Wins over Transpose and Octave there |
| The strip before / after | Left / Right | Mixer | Wins over Nudge there |
| Pan ±5% / ±1% | Alt+Left / Right, Alt+Shift+Left / Right | Mixer | Wins over Select next note and Shorter / longer there |
| Selected strips to 0 dB | 0 | Mixer | Free |
| Pin hits to the grid / its settings | Q / Shift+Q | Audio editor | The Quantize actions, same keys as for notes |
| Takes rows on or off | Alt+T | Audio editor | Free |
| Use the take above / below for the selected part | Up / Down | Audio editor, Takes shown | Wins over Transpose there (no notes in the audio editor) |
| Slice tool | C | Audio editor | Already the Slice key |
| Stop every clip now | Shift+click ■ Stop all; STOP ALL held 1 s on the panel | Mouse | No key, on purpose: hard to hit by accident |

**Perform keys** (S5), outside the editors only, opt-in: Space, Enter, Shift+Enter, Page Up / Page Down, Shift+Page Up
/ Shift+Page Down. Inside an editor, the editor's keys rule. Rack's arrow keys (they scroll the rack), its Ctrl
shortcuts and its computer-keyboard MIDI driver's letter and number rows are not used. To check in the hidden Rack
before building.

## 6. Phased plan

Each phase ships as AUDIO_MIXER.md §10 asks: it builds, its tests pass (each command's apply and revert, the
no-click checks, the §8.8 budgets), it is tried in the hidden Rack, and it brings its manual pages, screenshots, QA
steps, DECISIONS entries, shortcuts, tooltips and help overlay entries.

**Phase 1: the first minute, and a mixer that works anywhere.** Mostly S, no new module.
A1 a first loop that plays · L4 messages wherever you look · P5 the mixer in front of the Audio module · M1 a mixer
that fits any height · L2 a level bar in every row · M3 keys in the mixer · L1 one view across both editors · R1 every
pass is a take · R4 the IN switch · P1 EOC and state on output modules · P2 without REC (display, auto-advance, Stop
now).
*Why first:* it fixes the losses (F1, F12), the first-minute walls (F2, F3) and four of the nine open Trello items
(§8: the short mixer, the overhaul, one view state, EOC), for little code and no new module.

**Phase 2: record, comp, loop, edit.** M–L, all on the capture path and the audio editor.
R2 takes as rows · R3 ready to record · S1 audio slots that loop (and P2's REC) · A2 the editor's header · A3 quantize
audio with Q · A4 slice, fades, gain · A5 preview while stopped · R5 measure an effect loop · M2 the strip completed
(arm, IN, the FX rows' place) · E1 export with its tail · M5 the finish meter.

**Phase 3: the stage.** M–L, one new module.
P3 Perform · S2 the perform view · S3 Lock editing · S4 sections, Next song, scene follow, a scene's time signature ·
S5 Perform keys.

**Phase 4: depth.** M–L, one new module.
M4 automate every mixer control · S6 switch lanes and the Tempo lane · P4 FX Loop (the agreed chain) · A6 make
triggers by sound · A7 slice to a drum track (if Q12 says yes) · L3 the channel in the inspector.
P4 can move up to Phase 2 if the user wants the agreed chain sooner; series effects already work today when patched
in series between OUT and IN.

## 7. Open questions

Answer by number. Each has a recommendation (**rec.**). Q2, Q3, Q4, Q7 and Q16 touch choices already recorded
(DECISIONS §17, §77 with §119, §119, the open half of AUDIO_MIXER.md §12, §128); the review thinks they should
change, but only the user decides.

1. **The mixer when the pane is short** (M1). (a) Compact strips, then tiles, then meters, as designed. (b) Under
   150 px, hide the mixer and use the track rows' level bars (L2) instead. (c) Keep full strips and scroll the pane up
   and down. **Rec. (a):** short panes keep pan, mute, solo and a fader you can use.
2. **One view across both editors** (L1), against DECISIONS §17 ("the panel and overlay fold independently"). (a) Share
   what you look at, keep sizes per editor. (b) Share everything, zoom and scroll too. (c) Keep them separate and add
   **Match the other editor** to View ▾. **Rec. (a):** it answers the Trello card and the panel keeps its own small
   sizes.
3. **Loop passes on audio tracks** (R1), against DECISIONS §77 and §119. (a) Always takes on audio tracks; Merge,
   Stack and Replace stay for notes; Record ▾ gets "Audio passes: Keep as takes / Replace". (b) One setting for both,
   with Stack the new default. (c) As today. **Rec. (a):** losing passes silently is the worst thing a recorder can
   meet.
4. **Audio loops in slots** (S1), against DECISIONS §119 (a take recorded while the clips play lands, not looping and
   not launched, in the first empty slot). (a) Record, close and loop like notes, and overdub: each overdub pass is
   mixed with the loop into a new take; undo peels layers. (b) Record, close and loop like notes, no overdub: each
   pass is a take, comped later. (c) As today. **Rec. (a):** it is what live looping is, and it keeps "audio tracks
   never layer".
5. **Moves from hardware, and undo** (P3). (a) A move on Perform (or a mapped control) is one undo step once it rests
   a second. (b) Not undoable, like Rack's own mapped knobs. (c) Only mouse drags on the module are undoable.
   **Rec. (a):** it keeps "every edit is undoable" without flooding the undo list.
6. **The Perform module's shape** (P3). (a) One 30 HP module, eight strips, banks. (b) A 4 HP strip per track, placed
   by each voice. (c) Both, (b) later. **Rec. (a):** eight sliders map to eight hardware faders once, and banks reach
   64 tracks.
7. **The effect chain's slot** (P4; the open half of AUDIO_MIXER.md §12). (a) A 5 HP FX Loop module with SEND, RETURN,
   WET and BYPASS. (b) A Track Out in an "effect slot" role picked in its menu. (c) Not now: series effects patched
   between OUT and IN already work. **Rec. (a):** the panel says what it does and its controls can be mapped.
8. **What "global keyboard shortcuts" means.** (a) Opt-in Perform keys that work anywhere in Rack, editor closed
   (S5). (b) Editor keys that work whichever pane the mouse is over. (c) Keys you can remap, saved in Tessera's
   settings (EDITOR_UX §7's "v1.1" item). **Rec. (a) now, (c) later;** (b) would break "keys act on the pane under the
   mouse", which the scoped keys rely on.
9. **A set of songs** (S4). (a) Sections inside one song, Next song, scene follow. (b) The request as written: a CV
   input on output modules that picks the Tessera and track; it would switch only at a launch line, drop gates first
   and show "CV" on the screen, so nothing rebinds silently. (c) A set list of project files that Tessera preloads and
   swaps. **Rec. (a),** then (b) if a performer needs separate songs with separate patches.
10. **The metronome when an audio track is first armed** (R3). (a) Switch to Count-in only, and say so. (b) Leave it
    Off. (c) Ask once in a message. **Rec. (a).**
11. **Switch lanes** (S6). A Record lane punches a track in and out by itself; arming stays the switch for recording by
    hand (DECISIONS §73). (a) Mute and Record lanes under their track, with "Switches: another track". (b) Only their
    own track. (c) Only on the Master row, aimed at any track. **Rec. (a):** lanes stay where people look, and one
    control track can still run a set.
12. **Slice to a drum track** (A7): Tessera would play slices from drum lanes, its first audio played from notes.
    (a) Build it in Phase 4. (b) No: chop on the timeline and drive rack samplers with Make triggers. **Rec. (a):**
    it is the classic way to make a beat from a break, and nothing in the rack can load Tessera's slices.
13. **EOC per track** (P1). (a) A jack beside AUTO on Track Out and on Drum Out's poly row, and one per strip on
    Perform. (b) A ninth channel on the AUTO poly jack, no panel change. (c) Only on Perform. **Rec. (a).**
14. **What Lock editing locks** (S3). (a) The song's content; performing and mixing stay live. (b) The mixer and tempo
    too: only launching stays. (c) No lock; undo is enough. **Rec. (a).**
15. **The first loop on the timeline** (A1). (a) Set the song loop around it and turn the loop on. (b) Lengthen the
    clip to 8 bars instead. (c) As today. **Rec. (a):** the loop cycles at once and one undo takes it all back.
16. **Warp markers and the clip's window** (A3), against DECISIONS §128 ("With markers, window, edge and tempo drags are
    off"). (a) Window and edge drags work with markers, which move with the window; the tempo drag stays off. (b) As
    built: all three stay off until the markers are removed. **Rec. (a):** trimming a warped loop shouldn't mean
    losing its warp.

## 8. User feedback, item by item

| Trello | Status today | Here |
|---|---|---|
| "Mixer when the pane is very small looks very bad and unusable…" | Open | M1 (Q1), L2 |
| "Automation for all mixer elements (even children level, panning etc, in drums)" | Track volume, pan and sends, and Master volume, only | M4, S6 |
| "Bigger UX overhaul later" (DECISIONS §118) | This document | M1, M2, M3, L1–L4 |
| "State of the view between the panel view and the large editor should be consistent" | Open | L1 (Q2) |
| "CV selector for source and track on each output module… or maybe a project playlist feature / module" | Open | S4, P3 (Q9) |
| "Independent clip colors on the timeline view…" | **Done** (DECISIONS §89): a clip's own colour, in its slot and every copy | Nothing new; S1 notes how it marks loops |
| "EOC on the track device, for every time a clip on that track loops" | Open | P1, P3 (Q13) |
| "Automation lanes to control the other track's armed status… or really any other track's internal settings" | Open | S6 (Q11) |
| "Global keyboard shortcuts" | Open, meaning unclear | S5 (Q8) |
| "Tempo automation" (lower priority) | Scene tempo only | S6, Phase 4 |
| "Time signature as independent beats/subdivision selectors" (lower priority) | **Done**: Beats per bar and Beat unit are picked on their own (manual 03) | S4 adds a time signature per scene |
