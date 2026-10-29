# 13. Devices and modulators

**Devices** are note effects. They change a track's notes on their way out: an arpeggiator plays chords one note at a
time, an echo repeats notes, a chord device turns one note into three. The notes in your clips don't change; you can
switch a device off at any time.

**Modulators** (LFOs, envelopes, steps…) turn device knobs by themselves, so the music keeps moving.

## The device strip

Click **Devices** in the notes header (or press **F3**) to open the strip above the notes. It shows the selected
track's devices from left to right, in the order the notes flow through them: **in** → devices → **out**.

![The device strip: an LFO, an Arpeggiator and an Echo on the Chords track](images/device-strip.png)

When the strip is closed, a track with devices shows a thin bar naming its chain ("Arpeggiator → Echo"). Click it to
open the strip. A track without devices keeps the strip closed.

## Adding devices

Click **Add device** at the end of the chain, or the **+** between two devices. A searchable list opens.

![The device browser](images/device-browser.png)

- Type to filter by name or by purpose: "delay" finds Echo, "loose" finds Humanize.
- **Up / Down** choose, **Enter** adds, **Esc** closes.
- The footer says where the device will go.

A track holds up to 16 devices.

## The devices

| Device | What it does | Its knobs |
|---|---|---|
| **Transpose** | Moves notes up or down. | Semitones, Octaves, Key steps (moves within the key). |
| **Scale** | Keeps every note in a key. | Key from (Clip, Song, Fixed), Root, Scale, Notes off key (Nearest, Up, Down, or Drop them). Root and Scale hide unless Key from is Fixed. |
| **Chord** | Turns each note into a chord. | Chord, In key, Inversion, Voicing (Close, Open, Drop 2), Strum. |
| **Note Length** | Makes notes shorter, longer or legato. | Mode (Scale, Fixed, Legato), Scale %, Length, Overlap. |
| **Arpeggiator** | Plays held notes one at a time. | Pattern (Up, Down, Up-down, Down-up, Random, As played, Chord), Rate, Gate, Octaves, Swing, Velocity (As played, Fixed, Accent), Hold. |
| **Echo** | Repeats notes, fading out, like a delay. | Time, Repeats, Decay, Pitch (per repeat), Gate. |
| **Velocity** | Makes notes softer, harder, or keeps them in a range. | Curve, Min, Max, Random. |
| **Humanize** | Loosens timing, velocity and length a little, differently on each pass. | Timing (only ever later), Velocity, Length. |
| **Random** | Changes pitches and drops notes, differently on each pass. | Pitch (steps), In key, Drop (chance to skip a note). |
| **Groove** | Swings this track its own way, in place of the song's swing: a straight bass under swung drums, or extra-swung hats. Turn its Swing with an LFO or a lane to ride it. A new Groove starts on the grid most of the track's off-beat notes sit on. | Swing (50% straight to 75%), Grid (1/16 or 1/8). |
| **Note Filter** | Lets only some notes through. | Lowest, Highest, Min velocity, Max velocity, Every (play every Nth note), Offset, Channel (Any or 1 to 16). |

