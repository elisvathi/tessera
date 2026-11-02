# Review of the audio and mixer design (2 Nov 2026)

Two independent reviews of `AUDIO_MIXER.md` (revision 3) and its mockups, one by a product designer and one by a
UX specialist, merged here. Nothing is applied to the spec yet: the user picks first. "Both" means both
reviewers raised it.

Both agreed the base is strong. "IN is the track's sound", one Track Out, and "the overlap is the crossfade" are
each one rule covering many cases. Both said it is not ready to build. The rules have holes, new handles collide
with existing gestures, and many states are not specified.

---

## A. Rule holes (decide first)

1. **You can't hear a take while the source is still patched** (both). On an audio track, IN replaces the
   clips all the time. After recording, the musician has to unplug the cable to hear the take, and plug it back
   for take 2. It also contradicts today's "Monitor while armed" setting.
   *Proposal:* on an audio track, IN replaces the clips only when **OUT is also patched** (an effect loop) or
   **the track is armed** and Monitor while armed is on (the existing setting). Otherwise the clips play.
   Source chip: "Clips · IN when armed" / "IN · effect loop" / "IN · armed".
2. **An FX track with nothing on IN plays a dry copy of its sends** (UX). Turning up a send just makes that
   track louder, so it looks as if the send works with no effect in the loop.
   *Proposal:* an FX track with IN unpatched is silent. Chip "No effect" (dim), with a tooltip saying what to
   patch. The OUT meter still shows the sends arriving. Group tracks (P4) cover plain summing.
3. **"0 dB" means three different things** (product). Meters read 0 dB at 5 V, while the sound card clips at
   10 V. A 24-bit export writes 1.0 = 5 V, so it clips while the meter still shows "fine" amber.
   *Proposal:* meters and export always follow the Audio module: **10 V = 0 dB = full scale in the file**. The
   open constant then decides only the **import level**: a full-scale loop comes in at 5 V, reading −6 dB,
   level with the synths. This also settles the open "level" question. One clip light on the Master.
4. **Mute and solo with sends are not defined** (both).
   *Proposal:*
   - Mute silences the track and all its sends, before and after the fader.
   - Soloing an FX track plays only the effect: the tracks feeding it keep sending, but their own sound is muted.
   - Muting an FX track silences its return. OUT keeps feeding the effect, so tails come back at once on unmute.
5. **A clip dropped inside another leaves hidden audio** (product). The outer clip keeps sounding underneath,
   out of view, and a third overlap quietly trims a clip.
   *Proposal:* dropping inside **splits** the outer clip around the new one, with a visible crossfade at each
   seam (the same grammar as Stack off for notes).
6. **Tempo changes and moving tempo** (both).
   - Off clips change length with tempo, so a tempo change can create overlaps nobody made. *Rule:* tempo never
     trims a clip. Where three clips overlap, the latest start wins. One toast: "2 clips now overlap and
     crossfade".
   - The TEMPO knob can be modulated or mapped, and Tap exists. *Rule for any tempo source:* while the tempo
     moves, Stretch clips play through the varispeed reader (tape-like). A render starts after 500 ms of steady
     tempo. Sell it in the manual: "moving tempo sounds like tape".

## B. Gestures and hit zones

7. **Timeline handle hit map** (UX). Four collisions with code that exists today: the 6 px edge zones, the
   14 px title-strip drag (up/down = velocity, sideways = move), vertical body drag (move to another track), and
   the overlap.
   *Proposal:*
   - Handles get 12 px hit areas. Edge zones give up the top 10 px of the body to the fade handles.
   - Zero-length fade handles sit 3 px inside the corner.
   - Curve bends get always-visible dots, replacing "drag the line's middle".
   - In an overlap, the order is: crossfade handle and dot, then edges, then bodies. Selection never beats an
     edge there.
   - Handles show on bodies at least 16 px tall and clips at least 40 px wide.
   - Cursors: ResizeH for fades and crossfades, ResizeV for curves.
   - Readout pills: "Fade in 1/32 · 81 ms", "Crossfade 1/8 · 163 ms", "End of file".
