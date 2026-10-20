"""Shared primitives for the Tessera "Grout & Glaze" mockups: tiles, buttons, chips, demo song data."""
import random
from svg import Svg, Theme, icon

UI, MONO = "Instrument Sans", "JetBrains Mono"
GAP = 6        # grout between panes
R_PANE = 8     # pane tiles
R_CTRL = 5     # buttons, fields
R_ITEM = 4     # clips, slots
R_NOTE = 2.5   # notes, steps

# name, colour index (0..15 on the ring), kind, output module
TRACKS = [
    ("Drums", 0, "drum", "Drum Out"),
    ("Bass", 2, "inst", "Track Out"),
    ("Keys", 7, "inst", "Track Out"),
    ("Lead", 10, "inst", "Track Out"),
    ("Pad", 13, "inst", None),
    ("Arp", 4, "inst", "Track Out"),
]
# start bar, bars, name, linked?, loop length in bars (repeats drawn as separate tiles), muted?
CLIPS = {
    0: [(0, 4, "Beat A", True, 1), (4, 4, "Beat A", True, 1), (8, 8, "Beat B", False, 2), (16, 4, "Fill", False, 4)],
    1: [(0, 8, "Root", False, 4), (8, 8, "Walk", False, 4), (16, 4, "Root", False, 4)],
    2: [(4, 4, "Verse A", True, 2), (8, 4, "Verse A", True, 2), (12, 8, "Chorus", False, 4)],
    3: [(8, 8, "Hook", True, 4), (16, 4, "Hook", True, 4)],
    4: [(0, 16, "Air", False, 16), (16, 4, "Air", False, 4, "muted")],
    5: [(12, 8, "Spark", False, 2)],
}
SLOTS = {  # per track: scene -> (name, state)
    0: {0: ("Beat A", "playing"), 1: ("Beat B", None), 3: ("Fill", None)},
    1: {0: ("Root", None), 1: ("Walk", "queued")},
    2: {1: ("Verse A", "playing"), 2: ("Chorus", None)},
    3: {2: ("Hook", None), 4: ("Hook 2", None)},
    4: {0: ("Air", "playing"), 3: ("Swell", None)},
    5: {2: ("Spark", "recording")},
}
SCENES = [("Intro", None), ("Verse", None), ("Drop", "128"), ("Break", None), ("Outro", None), ("Bridge", None),
          ("", None), ("", None)]
MINOR = {0, 2, 3, 5, 7, 8, 10}


def sh(t):
    return sum(ord(ch) * (i + 1) for i, ch in enumerate(str(t)))


def tcol(T, ti):
    return T.tracks[TRACKS[ti][1]]


def tw(text, size, mono=False, weight=400):
    """Rough text width for layout."""
    k = 0.6 if mono else (0.56 if weight >= 600 else 0.53)
    return len(str(text)) * size * k


# ------------------------------------------------------------------------------------------------ tiles

def pane(s, T, x, y, w, h, fill=None, r=R_PANE):
    """A pane is a glazed tile on the grout."""
    s.rect(x, y, w, h, fill or T.surface, r=r)
    if T.dark:
        s.clip_begin(x, y, w, h, r)
        s.line(x + r, y + 0.5, x + w - r, y + 0.5, "#FFFFFF", 1, op=0.07)
        s.g_end()
    else:
        s.rect(x + 0.5, y + 0.5, w - 1, h - 1, "none", r=r, stroke="#3A2F1E", sw=1, sop=0.10)


def well(s, T, x, y, w, h, r=R_ITEM):
    s.rect(x, y, w, h, T.well, r=r)
    if not T.dark:
        s.rect(x + 0.5, y + 0.5, w - 1, h - 1, "none", r=r, stroke="#3A2F1E", sw=1, sop=0.06)