**Playheads** is different from the others: it doesn't change notes, it reads the clip. See
[Playheads](#playheads) below.

**Order matters.** Chord → Arpeggiator arpeggiates the chord. Arpeggiator → Chord makes a chord on every arpeggio step.

Random choices are repeatable: the same pass plays the same way every time, also in a MIDI export.

On **drum tracks**, devices that change pitch (Transpose, Scale, Chord, Random pitch) are skipped by default, because a
pitch is a drum lane there. Turn on **Affect drum lanes** in the device's ⋯ menu to use them anyway.

The track's **Voices** setting (Auto) follows its devices: a chord device adds voices, an arpeggiator plays one at a
time.

## Using a device card

- **Power button** (top left): bypass the device. The notes pass through unchanged.
- **⋯ menu**: rename, duplicate, delete, reset knobs, affect drum lanes, add a modulator to this device.
- **Drag the card's header** to move it in the chain. An orange line shows where it will land. **Ctrl+drag** copies it.
- Click a card, then **Delete** removes it and **Ctrl+D** duplicates it.

### Knobs

- **Drag** up or down (Shift for fine steps). You hear the change while you drag; it is one undo step.
- **Double-click** resets the knob.
- **Click the value** to type it.
- **Right-click** for: Reset, Type a value, **Modulate with ▸**, **Remove modulation ▸**, **Automate in the
  arranger**, **Automate in this clip**.
- Choices (like Pattern) are small menus. Switches (like Hold) flip on a click.

A knob draws an orange arc for its value. Each modulation adds a ring in the modulator's colour, showing how far it can
push the knob, and a white dot shows where the knob is right now.

## Playheads

The **Playheads** device reads the playing clip with several playheads at once. Each one moves through the notes in
its own way: slower, faster, backwards, bouncing, or repeating a short piece a few times. One melody becomes a canon,
or a texture that keeps shifting.

It is the first entry in the device browser, and it always sits first in the chain. A track holds one. While it is on,
it replaces the clip's own playhead; turn it off with its power button and the clip plays as usual.

It starts with two playheads: **Playhead 1** plays the clip as before, **Playhead 2** starts a beat later, so you hear a
canon at once. Each playhead shows as a line in its colour over the notes; the clip's own playhead stays as a faint
grey line.

- **The chips** at the top of the card are the playheads. Click one to show its settings. **+** adds a playhead (up to
  16), each starting a beat after the last.
- **Right-click a chip** to rename it, pick its **Channel**, mute or solo it, duplicate or delete it.
- The number at the right of a chip is its channel.

A playhead's settings:

| Setting | What it does |
|---|---|
| **Rate** | Speed against the song, from 1/16x to 16x. |
| **Direction** | Forward, Reverse, Ping-pong (turns at the ends), or Freeze (stands still and plays nothing). |
| **Start** | Where it starts, in 1/16 steps from the start of its region. |
| **Region start**, **Region length** | The part of the clip it loops over, in 1/16 steps. A length of 0 means the whole clip. |
| **Repeat**, **Repeat times** | Plays each piece of this length a number of times, then moves on one piece. 1/4 and 2 times plays beat 1, 1, 2, 2, 3, 3… |
| **Bend**, **Bend amount**, **Bend cycle** | Speeds up and slows down inside each cycle, without moving the cycle's ends: Ease in, Ease out, Ease in-out, Swing, Stairs. The cycle is counted in the playhead's own time: at 2x a 1/4 cycle passes in an eighth. |
| **Offset** | Moves the playhead forward or back, in 1/16 steps. Good to automate. |
| **Length**, **Fixed length** | How long its notes last: As written, Follows playhead (half speed, twice as long), or a Fixed length. |

The device's own knobs, after a small gap:

| Setting | What it does |
|---|---|
| **Restart**, **Restart bars** | Every playhead goes back to its start when the clip starts, or also every so many bars. |
| **Same note** | When two playheads play the same note at the same moment: play it once, or play each. |

On a drum clip whose lanes have their own length, the region is the whole lane cycle, and each lane repeats on its own
length under every playhead. Note operators (chance, repeats…) and note curves work as usual; each playhead makes its
own chance choices.

**When a note plays.** Going forward, a playhead plays a note when it reaches the note's start. Going backward, it plays
it when it reaches the note's end, so the order of the notes is reversed but each note still sounds normally. A
playhead that jumps (a repeat, a restart) into the middle of a note doesn't start it.

Every setting is a knob: modulators can move it, and it can be automated, as on any device. Moving **Rate** while it
plays speeds the playhead up or slows it down from where it is.

Playheads read launched clips and timeline clips alike. On the timeline, each clip starts its playheads again, from
where that clip begins (a clip that starts partway in starts the playheads partway in), and a stretched clip slows them
down. MIDI export with **Devices: Applied** writes what the playheads play, each playhead on its channel; **Bypassed**
writes the clips as written.

### Devices for one playhead

Right after the Playheads card, the shown playhead's **branch** sits on a band in its colour: devices only that
playhead's notes go through, before the rest of the chain. Click the **+** on the band to add one (up to 4 per
playhead). Give the slow reversed playhead an Echo, or move the fast one up an octave with Transpose, while the devices
after the band still shape every playhead. Click another chip to see its branch. Branch devices count toward the
track's 16 devices; their cards stay in their branch (they can't be dragged out).

### A sound for each playhead

