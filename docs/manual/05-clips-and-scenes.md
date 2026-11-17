# 5. Clips and scenes

A **clip** is a short piece of music on one track, such as a 4-bar bass line. Clips loop. You start (launch) them
while the music plays. A **scene** is a column of clips, one per track, that
you launch together: "Intro", "Verse", "Chorus".

## The clips panel

The clips sit between the track headers and the timeline. Each **row** is a track. Each **column** is a scene, with
the scene's ▶ and name on top.

![Clips playing: the Chorus scene is running](images/clips-playing.png)

A slot can look like this:

| Slot | Meaning |
|---|---|
| Dark, sunken, with a faint ■ | Empty. Click the ■ to stop the track. |
| Coloured, with a name and ▶ | A clip, ready to launch. |
| Bright, with a ring around ▶ | Playing. The ring fills up as the clip loops. |
| Blinking yellow ▶ | Waiting to start at the next launch line. |
| Red | Recording into this slot. |

![Verse queued: its triangles blink yellow until the next bar](images/clips-queued.png)

**F2** widens the clips to fill most of the song pane, for performing. Press it again to narrow them. You can also
drag the clips' right edge. **Shift+wheel** scrolls through scenes.

## Making clips

- **Double-click an empty slot.** A one-bar clip appears, is selected, and opens in the piano roll.
- Or select a track and **double-click the piano roll**.
- New clips are called "Clip 1", "Clip 2" and so on. Rename them from the clip's menu.
- A new clip on a track that plays nothing starts launched. Press play and you hear it.

## Launching and stopping

- **Click ▶ in a slot** to launch that clip.
  - While playing, it waits for the next **launch line** (by default the next bar), blinking yellow. Then it starts
    exactly on time.
  - While stopped, it starts at once and starts the transport.
- **Click ▶ on a scene's header** to launch the whole column. Tracks with an empty slot in that scene stop.
- **Click the ■ in an empty slot** to stop that track (at the launch line).
- **Stop all** (the ■ next to "Launch 1 bar" above the track names) stops every clip at the launch line.
- Only one clip plays per track. Launching another one replaces it at the launch line.
- A clip with looping off plays once, then the track stops.

When you press play later, the clips that were playing start again.

## Launch quantize: when clips start

Click **Launch 1 bar ▾** (above the track names) to choose the launch line.

![The launch quantize menu](images/launch-menu.png)

- **None** starts clips at once.
- **1/16**, **1/4**, **1 bar** (default), **2 bars**, **4 bars** wait for that line. Lines follow the song's bar
  numbers.
