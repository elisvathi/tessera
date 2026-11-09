#!/usr/bin/env python3
"""Writes the demo patches used for the user manual's screenshots.

Run it with an output folder: `demo_patch.py build/manual-rack`. It writes demo.vcv (a small song with its output
modules), panel.vcv (the same song in an 80 HP panel editor), modules.vcv (the modules zoomed in), first.vcv
(Roll, a Track Out and a simple voice from Fundamental, to patch by hand), fresh.vcv (a new Roll alone) and
polymeter.vcv (the demo with the Drums track open and its closed hat on a 3-step loop of its own), playheads.vcv
(Lead read by three playheads), curve.vcv (the Bass track open, a 16-bar Curve on its Transpose's key steps that
plays bars 5 to 8), hitvalues.vcv (the Drums track open on its Pitch lane: the snares tuned up step by step, the
second a shaped curve, the hats with timbre) and audio.vcv (the demo with an audio track, "Loop", playing a
synthesised 112 BPM drum loop from bar 5, its file in the patch's storage; Rack's patch archive format, so it carries
the file).
"""

import json
import math
import random
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

BEAT = 960
BAR = 4 * BEAT

TESSERA_ID = 1001
TRACK_IDS = {"Bass": 1, "Chords": 2, "Lead": 3, "Drums": 4, "Loop": 5}
COLORS = {"Bass": 0x4498E8, "Chords": 0xDA743C, "Lead": 0x9B7FE2, "Drums": 0x1AAE74, "Loop": 0xD79628}
PANEL_HP = 30  # Tessera's own width

# The model's 16 track colours (src/model/TrackColors.hpp); a kit lane takes colour (output * 5) % 16, as new kits do.
TRACK_COLORS = [0xEF806F, 0xE88949, 0xD79628, 0xBDA422, 0x9AB140, 0x6DBA67, 0x2DC08E, 0x00C0B4,
                0x00BBD5, 0x24B2ED, 0x65A7FA, 0x909BFB, 0xB28FEF, 0xCE85D8, 0xE17EB9, 0xED7C95]

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
                  "loop": 0, "step": 240, "color": TRACK_COLORS[(o * 5) % 16]} for n, p, o, ch in STANDARD_8] if kind == "drum" else []
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


def tessera_module(song, selected_clip, panel_hp, plays_timeline, selected_track="Lead", note_lane=None):
    """The Roll module with the song; params are play, reset, loop, tap, BPM, scene buttons, Plays, rec, fill."""
    params = [0.0] * 12
    params[4] = 112.0
    params[9] = 1.0 if plays_timeline else 0.0
    ui = {"selectedTrack": TRACK_IDS[selected_track], "selectedClip": selected_clip["id"], "panelHp": panel_hp,
          "overlay": {"openAutomation": [TRACK_IDS["Chords"]]}}
    if note_lane:
        ui["overlay"]["noteLane"] = note_lane
    return {"id": TESSERA_ID, "plugin": "Tessera", "model": "Roll", "version": "2.0.0",
            "params": [{"value": v, "id": i} for i, v in enumerate(params)], "pos": [0, 0],
            "data": {"schema": 8, "name": "Roll", "savedId": TESSERA_ID, "launcher": [], "song": song, "ui": ui}}


def output_module(mid, model, track, x):
    """A Track Out, Drum Out or Auto Out bound to `track` of the demo Tessera."""
    data = {"source": TESSERA_ID, "sourceName": "Roll", "track": TRACK_IDS[track],
            "followSelected": False}
    return {"id": mid, "plugin": "Tessera", "model": model, "version": "2.0.0", "params": [], "pos": [x, 0],
            "data": data}


def patch(modules, zoom=1.0):
    """A Rack 2 patch holding `modules` at the given rack zoom."""
    return {"version": "2.6.6", "zoom": zoom, "gridOffset": [0.0, 0.0], "modules": modules, "cables": []}


