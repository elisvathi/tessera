# Review 2 of the audio design (revisions 5 and 6)

A UX designer and a product manager each reviewed `AUDIO_MIXER.md` revision 6 on 2 Nov 2026, looking for design
flaws in what revisions 5 (sources) and 6 (sends to any track, no FX tracks, no faders without sound, bounce
lengths) changed. This file merges both reports. Nothing here is in the spec yet: the user decides first.

Both reviewers say the direction is sound: sources, sends to any track and no FX track kind fit "the cable is the
setting" and remove a track kind. The weak part is the same in both reports: **guessing four jobs from cable
paths** (§7.8).

## Blockers

### 1. Guessing jobs from cables gets common patches wrong (§2, §7.8)

"Follow cables, don't look inside modules" sees paths that carry no sound:
- **Rack's Audio module** has inputs and outputs in one module. MAIN goes into it and the mic comes out of it, so
  the mic traces back to MAIN and is never LIVE IN. A track's OUT going to the speakers makes the mic look like
  an effect loop.
- **Sidechains and CV:** a compressor on the mic keyed from the kick makes the mic FROM KICK, heard all the time.
  A modulation cable changes when you hear the mic.
- **Clock and transport jacks** (CLOCK, RUN, CLICK): a drum machine clocked by Tessera fits no job.
- **Switches and mixers** turned down still count as a path.
- A **second Tessera's** jacks have no rule.

**Recommendation.** Detect one thing only: **is there a cable path from this track's own OUT?** Then it is an
effect loop (LOOP). Every other patched IN is plain **IN**:
- On an **audio track**, IN plays only **while armed** (the rev 4 monitoring rule).
- On **instrument and drum tracks**, IN always plays.
- Tracing stops at Rack's Audio modules (a sound card input is always IN) and ignores clock and transport jacks.
- One rarely needed override in the Track Out's right-click menu: **IN is: Auto / Effect return / Input**.
  "Effect return" is how a reverb fed by rack cables plays all the time on an audio track.
- FROM BASS and VOICE go away, and so does the four-step match order. The manual needs only two sentences:
  "Patch OUT through an effect and back into IN, and you hear only the effect. On an audio track, IN plays while
  armed."

### 2. Cables can make feedback the loop guard can't see (§4.4, §7.6)

- **Resampling the mix:** MAIN into an armed audio track's IN. That track goes to the master, and the master
  goes back to MAIN, so it howls.
- **A cable back to the sender:** Bass sends to Reverb with **+**, and Reverb's OUT is patched into Bass's IN.

**Recommendation.** An IN whose path starts at MAIN, at the master's OUT, or at the OUT of a track this track
already feeds (by sends) is **recorded but not heard**. Its chip reads "Feedback blocked · recorded only". The
**+** menu also greys targets that already reach the sender by cable.

### 3. Recording on an audio track that has an effect loop (§2, §7.8, §8.6)

- **Mic on a second Track Out:** you hear the mic dry while recording, but the take plays back through the delay.
  What you hear while recording is not what plays back.
- **Mic and delay return mixed into one IN:** the job is LOOP, recording leaves loops out, and the take is
  silent.
- **Resampling a synth doubles it** (rev 6 FROM rule): a mult of Bass's synth into an audio track is heard at
  once, before arming, and again after the take. Recommendation 1 fixes this.

**Recommendation.** While armed, the track's IN also goes into its OUT, so you hear the mic through the loop.
The take records IN dry. If an armed track has nothing to record, its arm button turns amber: "Nothing to
record: IN is this track's effect loop".

## Major

### 4. Strips and rows appear and vanish under the mouse (§7.8, §7.9, §7.5)

- With every Use off, §7.8 shows "No source in use", but §7.9 hides the strip, so the chip, and the only place
  to turn Use back on, are gone. "Track Out bypassed" and "Track deleted" vanish the same way.
- Every repatch is unplug then replug: the strip disappears, the others shift sideways, and selection, Show sends
  and drags are lost.
- Track rows grow a second line when a cable arrives, so rows jump while you patch.

**Recommendation.** A track makes sound when its IN is **patched**, whatever its state. After a cable is pulled,
keep a dim strip reading "IN not patched" until the mixer is folded or the patch reloads. Rows never change
height: the level line uses the row's existing space (the short-row strip of §7.5 when it is tight).

