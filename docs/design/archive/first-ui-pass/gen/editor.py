"""Editor mockups: arranger + piano roll + inspector, launcher grid, drum step editor."""
import random
from svg import Svg, Theme, icon, track

UI, MONO, DISPLAY = "Inter", "JetBrains Mono", "Unbounded"
GAP = 6        # grout between tiles in the full editor
R_TILE = 8     # pane tile radius
R_CTRL = 5     # control radius
R_ITEM = 4     # clips, slots
R_NOTE = 2.5   # notes

TRACKS = [  # name, ring index, type, voices, output
    ("Drums", 15, "drum", "8 lanes", "Drum Out"),
    ("Bass", 1, "inst", "mono", "Track Out"),
    ("Keys", 6, "inst", "4 voices", "Track Out"),
    ("Lead", 10, "inst", "mono", "Track Out"),
    ("Pad", 13, "inst", "6 voices", "none"),
    ("Arp", 3, "inst", "2 voices", "Track Out"),
]
CLIPS = {
    0: [(0, 4, "Beat A", True), (4, 4, "Beat A", True), (8, 8, "Beat B", False), (16, 4, "Fill", False)],
    1: [(0, 8, "Root", False), (8, 8, "Walk", False), (16, 4, "Root", False)],
    2: [(4, 4, "Verse A", True), (8, 4, "Verse A", True), (12, 8, "Chorus", False)],
    3: [(8, 8, "Hook", True), (16, 4, "Hook", True)],
    4: [(0, 16, "Air", False), (16, 4, "Air", False, "muted")],
    5: [(12, 8, "Spark", False)],
}
def sh(t):
    return sum(ord(ch) * (i + 1) for i, ch in enumerate(str(t)))


MINOR = {0, 2, 3, 5, 7, 8, 10}  # C minor


def tcol(T, ti):
    return T.tracks[TRACKS[ti][1]]


# ------------------------------------------------------------------------------------------------ primitives

def tile(s, T, x, y, w, h, r=R_TILE, fill=None):
    s.rect(x, y, w, h, fill or T.surface, r=r)
    if T.dark:  # a hair of light on the top edge: the "glazed tile" cue, one line, cheap in NanoVG
        s.clip_begin(x, y, w, h, r)
        s.line(x + r, y + 0.5, x + w - r, y + 0.5, "#FFFFFF", 1, op=0.05)
        s.g_end()
    else:
        s.rect(x + 0.5, y + 0.5, w - 1, h - 1, "none", r=r, stroke="#000000", sw=1, sop=0.06)


def btn(s, T, x, y, w, h, ico=None, label=None, active=False, primary=False, color=None, txt=None, hover=False):
    if primary:
        s.rect(x, y, w, h, T.accent, r=R_CTRL)
        fg = T.onAccent
    elif active:
        s.rect(x, y, w, h, color or T.accent, r=R_CTRL, op=0.20 if T.dark else 0.14)
        fg = color or T.accentText
    else:
        if hover:
            s.rect(x, y, w, h, T.raised, r=R_CTRL)
        fg = txt or T.text1
    if ico and label:
        icon(s, ico, x + 14, y + h / 2, fg, 14)
        s.text(x + 26, y + h / 2 + 4, label, 11.5, fg, UI, 500)
    elif ico:
        icon(s, ico, x + w / 2, y + h / 2, fg, 15)
    elif label:
        s.text(x + w / 2, y + h / 2 + 4, label, 11.5, fg, UI, 500, "middle")


def segmented(s, T, x, y, items, sel, h=24, wpad=14):
    widths = [len(t) * 6.6 + wpad * 2 for t in items]
    s.rect(x, y, sum(widths) + 4, h, T.well, r=R_CTRL + 1)
    cx = x + 2
    for i, t in enumerate(items):
        if i == sel:
            s.rect(cx, y + 2, widths[i], h - 4, T.raised if T.dark else T.overlay, r=R_CTRL - 1)
            if not T.dark:
                s.rect(cx, y + 2, widths[i], h - 4, "none", r=R_CTRL - 1, stroke="#000", sw=1, sop=0.08)
        s.text(cx + widths[i] / 2, y + h / 2 + 4, t, 11.5, T.text0 if i == sel else T.text1, UI, 600 if i == sel else 500,
               "middle")
        cx += widths[i]
    return sum(widths) + 4


def chip(s, T, x, y, color, num=None, size=16):
    s.rect(x, y, size, size, color, r=3.5)
    if num is not None:
        s.text(x + size / 2, y + size / 2 + 3.6, str(num), 10, T.onTrack, UI, 700, "middle")


def note_tile(s, T, x, y, w, h, color, vel=1.0, selected=False, muted=False, label=None):
    if selected:  # grout ring: a canvas gap, then the accent ring, so it reads on any track colour
        s.rect(x - 2.5, y - 2.5, w + 5, h + 5, "none", r=R_NOTE + 2, stroke=T.accent, sw=1.5)
    op = 0.45 + 0.55 * vel
    if muted:
        s.rect(x, y, w, h, color, r=R_NOTE, op=0.18)
        s.rect(x, y, w, h, "none", r=R_NOTE, stroke=color, sw=1, sop=0.6, dash="2 2")
        return
    s.rect(x, y, w, h, color, r=R_NOTE, op=op)
    if w > 4 and T.dark:
        s.line(x + 1.5, y + 0.75, x + w - 1.5, y + 0.75, "#FFFFFF", 1, op=0.35)
    if selected:
        s.rect(x, y, w, h, "#FFFFFF", r=R_NOTE, op=0.14)
    if label and w > 26 and h >= 11:
        s.text(x + 4, y + h - 3.2, label, 8.5, T.onTrack, UI, 600)


def pname(p):
    return ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"][p % 12] + str(p // 12 - 1)


# ------------------------------------------------------------------------------------------------ transport