8. **Gain lives on the title strip** (UX). Up/down on the title strip is velocity scale on note clips, and it
   is free on audio clips. Make it clip gain there, so the gesture means "the clip's level" everywhere. The
   top-centre gain bar goes; the tempo chip covered it anyway. A dim "−3.0 dB" shows in the title bar when gain
   isn't 0 dB.
9. **Header level bar vs. row resize** (both). The 3 px strip's hit area in short rows covers the 4 px
   resize edge, and double-click to reset fights double-click to make the row compact.
   *Proposal:*
   - Drags are relative: a click never jumps the level, and a click with no movement selects the track.
   - In short rows, the strip sits just above the resize edge. The first 4 px of movement decide: sideways =
     level, up/down = resize.
   - The in-panel editor drops the dB text first, then the pan knob.
   - The pan knob gets a 16 px hit area.
   - Rewrite §7.5 against today's single-line rows: the "1 voice (auto)" line was already removed (§51).
10. **Esc** (UX). It would cancel a drag, leave sends mode, cancel a bounce, clear the selection, and close
    the editor.
    *Fixed order, whatever pane is under the mouse:* cancel drag → leave sends mode → clear selection → close
    editor. Esc never cancels a bounce or an export: the progress strip gets a **Cancel** button, and Stop also
    cancels.
11. **Keys** (UX, checked against `Shortcuts.cpp`).
    - T is the Mute tool, so Tap works only inside the "Tap along" popover (T or Enter).
    - F4 is free, but needs a `keyName()` case.
    - New keys: Ctrl/Cmd+Alt+T add an audio track; Ctrl/Cmd+Shift+O import audio; Ctrl/Cmd+B bounce;
      Ctrl/Cmd+Alt+E export audio.
    - A new mixer key scope: Up/Down set the selected strips' volume, Left/Right move between strips. Scoped
      actions must win over the global ones.
12. **Fades and grid snap** (UX). On a 1/16 grid a 10 ms fade is impossible.
    *Proposal:* below one grid step, fades don't snap. Beyond it they snap to grid lines. Alt frees.
13. **Dragging an automated fader** (UX). "Sets a new value at the playhead" would silently edit a lane.
    *Proposal:* while held, the fader overrides the lane, then returns to it over 50 ms. It writes into the lane
    only while the track is armed (DECISIONS §73–74).
14. **Rack habits** (UX). Accept Ctrl as well as Shift for fine drag. Make sure the panel editor's Alt hand-off
    (DECISIONS §34) covers the mixer pane.

## C. Simplify: one concept, one control

15. **Speed is set in one place: the clip tempo** (both). Today speed can change four ways: clip tempo, instance
    stretch (Alt+right edge), Clip Length ×2 (§47), and Repitch pitch. Pitch exists twice: clip Pitch and
    instance Transpose.
    *Proposal:*
    - For audio, Alt+right edge edits the clip tempo, with a readout like "92 → 46 BPM".
    - Clip Length means how long the clip loops, never speed.
    - Instance Transpose is hidden on audio clips, as velocity scale already is.
16. **Cut:**
    - Track Out's "OUT sends before/after the fader" menu: it breaks effect loops. OUT is always before the
      fader; stem export covers direct outputs.
    - The bounce dialog's "Before / After fader": one rule, the track's sound at IN.
    - The "→ Master" output chip on every strip: plain dim text until groups exist.
    - The loudness bar inside every strip meter: strips show peak and hold, and one loudness number goes on the
      Master.
    - The "solo-safe" label: make it a tooltip.
    - The audio editor's Draw tool: it has no job until warp markers. Keep Select and Slice.
    - "Clip gain curves" (P4): use the Volume lane in the clip instead (§38 supports clip lanes).