def btn(s, T, x, y, w, h, ico=None, label=None, active=False, primary=False, color=None, caret=False, size=11,
        weight=500, ghost=False, txt=None):
    """Buttons: ghost (no fill), raised, active (tinted), primary (accent)."""
    if primary:
        s.rect(x, y, w, h, T.accent, r=R_CTRL)
        fg = T.onAccent
    elif active:
        c = color or T.accent
        s.rect(x, y, w, h, c, r=R_CTRL, op=0.22 if T.dark else 0.13)
        s.rect(x + 0.5, y + 0.5, w - 1, h - 1, "none", r=R_CTRL, stroke=c, sw=1, sop=0.55)
        fg = color or T.accentText
    elif ghost:
        fg = T.text1
    else:
        s.rect(x, y, w, h, T.raised, r=R_CTRL)
        if T.dark:
            s.line(x + 3, y + 0.5, x + w - 3, y + 0.5, "#fff", 1, op=0.05)
        else:
            s.rect(x + 0.5, y + 0.5, w - 1, h - 1, "none", r=R_CTRL, stroke="#3A2F1E", sw=1, sop=0.13)
        fg = T.text1
    fg = txt or fg
    cx = x + w / 2
    if ico and label:
        lw = tw(label, size, weight=weight)
        total = 16 + 6 + lw + (12 if caret else 0)
        ix = x + (w - total) / 2
        icon(s, ico, ix + 8, y + h / 2, fg, 15)
        s.text(ix + 22, y + h / 2 + size * 0.36, label, size, fg, UI, weight)
        if caret:
            icon(s, "chev-d", ix + 22 + lw + 7, y + h / 2, fg, 11)
    elif ico:
        icon(s, ico, cx, y + h / 2, fg, 16)
    elif label:
        if caret:
            lw = tw(label, size, weight=weight)
            s.text(x + (w - lw - 12) / 2, y + h / 2 + size * 0.36, label, size, fg, UI, weight)
            icon(s, "chev-d", x + (w + lw - 12) / 2 + 9, y + h / 2, fg, 11)
        else:
            s.text(cx, y + h / 2 + size * 0.36, label, size, fg, UI, weight, "middle")


def track_tile(s, T, x, y, color, num, size=18, screen=False):
    """The track's identity tile: the same square in the editor, on the panel and on its output module."""
    s.rect(x, y, size, size, color, r=size * 0.24)
    s.text(x + size / 2, y + size * 0.69, num, size * 0.6, T.onTile, MONO, 700, "middle")


def logo(s, T, x, y, k=1.0, colors=None):
    """Four tesserae, one tilted out of line: the mark."""
    c = colors or [T.tracks[0], T.tracks[7], T.tracks[10], T.accent]
    u = 6 * k
    g = 1.6 * k
    s.rect(x, y, u, u, c[0], r=1.4 * k)
    s.rect(x + u + g, y, u, u, c[1], r=1.4 * k)
    s.rect(x, y + u + g, u, u, c[2], r=1.4 * k)
    s.g(f"rotate(12 {x + 1.5 * u + g:.2f} {y + 1.5 * u + g:.2f})")
    s.rect(x + u + g, y + u + g, u, u, c[3], r=1.4 * k)
    s.g_end()


def toggle_word(s, T, x, y, label, on, h=24, size=11):
    w = tw(label, size, weight=500) + 18
    btn(s, T, x, y, w, h, label=label, active=on, size=size)
    return w


def divider(s, T, x, y1, y2):
    s.line(x, y1, x, y2, T.line1 if T.dark else T.line2, 1, op=1 if T.dark else 0.7)


def badge(s, T, x, y, text, color, size=9):
    w = tw(text, size, weight=600) + 10
    s.rect(x, y, w, size + 6, color, r=(size + 6) / 2, op=0.20 if T.dark else 0.14)
    s.text(x + w / 2, y + size + 1.5, text, size, color, UI, 600, "middle")
    return w


def note_tile(s, T, x, y, w, h, color, vel=1.0, selected=False, muted=False, label=None, ghost=False):
    if ghost:
        s.rect(x, y, w, h, color, r=R_NOTE, op=0.16 if T.dark else 0.20)
        return
    if muted:
        s.rect(x, y, w, h, color, r=R_NOTE, op=0.25)
        s.rect(x, y, w, h, s.hatch(color, 0.6), r=R_NOTE)
        return
    s.rect(x, y, w, h, color, r=R_NOTE, op=0.45 + 0.55 * vel)
    if T.dark:
        s.line(x + 1.5, y + 0.6, x + w - 1.5, y + 0.6, "#fff", 1, op=0.25)
    if selected:
        s.rect(x - 1, y - 1, w + 2, h + 2, "none", r=R_NOTE + 1, stroke=T.accent, sw=1.6)
        s.rect(x + 0.5, y + 0.5, w - 1, h - 1, "none", r=R_NOTE, stroke="#FFFFFF", sw=1, sop=0.55)
    if label and w > 26 and h >= 10:
        s.text(x + 4, y + h / 2 + 3.4, label, min(9.5, h - 1), T.onTile, UI, 600)


def pname(p):
    return ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"][p % 12] + str(p // 12 - 1)


def caption(s, T, x, y, text, size=9, color=None, anchor="start"):
    s.text(x, y, text.upper(), size, color or T.text2, UI, 600, anchor, ls=0.7)
