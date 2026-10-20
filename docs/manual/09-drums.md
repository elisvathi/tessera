# 9. Drums

A drum track holds drum sounds in **lanes**: Kick, Snare, Closed hat and so on. Each lane has its own output on the
Drum Out module. Select a drum track and the notes pane shows the **step grid**.

![The drum step grid](images/drum-editor.png)

## The step grid

- Each row is a lane. Each cell is a step, 16 to a bar by default.
- A lit cell plays. Its height shows the velocity.
- The lane header shows the lane's name, its output ("→1"), and **M** (mute) and **S** (solo).
- **Click a lane's name** to hear it.
- Under the grid, a velocity lane shows the steps of the lane you clicked last.

**Steps | Piano roll** in the header switches between the step grid and a piano roll of the same notes. **G** does the
same. The piano roll is good for free timing and long notes. In it, turn on **Fold** (in the key chip, or Ctrl+Shift+F)
to see one named row per lane.

## Making a beat

With the **Draw** tool (D):

- **Click** a step to turn it on or off.
- **Drag** across steps to paint them on. If the first step was on, the drag clears instead.
- **Right-drag** clears steps.

With the **Select** tool (S):

- **Click an empty step** to turn it on (and select it). **Click** a lit step to select it. **Shift** adds, **Ctrl**
  adds or removes.
- **Drag from empty space** to select a box of steps.
- **Drag selected steps** to move them by steps and lanes. **Ctrl+drag** copies them.
- **Delete** removes the selected steps. **Ctrl+A** selects all.

Other tools in the **▾** menu: **Erase**, **Mute** (muted steps are dim with an outline) and **Velocity**. The menu
also has **Hear steps while editing**.

New steps use the last velocity you set. Each drag is one undo step.

### Velocity

- **Alt+drag** a step up or down to change its velocity (Shift for fine steps). If the step is selected, the whole
  selection changes.
- Or drag across the **velocity lane** under the grid. Each step you cross takes the height of the mouse.

### Step menu

Right-click a step for its menu. It acts on the selected steps (a step outside the selection becomes the selection):
**Velocity**, **Chance**, **Repeats**, **Only when…**, **Every … passes**, **Mute** and **Delete**. These are the same
operators as in the piano roll; see [Shaping notes](08-shaping-notes.md#operators-notes-that-change-from-pass-to-pass).

Steps with repeats are split into small cells. Steps with chance or a condition show a dot in the corner. With steps
selected, the inspector edits them too.

### Zoom

**Ctrl+wheel** zooms in time, up to 16 times. **=** and **-** zoom in and out. **F** fits the clip.

## Lane settings

Right-click a lane's name for its menu.

![The lane menu with the Euclidean submenu](images/drum-lane-menu.png)

| Item | What it does |
|---|---|
| **Rename** | Also by double-clicking the name. |
| **Colour** | The lane's colour (or the track's). |
| **Note** | The MIDI note this lane plays. A lane plays the clip's notes of this pitch. |
| **Output** | Which Drum Out output (1 to 16) the lane uses. Outputs 1 to 8 have their own jacks; 9 to 16 are on the poly jacks only. Lanes on the same output share it. |
| **Steps** | The lane's own loop length, in steps. "Clip" follows the clip's loop. See [Polymeter](#polymeter). |
| **Rate** | How long a step is for this lane: 1/4 to 1/32, triplets too. |
| **Choke group** | 1 to 8, or none. When a lane in a group plays, it cuts the other lanes of the same group. In the kits, closed and open hats share group 1, so a closed hat cuts a ringing open hat. |
| **Output length** | **Trigger** (1 ms, the default) or a gate of a fixed length in milliseconds. The note's own length doesn't matter. |

### Pattern tools

Also in the lane menu. Each one is a single undo step.

| Tool | What it does |
|---|---|
| **Euclidean** | Spreads a number of hits as evenly as possible over the lane's steps. The first step always hits. Great for quick rhythms: 3 hits over 8 steps, 5 over 16… |
| **Rotate left / right** | Shifts the pattern one step. |
| **Reverse** | Plays the pattern backwards. |
| **Fill every** | Turns on every 2nd, 3rd, 4th… step. |
| **Randomize** | Random steps: 25%, 50% or 75% of them. |
| **Clear** | Clears the lane. |
| **Copy lane / Paste lane** | Copies a lane's steps to another lane. |
| **Add lane / Delete lane** | A drum track has up to 16 lanes. |

## Kits

The **Kit** button loads a set of lanes:

- **Standard 8**: Kick, Snare, Clap, Closed hat, Open hat, Low tom, High tom, Rim. New drum tracks start with it.
- **16 GM-ish**: 16 lanes on General MIDI drum notes.

Loading a kit changes the lanes only. Notes stay, so lanes with the same notes pick them up again.

## Polymeter

A lane can loop on its own length. Set its **Steps** to 12 while the others use 16, and it drifts against the bar,
coming back in line every few bars. The lane's pattern is the first steps of the clip. The step grid shows its
repeats faded. It plays the same way from the clips panel and from the timeline.

Choose **Steps → Clip** to follow the clip's loop again.

## Clip length

The clip chip on the left of the header works as in the piano roll: rename, loop, lengths of 1 to 16 bars,
**Double** (copies the steps into the new half) and **Halve**.

## Wiring the Drum Out

- Patch jacks **1** to **8** to your drum modules' trigger inputs. Each jack's light flashes on a hit.
- **V1** to **V8** carry each lane's velocity, 0 to 10 V. Patch them to an accent or level input.
- **TRIGS** and **VEL** carry all 16 outputs on poly cables, one channel per output.
- **AUTO** carries the drum track's automation lanes.

See [The modules](02-modules.md#drum-out).
