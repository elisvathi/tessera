# Audio tracks and the mixer

Status: **proposal, revision 2 (2 Nov 2026).** Mockups: https://claude.ai/artifact/WNtGof3EbafDF7yptw5w5i
(private; source HTML is not in the repo). Nothing here is built. The user's answers are in §12. When the build
starts, each choice there gets a DECISIONS.md entry, and the build follows the phases in §11.

This feature replaces the DESIGN.md §1 non-goal "No audio recording, no audio clips". It also changes DESIGN.md
§2 ("no external dependencies"): §9.1 lists the libraries it needs.

---

## 1. What musicians come to do

Design from these journeys. Each one must feel short and obvious.

1. **Play a loop in time.** Drop a drum loop from a file browser onto Tessera. It lands on a new audio track, at
   the song's tempo, and plays. Change the tempo: the loop follows and keeps its pitch.
2. **Hear the rack and the files together, at the right levels.** Patch each synth voice's output back into its
   Track Out. Open the mixer. Every track has a fader, a pan knob and a meter. Patch Tessera's MAIN L/R into
   the Audio module. Done: one place to balance the whole song.
3. **Share one reverb.** Add an FX track. Send some of the lead and the pads to it. Its Track Out sends that mix
   to a reverb module and takes the reverb's output back in.
4. **Put a rack effect on one track.** Patch an audio track's Track Out OUT into a delay, and the delay's output
   back into the same module's IN. Like a hardware insert.
