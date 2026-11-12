# Arpeggiator patterns: what others offer, and what to add

Research note, 12 Nov 2026. Built the same day: Up-down ends twice (named "Up-down full"), Converge, Diverge, Shuffled and the restart on a
new chord (DECISIONS §152). The rest is not built.

## 1. Summary

Tessera's Arpeggiator already has the six patterns found almost everywhere: Up, Down, Up-down, Random, As played
and Chord. It also has Down-up. The one big gap is **Up-down with the end notes played twice** (C E G G E C). Nine of
the sources have it, often next to the version Tessera has now. After that come three patterns that are fixed and easy to
read: **Converge** (outside notes first, moving in), **Diverge** (middle first, moving out) and **Pedal low** (the
lowest note between every other note). For random, the most useful addition is a **Shuffle** that plays each note once,
in a seeded order, and keeps that order until the held notes change. This fits Tessera's "what you see is what plays"
rule better than more random modes do. Many sources also have rests and rhythm patterns, note repeats and ratchets.
Those need new controls, or another Tessera device already does them (Note Repeat, Note Filter, Random), so they are
not first in line. One behaviour fix ranks as high as any new pattern: **a new chord should restart the pattern**, using
the same rule the Arpeggiate command already uses (DECISIONS §45). Today the device carries on counting, so a new chord
can start in the middle of the pattern.

How to read the examples:

- `C E G` means the held notes C4, E4 and G4. `B` is B4.
- `C'` is one octave higher (C5). `C''` is two octaves higher.
- `|` marks where the pattern starts again.
- `[C E G]` means the notes play together, on one step.
- A **step** is one tick of the Rate (for example one 1/16). A **cycle** is one full pass through the pattern.

## 2. What Tessera does today

From `Chain::arpeggiator` in `src/model/Devices.cpp` [1] and the knob table in `src/model/DeviceTypes.cpp` [2].

**The note list.** The device keeps up to 16 held notes. When a 17th note comes in, the oldest one is dropped. Every
pattern except As played sorts the notes from low to high. As played keeps the order the notes arrived in. Octaves (1 to
4) makes the list longer: it adds the whole list again, one octave up each time. So with Octaves 2, `C E G` becomes the
list `C E G C' E' G'`, six notes long. Every pattern then walks this long list. That means the whole run goes through
every octave before it turns around.

**The step counter.** Steps fall on the song's grid. The step number only goes back to 0 when the arpeggiator starts
from silence, meaning no held notes at the moment of a step. If you change chords without a gap, or with Hold on, the
counter keeps going, and the new chord starts wherever the count happens to be. If a note joins in the middle, the list
gets longer and the position can jump.

| Pattern | What it plays | Octaves 1 | Octaves 2 |
|---|---|---|---|
| **Up** | Low to high, then again from the bottom. | `C E G \|` | `C E G C' E' G' \|` |
| **Down** | High to low, then again from the top. | `G E C \|` | `G' E' C' G E C \|` |
| **Up-down** | Up, then back down. The top and bottom notes play **once** at each turn (this is called "exclusive"). The turn happens at the top of the highest octave. | `C E G E \|` (4 steps) | `C E G C' E' G' E' C' G E \|` (10 steps) |
| **Down-up** | Up-down, upside down: starts at the top. Ends play once. | `G E C E \|` | `G' E' C' G E C E G C' E' \|` |
| **Random** | Each step picks any note in the list, octave copies included, with equal chance. The same note can come twice in a row. | for example `E E C G C …` | any of the 6 notes on each step |
| **As played** | The order the notes arrived in. Played G, then C, then E: | `G C E \|` | `G C E G' C' E' \|` |
| **Chord** | All notes at once on every step, all octaves together. | `[C E G] [C E G] …` | `[C E G C' E' G'] …` |

Notes on the details:

- **Random is repeatable.** Each pick comes from the song's seed, the device, the step number and the time of the step.
  Playing again from the same point gives the same notes. The next pass of a loop usually gives different notes,
  because the step number keeps growing.
