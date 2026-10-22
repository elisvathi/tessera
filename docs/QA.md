# Manual QA checklist

Run before each milestone commit. Items marked *(later)* become testable in the milestone named.

## Transport and timing
- [ ] Play/stop from the button and from RUN (toggle and gate modes). Gates drop on stop; no hanging notes.
- [ ] RESET button and input return to the start (to the loop start when LOOP is on).
- [ ] Tap tempo: four taps set the tempo; a pause of more than 2 s starts a new measurement.
- [ ] CLOCK, BAR and CLICK line up with notes on a scope (same sample).
- [ ] LOOP on: EOC fires each wrap; no hanging notes at the wrap.
- [ ] Sample rate change while playing: tempo stays correct.

## Output routing
- [ ] A new Track Out follows the last clicked Tessera, on its first track that has no output yet.
- [ ] Prev/next buttons step through instrument tracks; each step can be undone.
- [ ] Source and Track menus work; each change can be undone.
- [ ] Two Track Outs on the same track output the same thing.
- [ ] Delete Tessera: Track Outs show "No source", gates go low, nothing crashes. Undo restores the binding.
- [ ] Bypass Tessera: all gates go low on every Track Out.
- [ ] Two Tesseras in one patch, readers bound to each, both play.
- [ ] Rename a Tessera: Track Out displays follow the new name.
- [ ] Save and reload the patch: bindings and instance names survive.
- [ ] Copy/paste Tessera + Track Out together: the pasted Track Out follows the pasted Tessera ("Main 2").
- [ ] Paste a Track Out on its own: it keeps following the original Tessera.

## Song, undo and saving
- [ ] Tracks menu: mute and solo each track; Ctrl/Cmd+Z and redo restore exactly.
- [ ] Muting while playing silences the track at once; unmuting brings it back on the next note.
- [ ] Save, quit, reopen: the song (tracks, mute/solo, notes) is unchanged.
- [ ] Rename Tessera, then undo past the rename: earlier song edits still undo correctly.
- [ ] Initialize (Ctrl+I) resets the song; undo brings it back.
- [ ] Load a patch saved by milestone 1 (no song in it): the default song plays.

## Editor shell
- [ ] EDITOR button and "Open editor" menu item open the overlay; Esc (or ✕) closes it.
- [ ] With Leviathan's Deepcache in the patch (it replaces Rack's module browser): Esc closes the editor without
      crashing, Esc with a dropdown open closes only the dropdown, and the module browser still opens afterwards.
- [ ] While open, nothing behind it reacts: wheel, clicks, Delete/Backspace, Ctrl+C/V/D. Ctrl+S still saves.
- [ ] Delete the Tessera while its editor is open (from another window or undo): the editor closes cleanly.
- [ ] Wheel scrolls, Shift+wheel scrolls sideways, Ctrl+wheel zooms at the mouse, Alt+wheel zooms rows.
- [ ] Middle-drag and Space+drag pan; tapping Space still plays/stops.
- [ ] Ruler: click moves the playhead, double-click plays from there, vertical drag zooms around the click.
- [ ] Loop strip: draw, move, resize edges, double-click toggles loop; each is one undo step; readout shows.
- [ ] Markers: double-click adds, drag moves, click jumps; all undoable.
- [ ] Overview: drag window scrolls, edges zoom, click jumps, double-click fits.
- [ ] Hold Z: drag a rectangle to zoom; click zooms in, Alt+click out. Ctrl+[ / ] walk zoom history.
- [ ] Follow: Page and Scroll modes; following pauses while dragging or after scrolling, resumes after 2 s.
- [ ] Track headers: select, M/S (S toggles and keeps other solos, Shift+click S toggles and un-soloes the rest), drag bottom edge to resize (Alt: all),
      double-click edge toggles compact/expanded. Output dot shows bound Track Out modules.
- [ ] Piano roll shows the selected track's clip; switching tracks scrolls to the new clip's notes.
- [ ] Splitter drag and double-click; Tab switches pane; Shift+Tab maximizes; I toggles the inspector.
- [ ] Close and reopen the editor, and reload the patch: layout, zoom and track heights are restored.
- [ ] Editor size 75–200% from the module menu.

## In-panel editor
- [ ] Drag Tessera's right edge: the panel widens in HP steps, neighbours are pushed aside; narrow it below
      ~36 HP and it folds back to 24 HP. Ctrl/Cmd+Z restores width and neighbour positions in one step.
- [ ] Module menu > Panel editor: Off / 48 / 80 / 120 HP do the same.
- [ ] The panel shows the arranger above the piano roll; Arranger / Both / Piano roll switch the view, and
      the splitter, Tab and Shift+Tab work as in the overlay.