def transport(s, T, x, y, w, h=46, mode="Arranger", view="Show launcher", playing=True):
    tile(s, T, x, y, w, h)
    cy = y + h / 2
    bx = x + 10
    for name, col in (("play", T.play if playing else None), ("stop", None), ("record", T.record), ("loop", None)):
        if name == "play" and playing:
            s.rect(bx, cy - 15, 32, 30, T.play, r=R_CTRL, op=0.18 if T.dark else 0.14)
            icon(s, "play", bx + 16, cy, T.play, 17)
        elif name == "loop":
            s.rect(bx, cy - 15, 32, 30, T.accent, r=R_CTRL, op=0.20 if T.dark else 0.14)
            icon(s, "loop", bx + 16, cy, T.accentText, 17)
        else:
            icon(s, name, bx + 16, cy, col or T.text1, 17)
        bx += 36
    # readout well: position, tempo, metre, key — one tile because they are one thing: where and how fast
    wx = bx + 8
    ww = 372
    s.rect(wx, cy - 16, ww, 32, T.well, r=R_CTRL + 1)
    s.text(wx + 14, cy + 6.5, "9.3.1", 19, T.text0, MONO, 500)
    s.text(wx + 80, cy + 4, "bar", 9.5, T.text2, UI, 500)
    s.line(wx + 108, cy - 9, wx + 108, cy + 9, T.lineStrong, 1)
    s.text(wx + 122, cy + 6.5, "120.00", 19, T.text0, MONO, 500)
    s.text(wx + 196, cy + 4, "BPM", 9.5, T.text2, UI, 600)
    s.line(wx + 226, cy - 9, wx + 226, cy + 9, T.lineStrong, 1)
    s.text(wx + 240, cy + 5, "4/4", 13, T.text1, MONO, 500)
    s.line(wx + 274, cy - 9, wx + 274, cy + 9, T.lineStrong, 1)
    s.text(wx + 288, cy + 4.5, "C minor", 12, T.text1, UI, 500)
    icon(s, "chev-d", wx + 352, cy, T.text2, 12)
    # play mode: launcher or arranger
    mx = wx + ww + 14
    segmented(s, T, mx, cy - 13, ["Launcher", "Arranger"], 0 if mode == "Launcher" else 1, h=26)
    btn(s, T, mx + 168, cy - 13, 116, 26, "launcher", view)
    rx = x + w - 10
    for ic, wd in (("close", 30), ("inspector", 30)):
        rx -= wd
        btn(s, T, rx, cy - 13, wd, 26, ic, active=(ic == "inspector"))
        rx -= 4
    rx -= 104
    btn(s, T, rx, cy - 13, 100, 26, "follow", "Follow: Page", active=True)
    rx -= 50
    btn(s, T, rx, cy - 13, 46, 26, None, "File")
    rx -= 32
    btn(s, T, rx, cy - 13, 28, 26, "redo", txt=T.text2)
    rx -= 30
    btn(s, T, rx, cy - 13, 28, 26, "undo")


# ------------------------------------------------------------------------------------------------ arranger

def ruler(s, T, x, y, w, h, px_bar, bars, loop=None, markers=(), playhead=None, first=1, sub=4, start=0.0):
    s.rect(x, y, w, h, T.surface)
    s.clip_begin(x, y, w, h)
    if loop:
        lx0, lx1 = x + (loop[0] - start) * px_bar, x + (loop[1] - start) * px_bar
        s.rect(lx0, y + 2, lx1 - lx0, 8, T.accent, r=2, op=0.30 if T.dark else 0.22)
        s.rect(lx0, y + 2, 2, 8, T.accent, r=1)
        s.rect(lx1 - 2, y + 2, 2, 8, T.accent, r=1)
    for name, b in markers:
        mx = x + (b - start) * px_bar
        s.path(f"M{mx} {y+2} h7 l3 4 l-3 4 h-7 Z", fill=T.text2)
        s.text(mx + 13, y + 10, name, 9.5, T.text1, UI, 600)
    for i in range(int(bars) + 1):
        bx = x + i * px_bar
        s.line(bx, y + 14, bx, y + h, T.lineStrong, 1)
        s.text(bx + 4, y + h - 5, str(i + first), 10, T.text1, MONO, 500)
        for q in range(1, sub):
            qx = bx + q * px_bar / sub
            s.line(qx, y + h - 4, qx, y + h, T.line, 1)
    if playhead is not None:
        px = x + (playhead - start) * px_bar
        s.path(f"M{px-5} {y+h-9} h10 l-5 6 Z", fill=T.text0)
    s.g_end()
    s.line(x, y + h - 0.5, x + w, y + h - 0.5, T.line, 1)


def clip_preview(s, T, x, y, w, h, color, seed, drum=False, bars=1, loop_bars=None):
    rnd = random.Random(seed)
    if drum:
        rows = 4
        rh = (h - 4) / rows
        steps = int(bars * 16)
        for st in range(steps):
            for r in range(rows):
                pat = [st % 4 == 0, st % 8 == 4, st % 2 == 0, rnd.random() < 0.15][r]
                if pat:
                    sx = x + st * w / steps
                    s.rect(sx + 0.5, y + 2 + r * rh, max(w / steps - 1.5, 1.2), max(rh - 1.5, 1.5), color, r=0.8,
                           op=0.9)
        return
    n = int(bars * 6)
    for i in range(n):
        st = (i + rnd.random() * 0.4) / n
        ln = (0.6 + rnd.random() * 0.9) / n
        py = rnd.randint(0, 9)
        s.rect(x + st * w, y + 3 + py * (h - 8) / 10, max(ln * w, 2), max((h - 8) / 10 - 0.6, 1.6), color, r=0.8)