def fundamental(mid, model, x):
    """A module from VCV's free Fundamental plugin, placed at HP `x`."""
    return {"id": mid, "plugin": "Fundamental", "model": model, "version": "2.6.4", "params": [], "pos": [x, 0]}


def outputs(start):
    """The demo's output modules, placed from HP `start`: three Track Outs, a Drum Out and an Auto Out."""
    return [output_module(2001, "TrackOut", "Bass", start),
            output_module(2002, "TrackOut", "Chords", start + 6),
            output_module(2003, "TrackOut", "Lead", start + 12),
            output_module(2004, "DrumOut", "Drums", start + 18),
            output_module(2005, "AutoOut", "Chords", start + 38)]


def polymeter_song():
    """The demo song with the verse beat's closed hat on a 3-step loop of its own, one hit long: the clip loops the
    3 bars the hat needs to line up with the bar again. Returns the song and the verse beat."""
    song, _ = make_song()
    drums = next(t for t in song["tracks"] if t["name"] == "Drums")
    hat = next(l for l in drums["drumLanes"] if l["pitch"] == 42)
    hat["loop"] = 3 * BEAT // 4
    verse = next(c for c in song["clips"] if c["name"] == "Verse beat")
    verse["notes"] = sorted([n for n in verse["notes"] if n[2] != 42] + [note(0, BEAT // 4, 42, 0.7)],
                            key=lambda n: (n[0], n[2]))
    return song, verse


def hit_values_song():
    """The demo song with values on the verse beat's hits: the two snares at 0 and +3 semitones, the second with a
    falling curve, the closed hats' timbre opening up across the bar. Returns the song and the verse beat."""
    song, _ = make_song()
    verse = next(c for c in song["clips"] if c["name"] == "Verse beat")
    snares = [n for n in verse["notes"] if n[2] == 38]
    hats = [n for n in verse["notes"] if n[2] == 42]
    snares[1].append({"pitch": [[0.0, 7.0, 0.0], [0.6, 3.0, 0.3], [1.0, 3.0, 0.0]]})
    for i, n in enumerate(hats):
        n.append({"timbre": [[0.0, round(0.2 + 0.1 * i, 2), 0.0]]})
    kicks = [n for n in verse["notes"] if n[2] == 36]
    for n, st in zip(kicks, [0, -2, 2]):
        if st:
            n.append({"pitch": [[0.0, float(st), 0.0]]})
    return song, verse


def playheads_module():
    """The demo song with a Playheads device on Lead: three playheads (Sparkle off), the first with its own Echo,
    then a Scale for all of them. The device strip is open. Sparkle is off by the old "mute" switch, which loads as its
    On setting off (the settings list stays empty, so every other setting is its default)."""
    song, lead = make_song()
    for t in song["tracks"]:
        if t["name"] != "Lead":
            continue
        echo = {"id": ids.take(), "type": "echo", "name": "", "on": True, "params": []}
        heads = [{"id": ids.take(), "name": name, "color": color, "params": [], "channel": ch, "mute": mute,
                  "branch": branch}
                 for name, color, ch, mute, branch in (("Melody", 0, 0, False, [echo]), ("Bass", 1, 2, False, []),
                                                       ("Sparkle", 3, 3, True, []))]
        t["devices"] = [{"id": ids.take(), "type": "playheads", "name": "", "on": True, "params": [],
                         "playheads": heads},
                        {"id": ids.take(), "type": "scale", "name": "", "on": True, "params": []}]
    song["nextId"] = ids.next + 10
    module = tessera_module(song, lead, PANEL_HP, False)
    module["data"]["schema"] = 8
    module["data"]["ui"]["overlay"]["deviceStrip"] = True
    return module


def curve_module():
    """The demo song with the Bass track open: a Transpose device whose Key steps a Curve modulator moves. The curve
    is 16 bars of steps and one ramp; its segment plays bars 5 to 8. The device strip is open."""
    song, _ = make_song()
    bass = next(t for t in song["tracks"] if t["name"] == "Bass")
    verse = next(c for c in song["clips"] if c["name"] == "Verse bass")
    transpose = {"id": ids.take(), "type": "transpose", "name": "", "on": True, "params": []}
    bass["devices"] = [transpose]

    def at(steps):
        """The drawn value for `steps` key steps from the knob: Key steps span 28, turned all the way."""
        return 0.5 + steps / 56

    steps = [0, 0, 2, 4, 5, 4, 2, 0, -3, -3, -1, 0]
    points = [[bar * BAR, at(v), 0, 0.0] for bar, v in enumerate(steps)]
    points += [[12 * BAR, at(7), 1, 0.0], [15 * BAR, at(2), 0, 0.0]]
    bass["modulators"] = [{"id": ids.take(), "type": "curve", "name": "Bass Key steps", "params": [],
                           "length": 16 * BAR, "points": points, "segment": [4 * BAR, 8 * BAR],
                           "routes": [{"kind": "param", "amount": 1.0, "target": ["device", transpose["id"], 2]}]}]
    song["nextId"] = ids.next + 10
    module = tessera_module(song, verse, PANEL_HP, False, "Bass")
    module["data"]["schema"] = 8
    module["data"]["ui"]["overlay"]["deviceStrip"] = True
    return module


def drum_loop(path, bpm=112.0, bars=2, rate=44100):
    """Writes a stereo drum loop to `path`: kick, snare and closed hats, synthesised so the file is free to use."""
    beat = 60.0 / bpm
    n = int(round(bars * 4 * beat * rate))
    left, right = [0.0] * n, [0.0] * n
    rnd = random.Random(7)

    def hit(at, length, sample, pan=0.0):
        start = int(round(at * rate))
        for i in range(int(length * rate)):
            if start + i >= n:
                break
            v = sample(i / rate)
            left[start + i] += v * (1 - max(0.0, pan))
            right[start + i] += v * (1 + min(0.0, pan))

    kick = lambda t: 0.9 * math.sin(2 * math.pi * (50 * t + 120 * (1 - math.exp(-t * 25)) / 25)) * math.exp(-t * 7)
    snare = lambda t: (0.45 * (rnd.random() * 2 - 1) + 0.25 * math.sin(2 * math.pi * 190 * t)) * math.exp(-t * 18)
    hat = lambda t: 0.22 * (rnd.random() * 2 - 1) * math.exp(-t * 70)
    for bar in range(bars):
        t0 = bar * 4 * beat
        for k in (0, 1.5, 2) if bar == 0 else (0, 1.5, 2.5):
            hit(t0 + k * beat, 0.5, kick)
        for k in (1, 3):
            hit(t0 + k * beat, 0.3, snare)
        for k in range(8):
            hit(t0 + k * beat / 2, 0.08, hat, 0.3)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<hh", max(-32767, min(32767, int(a * 30000))),
                                           max(-32767, min(32767, int(b * 30000)))) for a, b in zip(left, right)))
    return n