- [ ] Track headers in the panel: select, mute, solo and resize, as in the overlay.
- [ ] The panel piano roll shows the selected track; the arrows change track (the overlay follows).
- [ ] Wheel, Ctrl+wheel, Alt+wheel, Space+drag, Z-hold zoom work while hovering the panel editor; the rack
      still scrolls when the mouse is elsewhere.
- [ ] Delete/Backspace over the panel editor never deletes the module; Ctrl+Shift+D (not ours) still works.
- [ ] "Full editor" opens the overlay; panel and overlay keep separate zoom.
- [ ] Lower Rack's room brightness: the panel editor stays lit.
- [ ] Save/reload, duplicate and copy/paste a widened Tessera: the width comes along and nothing overlaps.

## Piano roll editing
- [ ] Every tool from the toolbar and its key: S, D, P, E, C, T, Y. Tooltips show the keys.
- [ ] Pointer: click selects, Shift adds, Ctrl/Cmd toggles, drag on empty space rubber-bands (touching notes).
- [ ] Move snaps the note start to the grid (an off-grid note lands on it too), Alt free, Shift locks the axis,
      Ctrl/Cmd copies.
- [ ] Resize from either edge (all selected notes); Alt+Shift on an edge stretches the selection.
- [ ] Draw: click places last-used length/velocity; drag right sets length, up/down pitch; right-drag erases.
- [ ] Paint, Erase, Mute, Slice (click, line, Shift+click equal parts), Velocity tool and Alt+drag on a note.
- [ ] Transform box (2+ notes, Pointer): sides stretch time, top/bottom scale pitch, corners both. Ctrl+Shift+T hides it.
- [ ] Overlap rule: dropping a note on another of the same pitch trims it, or replaces it if fully covered.
- [ ] Keys: Delete, Ctrl+A / Ctrl+Shift+A / Ctrl+I, Ctrl+C / X / V, Ctrl+Shift+V at mouse, Ctrl+D,
      arrows transpose and nudge (Shift = octave / bar, Ctrl+Alt = fine), Shift+Alt+←/→ length,
      Ctrl+Alt+↑/↓ velocity, Alt+arrows cycle the selection, Ctrl+1..4 grid.
- [ ] Velocity lane: drag a stem, drag a selected group, Shift freehand, Alt ramp; unselected notes are locked
      while something is selected.
- [ ] Keyboard: click to hear, drag for a glissando, Ctrl/Cmd+click selects the pitch.
- [ ] Hear notes on the track's outputs while stopped; Alt+A silences editing previews.
- [ ] Every edit is one undo step and undo restores the selection. Esc cancels a drag, then clears the selection.
- [ ] Small editor's top-right corner: play, stop, record (right-click: its menu) and loop work as in the full editor;
      the window-and-arrow icon opens the full editor. With either pane maximized they stay in view.
- [ ] Timeline Slice tool: hovering a clip shows a line where a click would cut (snapped; Alt free).
- [ ] Ghosts (All tracks) with a 1-bar looping clip on another track: from the clips panel its ghost repeats every bar;
      from a timeline clip the ghosts are exactly what the other tracks play under it on the timeline.
- [ ] Ctrl/Cmd+drag a note, an arranger clip, a slot clip and a device, letting go of Ctrl before the mouse: each
      is still a copy and the original stays. Pressing Ctrl mid-drag turns a move into a copy.
- [ ] A track without a clip: double-click the grid creates one.
- [ ] All of the above also in the panel editor.

## Tracks
- [ ] Ctrl/Cmd+T and Ctrl/Cmd+Shift+T (arranger) add a track after the selected one; "+" in the corner too.
- [ ] Below the last track: the "+ Add track" row offers instrument/drum; right-click empty space (headers or
      lanes) offers the same; double-click empty header space adds an instrument track.
      The 65th track is refused.
- [ ] Shift+click selects a range, Ctrl/Cmd+click toggles; M / S / colour / voices act on the whole selection.
- [ ] Drag a header to reorder (insertion line); drop on a group header's lower half to join; drag a group
      header to move the whole group. Each is one undo step.
- [ ] Ctrl/Cmd+G groups the selection; fold arrows hide and show members (no undo step); Ungroup from the menu.
- [ ] Double-click a name to rename (Enter commits, Esc cancels, clicking away commits); typing never
      triggers shortcuts.