def arr_clip(s, T, x, y, w, h, color, name, linked=False, selected=False, muted=False, drum=False, seed=0,
             loop_px=None):
    if selected:
        s.rect(x - 2.5, y - 2.5, w + 5, h + 5, "none", r=R_ITEM + 2, stroke=T.accent, sw=1.5)
    s.rect(x, y, w, h, color, r=R_ITEM, op=0.08 if muted else T.tintAlpha + (0.06 if selected else 0))
    if muted:
        s.rect(x, y, w, h, s.hatch(T.text2, 0.25), r=R_ITEM)
    s.rect(x + 0.5, y + 0.5, w - 1, h - 1, "none", r=R_ITEM, stroke=color, sw=1, sop=0.35 if muted else 0.6)
    s.clip_begin(x, y, w, h, R_ITEM)
    lbl = T.text1 if muted else (color if T.dark else T.text0)
    s.text(x + 6, y + 12.5, name, 10.5, lbl, UI, 600)
    if linked:
        icon(s, "link", x + w - 11, y + 8.5, T.text1, 11)
    pv_col = T.text2 if muted else color
    if loop_px:
        k = 0
        while x + k * loop_px < x + w - 1:
            clip_preview(s, T, x + k * loop_px + 3, y + 17, loop_px - 6, h - 20, pv_col, seed, drum,
                         bars=loop_px / 48)
            if k:
                lx = x + k * loop_px
                s.path(f"M{lx-3} {y} L{lx} {y+3.5} L{lx+3} {y} Z", fill=color)
                s.path(f"M{lx-3} {y+h} L{lx} {y+h-3.5} L{lx+3} {y+h} Z", fill=color)
                s.line(lx, y + 4, lx, y + h - 4, color, 1, op=0.35, dash="2 3")
            k += 1
    else:
        clip_preview(s, T, x + 3, y + 17, w - 6, h - 20, pv_col, seed, drum, bars=w / 48)
    s.g_end()


def track_header(s, T, x, y, w, h, ti, selected=False, compact=False):
    name, ring, typ, voices, out = TRACKS[ti]
    color = T.tracks[ring]
    if selected:
        s.rect(x + 4, y + 3, w - 8, h - 6, T.raised, r=R_CTRL + 1)
    chip(s, T, x + 12, y + (h / 2 - 8 if compact else 10), color, ti + 1)
    ty = y + (h / 2 + 4 if compact else 22)
    s.text(x + 36, ty, name, 12, T.text0, UI, 600)
    if not compact:
        s.text(x + 36, y + 38, voices, 10.5, T.text2, UI, 450)
        dot_x = x + 36 + len(voices) * 6.3 + 8
        if out == "none":
            s.circle(dot_x + 3, y + 34.5, 3, "none", stroke=T.text2, sw=1.2)
            s.text(dot_x + 10, y + 38, "no output", 10.5, T.text2, UI, 450)
        else:
            s.circle(dot_x + 3, y + 34.5, 3, T.play)
            s.text(dot_x + 10, y + 38, out, 10.5, T.text1, UI, 450)
    # automation, arm, mute, solo — one row of small tiles
    bx = x + w - 98
    by = y + (h / 2 - 10 if compact else 9)
    state = {"M": ti == 4, "S": False, "arm": ti == 3}
    for k in ("auto", "arm", "M", "S"):
        on = state.get(k, False)
        if k == "arm":
            s.rect(bx, by, 22, 20, T.record if on else T.raised, r=R_CTRL - 1, op=1 if on else (1 if T.dark else 1))
            icon(s, "arm", bx + 11, by + 10, T.onState if on else T.text1, 13)
        elif k == "auto":
            icon(s, "curve", bx + 11, by + 10, T.text1, 13)
        else:
            fill = (T.text1 if k == "M" else T.queue) if on else T.raised
            s.rect(bx, by, 22, 20, fill, r=R_CTRL - 1)
            s.text(bx + 11, by + 14, k, 11, (T.surface if T.dark else "#FFFFFF") if on else T.text1, UI, 700, "middle")
        bx += 24
    if ti == 4:  # muted track: its name dims too, so mute never relies on the button alone
        pass


def launcher_slot(s, T, x, y, w, h, color=None, name=None, state=None, seed=0, drum=False):
    if color is None:
        s.rect(x, y, w, h, T.canvas, r=R_ITEM)
        if state == "stop":
            s.rect(x + w / 2 - 3, y + h / 2 - 3, 6, 6, T.text2, r=1, op=0.6)
        return
    s.rect(x, y, w, h, color, r=R_ITEM, op=T.tintAlpha + 0.04)
    s.rect(x + 0.5, y + 0.5, w - 1, h - 1, "none", r=R_ITEM, stroke=color, sw=1, sop=0.55)
    # launch tab: a solid tessera with the play glyph
    tab = 18
    if state == "playing":
        s.rect(x, y, tab, h, T.play, r=R_ITEM)
        icon(s, "play", x + tab / 2, y + h / 2, T.onState, 11)
    elif state == "queued":
        s.rect(x, y, tab, h, T.queue, r=R_ITEM, op=0.9)
        icon(s, "play", x + tab / 2, y + h / 2, T.onState, 11)
        s.rect(x - 1.5, y - 1.5, w + 3, h + 3, "none", r=R_ITEM + 1.5, stroke=T.queue, sw=1.2, dash="3 2.5")
    elif state == "recording":
        s.rect(x, y, tab, h, T.record, r=R_ITEM)
        s.circle(x + tab / 2, y + h / 2, 3.5, T.onState)
    else:
        s.rect(x, y, tab, h, color, r=R_ITEM)
        icon(s, "play", x + tab / 2, y + h / 2, T.onTrack, 11)
    s.text(x + tab + 5, y + 12, name, 10, color if T.dark else T.text0, UI, 600)
    if h > 26:
        s.clip_begin(x + tab + 3, y + 15, w - tab - 6, h - 17)
        clip_preview(s, T, x + tab + 3, y + 15, w - tab - 6, h - 17, color, seed, drum, bars=1)
        s.g_end()
    if state == "playing":  # progress bar along the bottom: playing is shown by motion and shape, not only green
        s.rect(x + tab + 3, y + h - 3, (w - tab - 6), 2, T.play, r=1, op=0.25)
        s.rect(x + tab + 3, y + h - 3, (w - tab - 6) * 0.62, 2, T.play, r=1)


