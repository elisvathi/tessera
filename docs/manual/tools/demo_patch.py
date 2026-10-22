#!/usr/bin/env python3
"""Writes the demo patches used for the user manual's screenshots.

Run it with an output folder: `demo_patch.py build/manual-rack`. It writes demo.vcv (a small song with its output
modules), panel.vcv (the same song in an 80 HP panel editor), modules.vcv (the modules zoomed in), first.vcv
(Tessera, a Track Out and a simple voice from Fundamental, to patch by hand) and fresh.vcv (a new Tessera alone).
"""

import json
import sys
from pathlib import Path

BEAT = 960
BAR = 4 * BEAT

TESSERA_ID = 1001
TRACK_IDS = {"Bass": 1, "Chords": 2, "Lead": 3, "Drums": 4}
COLORS = {"Bass": 0x4498E8, "Chords": 0xDA743C, "Lead": 0x9B7FE2, "Drums": 0x1AAE74}

STANDARD_8 = [
    ("Kick", 36, 0, 0), ("Snare", 38, 1, 0), ("Clap", 39, 2, 0), ("Closed hat", 42, 3, 1),
    ("Open hat", 46, 4, 1), ("Low tom", 45, 5, 0), ("High tom", 50, 6, 0), ("Rim", 37, 7, 0),
]


class Ids:
    """Hands out the song's unique IDs (notes, clips, instances, lanes, markers), as the model's counter does."""

    def __init__(self, start):
        self.next = start

    def take(self):
        """Returns a new ID."""
        self.next += 1
        return self.next - 1


ids = Ids(100)


def note(start, length, pitch, vel=0.8):
    """One note in the compact saved form: start, length, pitch, velocity, release, micro offset, flags, ID."""
    return [start, length, pitch, vel, 0.5, 0.0, 0, ids.take()]


def clip(name, bars, notes, color):
    """A looping clip of whole bars."""
    return {"id": ids.take(), "name": name, "color": color, "length": bars * BAR, "loop": [0, bars * BAR],
            "loopMode": "loop", "notes": sorted(notes, key=lambda n: (n[0], n[2])), "automation": []}