- [ ] Delete (arranger focused) and Duplicate; the last track can't be deleted.
- [ ] A Track Out bound to a deleted track shows "Track missing" and goes silent; undo brings it back playing.
- [ ] Duplicate a track with a clip: editing the copy's notes leaves the original alone.
- [ ] All of the above in the panel editor's arranger.

## Clip launcher
- [ ] Double-click an empty slot: a one-bar clip appears, selected, and the piano roll shows it.
- [ ] Click a triangle while playing: it blinks yellow until the next bar line, then starts exactly on it.
      With launch quantize "None" it starts at once. A clip's own quantize (right-click menu) wins.
- [ ] Click a triangle while stopped: the clip starts at once and the transport starts.
- [ ] Click an empty slot's square or a header's square: the track stops at the quantize line, notes released.
- [ ] Scene triangle: every track switches; tracks with an empty slot in that scene stop. A scene tempo moves
      the BPM knob when the scene starts.
- [ ] Timeline playing, launch a scene with empty slots (launch quantize 1 bar): until the bar line the scene's stop
      squares blink yellow and the tracks' timeline lanes dim; then they stop and "Back to timeline" shows.
- [ ] Click a timeline clip that plays its 1-bar loop for 4 bars: the piano roll shows the loop's repeats faded up
      to bar 5, a yellow line there, a yellow bracket under the ruler's loop bar ending at bar 5, and "4 bars on the
      timeline" in the header. Drag the bracket's end: the timeline clip gets longer or shorter (one undo). Clicking a clip in the clips panel shows none of it.
- [ ] Clip menu (clips panel and timeline) → Length: ×2 longer, ×½ shorter, and a typed % (Enter). Notes, loop,
      clip automation and every timeline use stretch with it; one Ctrl/Cmd+Z restores all.
- [ ] Drag a clip to another slot (moves), with Alt or Ctrl/Cmd (copies); onto a taken slot (replaces). A
      playing clip keeps playing after it is moved, and after a scene is inserted above it.
- [ ] Clip menu: rename, duplicate, delete, loop off (plays once, then the slot stops), launch
      quantize, export / replace with a .tessera-clip file. Empty slot menu: create, import, stop track.
- [ ] Scene menu: rename, colour, tempo, insert above/below, delete (not the last). "+ Add scene" adds one.
- [ ] Delete / Ctrl/Cmd+D on a clicked clip; after clicking a header they act on tracks.
- [ ] Click a clip in the clips panel: it opens in the notes pane. A drag still moves or places it.
- [ ] Rename a clip three ways: the clips panel menu, the arranger clip menu, and Rename… in the clip chip that
      starts the notes pane header (the same chip in the piano roll and the drum steps). Every place shows the
      new name, a linked clip renames all its uses, and one Ctrl/Cmd+Z restores the old name.
- [ ] Fold a group: one column; its triangle launches the group's clips in that scene.
- [ ] Every edit is one undo step; undo after a move puts the clip back and it keeps playing.
- [ ] Panel: PREV / NEXT change the scene on the display; LAUNCH launches it (light on while queued);
      STOP ALL stops everything. SCENE input (0.1 V and semitone modes) and LAUNCH trigger do the same.
- [ ] The display's track strip shows a line under tracks playing a clip (blinking while queued).
- [ ] Save and reload: the same clips play. An old patch plays each track's first clip.
- [ ] Clips and timeline show side by side in the full and panel editors. F2 widens the clips and back. The
      maximize button in each pane's corner maximizes and restores it. The piano roll playhead shows only while
      its clip plays.
- [ ] Plays switch: Clips frames the clips and dims the timeline; Timeline frames the timeline and dims the
      clips, except a clip launched over the timeline, which stays lit. Undo restores the switch.

## Arranger
- [ ] ARRANGE (panel) / the transport Plays switch: the timeline plays; notes start on time. In Launcher
      mode the timeline is silent. The display shows ARRANGER.
- [ ] A note crossing the song loop end stops there; seeking cuts sounding notes; stop leaves no gate high.
- [ ] Launch a clip in Arranger mode: that track leaves the timeline (lane dimmed), "Back to arr." appears;
      clicking it brings the track back. Stopping a track also takes it over.
- [ ] Drag a clip: snaps to the grid and to nearby clip edges, markers, loop and playhead; Alt is free; Shift
      locks an axis; dragging up/down moves it to other tracks (readout shows the track).
- [ ] Ctrl/Cmd+drag: linked copy (link badge on both). Ctrl/Cmd+Alt+drag: independent copy.
- [ ] Right edge: a looping clip repeats (seam notches); Alt+right edge stretches (×factor shown). Left edge
      trims, content stays put. Alt+drag body slips the content.