def audio_patch(out, crossfade=False):
    """Writes audio.vcv: the demo with a "Loop" audio track playing a drum loop on the timeline from bar 5 and in
    the Verse and Chorus slots, its file in the patch storage, and the Loop clip selected with the mixer showing in the lower pane.
    With `crossfade` it writes crossfade.vcv instead: two copies of the loop overlapping by a bar, the second one
    reversed, and a curved fade in on the first, with the notes showing in the lower pane."""
    song, _ = make_song()
    name = "Drum loop 112 BPM.wav"
    file = "audio/00000000-" + name
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        storage = root / "modules" / str(TESSERA_ID) / "audio"
        storage.mkdir(parents=True)
        frames = drum_loop(storage / Path(file).name)
        source = {"id": ids.take(), "name": name, "file": file, "hash": "0000000000000000", "channels": 2,
                  "rate": 44100.0, "frames": frames, "tempo": 112.0, "volts": 5.0}
        loop = {"id": ids.take(), "name": "Drum loop 112 BPM", "color": 0, "length": 2 * BAR, "loop": [0, 2 * BAR],
                "loopMode": "loop", "notes": [], "automation": [],
                "audio": {"source": source["id"], "start": 0, "follow": "stretch", "sound": "drums", "tempo": 112.0}}
        place = {"id": ids.take(), "clip": loop["id"], "pos": 4 * BAR, "len": 16 * BAR, "offset": 0, "muted": False,
                 "transpose": 0, "velScale": 1.0, "stretch": 1.0, "fadeIn": [BEAT // 2, 0.0]}
        places = [place]
        if crossfade:
            back = dict(loop, id=ids.take(), name="Drum loop reversed", color=2,
                        audio=dict(loop["audio"], reverse=True))
            place = dict(place, len=4 * BAR, fadeIn=[2 * BEAT, 0.6])
            second = {"id": ids.take(), "clip": back["id"], "pos": 7 * BAR, "len": 4 * BAR, "offset": 0,
                      "muted": False, "transpose": 0, "velScale": 1.0, "stretch": 1.0}
            places = [place, second]
            song["clips"].append(back)
        song["tracks"].append({"id": TRACK_IDS["Loop"], "name": "Loop", "color": COLORS["Loop"], "type": "audio",
                               "polyphony": 0, "mute": False, "solo": False, "armed": False, "monitor": True,
                               "recordInput": "any", "midiChannel": 0, "gateScale": 1.0, "transpose": 0,
                               "drumLanes": [], "slots": [None, loop["id"], loop["id"]] + [None] * 5,
                               "arrangement": places, "automation": [], "channel": {"volume": -3.0, "pan": 0.0}})
        song["clips"].append(loop)
        song["audioSources"] = [source]
        song["master"] = {"id": ids.take(), "volume": 0.0}
        song["nextId"] = ids.next + 10
        tessera = tessera_module(song, loop, PANEL_HP, True, "Loop")
        tessera["data"]["ui"]["overlay"].update({"mixerOpen": not crossfade, "mixerHeight": 300.0})
        (root / "patch.json").write_text(json.dumps(patch([tessera] + outputs(PANEL_HP))))
        name = "crossfade.vcv" if crossfade else "audio.vcv"
        subprocess.run(["tar", "--zstd", "-cf", str(out / name), "-C", tmp, "patch.json", "modules"], check=True)


def main():
    """Writes the patches into the folder given on the command line."""
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    song, lead = make_song()
    hp = PANEL_HP
    (out / "demo.vcv").write_text(json.dumps(patch([tessera_module(song, lead, hp, True)] + outputs(hp))))
    (out / "panel.vcv").write_text(json.dumps(patch([tessera_module(song, lead, 80, True)] + outputs(80))))
    (out / "modules.vcv").write_text(json.dumps(patch([tessera_module(song, lead, hp, True)] + outputs(hp), 1.6)))
    first = [tessera_module(song, lead, hp, False), output_module(2001, "TrackOut", "Bass", hp),
             fundamental(3001, "VCO", hp + 7), fundamental(3002, "ADSR", hp + 18), fundamental(3003, "VCA-1", hp + 28)]
    (out / "first.vcv").write_text(json.dumps(patch(first, 1.3)))
    poly, verse = polymeter_song()
    (out / "polymeter.vcv").write_text(json.dumps(patch([tessera_module(poly, verse, hp, False, "Drums")] +
                                                        outputs(hp))))
    values, beat = hit_values_song()
    (out / "hitvalues.vcv").write_text(json.dumps(patch([tessera_module(values, beat, hp, False, "Drums", "pitch")] +
                                                         outputs(hp))))
    (out / "playheads.vcv").write_text(json.dumps(patch([playheads_module()] + outputs(hp))))
    (out / "curve.vcv").write_text(json.dumps(patch([curve_module()] + outputs(hp))))
    (out / "fresh.vcv").write_text(json.dumps(patch([{"id": TESSERA_ID, "plugin": "Tessera", "model": "Roll",
                                                      "version": "2.0.0", "params": [], "pos": [0, 0]}])))
    audio_patch(out)
    audio_patch(out, crossfade=True)


if __name__ == "__main__":
    main()
