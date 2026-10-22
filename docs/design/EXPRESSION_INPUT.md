# Expression from MIDI input, and one way to record (design, 22 Oct)

Today Tessera's MIDI input hears only note-on and note-off (`TesseraModule::readRecordInputs`). Everything else a
keyboard sends is dropped. This plan records and plays through the rest, for any keyboard with poly aftertouch and
any MPE controller. A UX review of the whole recording and automation workflow (22 Oct) found that automation
recording is hard to set up, so the plan also gives recording one rule. Decision: DECISIONS.md §73.

## 1. What a keyboard sends

Two kinds of data, no matter the controller:

| Message | One value for… | Example |
|---|---|---|
| Note on / off, with velocity | each note | (recorded today) |
| Poly aftertouch | each note | press one key of a chord harder |
| Channel pressure ("mono aftertouch") | the whole channel | press any key; every note reacts |
| Pitch bend | the whole channel | the bend wheel |
| CC 0–127 | the whole channel | mod wheel = CC 1, sustain = CC 64 |

Channel pressure and pitch bend are their own message types, not CCs, but they behave like CCs: one value shared by
every note on the channel. A keyboard may send poly and channel pressure at the same time.

### MPE (MIDI Polyphonic Expression)

MPE gives each sounding note **its own MIDI channel**, so the per-channel messages on that channel become per note:

- **Pitch bend:** sideways slide, default range ±48 semitones.
- **CC 74:** "timbre" or "slide", usually up and down the key.
- **Channel pressure:** how hard that one finger presses.

Channels are grouped in a **zone**. The usual lower zone has channel 1 as the **master channel** (messages for all
notes: mod wheel, sustain, a master bend of ±2 semitones) and channels 2–16 as **member channels** (one per held
note). A mirror upper zone (master 16, members counting down) exists but is rare. A controller can announce its zone
with the MPE Configuration Message (RPN 6) and its bend range with RPN 0; otherwise the defaults apply. A controller
sends a note's starting bend, timbre and pressure **just before** its note-on, and those values belong to that note.

MIDI 2.0 has per-note controllers natively. Rack's MIDI is MIDI 1, so it is out of scope.

## 2. The one rule

**Arm the track, and everything you play goes into one take:** notes, their expression, bend, wheel, pressure,
any CC you move, and the track's knob and CV lanes. The take lands where the notes land: a launcher clip, or the
timeline. **One take is one undo step** (input quantize stays a second one).

- Lanes have **no arm switch of their own** any more. A lane records when its track is armed and the transport
  records. Patches with armed lanes load with the arm dropped, and the track arm takes over.
- **Nothing records silently empty:** a knob lane with no knob, or a CV lane with nothing patched, shows a warning in
  its header.

## 3. What is saved where

**Per note** (already in the model: `Note::pitchExpr`, `timbreExpr`, `pressureExpr`):

| Input | Note curve |
|---|---|
| Poly aftertouch | Pressure |
| MPE channel pressure (member channel) | Pressure |
| MPE CC 74 (member channel) | Timbre |
| MPE pitch bend (member channel) | Pitch |

The pitch curve's range grows from ±24 to ±48 semitones to fit MPE's default. MPE notes are recorded on one channel
(channel 1), not one per finger, and a track's channel filter lets a whole MPE zone through.

**Per take, in clip lanes** (normal mode, or the MPE master channel). Bend and pressure need no setup; a CC needs a
lane on the track (DECISIONS §76):

| Control | Lane | Output |
|---|---|---|
| Pitch bend | "Pitch bend", ±2 semitones | added to V/OCT on every voice; no AUTO channel |
| Channel pressure | "Pressure" | **PRESS** on every voice: the larger of it and the note's own pressure |
| A CC the track has a lane for | a copy of that lane, e.g. "Mod wheel (CC 1)", "Sustain (CC 64)" | that lane's AUTO channel |

- A CC with no lane is not recorded; after the take a message offers **Add lane**. Bank select and channel mode
  messages (CC 0, 32, 120-127) are never reported. A later take writes into the lane that already has that control.
- A lane's menu has **Don't record this control** for the rare opt-out, and learn to point it at another control.
- **Rest values:** when no clip lane plays them, bend, pressure and sustain go back to 0, so a track never stays
  detuned or held after a clip ends.
- Sustain is only a lane (a CV to patch); it does not lengthen notes.

**Knob and CV lanes** follow the same "where" rule: into the launcher clip being recorded, or the track's lanes on the
timeline. A CV lane picks its AUTO REC channel freely (default: its own AUTO channel), and defaults to Write mode.
Knob lanes keep Touch / Latch / Write.

**Settings** (MIDI IN menu): **Expression input: Normal / MPE**, and the MPE bend range (default 48, updated from
RPN 0). A controller that sends the MPE Configuration Message switches to MPE by itself.

## 4. How it reaches the output modules

| Module | Per-note expression | Per-channel controls |
|---|---|---|
| Track Out | Pitch and the bend lane added to V/OCT; TIMBRE; PRESS (note pressure, or channel pressure if larger) | CC lanes on the AUTO poly jack |
| Drum Out | Not in the first version (TRIG and VEL only) | CC lanes on the AUTO poly jack |
| Auto Out | — | one jack per lane |

The output modules don't change; they already copy `TrackFrame`. The work is upstream:

- **Live play-through.** While a track is armed, everything you play reaches its outputs at once, before and during
  recording. `LiveNote` becomes a live event with three kinds: note on/off, per-note value, per-channel value. The
  engine writes a per-note value to the voice that holds the note, and per-channel values to V/OCT, PRESS or the
  AUTO channel of the lane they would record into. Fixed-size arrays only: no allocation on the audio thread.
- **Recorder (UI thread).** Keeps timed values per held note and per control. At the end of the take it turns them
  into note curves (thinned: shape kept within about 1%, at most about 64 points per curve) and lane points, and
  commits notes and lanes as one command.
- **Nothing hangs.** On note-off, stop, mute or a snapshot swap, live values fall back to what the song plays, or
  their rest value.
- **What you see.** Lanes created during a take are drawn live, like the notes.

## 5. Also on this branch

- **Rack knob menu: "Automate on track X"** creates a lane, maps the knob and is ready to record in one step.
- Retrospective capture keeps bend, wheel and pressure with the notes.
- MIDI file import reads poly aftertouch and MPE into note curves, using the same reader (`src/model/MidiReader`).
- Tests: the reader with byte streams; values before a note-on; two notes in a row on one MPE channel; poly
  aftertouch after note-off (ignored); thinning keeps the shape; one take is one undo step; rest values.

## Sources

- What is MPE? — https://rogerlinndesign.com/support/support-linnstrument-what-is-mpe
- MPE zones — https://docs.juce.com/master/tutorial_mpe_zones.html
- MPE for MIDI 2 (MIDI Association) — https://midi.org/wp-content/uploads/2025/04/CCRMA-OH24-presentation.pdf