- [ ] Header strip: drag down/up changes velocity scale (percentage shown on the clip).
- [ ] Draw tool (D): drag in empty space makes an empty clip; a click makes a one-bar clip. Erase and Mute tools
      sweep over clips. Slice tool click splits.
- [ ] Double-click a clip: the piano roll shows it (its playhead follows the arranger); again: maximized.
      Double-click an empty lane: a one-bar clip.
- [ ] Ctrl/Cmd+D, Ctrl/Cmd+E (at the mouse, else the playhead), Ctrl/Cmd+J, Ctrl/Cmd+U, Delete, copy/cut/paste,
      arrows nudge, Shift+Alt+arrows change length, Up/Down transpose. Each one undo step.
- [ ] Rubber band from empty space selects touched clips; Shift adds, Ctrl/Cmd toggles.
- [ ] Clip menu: transpose, velocity, reset stretch, mute, loop, export. Lane menu: create, paste,
      place a launcher clip.
- [ ] Launcher "To arrangement": launch clips while playing, stop: the clips appear on the timeline where they
      played; with the song loop on, the last pass wins. One undo step.
- [ ] A folded group's row shows its tracks' clips.
- [ ] Save and reload in Arranger mode with a taken-over track: same state after loading.
- [ ] All of the above in the panel editor (Arranger strip button).

## Drum tracks
- [ ] Select a drum track: the step grid opens (G or "Piano roll" switches; "Steps" in the piano roll comes back).
- [ ] Drum step grid zoom: Ctrl/Cmd+wheel zooms time around the mouse (up to 16x), = and - zoom, F fits the clip.
- [ ] Draw tool: click a step, drag across steps (fast drags fill every step), drag from an on step to clear,
      right-drag clears. One undo each. Paint and Slice (chosen in the piano roll) act as Draw and light it.
- [ ] Pointer tool: click an empty step turns it on and selects it; click a step selects it (Shift adds,
      Ctrl/Cmd toggles); drag from empty space draws a box that selects; drag selected steps moves them by steps
      and lanes, Ctrl/Cmd+drag copies; a step they land on gives way. Esc / Ctrl+Shift+A clear, Ctrl+A selects
      all, Delete removes. Selected steps have the blue outline; the selection stays when G switches views.
- [ ] Erase and Mute tools: click or drag across steps. Muted steps are dim with an outline.
- [ ] Velocity tool or Alt+drag a step: velocity readout; the fill height follows, for the whole selection when
      the step is in it.
- [ ] With steps selected, the inspector shows them (fields, chart, operators). Right-click a step: the menu acts
      on the selection (a step outside it becomes the selection): velocity, chance, repeats, occurrence,
      recurrence, mute, delete. Repeats split the cell; chance, occurrence and recurrence show a corner dot.
- [ ] In-panel editor: the tools fit the header (they give way last); the step menu fits.
- [ ] Lane header: click the name to hear it, M / S work, double-click renames, right-click menu.
- [ ] Lane menu: note, output, steps (12 against 16: faded repeats; it drifts against the bar while playing),
      rate, choke group, output length; Euclidean, rotate, reverse, fill, randomize, clear, copy / paste lane;
      add / delete lane. Kit button: load presets.
- [ ] Drum Out: binds to the first drum track without an output; jacks 1-8 trigger (1 ms) with lights; TRIGS
      and VEL poly carry all outputs; a gate length lane holds its gate; closed hat chokes open hat.
- [ ] Drum Out V1-V8: each holds its lane's last velocity (0-10 V). A patch saved before these jacks existed
      keeps its TRIGS / VEL / AUTO cables on the same jacks.
- [ ] Playing in the launcher and in the arranger: polymetric lanes sound the same in both.
- [ ] Save and reload: lanes, kits and Drum Out binding come back.

## Scales
- [ ] A new Tessera shows "No key"; nothing is dimmed or flagged. An old patch opens with "No key" too.
- [ ] Key picker (transport key, piano roll key button): click a root, then a scale; the piano roll previews each
      click (rows, snap, fold). Esc puts the old key back; Enter or a click outside applies it as one undo step.
      "Notes: Move with the key / Keep pitches" decides what happens to notes and is remembered with the patch.
      Drum clips never move.
- [ ] Piano roll key button: Song | This clip. On This clip, give the clip its own key ("(clip)" shows in the
      header), reopen (it opens on This clip), then "Same as song". The inspector's Key row follows. "Custom
      scales…" applies the key and opens the custom scale menu.
