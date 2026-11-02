# Audio tracks and the mixer

Status: **proposal, revision 5 (2 Nov 2026). Not confirmed yet.** Mockups:
https://claude.ai/artifact/WNtGof3EbafDF7yptw5w5i (private; source HTML is not in the repo). Trello:
https://trello.com/c/RiahbPgS. Nothing here is built.

Revision 4 takes in everything from the product and UX review (`AUDIO_MIXER_REVIEW.md`), all as recommended by
the user. Revision 5 adds **sources** (§7.8): what happens when several Track Outs feed one track. The user's answers so far are in §12. When the build starts, each choice gets a DECISIONS.md entry and
the build follows the phases in §11.

**Nothing is implemented until the user confirms the whole design.**

This feature replaces the DESIGN.md §1 non-goal "No audio recording, no audio clips". It also changes DESIGN.md
§2 ("no external dependencies"): §9.1 lists the libraries it needs.

---

## 1. What musicians come to do

Design from these journeys. Each one must feel short and obvious, and must never go silent without saying why.

1. **Play a loop in time.** Drop a drum loop onto Tessera. It lands on a new audio track and plays at the song's
   tempo. In an empty song, the loop sets the song's tempo. Change the tempo: the loop follows and keeps its
   pitch.
2. **Hear the rack and the files together, at the right levels.** Patch each synth voice's output back into its
   Track Out. Every track gets a fader, a pan knob and a meter, in the mixer and in its track row. Patch MAIN
   L/R into the Audio module (or let Tessera do it). One place to balance the whole song.
3. **Share one reverb.** Add an FX track. Its Track Out sends the mix of everything sent to it into a reverb
   module and takes the reverb's output back in. Balance what goes in with "Show sends".
4. **Put a rack effect on one track.** Patch an audio track's Track Out OUT into a delay, and the delay's output
   back into the same module's IN. Like a hardware insert.
5. **Record.** Arm an audio track, patch a source into its IN, turn the metronome on, record. Stop, play, hear
   the take, with the cable still in. Record again.
6. **Bounce.** Turn a synth part into audio, to free the voice, to edit it as audio, or to keep a happy accident.
   One command records the track's sound in real time onto a new audio track.
7. **Edit audio.** Cut, trim, move, loop, fade, crossfade, reverse, change gain, tempo and pitch, without ever
   damaging the original file.
8. **Find the right loop.** Drop several files at once; swap the file on a clip with Ctrl+drop and keep its place,
   length and fades.
9. **Finish.** Export the song, or every track as its own file, to WAV. What the meters showed is what the file
   holds.

**The first minute** must work with one cable (MAIN to the Audio module) and no other setup. If that cable is
missing, Tessera says so where the musician is looking, and offers to make it.

---

## 2. The idea in one picture

Every track has a **sound**: what you hear from it in the mixer.

- **One Track Out for every track.** Instrument, drum, audio, FX tracks and the master all use the same Track Out
  module: the note jacks, plus OUT L/R and IN L/R. Jacks that do nothing for the bound track are drawn dim
  (§8.1). Drum Out stays as the per-lane drum console, with a stereo kit IN; Auto Out doesn't change.
- **OUT sends the track's own audio into the rack**, always before the fader: an audio track's clips, an FX
  track's sends, the master's mix. Instrument and drum tracks have no audio of their own, so their OUT is dim.
