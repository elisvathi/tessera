# Tessera user manual

Tessera is a sequencer for [VCV Rack 2](https://vcvrack.com). It brings the tools of a music program (a "DAW",
a "digital audio workstation") into your rack: a piano roll, a drum editor, clips you can launch, a
timeline, automation and note effects. It plays your music as voltages (CV and gates) that drive any module in the
patch.

![The Tessera editor](images/editor-overview.png)

## Chapters

1. [Getting started](01-getting-started.md): your first sound in five minutes.
2. [The modules](02-modules.md): Tessera, Track Out, Drum Out and Auto Out, their jacks and menus.
3. [The editor](03-editor.md): the layout, moving around, undo, and the small editor inside the panel.
4. [Tracks](04-tracks.md): adding, naming, muting, grouping, and what a track plays.
5. [Clips and scenes](05-clips-and-scenes.md): launching clips live, scenes, and the Plays switch.
6. [The timeline](06-timeline.md): arranging clips into a song.
7. [The piano roll](07-piano-roll.md): drawing and editing notes.
8. [Shaping notes](08-shaping-notes.md): the inspector, chance and repeats, bends, transforms, MIDI channels.
9. [Drums](09-drums.md): the drum editor, drum lanes, kits and the Drum Out module.
10. [Keys, scales and chords](10-scales-and-chords.md): keeping notes in a key and drawing chords.
11. [Recording](11-recording.md): playing notes in from MIDI, CV or your computer keyboard.
12. [Automation](12-automation.md): curves that move voltages and knobs over time.
13. [Devices and modulators](13-devices.md): arpeggiator, chord, echo, LFOs and more.
14. [Files, export and import](14-files.md): projects, MIDI files and DAWproject.
15. [Keyboard shortcuts](15-shortcuts.md): every key in one place.
16. [Problems and answers](16-troubleshooting.md): what to check when something doesn't work.

## Words used in this manual

| Word | What it means |
|---|---|
| **Track** | One part of your song, like "Bass" or "Drums". Each track sends its notes to its own output module. |
| **Clip** | A short piece of music on a track, for example a 4-bar bass line. Clips loop. |
| **Scene** | A row of clips across all tracks that you start together, like "Verse" or "Chorus". |
| **Timeline** | The song laid out from left to right in bars. Also called the arranger. |
| **Launch** | Start a clip (or a scene) playing. |
| **V/OCT** | A pitch voltage: 1 volt per octave. 0 V is middle C (C4). |
| **Gate** | A voltage that is high (10 V) while a note is held and low (0 V) when it is not. |
| **Poly cable** | One cable that carries up to 16 voices, one per channel. Tessera plays chords this way. |
| **Velocity** | How hard a note is played, from 0 to 100%. Sent as 0 to 10 V. |
| **Snap** | Notes and clips jump to the nearest grid line while you draw or drag them. |
| **Undo** | Ctrl+Z (Cmd+Z on a Mac) takes back the last change. Every change in Tessera can be undone. |

On a Mac, read **Cmd** wherever this manual says **Ctrl**, and **Option** for **Alt**.