- [ ] Highlight: key rows light, roots tinted, keyboard dims other keys; notes outside the key get an amber
      corner. Turning it off brings back plain black/white rows.
- [ ] Snap: Draw, Paint and double-click land on the key; dragging notes and Up/Down move by key steps; Alt
      drags chromatically. Transform box pitch handles stay in key.
- [ ] Fold (button or Ctrl+Shift+F): only key rows plus rows with notes; the view stays centred; dragging moves
      by rows. On a drum track in the piano roll, Fold shows one named row per lane.
- [ ] Custom scales: new scale from this key, rename, toggle keys (grid updates at once, each toggle undoes),
      delete (key falls back to no key; undo restores it). Saved with the patch.
- [ ] Chord stamp: triad / 7th / 9th in key change quality with the degree; fixed shapes, inversions, Open and
      Drop 2 voicings; the readout names the chord; one undo step per chord.
- [ ] Note menu "Fit to key" moves only notes outside the key.
- [ ] Save and reload: song key, clip keys, custom scales, and the Highlight / Snap / Fold / chord settings.

## Clip length
- [ ] Drum step grid and piano roll: the Length button sets 1-16 bars; Double copies the steps into the new half;
      Halve; each undoes in one step.

## Recording
- [ ] Panel: REC button (red light, blinking during count-in), REC V/OCT, GATE, VEL jacks. An old patch keeps its
      cables on RESET / RUN / SCENE / LAUNCH.
- [ ] Arm a track (header dot or launcher column dot; Ctrl+click arms several). Notes on REC GATE play through the
      armed track's Track Out (monitor), quantized to the key when that option is on.
- [ ] MIDI: pick a device in the module menu; a track set to MIDI channel 2 only takes channel 2.
- [ ] Count-in 1 bar: 4 clicks on CLICK OUT, then playback and recording start together.
- [ ] Launcher mode, empty track: record, play, stop recording: a clip of whole bars appears in the selected scene's
      slot (red "Recording" while it records) and keeps looping in time. Undo removes it in one step.
- [ ] Fixed length 1 bar: the clip appears after one bar and the next passes overdub it (each pass undoes alone).
- [ ] Record on a track playing a clip: new notes appear in red in the piano roll and merge at each loop end.
- [ ] Arranger mode: a red region grows with the notes; on stop a clip on whole bars appears. Overdub keeps the old
      notes under it; Replace removes them. With the loop on, each pass is its own undo step.
- [ ] Input quantize 1/16: the take lands on the grid; one Ctrl+Z brings back the played timing.
- [ ] Play a few notes while stopped, press Capture (Ctrl+Shift+R): a clip with them appears on the selected track.
- [ ] Stop while recording: no hanging notes; save and reload keeps the record settings and the MIDI device.
- [ ] Record while moving the mod wheel on a track with no Mod wheel lane: no lane appears, and a toast says it was
      not recorded. Click **Add lane**: a Mod wheel track lane appears; the next take records into the clip. Bend
      and pressure record with no lane set up. Changing the keyboard's sound (bank select) shows no toast.
- [ ] Import a General MIDI file: each clip gets lanes only for controllers that move (no CC 0, 32, or one-value
      volume, pan, reverb lanes).

## Automation
- [ ] "A" on a track header shows its lanes; "+ Add automation lane" adds Auto 1, 2, … on free AUTO channels. Drag a
      lane's bottom edge to resize it. A Track Out's AUTO jack sends the lanes (one channel each, in volts).
- [ ] Click adds points, drag moves (Alt free, Shift one axis), double-click deletes, the middle handle bends,
      Ctrl+drag a segment, right-click a point (curve, value in volts, delete). Draw freehand and Alt+line, Paint
      steps, Erase. Box-select 2+ points: scale, stretch and tilt with the box handles. Every edit undoes in one step.
- [ ] Lane menu: range presets change the header's volts; slew smooths a square; thin, quantize, invert, reverse,
      randomize, clear; stamp a sine across two bars.
- [ ] Piano roll lower pane: pick "Add clip lane", draw on it; it loops with the clip and overrides (or modulates)
      the track lane on the same channel.
- [ ] Map a knob: the banner appears, touch a VCO's FREQ, the editor comes back, the header says "→ VCO ·
      Frequency"; playing turns the knob. A knob mapped by MIDI-Map asks before being taken over. Reload the patch:
      the mapping is back.
- [ ] Arm a lane, record from its knob in Touch mode: only the stretch you moved is replaced. Latch and Write too.
      AUTO REC input with an LFO in Write mode records the LFO. Each pass undoes in one step.
