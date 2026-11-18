# 13. Devices and modulators

**Devices** are note effects. They change a track's notes on their way out: an arpeggiator plays chords one note at a
time, an echo repeats notes, a chord device turns one note into three. The notes in your clips don't change; you can
switch a device off at any time.

**Modulators** (LFOs, envelopes, steps…) move device settings by themselves, so the music keeps moving.

## The device strip

Click **Devices** at the left of the lower pane's header, between **Notes** and **Mixer** (or press **F3**), to open
the strip above the notes. **Devices** stays lit while the strip is open, and **Notes** too, since the notes still show
under it. Audio tracks have no note devices, so there **Devices** is greyed.

From left to right the strip shows the **SONG** modulators, the **TRACK | CLIP** switch, the **TRACK** (or **CLIP**)
modulators, then the devices, each with its own **DEVICE** modulators on its left edge. See
[Modulators](#modulators). It shows the selected
track's devices from left to right, in the order the notes flow through them: **in** → devices → **out**. Each device
is a card in the track's colour, with one to three small pictures that show what it does to the notes.

![The device strip: an Arpeggiator and an Echo on the Chords track, and its LFO as a dot on the folded TRACK section](images/device-strip.png)

The upright **TRACK | CLIP** switch says which devices the clip on show plays through: the track's or its own (see
[A clip's own devices](#a-clips-own-devices)). The side that plays is filled, and a small arrow points into the
modulators that go with it.

When the strip is closed, a track with devices shows a thin bar naming its chain ("Arpeggiator → Echo"). Click it to
open the strip. A track without devices keeps the strip closed.

**Room for more cards.** The strip always shows one row of cards. When they don't fit, the row scrolls sideways: turn
the mouse wheel over the cards.

**Changing the chain while it plays.** Adding, removing, moving or switching off a device while the song plays doesn't
stop the track. Every device that stays keeps what it holds, so an Arpeggiator plays on through the change, on its next
step. A device you remove or switch off hands the notes it holds on to the next device, so a chord keeps sounding. A
device you add starts working on the notes that come next.

**A short pane.** When the pane is too short for one row of cards and the notes together (the editor in the module
panel, for example), the devices take the whole pane. **Devices** then switches: click it to see the devices, and click **Notes**, or
**Devices** again, to go back to the notes. In a narrow pane the three fold into one button that names what shows
("Notes ▾"): click it to pick another. Picking **Draw a curve** on a tag, or clicking a Curve modulator, switches to the notes by itself, since the
curve is drawn in the notes' lower pane; the pane's maximize button gives it more room.

## Adding devices

Click **Add device** at the end of the chain, or the **+** between two devices. A searchable list opens.

![The device browser](images/device-browser.png)

- Type to filter by name or by purpose: "delay" finds Echo, "loose" finds Humanize.
- **Up / Down** choose, **Enter** adds, **Esc** closes.
- The footer says where the device will go.

A track holds up to 16 devices.

## The devices

| Device | What it does | Its settings |
|---|---|---|
| **Transpose** | Moves notes up or down. | Semitones, Octaves, Key steps (moves within the key). |
| **Scale** | Keeps every note in a key. | Key from (Clip, Song, Fixed), Root, Scale, Notes off key (Nearest, Up, Down, or Drop them). Root and Scale are faint unless Key from is Fixed. |
| **Chord** | Turns each note into a chord. | Chord, In key, Inversion, Voicing (Close, Open, Drop 2), Strum. |
| **Note Length** | Makes notes shorter, longer or legato. | Mode (Scale, Fixed, Legato), and the one setting that mode uses: Scale %, Length or Overlap. |
| **Arpeggiator** | Plays held notes one at a time. | Pattern (see below), Rate, Gate, Octaves, Swing, Velocity (As played, Fixed, Accent), Hold. |
| **Note Repeat** | Plays every note again and again for as long as it lasts, so a held chord repeats as a chord. | Rate, Gate, Decay (each repeat a little quieter). |
| **Echo** | Repeats notes, fading out, like a delay. | Time, Repeats, Decay, Pitch (per repeat), Gate. |
| **Velocity** | Makes notes softer, harder, or keeps them in a range. | Curve, Min, Max, Random. |
| **Humanize** | Loosens timing, velocity and length a little, differently on each pass. | Timing (only ever later), Velocity, Length. |
| **Random** | Changes pitches and drops notes, differently on each pass. | Pitch (steps), In key, Drop (chance to skip a note). |
| **Groove** | Swings this track its own way, in place of the song's swing: a straight bass under swung drums, or extra-swung hats. Turn its Swing with an LFO or a lane to ride it. A new Groove starts on the grid most of the track's off-beat notes sit on. | Swing (50% straight to 75%), Grid (1/16 or 1/8). |
| **Note Filter** | Lets only some notes through. | Lowest, Highest, Min velocity, Max velocity, Every (play every Nth note), Offset, Channels (any channel, or a set of them such as 1–4 and 10). |
| **Motion** | Moves the notes that joined it, a little differently on each pass. See [Notes that drift](#notes-that-drift). | Pitch (steps), Time (a delay in 1/16 steps), Length, Velocity, In key. |

**Playheads** is different from the others: it doesn't change notes, it reads the clip. See
[Playheads](#playheads) below.

**Arpeggiator patterns.** The examples hold C, E and G. All patterns except As played go from the lowest note to the
highest. **Octaves** adds the notes again an octave up, and the pattern runs over all of them (Pattern sets octaves per step instead).

| Pattern | What it plays | C E G |
|---|---|---|
| **Up** | Low to high, then again. | C E G, C E G… |
| **Down** | High to low, then again. | G E C, G E C… |
| **Up-down** | Up, then back down. The top and bottom notes play once at each turn. | C E G E, C E G E… |
| **Up-down full** | Up, then back down, with the top and bottom notes played twice. Four notes fill exactly 8 steps. | C E G G E C… |
| **Down-up** | Down, then back up, the ends once. | G E C E, G E C E… |
| **Down-up full** | Down, then back up, with the top and bottom notes played twice. | G E C C E G… |
| **Converge** | From the outside in: lowest, highest, then the next pair inward. | C G E, C G E… (with B too: C B E G) |
| **Diverge** | From the middle out, the lower note of each pair first. | E C G, E C G… (with B too: E G C B) |
| **As played** | In the order you played the notes. | as played |
| **Random** | Any note on each step; the same note can come twice. | for example E E C G… |
| **Walk** | Starts on the lowest note and wanders: half the time on to the next note, a quarter of the time the same note again, a quarter back one (round the ends). The same notes always walk the same path. | for example C E E C E G G E… |
| **Shuffled** | Each note once per round, in a mixed order. The same notes always mix the same way, so a loop sounds the same every pass. | for example G C E, G C E… |
| **Pattern** | A line of up to 16 steps you can write yourself, with chords and rests (see below). Until you do, it is made from the held notes, the lowest note twice as often as each of the others. | for example C G C E C C G E… |
| **Chord** | All the notes together on every step. | C+E+G… |

**Hold.** With **Hold** on, the arpeggio keeps playing the last chord after its notes end, through a rest, until the
next chord comes and takes its place. Chords that follow each other without a gap sound the same with Hold on or off;
Hold only changes the rests. It is most useful when you play the track live from a keyboard: play a chord, let go,
and the arpeggio plays on.

**Writing a Pattern line.** With **Pattern** picked, the card changes. Its settings sit in a narrow column on the left
(**Pattern**, **Rate**, **Swing**, **Hold**), and the rest of the card is the line: 16 steps, everything in one view.
Octaves, gate and velocity are set per step there, so the card's **Oct**, **Gate** and **Velocity** don't show.

![An Arpeggiator on Pattern: a chord of notes 1 and 3 on step 1, step 2 moved one chord note up (Move counts chord notes), step 3 silent at Vel 0, and the rest a line from the dice](images/arp-pattern.png)

- **The note rows.** Rows **1**, **2**, **3**… are the held notes, 1 the lowest. The grid has a row for each note of
  the biggest chord the arpeggio gets, from 4 up to 8: a track of six-note chords shows rows 1 to 6. It counts the
  clips' chords (with what a **Chord** device before it adds) and the chords it has held while playing. With more
  rows, the rows get thinner so the card keeps its height.
- **Lighting notes.** Each cell is its own switch: click it to light or clear that note on that step. Light several
  in one step for a chord, none for a rest. Drag along a row to light or clear a run of steps in one move. The first
  click fills the grid with a line from the dice, so it is never empty; the step you click then plays only your note.
- **Past the notes held**, a row wraps around to the lowest note again, in the same octave: rows 1 2 3 4 on C E G play
  C E G C. A note that comes up twice in one step plays once, so a step with every row lit plays exactly the chord.
- **What plays now.** The lit cells are what each step plays in this pass of the line, with the modulators as they
  are now, so the grid moves while they move. A note you wrote that plays somewhere else now (moved by **Move**), or
  not at all (a rest), shows as an outline where you wrote it.
- **Vel** scales the velocity of the note you hold (100% plays it as it is). At **0%** the step is a rest: no note, no
  gate. Its Vel cell reads **rest**, and its notes stay written as outlines. **Gate** is how long each note is, from 5% to 200% of a step; a
  → shows a note that runs into the next step.
- **Oct** moves a step from three octaves down to three up. **Move** moves it −12 to +12. The small chip by its name
  says what one step of Move is; click it to change: **semi** (a semitone), **key** (a step of the key: +2 on C in C
  major plays E, so the line never leaves the key) or **chord** (the next held note: +1 on C in C E G plays E, and
  past the top it wraps around to the lowest, never changing octave: that is **Oct**'s job). With **chord**, the lit
  cells move inside the chord as the step moves.
- **Changing a value.** Drag up or down in a cell; only that step changes. Start a drag sideways to set a whole run
  of steps to the same value in one move (on **Gate**, sideways makes that step longer or shorter instead, like its
  bar). A click on a cell's top half steps it up, on its bottom half down (Vel by 10%, Gate by 25%, Oct and Move by
  one). The mouse wheel steps a cell (Vel and Gate by 5%). Double-click resets a cell. Steps past a row's end are
  drawn fainter.
- **Each row has its own length.** The number on the right is the row's length in steps. Drag it left or right: the
  row's end moves as far as the pointer, starting from the length it has. Or turn the wheel over it, double-click it
  for 16, or drag the white line where a shorter row ends. Rows of different lengths drift against
  each other: a 12-step Vel row over 16 note steps makes a line that only repeats every 3 bars.
- **While it plays**, each row outlines the step it is on.
- **Every step can be automated and modulated.** Right-click a Vel, Gate, Oct or Move cell for the same menu a
  setting has: type a value, reset it, **Modulate with**, **Draw a curve**, **Automate in the timeline** or **in this
  clip**. Lanes name it "Step 5 velocity" and so on.
- **A whole row at once.** Each value row also has one setting that adds to every one of its steps: turn the wheel
  over the row's name, or right-click the name to type it, modulate it or automate it ("Move, every step"). While
  mapping, click a row's name to map the modulator to the whole row: an LFO on the Move row's name walks the whole
  line up and down the chord. When it isn't 0, the value shows by the name, and each step shows what it plays with
  it: a white line on Vel and Gate, a small number on Oct and Move. Mapping works on steps too: click a modulator's arrow and the
  steps it can reach light up with the tags; click one for +50%, or drag it up or down for how far. A step a modulator
  moves is outlined in its colour and, like a tag, has a depth bar along its bottom: the coloured part is how far each
  modulator or lane moves it across its range, the dot is where it is now. A step a lane sets
  wears an **A**.
- **Reset** in the card's header starts again from a plain rising arpeggio: notes 1 2 3 4 repeated, every row back to
  its default and 16 steps long. One undo step.
- Once you have edited it, the dice switch reads **Yours**. The dice writes fresh notes for the line (one undo step);
  **New** plays fresh notes on every play and loop pass, and **Yours** goes back to yours.

When you change to a new chord, the pattern starts again from its beginning, also with **Hold** on or when the chords
follow each other without a gap. If only one note of the chord changes (an inner voice moving), the pattern carries
on.

**Order matters.** Chord → Arpeggiator arpeggiates the chord. Arpeggiator → Chord makes a chord on every arpeggio step.

Random choices are repeatable: the same pass plays the same way every time, also in a MIDI export.

On **drum tracks**, devices that change pitch (Transpose, Scale, Chord, Random pitch) are skipped by default, because a
pitch is a drum lane there. Turn on **Affect drum lanes** in the device's ⋯ menu to use them anyway.

The track's **Voices** setting (Auto) follows its devices: a chord device adds voices, an arpeggiator plays one at a
time.

## Using a device card

- **Power dot** (top left): bypass the device. The dot turns hollow, the pictures fade, and the notes pass through
  unchanged.
- **⋯ menu**: rename, duplicate, delete, reset its settings, affect drum lanes, add a modulator to this device.
- **The dice**, on devices that play by chance (Random, Humanize, Velocity with Random above 0%, and the Arpeggiator
  on Random, Walk, Shuffled or Pattern): a dice button and a two-part switch, **[17 | New]**.
  - **New** (where a new device starts): new choices on every play and every pass of the song loop.
  - **17**, or any number from 1 to 999: the same choices every time. Click the number half to switch to it,
    double-click it to type one. The dice rolls another number and keeps it; each roll is one undo step.
  - A bake, bounce or export of something on **New** rolls fresh; its message offers **Keep it**, which sets the
    switches to the numbers it used, so you hear what you baked. The Random modulator and an LFO with a random
    shape have the same dice at the top of their settings.
- **Drag the card's header** to move it in the chain. A line shows where it will land. **Ctrl+drag** copies it.

### Selecting, copying and pasting

Click a card to select it: it gets an outline and a light tint. Click a modulator row to select that modulator, or a
section's header (or an empty part of it) to select the whole section without lighting each row. **Ctrl+click** adds
or removes one, **Shift+click** takes everything between, the same as with tracks and clips. Click empty strip space,
or press **Esc**, to clear the selection.

- **Ctrl+C** copies what is selected, **Ctrl+X** cuts it, **Ctrl+V** pastes it, **Ctrl+D** duplicates it and
  **Delete** removes it. **Ctrl+A** selects every card. These keys act on the strip after you click in it, never on
  the notes under it. The right-click menu has **Cut**, **Copy** and **Paste** too, naming what they act on ("Copy
  Arpeggiator + Env 1").
- A paste goes into the chain the TRACK | CLIP switch shows, after the card you right-clicked (or the last selected
  one), else at the end. Modulators go into that track's (or clip's) section. A card's own DEVICE modulators always
  go with it.
- Cards and track (or clip) modulators can be copied together, so an envelope keeps moving the arpeggiator it moves.
  Song modulators are copied on their own and paste as new song modulators.
- What a copied modulator moves among the copied cards follows the copies. Settings it moved that aren't in reach
  where you paste are left out, and the message says how many.
- A copy also pastes into another Roll in the same patch.

### Pictures and tags

Each picture shows one idea with a few bold shapes: grey outlines are the notes going in, shapes in the card's colour
the notes coming out. A line along its bottom says what the picture can't show, and follows the settings: the notes
an arpeggio plays on C E G, the notes of a chord on C, "A fifth up", "A canon, 1/8 behind".

The settings sit on the pictures as **tags**, pill-shaped labels with the value; the first tag on each picture is
tinted. A short word comes before the value only where the number alone would be unclear ("Gate 50%", "× 3").
Lengths read as fractions, with a small **T** for triplet and **D** for dotted. Every length setting uses the same
lengths, short to long: the straight ones from 1/64 to whole bars, and the triplet and dotted ones from 1/32 to 1/2.
Each setting offers the stretch that makes sense for it: note devices (Note Length, Arpeggiator, Note Repeat, Echo) go
from 1/64 to 8 bars, the LFO, Steps and Random modulators on to 32 bars. A setting that does nothing right now is
faint (Times and Curve while Piece is Off); one that doesn't apply is left out (Note Length shows only the setting its
Mode uses).

- **Drag** a tag left, right, up or down to step through its values. The picture redraws as you drag and you hear the
  change; letting go is one undo step. Hold **Shift** for smaller steps.
- **Mouse wheel** over a tag: one step.
- **Arrow keys**: one step on the tag you last touched.
- **Double-click** a tag to set it back to its default (a Note Filter's channels back to any). The first click opens
  the tag's panel; the second closes it again and resets the setting, as one undo step.
- On/off settings (In key, Hold) are small switches: click to flip them, double-click to set them back to their
  default.

### A tag's panel

Click or right-click a tag to open its panel: the setting's name, a box to type in, and every choice.

![The panel of Echo's Time: a box to type in, and the lengths in rows of straight, triplet and dotted](images/tag-panel.png)

- **Type** to find the closest choice: "1/8t", "3/16", "1 bar", "1.5", "reverse", "ease out". **Enter** picks it.
- Lengths are laid out in rows: straight, triplet and dotted. A playhead's Rate is laid out as faster, normal and slower,
  its Curve as drawn shapes, its Direction with small pictures.
- **Hover** a choice to see it in the picture; nothing changes until you click it. **Esc** or a click outside closes
  the panel and leaves the setting as it was.
- **Back to …** sets the default.
- **Draw a curve** opens a curve for the setting in the lower pane (see [Curves](#playheads) below), and
  **Modulate, automate…** has **Modulate with ▸**, **Remove modulation ▸**, **Automate in the timeline** and
  **Automate in this clip**. **Modulate with ▸** lists the modulators that can reach the setting; pick one and it
  moves the setting by +50%. **A new modulator…** at its end makes a new one that moves the setting by +50%. On a
  device's setting the new modulator lives on that device unless you pick another chip. The chips only offer places a
  modulator there can reach the setting from. One undo removes both.

The Note Filter's **Channels** panel has **Any** and the 16 channels to switch on and off; each click keeps the change.
You can also type a set: "1-4, 10", "any" or "all".

### When something moves a setting

- **A lane sets it:** the tag takes the lane's colour, and an **A** sits on its corner. Click the A to see the lane.
- **A modulator or a curve moves it:** a dot in the modulator's colour sits on the tag's corner.
- **How far:** a thin bar under the tag spans the setting's whole range. A lane shows the range it covers, from its
  lowest to its highest point; each modulation shows how far it reaches from where the setting is set, in the
  modulator's colour. A white dot shows where the setting is right now.
- The tag and its picture show the value as it plays now. Hover the tag to read each range in words, such as
  "Semitones: −5 st to +7 st".

## A clip's own devices

Devices belong to the track, and every clip on it plays through them. Any note clip can play through its own devices
instead: a faster arpeggio for the chorus only, a break without the echo, or a Playheads setup that belongs to one
clip. A clip with its own devices plays only through them; the track's are skipped for it. Nothing mixes.

![Chorus chords plays through its own devices: the switch at the left says CLIP, the cards wear the clip's colour](images/clip-own-devices.png)

**Switching.** The upright **TRACK | CLIP** switch at the strip's left says which devices the clip on show plays
through. Click **CLIP**: the first time, the clip gets a copy of the track's devices, with the track's modulators that
move them, so it sounds the same until you change something. Its devices then wear the clip's colour, and the section
after the switch becomes **CLIP · Chorus chords**. Hover either half to be told what it does ("Chorus chords skips the
track's devices"). Click **TRACK** to play through the track's devices again. The clip keeps its own, so you can
switch back and forth while it plays to compare. Both are undoable.

**The other chain.** The chain that isn't shown is folded into a dashed block at the end of the strip, listing its
devices:

- While the clip plays through its own, the block is the track's devices, "Skipped for Chorus chords". Click **Edit
  them** to change the track's devices from here, and click the block again to go back.
- While it plays through the track's but keeps its own, the block is "Chorus chords's own, kept, off for now". Click
  **Play through them** to switch.

**The mark.** A timeline tile of a clip with its own devices shows a tiny device card, with two tags on it, in its
title bar. It is light while the clip plays through them, and dark with a light outline while the clip keeps them but
plays the track's. A
launcher slot shows the same card, smaller on a narrow slot, at the right of the clip's name. Hover it for a reminder
of what it means.
The notes header adds "own devices" to the clip's name.

![The mark on slots and timeline tiles: Chorus chords plays its own devices (light), Verse chords keeps its own off (outlined)](images/clip-mark.png)

**From a menu.** Right-click a clip in a slot or on the timeline, or open the clip chip in the notes header:
**Devices** › **Play through the track's devices**, **Play through its own devices**, **Delete its own devices**, and
for one clip **Use its devices for the whole track** (the track's devices become a copy of the clip's, its modulators
join the track's, and the clip switches to the track's). On the timeline this works on every selected clip at once.

**What goes with it.**

- A clip's own chain has its own modulators: the section after the switch is the clip's (**CLIP · Chorus chords**)
  instead of the track's. A curve drawn on one of its settings, such as its Playheads' Rate, is a curve of the clip.
- Its settings are automated in the clip (**Automate in this clip**), so the automation travels with the clip too.
- Copying, duplicating or moving the clip, to another slot, scene or track, takes its devices along (as copies). A
  clip that plays through the track's devices takes those of the track it lands on.
- Live keys always go through the track's devices, whichever clip plays.
- Switching between clips cuts what the old clip's own chain still held (Echo repeats, held arpeggio notes), as a
  launch does. On the timeline, each clip plays through its own choice; where stacked clips overlap, the first one
  that covers a moment decides for all of them.
- MIDI export with **Devices: Applied** plays each clip through the chain it uses.
- Audio clips have no devices of their own: devices work on notes.

## Playheads

The **Playheads** device reads the playing clip with several playheads at once. Each one moves through the notes in
its own way: slower, faster, backwards, in another order, an octave down, only the lowest notes… One melody becomes a
canon, a bass and a melody, or a texture that keeps shifting.

It is the first entry in the device browser, and it sits first in the chain: only Motion devices may come before it
(see [Notes that drift](#notes-that-drift)). A track holds one. While it is on,
it replaces the clip's own playhead; turn it off with its power button and the clip plays as usual. The playheads
belong to the track: launch another clip and the same playheads read it.

It starts with two playheads, both reading the whole clip from its start. Change one's **Rate** or segment to hear
them part. Each playhead shows as a line in its colour over the notes (the one the card shows is bolder). The notes it
reads light up in its colour, and so do their keys on the piano at the left. The clip's own playhead isn't drawn:
with playheads, it doesn't say what plays.

**The card.** The chips beside its name are the playheads: click one to show it (the card takes its colour), **+** adds
one (up to 16). The dot at a chip's left end turns that playhead off and on without losing its settings: it is
hollow while the playhead is off (also while a curve or a lane has turned it off). Hover the dot or the name to be
told which does what. Alt-click the dot to solo it, or press
**Shift+M** to turn the shown playhead off and on. A playhead you turn off stops at once: the note it was playing ends
there, and the notes its own devices still had to play (an Echo's repeats) don't come. Right-click a chip to rename it,
pick its **Channel** (see [A sound for each playhead](#a-sound-for-each-playhead); the chip shows it), turn it **On**
or off, solo it, duplicate or delete it, or **Switch it on and off in time…** with a curve. Under the chips, the shown
playhead's settings on four pictures: how it moves (Direction and Rate), how its speed rises and falls (Speed curve
over a length), where it starts (Offset), and what it plays again (Piece × Times, and the Curve that spaces the
repeats):

![A Playheads card: Forward at 1×, Speed curve Ease in over 1/2, Offset 0, and Piece 1/16 × 4 with Curve Ease in, its four hits bunching up toward the end](images/playhead-repeat.png)

| Setting | What it does |
|---|---|
| **On** | Whether it plays: the dot on its chip. Right-click the chip for a curve on it, so the playhead can come in and drop out as the song plays (see **Curves** below). |
| **Rate** | Speed against the song, from 1/32x to 32x, each speed with its opposite: halves and doubles, thirds (1/3x, 2/3x, 4/3x, 3x…), 1/24x and 24x, dotted speeds (3/8x and 8/3x, 3/4x, 3/2x) and fives (4/5x, 5/4x). |
| **Direction** | Forward, Backward, Ping-pong (turns at the ends) or Freeze (stands still); or read the segment's steps in another order: **Outside in** (first, last, second, second-last…), **Inside out**, **Odd then even** or **Shuffle** (a new order each pass), with the step size (1/32 to 4 bars, triplets and dotted too). |
| **Speed curve** | Makes the playhead speed up and slow down as it goes, then catch up: over each **Over** length it follows the curve, and at the end of that length it is back where a steady playhead would be (see [Bending the speed](#bending-the-speed)). **Straight** keeps a steady speed. |
| **Over** | How long one speed curve lasts, in the song's time: 1/16 to 16 bars, triplets and dotted too. Faint while the Speed curve is Straight. |
| **Offset** | Plays this playhead's notes later (+) or earlier (−) than it reads them, by a note value: 1/64 up to a bar either way, with triplet (T) and dotted (D) values, and every 1/16 between (+5/16, +7/16…). Its line over the notes trails or leads by as much, so it crosses each note when you hear it. Two playheads on the same segment, one at **+1/4**, make a round. Double-click it to set it back to **0**. At the very start of a clip, a late playhead waits for its offset, and an early one skips the notes that would have come before the clip started. When the clip loops, notes carry over the loop as usual. A clip's end is a hard stop: a late note that would sound after the clip ends doesn't play, and an early playhead doesn't play notes from past the end. That holds for a timeline clip, and for a launched clip that doesn't loop. When a curve or a lane moves Offset later while the clip plays, the playhead waits until the new time catches up. It never plays the same notes twice. Moved earlier, it skips ahead. |
| **Piece** | The size of the piece it plays again: Off, or 1/64 up to 8 bars, with triplet (T) and dotted (D) sizes between. With **Times**, it plays each piece that many times, then moves on one piece: Piece 1/4, Times 2 plays beat 1, 1, 2, 2, 3, 3… |
| **Times** | How many times each piece plays, 2 to 16. |
| **Curve** | How the repeats are spaced in time (see [Spacing the repeats](#spacing-the-repeats)). **Straight** spaces them evenly. |
| **Rarer settings** | Right-click a playhead's chip: Notes (all, or only the **Lowest** or **Highest** of the notes that start together: a bass and a melody out of one piano roll, one playhead on Lowest with a Transpose of −2 octaves in its own chain, one on Highest), On a jump, Invert (mirrors its notes inside the clip's range, in the key), note Length (follows the playhead, the default: a note sounds while the playhead is on it, so at ½× it lasts twice as long; or as written; or fixed, 1/64 to 4 bars, triplets and dotted too), Path (from the settings, or drawn in the time map), and rename, channel, on, solo, duplicate, delete. Each of these settings' lists ends with **Draw a curve for it** and **Automate or modulate…**, so the rarer settings can move in time too. |

Pitch and loudness aren't playhead settings: put a **Transpose** (octaves, key steps) or a **Velocity** device in the
playhead's own chain, like any other note change. Songs made when a playhead had its own Octave, Steps and Velocity
get those devices added for them, curves included.

The card's **⋯** menu holds the settings every playhead shares: **Restart** (when the clip starts, or also every so
many bars) and **Same note** (two playheads on the same note at the same moment play it once, or each).

**The segment.** On top of the piano roll's ruler, the shown playhead's segment is a bar in its colour, drawn like the
clip's loop bar, with its name inside and a solid grip at each end: the part of the piano roll that playhead plays, over
and over, starting at its left end. A thin line drops from each grip through the notes, so you can see which notes it
reads. Drag a grip to resize the segment, the bar to move it. For a playhead, the piano roll is a canvas: a segment can
sit anywhere on it, past the clip's end too, and plays the notes written there (where nothing is written, it is
silent). So with playheads on, the clip's loop doesn't decide what plays: the loop bar is hidden, the inspector's
**Loop** reads **Playheads decide**, and the loop only sets where a new segment starts out. For a round, give the
second playhead an **Offset** of +1/4, or move its segment a beat later. The other playheads' segments show as thin lines in their colours.
Right-click the segment to move it or resize it as the song plays (**Move it in time with a curve…**, **Resize it in
time with a curve…**), to automate or modulate its start or length, or to make it **Follow the clip's loop** again.

**A clip played by playheads looks different.** Its written notes aren't what plays, so on the timeline and in its
launcher slot it doesn't show them. It shows one band per playhead instead, in the playhead's colour, with a block
wherever that playhead is over a note: you see how many playheads there are and the rhythm each plays. A short clip
(the timeline zoomed out, or a launcher slot) keeps the bands as thin as it must, and when even that doesn't fit, the
playheads' blocks share the clip. An off playhead's band is faint. One small stripe per playhead sits at the right
end (hollow when it is off), and the piano roll's title adds "read by 3 playheads". The bands follow the settings as
you set them; curves on them play but aren't drawn there.

**Curves.** Any setting can move by itself, on this card or any device card. Click a setting's tag and pick **Draw a
curve** in its panel: a curve for it opens in the piano roll's lower pane. On this card that is Direction, Rate,
Speed curve, Over, Offset, Piece, Times and Curve (On, and the rarer settings, from a chip's right-click menu, and the segment, from its
right-click menu): a curve on On can bring the
playhead in for bars 5 to 8 only, one on Direction can turn it round in bar 2, one on Piece can start repeating halfway
through. On has two rows: the curve on its top row leaves it on, a step down to the row below turns it off. (The step
orders in the Direction panel, such as Outside in, stay as you pick them.) Click to add a point: that makes a
**Curve** modulator on the track (see [Modulators](#modulators)), named after the playhead and the setting, such as
"Bass Rate".

- **Up and down are the setting's own values.** For Octaves or Key steps each row is one value, labelled with what
  plays, and points snap to the rows (hold **Alt** to place them between rows). The rows cover everything the setting
  can reach. The dashed line is the setting as you set it: the curve adds to it, so with the setting on +2 a point
  on the +5 row plays +5, and changing the setting moves the whole line.
- **Two kinds of point.** A square is a **Step**: it holds its value until the next point. A circle is a **Ramp**:
  it glides to the next point. A new point takes the kind of the point before it; the first point on a setting with
  rows is a Step. A ramp across rows is heard one row at a time, drawn as dotted stairs under it.
- **Picking points.** Click a point to pick it: it draws brighter, and a drag moves every picked point together, so
  a shape you built moves as one. **Shift+click** picks one more (or unpicks it). Drag an empty spot and a box
  picks the points it touches; **Shift** adds them to the picked ones. A plain click in empty space still adds a
  point, picked.
- **Step and Ramp in the row on top** make every picked point that kind, as one undo step; right-clicking a point
  does the same (it picks the point first). The count between the two says how many points are picked, and a button
  lights when every picked point is already its kind. Right-click a point also deletes it. **Delete** removes every
  picked point, after you have clicked in the curve pane. Selecting another track closes the curve.
- **Its own zoom.** The curve zooms and scrolls on its own: **Ctrl+wheel** across, **Alt+wheel** up and down, the
  wheel scrolls up and down, **Shift+wheel** across. Double-click its ruler to see the whole drawing. It opens lined
  up under the notes; right-click for **Line up under the notes** or **Show the whole drawing**.
- **Length** in the row on top says how much curve there is to draw: 1 to 256 bars. Next to it are ready **Shapes**
  (drawn across the segment and the rows shown), then **Moves ▾** with what the curve moves (click it to take a
  setting off the curve, map it to more knobs, or send it out as CV), **Remove curve** and a close button that puts
  the note lane back. Drag the pane's top edge to make it taller or shorter; it keeps the height you set. Double-click the edge to
  go back to the default height.
- **The segment says what plays.** On the curve's ruler, a bar drawn like a playhead's segment marks the part that
  plays, such as "plays 4 bars". Drag its grips to resize it, or the bar to move it; dragging past the drawing's
  end grows **Length** with it. It loops: when the clip starts
  (launched, or reached on the timeline), the curve plays from the segment's start and goes round. Draw 16 bars and
  play bars 5 to 8, or draw 100 bars with the segment covering them all for a slow change over the song. A new curve
  is as long as the clip, with the segment covering all of it.

Drag points, double-click to delete one. A tag that a curve moves has a dot in the curve's colour on its corner and a
bar under it showing how far the curve takes it. While the song
plays, a playhead line crosses the curve where it reads now.

![A curve on the Bass track's Key steps: 16 bars drawn, bars 5 to 8 play, two ramp points picked](images/curve-pane.png)

**Moved notes.** When the shown playhead plays notes at another pitch (its Invert, or a Transpose in its own chain,
and the curves on them), the piano roll shows where they go: each moved note appears again at the pitch it plays, in the playhead's
colour, with the change in semitones ("+2 st"). The written note it came from turns see-through with a dashed outline.
While the song plays this follows the pass playing now, so a curve on the Transpose shows each note moving from
pass to pass; stopped, it shows the first pass. The notes you edit stay where they are written.

On a drum clip whose lanes have their own length, the segment is the whole lane cycle, and each lane repeats on its own
length under every playhead. Note operators (chance, repeats…) and note curves work as usual; each playhead makes its
own chance choices.

**When a note plays.** Going forward, a playhead plays a note when it reaches the note's start. Going backward, it plays
it when it reaches the note's end, so the order of the notes is reversed but each note still sounds normally. A
playhead jumps when it goes back for a repeat, moves to the next repeated piece, wraps to its segment's start, moves
to the next step of an order, restarts, or steps along a drawn path (a ping-pong turn isn't a jump). What happens then
is the playhead's **On a jump** setting, in its **⋯** menu: **Retrigger** (the default) stops the note it was playing
and plays again any note it lands inside, for what is left of it, so a long note under a 1/16 repeat pulses in
sixteenths; **Let ring** lets the note it was playing ring on and plays only notes that start where it lands.

Every setting can also be moved by a modulator or an automation lane, as on any device. Moving **Rate** while it plays
speeds the playhead up or slows it down from where it is.

A clip read by playheads shows one band per playhead, with a block wherever that playhead is over a note, so you can see
how many there are and the rhythm of each. When the clip is too short for bands, the playheads' paths are drawn instead.

Playheads read launched clips and timeline clips alike. On the timeline, each clip starts its playheads again, from
where that clip begins (a clip that starts partway in starts the playheads partway in), and a stretched clip slows them
down. MIDI export with **Devices: Applied** writes what the playheads play, each playhead on its channel; **Bypassed**
writes the clips as written. Modulators on playhead settings go into the file when they follow the song alone: synced
curves and LFOs, Steps, Random, Ramp and Macro. A free-running curve or LFO, an Envelope or a Note modulator plays live
only.

### Bending the speed

The second picture on the Playheads card bends the playhead's speed, the way a player pushes and pulls the time. Pick a
**Speed curve** and how long it lasts with **Over**: with **Ease in** over **1/2**, the playhead starts each half bar
slowly and speeds up to catch up, so the notes in each half bar come late at first and crowd in at the end. **Ease out**
rushes ahead first and then waits; **Ease in-out** is slow at both ends and fast between; **Swing** lingers, then
hurries; **Stairs** surges four times. Whatever the shape, at the end of each **Over** length the playhead is exactly
where a steady one would be, so it stays in step with the song and with the other playheads.

The picture shows eight notes as the playhead meets them in one length, with dots under them where a steady playhead
would meet them, and the curve its place follows. The caption says what it does: "Starts slow, catches up each 1/2".

**Over** counts in the song's time, not the playhead's: at 2× a playhead still bends over half a bar when Over is
1/2. The lengths start again where the clip starts (and at every **Restart**). With **Length → Follows playhead**,
a note lasts while the playhead is on it, so notes it crosses slowly last longer. A Speed curve works together with
Rate, Direction, the Piece repeats and their Curve, and the step orders. A drawn path (Path › Drawn) ignores it, as it
ignores the other movement settings.

### Spacing the repeats

The fourth picture on the Playheads card shows where the repeats of one group land: Piece 1/16, Times 4 is four hits in
a beat.
**Curve** keeps the same hits in the same time, but spaces them differently. Eight repeats over eight seconds come one
a second when the Curve is **Straight**; with **Ease in** they start far apart and bunch up at the end, like a
bouncing ball; with **Ease out** they start bunched up and slow down. The first hit of each group stays where it was,
so the next group still lands on time. Dots under the hits show where they would land straight.

When a curve, another modulator or a lane moves Piece, Times or Curve, the picture shows them as they play now, and so
do their tags.

Click the Curve tag for its panel; each shape is drawn. **Ease in** and **Ease out** come soft, medium and
hard; then **Ease in-out** (slow, fast, slow), **Swing light** and **Swing heavy** (the first half lingers) and
**Stairs** (slow-fast steps). With Piece off, Times and Curve are faint: there is nothing to space.

With **Length → Follows playhead** (the default), a held note lasts until the next repeat. A drawn path (a chip's right-click menu ›
Path › Drawn) replaces the repeats and their spacing.

### The time map

In the piano roll's lower pane, pick **Time map (playheads)** from the pane's menu. Each playhead gets a row: across is
time since the clip started, lined up with the bars above; up is where in its region the playhead reads. A rising line
is a normal playhead, a steeper one is faster, a falling one goes backwards, a flat one is frozen, a staircase repeats,
and a jump is a wrap.

To draw a path, right-click a playhead's row and pick **Draw this path**: it starts from where the settings take the
playhead, and its cycle becomes as long as the clip.

- **Click** in the row to add a point, and **drag** a point to move it (it snaps to the grid; hold Alt to place it
  freely).
- **Double-click** a point to delete it.
- **Right-click** the row for **Back to settings**, **Clear points** and **Path cycle** (1/4 bar to 64 bars).

Between two points the playhead moves straight from one place to the other, and plays the notes it crosses on the way,
in either direction. A drawn path says where the playhead is at every moment.

### Devices for one playhead

Right after the Playheads card come the shown playhead's own devices: devices only that playhead's notes go through,
before the rest of the chain. They sit in a frame in the playhead's colour, with "Melody only" (its name) written down
its side; the cards inside take that colour too. Click the round **+** at the frame's end to add one (up to 4 per
playhead).

![Melody's own devices: an Echo in a frame in Melody's colour, after the Playheads card](images/playhead-own-devices.png)

A playhead without its own devices shows a slim dashed slot with a **+** and "Bass only" down its side. Click it to
add the first one.

![The Bass playhead has no devices of its own: a slim dashed slot after the Playheads card](images/playhead-empty-slot.png)

 Give the slow reversed playhead an
Echo, or move the fast one up an octave with Transpose, while the devices after them still shape every playhead. Click another chip to see its branch. Branch devices count toward the
track's 16 devices; their cards stay in their branch (they can't be dragged out).

### A sound for each playhead

Each playhead puts its **channel** on the notes it plays: Playhead 1 on channel 1, Playhead 2 on channel 2, and so on.
Right-click its chip and pick **Channel** to change it, or **Each note's own** to keep the channels the notes were
written on.
A Track Out plays **All channels** unless you pick one. To give Playhead 2 its own sound, add a second Track Out for the
same track, click its screen and pick **Channel 2**, and patch it to another voice. See
[Track Out](02-modules.md#track-out). On a drum track, Drum Out has the same picker: the reversed hats can go to one
voice and the straight ones to another.

### Notes that drift

A **Motion** device moves some of your notes while the clip plays: a few notes rise one step each pass and fall back,
a note slides later and later, velocities swell. The written notes never change; the motion plays on top of them.

**Which notes.** Select notes in the piano roll, right-click one and choose **Motion › New motion**. That adds a Motion
device, named "Motion 1" in a colour of its own, and the notes join it. A small corner in that colour marks them. To
add more notes later, select them and click **+ Selected** on the card; to take notes out, select them and click
**− Selected**. The chip beside them (**6 notes**) selects the card's notes in the piano roll. The inspector's
**MOTION** line says which Motion device the selected notes move with, and opens the same choices. Notes from any clip
on the track can join the same Motion device.

**Its settings.** **Pitch** (steps, in the key unless **In key** is off), **Time** (a delay in 1/16 steps), **Length**
and **Velocity**. To make them change, click a tag and pick **Draw a curve**: its curve opens in the lower pane (see
[Modulators](#modulators)). For one value each pass of the clip ("up one step a pass for four passes, then back"),
click a tag, then **Modulate, automate…** › **Drift per pass**: a **Steps** modulator set to **Per pass**, one step each pass. Any modulator
or automation lane can move these settings too.

**Seeing it.** A note a Motion moves shows where it plays, in the Motion's colour: the written note turns see-through
with a dashed outline, a thin line runs from its middle (a dot) to the middle of the note where it sounds, and that
note says what changed ("+7 st", "+2 steps", or both). It sits at the new pitch, starts later by the Time setting and
is as long as Length makes it. With curves on its settings it follows them: with playheads (which can read the note at
any moment) it is where the curves are right now, moving as the song plays; without, it is the pass playing now (the
first one when stopped). When the note moves out of view, a marker at the top or bottom edge
of its column says how far ("▼ −7 st").

**Where it sits.** A new Motion device goes before the Playheads device, the only place in front of it:

- **Before Playheads:** the notes move inside the clip, and every playhead reads them where they moved to. A note
  pushed later is reached later by a playhead going forward, earlier by one going backward, sooner by a fast one.
  Passes are the clip's, so every playhead hears the same drift.
- **After Playheads** (drag the card there): each note moves as a playhead plays it, and each playhead counts its own
  passes, so the melody and the bass drift apart.
- **In one playhead's own devices:** only that playhead's notes move.

Without playheads, a Motion device moves its notes wherever it sits in the chain. MIDI export with devices applied
plays the motion.

## Modulators

Modulators live in three places, and each place has the same kind of **section**:

- **SONG**, at the strip's left: for every track, for example one LFO that sweeps several tracks together. The same
  list shows whichever track is selected.
- **TRACK · Chords**, after the TRACK | CLIP switch: it can move any setting of this track's devices. While the clip
  plays through its own devices, this section is **CLIP · Verse chords** and holds the clip's modulators.
- **DEVICE**, on each card's left edge: it can move only that device's settings, and goes with the device when you
  move or duplicate it.

Click a section's **+** to add a modulator there: where you click decides what it can reach. Each section has a fold
arrow at its top left. Folded, a section is a thin strip with a dot per modulator and a small upright fader per Macro.
The SONG and TRACK sections start folded: click the arrow or the icon next to it to open one, or click its **+** to
add a modulator, which opens it.

Each folded strip has an icon at its top: SONG shows clips spread over time, TRACK a lane, CLIP a block with notes.
The TRACK | CLIP switch shows the same icons. When the pane is tall enough, the names show under the icons too. In a
short pane only the icons show, the dots get smaller and sit in rows, and in the shortest pane the **+** goes: open
the strip to add a modulator there. A card's DEVICE section starts folded until the device has modulators. Opening a section makes room for it:
the cards move right and the strip scrolls, but a device never gets narrower.

![The modulator browser](images/modulator-browser.png)

| Modulator | What it makes | Settings |
|---|---|---|
| **LFO** | A wave that keeps moving. | Shape (Sine, Triangle, Saw up, Saw down, Square, Sample & hold, Smooth random), Sync, Rate, Phase, Bipolar, Restart (Free, Each bar, Each note). |
| **Envelope** | Rises and falls with each note. | Attack, Decay, Sustain, Release. |
| **Steps** | A pattern of values you draw (1 to 16 steps). | Steps, Rate, Smooth, Bipolar. |
| **Random** | A new random value at a steady rate. | Rate, Smooth, Bipolar. |
| **Ramp** | Rises or falls over several bars: builds and fades. | Length (bars), Direction, Then (Hold or Loop), Curve. |
| **Macro** | One value you turn (or automate) that moves many settings at once. | Value. |
| **Note** | Follows the last note. | Follows (Velocity, Pitch, Release, Timbre, Pressure), Smooth. |
| **Curve** | A shape you draw, of any length; the part its segment marks plays and repeats. | None: Length and the segment sit over the drawing. |

**Curve** opens its drawing in the piano roll's lower pane: click its row (click it again to put the notes back), or
pick it in the lower pane's menu under **Curves**. A Curve has no settings column: what it moves, **Map to more knobs** and
**CV out…** are under **Moves ▾** in the pane's top row, and in its row's right-click menu. It plays the part of the drawing its segment marks, from when its track's clip starts, round and round
(a song modulator counts from the song's start). The pane works as described under [Curves](#playheads) above. For a
wave that runs at its own speed in Hz, use an **LFO**. The dashed line means "no change": above it moves the setting
up, below it down. Up and down are the values of the first setting it moves, which it turns all the way (its row in
**Moves** says **as drawn**, with no amount); other settings it moves follow by their own amounts.

Each row shows the modulator's colour, its name, a small live graph, and a **↗** arrow. Click a row to open its
settings in a column right of its section (a Curve opens its drawing instead, see above); click it again, or the
column's ◂, to close it. A DEVICE modulator's settings open inside its card. The strip makes room for the column, so it
never covers the notes:

![An LFO's settings](images/modulator-popover.png)

The **MOVES** list in the column shows every setting the modulator moves, with its amount and a **×** to remove it.
Right-click a row to rename or remove the modulator.

**A Macro's row is a fader.** Drag it sideways to set it (Shift for fine steps), scroll over it for 1% steps (Ctrl:
finer), double-click to set it back. Click it without dragging to open its settings. In a folded section, drag its
small upright fader up or down. While the track is armed and recording, a drag records into the Macro's lane.

### Connecting a modulator to settings

1. Open its section with the fold arrow, then click the modulator's **↗** (or **Map to knobs** in its settings column; for a Curve, **Moves ▾** › **Map to more knobs**).
2. The tags it can reach light up in its colour; the others fade. A message says "Mapping LFO · drag a lit tag up or
   down".
3. **Drag a lit tag** up for a positive amount, down for a negative one. The bar under the tag shows how far it will
   reach. A click adds +50%.
4. Press **Esc**, or click empty strip space, to finish.

![Mapping an LFO: the tags it can reach are lit](images/device-mapping.png)

A setting takes up to four modulations. Modulation adds to the setting's own value (or to its automation). The Note
Filter's Channels are only ever picked by hand: modulators, lanes and curves can't move them.

### Sending a modulator out as CV

In the settings column, **CV out ▾** sends the modulator out of Roll (for a Curve: **Moves ▾** › **CV out…**, or right-click
its row):

- To an **AUTO output** of a track: the value is added to that AUTO channel on its Track Out, Drum Out and Auto Out.
- To **a knob in the rack**: touch any knob, as when mapping automation. Its range can be set.

So one LFO can move an arpeggiator's gate and a filter in your patch together.

## Automating a device setting

Click or right-click a tag, pick **Modulate, automate…** in its panel, and choose:

- **Automate in the timeline**: a lane under the track, in song time. Its header reads, for example, "Arpeggiator ·
  Gate".
- **Automate in this clip**: a lane in the piano roll's lower pane, looping with the clip.

A new lane starts flat at the setting's value, so nothing changes until you draw. Once a setting has a lane, its tag
takes the lane's colour and shows an **A**: click the A to see the lane (the menu also says **Show its lane**). Edit
the lane like any automation lane ([Automation](12-automation.md)); its values are shown in the setting's own units.

- Setting lanes play only while the transport runs. When stopped, the setting is free to change and try out.
- A tag held with the mouse wins while you hold it, then the lane takes over again.
- Arm the track and record to capture your moves (Touch, Latch, Write). A clip's setting lane records while that
  clip plays.
- Setting lanes don't use an AUTO channel. A track or clip can have 16 of them.
- If the device is deleted, the lane says **Target missing** until you undo.

## Live playing and recording

Notes played live (MIDI, CV, computer keyboard) go through the armed track's devices, even while stopped, so you hear
the arpeggiator as you play. Recording keeps the notes as you played them, before the devices.

## Devices and export

MIDI and DAWproject export have a **Devices** choice: **Applied** writes what the chain plays (each arpeggio step
becomes a note), **Bypassed** writes the clips as written. See [Files](14-files.md#exporting-midi).
