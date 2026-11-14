# 18. The mixer

The mixer sets how loud each track that makes sound plays and where it sits from left to right, and shows the levels as they
play. It appears as soon as the song has an audio track.

![The mixer in the lower pane, with one audio track and the Master](images/mixer.png)

## Opening it

The mixer shares the lower pane with the notes (or the audio editor), and in a short pane with the devices too. Its
header starts with **Notes | Mixer** (**Audio | Mixer** when an audio track is selected): click **Mixer** to show the mixer there, the other name to go
back, or press **F4** to switch. Clicking any clip also goes back to it. The switch appears once
something in the song makes sound.

- It is the same pane: the line between the panes resizes it, its **maximize** button in the top-right corner
  fills the editor with it and restores the panes, and **Tab**, **Shift+Tab** and the focus ring work as for the
  notes.
- The header shows a small live meter for each track that makes sound, the Master's peak level, and **MAIN not
  patched** in amber when the Master plays but nothing is patched into MAIN.

## A strip

Each track that makes sound has a strip, in the same order as the tracks: an audio track, or any track whose sound
comes back on a Track Out's or Drum Out's IN. From top to bottom:

1. **The name**, under a bar in the track's colour. Click it to select the track.
2. **The source**: where the sound comes from. **Clips** for an audio track's clips; **IN · Track Out** or
   **IN · Drum Out** for a note track whose voice comes back on that module's IN; **2 sources** when the track has
   more than one, or **1 in · 1 fx** when it has an effect: then the arrow beside it folds the track's input strips
   (see [Several sources on one track](#several-sources-on-one-track) and [Effects on a track](#effects-on-a-track)). In amber: **Missing file** when
   a clip's file is gone, **Bypassed** when a module that brings the sound is bypassed. With one source, click it for a menu: **Add a
   Track Out for this track** places a new Track Out (a Drum Out for a drum track) bound to it, right of Roll;
   **Show Track Out in the rack** scrolls the rack to the module and makes its screen flash.
3. **Pan**: drag it to move the sound left or right. Double-click centres it.
4. **M** and **S**: mute and solo, the same switches as in the track rows.
5. **The fader and the meter**. The fader sets the volume; the meter beside it shows the level as it plays.
6. **The volume** in dB, and **→ Master**: where the track goes.

The **Master** strip sits at the right. It sets the level of everything together on its way to MAIN. Its light near
the top turns red and shows the peak (like **+2.3**) when the sound went over full scale; click it to clear it.
Under its fader it says how much memory the song's audio uses. When MAIN has no cable and the patch has an Audio
module, it offers **Connect to Audio**. With an effect on the Master, its input strips sit left of it: **Mix** (all
the tracks together) and the effect. The arrow on **MAIN** folds them.

## Several sources on one track

A track can hear more than one thing at once: an audio track's clips and a synth on its Track Out's IN, or two
Track Outs on one track, each with its own synth, to layer them. Each is a **source**, and every source plays.
When you patch a second source, a message says so: "Lead has 2 sources now. Both play", with **Use only the new
one** to switch the others off.

With two or more sources, each gets its own narrow strip next to the track's strip: **Clips** on an audio track, and
**IN** (or **IN 1**, **IN 2**) for each Track Out. Each has a fader, pan, **M**, **S** and a meter. The track's own strip is
the master amount for all of them. The arrow on the track's source label folds the strips away.

- **M** silences that input, without pulling a cable. Everything after it in the track's chain hears it go too: to
  keep an effect and drop the dry sound, set the effect's **Mix** to 100% instead.
- **S** plays that input alone, and solos its track too.
- Right-click an **IN** strip's name for **Show this module in the rack**.
- **Counted once**: when one cable feeds the INs of two Track Outs on the same track, the second adds nothing.

A Track Out's **Use** and **Level** are kept in the module and also sit in its right-click menu, under **As a source
of its track**. Its screen shows **IN OFF** or the level (like **IN -6.0 dB**) while they change the sound. They go
with the module when you move, duplicate or delete it.

## Effects on a track

Put an effect on a track by patching it through one of the track's Track Outs: the Track Out's **OUT** into the
effect, the effect's output back into the same Track Out's **IN**. That Track Out is now an **effect loop**: its OUT
carries the track's sound, and what comes back takes its place.

![A delay on the Loop track at 30% Mix, and a filter on the Master at 100%](images/mixer-effect-loops.png)

- **The Mix**: the loop gets a narrow strip next to the track's, named after the module patched into its OUT
  (**Delay**). Its fader is the **Mix**, how much of the effect you hear. At **100%**, where it starts, you hear only
  what comes back. At **50%** you hear the dry sound and the effect both in full. Under 50% the effect fades out,
  down to none at **0%**.
- The Mix fader works like the others: drag it (Shift for fine), double-click for 100%, scroll for 5% steps (Shift:
  1%), right-click to type a value. **M** turns the effect off: the sound passes by it untouched.
- **A chain**: put two or more Track Outs on one track, each with its own effect. The sound goes through them in
  order, left to right in the mixer. Drag a loop strip's name sideways to move it in the chain, or right-click the
  name for **Move left** and **Move right**. A new Track Out joins at the end. Each module keeps its place, so the
  order stays when you save, move or duplicate it.
- **Sources have a place too**: a Track Out with nothing on its OUT adds what comes in on its IN (a synth, a live
  input). An effect after it in the chain hears it; an effect before it doesn't.
- **The Master** works the same way: bind a Track Out to the Master and patch an effect through it.
- **A drum track**: patch the Drum Out's **Kit OUT** into the effect and its output back into **Kit IN**. The loop's
  strip comes after the lane strips.
- A Track Out with a cable on OUT and nothing on IN (into a scope, say) sends the sound out and lets it pass on
  untouched.
- Roll lines the dry sound up with what comes back, so 50% never sounds hollow. Each loop makes the track a few
  samples late, and the other tracks wait for it.
- The Track Out's screen shows **FX MIX 30%** (or **FX OFF**), and its right-click menu has **On** and **Mix** under
  **As an effect loop of its track**.
- **Older patches** sound as they did: their loops open at 50%, the dry sound and the effect both in full. An audio
  track whose Clips were turned down to hear only the effect gets its Clips back at 0 dB, and its loop the Mix that
  keeps the dry sound where it was.

## A short mixer

When the pane is less than about 200 px tall, for example in the in-panel editor, the strips change shape. Each has a level
knob, with what is sounding on the ring inside it, a pan knob, **M** and **S**. Drag the level knob up or down; everything
else works the same. An effect loop's knob is its **Mix**.

The knobs sit one above the other, spaced to fill the strip, with the level in dB when there is room for it. In a very short
pane, under about 80 px, they sit side by side.

## Sending to other tracks

To share one effect between tracks, send to it. Any track can receive sends: add a track, name it "Reverb", patch
its Track Out **OUT** into a reverb module and the reverb's output back into its **IN** (an
[effect loop](#effects-on-a-track) at 100% Mix). Then send other tracks to it.

- **Add a send**: click the dim **+** under a strip's name and pick a track. Tracks that would feed back into this
  one are listed but can't be picked ("Would feed back into Bass"). The send starts at -inf and the faders switch
  to show sends to that track, so you can turn it up.
- **Send knobs**: each send has a small knob on the sending strip, labelled with two letters of the receiving
  track in its colour, and **pre** when it goes before the fader. Drag it up or down; double-click for -inf;
  scroll for 0.5 dB steps. Right-click it to type a level, put it **before** or **after the fader**, or **remove**
  it.
- **← 2** under a strip's name means two tracks send to it. Click it (or right-click the name: **Show sends to this
  track**) to see every send to that track on the faders: a band says "Faders show sends to Reverb", the fader caps
  turn hollow in Reverb's colour, and the readouts say **send -15.5 dB**. Drag a fader to set that track's send
  (dragging up from -inf adds one); **pre** beside a fader switches the send before or after the fader. Tracks that
  can't send there, and the Master, are dimmed. **Esc**, the band's **×**, or folding the mixer goes back.
- What a track is sent joins its sound before its chain. With a reverb loop at 100% Mix you hear only the reverb;
  with no effect patched yet, you hear what it is sent as it is.
- A track that receives sends has a thin **OUT meter** beside its fader: what goes into its chain. It is dim while
  no Track Out of the track has a cable on OUT.
- **Mute** stops a track's sound and its sends. A muted receiving track still passes what it is sent to its OUT,
  so the effect's tail is ready when you unmute.
- **Solo follows the sends**: solo Bass and you hear Bass through the reverb it sends to. Solo the reverb track and
  you hear the reverb, still fed by the tracks that send to it, but not their dry sound.
- Sends you make in the rack with cables (a mult into the reverb) also work, but they don't follow the fader, mute
  or solo.
- In Show sends, each strip's meter shows what it sends to that track.
- **Automate a send**: right-click its knob ▸ **Automate in the timeline**. A **Send to Reverb** lane opens under the
  track; draw it like a Volume lane. While it plays, the knob follows it and has a ring in the lane's colour. Arm
  the track and record to write a knob move into it.
- **The inspector** lists the selected track's sends under **SENDS**: each with its level and whether it is before
  or after the fader (click for the same menu as the knob), then **Send to +**, and **Gets sends from** with the
  tracks that send to it.
- Deleting a track removes the sends to it, in the same undo step.

## Group strips

A group of tracks (select them and press **Ctrl+G**) gets a strip of its own in the mixer, right after its last
track: the sum of its tracks that make sound passes through it on the way to the Master. Its tracks say **→ Group
name** under their faders. The group strip has a pan knob, **M** (mute the whole group), a fader with a meter, and its
level. Drag, double-click and right-click work as on a track's fader. Stems in an export are each track on its own,
before the group strip.

## Drum lanes

When a drum module's output is patched into a lane's **IN** on the Drum Out, that lane gets a narrow strip of its own,
right after the drum track's strip, in the lane's colour and named after it (**Kick**, **Snare**). Each has a pan
knob, a fader with a meter, and its level in dB. Faders and pan knobs work as on any strip: drag, double-click to
reset, scroll, right-click to type a level.

Use them to balance the kit inside Roll; the drum track's own fader then sets the whole kit. The kit's **OUT**
on the Drum Out carries the lanes at these levels and pans.

## Faders

- **Drag** a fader up or down. It moves from where it is: a click never makes it jump.
- **Shift+drag** or **Ctrl+drag** moves it finely.
- It catches at **0 dB** as you pass; hold **Alt** to move through freely.
- **Double-click** sets 0 dB.
- **Scroll** over it to move 0.5 dB at a time (Shift: 0.1 dB).
- **Right-click** it, or double-click its value, to type a level.
- Select several audio tracks and drag one of their faders: all of them move by the same amount. A track at −inf
  stays there.

A drag is one undo step. You hear the new level while you drag.

## Levels

0 dB on every meter is full scale: 10 V on MAIN, the most Rack's Audio module sends to your sound card without
clipping. The meters run from −60 dB to +6 dB. They turn amber in the top 6 dB, and red above 0 dB. A thin line
holds each peak for a moment.

## Levels in the track rows

Audio tracks also show their level in the track rows, as a second line under the name: a level bar, its dB value,
and a small pan knob.

- The level bar is the fader and the meter in one. It glows in the track's colour as it plays, brightest in the top
  6 dB. The thin bright line is the fader.
- **Drag** it sideways to change the volume. A click without moving just selects the track.
- **Double-click** the bar for 0 dB, or the pan knob to centre it.
- **Scroll** over the bar to move 0.5 dB at a time.

Rows too short for a second line don't show it; make the row taller (drag its bottom edge) to see it.

## Automating the fader and pan

Right-click a fader, a level bar or a pan knob and choose **Automate in the timeline**. The track gets a **Volume**
(or **Pan**) lane, starting at the control's value, and its lanes open in the timeline. You can also add one from
**+ Add automation lane**.

- Draw the lane like any other: its scale reads in dB (from **-inf** up to **+6 dB**) or in pan.
- While the song plays, the fader, the level bar and the pan knob follow the lane, in the mixer and in the track
  rows. A control with a lane has a ring in the lane's colour.
- Drag the control while it plays and your hand wins for as long as you hold it. Dragging the pan leaves the
  Volume lane playing, and the other way round.
- **Record a move**: arm the track and record. While you drag the fader (or pan), its lane records your hand, like a
  knob lane (see [Recording](11-recording.md)): by default it writes while you move and stops half a second after you
  hold still or let go. The whole take is one undo step. Stopped, a drag only moves the control.
- Mute the lane (its **M**) to give the control back to the strip.

## The Master row

After the last track, above **+ Add track**, a thin **Master** row shows the Master's level bar and its volume in
dB. It works like a track's level bar: drag it sideways, double-click it for 0 dB, scroll over it to move 0.5 dB at
a time. Click the word **Master** to open the mixer. Like the mixer, it appears once a track makes sound.

**Automate the Master**: right-click its level bar (or the Master fader in the mixer) and choose **Automate in the
timeline**. A **Volume** lane opens under the Master row; draw it like any lane, for a fade-out at the end of a
song, say. The arrow beside the word **Master** shows or hides it. While it plays, the level bar and the Master
fader follow it, with a ring in the lane's colour. The Master has no arm, so its lane is only drawn, never recorded.