Each playhead puts its **channel** on the notes it plays: Playhead 1 on channel 1, Playhead 2 on channel 2, and so on.
A Track Out plays **All channels** unless you pick one. To give Playhead 2 its own sound, add a second Track Out for the
same track, click its screen and pick **Channel 2**, and patch it to another voice. See
[Track Out](02-modules.md#track-out). On a drum track, Drum Out has the same picker: the reversed hats can go to one
voice and the straight ones to another.

## Modulators

The **MODULATORS** list on the left of the strip holds the modulators. Click **+ Add** to make one. A chip row at the
top chooses where it lives:

- **Whole song**: one modulator for every track, for example one LFO that sweeps several tracks together.
- **The track**: it can turn any knob of this track's devices.
- **A device**: it can turn only that device's knobs.

![The modulator browser](images/modulator-browser.png)

| Modulator | What it makes | Settings |
|---|---|---|
| **LFO** | A wave that keeps moving. | Shape (Sine, Triangle, Saw up, Saw down, Square, Sample & hold, Smooth random), Sync, Rate, Phase, Bipolar, Restart (Free, Each bar, Each note). |
| **Envelope** | Rises and falls with each note. | Attack, Decay, Sustain, Release. |
| **Steps** | A pattern of values you draw (1 to 16 steps). | Steps, Rate, Smooth, Bipolar. |
| **Random** | A new random value at a steady rate. | Rate, Smooth, Bipolar. |
| **Ramp** | Rises or falls over several bars: builds and fades. | Length (bars), Direction, Then (Hold or Loop), Curve. |
| **Macro** | One knob you turn (or automate) that moves many knobs at once. | Value. |
| **Note** | Follows the last note. | Follows (Velocity, Pitch, Release, Timbre, Pressure), Smooth. |

Each row in the list shows the modulator's colour, its name, where it lives, a small live graph, and a **↗** arrow.
Click a row to open its settings:

![An LFO's settings](images/modulator-popover.png)

The **MOVES** list in the popover shows every knob the modulator turns, with its amount and a **×** to remove it.
Right-click a row in the list to rename or remove the modulator.

### Connecting a modulator to knobs

1. Click the modulator's **↗** (or **Map to knobs** in its popover).
2. The knobs it can reach light up. A message says "Mapping LFO · drag a lit knob up or down".
3. **Drag a lit knob** up for a positive amount, down for a negative one. A click adds +50%.
4. Press **Esc**, or click empty strip space, to finish.

![Mapping an LFO: the knobs it can reach are lit](images/device-mapping.png)

A knob takes up to four modulations. Modulation adds to the knob's own value (or to its automation).

### Sending a modulator out as CV

In the popover, **CV out ▾** sends the modulator out of Roll:

- To an **AUTO output** of a track: the value is added to that AUTO channel on its Track Out, Drum Out and Auto Out.
- To **a knob in the rack**: touch any knob, as when mapping automation. Its range can be set.

So one LFO can move an arpeggiator's gate and a filter in your patch together.

## Automating a device knob

Right-click a knob and choose:

- **Automate in the arranger**: a lane under the track, in song time. Its header reads, for example, "Arpeggiator ·
  Gate".
- **Automate in this clip**: a lane in the piano roll's lower pane, looping with the clip.

A new lane starts flat at the knob's value, so nothing changes until you draw. Once a knob has a lane, the menu says
**Show its lane** instead. Edit the lane like any automation lane ([Automation](12-automation.md)); its values are
shown in the knob's own units.

- Knob lanes play only while the transport runs. When stopped, the knob is free to turn and try out.
- A knob held with the mouse wins while you hold it, then the lane takes over again.
- Arm the track and record to capture your knob moves (Touch, Latch, Write). A clip's knob lane records while that
  clip plays.
- Knob lanes don't use an AUTO channel. A track or clip can have 16 of them.
- If the device is deleted, the lane says **Target missing** until you undo.

## Live playing and recording

Notes played live (MIDI, CV, computer keyboard) go through the armed track's devices, even while stopped, so you hear
the arpeggiator as you play. Recording keeps the notes as you played them, before the devices.

## Devices and export

MIDI and DAWproject export have a **Devices** choice: **Applied** writes what the chain plays (each arpeggio step
becomes a note), **Bypassed** writes the clips as written. See [Files](14-files.md#exporting-midi).