def arranger_pane(s, T, x, y, w, h, sel_track=2, sel_clip=("Verse A", 4), scenes=3, head_w=232, row_h=52,
                  px_bar=48, bars=20, playhead=9.5):
    tile(s, T, x, y, w, h)
    s.clip_begin(x, y, w, h, R_TILE)
    top = 34
    lw = scenes * 76 + 4
    tx = x + head_w + lw
    tw = w - head_w - lw
    # toolbar over the headers
    bx = x + 8
    for i, ic in enumerate(["pointer", "pencil", "eraser", "scissors", "mute", "range"]):
        btn(s, T, bx, y + 5, 26, 24, ic, active=(i == 0))
        bx += 28
    btn(s, T, x + head_w - 34, y + 5, 26, 24, "plus")
    # scene heads
    for i in range(scenes):
        sx = x + head_w + 2 + i * 76
        s.rect(sx + 2, y + 6, 72, 22, T.raised, r=R_CTRL - 1)
        icon(s, "scene", sx + 16, y + 17, T.text1, 12)
        s.text(sx + 30, y + 21, ["Intro", "Verse", "Drop"][i], 10.5, T.text0, UI, 600)
    ruler(s, T, tx, y, tw, top, px_bar, bars, loop=(4, 12), markers=[("Intro", 0), ("Verse", 4), ("Chorus", 12)],
          playhead=playhead)
    # rows
    s.rect(tx, y + top, tw, h - top, T.canvas)
    for b in range(bars + 1):
        lx = tx + b * px_bar
        if b % 2 == 1:
            s.rect(lx, y + top, px_bar, h - top, T.surface, op=0.35 if T.dark else 0.55)
        s.line(lx, y + top, lx, y + h, T.line, 1)
    s.rect(tx + 4 * px_bar, y + top, 8 * px_bar, h - top, T.accent, op=0.035)
    for ti in range(len(TRACKS)):
        ry = y + top + ti * row_h
        color = tcol(T, ti)
        track_header(s, T, x, ry, head_w, row_h, ti, selected=(ti == sel_track))
        s.line(x + 8, ry + row_h - 0.5, x + w, ry + row_h - 0.5, T.lineFaint if T.dark else T.line, 1)
        # launcher slots
        slots = {0: [("Beat A", "playing"), ("Beat B", None), None], 1: [("Root", "playing"), ("Walk", "queued"), None],
                 2: [None, ("Verse A", None), ("Chorus", None)], 3: [None, None, ("Hook", None)],
                 4: [("Air", None), None, None], 5: [None, None, ("Spark", "recording")]}[ti]
        for si, sl in enumerate(slots):
            sx = x + head_w + 2 + si * 76 + 2
            if sl:
                launcher_slot(s, T, sx, ry + 6, 72, row_h - 12, color, sl[0], sl[1], seed=sh(sl[0]) % 99,
                              drum=(ti == 0))
            else:
                launcher_slot(s, T, sx, ry + 6, 72, row_h - 12, state="stop")
        for c in CLIPS[ti]:
            st, ln, name, linked = c[:4]
            muted = len(c) > 4
            cx = tx + st * px_bar + 1
            sel = (name, st) == sel_clip and ti == sel_track
            arr_clip(s, T, cx, ry + 5, ln * px_bar - 2, row_h - 10, color, name, linked, sel, muted, ti == 0,
                     seed=sh(name) % 97, loop_px=(2 * px_bar if name == "Verse A" else None))
    # add track
    ay = y + top + len(TRACKS) * row_h
    icon(s, "plus", x + 20, ay + 16, T.text2, 12)
    s.text(x + 32, ay + 20, "Add track", 11, T.text2, UI, 500)
    # playhead
    phx = tx + playhead * px_bar
    s.line(phx, y + top, phx, y + h, T.text0, 1.25, op=0.9)
    # pane edges
    s.line(x + head_w, y, x + head_w, y + h, T.line, 1)
    s.line(tx, y, tx, y + h, T.line, 1)
    s.g_end()


# ------------------------------------------------------------------------------------------------ piano roll

KEYS_NOTES = [  # beat, length, pitch, velocity
    (0, 1.5, 55, .8), (0, 1.5, 58, .7), (0, 1.5, 62, .75), (1, .5, 65, .9), (1.5, .5, 60, .6),
    (2, 1.5, 53, .7), (2, 1.5, 57, .65), (2, 1.5, 60, .7), (3, .5, 67, 1.0), (3.5, .5, 62, .55),
    (4, 1.5, 51, .85), (4, 1.5, 55, .8), (4, 1.5, 58, .8), (5, .75, 63, .9), (5.5, .5, 63, .5),
    (6, 2, 50, .7), (6, 2, 53, .65), (6, 2, 57, .7), (7, .5, 62, .95),
]
SELECTED = {(4, 51), (4, 55), (4, 58)}


def detail_header(s, T, x, y, w, color, title, kind="roll"):
    h = 36
    s.rect(x, y, w, h, T.surface)
    btn(s, T, x + 6, y + 6, 24, 24, "chev-l")
    btn(s, T, x + 30, y + 6, 24, 24, "chev-r")
    btn(s, T, x + 60, y + 6, 86, 24, "devices", "Devices")
    chip(s, T, x + 156, y + 10, color, None, 16)
    s.text(x + 180, y + 22.5, title, 12, T.text0, UI, 600)
    s.line(x, y + h - 0.5, x + w, y + h - 0.5, T.line, 1)
    return h


def tool_strip(s, T, x, y, tools, active=0):
    for i, ic in enumerate(tools):
        btn(s, T, x + i * 28, y, 26, 24, ic, active=(i == active))
    return len(tools) * 28