- [ ] Alt+drag a range over two tracks: Delete empties it (clips cut at its edges, automation outside unchanged);
      Ctrl+Shift+Delete closes the gap; Ctrl+Shift+I opens one; Ctrl+D copies it after itself.
- [ ] Ripple on: deleting a clip pulls the later clips on its track left. Automation follows clips: moving a clip
      takes the curve under it along; turning it off leaves the curve.

## Piano roll v2
- [ ] Select notes: the inspector shows NOTE sliders. Drag Velocity sideways: all move; Alt+drag spreads; click and
      type *0.5 halves; double-click resets Release to 50%. Dice and ramp change every note in one undo step.
- [ ] Operators: Chance on → 50% and "50%" on the notes; off → back to 100%. Repeats on → 2 ticks inside notes and
      you hear ratchets. Recurrence on → passes 1 plays, 2 skips (loop the clip to hear it); click grey bar 4 → a
      4-pass cycle. Occurrence "Fill" plays only while F1 or the FILL button is held.
- [ ] Lane tabs: Chance, Repeats and Micro show stems; drag one, Shift-drag draws, Alt-drag ramps. Pitch tab: click
      inside a note to add a bend, drag it; V/OCT bends while it plays.
- [ ] ⋯ menu: Strum a chord (preview moves, Enter applies, Ctrl+Z undoes); Arpeggiate; Humanize then Esc changes
      nothing; Q quantizes to the grid.
- [ ] Ctrl+K then A, D, G: notes sound on the selected track; Z/X shifts the octave in the banner. Arm the track and
      record: the keys are recorded. Ctrl+R: each key press writes a note at the red cursor, a chord held together
      is one step, → rests, Backspace removes the last step, Esc stops.
- [ ] Ghost notes: pick Track 1 while editing Track 2: Track 1's notes show as outlines; Alt+Shift+click opens it.
- [ ] Launcher clip menu "Edit this scene's clips together": the other clips show dimmed with a chip strip; click a
      dimmed note to edit its clip; × ends.

## MIDI export
- [ ] Place a clip on the timeline, File → Export MIDI… (or Ctrl+Shift+E), Export…: the file opens in a DAW with the
      tempo, the track names, notes where they play, drums on channel 10. The toast names the file.
- [ ] Range "Loop": only the loop is written, starting at 0. "One file per track" writes "<name> - <track>.mid".
- [ ] Automation "As CC": a lane appears as its CC in the DAW; 14-bit gives MSB + LSB. Notes with chance and
      "Rendered" differ from "As written". Timing "As played" with swing moves off-beat 1/16s.
- [ ] MPE with a pitch curve: the DAW (MPE mode) bends that note only. Flatten: CC74 and poly aftertouch.
- [ ] Launcher clip menu → Export as MIDI…: one track named after the clip. Two clips selected in the arranger →
      one file per clip or one file with two tracks.
- [ ] A track with an Arpeggiator and a Chord: Export with Devices "Applied": the DAW shows the arpeggio's notes;
      "Bypassed": the clip's notes as written.
- [ ] Swing at 100%: off-beat 1/16s play later (listen or watch the gate); the notes stay on the grid in the editor.

## Project files and DAWproject (docs/DECISIONS.md §39)
- [ ] The editor's File button and the module's right-click File submenu show the same items: Open project…,
      Save project (the file's name once it has one), Save project as…, Export MIDI…, Export DAWproject….
- [ ] Save project as… writes a .tessera file. Change the song, then Open project… that file: the song, the BPM
      knob, loop on/off, Plays, scenes, devices, automation, instance name and editor layout come back (a file
      saved before 13 Oct 2026 has no BPM and leaves it as it is); the panel's width doesn't
      change. One Ctrl+Z brings back the song from before opening. Save project writes the same file again.
- [ ] Opening a file that isn't a project says so and changes nothing (in the toast, or a message box when the
      editor is closed).
- [ ] Output modules keep their tracks when a project saved from this patch is reopened.
- [ ] Export DAWproject…: open the file in a DAW that reads DAWproject: one track per Tessera track with its name
      and colour; arranger clips where they are, with their notes; launcher clips as scenes (looping); tempo, time
      signature, markers. Devices "Applied": the arpeggio is in the notes. Automation "As CC": the lanes appear as
      CC automation. Muted tracks only with "Include muted".

## Milestone 14
- [ ] Patch an LFO square (48 Hz = 120 BPM at 24 PPQN) into CLOCK: the panel says EXT CLOCK, the editor "EXT 120.0";
      play starts on the next pulse and stays in time; change the LFO rate: the tempo follows; stop the LFO: the
      song holds; RESET restarts it. Change PPQN in the module menu.
