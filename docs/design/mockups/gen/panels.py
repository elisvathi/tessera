"""Module panel mockups in Rack px (1 HP = 15 px, panel height 380 px): Tessera (default and widened),
Track Out, Drum Out (proposed: lane names + bank switch), Auto Out, Drum Out variants, the Library view, and the
design-system board."""
import base64, math, os, random
from svg import Svg, Theme, icon, ICON_NAMES
from common import *
import colors as C
import editor as E

RACK = "#2B2B2B"
HERE = os.path.dirname(os.path.abspath(__file__))
CURRENT = os.path.join(os.path.dirname(os.path.dirname(HERE)), "current")


def rails(s, x, w, y0=0, h=380):
    for ry in (y0 - 15, y0 + h):
        s.rect(x, ry, w, 15, "#A9ADB3")
        s.rect(x, ry + 1, w, 2, "#D8DBDF")
        s.rect(x, ry + 12, w, 3, "#7E8288")
        for k in range(int(w // 15) + 1):
            s.circle(x + 7.5 + k * 15, ry + 8, 2.4, "#45484D")


def screw(s, T, cx, cy):
    ring = "#5C626C" if T.dark else "#B3AEA6"
    s.circle(cx, cy, 5, ring)
    s.circle(cx, cy, 4, "#30353D" if T.dark else "#DAD5CD")
    s.path(f"M{cx - 2.4} {cy - 2.4} L{cx + 2.4} {cy + 2.4} M{cx + 2.4} {cy - 2.4} L{cx - 2.4} {cy + 2.4}",
           stroke=ring, sw=1.1)


def jack(s, T, cx, cy, on_plate=False):
    """Rack's themed PJ301M look: dark nut on dark panels, light nut on light ones (inverted on the plate)."""
    darkface = T.dark != on_plate
    nut = "#9AA0AA" if darkface else "#C9C4BC"
    s.circle(cx, cy + 1, 11, "#000", op=0.30 if darkface else 0.12)
    s.circle(cx, cy, 11, nut)
    s.circle(cx, cy, 9.2, "#D5D9DF" if darkface else "#F4F1EC")
    s.circle(cx, cy, 6.6, "#202328" if darkface else "#3A3E45")
    s.circle(cx, cy, 4.2, "#08090B")


def label(s, x, y, text, color, size=7.6, weight=700, anchor="middle", ls=0.8):
    s.text(x, y, text, size, color, UI, weight, anchor, ls=ls)


def panel_face(s, T, x, w, h=380):
    s.rect(x, 0, w, h, T.panel)
    if T.dark:
        s.rect(x, 0, w, 1, "#FFFFFF", op=0.06)
    s.rect(x + 0.5, 0.5, w - 1, h - 1, "none", stroke="#000", sw=1, sop=0.45 if T.dark else 0.18)


def wordmark(s, T, cx, y, text, size=11, mark=True):
    tw_ = tw(text, size, weight=700) * 1.25 + len(text) * 2.2
    if mark:
        logo(s, T, cx - tw_ / 2 - 12, y - 10, 0.82)
        s.text(cx + 8, y, text, size, T.panelText, UI, 700, "middle", ls=2.4)
    else:
        s.text(cx, y, text, size, T.panelText, UI, 700, "middle", ls=2.0)


def plate(s, T, x, y, w, h):
    """Outputs sit on an inverted plate (Rack convention): plaster on dark panels, ink on light ones."""
    s.rect(x, y, w, h, T.plate, r=6)


def tile_button(s, T, cx, cy, ico=None, lit=None, size=22, lbl=None, text=None, wide=None):
    w = wide or size
    x, y = cx - w / 2, cy - size / 2
    base = T.raised if T.dark else "#FFFFFF"
    s.rect(x, y + 1.3, w, size, "#000", r=5, op=0.40 if T.dark else 0.14)
    s.rect(x, y, w, size, base, r=5)
    if lit:
        s.rect(x, y, w, size, lit, r=5, op=0.20 if T.dark else 0.14)
        s.rect(x + 0.7, y + 0.7, w - 1.4, size - 1.4, "none", r=4.5, stroke=lit, sw=1.4)
    elif T.dark:
        s.line(x + 4, y + 0.6, x + w - 4, y + 0.6, "#fff", 1, op=0.08)
    else:
        s.rect(x + 0.5, y + 0.5, w - 1, size - 1, "none", r=5, stroke="#3A2F1E", sw=1, sop=0.16)
    fg = lit or T.text1
    if ico:
        icon(s, ico, cx, cy, fg, size * 0.6)
    if text:
        s.text(cx, cy + 3.3, text, 8.5, fg, UI, 700, "middle", ls=0.6)
    if lbl:
        label(s, cx, cy + size / 2 + 10.5, lbl, T.text1 if T.dark else T.text1)


def knob(s, T, cx, cy, r=16, frac=0.45):
    a0, a1 = math.radians(135), math.radians(405)
    av = a0 + (a1 - a0) * frac

    def arc(rad, aa, bb):
        x0, y0 = cx + rad * math.cos(aa), cy + rad * math.sin(aa)
        x1, y1 = cx + rad * math.cos(bb), cy + rad * math.sin(bb)
        large = 1 if (bb - aa) > math.pi else 0
        return f"M{x0:.2f} {y0:.2f} A{rad} {rad} 0 {large} 1 {x1:.2f} {y1:.2f}"
    s.path(arc(r + 5, a0, a1), stroke=T.line2, sw=2.4)
    s.path(arc(r + 5, a0, av), stroke=T.accent, sw=2.4)
    s.circle(cx, cy + 1.6, r, "#000", op=0.40 if T.dark else 0.15)
    s.circle(cx, cy, r, T.raised if T.dark else "#FFFFFF")
    if not T.dark:
        s.circle(cx, cy, r - 0.5, "none", stroke="#3A2F1E", sw=1)
    s.line(cx + r * 0.2 * math.cos(av), cy + r * 0.2 * math.sin(av), cx + (r - 3.5) * math.cos(av),
           cy + (r - 3.5) * math.sin(av), T.text0, 2.2, cap="round")


def screen(s, T, x, y, w, h, r=5):
    D = T.D
    s.rect(x, y, w, h, D["screen"], r=r)
    s.rect(x + 0.5, y + 0.5, w - 1, h - 1, "none", r=r, stroke="#000" if not T.dark else "#FFFFFF", sw=1,
           sop=0.25 if not T.dark else 0.06)


def mosaic(s, T, x, y, w, h, scenes=8, demo=True):
    """The panel's signature: one tile per clip slot, tracks down, scenes across. Lit = playing, dashed gold =
    queued, the framed column = the scene LAUNCH starts."""
    D = T.D
    rows = len(TRACKS)
    head = 13
    g = 2
    tile_w = 9
    cw = (w - tile_w - 4 - g * (scenes - 1)) / scenes
    ch = (h - head - g * (rows - 1)) / rows
    sel = 1
    for sc in range(scenes):
        cx = x + tile_w + 4 + sc * (cw + g)
        nm = SCENES[sc][0] or str(sc + 1)
        if sc == sel:
            s.text(cx + cw / 2, y + 8, nm.upper(), 6.5, D["text0"], UI, 700, "middle", ls=0.5)
        else:
            s.text(cx + cw / 2, y + 8, str(sc + 1), 6.5, D["text2"], MONO, 500, "middle")
    for ti in range(rows):
        cy = y + head + ti * (ch + g)
        color = T.screenTracks[TRACKS[ti][1]]
        s.rect(x, cy, tile_w, ch, color, r=1.5)
        for sc in range(scenes):
            cx = x + tile_w + 4 + sc * (cw + g)
            info = SLOTS[ti].get(sc)
            if not info:
                s.rect(cx, cy, cw, ch, D["line1"], r=1.5)
                continue
            st = info[1]
            if st == "playing":
                s.rect(cx, cy, cw, ch, color, r=1.5)
                s.rect(cx + 1.5, cy + ch - 2.2, (cw - 3) * 0.6, 1.2, D["screen"], op=0.8)
            elif st == "queued":
                s.rect(cx, cy, cw, ch, color, r=1.5, op=0.35)
                s.rect(cx + 0.6, cy + 0.6, cw - 1.2, ch - 1.2, "none", r=1.2, stroke=D["queue"], sw=1.1, dash="2 1.5")
            elif st == "recording":
                s.rect(cx, cy, cw, ch, D["record"], r=1.5)
            else:
                s.rect(cx, cy, cw, ch, color, r=1.5, op=0.35)
    fx = x + tile_w + 4 + sel * (cw + g)
    s.rect(fx - 1.6, y + head - 2, cw + 3.2, h - head + 3, "none", r=2.5, stroke=D["text0"], sw=1.1)


KEYS_NOTES = [  # (start in 16ths over 2 bars, length, pitch) for the panel's mini piano roll
    (0, 6, 62), (0, 6, 58), (0, 6, 55), (6, 2, 60), (8, 4, 63), (8, 4, 58), (8, 4, 55), (12, 4, 67), (14, 2, 65),
    (16, 6, 62), (16, 6, 58), (22, 2, 63), (24, 8, 60), (24, 8, 56), (28, 2, 70), (30, 2, 65)]


def mini_timeline(s, T, x, y, w, h, bars=20, playhead=9.5, loop=(4, 12), dim=False):
    """The song as the editor's timeline: tracks down, clips as tiles with their notes, playhead across."""
    D = T.D
    rows = len(TRACKS)
    g = 1.2
    tile_w = 6
    rh = (h - 5 - g * (rows - 1)) / rows
    x0 = x + tile_w + 3
    pb = (w - tile_w - 3) / bars
    lx0, lx1 = x0 + loop[0] * pb, x0 + loop[1] * pb
    s.rect(lx0, y, lx1 - lx0, 2.5, D["accent"], r=1, op=0.8)
    for ti in range(rows):
        cy = y + 5 + ti * (rh + g)
        color = T.screenTracks[TRACKS[ti][1]]
        s.rect(x, cy, tile_w, rh, color, r=1.2)
        s.rect(x0, cy, w - tile_w - 3, rh, D["line0"], r=1, op=0.6)
        launched = ti in (0, 4)
        for c in CLIPS[ti]:
            st, ln = c[0], c[1]
            if st >= bars:
                continue
            ln = min(ln, bars - st)
            cx, cw = x0 + st * pb + 0.4, ln * pb - 0.8
            op = 0.25 if launched else 0.45
            s.rect(cx, cy, cw, rh, color, r=1, op=op * (0.5 if dim else 1))
            rnd = random.Random(sh(c[2]) + st)
            px = cx + 1
            while px < cx + cw - 2:
                ln_ = rnd.choice([1.5, 3, 4.5])
                s.rect(px, cy + 1 + rnd.random() * (rh - 3), min(ln_, cx + cw - 1 - px), 1, color,
                       op=(0.5 if launched else 1) * (0.5 if dim else 1))
                px += ln_ + rnd.choice([0.8, 1.6, 3])
    phx = x0 + playhead * pb
    s.line(phx, y, phx, y + h, D["text0"], 1, op=0.9)


def mini_roll(s, T, x, y, w, h, ti=2, playhead=9.5):
    """The selected track's clip as a tiny piano roll; notes under the playhead light up."""
    D = T.D
    color = T.screenTracks[TRACKS[ti][1]]
    lo, hi = 55, 70
    rows = hi - lo + 1
    rh = h / rows
    kw = 7
    for i, pch in enumerate(range(hi, lo - 1, -1)):
        black = pch % 12 in (1, 3, 6, 8, 10)
        s.rect(x, y + i * rh, kw, rh, "#9EA3AB" if not black else "#3A3E46")
        if pch % 12 not in MINOR:
            s.rect(x + kw, y + i * rh, w - kw, rh, D["line0"], op=0.35)
    x0 = x + kw + 2
    px16 = (w - kw - 2) / 32
    for b in range(0, 33, 4):
        s.line(x0 + b * px16, y, x0 + b * px16, y + h, D["line1"] if b % 16 else D["line2"], 0.6)
    ph16 = (playhead % 2) * 16
    for st, ln, pch in KEYS_NOTES:
        ny = y + (hi - pch) * rh
        lit = st <= ph16 < st + ln
        s.rect(x0 + st * px16 + 0.4, ny + 0.2, ln * px16 - 0.8, rh - 0.4, color, r=0.8, op=1 if lit else 0.5)
        if lit:
            s.rect(x0 + st * px16 + 0.4, ny + 0.2, ln * px16 - 0.8, rh - 0.4, "none", r=0.8, stroke="#FFFFFF",
                   sw=0.7, sop=0.8)
            s.rect(x, ny, kw, rh, color)
    s.line(x0 + ph16 * px16, y, x0 + ph16 * px16, y + h, D["text0"], 1, op=0.9)


def screen_pill(s, D, x, y, text, color, ico=None):
    w = tw(text, 7, weight=700) + (20 if ico else 12)
    s.rect(x - w, y, w, 12, color, r=6, op=0.20)
    if ico:
        icon(s, ico, x - w + 8, y + 6, color, 7)
    s.text(x - w + (14 if ico else 6), y + 9, text, 7, color, UI, 700, ls=0.5)
    return w


def panel_screen(s, T, sx, sy, sw_, sh_, mode="split", plays="timeline"):
    """Tessera's screen. split = the song (timeline or clip mosaic, whichever plays) over the selected track's
    notes. song / notes = one of the two, larger."""
    D = T.D
    screen(s, T, sx, sy, sw_, sh_)
    s.text(sx + 9, sy + 14, "Main", 9.5, D["text0"], UI, 600)
    rx = sx + sw_ - 8
    rx -= screen_pill(s, D, rx, sy + 5, "TIMELINE" if plays == "timeline" else "CLIPS", D["play"], "play") + 4
    screen_pill(s, D, rx, sy + 5, "LOOP", D["accentText"])
    top = sy + 22
    foot = 17
    body = sh_ - 22 - foot - 4
    if mode == "split":
        song_h = body * 0.52
        notes_h = body - song_h - 6
    elif mode == "song":
        song_h, notes_h = body, 0
    else:
        song_h, notes_h = 0, body
    if song_h:
        if plays == "timeline":
            mini_timeline(s, T, sx + 9, top, sw_ - 18, song_h)
        else:
            mosaic(s, T, sx + 9, top - 2, sw_ - 18, song_h + 2)
    if notes_h:
        ny = top + song_h + (6 if song_h else 0)
        if song_h:
            s.line(sx + 9, ny - 3, sx + sw_ - 9, ny - 3, D["line1"], 0.8)
        mini_roll(s, T, sx + 9, ny, sw_ - 18, notes_h)
    fy = sy + sh_ - 6
    s.text(sx + 9, fy, "9.3.1", 13, D["text0"], MONO, 500)
    s.text(sx + 51, fy, "BAR", 6, D["text2"], UI, 700, ls=0.6)
    color = T.screenTracks[TRACKS[2][1]]
    s.rect(sx + sw_ / 2 - 40, fy - 8, 8, 8, color, r=2)
    s.text(sx + sw_ / 2 - 28, fy, "Keys · Verse A", 9, D["text1"], UI, 600)
    s.text(sx + sw_ - 30, fy, "120.0", 13, D["text0"], MONO, 500, "end")
    s.text(sx + sw_ - 9, fy, "BPM", 6, D["text2"], UI, 700, "end", ls=0.6)


def plays_hw_switch(s, T, x, cy, plays="timeline", w=78, h=22):
    """The panel's PLAYS switch (MODE_PARAM): one button, two lit halves."""
    s.rect(x, cy - h / 2 + 1.3, w, h, "#000", r=5, op=0.40 if T.dark else 0.14)
    s.rect(x, cy - h / 2, w, h, T.raised if T.dark else "#FFFFFF", r=5)
    if not T.dark:
        s.rect(x + 0.5, cy - h / 2 + 0.5, w - 1, h - 1, "none", r=5, stroke="#3A2F1E", sw=1, sop=0.16)
    half = (w - 4) / 2
    for i, (key, word) in enumerate((("clips", "CLIPS"), ("timeline", "TIMELINE"))):
        hx = x + 2 + i * half
        if key == plays:
            s.rect(hx, cy - h / 2 + 2, half, h - 4, T.play, r=3.5)
        s.text(hx + half / 2, cy + 2.6, word, 6.6, T.onState if key == plays else T.text2, UI, 700, "middle",
               ls=0.3)


def tessera_panel(s, T, x, w=360, wide=False, edge_tab=True, screen_mode="split", plays="timeline"):
    panel_face(s, T, x, w)
    for cx in (x + 22, x + 360 - 22):
        screw(s, T, cx, 7.5)
        screw(s, T, cx, 372.5)
    wordmark(s, T, x + 180, 28, "TESSERA", 11.5)
    D = T.D
    sx, sy, sw_, sh_ = x + 12, 38, 336, 126
    panel_screen(s, T, sx, sy, sw_, sh_, screen_mode, plays)
    # transport cluster
    ry = 183
    tile_button(s, T, x + 30, ry, "play", lit=T.play, size=26, lbl="PLAY")
    tile_button(s, T, x + 66, ry, "record", lit=None, size=26, lbl="REC")
    tile_button(s, T, x + 102, ry, "loop", lit=T.accent, size=26, lbl="LOOP")
    tile_button(s, T, x + 138, ry, None, size=22, lbl="RESET", text="↺")
    tile_button(s, T, x + 172, ry, "metronome", size=22, lbl="TAP")
    knob(s, T, x + 222, ry + 2, 15)
    label(s, x + 222, ry + 32, "TEMPO", T.text1)
    # open editor (primary) and mode
    bx = x + 262
    s.rect(bx, ry - 14, 86, 28, T.accent, r=6)
    s.text(bx + 43, ry + 4, "Open editor", 10.5, T.onAccent, UI, 700, "middle")
    # scene cluster
    ry2 = 236
    s.rect(x + 12, ry2 - 18, 214, 52, T.well if T.dark else "#E6E0D6", r=7, op=0.6 if T.dark else 0.6)
    label(s, x + 22, ry2 - 7, "SCENE", T.text2, 6.8, anchor="start")
    tile_button(s, T, x + 36, ry2 + 8, "chev-l", size=20, lbl=None)
    tile_button(s, T, x + 62, ry2 + 8, "chev-r", size=20)
    tile_button(s, T, x + 100, ry2 + 8, "scene", lit=T.queue, size=20, wide=34)
    tile_button(s, T, x + 142, ry2 + 8, "stop", size=20, wide=34)
    tile_button(s, T, x + 186, ry2 + 8, None, size=20, wide=34, text="FILL")
    for cx, t in ((x + 49, "PREV · NEXT"), (x + 100, "LAUNCH"), (x + 142, "STOP ALL"), (x + 186, "")):
        if t:
            label(s, cx, ry2 + 28.5, t, T.text1, 6.6)
    plays_hw_switch(s, T, x + 242, ry2 + 8, plays)
    label(s, x + 281, ry2 + 28.5, "PLAYS", T.text1, 6.6)
    tile_button(s, T, x + 334, ry2 + 8, "capture", size=20, wide=26)
    label(s, x + 334, ry2 + 28.5, "CAPTURE", T.text1, 6.2, ls=0.3)
    # jacks
    jy1, jy2 = 296, 344
    ins = [("CLOCK", 24), ("RESET", 55), ("RUN", 86), ("SCENE", 119), ("LAUNCH", 152), ("FILL", 184)]
    for t, cx in ins:
        jack(s, T, x + cx, jy1)
        label(s, x + cx, jy1 - 16, t, T.panelText, 6.3, ls=0.3)
    # record inputs, grouped and named
    s.rect(x + 10, jy2 - 30, 140, 50, "none", r=7, stroke=T.record, sw=1.2, sop=0.75)
    s.rect(x + 18, jy2 - 34, 30, 9, T.panel)
    label(s, x + 33, jy2 - 27.5, "REC", T.record, 7)
    for t, cx in (("V/OCT", 30), ("GATE", 63), ("VEL", 96), ("AUTO", 129)):
        jack(s, T, x + cx, jy2 + 3)
        label(s, x + cx, jy2 - 13, t, T.panelText, 6.8)
    # outputs on the inverted plate
    plate(s, T, x + 206, 274, 144, 96)
    for i, t in enumerate(["CLOCK", "RESET", "RUN", "EOC", "BAR", "CLICK"]):
        cx = x + 229 + (i % 3) * 49
        cy = jy1 + (i // 3) * 50
        jack(s, T, cx, cy + 4, on_plate=True)
        label(s, cx, cy - 12, t, T.plateText, 6.8)
    if edge_tab and not wide:
        # the pull tab: drag the edge (or click) to open the editor inside the panel
        s.rect(x + w - 9, 160, 6, 60, T.accent, r=3, op=0.85)
        for k in range(3):
            s.line(x + w - 7.5 + k * 1.5, 182, x + w - 7.5 + k * 1.5, 198, T.onAccent, 0.8, op=0.7)


def out_display(s, T, x, y, w, h, ti, name, source="Main", extra=None, follow=False):
    D = T.D
    screen(s, T, x, y, w, h)
    color = T.screenTracks[TRACKS[ti][1]]
    s.rect(x, y, w, 3, color, r=1.5)
    s.text(x + w / 2, y + 15, source, 8, D["text2"], UI, 600, "middle")
    ts = 26
    s.rect(x + w / 2 - ts / 2, y + 21, ts, ts, color, r=6)
    s.text(x + w / 2, y + 21 + ts * 0.7, str(ti + 1), 16, C.ON_TILE, MONO, 700, "middle")
    s.text(x + w / 2, y + h - (17 if extra else 8), name, 10, D["text0"], UI, 600, "middle")
    if extra:
        s.text(x + w / 2, y + h - 6, extra, 7.5, D["text2"], UI, 500, "middle")


def step_pair(s, T, cx1, cx2, cy):
    for cx, ic in ((cx1, "chev-l"), (cx2, "chev-r")):
        tile_button(s, T, cx, cy, ic, size=16, wide=26)


def track_out(s, T, x, ti=2):
    w = 90
    panel_face(s, T, x, w)
    screw(s, T, x + 15, 7.5)
    screw(s, T, x + w - 15, 372.5)
    wordmark(s, T, x + w / 2, 28, "TRACK OUT", 8.5, mark=False)
    out_display(s, T, x + 7, 38, w - 14, 74, ti, "Keys", extra="4 voices")
    step_pair(s, T, x + 28, x + 62, 126)
    plate(s, T, x + 6, 146, w - 12, 224)
    rows = [("V/OCT", "GATE"), ("VEL", "REL"), ("TIMBRE", "PRESS")]
    for i, (a, b) in enumerate(rows):
        cy = 184 + i * 52
        for cx, t in ((x + 26, a), (x + 64, b)):
            jack(s, T, cx, cy, on_plate=True)
            label(s, cx, cy - 16, t, T.plateText, 6.6, ls=0.4)
    jack(s, T, x + 45, 342, on_plate=True)
    label(s, x + 45, 326, "AUTO", T.plateText, 6.6)


DRUM_LANES = ["Kick", "Snare", "Clap", "C. hat", "O. hat", "Low tom", "Hi tom", "Rim"]
DRUM_LANES_2 = ["Crash", "Ride", "Cowbell", "Shaker", "—", "—", "—", "—"]


def drum_out(s, T, x, ti=0, bank=0):
    """Proposed Drum Out (10 HP): lane names beside their jacks, TRIG and VEL columns, and a bank switch that
    picks lanes 1-8 or 9-16. A second Drum Out on bank 9-16 gives a 16-lane kit separate jacks."""
    w = 150
    D = T.D
    panel_face(s, T, x, w)
    screw(s, T, x + 15, 7.5)
    screw(s, T, x + w - 15, 372.5)
    wordmark(s, T, x + w / 2, 28, "DRUM OUT", 8.5, mark=False)
    # compact display: tile + names in one row
    screen(s, T, x + 7, 36, w - 14, 40)
    color = T.screenTracks[TRACKS[ti][1]]
    s.rect(x + 7, 36, w - 14, 3, color, r=1.5)
    s.rect(x + 15, 45, 24, 24, color, r=5.5)
    s.text(x + 27, 62, str(ti + 1), 14, C.ON_TILE, MONO, 700, "middle")
    s.text(x + 46, 54, "Main", 7.5, D["text2"], UI, 600)
    s.text(x + 46, 67, "Drums", 10, D["text0"], UI, 600)
    tile_button(s, T, x + 112, 57, "chev-l", size=16, wide=20)
    tile_button(s, T, x + 133, 57, "chev-r", size=16, wide=20)
    # bank switch
    by = 84
    s.rect(x + 7, by, w - 14, 18, T.well if T.dark else "#E3DDD3", r=5)
    half = (w - 18) / 2
    for i, t in enumerate(["LANES 1–8", "9–16"]):
        bx = x + 9 + i * half
        if i == bank:
            s.rect(bx, by + 2, half, 14, T.raised if T.dark else "#FFFFFF", r=4)
        s.text(bx + half / 2, by + 12, t, 7, T.text0 if i == bank else T.text2, UI, 700, "middle", ls=0.5)
    # plate: lane screen + TRIG / VEL columns
    plate(s, T, x + 6, 108, w - 12, 262)
    label(s, x + 98, 120, "TRIG", T.plateText, 6.6)
    label(s, x + 128, 120, "VEL", T.plateText, 6.6)
    sx0, sy0 = x + 11, 126
    screen(s, T, sx0, sy0, 66, 8 * 24.5 + 2, r=4)
    if not T.dark:
        s.rect(sx0 - 0.5, sy0 - 0.5, 67, 8 * 24.5 + 3, "none", r=4.5, stroke="#FFFFFF", sw=1, sop=0.18)
    names = DRUM_LANES if bank == 0 else DRUM_LANES_2
    for i in range(8):
        cy = 138 + i * 24.5
        n = i + 1 + bank * 8
        used = names[i] != "—"
        hit = i in (0, 3) and bank == 0
        s.text(sx0 + 6, cy + 3, str(n), 7.5, D["text2"], MONO, 500)
        s.text(sx0 + 19, cy + 3, names[i], 8.5, D["text0"] if used else D["text2"], UI, 600)
        if hit:
            s.circle(sx0 + 60, cy, 2.5, color)
        jack(s, T, x + 98, cy, on_plate=True)
        jack(s, T, x + 128, cy, on_plate=True)
    # poly row
    py = 352
    for cx, t in ((x + 38, "TRIGS"), (x + 82, "VELS"), (x + 124, "AUTO")):
        jack(s, T, cx, py, on_plate=True)
        label(s, cx + 0, py - 14.5, t, T.plateText, 6.2)


def auto_out(s, T, x, ti=1):
    w = 120
    D = T.D
    panel_face(s, T, x, w)
    screw(s, T, x + 15, 7.5)
    screw(s, T, x + w - 15, 372.5)
    wordmark(s, T, x + w / 2, 28, "AUTO OUT", 8.5, mark=False)
    screen(s, T, x + 7, 36, w - 14, 40)
    color = T.screenTracks[TRACKS[ti][1]]
    s.rect(x + 7, 36, w - 14, 3, color, r=1.5)
    s.rect(x + 15, 45, 24, 24, color, r=5.5)
    s.text(x + 27, 62, str(ti + 1), 14, C.ON_TILE, MONO, 700, "middle")
    s.text(x + 46, 54, "Main", 7.5, D["text2"], UI, 600)
    s.text(x + 46, 67, "Bass", 10, D["text0"], UI, 600)
    step_pair(s, T, x + 44, x + 76, 90)
    plate(s, T, x + 6, 108, w - 12, 262)
    sx0 = x + 11
    screen(s, T, sx0, 126, 66, 8 * 28 + 2, r=4)
    lanes = [("Cutoff", 0.62), ("Reso", 0.3), ("Drive", 0.75), None, None, None, None, None]
    for i in range(8):
        cy = 141 + i * 28
        s.text(sx0 + 6, cy + 1, str(i + 1), 7.5, D["text2"], MONO, 500)
        if lanes[i]:
            nm, v = lanes[i]
            s.text(sx0 + 18, cy + 1, nm, 8.5, D["text0"], UI, 600)
            s.rect(sx0 + 18, cy + 6, 42, 2.2, D["line1"], r=1)
            s.rect(sx0 + 18, cy + 6, 42 * v, 2.2, color, r=1)
        else:
            s.text(sx0 + 18, cy + 1, "—", 8.5, D["text2"], UI, 500)
        jack(s, T, x + 96, cy + 2, on_plate=True)
    label(s, x + 96, 120, "OUT", T.plateText, 6.6)


def board(T, w, h):
    s = Svg(w, h, RACK)
    return s


def mock_panels(T, path):
    k = 2.0
    s = board(T, 1600, 900)
    s.text(44, 52, f"Tessera family · {T.name} panels · 24 + 6 + 10 + 8 HP", 22, "#E8E8E8", UI, 600)
    s.text(44, 80, "Same tokens as the editor. Screens stay dark in both themes; outputs sit on an inverted plate.",
           13, "#A8A8A8", UI, 400)
    s.g(f"translate(44,118) scale({k})")
    rails(s, 0, 756)
    tessera_panel(s, T, 0)
    track_out(s, T, 372)
    drum_out(s, T, 474)
    auto_out(s, T, 636)
    s.g_end()
    s.save(path)


def mini_slot(s, T, x, y, w, h, ti, sc, plays):
    info = SLOTS[ti].get(sc)
    if not info:
        well(s, T, x, y, w, h, r=3)
        return False
    color = tcol(T, ti)
    st = E.slot_state(ti, sc, plays)
    if st == "recording":
        s.rect(x, y, w, h, color, r=3, op=0.30)
        s.rect(x, y, 8, h, T.record, r=3)
    elif st == "playing":
        s.rect(x, y, w, h, color, r=3)
        icon(s, "play", x + 6, y + h / 2, T.onTile, 8)
        s.rect(x + 10, y + h - 2.5, (w - 12) * 0.6, 2, T.onTile, op=0.5)
    else:
        s.rect(x, y, w, h, color, r=3, op=0.30 if T.dark else 0.26)
        icon(s, "play", x + 6, y + h / 2, color, 8)
        if st == "queued":
            s.rect(x + 0.6, y + 0.6, w - 1.2, h - 1.2, "none", r=3, stroke=T.queue, sw=1.2, dash="3 2")
    s.text(x + 12, y + h / 2 + 3, info[0], 8, T.onTile if st == "playing" else T.text0, UI, 600)
    return st in ("playing", "recording")


def panel_editor(s, T, x, y, w, h, plays="timeline", maximized=None):
    """The in-panel editor: Song above Notes, laid out from the 350 px height budget. Each pane has a
    maximize / restore button in its corner; there is no Song / Both / Notes switch."""
    s.rect(x, y, w, h, T.ground, r=6)
    # strip: tools and the way out to the full editor
    sh_ = 26
    s.rect(x + 4, y + 4, w - 8, sh_, T.surface, r=5)
    tools = ["pointer", "pencil", "brush", "eraser", "scissors", "mute", "velocity", "range"]
    s.rect(x + 8, y + 7, len(tools) * 22 + 4, 20, T.well, r=4)
    for i, t in enumerate(tools):
        if i == 0:
            s.rect(x + 10 + i * 22, y + 8, 20, 18, T.raised, r=3)
        icon(s, t, x + 20 + i * 22, y + 17, T.accentText if i == 0 else T.text1, 12)
    btn(s, T, x + w - 104, y + 7, 96, 20, ico="inspector", label="Full editor", size=10, ghost=True)
    py = y + sh_ + 8
    song_h = 0 if maximized == "notes" else (y + h - py - 4 if maximized == "song" else 140)
    if song_h:
        pane(s, T, x + 4, py, w - 8, song_h)
        head_w = 132
        col_w = 50
        scenes = 3
        cx0 = x + 4 + head_w + 4
        tl_x = cx0 + scenes * col_w + 4
        tl_w = x + w - 8 - tl_x
        hdr, rul = 22, 15
        if plays == "clips":
            E.section_glow(s, T, x + 4 + head_w, py, tl_x - x - 4 - head_w, song_h)
        else:
            E.section_glow(s, T, tl_x, py, x + w - 4 - tl_x, song_h)
        s.text(x + 14, py + 15, "Tracks", 10.5, T.text0, UI, 600)
        s.text(cx0 + 4, py + 15, "Clips", 10.5, T.text0, UI, 600)
        s.text(tl_x + 8, py + 15, "Timeline", 10.5, T.text0, UI, 600)
        if plays == "clips":
            E.playing_tag(s, T, cx0 + 82, py + 3)
        else:
            E.playing_tag(s, T, tl_x + 66, py + 3)
        E.max_btn(s, T, x + w - 30, py + 2, maximized == "song", size=20)
        ry = py + hdr
        for i in range(scenes):
            s.text(cx0 + i * col_w + 4, ry + 11, f"{i + 1} {SCENES[i][0]}", 8, T.text1, UI, 600)
        px_bar = tl_w / 16
        for b in range(16):
            s.text(tl_x + b * px_bar + 3, ry + 11, str(b + 1), 8, T.text2, MONO)
            s.line(tl_x + b * px_bar, ry + 6, tl_x + b * px_bar, ry + rul, T.line2, 1)
        s.rect(tl_x + 4 * px_bar, ry, 8 * px_bar, 3, T.accent, r=1.5, op=0.5)
        rh = 25
        live = []
        nrows = min(len(TRACKS), int((py + song_h - ry - rul - 4) // rh))
        for ti in range(nrows):
            yy = ry + rul + ti * rh
            color = tcol(T, ti)
            if ti == 2:
                s.rect(x + 4, yy, head_w, rh, T.raised if T.dark else T.hover)
            s.rect(x + 6, yy + 4, 2.5, rh - 8, color, r=1)
            track_tile(s, T, x + 14, yy + 4.5, color, ti + 1, size=16)
            s.text(x + 36, yy + 16, TRACKS[ti][0], 10.5, T.text0, UI, 600)
            btn(s, T, x + head_w - 42, yy + 4, 18, 17, label="M", size=8.5, weight=700)
            btn(s, T, x + head_w - 21, yy + 4, 18, 17, label="S", size=8.5, weight=700)
            for sc in range(scenes):
                args = (cx0 + sc * col_w + 1, yy + 3, col_w - 4, rh - 6, ti, sc, plays)
                if mini_slot(s, T, *args):
                    live.append(args)
            launched = plays == "timeline" and ti in E.OVERRIDES
            for c in CLIPS[ti]:
                st, ln, nm, linked, loop = c[:5]
                if st >= 16:
                    continue
                ln = min(ln, 16 - st)
                E.clip_tile(s, T, tl_x + st * px_bar + 1, yy + 3, ln * px_bar - 2, rh - 6, color, nm, sh(nm) + st,
                            drum=TRACKS[ti][2] == "drum", loop_px=loop * px_bar, selected=(ti, st) == (2, 4),
                            dimmed=launched)
            s.line(x + 4, yy + rh - 0.5, x + w - 4, yy + rh - 0.5, T.line1, 1)
        rows_end = ry + rul + nrows * rh
        s.line(tl_x + 9.5 * px_bar, ry + rul, tl_x + 9.5 * px_bar, rows_end, T.text0, 1.2, op=0.8)
        dim = 0.55 if T.dark else 0.6
        if plays == "timeline":
            s.rect(cx0 - 3, ry, tl_x - cx0 + 2, py + song_h - ry - 2, T.canvas, op=dim)
            for args in live:
                mini_slot(s, T, *args)
        else:
            s.rect(tl_x + 1, ry, x + w - 6 - tl_x, py + song_h - ry - 2, T.canvas, op=dim)
        s.line(cx0 - 2, py + 4, cx0 - 2, py + song_h - 4, T.line1, 1)
        s.line(tl_x - 1, py + 4, tl_x - 1, py + song_h - 4, T.line1, 1)
    # notes pane
    if maximized == "song":
        return
    ny = py + (song_h + 6 if song_h else 0)
    nh = y + h - ny - 4
    pane(s, T, x + 4, ny, w - 8, nh)
    color = tcol(T, 2)
    s.rect(x + 12, ny + 7, 10, 10, color, r=2.5)
    s.text(x + 28, ny + 16, "Keys · Verse A", 10.5, T.text0, UI, 600)
    s.text(x + 112, ny + 16, "2 bars", 9.5, T.text2, MONO)
    icon(s, "chev-d", x + 152, ny + 12, T.text2, 10)
    btn(s, T, x + 166, ny + 4, 74, 18, ico="devices", label="Devices", size=9.5, ghost=True)
    btn(s, T, x + 246, ny + 4, 74, 18, label="1/16", caret=True, size=9.5, ghost=True)
    btn(s, T, x + 324, ny + 4, 74, 18, label="C minor", caret=True, size=9.5, ghost=True)
    btn(s, T, x + w - 124, ny + 4, 82, 18, label="Velocity", caret=True, size=9.5, ghost=True)
    E.max_btn(s, T, x + w - 30, ny + 2, maximized == "notes", size=20)
    s.clip_begin(x + 4, ny + 24, w - 8, nh - 26, 6)
    hi = 66 if maximized != "notes" else 72
    lo = 55 if maximized != "notes" else 48
    E.piano_roll(s, T, x + 4, ny + 24, w - 8, nh - 26, color, lane_h=40, px_beat=(w - 60) / 16, lo=lo, hi=hi)
    s.g_end()


def mock_widened(T, path):
    hp = 80
    w = hp * 15
    k = 1600 / (w + 80)
    one = int(460 * k)
    s = board(T, 1600, 2 * one + 150)
    heads = [f"Tessera widened to {hp} HP · in-panel editor · {T.name} · Timeline plays",
             "Notes pane maximised (restore button in its corner) · Clips play"]
    for i, (plays, mx) in enumerate((("timeline", None), ("clips", "notes"))):
        oy = i * (one + 70)
        s.text(40, oy + 46, heads[i], 22 if i == 0 else 18, "#E8E8E8", UI, 600)
        s.g(f"translate({40 * k:.1f},{oy + 80}) scale({k:.4f})")
        rails(s, 0, w)
        panel_face(s, T, 0, w)
        tessera_panel(s, T, 0, w=w, wide=True, plays=plays)
        for cx in (w - 22,):
            screw(s, T, cx, 7.5)
            screw(s, T, cx, 372.5)
        s.line(360.5, 16, 360.5, 364, T.line2, 1, op=0.8)
        panel_editor(s, T, 366, 16, w - 366 - 14, 348, plays=plays, maximized=mx)
        s.rect(w - 9, 160, 5, 60, T.line2, r=2.5)
        s.g_end()
    s.save(path)


def mock_variants(T, path):
    """Drum Out: four ways to reach lanes 9-16 with mono jacks."""
    k = 1.55
    s = board(T, 1600, 860)
    s.text(40, 46, f"Drum Out variants for lanes 9–16 · {T.name}", 22, "#E8E8E8", UI, 600)
    s.text(40, 72, "B is recommended: the same module twice, the second switched to lanes 9–16. No new module, "
                   "no hidden per-jack settings.", 13, "#A8A8A8", UI, 400)
    s.g(f"translate(40,110) scale({k})")
    rails(s, 0, 980)
    # A: today's layout, plus names (keep as is)
    xa = 0
    drum_out(s, T, xa, bank=0)
    # B: two Drum Outs, banks 1-8 and 9-16
    xb = 190
    drum_out(s, T, xb, bank=0)
    drum_out(s, T, xb + 152, bank=1)
    # C: wide 16-lane Drum Out (16 HP), triggers only, velocity on poly
    xc = 540
    w = 240
    D = T.D
    panel_face(s, T, xc, w)
    wordmark(s, T, xc + w / 2, 28, "DRUM OUT 16", 8.5, mark=False)
    screen(s, T, xc + 7, 36, w - 14, 40)
    color = T.screenTracks[TRACKS[0][1]]
    s.rect(xc + 15, 45, 24, 24, color, r=5.5)
    s.text(xc + 27, 62, "1", 14, C.ON_TILE, MONO, 700, "middle")
    s.text(xc + 46, 64, "Drums · 12 lanes", 10, D["text0"], UI, 600)
    plate(s, T, xc + 6, 90, w - 12, 280)
    names = DRUM_LANES + DRUM_LANES_2
    for i in range(16):
        col, row = i % 4, i // 4
        cx = xc + 36 + col * 56
        cy = 128 + row * 60
        jack(s, T, cx, cy, on_plate=True)
        label(s, cx, cy - 16, f"{i + 1} {names[i].upper() if names[i] != '—' else ''}".strip(), T.plateText, 6)
    # D: per-jack lane choice (menu per jack)
    xd = 800
    drum_out(s, T, xd, bank=0)
    # captions
    s.g_end()
    caps = [(0, "A · Keep, add lane names", "8 jacks + poly. Lanes 9–16 need a poly split."),
            (190, "B · Bank switch  (recommended)", "Second Drum Out set to 9–16. Same binding, one new field."),
            (540, "C · Wide 16-lane module (16 HP)", "All lanes visible; big, and no per-lane velocity jacks."),
            (800, "D · Pick a lane per jack", "Any 8 lanes on 8 jacks, via a menu per jack.")]
    for xx, t, sub in caps:
        X = 40 + xx * k
        s.text(X, 110 + 410 * k + 30, t, 14, "#F0F0F0", UI, 600)
        s.text(X, 110 + 410 * k + 52, sub, 11.5, "#B0B0B0", UI, 400)
    # D annotation: a menu on a jack
    mx = 40 + (xd + 120) * k
    my = 110 + 160 * k
    s.rect(mx, my, 150, 92, "#2F3239", r=6)
    for i, t in enumerate(["Jack 2 plays:", "  Snare", "✓ Ride (lane 10)", "  Cowbell"]):
        s.text(mx + 10, my + 20 + i * 20, t, 11, "#E6E6E6" if i else "#9AA0A8", UI, 500)
    s.save(path)


def png_data(path):
    with open(path, "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode()


def mock_library(T, path):
    """What the VCV Library shows: Rack renders each module with no module instance, light panels forced."""
    s = Svg(1600, 900, "#F2F2F2")
    s.text(44, 54, "The VCV Library page: today vs proposed", 24, "#1A1A1A", UI, 600)
    s.text(44, 82, "Rack makes Library images with Window::screenshotModules: no module instance "
                   "(createModuleWidget(NULL)) and preferDarkPanels forced off. So the light panel, in its "
                   "default width, is the product shot.", 13, "#555", UI, 400)
    # today
    s.text(44, 130, "Today", 15, "#1A1A1A", UI, 600)
    img = os.path.join(CURRENT, "19-module-browser-previews.png")
    if os.path.exists(img):
        s.add(f'<image x="44" y="146" width="700" height="331" href="{png_data(img)}"/>')
    s.text(44, 500, "Dark panels (no light variant), a transport display with four grey squares. Nothing says "
                    "piano roll, clips or arranger.", 12, "#555", UI, 400)
    # proposed (light, as the Library will render it)
    L = Theme("light")
    s.text(800, 130, "Proposed (light panels, as the Library renders them)", 15, "#1A1A1A", UI, 600)
    k = 0.92
    s.g(f"translate(800,150) scale({k})")
    s.rect(-10, -10, 780, 400, "#FFFFFF", r=8)
    tessera_panel(s, L, 0)
    track_out(s, L, 372)
    drum_out(s, L, 474)
    auto_out(s, L, 636)
    s.g_end()
    s.text(800, 530, "The screen draws the demo song (previewSong) as the mosaic: coloured clips, one playing, one "
                     "queued.", 12, "#555", UI, 400)
    s.text(800, 550, "The accent tab on the right edge says the panel opens into an editor.", 12, "#555", UI, 400)
    # thumbnail scale strip
    s.text(44, 610, "At Library thumbnail size (about 25%)", 15, "#1A1A1A", UI, 600)
    s.g("translate(44,630) scale(0.42)")
    tessera_panel(s, L, 0)
    s.g_end()
    s.g("translate(250,630) scale(0.42)")
    tessera_panel(s, Theme("dark"), 0)
    s.g_end()
    s.text(44, 812, "Light (Library)", 11, "#555", UI, 500)
    s.text(250, 812, "Dark (in a dark rack)", 11, "#555", UI, 500)
    s.text(470, 660, "Why it reads at a glance:", 13, "#1A1A1A", UI, 600)
    for i, t in enumerate(["• a dark screen full of coloured tiles = clips, the first thing the eye finds",
                           "• the four-tile mark repeats the idea in the title",
                           "• PLAY / REC / LOOP are larger and lit; the rest are grouped and quieter",
                           "• outputs on the inverted plate, inputs on the face: Rack convention",
                           "• one accent (lapis) for Open editor and the edge tab"]):
        s.text(470, 688 + i * 22, t, 12.5, "#333", UI, 400)
    s.save(path)


def mock_system(T, path):
    s = Svg(1600, 1180, T.ground)
    h = C.resolve(C.DARK if T.dark else C.LIGHT)
    s.text(44, 56, f"Grout & Glaze · design system · {T.name}", 24, T.text0, UI, 600)
    s.text(44, 82, "Tiles carry the music; the grout carries the interface. One accent, used rarely.", 13,
           T.text2, UI, 400)
    # neutrals
    y = 120
    caption(s, T, 44, y, "Neutrals", 10)
    keys = ["ground", "canvas", "surface", "raised", "hover", "pressed", "well", "line0", "line1", "line2", "text2",
            "text1", "text0"]
    for i, k in enumerate(keys):
        x = 44 + i * 116
        s.rect(x, y + 12, 108, 56, h[k], r=6)
        s.rect(x + 0.5, y + 12.5, 107, 55, "none", r=6, stroke=T.line2, sw=1)
        s.text(x, y + 86, k, 11, T.text0, UI, 600)
        s.text(x, y + 101, h[k], 10, T.text2, MONO)
    # roles
    y = 250
    caption(s, T, 44, y, "Accent and states", 10)
    roles = [("accent", "focus · selection · primary"), ("play", "playing"), ("queue", "queued"),
             ("record", "recording · armed")]
    for i, (k, use) in enumerate(roles):
        x = 44 + i * 230
        s.rect(x, y + 12, 220, 56, h[k], r=6)
        s.text(x + 12, y + 46, k, 14, h["onAccent"] if k == "accent" else T.onState, UI, 700)
        s.text(x, y + 86, f"{h[k]} · {use}", 10.5, T.text1, UI, 500)
    x = 44 + 4 * 230
    s.rect(x, y + 12, 220, 56, h["plate"], r=6)
    s.text(x + 12, y + 46, "plate", 14, h["plateText"], UI, 700)
    s.text(x, y + 86, f"{h['plate']} · output plate", 10.5, T.text1, UI, 500)
    x = 44 + 5 * 230
    s.rect(x, y + 12, 220, 56, T.D["screen"], r=6)
    s.text(x + 12, y + 46, "screen", 14, T.D["text0"], UI, 700)
    s.text(x, y + 86, f"{T.D['screen']} · module displays", 10.5, T.text1, UI, 500)
    # tracks
    y = 380
    caption(s, T, 44, y, "Track colours (OKLCH ring, 16 hues)", 10)
    for i, c in enumerate(T.tracks):
        x = 44 + i * 94
        s.rect(x, y + 12, 86, 44, c, r=6)
        s.text(x + 10, y + 39, str(i + 1), 13, T.onTile, MONO, 700)
        s.text(x, y + 72, c, 9.5, T.text2, MONO)
    # type
    y = 490
    caption(s, T, 44, y, "Type", 10)
    specs = [("Display · 22 / 600", "Keys · Verse A", 22, UI, 600), ("Title · 15 / 600", "3 notes", 15, UI, 600),
             ("Body · 12 / 500", "Tracks, buttons, menus", 12, UI, 500), ("Small · 11 / 500", "Snap: 1/16 · Fold", 11, UI, 500),
             ("Caption · 9 / 600 caps", "PLAYS · SCENE", 9, UI, 600), ("Number · 19 mono", "120.00  9.3.1", 19, MONO, 500),
             ("Value · 11 mono", "Eb3 – Bb3  82%", 11, MONO, 500)]
    for i, (lab, sample, size, font, wt) in enumerate(specs):
        yy = y + 30 + i * 34
        s.text(44, yy, lab, 10.5, T.text2, UI, 500)
        s.text(250, yy, sample, size, T.text0, font, wt)
    # icons
    y = 760
    caption(s, T, 44, y, "Icons · 16 px grid, 1.5 px stroke, round joins", 10)
    for i, n in enumerate(ICON_NAMES):
        x = 44 + (i % 21) * 72
        yy = y + 30 + (i // 21) * 62
        s.rect(x, yy - 18, 36, 36, T.surface, r=6)
        icon(s, n, x + 18, yy, T.text0, 18)
        s.text(x + 18, yy + 32, n, 8.5, T.text2, UI, 500, "middle")
    # states demo
    y = 930
    caption(s, T, 44, y, "Clip and slot states (never colour alone)", 10)
    col = T.tracks[7]
    states = [(None, "Empty"), ("idle", "Stopped"), ("playing", "Playing: solid + ▶ + progress"),
              ("queued", "Queued: dashed gold + word"), ("recording", "Recording: red tab + ● + word")]
    for i, (st, lab) in enumerate(states):
        x = 44 + i * 300
        if st is None:
            E.slot(s, T, x, y + 14, 150, 64)
        else:
            E.slot(s, T, x, y + 14, 150, 64, col, "Verse A", None if st == "idle" else st, seed=4, big=True)
        s.text(x, y + 98, lab, 11, T.text1, UI, 500)
    y = 1060
    caption(s, T, 44, y, "Notes", 10)
    note_tile(s, T, 44, y + 16, 110, 16, col, vel=1.0, label="Eb4")
    note_tile(s, T, 170, y + 16, 110, 16, col, vel=0.4, label="C4")
    note_tile(s, T, 296, y + 16, 110, 16, col, vel=0.9, selected=True, label="Bb3")
    note_tile(s, T, 422, y + 16, 110, 16, col, muted=True)
    note_tile(s, T, 548, y + 16, 110, 16, col, ghost=True)
    for i, t in enumerate(["velocity 100%", "velocity 40%", "selected", "muted (hatched)", "ghost / loop repeat"]):
        s.text(44 + i * 126, y + 52, t, 10.5, T.text2, UI, 500)
    s.save(path)


def mock_screens(T, path):
    """Options for the default-width panel's screen."""
    s = board(T, 1600, 1060)
    s.text(40, 46, "Tessera screen at default width: what it previews", 22, "#E8E8E8", UI, 600)
    s.text(40, 72, "C is recommended: the song on top follows the PLAYS switch (timeline, or the clip grid), and the "
                   "selected track's notes play underneath.", 13, "#A8A8A8", UI, 400)
    k = 2.0
    opts = [("A · Song only", "The timeline, larger. Says “arranger” but not “piano roll”.", "song", "timeline"),
            ("B · Notes only", "The selected track as a piano roll. Hides the rest of the song.", "notes", "timeline"),
            ("C · Song + notes, Timeline plays  (recommended)", "Clips as tiles on the timeline; notes light up under "
             "the playhead.", "split", "timeline"),
            ("C · Song + notes, Clips play", "Same screen; the top half becomes the clip grid. The framed column is "
             "the scene LAUNCH starts.", "split", "clips")]
    for i, (title, sub, mode, plays) in enumerate(opts):
        col, row = i % 2, i // 2
        X, Y = 40 + col * 780, 110 + row * 470
        s.g(f"translate({X},{Y}) scale({k})")
        panel_face(s, T, 0, 360, 150)
        s.rect(0, 0, 360, 150, T.panel)
        panel_screen(s, T, 12, 12, 336, 126, mode, plays)
        s.g_end()
        s.text(X, Y + 150 * k + 34, title, 15, "#F0F0F0", UI, 600)
        s.text(X, Y + 150 * k + 58, sub, 12, "#B0B0B0", UI, 400)
    s.save(path)


def mock_schemes(T, path):
    """The colour schemes, each shown on the same editor."""
    s = Svg(1600, 1480, "#2B2B2B")
    s.text(44, 52, "Colour schemes", 24, "#EEE", UI, 600)
    s.text(44, 80, "Instrument menu (Main ▾) › Colour scheme. “Follow Rack” (default) uses Plaster with light panels "
                   "and Slate with dark panels. Screens and track colours stay the same in every scheme.", 13,
           "#AAA", UI, 400)
    tw_, th_ = E.W, E.H
    k = 0.5
    for i, key in enumerate(C.SCHEME_ORDER):
        T2 = Theme(key)
        col, row = i % 2, i // 2
        X, Y = 44 + col * 776, 112 + row * 530
        sub = Svg(tw_, th_, T2.ground)
        sub.ids = (i + 1) * 1000
        E.transport(sub, T2, E.GAP, E.GAP, tw_ - 2 * E.GAP)
        sy = E.GAP + 44 + E.GAP
        E.song_pane(sub, T2, E.GAP, sy, tw_ - 2 * E.GAP, 396)
        ny = sy + 396 + E.GAP
        nh = th_ - ny - E.GAP
        nw = tw_ - 2 * E.GAP - 284 - E.GAP
        pane(sub, T2, E.GAP, ny, nw, nh)
        E.notes_header(sub, T2, E.GAP, ny, nw, tcol(T2, 2), "Keys · Verse A")
        sub.clip_begin(E.GAP, ny + 36, nw, nh - 36, R_PANE)
        E.piano_roll(sub, T2, E.GAP, ny + 36, nw, nh - 36, tcol(T2, 2))
        sub.g_end()
        E.inspector(sub, T2, E.GAP + nw + E.GAP, ny, 284, nh)
        s.defs += sub.defs
        s.g(f"translate({X},{Y}) scale({k})")
        s.rect(0, 0, tw_, th_, T2.ground)
        s.parts += sub.parts
        s.g_end()
        sc = C.SCHEMES[key]
        s.text(X, Y + th_ * k + 24, sc["label"] + ("  · default" if key in ("plaster", "slate") else ""), 15,
               "#F0F0F0", UI, 600)
        s.text(X, Y + th_ * k + 44, sc["desc"], 12, "#B0B0B0", UI, 400)
        for j, tok in enumerate(("ground", "canvas", "surface", "raised", "text0", "accent")):
            s.rect(X + 516 + j * 34, Y + th_ * k + 14, 28, 28, getattr(T2, tok), r=5)
            s.rect(X + 516.5 + j * 34, Y + th_ * k + 14.5, 27, 27, "none", r=5, stroke="#FFFFFF", sw=1, sop=0.15)
    # the menu
    X, Y = 44, 112 + 2 * 530
    s.rect(X, Y, 300, 206, "#33363C", r=8)
    items = ["Colour scheme", "✓ Follow Rack (Plaster / Slate)", "   Plaster", "   Sand", "   Slate",
             "   Walnut"]
    for j, t in enumerate(items):
        s.text(X + 16, Y + 30 + j * 30, t, 13 if j else 11, "#EEE" if j else "#9AA0A8", UI, 500 if j else 600)
    s.text(X + 340, Y + 30, "One menu, one choice.", 14, "#F0F0F0", UI, 600)
    for j, t in enumerate(["Each scheme is a full set of the same tokens, so every view", "follows it without special "
                           "cases. Module panels keep following", "Rack's own light/dark panel setting."]):
        s.text(X + 340, Y + 56 + j * 20, t, 12, "#B0B0B0", UI, 400)
    s.save(path)
