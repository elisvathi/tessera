# Changelog

What is new in each version of Tessera. The
[user manual](https://github.com/elisvathi/tessera/blob/main/docs/manual/README.md) explains every feature in full.

## 2.1.0

The biggest release so far. Tessera now plays audio as well as notes, mixes every track, and can read one clip with
many playheads at once.

### Audio tracks

- **Drop a sound file** (WAV, AIFF, FLAC or MP3) on the panel or into the editor to make an audio clip. Tessera
  guesses its tempo and whether it is a loop or a one-shot, and keeps it in time with the song.
- **Stretch and pitch** clips independently. Clips follow the TEMPO knob and stay in tune.
- **The audio editor** in the lower pane: cut, move, trim, fade and crossfade parts of a clip, reverse, normalise,
  quantize, and render to a new file. The file itself is never changed.
- **Hits and warp markers:** find the hits in a file, split at them, turn them into drum triggers, or pin them to
  the beat.
- **Record audio** from a track's input, keeping every pass as a take. Comp the best parts by dragging across the
  takes. A metronome and a recording offset you can measure help it line up.
- **Bounce** a track's sound (effects included) to a new audio track, or its notes to a new note track.
- **Export** the master, or the master and every track as stems, to WAV or FLAC.
- Audio goes out of the Roll's new **MAIN L/R** jacks, with a meter on the panel.

### The mixer

- **A strip for every track that makes sound**, plus the master, with fader, pan, mute, solo and meters. It opens in
  the lower pane, beside the notes and the devices.
- **Sends** from any track to any other track, for shared reverbs and delays.
- **Effect loops:** patch a Track Out's OUT through any Rack effect and back into its IN, then set the dry-wet Mix.
  Works on the master too.
- **Group strips** that sum a set of tracks, **drum lane strips** with a level and pan per lane, and **input strips**
  for each source patched into a track.
- **Automate** faders, pans, sends and parts with lanes, or record your moves while the track is armed.
- The track headers show each track's level and live meters. One arrow opens a track's lanes, parts and sends right
  under it.

### Playheads

- The **Playheads** device reads one clip with up to 16 playheads at once. Each has its own speed, direction,
  segment, repeats, offset and note order. One melody becomes a canon, a bass line or a texture that keeps shifting.
- Each playhead can have **its own devices and its own output channel**, so it can play a different sound.
- **Bend a playhead's speed** with a Speed curve, and space its repeats with ready-made shapes.
- The piano roll shows every playhead's path, and lights each note in the colour of the playhead playing it.

### Motion, curves and modulators

- The **Motion** device makes chosen notes drift while the clip loops: rise a step each pass, slide later, swell in
  velocity. The written notes stay as they are, and the piano roll shows where each note really plays.
- **Curve** modulators draw how any setting changes over time, or by one value for each pass of the clip.
- Modulators live in three places: **song**, **track** and **clip**. **Macro** faders turn many settings at once.
  Drag modulators between places, or copy and paste them.
- **One dice for randomness:** random devices can keep their numbers or roll new ones every play.

### Devices

- **A clip can have its own devices** instead of its track's, so two clips on one track can sound different.
- **Arpeggiator:** an editable **Pattern** grid with velocity, gate, octave and pitch per step, and many new orders
  (Converge, Diverge, Walk, Thumb, Pinky, Shuffle and more).
- A new **Note Repeat** device.
- Device cards show their settings as **small pictures with tags you drag**. Cards fold to a thin rail.
- A Note Filter can keep a set of channels, not just one.

### Notes and playing

- **Ties and glides:** tie notes so they play without a new gate, and glide across the joint.
- **Voice allocation** you choose per track, including **Channel**: note channel N always plays on poly channel N.
- **Next actions:** a clip can launch the next one by itself after a number of loops or bars, with a chance.
- **Launch clips from a MIDI controller.**
- Bring tracks back to the timeline one by one, all at once, or as a next action.

### Files

- **Import DAWproject files** as a new song, with their audio, mixer, scenes and note curves.
- Export audio tracks, note curves, pitch bend and pressure to DAWproject, and note curves to MIDI.
- Save a project with its audio in one file. Missing files are found all at once.

### Faster

- An idle Roll costs next to nothing: stopped tracks sleep and silent tracks rest.
- A busy patch costs less: the mix does less work for each sample.

### Updating from 2.0

- **Your patches and songs open as before** and play the same notes. This was checked by playing songs saved with
  2.0 versions in both versions, and by opening and saving 2.0 patches in Rack.
- **The Roll is wider** (30 HP, was 24) to make room for the new jacks. Rack moves the module to its right out of the
  way once.
- **Timeline tiles no longer have a velocity scale.** A song that used one has the scale written into the notes when
  it loads, so it sounds the same. A clip used at several scales gets a copy for each.
- **Draw a curve is gone.** Use a setting's **Modulate, automate…** menu: a new Curve modulator does the same.
- **Humanize rolls different random numbers** than 2.0 did. Its amounts are the same.
- **Going back to 2.0:** it opens a patch saved by 2.1.0 but drops everything that is new. Keep a copy of your patch
  before you try 2.1.0.

## Earlier versions

See the [releases page](https://github.com/elisvathi/tessera/releases).