def piano_roll(s, T, x, y, w, h, color, px_beat=64, lo=50, hi=68, playhead_beat=5.0, tabs=True, lane_h=74,
               header=True, compact=False):
    s.clip_begin(x, y, w, h, R_TILE)
    hh = 0
    if header:
        hh = detail_header(s, T, x, y, w, color, "Keys  ·  Verse A")
        hx = x + 330
        s.text(hx, y + 22.5, "2 bars", 11.5, T.text1, MONO, 500)
        tw = tool_strip(s, T, hx + 60, y + 6, ["pointer", "pencil", "brush", "eraser", "scissors", "mute", "velocity"])
        sx = hx + 60 + tw + 10
        s.rect(sx, y + 6, 96, 24, T.raised, r=R_CTRL)
        icon(s, "snap", sx + 13, y + 18, T.text1, 13)
        s.text(sx + 26, y + 22, "1/16", 11.5, T.text0, MONO, 500)
        icon(s, "chev-d", sx + 84, y + 18, T.text2, 11)
        kx = sx + 104
        s.rect(kx, y + 6, 108, 24, T.raised, r=R_CTRL)
        s.text(kx + 10, y + 22, "C minor", 11.5, T.text0, UI, 500)
        icon(s, "chev-d", kx + 96, y + 18, T.text2, 11)
        rx = x + w - 8
        for ic, on in (("speaker", True), ("ghost", False), ("fold", False), ("chord", False), ("more", False)):
            rx -= 28
            btn(s, T, rx, y + 6, 26, 24, ic, active=on)
    ky = y + hh
    rh_ruler = 26
    kw = 56
    lane_total = (22 + lane_h) if tabs else 0
    gy = ky + rh_ruler
    gh = h - hh - rh_ruler - lane_total
    rows = hi - lo
    rowh = gh / rows
    gx = x + kw
    gw = w - kw
    beats = gw / px_beat
    # ruler
    s.rect(x, ky, kw, rh_ruler, T.surface)
    ruler(s, T, gx, ky, gw, rh_ruler, px_beat * 4, beats / 4, loop=(0, 2), playhead=playhead_beat / 4, sub=4)
    # grid
    s.rect(gx, gy, gw, gh, T.canvas)
    for r in range(rows):
        p = hi - 1 - r
        ry = gy + r * rowh
        black = p % 12 in (1, 3, 6, 8, 10)
        if p % 12 not in MINOR:
            s.rect(gx, ry, gw, rowh, T.well if T.dark else T.surface, op=0.9 if T.dark else 0.8)
        elif black:
            s.rect(gx, ry, gw, rowh, T.surface, op=0.5)
        if p % 12 == 0:
            s.rect(gx, ry, gw, rowh, T.accent, op=0.05)
        s.line(gx, ry, gx + gw, ry, T.lineFaint, 1)
    for b in range(int(beats * 4) + 1):
        lx = gx + b * px_beat / 4
        col = T.lineStrong if b % 16 == 0 else (T.line if b % 4 == 0 else T.lineFaint)
        s.line(lx, gy, lx, gy + gh, col, 1)
    # beyond the clip loop
    s.rect(gx + 8 * px_beat, gy, gw - 8 * px_beat, gh, T.grout, op=0.45 if T.dark else 0.25)
    # notes (and their repeat beyond the loop, dimmed)
    for rep in (0, 1):
        for (b, ln, p, v) in KEYS_NOTES:
            if not (lo <= p < hi):
                continue
            nx = gx + (b + rep * 8) * px_beat + 1
            ny = gy + (hi - 1 - p) * rowh + 1
            if nx > gx + gw:
                continue
            if rep:
                s.rect(nx, ny, ln * px_beat - 2, rowh - 2, color, r=R_NOTE, op=0.22)
            else:
                note_tile(s, T, nx, ny, ln * px_beat - 2, rowh - 2, color, v, (b, p) in SELECTED,
                          label=pname(p))
    # hover crosshair readout pill (drag readout)
    if not compact:
        px_, py_ = gx + 4 * px_beat + 70, gy + (hi - 1 - 58) * rowh - 20
        s.rect(px_, py_, 104, 20, T.overlay, r=10)
        if not T.dark:
            s.rect(px_, py_, 104, 20, "none", r=10, stroke="#000", sw=1, sop=0.1)
        s.text(px_ + 10, py_ + 14, "Bb3  ·  vel 80%", 10.5, T.text0, MONO, 500)
    # keyboard: keys as tiles with grout gaps
    s.rect(x, gy, kw, gh, T.grout if T.dark else T.raised)
    for r in range(rows):
        p = hi - 1 - r
        ry = gy + r * rowh
        black = p % 12 in (1, 3, 6, 8, 10)
        kx0 = x + (18 if black else 2)
        s.rect(kx0, ry + 0.5, kw - (20 if black else 4), rowh - 1, T.keyBlack if black else T.keyWhite, r=2)
        if p % 12 == 0:
            s.text(x + 6, ry + rowh - 3.5, pname(p), 8.5, T.keyBlack, MONO, 600)
            s.circle(x + kw - 8, ry + rowh / 2, 2, T.accent)
        if p in (51, 55, 58):  # sounding keys light up in the track colour
            s.rect(kx0, ry + 0.5, kw - (20 if black else 4), rowh - 1, color, r=2, op=0.7)
    # playhead
    phx = gx + playhead_beat * px_beat
    s.line(phx, gy, phx, gy + gh, T.text0, 1.25, op=0.9)
    # expression lanes
    if tabs:
        ty = gy + gh
        s.rect(x, ty, w, 22, T.surface)
        s.line(x, ty + 0.5, x + w, ty + 0.5, T.line, 1)
        tx = gx + 4
        for i, t in enumerate(["Velocity", "Release", "Chance", "Repeats", "Micro", "Pitch", "Timbre", "Pressure",
                               "Automation"]):
            tw_ = len(t) * 6.1 + 18
            if i == 0:
                s.rect(tx, ty + 3, tw_, 17, T.raised, r=R_CTRL - 1)
            s.text(tx + tw_ / 2, ty + 15, t, 10.5, T.text0 if i == 0 else T.text1, UI, 600 if i == 0 else 500,
                   "middle")
            tx += tw_ + 2
        icon(s, "chev-d", tx - 8, ty + 11.5, T.text2, 10)
        ly = ty + 22
        s.rect(x, ly, kw, lane_h, T.surface)
        s.text(x + 8, ly + 16, "Vel", 10.5, T.text1, UI, 600)
        s.text(x + 8, ly + 30, "100%", 9.5, T.text2, MONO, 500)
        s.rect(gx, ly, gw, lane_h, T.canvas)
        for fr in (0.25, 0.5, 0.75):
            s.line(gx, ly + lane_h * fr, gx + gw, ly + lane_h * fr, T.lineFaint, 1)
        for (b, ln, p, v) in KEYS_NOTES:
            bx = gx + b * px_beat + 1
            bh = (lane_h - 10) * v
            sel = (b, p) in SELECTED
            s.rect(bx, ly + lane_h - 4 - bh, 5, bh, color, r=2, op=0.95 if sel else 0.7)
            if sel:
                s.rect(bx - 2, ly + lane_h - 6 - bh, 9, bh + 4, "none", r=3, stroke=T.accent, sw=1.3)
        s.line(phx, ly, phx, ly + lane_h, T.text0, 1.25, op=0.5)
    s.g_end()