### 5. Sends made with cables ignore mute and solo (§4.5, §7.6)

Mute Bass and its rack-made reverb keeps playing. Solo Bass and Reverb goes silent, because solo follows Tessera
sends only.

**Recommendation.** With recommendation 1, rack-made sends are just a rack effect returning on a track. Say it
once, in the **+** tooltip and the manual: "Sends made here follow mute, solo and the fader; cables in the rack
don't." Following cables for mute and solo would need the path guessing that recommendation 1 removes.

### 6. Bounce with "Mute the original" changes the mix (§8.6)

The new track starts at 0 dB, centred, with no sends, so the level jumps and the reverb disappears. Not defined:
a muted or un-soloed track; a selection across several tracks; "Until silent" on a track with no notes (a
reverb); bounce while Plays is Clips.

**Recommendation.**
- The new track copies the original's fader, pan, sends and their automation.
- A bounce ignores the bounced track's own mute and solo.
- Several tracks selected: one bounce per track, in one pass.
- "Until silent" on a track with no notes counts from the end of the selection.
- While Plays is Clips, Bounce records the slots that are playing, until you press Stop (as Export does).

### 7. Duplicating a Track Out for the next synth (§7.8)

Rack users duplicate a Track Out to set up the next voice. The copy is still bound to Bass, so the new synth adds
into Bass. The toast's "Use only the new one" turns off the working voice.

**Recommendation.** The toast reads "Bass has 2 sources now. Both play. · Move the new one to another track… ·
Show sources". The Track Out display shows "2 SRC".

### 8. OUT on a muted track (§4.5, §8.1)

Not defined. An audio track's OUT used as a direct out keeps playing when M is pressed, while its notes stop.

**Recommendation.** OUT follows mute and solo, except on a track that receives sends, where it keeps feeding the
effect so tails return on unmute (as written).

### 9. Phase 2 is too big (§11)

Track Out redesign, return bus, jobs, sources, sends to any track, loop guard, Show sends, solo, automation and
helpers are all one phase, and it is the riskiest one.

**Recommendation.** **P2a:** Track Out OUT/IN, one source per track, LOOP detection, strips for every track that
makes sound (journeys 2 and 4). **P2b:** sends, Show sends, solo rules, several sources, helpers.

## Minor

10. **Use and Level are two controls for one idea** (§7.8): Level at −∞ and Use off do the same thing.
    Recommendation: keep Level only; −∞ is off, and double-click goes back to 0 dB. Write down that source levels
    are patch data, so a .tessera loaded into another patch keeps none of them.
11. **Send labels collide** (§7.2): "Synth 1" and "Synth 2" both read "Sy". Use the target's number tile in its
    colour, as the track rows do.
12. **The + menu lists up to 63 tracks** (§7.6): list tracks with an effect loop first, the rest under "No effect
    yet".
13. **Which kind makes a good reverb track** isn't said (§1.3): the journey and the manual say to use an
    audio track (its OUT is already live for sends). Both reviewers also asked for a "New effect track…" helper;
    it is left out because the user rejected it (2 Nov).
14. **OUT warning wording** (§8.1): "Nothing comes out here for an audio track" is wrong on an instrument track.
    Word it per kind ("No sends reach this track yet").
15. **Fixed limits on the audio thread** (§4.4, §8.3): sends and return slots need caps (for example 8 sends per
    track, 128 return slots); grey the **+** when a track is full. Rack has no cable-change event, so the UI thread
    compares the cable list each frame and recomputes only when it changes.
16. **Wording** (§7.6, §7.8): "Five places, one value" lists rack cables, then says they aren't a send; the chip
    can read "1 sources"; source names renumber when a module is moved (number them by binding order); Follow mode
    is only explained in a tooltip (show "IN OFF · FOLLOWS" on the display).
17. **MIDI export** writes a reverb track as an empty track. Skip tracks with no notes that receive sends.

## Good, and should stay

One Track Out for every track, with dim jacks; "Sends go nowhere"; Show sends with three signals and drag-up to
add a send; levels per source shown only when needed; "Same cable, counted once"; the greyed loop guard with its
reason; the dim, never hidden OUT meter; §7.9's aim that Tessera stays a plain sequencer; bounce choices that show
only when they apply; one Esc order; solo follows sends.