- [ ] Track Out → Track → Follow the track selected in the editor: select another track in the editor; the Track
      Out shows it and outputs it. The arrows turn following off.
- [ ] Press ? in the editor: every shortcut, grouped; any key closes it.
- [ ] Playing notes glow as they start; mute a note (T): it is hatched; hover the grid: a faint row/column band;
      drag a note: an orange tick on the ruler shows where it snaps.
- [ ] Rack's CPU meter with 16 busy tracks stays near 1 %.

## Stacked clips
- [ ] Ctrl+drag a clip so the copy overlaps it: both show, stacked, the track grows, and both play (voices add up).
- [ ] Turn Stack off (corner): dragging a clip over another trims it, as before.

## Launcher beside the arranger
- [ ] Show the arranger: the launcher panel sits left of the timeline, rows lined up with the tracks. Launch a
      slot with its triangle, stop with an empty slot's square, launch a scene from its header.
- [ ] Drag a slot's clip onto the timeline: a preview follows, it lands snapped, one Ctrl+Z removes it. Drag a
      timeline clip onto an empty slot: the slot shows it (linked). Drag between slots; Ctrl copies.
- [ ] Drag the panel's right edge; hide it with the corner button; it stays hidden after reopening the editor.

## Inspector chart
- [ ] Select several notes: click Velocity, its chart opens (bars per value, a mean marker). Drag Mean: all move.
      Drag Spread left: they gather toward the mean; right: they spread. Drag Chaos: they scatter; each drag is one
      Ctrl+Z. Spread and Chaos show their amount while dragging and spring back after.
- [ ] Click Pitch: its chart opens and Velocity's closes. One note selected: no chart, no ramp or dice buttons.

## Device strip (docs/DEVICES.md)
- [ ] F3 and the Devices button open and close the strip above the notes, in the piano roll and the drum editor.
      Closed, a track with devices shows the chain in a thin bar; clicking it opens the strip.
- [ ] + Add device, the + between cards and the Add device tile open the browser: type "delay" (Echo), "arp",
      "humanize" (Random); ↑ ↓ and Enter add it; Esc closes. The footer says where it goes.
- [ ] Each of the nine devices changes what you hear: Transpose, Scale (in and out of key), Velocity, Note
      Length (fixed, legato), Chord (on a one-note melody: a chord, with Voices on Auto), Arpeggiator (on the
      grid, patterns, Hold), Echo (repeats fade), Random (different each pass, the same on the same pass), Note
      Filter (range, every Nth). Bypass (power button): the notes pass unchanged.
- [ ] Knobs: drag (heard while dragging, one Ctrl+Z on release), Shift for fine, double-click resets, click the
      value to type it, right-click: Reset, Type, Modulate with, Remove modulation. Choices open a menu; switches
      flip on a click.
- [ ] Drag a card by its header to reorder (an orange line shows where); Ctrl+drag copies. Click a card, then
      Delete removes it and Ctrl+D duplicates it (only right after clicking in the strip; otherwise they edit notes).
- [ ] Modulators: + Add with each chip (whole song, the track, a device); each type moves (LFO, Envelope on each
      note, Steps drawn in the popover, Random, Ramp over bars, Macro, Note). The row's little graph moves.
- [ ] Mapping: click ↗, only reachable knobs light (a device modulator lights only its device), the message shows
      under the strip; drag a lit knob up / down: a ring in the modulator's colour; a click adds +50%; Esc ends.