def chord_bar(bar, pitches, vel=0.72):
    """A bar of chord stabs on a 3 + 3 + 2 eighth-note rhythm."""
    out = []
    for start, length in ((0, 3 * BEAT // 2), (3 * BEAT // 2, 3 * BEAT // 2), (3 * BEAT, BEAT)):
        out += [note(bar * BAR + start, length - 60, p, vel) for p in pitches]
    return out


def bass_bar(bar, root):
    """A bar of eighth notes on the root, jumping an octave on the off-beats of beats 2 and 4."""
    out = []
    for i in range(8):
        pitch = root + 12 if i in (3, 7) else root
        out.append(note(bar * BAR + i * BEAT // 2, BEAT // 2 - 80, pitch, 0.9 if i % 2 == 0 else 0.65))
    return out


def drum_bar(kick, snare, hats, clap=(), open_hat=()):
    """One bar of drums; each argument lists 1/16 step numbers (0-15)."""
    step = BEAT // 4
    out = []
    for pitch, steps, vel in ((36, kick, 0.95), (38, snare, 0.9), (42, hats, 0.6), (39, clap, 0.8),
                              (46, open_hat, 0.7)):
        out += [note(s * step, step, pitch, vel if s % 4 == 0 else vel * 0.8) for s in steps]
    return out


def make_song():
    """The demo song: A minor, three sections, launcher scenes and a timeline that plays them in order."""
    chords_verse = clip("Verse chords", 4, chord_bar(0, [57, 60, 64]) + chord_bar(1, [53, 57, 60]) +
                        chord_bar(2, [55, 60, 64]) + chord_bar(3, [55, 59, 62]), COLORS["Chords"])
    chords_chorus = clip("Chorus chords", 4, chord_bar(0, [53, 57, 60, 65], 0.8) + chord_bar(1, [55, 59, 62, 67], 0.8) +
                         chord_bar(2, [57, 60, 64, 69], 0.85) + chord_bar(3, [57, 60, 64, 69], 0.75), COLORS["Chords"])
    bass_verse = clip("Verse bass", 4, bass_bar(0, 45) + bass_bar(1, 41) + bass_bar(2, 48) + bass_bar(3, 43),
                      COLORS["Bass"])
    bass_chorus = clip("Chorus bass", 4, bass_bar(0, 41) + bass_bar(1, 43) + bass_bar(2, 45) + bass_bar(3, 45),
                       COLORS["Bass"])
    e = BEAT // 2
    melody = [(0, 3, 72), (3, 1, 74), (4, 2, 76), (6, 2, 74), (8, 3, 79), (11, 1, 77), (12, 4, 76),
              (16, 2, 72), (18, 2, 74), (20, 2, 76), (22, 2, 77), (24, 6, 76), (30, 2, 72)]
    lead = clip("Hook", 4, [note(s * e, n * e - 40, p, 0.85) for s, n, p in melody], COLORS["Lead"])
    drums_intro = clip("Intro beat", 1, drum_bar([0, 8], [], range(0, 16, 2)), COLORS["Drums"])
    drums_verse = clip("Verse beat", 1, drum_bar([0, 6, 8], [4, 12], range(0, 16, 2)), COLORS["Drums"])
    drums_chorus = clip("Chorus beat", 1, drum_bar([0, 6, 8, 10], [4, 12], range(16), clap=[12], open_hat=[14]),
                        COLORS["Drums"])

    def place(c, bar, bars):
        """A timeline instance of clip `c` from `bar`, `bars` long (loops fill the rest)."""
        return {"id": ids.take(), "clip": c["id"], "pos": bar * BAR, "len": bars * BAR, "offset": 0, "muted": False,
                "transpose": 0, "velScale": 1.0, "stretch": 1.0}

    def track(name, kind, slots, arrangement, automation=()):
        """A track with its launcher slots (8 scenes) and timeline clips."""
        lanes = [{"name": n, "pitch": p, "output": o, "mute": False, "solo": False, "gateMs": 0.0, "choke": ch,
                  "loop": 0, "step": 240, "color": 0} for n, p, o, ch in STANDARD_8] if kind == "drum" else []
        return {"id": TRACK_IDS[name], "name": name, "color": COLORS[name], "type": kind, "polyphony": 0,
                "mute": False, "solo": False, "armed": False, "monitor": True, "recordInput": "any",
                "midiChannel": 0, "gateScale": 1.0, "transpose": 0, "drumLanes": lanes,
                "slots": [s["id"] if s else None for s in slots] + [None] * (8 - len(slots)),
                "arrangement": arrangement, "automation": list(automation)}

    cutoff = {"id": ids.take(), "name": "Cutoff", "color": 0, "output": 0, "range": [0.0, 10.0], "bipolar": False,
              "mute": False, "slew": 0.0, "combine": "override", "source": "knob", "midi": ["cc", 74],
              "recordMode": "touch", "targets": [],
              "points": [[0, 0.15, 1, 0.0], [4 * BAR, 0.35, 1, 0.0], [12 * BAR, 0.45, 3, 0.0],
                         [13 * BAR, 0.85, 1, 0.0], [20 * BAR, 0.6, 1, 0.0]]}

    tracks = [
        track("Bass", "instrument", [None, bass_verse, bass_chorus],
              [place(bass_verse, 4, 8), place(bass_chorus, 12, 8)]),
        track("Chords", "instrument", [chords_verse, chords_verse, chords_chorus],
              [place(chords_verse, 0, 12), place(chords_chorus, 12, 8)], [cutoff]),
        track("Lead", "instrument", [None, None, lead], [place(lead, 12, 8)]),
        track("Drums", "drum", [drums_intro, drums_verse, drums_chorus],
              [place(drums_intro, 0, 4), place(drums_verse, 4, 8), place(drums_chorus, 12, 8)]),
    ]
    scenes = [{"name": n, "color": 0} for n in ("Intro", "Verse", "Chorus")] + [{"name": "", "color": 0}] * 5
    markers = [{"id": ids.take(), "pos": b * BAR, "name": n, "color": 0}
               for b, n in ((0, "Intro"), (4, "Verse"), (12, "Chorus"))]
    clips = [chords_verse, chords_chorus, bass_verse, bass_chorus, lead, drums_intro, drums_verse, drums_chorus]
    return {"timeSig": [4, 4], "swing": 0.0, "scale": {"root": 9, "scale": "minor"}, "customScales": [],
            "loop": [12 * BAR, 20 * BAR], "seed": 1, "launchQuantize": "1bar", "nextId": ids.next + 10,
            "tracks": tracks, "groups": [], "scenes": scenes, "clips": clips, "markers": markers}, lead


def tessera_module(song, selected_clip, panel_hp, plays_timeline):
    """The Tessera module with the song; params are play, reset, loop, tap, BPM, scene buttons, Plays, rec, fill."""
    params = [0.0] * 12
    params[4] = 112.0
    params[9] = 1.0 if plays_timeline else 0.0
    ui = {"selectedTrack": TRACK_IDS["Lead"], "selectedClip": selected_clip["id"], "panelHp": panel_hp,
          "overlay": {"openAutomation": [TRACK_IDS["Chords"]]}}
    return {"id": TESSERA_ID, "plugin": "Tessera", "model": "Tessera", "version": "2.0.0",
            "params": [{"value": v, "id": i} for i, v in enumerate(params)], "pos": [0, 0],
            "data": {"schema": 5, "name": "Tessera", "savedId": TESSERA_ID, "launcher": [], "song": song, "ui": ui}}


def output_module(mid, model, track, x):
    """A Track Out, Drum Out or Auto Out bound to `track` of the demo Tessera."""
    return {"id": mid, "plugin": "Tessera", "model": model, "version": "2.0.0", "params": [], "pos": [x, 0],
            "data": {"source": TESSERA_ID, "sourceName": "Tessera", "track": TRACK_IDS[track],
                     "followSelected": False}}


def patch(modules, zoom=1.0):
    """A Rack 2 patch holding `modules` at the given rack zoom."""
    return {"version": "2.6.6", "zoom": zoom, "gridOffset": [0.0, 0.0], "modules": modules, "cables": []}


def fundamental(mid, model, x):
    """A module from VCV's free Fundamental plugin, placed at HP `x`."""
    return {"id": mid, "plugin": "Fundamental", "model": model, "version": "2.6.4", "params": [], "pos": [x, 0]}


def outputs(start):
    """The demo's output modules, placed from HP `start`: three Track Outs, a Drum Out and an Auto Out."""
    return [output_module(2001, "TesseraTrackOut", "Bass", start),
            output_module(2002, "TesseraTrackOut", "Chords", start + 6),
            output_module(2003, "TesseraTrackOut", "Lead", start + 12),
            output_module(2004, "TesseraDrumOut", "Drums", start + 18),
            output_module(2005, "TesseraAutoOut", "Chords", start + 28)]


def main():
    """Writes the patches into the folder given on the command line."""
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    song, lead = make_song()
    (out / "demo.vcv").write_text(json.dumps(patch([tessera_module(song, lead, 24, True)] + outputs(24))))
    (out / "panel.vcv").write_text(json.dumps(patch([tessera_module(song, lead, 80, True)] + outputs(80))))
    (out / "modules.vcv").write_text(json.dumps(patch([tessera_module(song, lead, 24, True)] + outputs(24), 1.6)))
    first = [tessera_module(song, lead, 24, False), output_module(2001, "TesseraTrackOut", "Bass", 24),
             fundamental(3001, "VCO", 31), fundamental(3002, "ADSR", 42), fundamental(3003, "VCA-1", 52)]
    (out / "first.vcv").write_text(json.dumps(patch(first, 1.3)))
    (out / "fresh.vcv").write_text(json.dumps(patch([{"id": TESSERA_ID, "plugin": "Tessera", "model": "Tessera",
                                                      "version": "2.0.0", "params": [], "pos": [0, 0]}])))


if __name__ == "__main__":
    main()