- **Notes above MIDI 127** are not played. The step stays silent. (Yarns [3] moves such a note down by octaves until it
  fits.)
- **Other knobs.** Gate is 5 to 200 % of a step. Over 100 % the notes overlap. Swing delays every second step by
  Swing % of one third of a step. Velocity "Accent" makes every 4th step (steps 1, 5, 9 …) 25 % louder and the others
  15 % softer. Hold keeps the notes after you let go. New notes after letting go replace them.
- **A doc mismatch.** `docs/DEVICES.md` lists a "steps" knob for the Arpeggiator. The knob table has no such knob.

## 3. Candidate patterns

"Sources" counts the sources in section 5 that have the pattern. A pattern with a different name but the same notes counts.
Where two sources disagree, the row says so.

### 3a. Direction patterns (they fit in the Pattern list)

| Name(s) | What it plays | `C E G` (Octaves 1) | Other example | Sources |
|---|---|---|---|---|
| **Up-down, ends twice** ("Inclusive", "Up/Down 2", "Up/Dn", "Ping Pong", "up and down") | Up, then down. The top and bottom notes each play twice in a row at the turn. | `C E G G E C \|` (6 steps) | `C E G B B G E C \|` (8 steps). Octaves 2, whole run: `C E G C' E' G' G' E' C' G E C \|` | 9: [11] [14] [16] [19] [20] [9] [10] [5] [8] (AH as an option "Play last note") |
| Up-down, ends once (Tessera's Up-down: "Exclusive", "Up/Down", "Up & Dn", "Pendulum", "up+down") | Up, then down, ends once. | `C E G E \|` | `C E G B G E \|` | 13: [11] [14] [15] [16] [19] [20] [3] [4] [6] [7] [9] [10] [8], plus [21] across octaves |
| **Down-up, ends twice** ("down and up") | Starts at the top, ends twice. | `G E C C E G \|` | `B G E C C E G B \|` | 1: [10] |
| **Converge** ("outside in") | Outside notes first, moving in: lowest, highest, second lowest, second highest … | `C G E \|` | `C B E G \|`; Octaves 2: `C G' E E' G C' \|` | 2: [20], [10] (but [10] starts from the top: `G C E`, `B C G E`) |
| **Diverge** ("inside out") | Middle first, moving out, the lower note first in each pair. | `E C G \|` | `E G C B \|` | 2: [20], [10] ([10] goes up first and, with 4 notes, starts on the upper middle: `E G C`, `G E B C`) |
| **Pedal low** ("Pedal Point", "repeat low"; some software calls it "Thumb") | The lowest note comes back between every other note. The other notes go up. | `C E C G \|` | `C E C G C B \|`; Octaves 2: `C E C G C C' C E' C G' \|` | 2: [20] [10] |
| **Pedal high** ("repeat high"; some software calls it "Pinky") | The highest note comes back between every other note. | `G C G E \|` ([10], the others go up) or `G E G C \|` ([20] played in reverse, the others go down) | `B C B E B G \|` ([10]) or `B G B E B C \|` ([20]) | 2: [20] [10] |
| **Odd/even** ("Jump by 2"; also the "step" knob in [7]) | Every second note, then the ones skipped. | `C G E \|` | `C G E B \|` | 2: [20] [7]. Careful: with "jump by 2", an even note count never reaches half the notes: [20] plays `1 3 5 7` for 8 stages, and [7] with 4 notes plays only `C G C G`. |
| **Hopscotch** ("Crab") | Two up, one back: positions 1 3 2 4 3 5 … | `C G \|` in [8] (it stops at the top); `C G E C G E` in [20] (it wraps around) | `C G E B \|` in [8]; `C G E B G C B E \|` in [20] | 2: [20] [8] |
| **Crawl** ("Arp 3 Shift -1") | A short run that moves up one note each time. | In [6] the run is as long as the chord, so 1 octave sounds like Up. Octaves 2: `C E G E G C' G C' E' C' E' G' \|` | Runs of 3 that wrap, in [20]: `C E G E G B G B C B C E \|` | 2: [6] [20] |
| **Growing** / **Shrinking** | 1, then 1 2, then 1 2 3 … (Shrinking does the opposite). | `C C E C E G \|` | `C C E C E G C E G B \|` | 1: [20] |
| Converge then diverge | Converge, then Diverge. | — | — | 0 in the sources here (named in one software tool, not defined there) |

### 3b. Random patterns (they fit in the Pattern list)

| Name(s) | What it plays | `C E G` example | Sources |
|---|---|---|---|
| Random (dice) | Any note on each step. Repeats allowed. Tessera has this. | `E E C G C …` | nearly all: [3] [4] [5] [9] [11] [12] [13] [14] [15] [16] [17] [19] [20] |
| **Shuffle, new each cycle** | Each note once per cycle, in a new random order every cycle. A note can still repeat across the join between cycles. | `E C G \| G E C \| C G E \|` (note `C \| C` can happen) | 4: [17] (shuffled one octave at a time), [9], [10], [6] (all notes and octaves together) |
| **Shuffle, kept** ("Random once", "Pattern") | Shuffles once, then repeats that order until the notes change or the pattern is reset. | `E C G \| E C G \| …` | 3: [20] ("Shuffle"), [5] ("Pattern"), [13] ("Pattern": a new random list on every new note, and the lowest note comes twice as often) |
| **Random, never twice in a row** ("Quasi Random") | Dice, but the same note never plays twice in a row. | `E C G C E G E …` | 1: [20] |
| **Random walk** ("Walk", "Brownian") | Mostly moves to the next note, sometimes stays, sometimes goes back one. [20] and [13]: 50 % next, 25 % stay, 25 % back. [5] has three speeds: 40/30/30, 50/25/25 and 60/20/20. | `C E E C E G G E …` | 3: [20] [13] [5] |
| Random note and octave apart | Picks the note and the octave separately. | — | [3]. Tessera's Random gives the same odds, because it picks from the long list. |

The names clash here. "Shuffle" means "new order every cycle" in [17], [9] and [10], but "keep one order until reset" in
[20]. Tessera should not call either one just "Shuffle" without saying which it means.

### 3c. Options that change the order (each one needs a new knob or a menu)

| Option | What it does | Example | Sources | In Tessera today |
|---|---|---|---|---|
| **Octave direction** | Octaves go down, or up and back down, or at random, in place of only up. | Octaves up-down with 3 octaves: the run in octave 1, 2, 3, 2, then again [19]. Random octave in [13]: 75 % the played octave, 15 % one up, 7 % one down, 3 % two up. | [19] (Up, Down, UpDn, Alt, Alt 2), [5] (Up, Down, Up-down, Alternate, Random), [21] (Alt Up, Alt Down), [13] (Rand Oct) | No. Put a Transpose device of −12 before the Arpeggiator to start lower. |
| **Bounce in each octave, or across the whole range** | Up-down can turn around inside each octave, or only at the top of the highest one. | Per octave, ends twice, Octaves 2: `C E G G E C C' E' G' G' E' C' \|` [19] [5]. Whole range: Tessera's way, also [3] [4] [6] [21], and [19] with "Alt 2". | as listed | Tessera always uses the whole range. |
| **Each note in every octave first** | `C C' E E' G G'` | — | **Not found in any source checked.** Every source plays the whole run in one octave, then moves to the next. | No |
| **Note repeats per step** ("Up x2", "Repeat", "step repeats") | Each note plays 2 (or more) times before the next. | `C C E E G G \|` | 3: [11] (Up x2, Down x2), [13] (Repeat, always ×2), [5] (any number) | Yes, in effect: Arpeggiator at 1/8 with Gate 100 %, then **Note Repeat** at 1/16. |
| **Ratchet** | Splits one step into 2, 4 or 8 fast hits, sometimes by chance. | `C C C C E G` | 4: [19] (1/2/4/8 plus Chance), [13], [5], [16] (per step) | Note Repeat after the Arpeggiator does the same on every step, without chance. |
| **Rests / rhythm patterns** | A pattern of played and silent steps on top of the note order. | `O X O`: `C – E – G –` | 9: [3] (22 patterns plus Euclidean), [4], [14] (33 patterns), [21] (15 patterns plus your own), [15] (O, OXO, OXXO, random, "Deviate"), [16], [6] (16 steps on/off), [22] (dots and dashes), [5] | Only roughly: Note Filter "Every" and "Offset", or Random "Drop". |
| **Does a rest use up a note?** | The sources disagree. In [3] and [15] the note waits for the next played step. In [6] the order moves on during a rest. [21] has both: "pause" waits, "silent" moves on. [22] offers "skip" (remove the step) or "pause". | `C – E – G –` versus `C – G – E –` | as listed | — |
| **Length** | Cuts or stretches the pattern to N steps, apart from how many notes are held. | Length 3 with `C E G B`: `C E G \|` [19] | [12] (1 to 64), [19], [5], [10] ("Beats"), [17] | No |
| **Restart / retrigger** | When the pattern starts again from the first note. [12]: at the end of the Length, or never. [21] "Pattern Reset": the note order restarts with the rhythm. | — | [12] [21] | Only after silence (section 2). The Arpeggiate command restarts on a new chord (DECISIONS §45). |
| **Start offset** | Starts N steps into the pattern. | Offset 1 with `C E G`: `E G \| C E G …` [19] | [19] [8] | No |
| **Chord with octaves** | [19] plays the chord one octave at a time: `[C E G] [C' E' G'] \|`. Tessera plays all octaves at once. | — | [19] | Different |
| **Per-step note choice** | Each step of a rhythm can play the first note, the last, first and last, the whole chord, a random note, or the note before. | — | [21] | No |
| **Generative and "mutate"** | Preset phrases, made-up note patterns, or a knob that swaps or shifts notes at random. | — | [19] (64 phrases), [12] (9 made-up modes, Mutate, Spice), [13] (Mutate), [15] [16] (Mutate, Deviate) | Random, Motion and Velocity devices |
| **Strum** | Plays the held notes one by one as you move the mod wheel. | — | [16] | The Chord device's Strum |

## 4. Recommendations

Tessera's rules for this: one control per concept, no duplicates, "what you see is what plays" over hidden randomness,
and fewer clear controls over many. A pattern that another device already gives is a strong case for skipping it.

### Add, in this order

1. **Up-down, ends twice.** Pattern list, no new knob. It is the most common pattern Tessera lacks (9 sources). It also
   matters for timing. With N notes, ends-once loops every 2N−2 steps and ends-twice every 2N steps. With 4 notes that is
   6 versus 8 steps, and only 8 lines up with a bar of 1/16 notes. With 3 notes it is the other way round (4 versus 6
   steps). So each version is the right one for some chords. Keep the whole-range bounce, as Up-down does today. Name it
   plainly, for example "Up-down (ends twice)". Rename today's entry "Up-down (ends once)" so the pair is clear. Skip the
   ends-twice version of Down-up: only 1 source has it.

2. **A new chord restarts the pattern.** This is a behaviour fix, not a knob. Use the rule DECISIONS §45 already uses
   for the Arpeggiate command: restart when most of the held notes have just begun, but not when one inner voice moves.
   The device and the command then agree, and a chord change always starts on the first note of the pattern. That makes
   every pattern, including the new ones, easier to predict. No source needs a knob for the plain version.

3. **Converge and Diverge.** Pattern list, no new knob. Both are fixed and easy to hear. Fix one exact rule, because the
   sources differ. Proposed rule: Converge = lowest, highest, second lowest, second highest … (`C G E`, `C B E G`).
   Diverge = from the middle out, the lower note first in each pair (`E C G`, `E G C B`). For an even number of notes this matches [20]. The octave
   copies count as more notes, as in Up-down today.

4. **Shuffle, kept until the notes change.** Pattern list, no new knob. It plays each note once per cycle, in a random
   order taken from the song's seed. The order stays the same every cycle. It changes when the held notes change, or on a
   restart. You get the feel of random without the clumps of dice (`C C C`), and a loop sounds the same every pass, so
   what you hear is what you will hear again. Call it something that says it stays, for example "Shuffled". Leave out
   the "new order every cycle" version: Random already gives a different result every pass.

5. **Pedal low.** Pattern list, no new knob. A classic bass and keyboard figure (`C E C G`). It only comes from 2 sources
   here, but the result is fixed and easy to hear. Add **Pedal high** only if users ask. If it is added, make it Pedal
   low turned upside down (top note between, the others going down: `G E G C`), so one rule covers both.

6. **Chord steps through the octaves.** A behaviour change, no knob. With Octaves 2, play `[C E G]`, then
   `[C' E' G']`, as [19] does. Today all octaves sound at once, which makes Octaves act like a thicker chord, not an
   arpeggio. This needs a manual update and a DECISIONS entry, because it changes how existing songs sound.

### Maybe later

- **Rests and rhythm patterns.** 9 sources have them, so users will ask. But they need a new control (a row of steps),
  and that should get its own design with a mockup first. Decide up front whether a rest uses up a note (see 3c). Until
  then, Note Filter "Every" and Random "Drop" give rough rests.
- **Random, never twice in a row.** This could become the rule for the existing Random entry, not a new entry. It
  changes how existing songs sound, so it needs a decision.
- **Crawl** and **Odd/even.** Fixed and clear, but only 2 sources each, and Odd/even only differs from Converge once
  there are 4 or more notes.
- **Length** and **Restart every N steps.** Useful for odd-length riffs. This would be a new knob, and it may be what
  the "steps" knob in DEVICES.md was meant to be.

### Skip

| Candidate | Why skip it |
|---|---|
| Down-up, ends twice | 1 source. One more list entry for little gain. |
| Converge then diverge | No source here defines it. Converge and Diverge cover the sound. |
| Hopscotch / Crab | The sources disagree, and with 3 notes it gives odd results (`C G` only, in [8]). |
| Growing / Shrinking | 1 source. Long loops that are hard to read in the strip. |
| Random walk | Hidden randomness, and it often sounds like Random. The Random and Motion devices already add drift. |
| Shuffle, new each cycle | Random already gives a different result every pass. |
| Note repeats (Up x2) | Note Repeat after the Arpeggiator already does it. |
| Ratchet | Note Repeat already does it. Ratchet by chance is hidden randomness. |
| Octave direction | A Transpose device before the Arpeggiator moves the start down. The up-and-back bounce is Up-down. Random octave is hidden randomness. |
| Each note in every octave first | No source has it. |
| Generative modes, phrases, mutate | Hidden randomness. Random, Motion and Velocity cover it. |
| Strum | The Chord device's Strum does it. |
| Per-step note choice | Needs a step editor. Revisit together with rests. |

### Knob or list entry?

- **Fit in the Pattern list** (no new knob): Up-down ends twice, Converge, Diverge, Shuffled, Pedal low, Pedal high,
  Odd/even, Crawl.
- **Behaviour changes** (no knob, but they need a DECISIONS entry and a manual update): restart on a new chord, Chord
  through the octaves, Random never twice in a row.
- **New control needed**: rests and rhythm, Length, Restart every N, Start offset, octave direction.

With the first five added, the Pattern list grows from 7 to 12 entries. That is still one short menu, and each entry
plays a fixed order you can hear and predict, except Random.

## 5. Sources

Repositories were read at their latest commit on 12 Nov 2026.

1. Tessera, `src/model/Devices.cpp`, `Chain::arpeggiator` and `Chain::release`.
2. Tessera, `src/model/DeviceTypes.cpp`, the Arpeggiator knob table.
3. Mutable Instruments Yarns firmware, `yarns/part.cc` (`ClockArpeggiator`), `yarns/part.h`,
   `yarns/resources.cc` — https://github.com/pichenettes/eurorack
4. Mutable Instruments MIDIpal firmware, `midipal/apps/arpeggiator.cc` — https://github.com/pichenettes/midipal
5. Synthstrom Deluge community firmware, `src/deluge/modulation/arpeggiator.cpp`, `arpeggiator.h`,
   `src/definitions_cxx.hpp` — https://github.com/SynthstromAudible/DelugeFirmware
6. Odin 2 synthesizer, `Source/gui/OdinArpeggiator.cpp`, `OdinArpeggiator.h`, `ArpComponent.cpp` —
   https://github.com/TheWaveWarden/odin2
7. Bespoke Synth, `Source/Arpeggiator.cpp` — https://github.com/BespokeSynth/BespokeSynth
8. Amalgamated Harmonics for VCV Rack, `src/Arp31c.cpp` (Arp 3.1 Chord), `src/Arp32p.cpp` (Arp 3.2 Pattern) —
   https://github.com/jhoar/AmalgamatedHarmonics. Its "Diverge" and "Converge" are interval patterns built from one
   note, not orders of held notes, so they don't count under Converge/Diverge.
9. Bogaudio ARP for VCV Rack, `src/Arp.cpp`, `README.md` — https://github.com/bogaudio/BogaudioModules
10. Squinktronix SqHarmony Arpeggiator for VCV Rack, `docs/arpeggiator.md`, `util/ArpegPlayer.h`,
    `tests/testArpegPlayer.cpp` — https://github.com/squinkylabs/SqHarmony. The docs and the tests disagree about which
    of "inside out" and "outside in" is which. The examples above follow the tests (the code).
11. Arturia KeyStep User's Manual 1.0.0, §6.3 —
    https://downloads.arturia.com/products/keystep/manual/KeyStep_Manual_1_0_0_EN.pdf
12. Arturia KeyStep mk2 User Manual, §3.2–3.3 (copy hosted by a dealer) —
    https://files.kraftmusic.com/media/ownersmanual/Arturia_KeyStep_Mk2_User_Manual.pdf
13. Arturia MiniFreak User Manual, §14.2 (copy hosted by a dealer) —
    https://www.adorama.com/col/productManuals/AR571001.pdf
14. Novation Summit User Guide 1.0.1, "The Arpeggiator" and "Arp Mode" —
    https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/Summit%20manual%201.0.1.pdf
15. Novation Launchkey MK3 User Guide v6, "Arp Modes", "Arp Rhythms" —
    https://fael-downloads-prod.focusrite.com/customer/prod/downloads/Launchkey%20MK3%20User%20Guide%20v6%20-%20EN.pdf
16. Novation Launchkey 49 MK4 User Guide v3, "Arp Type", "Arp Pattern Mode" —
    https://fael-downloads-prod.focusrite.com/customer/prod/downloads/launchkey_mk4_49_user_guide_v3_en.pdf
17. Elektron Analog Four MKII User Manual OS1.51C, §10.6 —
    https://elektron.se/wp-content/uploads/2024/09/Analog-Four-MKII-User-Manual_ENG_OS1.51C_220204-1.pdf
18. Elektron Digitone Keys User Manual OS1.41, §9.6 (modes True, Up, Down, Cycle only; used to check, not counted
    above) — https://elektron.se/wp-content/uploads/2024/09/Digitone_Keys_User_Manual_ENG_OS1.41_231108.pdf
19. ASM Hydrasynth Deluxe Owner's Manual 2.2.0, "Arp Edit Mode" (copy hosted by a dealer) —
    https://kmraudio.com/cdn/shop/files/Hydrasynth_Deluxe_Owners_Manual_2.2.0.pdf
20. Intellijel Metropolix Manual v1.6.5, "Order Options" (these are orders for sequencer stages; the examples apply them
    to held notes) — https://intellijel.com/downloads/manuals/metropolix_manual_v1.6.5_2026.09.16.pdf
21. Waldorf Blofeld User's Manual, "Arpeggiator" pages 74–79 (copy hosted by a dealer) —
    https://www.adorama.com/col/productManuals/WDFBLF1.pdf
22. Teenage Engineering OP-1 guide, "Sequencers", arpeggio section —
    https://teenage.engineering/guides/op-1/original/sequencers