- A clip can have its own setting: right-click it, **Launch quantize**. "Song setting" uses the menu above.
- Clicking a ruler to move the playhead while playing waits on the same lines, landing on the beat you clicked.
  See [The editor](03-editor.md#the-ruler-loop-and-markers).
- **Record into timeline** writes what you launch onto the timeline. See [The timeline](06-timeline.md#recording-what-you-launch).
- **Next actions** (on by default) lets clips move on by themselves. Turn it off to hold every clip where it is. See
  [Next actions](#next-actions-clips-that-move-on-by-themselves).

## Editing clips

- **Click a clip** to show it in the notes pane below.
- **Drag** a clip to another slot to move it. Hold **Ctrl** or **Alt** to copy it. Dropping on a slot that has a clip
  replaces that clip.
- **Drag a clip onto the timeline** to place it there. The timeline copy is **linked**: change the notes in one and
  both change.
- **Drag a timeline clip onto an empty slot** to link it into that slot.
- **Delete** removes the last clicked clip. **Ctrl+D** copies it into the slot below (a new scene is added if that
  slot is taken).

### The clip menu

Right-click a clip in a slot:

| Item | What it does |
|---|---|
| **Launch** | Same as its ▶. |
| **Edit this scene's clips together** | On a note clip: shows the other clips of the scene dimmed in the piano roll, so you can write parts that fit. See [The piano roll](07-piano-roll.md#editing-several-clips-together). |
| **Rename** | A new name for the clip. Every place that uses the clip shows it. |
| **Colour** | **Track colour** (the default: it follows the track) or one of 16 colours, for the slot and every copy of the clip on the timeline. Handy to tell takes apart, such as record, overdub and play clips. Notes keep the track's colour. |
| **Devices** | Whether the clip plays through the track's devices or its own, and deleting its own (see [A clip's own devices](13-devices.md#a-clips-own-devices)). A slot with its own devices shows a small dot in its top-right corner (a wide slot shows a small device card instead). |
| **Place on the timeline at the playhead** | Puts a linked copy on the timeline at the playhead. |
| **Duplicate**, **Delete** | As above. |
| **Export as MIDI…** | Saves this clip as a MIDI file. |
| **Loop** | On: the clip repeats. Off: it plays once and stops. |
| **Launch quantize** | This clip's own launch line. |
| **Next action** | What the clip does by itself after a few loops: play the next clip, a random one, and more. See [Next actions](#next-actions-clips-that-move-on-by-themselves). |
| **Length** | **×2 longer** (half speed), **×½ shorter** (double speed), or a typed percentage. Notes, loop and automation all stretch, and every timeline use grows or shrinks with it. |
| **Save as / Replace with Tessera clip file…** | A `.tessera-clip` file, to reuse a clip in another patch. |

On a note clip, **Bounce notes to a new track…** makes a new clip from what it plays, on a new track in the same scene.

Right-click an **empty slot** for: **Create clip**, **Import MIDI file…** and **Load a Tessera clip file…** (note
and drum tracks), **Import audio file…** (audio tracks), **Stop the track now** and **Has a stop button**. An empty
slot normally has a stop button (the small square): launching its scene stops that track. Turn **Has a stop button**
off and the slot is blank: its scene leaves that track playing whatever it plays, and
clicking the slot does nothing. Use it for a part that should keep going while other scenes change.

### Clip length and loop

The clip chip at the left of the notes header ("Lead · Hook · 4 bars ▾") changes the clip shown below:

- **Rename…**, **Loop** on or off.
- Lengths of **1, 2, 4, 8 or 16 bars**.
- **Double**: twice as long, with the notes copied into the new half (great for building variations).
- **Halve**: half as long.
- **Save as Tessera clip file…**, **Export as MIDI…**.
- **Track**: jump to the previous, next or any track.

In the piano roll's ruler, the coloured loop bar shows the clip's loop. Drag its ends to change it. If you draw notes
past the end of a looping clip, the clip grows to hold them, in whole bars.

## Next actions: clips that move on by themselves

A clip can launch another clip by itself once it has played for a while. For example: play the bass line 4 times,
then the next one. Or: after every loop, pick a lead line at random. Your song plays itself, and changes as it goes.

Right-click a clip → **Next action**, and pick what it does:

![The Next action menu of a clip](images/next-action-menu.png)

| Action | What happens |
|---|---|
| **Keep looping** | Nothing (the default). The clip loops until you launch something else. |
| **Play next** | The clip to the right, skipping empty slots. After the last clip, the track stops. |
| **Play previous** | The clip to the left. Before the first clip, the track stops. |
| **Play first**, **Play last** | The first or the last clip on the track. |
| **Play random** | Any clip on the track. It can be the same clip again: then it starts over. |
| **Play other** | A random clip, but never the one that just played. |
| **Round robin** | The next clip, and after the last one the first again. The track goes round and round. |
| **Go back** | The clip that played before this one. |
| **Stop** | The track stops. |

In the same menu:

- **After**: how long the clip plays first. Count **loops of the clip** (the default is 4) or **bars**. A clip with
  looping off plays once, then does its action.
- **How often**: **Always**, or a chance such as **75%**. The rest of the time the clip does its **Otherwise** action
  instead. For example: 75% **Play next**, otherwise **Play other**. With **Keep looping** as the Otherwise action,
  the clip plays another round and rolls again.
- **Pick from**: the **whole track**, or **this block**. A block is a run of clips with an empty slot on each side. So
  one track can hold two groups of clips, for example verse parts and chorus parts, and the next actions stay in
  their own group.

The inspector (**I**) shows the same settings under **NEXT ACTION** when a clip in a slot is selected, with a
sentence that says what will happen, such as "Plays 4 times, then Bass 2."

**On the slot.** A clip with a next action shows a small tag at its right: the action's icon and its count, such as
→ **4** (4 loops) or ↻ **8 bars**. While the clip plays, the tag counts: **2/4** means the second of four loops. One
launch line before the action, the clip that comes next blinks yellow, as a launched clip does. So even a random pick
shows before it plays. A narrow slot shows the icon alone.

**You stay in charge.** Launching a clip yourself always wins over a next action. To hold everything where it is,
turn off **Next actions** in the **Launch 1 bar ▾** menu. The tags fade, and every clip keeps looping until you turn it
back on.

Next actions work in the launcher. A track that plays the timeline doesn't use them, and **Record into timeline**
writes the clips they launch onto the timeline as it would your own launches.

## Scenes

- **Click a scene's ▶** to launch it.
- **Double-click a scene's name** to rename it.
- Right-click a scene's header for: **Launch**, **Rename**, **Colour**, **Tempo** (a tempo the song jumps to when the
  scene launches), **Insert scene above / below**, and **Delete**.
- Click **+** after the last scene to add one. A song has up to 64 scenes.

### Scenes from the panel

The Roll panel can launch scenes without the editor: **PREV** and **NEXT** pick a scene, **LAUNCH** starts it,
**STOP ALL** stops all clips. The **SCENE** and **LAUNCH** inputs do the same from CV, so a sequencer or a MIDI
keyboard can switch your song's parts. The picked scene shows on the display and is outlined in the editor.

## Launching clips from a MIDI controller

**Roll ▾ → Clip control** in the editor (or right-click the Roll panel → **Clip control**) sets up a MIDI controller
to launch clips. There are three ways, and
you can use them together.

**A pad grid.** Pick your controller under **Controller**: **Launchpad X**, **Launchpad Mini MK3**, **APC mini MK2**,
or **Any 8x8 pads (notes)** for other pad controllers. Then pick it under **Controller input**, and for the first
three also under **Controller lights (its MIDI out)** (on a Launchpad, its **MIDI** port, not its DAW port). The pads
show 8 tracks by 8 scenes of the launcher, outlined in the editor:

| On the controller | What it does |
|---|---|
| A pad | Launches that slot's clip. A pad with no clip stops its track (as an empty slot's ■ does). |
| The right column (Launchpad), the scene buttons (APC mini) | Launch that row's scene. |
| The track buttons (APC mini) | Stop that track. |
| The arrows (Launchpad), Shift + the last four track buttons (APC mini) | Move the 8x8 window a track or a scene. |
| The top row's last button (Launchpad) | Stops all clips. |

The pads light up as the launcher does: a clip in its own colour (dim while it waits), pulsing while it plays,
flashing while it waits to start or stop, red while it records. Clips start on the launch quantize, as when you
click them. For **Any 8x8 pads**, set the note of the **Bottom-left pad**; those controllers get no lights.

**Learned controls.** Right-click a slot or a scene → **Learn MIDI**, then press any pad, key, button or pedal on
the controller: it now launches that slot or scene. A slot with a learned control shows a small dot in its corner;
the menu names the control (**Note C3, ch 1**) and has **Forget MIDI**. A learned control wins over the grid, and
**Clip control → Forget learned controls** clears them all.

**Program changes.** With **Program changes launch scenes** on, program change 1 launches the first scene, 2 the
second, and so on: handy for foot controllers and set lists. **Channel** limits learned controls, the note pads
and program changes to one MIDI channel.

The controller settings are saved with the patch (not in project files: the devices belong to your setup).

## What plays: clips or timeline

The **Plays** switch in the transport bar (and **PLAYS** on the panel) decides what the tracks play:

- **Clips**: each track plays the clip you launched. The timeline is silent.
- **Timeline**: each track plays the timeline from left to right.

The section that plays is framed in green while the transport runs. The other one is dimmed, but you can still edit
it.

**Launching over the timeline.** While the timeline plays, you can still launch a clip. That track then leaves the
timeline and plays the clip. Its timeline lane is dimmed. Click **Back to
timeline** on the timeline's ruler to bring every track back. Switching the Plays switch to Timeline also brings them
back.