- [ ] Popover: settings in two columns (an LFO shows one Rate), Moves with amount bars and ×, Map to knobs,
      CV out → AUTO output (a Track Out's AUTO jack moves) and → A knob in the rack (touch a knob: it moves).
- [ ] Stop, loop and mute while an arpeggiator or legato plays: no hanging notes. Remove a device while a note is
      held: it ends.
- [ ] Save, reload the patch: devices, knob values, modulators and their routes come back; Rack knobs follow again.
- [ ] Live notes (MIDI or keyboard, track armed) go through the chain, also while stopped.
- [ ] In-panel editor: the strip starts closed; open it and drag the divider down: the strip shrinks, then
      becomes a bar saying there is no room; nothing is ever drawn over the rack. Open a modulator: the popover
      fits beside the list and scrolls. The header never overlaps the Devices button.

## Knob automation (docs/DEVICES.md §1.3)
- [ ] Right-click a knob: "Automate in the arranger" opens the arranger with a lane under the track ("Arpeggiator
      · Gate", "knob · 80%"); "Automate in this clip" opens it as the piano roll's lower lane. Adding a lane
      changes nothing until you draw. Once a knob has a lane, its menu says "Show its lane" instead.
- [ ] The knob wears an "A" and draws its value in the lane's colour. Playing, it follows the lane (modulation
      rings still move on top); stopped, it is free and shows its saved value.
- [ ] The lane's scale and readouts are in the knob's units (5%–200%, 1/16...). Clicked or drawn points on a
      choice or whole-number knob hold until the next point; the point menu lists the choices by name.
- [ ] A clip lane overrides the track lane on the same knob, or with "Modulate" moves around it.
- [ ] Turning an automated knob while it plays: you hear it while you hold it, then the lane takes it back.
- [ ] Recording: arm the lane, record, turn the knob: the lane is written (Touch, Latch, Write as for Rack knob
      lanes); the knob's saved value doesn't change. Undo removes the take.
- [ ] The lane menu has no AUTO output, voltage range, slew or Rack mapping. AUTO lanes still get channels 1–8:
      a knob lane takes none, and the Track Out / AUTO module labels ignore knob lanes.
- [ ] Delete the device: the lane stays with "Target missing" in red; undo brings both back.
- [ ] MIDI export with devices applied follows the lanes (an arpeggiator's rate change shows in the file);
      knob lanes never become CC lanes.
- [ ] Save and reload: the lanes and their knobs come back.

## Later milestones
- [ ] Undo after every editor operation *(3, 4)*
- [ ] Patch reload fidelity for param mappings *(11)*
- [ ] 64 tracks *(5)*, 16-voice chords *(5)*, drum choke groups *(8)*
- [ ] External clock jitter *(14)*

## Bug fixes after milestone 8

- [ ] Close the full editor with the mouse resting on the close button (tooltip showing): no crash.
- [ ] Panel editor: select a note and press Delete / Backspace / Ctrl+D: only notes change, the module stays.
- [ ] Arrow keys transpose and nudge (Shift+Up/Down = octave) in the panel and full editor; the rack never
      scrolls while the editor has the keys.
- [ ] While a song plays, open a clip that isn't playing (or a new 1-bar clip): the piano roll draws normally.
- [ ] Voices "Auto": a track with 3-note chords sends 3 channels; an old patch's 8-voice tracks load as Auto.
- [ ] Snap 1/8 or 1/8T: grid lines follow the snap.
- [ ] Erase button with a selection deletes it (piano roll and arranger).
- [ ] Ctrl+drag from empty space draws a selection box with any tool; Ctrl+drag on a note or clip copies it.
- [ ] Panel arranger: clips show their notes under the name bar; stepping tracks with the piano roll arrows
      scrolls the launcher / arranger to the track and opens its timeline clip.
- [ ] Panel piano roll: a rubber band near the grid's edge doesn't scroll the notes away.

## Bug fixes after milestone 9

- [ ] Drag a note that starts off the grid, left and right: its start lands on grid lines and never jumps
      against the drag. Arranger clips the same.
- [ ] Open a short (1-bar) clip, drag its loop end past the right edge of the piano roll: the view scrolls
      and the loop grows.
- [ ] Arranger mode, launch a clip: that track's header says "Launcher clip playing", the transport shows
      "Back to arr."; clicking it brings the track back to the timeline and the note goes away.
- [ ] Drum step grid: the velocity lane under it shows the lane last clicked (its header is tinted); drag across
      it to set velocities (one undo step); the step fills follow. Works in the panel editor too.

## Bug fixes after milestone 11
- [ ] C major, fold on, chord stamp Major: every chord is two rows and two more, on any degree.
- [ ] Drum track: "Steps | Piano roll" in both views, the current one lit; either button switches.

## Bug fixes after milestone 12

- [ ] Launcher mode (ARRANGE off): draw a track lane with points, map a knob, press play: the knob moves and the
      lane's AUTO channel moves.
- [ ] Add Tessera Auto Out: it binds to a track, shows "1 Auto 1" beside the first jack with a moving bar; arrows
      and the Track menu switch tracks (drum tracks too). A lane on AUTO 3 shows beside jack 3.

## Bug fixes, 19 Oct

- [ ] Full editor: launch a clip from its slot, move the mouse over the piano roll, press Ctrl+A then Ctrl+D: the
      notes are selected and copied, no tracks are added, and the focus ring is on the notes. Over the song pane the
      same keys act on the song. Same in the panel editor.
- [ ] Two notes touching on one row: just left of the seam a drag resizes the first note's end; just right of it,
      the second note's start. Both ways, both directions. Two touching arranger clips the same.
