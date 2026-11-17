# Ties, glides and voice allocation

Agreed with the user on 17 Nov 2026. Mockups: boards "Ties" and "Voices" on the design canvas.

## 1. Ties

A tie joins a note to the one before it: the gate stays high and the envelope is not started again.

- **On the note.** `Note::tied` means "this note continues the note before it".
- **Its partner** is the note on the same note channel that starts earlier and ends where the tied note starts, or
  overlaps it. With several, the one nearest in pitch; each note continues at most one. `ties::partners()` is the
  one rule, used by the editor, the snapshot, export and the tests.
- **No partner** (the notes were dragged apart): the tie is drawn broken (a dim open dot) and the note plays as a
  normal note.
- **Same pitch:** the two notes play as one long note.
- **Gates are never left high.** Stop, a song loop wrap, a launcher clip switch and mute end the note. A tie may
  carry over the clip's own loop (the last note runs into the first), because looping lines need it.
- **Gate length** (the track's staccato scale) never shortens a note that a tie continues.
- **Devices:** the flag travels with the note. Devices that copy notes (transpose, chord, echo) keep it, so an
  echoed tied line is a tied line. Notes a device makes from scratch (the arpeggiator's) are not tied.

## 2. The glide is a pitch line across the joint

The glide belongs to the tie. It is stored on the tied note:

- `glideBefore`: how long before the joint the pitch starts to move (inside the earlier note).
- `glideAfter`: how long after the joint it arrives (inside the tied note).
- `glideShape`: -1..1, the curve's bend (0 = straight), like a pitch curve segment's tension.

Both 0 = the pitch jumps at the joint. There is no track-wide glide time: a slew limiter does that job.

- While tied, the tie owns the glide: moving or transposing either note keeps the line joined.
- Each note's own pitch curve plays on top of the glide.
- **Untie, keep the glide** (J) writes the line into the two notes' own pitch curves. **Untie, remove the glide**
  (right-click) drops it.

## 3. Voices

The count is unchanged: Auto or 1 to 16, and playheads decide their own voices. A new **Allocation** choice
decides which channel a note lands on, inside each voice block (each playhead's voices too):

| Option | Rule |
|---|---|
| Rotate (default) | The next free channel after the last one used. Release tails ring longest. |
| Reuse | A pitch goes back to the channel it played on last, even while it still sounds. |
| Reset | The lowest free channel. Notes that start together (within 1/32) go low to high, strummed or not. |

Fixed rules under every option: a tied note keeps its partner's voice; Rotate starts again from the first channel
when play starts; when all voices are busy the oldest note gives way. Under Rotate a playhead's voices keep their
first-free rule, so a playhead playing one line sends one channel.

**One voice** is the mono case: keys played overlapping sound legato and record as ties, and so does a REC V/OCT
pitch change while the gate stays high.

## 4. Export and import

No file format has a tie mark. The two ways every program reads:

- **Overlapping notes** (default): the earlier note ends 1/128 after the tied note starts. A mono instrument in
  legato mode plays it without a new attack; its own glide setting makes the slide.
- **One note with pitch bend**: a chain of tied notes becomes one note, its glides written as pitch bend. MIDI
  export with MPE only (channel bend for one-voice tracks without MPE is not built).
- DAWproject writes the overlap. Its notes may hold "transpose" points, but the reference doesn't say whether their
  times count from the note or the clip; per-note curves wait until that is checked.
- Bouncing notes keeps the ties.

MIDI import: a part that is one line (where overlaps only happen as one note hands over to the next) gets those
overlaps as ties.
