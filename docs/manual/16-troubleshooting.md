# 16. Problems and answers

## I press play but hear nothing

Check these in order:

1. **Is there an output module?** A track with notes but no output module shows an amber jack in its header. Add a
   **Track Out** (or **Drum Out** for drums) and make sure its screen shows the right track.
2. **Is the voice patched?** Track Out's **V/OCT** and **GATE** must go to your oscillator and envelope, and the
   sound must reach Rack's **Audio** module.
3. **Does the track play something?** With **Plays** set to **Clips**, a track only plays a launched clip: look for
   the play ring in its slot, or click the clip's ▶. With **Plays** set to **Timeline**, the timeline needs clips at
   the playhead.
4. **Is the track muted, or another track soloed?** Look at **M** and **S** in the track headers.
5. **Is a note muted?** Muted notes are hatched. Right-click them and choose **Mute / unmute**.
6. **Is Tessera bypassed?** A bypassed Tessera sends no gates.

## The output module says "No source" or "Track missing"

- **No source**: it can't find its Tessera. Right-click it and pick one under **Source**. This happens when the
  Tessera was deleted, or after pasting the output module alone into another patch.
- **Track missing**: its track was deleted. Undo the delete, or pick another track with the arrows or the **Track**
  menu. Output modules never switch tracks by themselves.

## A key does nothing (or does something else)

- Keys go to the pane **under the mouse**. Move the mouse over the notes for note keys, over the song pane for track
  and clip keys.
- In the panel editor, keys only reach Tessera while the mouse is over the panel.
- While **keyboard play** (Ctrl+K) is on, letter keys play notes instead of switching tools. Press Ctrl+K to stop.
- While typing in a field (a name, a value), keys go to the field.

## Chords play only one note

Look at the track's **Voices** (inspector or track menu). With a fixed number like 1, only one note plays at a time.
Choose **Auto**. Also check that your oscillator and envelope are polyphonic.

## Notes play at the wrong pitch

- Check that Track Out's **V/OCT** goes to the oscillator's V/OCT input, not FM.
- Tune the oscillator: Tessera sends 0 V for C4.
- A **Transpose** device, a timeline clip's **Transpose**, or a **pitch bend** on the notes also change the pitch.

## The music doesn't follow my external clock

- Patch the clock into **CLOCK** on the top input row. The display should say **EXT CLOCK**.
- Set the right pulses per quarter note in the module menu: **CLOCK input** (24 PPQN for MIDI clock).
- Starting the transport starts the song on the next pulse. Send a trigger to **RESET** to restart the count.
- While a clock is patched, the tempo can't be dragged: it follows the clock.

## A clip doesn't start when I click it

While the transport plays, clips wait for the next **launch line** (by default the next bar), blinking yellow. Choose
**None** in **Launch 1 bar ▾** to start clips at once.

## I recorded but got nothing

- Is the track **armed** (red ring)?
- Does **Record from** match your source (Any, REC CV inputs, MIDI) and the **MIDI channel** your keyboard sends on?
- Is the MIDI device chosen in **MIDI input (for recording)**?
- For CV: the gate must rise above 1 V to start a note.
- If you played nothing while recording, no clip is made. Try **Capture** (Ctrl+Shift+R) to keep what you played
  before pressing record.

## My edit went wrong

Press **Ctrl+Z**. Every change in Tessera can be undone, including track deletes, recordings and opening a project.
While dragging, **Esc** cancels the drag.

## The panel editor is too small

Rack modules are always the same height. Zoom the rack in to make the panel editor bigger, maximize one pane with its
corner button, or open the **Full editor**.

## The editor is too small or too big

Right-click the Tessera panel and pick **Editor size**.

## Several Tesseras: which one does an output module follow?

Give each Tessera its own **Instance name** (module menu). Output modules show the name on their screen, and the
**Source** menu lists the names.

## Where is…

| Looking for | Find it in |
|---|---|
| Swing | **4/4 ▾** in the transport bar |
| Time signature | **4/4 ▾** in the transport bar |
| The key | The key chip in the notes header |
| Count-in | The ▾ next to the record button |
| Ghost notes | **View ▾** in the notes header |
| Flats instead of sharps | **View ▾ → Name notes with flats** |
| Colour scheme | **Tessera ▾ → Colour scheme** |
| Quantize | **Notes ▾ → Quantize…**, or **Q** |
| Clip length | The clip chip at the left of the notes header |
| Launch quantize | **Launch 1 bar ▾** above the track names |
| Ripple, Stack, Automation follows clips | The ▾ next to Select and Draw in the song pane |
| MPE input | The ▾ next to the record button |
| Recording a lane (the old lane arm) | Arm the track; the lane menu's **Records from** picks the source |
| Automating a rack knob | Track menu → **Automate a knob: touch it in the rack** |
| Every shortcut | Press **?** in the editor |