17. **Words** (both).
    - The FX strip button becomes **"Show sends"**, and "send −15.5" replaces "→ FX 1" under the faders. The
      arrow read as a routing change.
    - "Before the fader / After the fader" everywhere; "pre" only as short text on knobs.
    - FX tracks are called by their track name everywhere, with a small FX badge. Send labels use two letters
      plus the colour.
    - FX tracks sit at the end of the track list, just above the Master, so rows and strips share one order.
18. **Tempo chip → a badge** (both). A small dim "92 ▾" in every audio clip's title bar, always there, opens the
    menu. The big chip shows only on the clip just dropped, until the next click.
    - The file-tag, file-name and loop-length guesses run at drop time, so loops play right from the first
      sound.
    - Hovering ×2 or ÷2 previews the beat grid on the waveform.

## D. First minute and setup cost

19. **A silent MAIN** (both). On the first drop with MAIN unpatched:
    - a toast says "To hear audio tracks, patch MAIN L/R on the Tessera panel to the Audio module";
    - an amber dot shows beside MAIN on the panel while the master has signal but MAIN has no cable;
    - the folded mixer tab reads "MIXER · MAIN not patched" in amber.
    - Optional helper: a **"Connect to Audio module"** button that adds both cables (one Rack undo step).
20. **File drops** (both). GLFW reports only the drop, so a live preview is impossible.
    - **Where files go:** dropping on the Tessera panel or empty space makes a new audio track per file, all at
      the drop bar. Several files dropped on one track go end to end. A drop on a note track or FX row makes an
      audio track just below. Never refuse a drop. .mid goes to MIDI import.
    - **After the drop:** snap to the beat, select, scroll into view, toast "Imported … · Undo".
    - **Ctrl+drop onto a clip** replaces its file and keeps place, length, fades and gain.
    - A click in the audio editor's ruler plays from there.
21. **The first loop sets the tempo** (product). In a song with no other clips, the first dropped loop sets the
    song tempo by itself, as one undo step, with "Keep 120 BPM" in the chip.
22. **Track Out setup** (product).
    - Default binding: the **selected** track if it has no Track Out. Otherwise the first instrument or drum
      track without one, never an audio, FX or master track unless it is selected.
    - Helpers in the strip's source chip menu: **"Add a Track Out for this track"** (placed beside Tessera,
      bound, one undo step) and **"Show in the rack"**.
23. **A metronome you can hear** (product). Recording needs a click, and CLICK is only a trigger.
    *Proposal:* Record ▾ → Metronome: Off / Count-in only / While recording / Always, with a level. It plays
    on MAIN only, never in exports, stems or bounces.
24. **Patch size** (product). Copied files and float recordings can make patches hundreds of MB, and Rack
    repacks on every save.
    *Proposal:* show the audio size in the Master tooltip from P1. Warn once above about 300 MB. Move "Remove
    unused" to P1. Smaller take storage (FLAC needs an encoder, which Rack doesn't ship) is a later choice.

## E. States the spec must define (UX)

| State | Behaviour |
|---|---|
| Decoding | Clip appears at once, "Loading" in its title, waveform fills in left to right, silent until ready |
| Render failed | Keeps the varispeed version; amber "!" with a tooltip; "Try again" in the clip menu |
| Unsupported or corrupt file | Nothing created; toast "Couldn't read 'loop.ogg'. Tessera reads WAV, AIFF, FLAC and MP3." |
| File too long / out of memory | Estimate first; above about 20 min or 1 GB total, ask once; clear message if loading fails |
| More than 2 channels | Mixed down; badge "6→2" and a note in the inspector |
| Missing file | Hatched clip, silent; strip chip "Missing file"; one toast on load "2 audio files are missing · Find…" |
| Sample-rate change | Strip "Preparing audio for 96 kHz · 12 of 40"; clips keep playing through varispeed; a running recording stops and keeps its take |
| Tessera bypassed | Track Outs fade out over 5 ms; display "Tessera bypassed" |
| Track Out bypassed | No IN→OUT bypass route (it would make a feedback loop); strip chip "Track Out bypassed" |
| Track deleted | Track Out shows "Track deleted" in amber, IN ignored, light amber |
| Cable on a dim jack | Amber ring; tooltip "Nothing comes out here for an audio track" |
| Two Track Outs on one track | Summed; chip "IN · 2 Track Outs" |
| FX track deleted | Its sends go in the same undo step; sends mode exits; toast "Deleted Reverb and 5 sends to it · Undo" |
| Master IN patched without OUT | Amber chip "IN only: replaces the mix" (the whole song would go silent behind one cable) |
| Bad signal (NaN or inf) on IN | Treated as silence; chip "Bad signal on IN" |
| Polyphonic cable on IN | Summed to stereo; said in the jack tooltip |
| Clip in a crossfade, inspector | "Crossfade with … · 1/8" replaces Fade out; its curve stays editable |
| Recording error (disk full, writer behind) | Stops and keeps the take, title "Recording stopped: disk full" |
| Editing during stem export | Mute and solo locked, with a tooltip |
| Sends mode and pane changes | Folding the mixer leaves the mode; maximise and restore keep it |