5. **Record.** Arm an audio track, patch a source (the Audio module's input, a synth) into its IN, press record.
6. **Bounce.** Turn a synth part into audio, to free the voice, to edit it as audio, or to keep a happy accident.
   One command records the track's sound in real time onto a new audio track.
7. **Edit audio.** Cut, trim, move, loop, fade, crossfade, reverse, change gain and pitch, without ever damaging
   the original file.
8. **Finish.** Export the song (or every track as a separate file) to WAV.

The first minute matters most: journey 1 must work with **one cable** (MAIN L/R to the Audio module) and no
other setup.

---

## 2. The idea in one picture

Every track has a **sound**: what you hear from it in the mixer.

- **One Track Out for every track.** Instrument, drum, audio, FX tracks and the master all use the same Track Out
  module: the note jacks, plus OUT L/R and IN L/R. Jacks that do nothing for the bound track are drawn dim
  (§8.1). Drum Out stays as the per-lane drum console, and Auto Out stays for named automation jacks.
- **IN is the track's sound.** Track Out and Drum Out get a stereo IN. What you patch there is that track's
  sound in the mixer.
- **An unpatched IN falls back to the track's own audio.** An audio track's sound is its clips. An FX track's
  sound is what the sends bring it. An instrument or drum track with no IN has no sound in the mixer (its strip
  says so and shows how to fix it).
- **OUT sends the track's own audio into the rack**: an audio track's clips, an FX track's sends, the master's
  mix. Instrument and drum tracks have no audio of their own, so their OUT is dim. With OUT → effect → IN, the
  effect sits on the track, like a hardware insert. Patch IN alone and you replace the track's audio with
  whatever you patch.
- **Recording and bouncing capture the track's sound**, at the IN, before the fader.
- **The mixer then does the rest**: fader, pan, mute, solo, sends, into the master. The master leaves Tessera at
  **MAIN L/R** on the Tessera panel.

One rule ("IN is the track's sound") covers synth returns, effect inserts, recording and bouncing. There is no
input-monitoring switch and no per-track input picker: the cable is the setting.

```
 notes  ─► Track Out ─► VCO ► VCF ► VCA ─► Track Out IN ─┐
 clips  ─► audio track ──────────────────────────────────┤ (or OUT ► delay ► IN, an insert)
                                                         ▼
                                    fader · pan · M/S · sends ─► FX tracks ─┐
                                                         │                  │
                                                         ▼                  ▼
                                                       MASTER ─► (insert) ─► MAIN L/R ─► Audio module
```

Nothing changes for people who don't want the mixer: their patches keep working, no jack is moved, and MAIN
stays silent until something reaches the master.

---

## 3. Feature list

Grouped by phase (§11). "P1" ships first.

### 3.1 Audio tracks and clips (P1)
- New track kinds: **Audio** and **FX**. The **Master** always exists (one per song, §4.4).
- Import: drag files from the computer onto the timeline, a track, an empty area (makes a new audio track) or a
  launcher slot. File ▸ Import audio… does the same. WAV, AIFF, FLAC, MP3.
- Imported files are **copied into the patch** (Rack's patch storage), so a saved patch is complete.
- Audio clips work in the launcher and on the timeline, with the same gestures as note clips: move, copy
  (linked or independent), trim either edge, slip the content (Alt+drag), loop (right edge), split (Ctrl/Cmd+E),
  duplicate (Ctrl/Cmd+D), glue (Ctrl/Cmd+J), make unique, mute, colour, rename, range edits.
- Waveforms in the clip tiles (under the title bar, which stays) and in the launcher slots.
- **Tempo and speed done properly** (§5): every clip either follows the song tempo with high-quality time
  stretching (pitch kept), follows it like tape (speed and pitch together), or plays at its own speed.
- Tempo detection on import, with a chip that shows the guess and lets you fix it in one click.
- Clip gain, pitch (semitones and cents), reverse, normalise.
- Fades in and out on every timeline clip, with a bendable curve. Audio clips never layer: **where two clips
  overlap, they crossfade**, and the overlap is the crossfade (§6.3).
- Silent safety fades of 2 ms at every edge, so cuts never click.
- **The audio editor**: the lower pane shows the selected audio clip large, with the file's beat grid (§6.4).

### 3.2 The mixer (P1 basic, P2 full)
- A **Mixer pane** under the notes pane, folded to a tab by default, with maximise like the other panes (§7).
- One strip per track: name and colour, where its sound comes from, sends, pan, mute / solo / arm, fader with a
  peak and loudness meter, output.
- FX strips, then the Master strip, on the right.
- Every mixer control can be automated with the same lanes as device knobs (DECISIONS §38).
- **Volume and pan in each track row** of the song pane too (the user asked for both places). The volume slider
  shows the live level inside it, so the row needs no separate meter. They are the same values as the strip.
- P1: audio tracks and the master only (that is what can make sound without the round trip). P2: every track.

### 3.3 The rack round trip (P2)
- Track Out gains OUT L/R and IN L/R and binds to any track, the master included. Drum Out gains a stereo kit
  IN. R follows L when only L is patched.
- MAIN L/R outputs on the Tessera panel (P1, since journey 1 needs them).
- FX tracks and sends (pre or post fader). FX tracks keep playing while another track is soloed.

### 3.4 Recording, bouncing, export (P3)
- Record audio into armed audio tracks, in the timeline and into launcher slots, using the existing record
  modes and count-in.
- **Bounce** a track, a clip or a time range to a new audio track, in real time, with a tail for reverbs.
- **Export audio**: the master, or every track as its own file (stems), in one real-time pass. WAV 24-bit or
  32-bit float.
- Audio files window: every file in the song, its use count, memory use, "Remove unused", "Find missing file".
- Recording offset setting, with a guided measurement (§8.4).

### 3.5 Deeper editing (P4)
- Warp markers: pin a point in the audio to a beat, to fix a loose take or quantise audio to the grid.
- Transient markers and "Split at transients".
- "Make triggers from audio": turns a drum loop's hits into notes on a drum track, to drive rack drums.
- Takes and comping for audio recorded in Stack mode (takes are kept inside the clip, not as layers).
- Group tracks: a track folder that also sums its tracks into one strip.
- Clip gain curves; DAWproject export of audio clips; FLAC export.

### 3.6 Not in scope
- Hosting plugins. The rack is the effects rack.
- Faster-than-real-time rendering. Rack runs in real time, so bounces and exports take as long as the music.
- Editing samples inside a file (pencil on the waveform). Every edit stays non-destructive.
- Per-lane audio returns for drum tracks (one stereo IN per drum track).

---

## 4. Data model

The model stays in ticks (DESIGN.md §4). Audio adds frames of a source file, and the one exception in §5.4.

### 4.1 Audio sources: the files

```cpp
struct AudioSource {          // one file in the patch, never changed after import or recording
    AudioSourceId id;
    std::string name;         // shown to the user, from the file name
    std::string file;         // path inside the module's patch storage, e.g. "audio/a1f3-drumloop.wav"
    uint64_t contentHash = 0; // finds the same file again after paste, duplicate or a moved patch
    int channels = 2;         // 1 or 2 (more are mixed down on import, with a note to the user)
    double sampleRate = 48000;
    int64_t frames = 0;
    double tempo = 0;         // what the file was played at: detected, from the file, or set; 0 = none
};
// Song::audioSources, sorted by id.
```

- A source is **immutable**. Recording and consolidating make new sources; nothing ever overwrites one.
- Sources are not deleted during a session (undo may need them). On patch load, files in storage that no source
  references are removed, and "Remove unused" in the Audio files window removes unreferenced sources on purpose
  (one undo step that keeps the files until the next load).

### 4.2 Audio clips reuse `Clip`

Audio clips live in the existing `ClipPool`, so slots, timeline instances, linking, Make unique, loop, launch and
stacking work without new machinery.

```cpp
enum class ClipKind : uint8_t { Notes, Audio };
enum class TempoFollow : uint8_t { Stretch, Repitch, Off };      // §5
enum class SoundType : uint8_t { FullMix, Drums, Melody, Voice }; // picks the stretch settings, §5.3

struct AudioContent {
    AudioSourceId source;
    int64_t sourceStart = 0;   // the source frame at clip tick 0
    TempoFollow follow = TempoFollow::Stretch;
    SoundType sound = SoundType::FullMix;
    double sourceTempo = 120;  // defaults to AudioSource::tempo, or the song tempo at import
    float gainDb = 0;          // -inf..+24
    int8_t semitones = 0;      // -24..24
    float cents = 0;           // -50..50
    bool reverse = false;      // plays the clip's window of the source backwards
    // P4: std::vector<WarpMarker> warps; // {Tick beat; int64_t frame}
};

// Clip gains: ClipKind kind = Notes; std::optional<AudioContent> audio;   (audio clips have no notes)
```

- `Clip::length`, `loopStart`, `loopEnd`, `loopMode` keep their meaning, in ticks.
- A clip's kind must match its track: note clips never land on audio tracks and the other way round. Commands
  refuse it; the UI shows the "not allowed" cursor and a short reason.

### 4.3 Timeline instances: fades and crossfades

`ArrangerClip` gains, for audio clips only:

```cpp
struct Fade { Tick length = 0; float curve = 0; };  // curve -1..1; 0 = linear for fades, equal power in a crossfade
Fade fadeIn, fadeOut;
```

- **A crossfade is not stored.** It is the overlap of two instances on an audio track. Over the overlap, the
  earlier clip's `fadeOut.curve` and the later clip's `fadeIn.curve` shape it; the overlap's length replaces their
  fade lengths there. Moving a clip away ends the crossfade, and its own fades come back.
- `layer` stays 0 on audio tracks (no sub-rows).

- Existing instance fields keep working on audio: `transpose` adds semitones; `stretch` (Alt+drag right edge)
  stretches the audio; `velocityScale` is not shown for audio clips.
- Launcher clips have no fades; they use the 2 ms safety fades and a short fade at each loop seam.

### 4.4 Track kinds, the master and the channel

```cpp
enum class TrackType : uint8_t { Instrument, Drum, Audio, Fx };

struct Send { TrackId fx; float levelDb = kSilentDb; bool preFader = false; };

struct Channel {               // one mixer strip
    float volumeDb = 0;        // kSilentDb..+6
    float pan = 0;             // -1..1
    std::vector<Send> sends;   // only sends that were turned up; FX tracks themselves have none
    TrackId output;            // invalid = the master; P4: a group track
    int latencySamples = 0;    // extra delay this track's sound is known to have, §8.4
};
// Track gains: Channel channel;
// Song gains: MasterChannel master { TrackId id; float volumeDb; std::vector<AutomationEnvelope> automation; }
```

- **The master is not a `Track`.** Many paths assume a track has clips, voices and export. `Song::master` has its
  own `TrackId` (from `newId`), so Track Out binds to it like to any track, and mixer automation uses it. The song
  pane shows it as a thin last row, for its volume and its automation lanes.
- FX tracks are `Track`s (rows, automation, an output module), with no slots and no clips. Their row shows
  the name, M / S, volume, pan and the lane arrow.
- Mute and solo stay the existing `Track::mute` / `solo`. They now also silence or keep the track's sound.
  Soloing never silences FX tracks or the master ("solo-safe"): they carry only what the soloed tracks send.
- Schema version goes up by one. Old songs load with a master at 0 dB and every channel at 0 dB, centred, no
  sends. MAIN stays silent until something is patched.

---

## 5. Speed, tempo and time stretching

The user's requirement: **proper speed and time-stretching algorithms on import.** This section is the core of
P1, not an add-on.

### 5.1 The three ways a clip meets the tempo

| Follow tempo | What you hear when the song is faster than the clip's tempo | Use it for |
|---|---|---|
| **Stretch** (default for loops and longer material) | Same pitch, shorter. High-quality time stretching. | Drum loops, music, vocals |
| **Repitch** | Faster and higher, like speeding up tape. No stretch artefacts at all. | Breaks you want to "pitch up", tape effects, modular wobble |
| **Off** (default for one-shots) | Plays at its own speed and pitch. | Hits, risers, speech, field recordings |

With Stretch, **Pitch** (semitones, cents) changes the pitch without changing the speed. With Repitch, Pitch
changes the speed too (it is the same as playing a different note on tape), and the clip's length on the
timeline follows.

### 5.2 Where the quality comes from

The audio thread never stretches, decodes or resamples at high quality. It plays prepared buffers.

1. **Import**: decode the file to float. Keep the original file untouched in patch storage.
2. **Prepare to the engine's sample rate** on a worker thread with a high-quality band-limited resampler
   (libsamplerate, best sinc). Every source gets one prepared copy at the engine rate; this alone fixes "wrong
   speed" from a 44.1 kHz file in a 48 kHz patch. Redo it when Rack's sample rate changes.
3. **Render each clip that needs it**: when a clip's ratio (song tempo ÷ clip tempo), pitch, reverse or sound
   type is not neutral, a worker renders it with the time stretcher (§9.1, behind a swappable interface) into a
   cached buffer, rendering offline at the best quality settings (no real-time shortcuts). Linked instances
   share a render.
4. **Play**: the engine copies samples from the prepared or rendered buffer, sample for sample.

**While a render is not ready** (just imported, tempo knob being dragged), the clip plays the closest buffer it has
through a real-time varispeed reader: a windowed-sinc interpolator (16 taps, polyphase), not linear or cubic.
You hear the change at once, a little tape-like, and it turns into the clean stretch as soon as the render is
done. The switch is a 10 ms crossfade, so it never clicks. The clip's title bar shows a small spinner while it
renders. Renders start at the playhead and go outwards, so the part about to play is ready first.

**Repitch** never needs the stretcher: it is the varispeed reader all the time (it is exact, not an
approximation).

**External clock**: tempo from CLOCK moves a little all the time. Renders are made for the smoothed tempo
(DESIGN.md §7); the small difference left is taken up by the varispeed reader (capped at ±1%, about 17 cents
at worst, normally far less), and a new render starts when the smoothed tempo settles more than 0.5% away.

### 5.3 Sound type

One choice, in plain words, picks the stretcher's settings for the material (transient handling, window size,
formants, channels together):

- **Drums**: keeps attacks sharp.
- **Melody**: smooth, for bass, keys, pads, single instruments.
- **Voice**: keeps the character of a voice when pitching (formants).
- **Full mix**: a balance, for finished music.

Tempo detection (§5.5) suggests Drums or Full mix; the user changes it in the clip chip or the inspector.

### 5.4 The tick model, and its one exception

- Stretch and Repitch clips are tick-based like everything else: their length is in ticks; the ratio comes from
  the song tempo.
- **Off clips** play at their own speed, so their length on the timeline depends on the tempo. They store their
  start in ticks and their length in **source frames**; the timeline works out the length in ticks from the
  current tempo when it draws, hit-tests and snaps. This is the one place the model needs the tempo; it is
  passed in (`TempoContext`), never stored. Record this in DECISIONS.md.

### 5.5 Tempo detection on import

Runs on a worker in under a second for a loop; the clip plays meanwhile (at the song tempo, as Off).

1. Tempo written in the file (the tempo chunk loop libraries put in WAV files), or in the file name
   ("drums_92bpm.wav", "Loop 120 BPM.aif").
2. **Loop-length guess**: if the file holds a whole number of beats (1, 2, 4, 8, 16, 32, 64; 3, 6, 12 in 3/4
   songs) at a tempo between 70 and 180, prefer the candidate nearest the song tempo. Most loops are found here,
   exactly.
3. **Onset analysis** for longer files: an onset-strength curve, its autocorrelation over 60–200 BPM with a
   gentle preference around 120, and the strongest early onset as the downbeat.
4. **One-shot**: shorter than about 1.5 s with one onset that decays → Follow tempo **Off**.

The result appears as a **chip on the new clip**: `92 BPM · 2 bars · Stretch ▾`. Its menu offers ×2 and ÷2,
"Type a tempo…", "Tap along…", Follow tempo, Sound type, and, when the song is empty or has only this clip,
"Set the song to 92 BPM". The chip stays on the clip for this session; afterwards the same choices are in the
inspector. Never a modal dialog on import.

---

## 6. Editing audio

### 6.1 On the timeline

Same grammar as note clips (EDITOR_UX §6.3); new parts in bold.

| Gesture | Result |
|---|---|
| Drag body / Ctrl+drag / Alt+Ctrl+drag | Move / linked copy / independent copy |
| Drag left or right edge | Trim (non-destructive; the hidden audio is still there) |
| Drag right edge of a looping clip | Repeats the audio |
| Alt+drag right edge | Stretch the instance (the audio is time-stretched with the clip's Sound type) |
| Alt+drag body | Slip the audio inside the clip |
| **Drag a fade handle** (top corners of the body, under the title bar) | Fade in / fade out length |
| **Drag the fade line's middle up or down** | Bend the fade curve |
| **Drag the gain handle** (small bar at the top centre of the body) | Clip gain in dB; the waveform grows and shrinks live; double-click resets |
| **Drag one clip over another** (or either edge into its neighbour) | The overlap becomes a crossfade; drag the edge to change its length |
| **Drag the crossfade's middle up or down** | Bend both curves (equal power by default) |
| **Drag the crossfade handle** (top of the overlap) | Grow or shrink the crossfade evenly on both sides of its centre |
| Ctrl/Cmd+E | Split at the playhead or mouse, nudged to the nearest zero crossing within 1 ms |
| Ctrl/Cmd+J | Glue: one clip that plays the same; P3 renders a new file when the parts come from different files |

Fade, gain and crossfade handles are **always visible, small and dim**, brighter on hover (no hover-only
controls, per the 13 Oct review). Under 24 px of clip height they hide and the inspector has the values.

Waveforms: min/max per pixel from a peak cache, mirrored, in a lighter tone of the clip colour; scaled by the
clip gain so what you see is what plays. Fades shade the waveform. Reversed clips show a small ◀ in the title
bar. A clip whose render is not ready shows a spinner in the title bar.

### 6.2 Clip menu (right-click) and Inspector

Clip menu, audio section: Reverse · Normalise · Follow tempo ▸ · Sound type ▸ · Set tempo… · Bounce… (P3) ·
Show file in Audio files.

Inspector (audio clip selected), top to bottom: Name, Colour · **Gain** (dB) · **Pitch** (st, ct) · **Follow
tempo** (Stretch / Repitch / Off) · **Sound type** · **Clip tempo** (BPM, with ×2 ÷2) · **Reverse** ·
**Fade in / Fade out** (length, curve) · Loop · Start offset · Transpose · File (name, length, sample rate,
channels). Keep the inspector's "settings you can change" grammar (DECISIONS §55).

### 6.3 No layers, and overlaps crossfade (agreed)

- **Audio tracks never layer.** The Stack switch (DECISIONS §32) applies to note tracks only; on an audio track
  it is drawn dim with the tooltip "Audio clips don't layer: where they overlap, they crossfade". One row per
  audio track, always.
- **The overlap is the crossfade.** Where two clips overlap, the earlier fades out while the later fades in,
  across exactly the overlap you see. Equal power by default, so the level doesn't dip. To change it, drag an
  edge (shorter or longer overlap), the handle at the top of the overlap (both sides evenly), or the middle of
  the X (the curve).
- **Clips that only touch** get the 2 ms safety fades and no crossfade.
- **At most two clips sound at any moment.** A clip dropped wholly inside another sounds instead of it there,
  with a 10 ms crossfade at each of its edges (its fade handles lengthen them). Where a move would make three
  clips overlap, the one underneath is trimmed back, in the same undo step.
- **Pasting, recording in Replace or Merge, and range edits** follow the same rule. Recording in Stack mode keeps
  takes inside the clip (P4 comping) instead of layering them.
- The crossfade can only reach as far as the clips have audio: a clip trimmed to the very end of its file stops
  the edge there.

### 6.4 The audio editor (lower pane)

Opens when an audio clip is selected, where the piano roll would be. Same pane header grammar as the notes
pane (clip chip, tool switch, View ▾, maximise).

- **The whole file** across the pane, the clip's window bright, the rest dim, so trimming and slipping can be
  done here too (drag the window's edges or body).
- **Two rulers**: the song's bars on top, and under them the clip's own beat grid ("clip beats"). Drag the beat
  grid sideways to set where beat 1 is; drag a beat line to set the clip tempo (the grid stretches from beat 1).
  Tap along with T while it plays.
- Loop brackets, fades and gain, same handles as the timeline.
- Stereo waveform as two halves; mono as one.
- P4: warp markers on the beat grid (double-click adds; drag pins audio to a beat), transient marks.

---

## 7. The mixer pane

### 7.1 Where it lives

- A **third pane under the notes pane**, with its own splitter, folded to a 22 px "MIXER" tab by default, like
  the panel editor's CLIPS fold tab. A click on the tab opens it at its last height.
- Its top-right corner has **maximise / restore**, like the song and notes panes: the mixer fills the editor.
  **F4** toggles maximise (F2 and F3 are taken). Not a view switch; the other panes come back on restore.
- In the in-panel editor it starts folded; maximised it fills the panel editor.
- **Track rows have volume and pan too** (agreed). The layout is in §7.5.

### 7.2 A strip, top to bottom

1. **Colour bar and name** (click selects the track, same selection as the song pane).
2. **Source chip**: where the sound comes from, in words: "IN · Track Out", "IN · Drum Out", "Clips",
   "Sends", or "Not patched" (dim, with a tooltip: "Patch your voice's output into this track's Track Out IN").
   A small loop icon when OUT and IN are both patched (an insert is in place).
3. **Sends**: one small knob per FX track, named by its FX track's colour and first letters. Knobs at −∞ are
   drawn small and dim (never hidden). A dot marks pre-fader.
4. **Pan** knob.
5. **M · S · ●** (record arm on audio tracks). These are the same switches as in the track rows: same state.
6. **Fader and meter**: fader on the left, a two-channel meter on the right (peak bar with a fine loudness bar
   inside it, peak-hold tick, a clip light that a click clears). dB readout under it; double-click to type.
7. **Output chip**: "→ Master" (P4: or a group).

Strip width 64 px; 44 px narrow (sends become dots, pan a small arc) when the pane is short of room.
FX strips are tinted, after a gap; the Master strip is wider (88 px) at the far right, with a "MAIN not patched"
note when nothing is on MAIN L/R.

### 7.3 Gestures

Same modifier vocabulary as everywhere (EDITOR_UX §2): drag, Shift for fine, double-click to reset (0 dB,
centre, −∞ for sends), Alt bypasses snapping (faders softly snap to 0 dB). Dragging a fader of a selected strip
moves every selected strip by the same dB. A drag is one undo step, committed on mouse-up.

### 7.4 Level standard

Provisional: the user left this open. Keep it in one constant (`kVoltsAtFullScale`) so it can change before
release without touching anything else.

- **1.0 in a file = 5 V** in Rack, the usual level of an audio oscillator. Meters read 0 dB at 5 V.
- Rack's Audio module clips at 10 V, which is **+6 dB** on Tessera's meters. The meter's top 6 dB are shaded
  amber, and the clip light marks +6 dB.
- Recordings and bounces are written as 32-bit float WAV, so nothing clips in the file.

### 7.5 Levels in track headers

The user wants volume and pan in the song pane's track headers as well as in the mixer. Track rows are already
busy (name, A / ● / M / S, voices, lane arrow), so the layout matters. Three options are in the mockups; **A is
proposed**.

**A. Level bar under the name (proposed).** The row's second line becomes a horizontal volume slider. Its groove
shows the live level, green turning amber in the top 6 dB, so the slider is also the meter. The fader position
is a thin bright line across it, with the dB value after it and a small pan knob at the end.
- Reads like a fader and a meter at once, and it fits the rows' left-to-right reading.
- It replaces the grey second line ("1 voice (auto)"); voices move to the inspector and the row's tooltip.
- Rows under 40 px: the bar shrinks to a 3 px strip along the bottom of the header. It stays draggable (the whole
  bottom 8 px is the hit area), and the dB value and pan knob are hidden. They are still in the strip and the
  inspector.

**B. A mini fader at the right edge.** A vertical meter with a fader cap fills the row's height on the right of
the header, with pan as an arc above it. It looks like a small mixer strip, but it gets too short to use in
short rows, and it competes with M / S for the same corner.

**C. Number cells.** "−4.0 dB" and "C" as two drag-number cells (drag up or down), with a thin meter line under
the name. The most compact, but the level can't be read at a glance, and a cell doesn't look like a fader.

Rules for any option:
- **Same values, same undo, same gestures** as the strip: drag, Shift for fine, double-click resets to 0 dB or
  centre, Alt bypasses the soft snap at 0 dB. Dragging a selected row's level moves every selected row's level
  by the same dB. One undo step per drag.
- **While dragging**, a readout pill shows the value (as everywhere else in the editor).
- **Automated volume or pan**: the control follows the lane while playing and draws a small lane-colour dot.
  Dragging it while the lane plays sets a new value at the playhead, like dragging an automated device knob
  (DECISIONS §38).
- **Instrument or drum tracks with nothing on IN**: the slider still sets the level (it applies as soon as
  audio arrives). Its groove is hatched instead of empty, with the tooltip "No audio: patch your voice's output
  into Track Out IN".
- **FX tracks** have the same row controls. The **Master row** (thin, last) shows only the level bar and the dB
  value.
- **In-panel editor**: names stay first (DECISIONS §51). The level bar gets whatever width is left after the
  name and buttons, at least 40 px. Below that it becomes the 3 px strip.
- **Colours** of the groove, the amber zone and the hatching go in `Theme.hpp`, shared with the mixer's meters.

---

## 8. The rack side

### 8.1 Modules

| Module | Change |
|---|---|
| **Tessera** | MAIN L / MAIN R outputs, a third row in the output plate. The master after its fader. |
| **Track Out** | Binds to **any** track and to the master. Gains an audio plate: OUT L/R and IN L/R (R follows L when unpatched), with a small signal light. Stays 6 HP: the display gets shorter and the jacks are laid out again. |
| **Drum Out** | A stereo kit IN (L/R) under the lane rows. |
| **Auto Out** | No change. |

**One module, dim where it does nothing.** Track Out draws the labels (and a darker jack ring) of jacks that do
nothing for the bound track at about 35% strength, and their tooltips say why:

| Bound to | Note jacks (V/OCT … AUTO) | OUT L/R | IN L/R |
|---|---|---|---|
| Instrument / drum track | live | dim ("instrument tracks have no audio of their own") | live |
| Audio track | dim, except AUTO | the clips | live (insert return or record source) |
| FX track | dim, except AUTO | the sends' mix | live (the effect's return) |
| Master | dim, except AUTO | the master mix | live (master insert) |

- New jacks are appended to the port enums, so saved patches keep their cables even though the panel layout
  changes (cables follow port IDs, not positions).
- The track picker (menu and arrows) lists every track kind, the master last. Default binding on add: the first
  track with no Track Out yet, in track order.
- Menu: "OUT sends: before the fader (for effects) / after the fader (a direct output)".
- Panel art is drawn by Tessera's own panel code (`ui/widgets/Panel`), so the dimming follows the binding live.

### 8.2 Buses and threading

- **Tessera → Track Out**: extend the existing `OutputBus` (same pointer, same lifetime handling) with a
  parity-double-buffered audio array, one stereo pair per track slot plus one for the master, written every
  sample. Keep `TrackFrame` as it is: it is only rewritten when voices change (DECISIONS §31), and audio changes
  every sample.
- **Modules → Tessera (the return bus)**: each output module gets **its own return slot**, handed out on the UI
  thread when its binding resolves (several modules may share a track, so they must never write one slot).
  The module writes `{sample, trackId, patched, L, R}` into buffer `n & 1`; Tessera reads buffer
  `(n − 1) & 1` and uses a slot only when its sample stamp is `n − 1` and its trackId matches. A missing,
  bypassed or removed module simply drops out. Tessera sums every patched slot of a track.
- Copy the OutputBus tests for the return bus: shuffled module order, several threads, module removed mid-stream,
  two Tesseras.

### 8.3 What Tessera's `process()` does each sample

1. The sequencer, as today.
2. Audio clip playback for audio tracks (prepared or rendered buffers; varispeed reader when needed).
3. Read the return bus. Each track's sound = the sum of its patched INs, else its own audio.
4. Recording taps (§8.5).
5. Each strip: gain (smoothed), pan (−3 dB centre, constant power; balance for stereo), mute/solo, sends.
6. FX tracks: their OUT this sample is the sum of what was sent to them; their sound is their IN, or the sends
   sum when IN is unpatched.
7. Master: the sum; its sound is its Track Out IN when patched (a master insert); then the master fader → MAIN.
8. Write the audio array for the Track Out modules. Update meters every 32 samples.

Mixer values (volume, pan, sends) live in an atomic parameter block owned by the module, refreshed from the song
after every commit and written directly during a fader drag. The audio thread reads it every 32 samples and
smooths per sample. A fader drag never rebuilds the snapshot.

**No clicks, ever** (the audio version of "never leave a gate high"): stop, loop wrap, mute, solo, a clip
starting or ending, a render switch and a snapshot swap all use short fades (2–10 ms).

### 8.4 Latency

- Fixed and known: a note reaches an output module one sample late (exists); audio reaches Track Out's OUT one
  sample late; a return reaches Tessera one sample late; every Rack cable adds one sample.
- Tessera subtracts its own known part from recordings and bounces automatically.
- What it can't know (a module's internal delay, the sound card): per track `latencySamples` in the strip menu,
  and a global **Recording offset** with a guided measurement: patch CLICK out through the sound card and back
  into an armed audio track; Tessera plays a click and measures the round trip.
- P4: automatic delay compensation across strips (delay every strip to the slowest known path), for parallel
  processing without phase smear.

### 8.5 Recording and bouncing

- Arming allocates a lock-free ring buffer per armed audio track (about 5 s). The audio thread only pushes.
- A **dedicated writer thread** (never the UI thread: Rack's file dialogs block the UI thread) drains it into a
  32-bit float WAV in patch storage and updates the peaks for the live waveform.
- On stop, the UI commits one `RecordAudioTake` command: a new source and its clip(s). Overflow marks the take
  and shows a warning on the clip; it never blocks the audio thread.
- **Bounce** (clip menu, track menu, range menu): plays the range from one bar before (so delays and reverbs
  start), records the track's sound, and keeps recording for the **tail** (default "until silent": below −60 dB
  for half a second, at most 8 bars). Result: a new audio track under the source track, named
  "Bass (bounce)", and the source track muted (a checkbox in the bounce dialog, on by default). Progress shows in
  the transport: "Bouncing Bass · bar 3 of 8 · Esc cancels". Cancel leaves nothing behind.
- **Export audio** (File menu): the master, or stems (every track's sound, after the fader, plus the master) in
  one pass. Same progress strip.

### 8.6 Files and patch storage

- `createPatchStorageDirectory()` / `getPatchStorageDirectory()` hold the `audio/` folder; Rack packs it into the
  .vcv patch.
- **Verify in Phase 0, in the hidden Rack**: save and reload; module duplicate; copy/paste; delete and undo;
  two Tesseras. Where Rack does not copy storage, find the files by `contentHash` in the source module's
  storage (the pasted JSON says which module it came from) and copy them; otherwise show "Missing file".
- A .tessera project file (already a zip) carries `audio/` too.
- Missing files load as hatched clips with "Missing file" and "Find…" in their menu.
- Memory: decoded audio lives in RAM as float (a 4-minute stereo file at 48 kHz is about 92 MB, plus a render
  when stretched). The Audio files window shows the total. Renders are made only for clips that are used, shared
  by linked instances, and freed when no snapshot needs them.

---

## 9. Code layout and libraries

### 9.1 Libraries

| Need | Choice | Licence | Notes |
|---|---|---|---|
| Decode WAV, FLAC, MP3 | dr_wav, dr_flac, dr_mp3 (single headers) | public domain / MIT-0 | AIFF via dr_wav; verify, else a small reader in `model/` |
| Resample | libsamplerate | BSD-2 | Ships with the Rack SDK (`samplerate.h`); verify the symbols are exported by Rack on all three platforms |
| Time stretch | **swappable** (below): Signalsmith Stretch and Rubber Band (v4, R3 "finer") | MIT / GPL-2.0-or-later | |
| FFT (analysis, Signalsmith) | pffft | BSD | Ships with the Rack SDK |

**The time stretcher is swappable (agreed).** Signalsmith Stretch (MIT) and Rubber Band (GPL) sit behind one
interface. So:

```cpp
// src/audio/TimeStretcher.hpp: used only on worker threads.
struct StretchJob {
    const float* const* in; int channels; int64_t inFrames; double sampleRate;
    double ratio;            // output length / input length
    double semitones;        // pitch change, independent of the ratio
    SoundType sound;         // the adapter maps it to its own settings
};
class TimeStretcher {
  public:
    virtual ~TimeStretcher() = default;
    virtual const char* id() const = 0;   // goes into the render cache key and the saved render settings
    /// Renders the whole job into out (outFrames = inFrames * ratio). Calls progress() now and then; returns
    /// false when cancelled.
    virtual bool render(const StretchJob& job, float* const* out, int64_t outFrames,
                        const std::function<bool(double)>& progress) = 0;
};
std::unique_ptr<TimeStretcher> makeStretcher(const std::string& id);   // falls back to the default one
std::vector<std::string> availableStretchers();
```

- One adapter per library in `src/audio/stretch/`, each with its library vendored under `dep/<name>/` with its
  licence file. Nothing outside the adapter includes a library header.
- The Makefile picks which adapters to build (`STRETCHERS ?= signalsmith rubberband`). A release build can leave
  Rubber Band out completely with no code change.
- When more than one is built, Settings ▸ Audio ▸ "Time stretch engine" picks one (for the listening test and
  for users who build their own). Changing it re-renders in the background; playback carries on meanwhile.
- The song never stores which engine was used, so a song made with one engine opens with the other.
- The varispeed reader (Repitch and the "render not ready" fallback) is Tessera's own code, not part of the
  interface.
- A conformance test runs every built adapter through the same checks: output length, a stretched click track
  keeps its clicks on the beat within 1 ms, a stretched sine keeps its pitch within 1 cent, cancel works.

Phase 0 listening test: 8 files (drum loop, break, bass, vocal, piano, pad, full mix, speech) at 0.75×, 0.9×,
1.1×, 1.33× and ±3 semitones, rendered by both, compared blind by the user; plus render speed and a build on
macOS, Windows and Linux. Check Rubber Band's single-file build and R3 settings in its docs. Its higher CPU
cost matters less because renders are offline. The test picks the default engine; the other can stay built.

### 9.2 Where code goes

```
src/model/AudioSource.hpp        AudioSource, AudioContent, Fade, Channel (pure data)
src/model/AudioEdits.*           commands: import, fades, gain, crossfade, reverse, tempo, follow, pitch
src/model/MixerEdits.*           commands: volume, pan, send, output, latency
src/model/AudioAnalysis.*        peaks, tempo detection, onset curve, zero crossings (no Rack, unit tested)
src/model/WavFile.*              WAV writer (float/24-bit) and reader for tests
src/engine/AudioPlayer.*         per-track clip playback, varispeed reader, fades, crossfades
src/engine/Mixer.*               strips, sends, master, meters (no Rack, unit tested)
src/engine/ReturnBus.hpp         return slots (no Rack, unit tested)
src/engine/AudioRecorder.*       ring buffers for takes
src/audio/AudioWorker.*          decode, prepare, render, peaks jobs (worker threads, Rack allowed)
src/audio/RenderCache.*          renders by (source, ratio, pitch, sound, reverse, rate, stretcher id)
src/audio/TimeStretcher.hpp      the swappable interface (§9.1)
src/audio/stretch/*              one adapter per library
src/audio/TakeWriter.*           the writer thread
dep/<library>/                   vendored libraries with their licence files
src/module/TrackOutModule.*      gains OUT/IN, any track kind, live dimming
src/ui/views/AudioClipDraw.*     waveform tiles, fade / gain / crossfade handles
src/ui/views/AudioEditor.*       lower pane for audio clips
src/ui/views/MixerPane.*         the mixer
src/ui/editing/AudioActions.*    import, drop, bounce, export
```

`src/model/` and `src/engine/` still build without Rack. The engine gets plain pointers to prepared buffers in the
snapshot; the snapshot holds `shared_ptr`s, so a buffer can only be freed on the UI thread when its snapshot
retires (DESIGN.md §5).

---

## 10. Instructions for the implementer

Read this before starting any phase.

1. **Read first**: this file, CLAUDE.md, DESIGN.md §5–7, DECISIONS §2–3 (the bus), §31 (performance), §32 and
   §91 (stacking), §38 (knob lanes), §42 (panes), §55 (inspector), EDITOR_UX §2 and §6.3.
2. **Phase 0 comes first and decides things.** Do not write feature code before the stretcher interface, the
   default engine and the patch-storage behaviour are settled and written into DECISIONS.md.
3. **Merge through a pull request**, as every branch does.
4. **Audio thread rules** (CLAUDE.md) apply with extra force: no decoding, resampling at high quality,
   stretching, file writes, allocation or locks in `process()`. If you need any of these, it is a worker job.
5. **No clicks.** Every start, stop, cut, mute and switch gets a short fade. Add engine tests that check the first
   and last samples of a clip, a loop wrap and a stop for steps above a small threshold.
6. **Every edit is a Command**, including fades, gain drags, crossfades and every mixer control. A drag is one
   undo step. Test apply/revert symmetry for each.
7. **Don't name other DAWs** anywhere, including in comments about "how others do crossfades".
8. **Mockups are the agreed look** once the user approves them; ask before departing from them. Colours and
   sizes go in `Theme.hpp` (mixer strip widths, meter colours, the +6 dB amber zone).
9. **Each phase ships**: it builds, tests pass, `make install`, test in the hidden Rack on display :91 (never the
   user's screen), manual pages updated with screenshots, QA steps added, DECISIONS entries written, shortcuts
   in `Shortcuts.cpp` with tooltips and help overlay entries. Then ask the user to restart Rack and try it.
10. **Test with real files**: keep a small set of short, freely licensed test files in `tests/audio/` (a click
   track at known BPM, a sine sweep, a one-shot). Measure: a 1 kHz sine resampled 44.1→48 kHz stays clean
   (THD+N under −90 dB); a click track at 120 BPM detects as 120.0; a stretched click track keeps its clicks on
   the beat within 1 ms.
11. **Performance**: audio playback and the mixer for 64 busy tracks stay under 3% of one core at 48 kHz,
    measured like DECISIONS §31. Silent tracks cost nearly nothing.
12. **Manual**: new pages `docs/manual/17-audio.md` (import, tempo, editing, recording, bounce, export) and
    `18-mixer.md` (the mixer, IN/OUT, FX tracks, MAIN, levels); update `02-modules.md` (Track Out for every track, new jacks),
    `04-tracks.md` (new kinds), `15-shortcuts.md`, `14-files.md`.

---

## 11. Phases

Each phase is usable on its own.

**Phase 0: decide (no feature code)**
- The `TimeStretcher` interface with both adapters; the blind listening test picks the default engine.
- Decoder and libsamplerate builds on all platforms.
- Patch storage behaviour in Rack (save, duplicate, paste, undo).
- DECISIONS entries: one Track Out for every track, swappable stretcher, no layers on audio tracks, tick
  exception for Off clips, level standard (provisional), mixer pane, levels in track headers.

**Phase 1: audio tracks that play in time**
- Model (§4), schema bump, serialisation, commands with tests.
- Import (drop + menu), patch storage, decode, prepare, peaks, tempo detection, clip chip.
- Follow tempo (Stretch / Repitch / Off), render cache, varispeed reader.
- Timeline and launcher audio clips; fades, crossfades, gain, pitch, reverse, normalise; inspector.
- Audio editor (lower pane) without warp markers.
- Master, MAIN L/R, mixer pane with audio track strips and the master (no sends); volume and pan in the
  track headers of audio tracks and the master.
- Manual 17 (first half) and 18 (first half).

**Phase 2: the rack round trip**
- Track Out for every track kind (OUT, IN, live dimming); kit IN on Drum Out; return bus; strips and
  track-header levels for every track.
- FX tracks and sends; solo-safe; mixer automation lanes.

**Phase 3: record, bounce, export**
- Audio recording (timeline + launcher), take writer, record modes; recording offset and measurement.
- Bounce (clip, track, range) with tail; Export audio (master, stems).
- Audio files window; missing files; glue across files; consolidate.

**Phase 4: deeper**
- Warp markers, transients, Split at transients, Make triggers from audio.
- Takes and comping; group tracks; automatic delay compensation; DAWproject audio export; FLAC export.

---

## 12. The user's answers (2 Nov 2026)

1. **Time stretcher: swappable.** Both libraries sit behind one interface (§9.1).
2. **"FX track"** is the name.
3. **No layering on audio tracks; overlaps are automatic crossfades** (§6.3).
4. **A third pane** for the mixer, **and volume and pan in the track headers** too (§7.1, §7.5).
5. **Level: open.** The user doesn't know yet. Build with 5 V = full scale behind one constant (§7.4) and decide
   before release, by ear, with real effects modules.
6. **Yes, a thin Master row** at the bottom of the song pane.
7. **One Track Out instead of Audio Out** (the user's idea): Track Out binds to any track and dims what doesn't
   apply (§8.1).

Still open: which of the three track-header layouts in §7.5 (proposed: A).