# ------------------------------------------------------------------------------------------------ inspector

def field(s, T, x, y, w, label, value, frac=None, color=None):
    s.text(x, y + 15, label, 11, T.text1, UI, 500)
    fx = x + 88
    fw = w - 88
    s.rect(fx, y + 2, fw, 20, T.well if T.dark else T.raised, r=R_CTRL - 1)
    if frac is not None:
        s.rect(fx, y + 2, fw * frac, 20, color or T.accent, r=R_CTRL - 1, op=0.35 if T.dark else 0.28)
        s.rect(fx + fw * frac - 1.5, y + 4, 2, 16, color or T.accent, r=1)
    s.text(fx + fw - 8, y + 16, value, 11, T.text0, MONO, 500, "end")


def inspector(s, T, x, y, w, h, mode="notes"):
    tile(s, T, x, y, w, h)
    s.clip_begin(x, y, w, h, R_TILE)
    px = x + 16
    iw = w - 32
    cy = y + 16
    if mode == "notes":
        s.text(px, cy + 12, "3 notes", 14, T.text0, UI, 600)
        s.rect(px + 64, cy, 46, 18, T.accent, r=9, op=0.2 if T.dark else 0.14)
        s.text(px + 87, cy + 13, "Eb maj", 10, T.accentText, UI, 600, "middle")
        cy += 34
        for lbl, val, fr in (("Start", "2.1.1", None), ("Length", "1/4 + 1/8", None), ("Pitch", "Eb3 – Bb3", None),
                             ("Velocity", "82%", .82), ("Release", "50%", .5), ("Micro", "0%", None)):
            field(s, T, px, cy, iw, lbl, val, fr)
            cy += 28
        cy += 10
        s.text(px, cy + 10, "Operators", 12, T.text0, UI, 600)
        s.text(px + iw, cy + 10, "per note", 10, T.text2, UI, 500, "end")
        cy += 20
        ops = [("Chance", "100%"), ("Repeats", "Off"), ("Occurs", "Always"), ("Every", "1 / 1")]
        for i, (a, b) in enumerate(ops):
            ox = px + (i % 2) * (iw / 2 + 3)
            oy = cy + (i // 2) * 44
            s.rect(ox, oy, iw / 2 - 3, 38, T.raised, r=R_CTRL)
            s.text(ox + 9, oy + 15, a, 10, T.text1, UI, 500)
            s.text(ox + 9, oy + 30, b, 11.5, T.text0, MONO, 500)
        cy += 98
    elif mode == "step":
        s.text(px, cy + 12, "Kick  ·  step 5", 14, T.text0, UI, 600)
        cy += 34
        for lbl, val, fr in (("Velocity", "100%", 1.0), ("Chance", "75%", .75), ("Micro", "+8%", None),
                             ("Repeats", "Off", None), ("Occurs", "Always", None)):
            field(s, T, px, cy, iw, lbl, val, fr)
            cy += 28
        cy += 10
    s.line(px, cy, px + iw, cy, T.line, 1)
    cy += 18
    s.text(px, cy + 6, "Clip", 12, T.text0, UI, 600)
    cy += 18
    rows = [("Name", "Verse A"), ("Length", "2 bars"), ("Loop", "1.1.1 – 3.1.1"), ("Key", "Song (C minor)"),
            ("Used", "2 times")] if mode == "notes" else [("Name", "Beat A"), ("Length", "1 bar"), ("Lanes", "8"),
                                                          ("Used", "4 times")]
    for a, b in rows:
        s.text(px, cy + 14, a, 11, T.text1, UI, 500)
        s.text(px + iw, cy + 14, b, 11, T.text0, UI, 500, "end")
        cy += 22
    s.g_end()


# ------------------------------------------------------------------------------------------------ launcher grid

def launcher_grid(s, T, x, y, w, h):
    tile(s, T, x, y, w, h)
    s.clip_begin(x, y, w, h, R_TILE)
    # header strip
    s.rect(x, y, w, 36, T.surface)
    segmented(s, T, x + 10, y + 6, ["Off", "1/4", "1 bar", "2 bars"], 2, h=24, wpad=10)
    s.text(x + 10 + 220, y + 22.5, "Launch quantize", 10.5, T.text2, UI, 500)
    btn(s, T, x + 360, y + 6, 92, 24, "stop", "Stop all")
    btn(s, T, x + 456, y + 6, 150, 24, "record", "To arrangement", txt=T.text1)
    btn(s, T, x + 610, y + 6, 92, 24, "capture", "Capture")
    s.line(x, y + 36, x + w, y + 36, T.line, 1)
    scene_w = 200
    cols = len(TRACKS)
    col_w = (w - scene_w - 12) / cols
    gy = y + 36
    # column heads
    for ti in range(cols):
        cx = x + 6 + ti * col_w
        color = tcol(T, ti)
        chip(s, T, cx + 6, gy + 10, color, ti + 1)
        s.text(cx + 28, gy + 22.5, TRACKS[ti][0], 12, T.text0, UI, 600)
        icon(s, "arm", cx + col_w - 44, gy + 18, T.record if ti == 5 else T.text2, 13)
        icon(s, "stop", cx + col_w - 22, gy + 18, T.text2, 13)
        # activity meter: a row of tiny tesserae under the head
        for k in range(10):
            on = k < [7, 6, 0, 0, 3, 2][ti]
            s.rect(cx + 6 + k * 7, gy + 33, 5, 3, color if on else T.line, r=1, op=1 if on else 1)
    s.rect(x + w - scene_w - 6, gy + 6, scene_w, 30, T.surface)
    s.text(x + w - scene_w + 6, gy + 25, "Scenes", 12, T.text0, UI, 600)
    rows = 8
    row_h = (h - 36 - 44 - 36) / rows
    sy = gy + 44
    grid = {
        0: {0: ("Beat A", "playing"), 1: ("Beat B", None), 2: ("Beat B", None), 3: ("Fill", None), 5: ("Break", None)},
        1: {0: ("Root", "playing"), 1: ("Walk", "queued"), 2: ("Walk", None), 4: ("Sub", None)},
        2: {1: ("Verse A", None), 2: ("Chorus", None), 3: ("Chorus", None)},
        3: {2: ("Hook", None), 3: ("Hook 2", None)},
        4: {0: ("Air", None), 4: ("Swell", None)},
        5: {2: ("Spark", "recording")},
    }
    for r in range(rows):
        ry = sy + r * row_h
        for ti in range(cols):
            cx = x + 6 + ti * col_w + 3
            cell = grid[ti].get(r)
            if cell:
                launcher_slot(s, T, cx, ry + 3, col_w - 6, row_h - 6, tcol(T, ti), cell[0], cell[1],
                              seed=sh(cell[0]) % 50, drum=(ti == 0))
            else:
                launcher_slot(s, T, cx, ry + 3, col_w - 6, row_h - 6, state="stop" if r < 6 else None)
        # scene cell
        sx = x + w - scene_w - 6
        live = r == 0
        s.rect(sx, ry + 3, scene_w, row_h - 6, T.raised, r=R_ITEM)
        if live:
            s.rect(sx - 1.5, ry + 1.5, scene_w + 3, row_h - 3, "none", r=R_ITEM + 1.5, stroke=T.play, sw=1.5)
        icon(s, "scene", sx + 16, ry + row_h / 2, T.play if live else T.text1, 13)
        s.text(sx + 32, ry + row_h / 2 + 4, ["Intro", "Verse", "Drop", "Break", "Outro", "Bridge", "7", "8"][r], 12,
               T.text0, UI, 600)
        if r == 2:
            s.text(sx + scene_w - 10, ry + row_h / 2 + 4, "128 BPM", 10.5, T.text1, MONO, 500, "end")
    ay = sy + rows * row_h + 8
    icon(s, "plus", x + w - scene_w + 8, ay + 10, T.text2, 12)
    s.text(x + w - scene_w + 20, ay + 14, "Add scene", 11, T.text2, UI, 500)
    s.g_end()


# ------------------------------------------------------------------------------------------------ drum editor

LANES = ["Kick", "Snare", "Clap", "Closed hat", "Open hat", "Low tom", "High tom", "Rim"]
PATTERN = {
    0: {0: 1, 3: .6, 4: .9, 6: .8, 8: 1, 11: .5, 14: .7},
    1: {4: 1, 12: 1, 15: .4},
    2: {4: .7, 12: .7},
    3: {i: (1 if i % 2 == 0 else .5) for i in range(12)},
    4: {2: .8, 10: .8},
    5: {13: .7},
    6: {14: .8, 15: .6},
    7: {7: .5},
}


def drum_editor(s, T, x, y, w, h):
    color = tcol(T, 0)
    s.clip_begin(x, y, w, h, R_TILE)
    hh = detail_header(s, T, x, y, w, color, "Drums  ·  Beat A")
    hx = x + 330
    s.text(hx, y + 22.5, "1 bar", 11.5, T.text1, MONO, 500)
    tool_strip(s, T, hx + 52, y + 6, ["pointer", "pencil", "eraser", "mute", "velocity"])
    segmented(s, T, hx + 200, y + 6, ["Steps", "Grid"], 0, h=24)
    btn(s, T, hx + 330, y + 6, 70, 24, "drum", "Kit")
    btn(s, T, hx + 404, y + 6, 100, 24, "dice", "Generate")
    btn(s, T, x + w - 36, y + 6, 26, 24, "speaker", active=True)
    lw = 196
    top = y + hh
    rh = 26
    s.rect(x, top, w, rh, T.surface)
    gx = x + lw
    gw = w - lw - 10
    steps = 16
    sw_ = gw / steps
    for b in range(4):
        s.text(gx + b * 4 * sw_ + 4, top + 18, f"1.{b+1}", 10, T.text1, MONO, 500)
    s.path(f"M{gx + 9.5*sw_ - 5} {top + rh - 9} h10 l-5 6 Z", fill=T.text0)
    s.line(x, top + rh - 0.5, x + w, top + rh - 0.5, T.line, 1)
    ly0 = top + rh
    lane_h = 34
    s.rect(x, ly0, w, lane_h * len(LANES) + 8, T.canvas)
    for li, name in enumerate(LANES):
        ly = ly0 + 4 + li * lane_h
        sel = li == 0
        if sel:
            s.rect(x + 4, ly + 1, lw - 8, lane_h - 2, T.raised, r=R_CTRL)
        s.rect(x + 12, ly + 8, 4, lane_h - 16, color, r=2)
        s.text(x + 24, ly + 19, name, 11.5, T.text0, UI, 600 if sel else 500)
        s.text(x + lw - 74, ly + 19, str(li + 1), 10, T.text2, MONO, 500, "end")
        for k, bx in (("M", lw - 64), ("S", lw - 40)):
            on = (k == "M" and li == 6)
            s.rect(x + bx, ly + 5, 20, 20, T.text1 if on else T.raised, r=R_CTRL - 1)
            s.text(x + bx + 10, ly + 19, k, 10.5, (T.surface if T.dark else "#fff") if on else T.text1, UI, 700,
                   "middle")
        lane_steps = 12 if li == 3 else 16
        for st in range(steps):
            sx = gx + st * sw_ + 2
            tile_w = sw_ - 4
            ty = ly + 3
            th = lane_h - 6
            beat_shade = (st // 4) % 2 == 1
            v = PATTERN[li].get(st)
            outside = st >= lane_steps
            base = T.surface if beat_shade else T.raised
            if T.dark:
                base = "#1B1D24" if beat_shade else "#22242C"
            if outside:
                s.rect(sx, ty, tile_w, th, "none", r=3, stroke=T.line, sw=1, dash="2 2")
                continue
            s.rect(sx, ty, tile_w, th, base, r=3)
            if v:
                fh = th * (0.35 + 0.65 * v)
                muted = li == 6
                s.rect(sx, ty + th - fh, tile_w, fh, color, r=3, op=0.35 if muted else 1)
                if T.dark and not muted:
                    s.line(sx + 2, ty + th - fh + 0.75, sx + tile_w - 2, ty + th - fh + 0.75, "#fff", 1, op=0.35)
                if li == 0 and st == 6:  # chance < 100%: a corner notch, so it reads without colour
                    s.path(f"M{sx+tile_w-8} {ty} h8 v8 Z", fill=T.onTrack, op=0.85)
                if li == 3 and st in (5, 9):  # repeats: small ticks along the top
                    for q in range(3):
                        s.rect(sx + 3 + q * 4, ty + 3, 2, 4, T.onTrack, r=0.5)
        if li == 3:
            s.text(gx + 12 * sw_ + 6, ly + 19, "12 steps", 10, T.text2, UI, 500)
    # selected step ring
    sx = gx + 4 * sw_ + 2
    s.rect(sx - 2.5, ly0 + 4 + 3 - 2.5, sw_ - 4 + 5, lane_h - 6 + 5, "none", r=5, stroke=T.accent, sw=1.5)
    # playing column
    pcx = gx + 9 * sw_
    s.rect(pcx, ly0, sw_, lane_h * len(LANES) + 8, T.text0, op=0.05)
    # velocity lane
    vy = ly0 + lane_h * len(LANES) + 8
    vh = y + h - vy
    s.rect(x, vy, w, vh, T.surface)
    s.line(x, vy + 0.5, x + w, vy + 0.5, T.line, 1)
    s.text(x + 12, vy + 20, "Velocity", 11, T.text1, UI, 600)
    s.text(x + 12, vy + 35, "Kick", 10.5, T.text2, UI, 500)
    s.rect(gx, vy + 6, gw, vh - 12, T.canvas, r=R_CTRL)
    for st, v in PATTERN[0].items():
        bx = gx + st * sw_ + sw_ / 2 - 4
        bh = (vh - 22) * v
        s.rect(bx, vy + vh - 10 - bh, 8, bh, color, r=2.5, op=0.85)
    s.g_end()


# ------------------------------------------------------------------------------------------------ compositions

def editor_frame(T, W=1440, H=900):
    s = Svg(W, H, T.grout)
    return s


def mock_arranger(T, path):
    W, H = 1440, 900
    s = editor_frame(T, W, H)
    transport(s, T, GAP, GAP, W - 2 * GAP)
    top_y = GAP + 46 + GAP
    top_h = 34 + 52 * 6 + 34
    arranger_pane(s, T, GAP, top_y, W - 2 * GAP, top_h)
    by = top_y + top_h + GAP
    bh = H - by - GAP
    insp_w = 292
    dw = W - 3 * GAP - insp_w
    tile(s, T, GAP, by, dw, bh)
    piano_roll(s, T, GAP, by, dw, bh, tcol(T, 2), px_beat=96, lo=49, hi=69, playhead_beat=5.0)
    inspector(s, T, GAP + dw + GAP, by, insp_w, bh)
    s.save(path)


def mock_launcher(T, path):
    W, H = 1440, 900
    s = editor_frame(T, W, H)
    transport(s, T, GAP, GAP, W - 2 * GAP, mode="Launcher", view="Show arranger")
    top_y = GAP + 46 + GAP
    top_h = 470
    launcher_grid(s, T, GAP, top_y, W - 2 * GAP, top_h)
    by = top_y + top_h + GAP
    bh = H - by - GAP
    insp_w = 292
    dw = W - 3 * GAP - insp_w
    tile(s, T, GAP, by, dw, bh)
    piano_roll(s, T, GAP, by, dw, bh, tcol(T, 2), px_beat=96, lo=50, hi=68, playhead_beat=1.6, lane_h=0,
               tabs=False, compact=True)
    inspector(s, T, GAP + dw + GAP, by, insp_w, bh)
    s.save(path)


def mock_drums(T, path):
    W, H = 1440, 900
    s = editor_frame(T, W, H)
    transport(s, T, GAP, GAP, W - 2 * GAP)
    top_y = GAP + 46 + GAP
    top_h = 34 + 52 * 6 + 34
    arranger_pane(s, T, GAP, top_y, W - 2 * GAP, top_h, sel_track=0, sel_clip=("Beat A", 4))
    by = top_y + top_h + GAP
    bh = H - by - GAP
    insp_w = 292
    dw = W - 3 * GAP - insp_w
    tile(s, T, GAP, by, dw, bh)
    drum_editor(s, T, GAP, by, dw, bh)
    inspector(s, T, GAP + dw + GAP, by, insp_w, bh, mode="step")
    s.save(path)