## F. Mixer details (UX)

- **Small sizes:** fader at least 60 px. Below 260 px of pane height, sends become dots. Below 180 px, compact
  strips (name, M/S, level bar). The Master is pinned right, and the other strips scroll sideways. Many FX
  tracks: send rows grow, never hidden. The in-panel fold tab is 16 px.
- **Meters:** −60 to +6 dB, a 0 dB line, 1.5 s hold, 20 dB/s fall. Fader taper puts 0 dB at 75% of travel.
  The clip light shows the peak value. The mixer tab shows "peak −6.2", not a bare dB.
- **Right-click** on any mixer control: Automate in the timeline / Type a value… / Reset (sends add Before /
  After the fader). The strip name: Rename, Colour, Latency…, Delete track.
- **Arm** shows on every non-FX strip, as on the rows.
- **Colour is never the only signal:**
  - Dim jacks: tooltip, plus an amber ring when a cable is on one.
  - Amber zone: a 0 dB line and the peak value.
  - Pre-fader: the text "pre".
  - Sends mode: hollow fader caps and the "send" prefix.
  - Automation: a ring.
  - "patched": the word itself.
  - The FX tint: the FX badge.
- **Text and contrast:** strip text at least 9 px. Check all eight colour schemes.

## G. Delight

1. The crossfade X forms live while you drag, with a readout pill.
2. A hard stop at the end of the file, a bright tick, and "End of file".
3. A 0 dB soft detent on faders, clip gain and the level bar, with the mark brightening for that moment.
4. Group drags light every selected fader cap: "3 tracks · −2.0 dB".
5. The folded mixer tab shows 3 px live meters per track and the master peak.
6. Sends mode: the faders glide to their send positions over 120 ms, and glide back on leaving.
7. The waveform scales live while you drag the gain.
8. The first time a synth's audio comes back, the Track Out light and the strip meter move together, and the
   chip briefly reads "IN · Track Out ✓".
9. The waveform fills in as real decoding progress.
10. Calm rows: the header groove draws in the track colour at low strength, bright only near the top.

## H. Plan and mockups

- **Split Phase 1** (product).
  - **P1a "a loop in time":** drop and import, the tempo badge, Stretch / Off, waveforms, gain and fades, MAIN,
    the Master row and audio strips.
  - **P1b:** crossfades, the audio editor, Repitch, reverse and normalise.
- **The editor mockup doesn't match today's editor.** It lacks the clips column (§42). It shows the old
  "6 TRACKS +" corner instead of "+ Add track" (§52), and text track numbers instead of the colour tiles. The
  lanes arrow is missing.
- **The Tessera panel has no free row for MAIN.** The panel layout must change, and the mockup should show it.
- **The FX OUT meter "only appears when patched"**, which breaks "draw dim, never hide". Draw it dim instead.
- **The Stack switch** is a song-wide item in the tool menu (§52), not per track. Its tooltip gains "Audio
  tracks never layer". The §91 row moves never apply to audio clips.
- **The Audio files window** is referenced in P1 but planned for P3.