- **IN is how the rack's sound comes back.** Each patched IN is a **source** of its track. A track may have
  several (several Track Outs, or a Drum Out's kit IN plus a Track Out); they add up, each at its own level
  (§7.8). Each source's **job** comes from its own cables:

| Job | When | What it does |
|---|---|---|
| **Voice** | IN patched, on an instrument or drum track | Part of the track's sound. |
| **Effect loop** | IN patched on an audio track, and a cable path runs from any OUT of the same track to this IN (§7.8) | Its return replaces the dry clips. |
| **Return** | IN patched on an FX track or the master | Its return replaces the send mix (FX) or the mix (master insert). |
| **Live input** | IN patched on an audio track, with no cable path from the track's OUT | Heard only **while the track is armed** and "Monitor while armed" is on, so you hear what you record. |

| Track | Its sound in the mixer |
|---|---|
| Instrument / drum | Its voices. None: no sound, and the strip says how to fix it. |
| Audio | Its effect loops if it has any, else its clips. Plus its live inputs while armed. |
| FX | Its returns (the effect's return). None: **silent**, the strip says "No effect". |
| Master | Its returns (a master insert), else the mix. |

- **Recording captures the live inputs; bouncing captures the whole sound**, both before the fader and at each
  source's level. Bounces of instrument tracks take the voices.
- **The mixer then does the rest**: fader, pan, mute, solo, sends, into the master. The master leaves Tessera at
  **MAIN L/R** on the Tessera panel.

"Monitor while armed" is the existing track setting (manual 11), so no new switch appears. The cable stays the
setting: patching IN is how a track gets sound from the rack.

```
 notes  ─► Track Out ─► VCO ► VCF ► VCA ─► Track Out IN ─┐
 clips  ─► audio track ──(OUT ► delay ► IN, optional)────┤
                                                         ▼
                                    fader · pan · M/S · sends ─► FX tracks (OUT ► reverb ► IN)
                                                         │                  │
                                                         ▼                  ▼
                                                       MASTER ─► (insert) ─► MAIN L/R ─► Audio module
```

Nothing changes for people who don't want the mixer: their patches keep working, no cable is lost, and MAIN stays
silent until something reaches the master.

---

## 3. Feature list

Grouped by phase (§11).

### 3.1 Audio tracks and clips (P1a, P1b)
- New track kinds: **Audio** and **FX**. The **Master** always exists (one per song, §4.4).
- Import by dropping files (§6.5) or File ▸ Import audio… (Ctrl/Cmd+Shift+O). WAV, AIFF, FLAC, MP3. Several files
  at once. Ctrl+drop on a clip replaces its file.
- Imported files are **copied into the patch** (Rack's patch storage), so a saved patch is complete.
- Audio clips work in the launcher and on the timeline with the same gestures as note clips (§6.1).
- Waveforms in clip tiles (under the title bar, which stays) and launcher slots.
- **Tempo and speed done properly** (§5): Stretch (pitch kept), Repitch (like tape) or Off (own speed). A tempo
  badge on every audio clip; the first loop in an empty song sets the song tempo.
- Clip gain (drag the title bar up or down), pitch, reverse, normalise.
- Fades with bendable curves. Audio clips never layer: **where two clips overlap, they crossfade** (§6.3).
- Silent safety fades of 2 ms at every edge, so cuts never click.
- **The audio editor** in the lower pane (§6.4).

### 3.2 The mixer (P1a basic, P2 full)
- A **Mixer pane** under the notes pane, folded to a tab, with maximise like the other panes (§7). F4.
- One strip per track: name, source chip, sends, pan, M / S / arm, fader and meter.
- FX strips, then the Master strip, at the right; FX tracks are always the last tracks, so rows and strips share
  one order.
- **Show sends**: one click on an FX strip turns every fader into its send to that FX track. FX strips have an
  OUT meter for what goes into the effect module (§7.6).
- **Volume and pan in each track row** too (layout A, §7.5).
- Every mixer control can be automated with the same lanes as device knobs (DECISIONS §38).
- P1a: audio tracks and the master. P2: every track.

### 3.3 The rack round trip (P2)
- Track Out binds to any track and gains OUT L/R and IN L/R. Drum Out gains a stereo kit IN.
- MAIN L/R on the Tessera panel come in P1a, with the "Connect to Audio module" helper.
- FX tracks and sends; "Add a Track Out for this track" and "Show in the rack" helpers.

### 3.4 Recording, bouncing, export (P3)
- Record audio into armed audio tracks (timeline and launcher slots), with the existing record modes and count-in.
- **A metronome you can hear**, on MAIN only (§8.5).
- **Bounce** a clip, a track or a range to a new audio track, with a tail.
- **Export audio**: the master, or stems, in one real-time pass. It follows Plays.
- The Audio files window. (A plain "Remove unused audio" command comes in P1a.)
- Recording offset with a guided measurement.

### 3.5 Deeper editing (P4)
- Warp markers, transient markers, "Split at transients".
- "Make triggers from audio": a drum loop's hits become notes on a drum track, to drive rack drums.
- Takes and comping for audio recorded in Stack mode (kept inside the clip, not as layers).
- Groups (Ctrl+G, today a view only) get a strip that sums their tracks. No new track kind.
- Automatic delay compensation; DAWproject export of audio clips; FLAC export.

### 3.6 Not in scope
- Hosting plugins: the rack is the effects rack.
- Faster-than-real-time rendering: Rack runs in real time, so bounces and exports take as long as the music.
- Editing samples inside a file (pencil on the waveform). Every edit stays non-destructive.
- Per-lane audio returns for drum tracks.
- A per-clip gain curve: the Volume lane inside a clip does that job (DECISIONS §38 clip lanes).

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
    int channels = 2;         // 1 or 2 (more are mixed down on import, with a note)
    double sampleRate = 48000;
    int64_t frames = 0;
    double tempo = 0;         // what the file was played at: detected, from the file, or set; 0 = none
    float volts = 5;          // the voltage a full-scale sample plays at: kImportVolts for imports, 10 for
                              // recordings and bounces (so they play back at the level they were recorded)
};
// Song::audioSources, sorted by id.
```

- A source is **immutable**. Recording and gluing across files make new sources; nothing ever overwrites one.
- Sources are not deleted during a session (undo may need them). On patch load, files in storage that no source
  references are removed. "Remove unused audio" removes unreferenced sources on purpose (one undo step; the
  files stay until the next load).

### 4.2 Audio clips reuse `Clip`

Audio clips live in the existing `ClipPool`, so slots, timeline instances, linking, Make unique, loop and launch
work without new machinery.

```cpp
enum class ClipKind : uint8_t { Notes, Audio };
enum class TempoFollow : uint8_t { Stretch, Repitch, Off };      // §5
enum class SoundType : uint8_t { FullMix, Drums, Melody, Voice }; // picks the stretch settings, §5.3

struct AudioContent {
    AudioSourceId source;
    int64_t sourceStart = 0;   // the source frame at clip tick 0
    TempoFollow follow = TempoFollow::Stretch;
    SoundType sound = SoundType::FullMix;
    double sourceTempo = 120;  // the clip tempo: the one place speed is set (§5.1)
    float gainDb = 0;          // -inf..+24
    int8_t semitones = 0;      // -24..24
    float cents = 0;           // -50..50
    bool reverse = false;      // plays the clip's window of the source backwards
    // P4: std::vector<WarpMarker> warps; // {Tick beat; int64_t frame}
};

// Clip gains: ClipKind kind = Notes; std::optional<AudioContent> audio;   (audio clips have no notes)
```

- `Clip::length`, `loopStart`, `loopEnd`, `loopMode` keep their meaning, in ticks. **Clip Length (×2, ×½,
  custom, DECISIONS §47) always means how long the clip plays or loops, never its speed.**
- Gain, pitch and tempo are clip settings, shared by linked copies. Their controls say "all copies" in the
  tooltip when the clip has more than one use.
- A clip's kind must match its track. A drop of audio on a note track makes a new audio track instead (§6.5);
  other moves across kinds show the "not allowed" cursor with a short reason.

### 4.3 Timeline instances: fades and crossfades

`ArrangerClip` gains, for audio clips only:

```cpp
struct Fade { Tick length = 0; float curve = 0; };  // curve -1..1; 0 = linear for fades, equal power in a crossfade
Fade fadeIn, fadeOut;
```

- **A crossfade is not stored.** It is the overlap of two instances on an audio track. Over the overlap, the
  earlier clip's `fadeOut.curve` and the later clip's `fadeIn.curve` shape it; the overlap's length replaces their
  fade lengths there. Moving a clip away ends the crossfade, and its own fades come back.
- `layer` stays 0 on audio tracks (no sub-rows). The row moves of DECISIONS §91 never apply to audio clips.
- **Not used on audio instances:** `transpose` and `velocityScale` (hidden; pitch and gain live on the clip), and
  `stretch` (Alt+right edge edits the clip tempo instead, §6.1). They stay at their defaults.
- Launcher clips have no fades; they use the 2 ms safety fades and a short fade at each loop seam.

### 4.4 Track kinds, the master and the channel

```cpp
enum class TrackType : uint8_t { Instrument, Drum, Audio, Fx };

struct Send { TrackId fx; float levelDb = kSilentDb; bool preFader = false; };

struct Channel {               // one mixer strip
    float volumeDb = 0;        // kSilentDb..+6
    float pan = 0;             // -1..1
    std::vector<Send> sends;   // only sends that were turned up; FX tracks themselves have none
    int latencySamples = 0;    // extra delay this track's sound is known to have, §8.4
};
// Track gains: Channel channel;
// Song gains: MasterChannel master { TrackId id; float volumeDb; std::vector<AutomationEnvelope> automation; }
```

- **The master is not a `Track`.** Many paths assume a track has clips, voices and export. `Song::master` has its
  own `TrackId` (from `newId`), so Track Out binds to it like to any track, and mixer automation uses it. The song
  pane shows it as a thin last row, for its volume and its automation lanes.
- **FX tracks** are `Track`s (rows, automation, a Track Out) with no slots and no clips. They are **always the last
  tracks** in `Song::tracks`: adding one puts it at the end, and moves keep FX tracks after every other track. New
  FX tracks are named "FX 1", "FX 2"… until renamed; a small FX badge marks the kind everywhere, so the name stays
  the user's.
- Deleting an FX track removes every send to it in the same undo step.
- A channel's output is always the master until groups get strips (P4); no field for it yet.
- Schema version goes up by one. Old songs load with a master at 0 dB, every channel at 0 dB, centred, no sends.

### 4.5 Mute and solo

`Track::mute` / `solo` keep their meaning for notes, and now also cover sound:

- **Mute** silences the track's sound and **all its sends**, before and after the fader.
- **Solo** a normal track: other normal tracks go silent. FX tracks and the master stay on and carry only what the
  soloed tracks send. (The "solo-safe" idea is a tooltip on FX strips: "Stays on when you solo a track".)
- **Solo an FX track**: you hear only the effect. The tracks that send to it keep feeding it, but their own sound
  is silent.
- **Mute an FX track**: its return is silent, but its OUT keeps feeding the effect, so tails come back at once on
  unmute.

---

## 5. Speed, tempo and time stretching

The user's requirement: **proper speed and time-stretching algorithms on import.** This is the core of P1a.

### 5.1 The three ways a clip meets the tempo

| Follow tempo | When the song is faster than the clip's tempo | Use it for |
|---|---|---|
| **Stretch** (default for loops and longer material) | Same pitch, shorter. High-quality time stretching. | Drum loops, music, vocals |
| **Repitch** | Faster and higher, like speeding up tape. No stretch artefacts. | Breaks to pitch up, tape effects, modular wobble |
| **Off** (default for one-shots) | Plays at its own speed and pitch. | Hits, risers, speech, field recordings |

- **Speed is set in one place: the clip tempo.** The tempo badge, the inspector's Clip tempo, dragging a beat
  line in the audio editor and Alt+drag on the clip's right edge all change it. Nothing else changes speed.
- With Stretch, **Pitch** changes pitch without changing speed. With Repitch, Pitch changes the speed too (like a
  different note on tape), and the clip's length on the timeline follows.

### 5.2 Where the quality comes from

The audio thread never stretches, decodes or resamples at high quality. It plays prepared buffers.

1. **Import**: decode the file to float. Keep the original file untouched in patch storage.
2. **Prepare to the engine's sample rate** on a worker thread with libsamplerate (best sinc). This alone fixes
   "wrong speed" from a 44.1 kHz file in a 48 kHz patch. Redo it when Rack's sample rate changes.
3. **Render each clip that needs it**: when a clip's ratio (song tempo ÷ clip tempo), pitch, reverse or sound
   type is not neutral, a worker renders it with the time stretcher (§9.1) into a cached buffer, offline at the
   best quality settings. Linked copies share a render.
4. **Play**: the engine copies samples from the prepared or rendered buffer.

**While a render is not ready**, the clip plays the closest buffer it has through a real-time varispeed reader (a
16-tap polyphase windowed-sinc interpolator, not linear or cubic). You hear the change at once, a little
tape-like, and it turns into the clean stretch when the render is done, through a 10 ms crossfade. Renders start
at the playhead and go outwards, so the part about to play is ready first.

**Repitch** is the varispeed reader all the time (exact, not an approximation).

**Moving tempo, from any source** (the TEMPO knob dragged, modulated or MIDI-mapped, Tap, the external CLOCK, a
scene tempo): while the tempo keeps moving, Stretch clips play through the varispeed reader. A new render starts
after the tempo has been steady for 500 ms (for CLOCK: the smoothed tempo, more than 0.5% away from the last
render). The small difference left is taken up by the varispeed reader, capped at ±1%. The manual sells this as a
feature: **moving tempo sounds like tape.**

### 5.3 Sound type

One choice, in plain words, picks the stretcher's settings (transient handling, window size, formants, channels
together):

- **Drums**: keeps attacks sharp.
- **Melody**: smooth, for bass, keys, pads, single instruments.
- **Voice**: keeps the character of a voice when pitching (formants).
- **Full mix**: a balance, for finished music.

The tempo guess suggests Drums or Full mix; the user changes it in the badge menu or the inspector.

### 5.4 The tick model, and its one exception

- Stretch and Repitch clips are tick-based: their length is in ticks; the ratio comes from the song tempo.
- **Off clips** play at their own speed, so their timeline length depends on the tempo. They store their start in
  ticks and their length in source frames; the timeline works out the length in ticks from the current tempo
  when it draws, hit-tests and snaps. This is the one place the model needs the tempo; it is passed in
  (`TempoContext`), never stored.
- **Tempo never edits clips.** If a tempo change makes Off clips overlap, the overlaps crossfade like any other;
  where three would overlap, the latest start wins and the others are silent there (nothing is trimmed). One
  toast per tempo change that creates overlaps: "2 clips now overlap and crossfade".

### 5.5 The tempo guess, the badge, and the first loop

Steps 1 and 2 run **at drop time**, before the first sound, so loops play right from the start. Steps 3 and 4 run
on a worker afterwards (under a second for a loop).

1. Tempo written in the file (the tempo chunk loop libraries put in WAV files), or in the file name
   ("drums_92bpm.wav", "Loop 120 BPM.aif").
2. **Loop length**: a whole number of beats (1, 2, 4 … 64; 3, 6, 12 in 3/4 songs) at 70–180 BPM, nearest the song
   tempo.
3. **Onset analysis** for longer files: onset-strength autocorrelation over 60–200 BPM, gentle preference around
   120, downbeat at the strongest early onset.
4. **One-shot**: shorter than about 1.5 s with one decaying onset → Follow tempo Off.

**The badge.** Every audio clip shows a small dim **"92 ▾"** in its title bar, after the name (a lock icon instead
for Off clips). Its menu: the guess and its ×2 / ÷2 neighbours (hovering one draws its beat grid over the
waveform before you click), Type a tempo…, Tap along… (a small popover; T or Enter taps inside it), Follow tempo
▸, Sound type ▸.

**The just-dropped clip** shows the big form, "92 BPM · 2 bars · Stretch ▾", in its title bar until the next click
elsewhere. Never a dialog on import.

**The first loop sets the tempo.** When the song has no other clips, dropping a loop sets the song tempo to the
loop's tempo, in the same undo step as the drop. The big badge then reads "Song set to 92 BPM · Keep 120 BPM",
and "Keep 120 BPM" puts it back. Off clips (one-shots) never change the song tempo.

---

## 6. Editing audio

### 6.1 On the timeline

Same grammar as note clips (EDITOR_UX §6.3); new or changed parts in bold.

| Gesture | Result |
|---|---|
| Drag body / Ctrl+drag / Alt+Ctrl+drag | Move / linked copy / independent copy |
| Drag left or right edge | Trim (non-destructive; the hidden audio is still there). Hard stop at the end of the file, with a bright tick and the readout "End of file". |
| Drag right edge of a looping clip | Repeats the audio |
| **Alt+drag right edge** | **Changes the clip tempo** (all copies), readout "92 → 46 BPM". For Off clips it does nothing (the tooltip says why). |
| Alt+drag body | Slip the audio inside the clip, readout "Start in file 0:00.250" |
| **Drag the title bar up or down** | **Clip gain** (all copies), readout "Gain −3.0 dB", soft snap at 0 dB, applies to the selection. Sideways still moves. The waveform scales live. |
| **Drag a fade handle** (top corners of the body) | Fade length, readout "Fade in 1/32 · 81 ms" |
| **Drag a curve dot** (middle of a fade or of a crossfade's X) | Bend the curve, readout "Curve +40%" |
| **Drag one clip over another, or an edge into its neighbour** | The overlap is the crossfade; the X forms live while you drag, readout "Crossfade 1/8 · 163 ms" |
| **Drag the crossfade handle** (top of the overlap) | Grow or shrink the crossfade evenly around its centre |
| **Drop a clip inside another** | **Splits** the outer clip around it, with a crossfade at each seam |
| Ctrl/Cmd+E | Split at the playhead or mouse, nudged to the nearest zero crossing within 1 ms (the split line shows the nudge) |
| Ctrl/Cmd+J | Glue: one clip that plays the same; P3 renders a new file when the parts come from different files |

**Hit map** (decided against the code: `kEdgeHit` 6 px, the 14 px title strip, vertical body drag = move to
another track):
- Every handle and curve dot is drawn 7 px and hit at **12 px**.
- Edge zones give the **top 10 px of the body** to the fade handles. A zero-length fade handle sits 3 px inside
  the corner.
- In an overlap, the order is: crossfade handle and its curve dot → both clips' edges → bodies. Selection never
  beats an edge in an overlap.
- Handles and dots show on bodies at least **16 px tall** and clips at least **40 px wide**; otherwise the
  inspector has the values.
- Cursors: fade and crossfade handles ResizeH; curve dots and the title-bar gain drag ResizeV once the drag has
  picked its direction.
- Fades don't snap below one grid step; beyond it they snap to grid lines; Alt frees.

**Look.** Waveforms: min/max per pixel from a peak cache, mirrored, a lighter tone of the clip colour, scaled by
clip gain. Fades shade the waveform. The title bar shows: name, the tempo badge, ◀ when reversed, a dim gain
("−3.0 dB") when not 0 dB, and status marks (§6.6). Handles are small and dim, brighter on hover, never hidden.

### 6.2 Clip menu and inspector

**Clip menu**, audio section: Reverse · Normalise · Follow tempo ▸ · Sound type ▸ · Set tempo… · Replace file… ·
Bounce… (P3) · Show in Audio files (P3).

**Inspector** (audio clip selected): Name, Colour · **Gain** · **Pitch** (st, ct) · **Follow tempo** · **Sound
type** · **Clip tempo** (BPM, ×2 ÷2) · **Reverse** · **Fade in / Fade out** (length in ms first when under a beat,
curve) · Loop · Start offset · File (name, length, sample rate, mono/stereo, "6→2" when mixed down). When a side
is in a crossfade, its fade row reads "Crossfade with Amen 92 · fill · 1/8", and the curve stays editable.
Keep the inspector's "settings you can change" grammar (DECISIONS §55).

### 6.3 No layers, and overlaps crossfade

- **Audio tracks never layer**: one row per audio track, always. The song-wide Stack item in the tool menu
  (DECISIONS §32, §52) stays; its tooltip gains "Audio tracks never layer: overlaps crossfade".
- **The overlap is the crossfade.** Equal power by default. Change it by dragging an edge, the handle on top, or
  the X's curve dot.
- **Clips that only touch** get the 2 ms safety fades and no crossfade.
- **At most two clips sound at once.** A drop inside another clip splits it (§6.1). A move that would make three
  clips overlap trims the earliest one back, in the same undo step, and the trim shows live while dragging.
- **Pasting, recording in Replace or Merge, and range edits** follow the same rules. Recording in Stack mode keeps
  takes inside the clip (P4 comping).
- The crossfade can only reach as far as the clips have audio.

### 6.4 The audio editor (lower pane)

Opens when an audio clip is selected, where the piano roll would be. With several audio clips selected, it shows
the last clicked one; the clip chip's menu lists the others. A mixed selection shows the kind of the last clicked
clip.

- **The x axis is file time.** The whole file runs across the pane, the clip's window bright, the rest dim. A
  looping clip's repeats show as dim copies after the window. Dragging the window's body slips (the readout says
  "Start in file"); its edges trim. The song ruler above moves as you slip.
- **Two rulers**: the song's bars, and the clip's beats. Beat 1 has a "1" flag: drag it to move where beat 1 is.
  Other beat lines take a drag within 4 px of the line and set the clip tempo (the grid stretches from beat 1).
  Between lines, a drag does nothing.
- **Click in the ruler** to play from there (with the existing "hear while editing" speaker).
- Fades, curve dots and the title-bar gain behave as on the timeline.
- Tool switch: **Select | Slice** (C). No Draw: there is nothing to draw on audio until warp markers (P4).
- Stereo as two halves; mono as one.
- P4: warp markers on the beat grid, transient marks.

### 6.5 Dropping files

GLFW reports only the drop itself (no hover), so there is no live preview; the feedback comes right after.

| Dropped on | Result |
|---|---|
| The Tessera panel, or empty space in the song pane or mixer | One new audio track per file. Where the clip goes follows **what plays**: Plays = Timeline → the timeline, at the drop bar (bar 1 from the panel); Plays = Clips → the new track's first slot. |
| An audio track (timeline) | At the drop point, snapped to the beat. Several files go end to end. |
| A launcher slot of an audio track | Into that slot. Several files fill the slots below. |
| A note track or FX row | A new audio track just below it, toast "Added an audio track: Lead plays notes". |
| An audio clip, **with Ctrl/Cmd held** | Replaces its file; keeps place, length, fades and gain. |
| A .mid file | MIDI import (DECISIONS §60). |
| Anything else | Nothing is created; toast "Couldn't read 'x'. Tessera reads WAV, AIFF, FLAC and MP3." |

After a drop: select the new clip(s), scroll them into view, toast "Imported drums_92bpm.wav · Undo". A drop of
N files is one undo step. Never refuse a drop that holds audio.

### 6.6 Clip states

| State | What the clip shows | What plays |
|---|---|---|
| Decoding | "Loading" in the title; the waveform fills in left to right as real progress | Silence until decoded, then at once |
| Rendering | A spinner (a still dots mark with reduced motion), "40%" when wide enough; tooltip "Playing a quick version until the clean one is ready" | The varispeed version |
| Render failed | Amber "!"; tooltip "Couldn't make the stretched version; playing a simpler one"; clip menu "Try again" | The varispeed version |
| Missing file | Hatched; "Missing file"; clip menu "Find…" | Silence |
| Mixed down | "6→2" badge | The stereo mix |
| Recording stopped early | Title "Recording stopped: disk full" (or "writer fell behind") | The take up to that point |

The same marks show on launcher slots.

---

## 7. The mixer pane

### 7.1 Where it lives

- A **third pane under the notes pane**, folded by default to a tab (22 px; 16 px in the in-panel editor). The tab
  is also the splitter: click opens it at its last height, drag sets a height, double-click maximises.
- The folded tab is never dead: it shows a 3 px live meter per track in its colour, the master's peak as
  "peak −6.2", and, when MAIN has no cable, "MAIN not patched" in amber.
- Its top-right corner has **maximise / restore**; **F4** does the same (F2, F3 are taken; F4 needs a `keyName()`
  case). Tab cycles the three panes; Shift+Tab maximises the focused one. The mixer gets the focus ring.
- In the in-panel editor it starts folded; maximised it fills the panel editor (about 330 px tall).

### 7.2 A strip, top to bottom

1. **Colour bar and name**, with the FX badge on FX tracks. A click selects the track (same selection as the
   song pane). Right-click: Rename, Colour, Latency…, Delete track.
2. **Source chip**, in words: "IN · Track Out", "IN · Drum Out", "2 sources ▾" (§7.8), "Clips",
   "Clips · IN when armed", "IN · effect loop ⟲", "No effect" (FX, dim), "Not patched" (dim, tooltip "Patch your
   voice's output into this track's Track Out IN"), "Missing file", "Track Out bypassed", "Bad signal on IN".
   Its menu: **Add a Track Out for this track** (P2) and **Show in the rack** (P2).
3. **Sends**: one small knob per FX track, labelled with two letters of its name in its colour ("Re", "De"). At
   −∞ they are drawn small and dim, never hidden. A send before the fader shows "pre" under its knob. Many FX
   tracks: two knobs per row, and the rows grow. FX strips have a **Show sends** button here instead (§7.6).
4. **Pan** knob.
5. **M · S · ●**: the same switches as the track rows, same state. Arm shows on every strip except FX and master.
6. **Fader and meter**: fader, then a two-channel meter (peak bar, peak-hold tick, a 0 dB line). The dB readout
   under it; double-click the readout to type.
7. A dim **"→ Master"** under it, as text (it becomes a choice when groups get strips, P4).

Widths: 64 px strips, 44 px narrow; the Master 88 px, pinned at the right, with a "MAIN not patched" note when
MAIN has no cable and a loudness readout (short-term) for finishing. FX strips are tinted, after a gap.

### 7.3 Gestures

- Same vocabulary as everywhere (EDITOR_UX §2). Drag is relative (a click never jumps a fader). **Shift or Ctrl**
  for fine (Rack users fine-tune with Ctrl). Double-click resets (0 dB, centre, −∞ for sends). Faders softly snap
  to 0 dB (a 6 px detent; the 0 dB mark brightens); Alt skips the snap. The wheel nudges by 0.5 dB.
- Dragging a fader of a selected strip moves every selected strip by the same dB; every moving cap lights, and the
  readout reads "3 tracks · −2.0 dB". Strips at −∞ stay at −∞.
- A drag is one undo step, committed on mouse-up.
- **Right-click** on a fader, level bar, pan or send: Automate in the timeline · Type a value… · Reset. Sends add
  Before the fader / After the fader.
- **Automated controls** draw a ring in the lane colour. While held, the control overrides the lane and returns to
  it over 50 ms on release. It writes into the lane only while the track is armed (DECISIONS §73–74). Stopped, it
  moves freely, as in §38.
- **Keys, mixer scope** (scoped actions win over the global ones in `findAction`): Up/Down ±0.5 dB on the selected
  strips (Shift ±0.1 dB); Left/Right move the selection between strips; M and Alt+S work as everywhere; Delete
  does nothing over the mixer.
- The panel editor's Alt hand-off (DECISIONS §34) covers the mixer pane.

### 7.4 Levels: one meaning of 0 dB

- **0 dB is the sound card's full scale: 10 V on MAIN**, which is what Rack's Audio module sends at full volume.
  Meters, the master, MAIN and exported files all use it, so a meter that never goes red means an export that
  never clips.
- **Import level** is the one constant left open: `kImportVolts`, provisionally **5 V**. A full-scale loop comes
  in at 5 V and reads **−6 dB**, level with rack oscillators, which is what rack effects expect. Decide it by ear
  before release; nothing else depends on it.
- Recordings and bounces store what reached IN as V / 10 (32-bit float, so nothing over 10 V is lost), and play
  back at 10 V per full scale (`AudioSource::volts`).
- **Meters**: −60 to +6 dB, a 0 dB line, the top above 0 dB red. Peak hold 1.5 s, fall 20 dB/s. Fader taper puts
  0 dB at 75% of travel.
- **One clip light, on the Master**, showing the peak value ("+2.3"); a click clears it. Strip meters just show
  red above 0 dB.

### 7.5 Levels in track headers (layout A, agreed)

Today's rows have one line (DECISIONS §51): colour tile with the number, name, A / ● / M / S, lanes arrow. Layout A
adds a **second line**: a level bar, its dB value and a pan knob.

- **The level bar** is the fader and the meter in one: the groove shows the live level in the **track colour at low
  strength**, bright only in the top 6 dB, so a busy song doesn't flicker in every row. The fader position is a
  thin bright line.
- **Gestures** as in the mixer, with relative drag. A click with no movement selects the track (the existing
  click-or-drag pattern), so the bar never jumps. Readout pill while dragging.
- **Short rows** (under 40 px): the bar becomes a 3 px strip just **above** the 4 px resize edge, with a 6 px hit
  area. Where the two meet, the first 4 px of movement decide: sideways = level, up/down = resize (as the title
  strip does). Double-click resets the level only in the bar's own zone, so double-click-to-compact on the edge
  keeps working.
- **In-panel editor** (about 150 px headers): names stay first (DECISIONS §51). The dB text goes first, then the
  pan knob; the bar stays, at least 40 px.
- The pan knob has a 16 px hit area.
- **Automated**: a ring in the lane colour; behaviour as in §7.3.
- **Nothing on IN** (instrument or drum track): the groove is hatched, tooltip "No audio: patch your voice's
  output into Track Out IN". The bar still sets the level.
- **FX rows** have the same controls and the FX badge. The **Master row** (thin, last) shows only its level bar
  and dB.
- Colours of the groove, the hatching and the red zone go in `Theme.hpp`, shared with the mixer. Check them in all
  eight colour schemes (DECISIONS §43).

### 7.6 Setting what goes into an FX track

Four places, one value, the same undo:

1. **The send knob** on each track's strip.
2. **Show sends** on an FX strip, to balance everything that goes into it (below).
3. **The inspector's Sends section** for the selected track (below).
4. **Automation lanes**, like device knobs.

**Show sends.** Click an FX strip's **Show sends** button (it then reads "Showing sends"). Every other strip's
fader, readout and meter show that track's send to this FX track instead of its volume:
- **Three signals, so colour is never the only one**: fader caps in the FX track's colour and drawn hollow; the
  readout reads "send −15.5"; a band at the top of the pane: "Faders show sends to Reverb · Esc to go back".
- Entering, the faders glide to their send positions over 120 ms; leaving, they glide back.
- The chosen FX strip stays lit; the master and the other FX strips dim.
- A send before the fader shows "pre" above its fader; clicking it switches to after the fader (one undo step).
- Mute, solo, pan and arm work as usual. Send knobs are hidden in this mode (they'd show the same values).
- Leave by clicking the button again, the band's ×, or Esc (§7.7). Another FX strip's button switches to it.
  Folding the mixer leaves the mode; maximise and restore keep it. Not saved with the song. Track headers keep
  showing volume.
- Clicking an FX strip's **name** selects the track, as on every strip; only the button enters the mode.

**Two meters on FX strips.** The normal meter shows the FX track's sound (what comes back on IN). A thinner
**OUT** meter on its left shows the sends' mix as it leaves on OUT, before the FX fader, with the same scale. A
reverb fed too hot shows red there before you hear it distort. When OUT isn't patched, the OUT meter is drawn dim
(never hidden).

**Inspector Sends section.** With a track selected, the inspector lists every FX track by name and colour, each
with a level bar and "Before the fader / After the fader". FX tracks with no send yet are listed at −∞, dim. With
no FX tracks: "No FX tracks · + Add FX track".

### 7.7 Esc, in one order

Whatever pane is under the mouse: **cancel a drag → leave Show sends → clear the selection → close the editor**.
One press does one step; an Esc that left Show sends never also closes the editor. Esc never cancels a bounce or
an export: their progress strip has a **Cancel** button, and Stop also cancels.

### 7.8 Several sources on one track (rev 5)

Several Track Outs may play one track (DESIGN §6: "one next to the VCO, one next to the filter"). Revision 4 only
said "summed", which broke real patches: a Track Out duplicated with its cables played the voice twice as loud; a
mic for recording and a delay loop on the same audio track had no defined meaning; a loop from one Track Out's OUT
into another's IN looked like a live input. Sources fix this.

**What the musician sees.** Bass plays a synth through Track Out 1. They add Track Out 2 on Bass and patch a
second synth into its IN. Bass now has two sources, and the strip's chip reads "2 sources ▾". Clicking it opens:

```
 Bass · sources
 ☑  Track Out 1   VOICE   ▮▮▮▮▯   ───●───   0.0 dB   ⌖
 ☑  Track Out 2   VOICE   ▮▮▯▯▯   ──●────  −6.0 dB   ⌖
```

- **Use** (the check box) takes a source out of the sound without pulling a cable.
- **Level** (−∞ to +6 dB, default 0 dB) balances layers. Same gestures as a fader (§7.3): relative drag, Shift
  or Ctrl for fine, double-click resets to 0 dB, one undo step per drag. Not automatable for now.
- A small live meter, the job, and **⌖ Show in the rack** (P2, §8.2).
- Names: "Track Out 1, 2…" numbered by place in the rack (left to right, then top to bottom), "Drum Out" for a
  kit IN.

**Levels show only when they are needed** (the user's choice, 2 Nov). With one source, in use, at 0 dB, nothing
extra appears and the chip reads as before ("IN · Track Out"); the track fader does the job. The list and the
"N sources ▾" chip appear when a track has two or more sources, **or** when a source is off or not at 0 dB, so a
level never hides once it changes the sound. The same Level and Use also sit in the Track Out's right-click menu
(a slider), for people working in the rack. They are one value shown in two places, like M and S in rows and
strips.

**Jobs come from the cables** (table in §2). Tessera works them out on the UI thread whenever cables change,
never on the audio thread. "A cable path from the track's OUT" means: follow cables from module to module,
starting at any OUT of any Tessera output module bound to that track, and see if one reaches this IN. It doesn't
look inside modules; if a path exists, the clips can reach that IN. This also fixes a single-module case: OUT to
a scope only, and a mic on IN, is a live input, not an effect loop.

The Track Out display shows the job: **VOICE**, **LOOP**, **LIVE IN**, **RETURN**, or **IN OFF** when Use is off.

**Rules:**
- **The same signal never counts twice.** One output port cabled into two INs of the same track counts once.
  The second row reads "Same cable as Track Out 1 · counted once", with no level.
- Turning every source off is allowed: the chip reads "No source in use" (dim), the track is silent.
- A Drum Out's kit IN and a Track Out on the same drum track are two sources, the same way.
- Removing a Track Out removes its source and level with it; undoing the removal brings both back.
- Mute and solo stay per track. There is no solo per source.
- Sources reach Tessera a few samples apart when their cable paths differ in length. Per-track latency (§8.6)
  covers the track, not each source.

**Saved in the Track Out**, not in the song: Level and Use are that module's patch data, changed as a Rack undo
step. They follow the module when it is moved or duplicated, and a deleted module takes them along.

**Still open:**
1. **A new second source**: both play at once, with a toast "Bass has 2 sources now. Both play. · Use only the
   new one · Show sources" (recommended); or the new source starts with Use off.
2. **"Follow the selected track"**: its IN joins the selected track as a normal source (recommended, it is the
   audition use); or it plays only on tracks with no source of their own.

---

## 8. The rack side

### 8.1 Modules

| Module | Change |
|---|---|
| **Tessera** | MAIN L / MAIN R outputs with a small level meter. The panel has no free row for them today, so the panel gets a new layout, shown as a mockup for approval before building (P1a). Tooltip on MAIN: "Master after its fader: −6.2 dB peak". An amber dot beside MAIN while the master has signal but MAIN has no cable. |
| **Track Out** | Binds to **any** track and to the master. Gains OUT L/R and IN L/R (R follows L when unpatched), and an **IN level** light: green while audio comes back, red above 0 dB, off when nothing arrives. The display shows its IN's job (VOICE, LOOP, LIVE IN, RETURN, IN OFF, §7.8); the right-click menu has its source Level and Use. Stays 6 HP: the display gets shorter and the jacks are laid out again. |
| **Drum Out** | A stereo kit IN (L/R) under the lane rows. |
| **Auto Out** | No change. |

**One module, dim where it does nothing.** Track Out draws the labels and rings of jacks that do nothing for the
bound track at about 35% strength; their tooltips say why. **A cable on a dim jack** gets an amber ring and the
tooltip "Nothing comes out here for an audio track".

| Bound to | Note jacks (V/OCT … PRESS) | AUTO | OUT L/R | IN L/R |
|---|---|---|---|---|
| Instrument / drum track | live | live | dim | live (the voice) |
| Audio track | dim | live | the clips | live (effect return, or what you record) |
| FX track | dim | live | the sends' mix | live (the effect's return) |
| Master | dim | live | the master mix | live (master insert) |

- New jacks are appended to the port enums, so saved patches keep their cables (cables follow port IDs).
- OUT is always before the fader. There is no "after the fader" option: it would apply the fader twice in an
  effect loop. Stem export covers direct outputs.
- **Default binding on add**: the **selected** track if it has no Track Out; otherwise the first instrument or
  drum track without one. Never an audio, FX or master track unless it is selected.
- The track picker lists every kind, the master last.
- A polyphonic cable on IN is summed to stereo (the jack tooltip says so). In "Follow the selected track" mode, IN
  goes to the selected track too; the manual notes that the sound moves strips.
- **Display states**: "Track deleted" (amber; IN ignored; light amber), "Tessera bypassed", "No source". No silent
  rebind (CLAUDE.md).
- **Bypassing Track Out** must **not** route IN to OUT (in an effect loop that makes feedback). Its return simply
  drops out; the strip chip says "Track Out bypassed".
- Panel art is drawn by Tessera's own panel code, so dimming follows the binding live.

### 8.2 Setup helpers (agreed)

Each is one Rack undo step:
- **Connect to Audio module** (P1a): offered in the "MAIN not patched" toast and the Master strip's note when an
  Audio module is in the patch. Cables MAIN L/R to its first two inputs if both are free; otherwise it says
  "The Audio module's inputs 1 and 2 are in use" and changes nothing.
- **Add a Track Out for this track** (P2): in the source chip menu. Places a Track Out beside Tessera (to its right,
  in the first free space), bound to that track.
- **Show in the rack** (P2): scrolls Rack's view to that track's Track Out and flashes its display.

### 8.3 Buses and threading

- **Tessera → Track Out**: extend the existing `OutputBus` (same pointer, same lifetime handling) with a
  parity-double-buffered audio array, one stereo pair per track slot plus one for the master, written every
  sample. Keep `TrackFrame` as it is (rewritten only when voices change, DECISIONS §31).
- **Modules → Tessera (the return bus)**: each output module gets **its own return slot**, handed out on the UI
  thread when its binding resolves (several modules may share a track, so they never write one slot). The module
  writes `{sample, trackId, patched, use, levelGain, L, R}` into buffer `n & 1`; Tessera reads buffer `(n − 1) & 1` and uses a slot
  only when its stamp is `n − 1` and its trackId matches. A missing, bypassed or removed module drops out. NaN or
  infinity counts as silence and sets the "Bad signal on IN" flag.
- Copy the OutputBus tests for the return bus: shuffled module order, several threads, module removed mid-stream,
  two Tesseras.

### 8.4 What Tessera's `process()` does each sample

1. The sequencer, as today.
2. Audio clip playback (prepared or rendered buffers; varispeed reader when needed).
3. Read the return bus.
4. Each track's sound, by the tables in §2: sum its sources by job, each smoothed to its level, skipping sources
   not in use and duplicates of one output port (§7.8). Jobs arrive from the UI thread in the parameter block.
5. Recording taps (§8.6).
6. Each strip: gain (smoothed), pan (−3 dB centre, constant power; balance for stereo), mute/solo (§4.5), sends.
7. FX tracks: OUT = the sum of what was sent to them this sample; sound = IN, or silence when IN is unpatched.
8. Master: the sum, or its IN when patched; the master fader → MAIN; the metronome is added to MAIN only.
9. Write the audio array for Track Out. Meters every 32 samples.

Mixer values live in an atomic parameter block owned by the module, refreshed from the song after every commit and
written directly during a drag. The audio thread reads it every 32 samples and smooths per sample. A drag never
rebuilds the snapshot.

**No clicks, ever** (the audio version of "never leave a gate high"): stop, loop wrap, mute, solo, a clip starting
or ending, a render switch, a snapshot swap and a bypass all use short fades (2–10 ms). Tessera bypassed: Track
Outs fade out over 5 ms.

### 8.5 Metronome (agreed)

Record ▾ gains **Metronome: Off / Count-in only / While recording / Always**, with a level. A soft click (accent
on the bar) plays on MAIN only: never in Track Out OUT, bounces, exports or stems. The CLICK output jack keeps its
trigger, unchanged.

### 8.6 Latency, recording, bouncing, export

- **Latency.** Known parts: a note reaches an output module one sample late; audio reaches Track Out's OUT one
  sample late; a return reaches Tessera one sample late; every Rack cable adds one. Tessera subtracts its own
  known part from recordings and bounces. What it can't know: per-track `latencySamples` (strip menu) and a global
  **Recording offset** with a guided measurement (patch CLICK through the sound card and back into an armed audio
  track; Tessera plays a click and measures). P4: automatic delay compensation.
- **Recording.** Arming allocates a lock-free ring buffer per armed audio track (about 5 s). The audio thread only
  pushes. A **dedicated writer thread** (never the UI thread: Rack's file dialogs block it) writes a 32-bit float
  WAV in patch storage and updates the live waveform's peaks. On stop, one `RecordAudioTake` command adds the
  source and its clip(s). A full disk or a writer that falls behind stops the recording and keeps the take
  (§6.6). A sample-rate change during recording stops it and keeps the take.
- **Bounce** (clip, track or range menu; Ctrl/Cmd+B). A small dialog: Range, Tail ("Until silent", max 8 bars, or
  a fixed length), "Mute the original afterwards" (on). It always records the track's sound at IN, before the
  fader. It plays from one bar before (so delays and reverbs are sounding), records, and keeps going for the
  tail. Result: a new audio track under the source, "Bass (bounce)". Progress in the transport: "Bouncing Bass ·
  bar 3 of 8" with a **Cancel** button; Stop also cancels; cancel leaves nothing behind. An estimate shows in the
  dialog ("about 21 seconds").
- **Export audio** (File menu; Ctrl/Cmd+Alt+E): the master, or the master plus every track's sound after its
  fader (stems), in one pass. WAV 24-bit (dithered) or 32-bit float. **It follows Plays**: with Timeline, the
  song's range (or the loop); with Clips, it records the master from now until you press Stop, so a live clip
  performance becomes a file. During stem export, mute and solo are locked (tooltip says why); stems of muted
  tracks are skipped and listed in the result toast. Editing is allowed and is heard in the result.

### 8.7 Files, patch storage and size

- `createPatchStorageDirectory()` / `getPatchStorageDirectory()` hold the `audio/` folder; Rack packs it into the
  .vcv patch. A .tessera project file carries `audio/` too.
- **Verify in Phase 0, in the hidden Rack**: save and reload; duplicate; copy/paste; delete and undo; two
  Tesseras. Where Rack doesn't copy storage, find the files by `contentHash` in the source module's storage and
  copy them; otherwise "Missing file".
- **Missing files** on load: hatched clips, the strip chip "Missing file", and one toast "2 audio files are
  missing · Find…".
- **Memory and size.** Decoded audio lives in RAM as float (a 4-minute stereo file at 48 kHz is about 92 MB, plus
  a render when stretched). Before decoding, estimate: above about 20 minutes for one file, or 1 GB in total, ask
  once ("This file is 58 minutes and needs about 1.3 GB of memory. Import anyway?"). A failed allocation says
  "Not enough memory to load 'x'". The Master strip's tooltip shows the audio's size on disk and in memory from
  P1a; above about 300 MB on disk, warn once that saving will be slow. "Remove unused audio" is a File menu
  command from P1a. Smaller storage for takes (FLAC needs an encoder, which Rack doesn't ship) is a later choice.
- **Sample-rate change**: a strip in the editor, "Preparing audio for 96 kHz · 12 of 40"; clips keep playing
  through the varispeed reader meanwhile.

---

## 9. Code layout and libraries

### 9.1 Libraries

| Need | Choice | Licence | Notes |
|---|---|---|---|
| Decode WAV, FLAC, MP3 | dr_wav, dr_flac, dr_mp3 (single headers) | public domain / MIT-0 | AIFF via dr_wav; verify, else a small reader in `model/` |
| Resample | libsamplerate | BSD-2 | Ships with the Rack SDK (`samplerate.h`); verify the symbols are exported on all three platforms |
| Time stretch | **swappable**: Signalsmith Stretch and Rubber Band (v4, R3 "finer") | MIT / GPL-2.0-or-later | |
| FFT (analysis, Signalsmith) | pffft | BSD | Ships with the Rack SDK |

**The time stretcher is swappable (agreed).** Signalsmith Stretch (MIT) and Rubber Band (GPL) sit behind one
interface.

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
    virtual const char* id() const = 0;   // goes into the render cache key
    /// Renders the whole job into out (outFrames = inFrames * ratio). Calls progress() now and then; returns
    /// false when cancelled.
    virtual bool render(const StretchJob& job, float* const* out, int64_t outFrames,
                        const std::function<bool(double)>& progress) = 0;
};
std::unique_ptr<TimeStretcher> makeStretcher(const std::string& id);   // falls back to the default one
std::vector<std::string> availableStretchers();
```

- One adapter per library in `src/audio/stretch/`, each library vendored under `dep/<name>/` with its licence
  file. Nothing outside the adapter includes a library header.
- The Makefile picks the adapters (`STRETCHERS ?= signalsmith rubberband`). A release can leave Rubber Band out with
  no code change.
- With more than one built, Settings ▸ Audio ▸ "Time stretch engine" picks one; changing it re-renders in the
  background. The song never stores the engine.
- The varispeed reader is Tessera's own code, not part of the interface.
- A conformance test runs every adapter through the same checks: output length; a stretched click track keeps its
  clicks on the beat within 1 ms; a stretched sine keeps its pitch within 1 cent; cancel works.

**Phase 0 blind listening test**, run just before implementation starts (the user's choice). Ask the user for their
own test files first. 8 kinds of material (drum loop, break, bass, vocal, piano, pad, full mix, speech) at 0.75×,
0.9×, 1.1×, 1.33× and ±3 semitones, rendered by both, on a private listening page: A and B per row, shuffled and
hidden, the user picks, then Reveal shows which library won and the render times. Built as a throwaway tool
outside Tessera. Plus builds on macOS, Windows and Linux. The test picks the default engine.

### 9.2 Where code goes

```
src/model/AudioSource.hpp        AudioSource, AudioContent, Fade, Channel (pure data)
src/model/AudioEdits.*           commands: import, replace file, fades, gain, reverse, tempo, follow, pitch, split-on-drop
src/model/MixerEdits.*           commands: volume, pan, send, latency
src/model/AudioAnalysis.*        peaks, tempo guess, onset curve, zero crossings (no Rack, unit tested)
src/model/WavFile.*              WAV writer (float / 24-bit) and reader for tests
src/engine/AudioPlayer.*         clip playback, varispeed reader, fades, crossfades
src/engine/Mixer.*               sound rules, strips, sends, mute/solo, master, metronome, meters (no Rack, tested)
src/engine/ReturnBus.hpp         return slots (no Rack, unit tested)
src/engine/AudioRecorder.*       ring buffers for takes
src/audio/AudioWorker.*          decode, prepare, render, peaks jobs (worker threads, Rack allowed)
src/audio/RenderCache.*          renders by (source, ratio, pitch, sound, reverse, rate, stretcher id)
src/audio/TimeStretcher.hpp      the swappable interface
src/audio/stretch/*              one adapter per library
src/audio/TakeWriter.*           the writer thread
dep/<library>/                   vendored libraries with their licence files
src/module/TrackOutModule.*      OUT/IN, any track kind, live dimming, IN light
src/ui/views/AudioClipDraw.*     waveform tiles, handles, badges, states
src/ui/views/AudioEditor.*       lower pane for audio clips
src/ui/views/MixerPane.*         the mixer
src/ui/editing/AudioActions.*    import, drop, replace, bounce, export, setup helpers
```

`src/model/` and `src/engine/` still build without Rack. The snapshot holds `shared_ptr`s to prepared buffers, so a
buffer is only freed on the UI thread when its snapshot retires (DESIGN.md §5).

### 9.3 Keys

Checked against `Shortcuts.cpp`:

| Action | Key |
|---|---|
| Maximise / restore the mixer | F4 (add the `keyName()` case) |
| Add audio track | Ctrl/Cmd+Alt+T |
| Add FX track | the mixer's "+ FX track", and the Add track menu |
| Import audio… | Ctrl/Cmd+Shift+O |
| Bounce… | Ctrl/Cmd+B |
| Export audio… | Ctrl/Cmd+Alt+E |
| Tap tempo | T or Enter, inside the "Tap along" popover only (plain T is the Mute tool) |
| Mixer: volume / move between strips | Up/Down (Shift fine), Left/Right, mixer scope only |

---

## 10. Instructions for the implementer

1. **Read first**: this file, `AUDIO_MIXER_REVIEW.md`, CLAUDE.md, DESIGN.md §5–7, DECISIONS §2–3 (the bus), §31
   (performance), §32, §34, §38, §42, §43, §47, §51–55, §60, §69, §73–74, §91, EDITOR_UX §2 and §6.3.
2. **Nothing is implemented until the user confirms the whole design.** Then Phase 0 comes first and decides
   things: no feature code before the stretcher interface, the default engine, the patch-storage behaviour and the
   Tessera panel layout are settled and written into DECISIONS.md.
3. **Merge through a pull request**, as every branch does.
4. **Audio thread rules** (CLAUDE.md) apply with extra force: no decoding, high-quality resampling, stretching,
   file writes, allocation or locks in `process()`.
5. **No clicks.** Engine tests check the first and last samples of a clip, a loop wrap, a stop, a mute and a
   render switch for steps above a small threshold.
6. **Every edit is a Command**, including fades, gain drags, crossfades, split-on-drop, file replace and every mixer
   control. A drag is one undo step. Test apply/revert symmetry for each. Setup helpers are Rack undo steps.
7. **Don't name other DAWs** anywhere.
8. **Mockups are the agreed look** once approved; ask before departing from them. Colours and sizes go in
   `Theme.hpp`.
9. **Every state in §6.6, §8.1 and §8.7 is built and tested**, not left for later.
10. **Each phase ships**: builds, tests pass, `make install`, test in the hidden Rack on display :91 (never the
    user's screen), manual pages with screenshots, QA steps, DECISIONS entries, shortcuts in `Shortcuts.cpp` with
    tooltips and help overlay entries. Then ask the user to restart Rack and try it. Update the Trello card.
11. **Test with real files** in `tests/audio/` (short, freely licensed: a click track at a known BPM, a sine sweep, a
    one-shot). A 1 kHz sine resampled 44.1→48 kHz stays clean (THD+N under −90 dB); a 120 BPM click track guesses
    120.0; a stretched click track keeps its clicks on the beat within 1 ms.
12. **Performance**: audio playback and the mixer for 64 busy tracks stay under 3% of one core at 48 kHz, measured
    like DECISIONS §31. Silent tracks cost nearly nothing.
13. **Manual**: new pages `17-audio.md` and `18-mixer.md`; update `02-modules.md`, `04-tracks.md`,
    `11-recording.md` (monitoring, metronome), `14-files.md`, `15-shortcuts.md`. Fix EDITOR_UX §2's stale line
    "Alt+drag body = primary value" while there.

---

## 11. Phases

Each phase is usable on its own.

**Phase 0: decide (no feature code)**
- `TimeStretcher` interface with both adapters; the blind listening test picks the default.
- Decoder and libsamplerate builds on all platforms; patch storage behaviour in Rack.
- The Tessera panel layout with MAIN (mockup, user approval).
- DECISIONS entries for every agreed choice.

**Phase 1a: a loop in time**
- Model, schema bump, serialisation, commands with tests.
- Drop and import (all rules in §6.5), patch storage, decode, prepare, peaks.
- Tempo guess, the badge, the first loop sets the tempo; Stretch and Off; render cache; varispeed reader; moving
  tempo sounds like tape.
- Timeline and launcher audio clips: move, copy, trim, loop, split, gain on the title bar, fades.
- Clip states (§6.6).
- Master, MAIN L/R, the Connect to Audio module helper, the MAIN warnings.
- Mixer pane with audio track strips and the master; levels in the track headers of audio tracks and the master.
- Remove unused audio; size warnings.
- Manual 17 and 18, first parts.

**Phase 1b: editing**
- Crossfades and every overlap rule; split on drop; Replace file.
- The audio editor; Repitch; reverse; normalise; Alt+edge tempo.

**Phase 2: the rack round trip**
- Track Out for every track kind (OUT, IN, dimming, IN light, states); kit IN; the return bus; the sound rules.
- Sources (§7.8): jobs from cable paths, the sources list with Use and Level, the Track Out menu slider.
- Strips and track-header levels for every track; FX tracks; sends; Show sends; OUT meter; inspector Sends.
- Mute/solo rules; mixer automation; Add a Track Out / Show in the rack.

**Phase 3: record, bounce, export**
- Audio recording (timeline + launcher), take writer, monitoring rule, metronome, recording offset.
- Bounce with tail; Export audio (master, stems, follows Plays).
- The Audio files window; glue across files.

**Phase 4: deeper**
- Warp markers, transients, Split at transients, Make triggers from audio.
- Takes and comping; group strips; automatic delay compensation; DAWproject audio export; FLAC export.

---

## 12. The user's answers (2 Nov 2026)

1. **Time stretcher: swappable** (§9.1).
2. **"FX track"** is the name.
3. **No layering on audio tracks; overlaps are automatic crossfades** (§6.3).
4. **A third pane** for the mixer, **and volume and pan in the track headers** (§7.1, §7.5).
5. **Level**: settled by the review: 0 dB = 10 V everywhere; only the import level stays open (§7.4).
6. **A thin Master row** at the bottom of the song pane.
7. **One Track Out for every track** (the user's idea, §8.1).
8. **Track headers: layout A** (§7.5).
9. **Setting what goes into an FX track**: Show sends, the OUT meter, the inspector Sends section (§7.6).
10. **The blind test** runs just before implementation, not now (§9.1).
11. **The review, all as recommended**: the monitoring rule (§2), 0 dB = 10 V (§7.4), a silent FX track without an
    effect (§2), the first loop sets the tempo (§5.5), setup helpers (§8.2), a metronome (§8.5), Phase 1 split in
    two (§11), and every other fix in `AUDIO_MIXER_REVIEW.md`.

12. **Several sources on one track: a level per source** that shows only when needed (§7.8).

Still open: the user's confirmation of the whole design, the import level (decided by ear before release), and
the two sources questions at the end of §7.8.
