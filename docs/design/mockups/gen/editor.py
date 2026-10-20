"""Editor mockups following PROPOSAL.md: one Song view (tracks, clip shelf, timeline), the Notes pane, the inspector,
the widened shelf (Perform), and the drum editor."""
import random
from svg import Svg, icon
from common import *

W, H = 1440, 900


# ------------------------------------------------------------------------------------------------ transport

def plays_switch(s, T, x, cy, plays="timeline", playing=True, h=30, seg_w=78, size=12):
    """What plays: the clips or the timeline. One two-way switch; the lit half is what you hear."""
    w = 2 * seg_w + 6
    s.rect(x, cy - h / 2, w, h, T.well, r=R_CTRL + 1)
    if not T.dark:
        s.rect(x + 0.5, cy - h / 2 + 0.5, w - 1, h - 1, "none", r=R_CTRL + 1, stroke="#3A2F1E", sw=1, sop=0.12)
    for i, (key, ico, word) in enumerate((("clips", "launcher", "Clips"), ("timeline", "timeline", "Timeline"))):
        sx = x + 3 + i * seg_w
        on = key == plays
        if on:
            col = T.play if playing else T.raised
            s.rect(sx, cy - h / 2 + 3, seg_w, h - 6, col, r=R_CTRL - 1)
        fg = (T.onState if playing else T.text0) if on else T.text2
        lw = tw(word, size, weight=600)
        ix = sx + (seg_w - lw - 22) / 2
        icon(s, "play" if on and playing else ico, ix + 7, cy, fg, 13)
        s.text(ix + 20, cy + size * 0.36, word, size, fg, UI, 600 if on else 500)
    return w


def max_btn(s, T, x, y, maximized=False, size=24):
    """Maximize / restore: the same button in the corner of every pane."""
    btn(s, T, x, y, size, size, ico="restore" if maximized else "max", ghost=not maximized, active=maximized)


def transport(s, T, x, y, w, h=44, playing=True, recording=False, plays="timeline"):
    pane(s, T, x, y, w, h)
    cy = y + h / 2
    # instance menu
    logo(s, T, x + 14, cy - 7.5, 1.0)
    s.text(x + 38, cy + 4.5, "Main", 13, T.text0, UI, 600)
    icon(s, "chev-d", x + 78, cy, T.text2, 12)
    divider(s, T, x + 94, y + 10, y + h - 10)
    # transport group
    bx = x + 104
    if playing:
        s.rect(bx, cy - 15, 34, 30, T.play, r=R_CTRL)
        icon(s, "play", bx + 17, cy, T.onState, 16)
    else:
        btn(s, T, bx, cy - 15, 34, 30, ico="play")
    btn(s, T, bx + 38, cy - 15, 30, 30, ico="stop", ghost=True)
    # record + its settings caret: one control, the caret opens count-in, quantize, new clip length
    s.rect(bx + 72, cy - 15, 44, 30, T.raised, r=R_CTRL) if not recording else s.rect(bx + 72, cy - 15, 44, 30, T.record, r=R_CTRL)
    icon(s, "record", bx + 88, cy, T.record if not recording else T.onState, 15)
    icon(s, "chev-d", bx + 106, cy + 1, T.text2, 10)
    # capture: lit dot when something was played and can be kept
    btn(s, T, bx + 120, cy - 15, 30, 30, ico="capture", ghost=True)
    s.circle(bx + 143, cy - 9, 3, T.record)
    btn(s, T, bx + 154, cy - 15, 30, 30, ico="loop", active=True)
    divider(s, T, bx + 194, y + 10, y + h - 10)
    caption(s, T, bx + 206, cy + 3.5, "Plays", 9)
    sw_ = plays_switch(s, T, bx + 242, cy, plays, playing)
    divider(s, T, bx + 252 + sw_, y + 10, y + h - 10)
    # song settings: position, tempo, swing, metre, key
    px = bx + 264 + sw_
    s.text(px, cy + 6, "9.3.1", 19, T.text0, MONO, 500)
    caption(s, T, px + 64, cy + 5, "bar", 8)
    divider(s, T, px + 90, y + 12, y + h - 12)
    s.text(px + 102, cy + 6, "120.00", 19, T.text0, MONO, 500)
    caption(s, T, px + 176, cy + 5, "bpm", 8)
    divider(s, T, px + 204, y + 12, y + h - 12)
    s.text(px + 216, cy + 5, "Swing", 11, T.text2, UI, 500)
    s.text(px + 252, cy + 5.5, "12%", 13, T.text0, MONO, 500)
    divider(s, T, px + 290, y + 12, y + h - 12)
    s.text(px + 302, cy + 5.5, "4/4", 13, T.text0, MONO, 500)
    btn(s, T, px + 336, cy - 13, 96, 26, label="C minor", caret=True, ghost=True, size=12, txt=T.text0)
    # one tool palette for whichever pane has focus
    tools = ["pointer", "pencil", "brush", "eraser", "scissors", "mute", "velocity", "range"]
    tx = x + w - 10 - 214 - len(tools) * 28
    s.rect(tx - 4, cy - 15, len(tools) * 28 + 8, 30, T.well, r=R_CTRL + 1)
    for i, t in enumerate(tools):
        if i == 0:
            s.rect(tx + i * 28, cy - 12, 26, 24, T.raised, r=R_CTRL - 1)
            if not T.dark:
                s.rect(tx + i * 28 + 0.5, cy - 11.5, 25, 23, "none", r=R_CTRL - 1, stroke="#3A2F1E", sw=1, sop=0.15)
        icon(s, t, tx + i * 28 + 13, cy, T.accentText if i == 0 else T.text1, 15)
    # right side
    rx = x + w - 10
    btn(s, T, rx - 28, cy - 14, 28, 28, ico="close", ghost=True)
    btn(s, T, rx - 60, cy - 14, 28, 28, ghost=True)
    s.text(rx - 46, cy + 5, "?", 14, T.text1, UI, 600, "middle")
    btn(s, T, rx - 92, cy - 14, 28, 28, ico="inspector", active=True)
    btn(s, T, rx - 124, cy - 14, 28, 28, ico="follow", active=True)
    divider(s, T, rx - 132, y + 10, y + h - 10)
    btn(s, T, rx - 166, cy - 14, 28, 28, ico="redo", ghost=True)
    btn(s, T, rx - 196, cy - 14, 28, 28, ico="undo", ghost=True)


# ------------------------------------------------------------------------------------------------ song pane

def ruler_bars(s, T, x, y, w, h, px_bar, bars, loop=None, playhead=None, markers=()):
    s.rect(x, y, w, h, T.surface)
    if loop:
        lx0, lx1 = x + loop[0] * px_bar, x + loop[1] * px_bar
        s.rect(lx0, y + 3, lx1 - lx0, 9, T.accent, r=2.5, op=0.26 if T.dark else 0.18)
        s.rect(lx0, y + 3, 2.5, 9, T.accent, r=1)
        s.rect(lx1 - 2.5, y + 3, 2.5, 9, T.accent, r=1)
    for b in range(bars + 1):
        bx = x + b * px_bar
        s.line(bx, y + h - 9, bx, y + h, T.line2, 1)
        for q in (1, 2, 3):
            s.line(bx + q * px_bar / 4, y + h - 4, bx + q * px_bar / 4, y + h, T.line1 if T.dark else T.line2, 1,
                   op=0.8)
        if b < bars:
            s.text(bx + 4, y + h - 4, str(b + 1), 10, T.text2, MONO, 500)
    for mb, name in markers:
        mx = x + mb * px_bar
        mw = tw(name, 9, weight=600) + 10
        s.rect(mx + 4, y + 3, mw, 9, T.text1, r=2, op=0.9)
        s.text(mx + 9, y + 10.5, name, 8, T.surface, UI, 700)
    if playhead is not None:
        px = x + playhead * px_bar
        s.rect(px - 4, y + h - 9, 8, 8, T.accent, r=2)
    s.line(x, y + h - 0.5, x + w, y + h - 0.5, T.line1, 1)


def clip_tile(s, T, x, y, w, h, color, name, seed, drum=False, loop_px=None, linked=False, selected=False,
              muted=False, dimmed=False):
    """A clip is a tinted tile; each loop repeat is its own tile, joined by a grout seam."""
    seg = loop_px if loop_px and loop_px < w - 2 else w
    k = 0
    xx = x
    while xx < x + w - 1:
        sw = min(seg, x + w - xx)
        first = k == 0
        op_body = (0.30 if T.dark else 0.30) if first else (0.20 if T.dark else 0.19)
        if dimmed:
            op_body *= 0.45
        s.rect(xx, y, sw - (1.5 if xx + sw < x + w - 1 else 0), h, color, r=R_ITEM, op=op_body)
        if muted:
            s.rect(xx, y, sw, h, s.hatch(color, 0.35), r=R_ITEM)
        # content
        rnd = random.Random(seed)
        top, bot = y + (16 if h >= 34 else 4), y + h - 4
        nop = (0.95 if first else 0.65) * (0.4 if dimmed or muted else 1)
        if drum:
            for i in range(int(sw // 5)):
                if rnd.random() < 0.55:
                    ry = top + rnd.choice([0, 0.3, 0.6, 0.85]) * (bot - top - 2)
                    s.rect(xx + 3 + i * 5, ry, 2, 2, color, op=nop)
        else:
            px = xx + 3
            while px < xx + sw - 6:
                ln = rnd.choice([6, 10, 14, 20])
                ry = top + rnd.random() * (bot - top - 2)
                s.rect(px, ry, min(ln, xx + sw - 4 - px), 2.2, color, r=1, op=nop)
                px += ln + rnd.choice([2, 4, 8])
        xx += seg
        k += 1
    if h >= 34:
        s.text(x + 6, y + 12, name, 10, T.text0 if not dimmed else T.text2, UI, 600)
        if linked:
            icon(s, "link", x + w - 10, y + 8, T.text1, 11)
    if selected:
        s.rect(x - 1, y - 1, w + 2, h + 2, "none", r=R_ITEM + 1, stroke=T.accent, sw=1.8)


def jack_dot(s, T, x, y, connected=True):
    """Output status: a tiny jack. Filled ring = an output module plays this track; hollow = none."""
    if connected:
        s.circle(x, y, 4.2, T.text1)
        s.circle(x, y, 1.8, T.surface)
    else:
        s.circle(x, y, 4, "none", stroke=T.text2, sw=1.2)


def track_header(s, T, x, y, w, h, ti, selected=False, state="Timeline", armed=False, muted=False, solo=False,
                 expanded=False, hover=False):
    name, ci, kind, out = TRACKS[ti]
    color = tcol(T, ti)
    if selected:
        s.rect(x, y, w, h, T.raised if T.dark else T.hover)
    s.rect(x, y + 3, 3, h - 6, color, r=1.5)
    icon(s, "chev-d" if expanded else "chev-r", x + 14, y + 17, T.text2, 11)
    track_tile(s, T, x + 24, y + 8, color, ti + 1)
    s.text(x + 49, y + 21, name, 12.5, T.text0, UI, 600)
    if kind == "drum":
        icon(s, "drum", x + 49 + tw(name, 12.5, weight=600) * 1.12 + 12, y + 16.5, T.text2, 12)
    # second line: what it plays, where it goes
    ly = y + 37
    if state.startswith("▶"):
        s.text(x + 49, ly, state, 10.5, T.play, UI, 500)
        sx = x + 49 + tw(state, 10.5) + 4
    else:
        s.text(x + 49, ly, state, 10.5, T.text2, UI, 500)
        sx = x + 49 + tw(state, 10.5) + 4
    if h >= 40:
        jack_dot(s, T, sx + 8, ly - 3.5, out is not None)
        s.text(sx + 16, ly, out or "no output", 10.5, T.text2 if out else T.text2, UI, 400)
    # right controls: arm only when armed or hovered; M and S always
    bx = x + w - 54
    if armed or hover:
        btn(s, T, bx - 26, y + 9, 22, 20, active=armed, color=T.record)
        s.circle(bx - 15, y + 19, 4.2, T.record if armed else T.text2)
    btn(s, T, bx, y + 9, 22, 20, label="M", active=muted, color=T.queue, size=10, weight=700)
    btn(s, T, bx + 26, y + 9, 22, 20, label="S", active=solo, color=T.accent, size=10, weight=700)
    s.line(x, y + h - 0.5, x + w, y + h - 0.5, T.line1, 1)


def slot(s, T, x, y, w, h, color=None, name=None, state=None, seed=0, drum=False, big=False, progress=0.62):
    if not name:
        well(s, T, x, y, w, h)
        return
    solid = state == "playing"
    s.rect(x, y, w, h, color, r=R_ITEM, op=1 if solid else (0.30 if T.dark else 0.26))
    # launch area at left
    lw = 18 if not big else 22
    if solid:
        s.rect(x, y, lw, h, "#000", r=R_ITEM, op=0.18)
        icon(s, "play", x + lw / 2, y + h / 2, T.onTile, 11 if not big else 13)
    elif state == "recording":
        s.rect(x, y, lw, h, T.record, r=R_ITEM)
        s.circle(x + lw / 2, y + h / 2, 3.6, "#fff")
    else:
        icon(s, "play", x + lw / 2, y + h / 2, color, 11 if not big else 13)
    fg = T.onTile if solid else T.text0
    s.text(x + lw + 5, y + 13 if not big else y + 16, name, 10 if not big else 11.5, fg, UI, 600)
    # mini preview
    rnd = random.Random(seed)
    top, bot = y + (18 if not big else 24), y + h - 5
    px = x + lw + 5
    while px < x + w - 8 and bot - top > 4:
        ln = rnd.choice([4, 6, 9])
        ry = top + rnd.random() * (bot - top - 2)
        s.rect(px, ry, ln, 2, T.onTile if solid else color, r=1, op=0.7)
        px += ln + 3
    if solid:  # progress through the clip: a bar along the bottom (shape, not only colour)
        s.rect(x + lw, y + h - 3, (w - lw) * progress, 3, T.onTile, op=0.55)
    if state == "queued":
        s.rect(x + 0.75, y + 0.75, w - 1.5, h - 1.5, "none", r=R_ITEM, stroke=T.queue, sw=1.5, dash="4 3")
        if big:
            s.text(x + w - 6, y + h - 6, "queued", 9.5, T.queue, UI, 600, "end")
    if state == "recording" and big:
        s.text(x + w - 6, y + h - 6, "recording", 9.5, T.record, UI, 600, "end")


OVERRIDES = (0, 4)   # in Timeline mode, these tracks were taken over by a launched clip


def slot_state(ti, sc, plays):
    info = SLOTS[ti].get(sc)
    if not info:
        return None
    st = info[1]
    if plays == "timeline" and st == "playing" and ti not in OVERRIDES:
        st = None
    return st


def track_state(ti, plays):
    if ti == 5:
        return "● Recording"
    playing = [v[0] for v in SLOTS[ti].values() if v[1] == "playing"]
    if plays == "timeline":
        return "▶ " + playing[0] if ti in OVERRIDES else "Timeline"
    return "▶ " + playing[0] if playing else "Stopped"


def playing_tag(s, T, x, y):
    w = 66
    s.rect(x, y, w, 18, T.play, r=9, op=0.18 if T.dark else 0.14)
    icon(s, "play", x + 11, y + 9, T.play, 9)
    s.text(x + 19, y + 12.5, "PLAYING", 8.5, T.play, UI, 700, ls=0.6)
    return w


def section_glow(s, T, x, y, w, h):
    """The section that plays gets a jade frame and a tinted header."""
    s.rect(x, y, w, 32, T.play, op=0.10 if T.dark else 0.08)
    s.rect(x + 0.75, y + 0.75, w - 1.5, h - 1.5, "none", r=R_PANE - 2, stroke=T.play, sw=1.5, sop=0.85)


def song_pane(s, T, x, y, w, h, scenes=3, col_w=78, head_w=226, row_h=50, lane_track=1, perform=False,
              sel_clip=(2, 1), plays="timeline", maximized=False):
    pane(s, T, x, y, w, h)
    clips_x1 = x + head_w + scenes * col_w + 12 if not perform else x + w - 36
    if plays == "clips":
        section_glow(s, T, x + head_w + 1, y + 1, clips_x1 - x - head_w - 1, h - 2)
    elif not perform:
        section_glow(s, T, clips_x1, y + 1, x + w - clips_x1 - 1, h - 2)
    hdr = 32
    shelf_w = scenes * col_w + 12
    tl_x = x + head_w + shelf_w + (0 if perform else 0)
    tl_w = 0 if perform else (x + w - tl_x - 6)
    # header row
    s.text(x + 14, y + 21, "Tracks", 12, T.text0, UI, 600)
    s.text(x + 62, y + 21, "6", 11, T.text2, MONO, 500)
    btn(s, T, x + head_w - 34, y + 6, 24, 20, ico="plus", ghost=True)
    divider(s, T, x + head_w, y + 6, y + h - 6)
    cx0 = x + head_w + 8
    s.text(cx0, y + 21, "Clips", 12, T.text0, UI, 600)
    if perform:
        bx = cx0 + 46
        s.text(bx, y + 21, "Launch", 11, T.text2, UI, 500)
        segs = ["Off", "1/4", "1 bar", "2 bars"]
        sx = bx + 44
        s.rect(sx - 2, y + 6, 4 + sum(tw(t, 11) + 16 for t in segs), 20, T.well, r=R_CTRL)
        for i, t in enumerate(segs):
            ww = tw(t, 11) + 16
            if i == 2:
                s.rect(sx, y + 8, ww, 16, T.raised, r=R_CTRL - 1)
            s.text(sx + ww / 2, y + 20, t, 11, T.text0 if i == 2 else T.text1, UI, 500, "middle")
            sx += ww
        btn(s, T, sx + 14, y + 5, 86, 22, ico="stop", label="Stop all", size=11)
        btn(s, T, sx + 106, y + 5, 168, 22, ico="record", label="Record into timeline", size=11)
        if plays == "clips":
            playing_tag(s, T, sx + 290, y + 7)
    else:
        btn(s, T, cx0 + 42, y + 6, 66, 20, label="1 bar", caret=True, ghost=True, size=11)
        btn(s, T, cx0 + 112, y + 6, 24, 20, ico="stop", ghost=True)
        btn(s, T, cx0 + 138, y + 6, 24, 20, ico="record", ghost=True)
        if plays == "clips":
            playing_tag(s, T, cx0 + 168, y + 7)
    if not perform:
        divider(s, T, tl_x, y + 6, y + h - 6)
        s.text(tl_x + 10, y + 21, "Timeline", 12, T.text0, UI, 600)
        icon(s, "chev-d", tl_x + 70, y + 17, T.text2, 11)
        badge(s, T, tl_x + 84, y + 9, "Stack", T.text2)
        btn(s, T, x + w - 124, y + 6, 80, 20, label="Snap: Bar", caret=True, ghost=True, size=11)
        if plays == "timeline":
            playing_tag(s, T, x + w - 200, y + 7)
    # ruler row: scene names over the shelf, bars over the timeline
    ry = y + hdr
    rh = 28
    s.rect(x + head_w + 1, ry, w - head_w - 2, rh, T.surface)
    for i in range(scenes):
        sx = cx0 + i * col_w
        nm, tempo = SCENES[i]
        icon(s, "scene", sx + 9, ry + 14, T.text1, 11)
        label = f"{i + 1} {nm}".strip()
        s.text(sx + 19, ry + 18, label, 10.5, T.text0 if i == 1 else T.text1, UI, 600)
        if tempo:
            s.text(sx + col_w - 8, ry + 18, tempo, 9, T.text2, MONO, 500, "end")
    if perform:
        s.text(cx0 + scenes * col_w + 6, ry + 18, "+ Scene", 10.5, T.text2, UI, 500)
    px_bar = tl_w / 20 if tl_w else 0
    if not perform:
        ruler_bars(s, T, tl_x + 1, ry, tl_w, rh, px_bar, 20, loop=(4, 12), playhead=9.5,
                   markers=[(4, "Verse"), (12, "Chorus")])
    s.line(x + 1, ry + rh - 0.5, x + head_w, ry + rh - 0.5, T.line1, 1)
    # rows
    yy = ry + rh
    rows_bottom = y + h - 26
    live = []   # slots that sound even though their section does not: launched over the timeline, recording
    for ti in range(len(TRACKS)):
        rh_t = row_h
        state = track_state(ti, plays)
        track_header(s, T, x, yy, head_w, rh_t, ti, selected=(ti == 2), state=state, armed=(ti == 5),
                     muted=(ti == 4 and not perform), solo=False, expanded=(ti == lane_track), hover=(ti == 3))
        # shelf
        for sc in range(scenes):
            sx = cx0 + sc * col_w
            info = SLOTS[ti].get(sc)
            if info:
                st = slot_state(ti, sc, plays)
                args = (sx + 2, yy + 5, col_w - 6, rh_t - 10, tcol(T, ti), info[0], st)
                kw = dict(seed=sh(info[0]), drum=TRACKS[ti][2] == "drum", big=perform)
                slot(s, T, *args, **kw)
                if st in ("playing", "recording"):
                    live.append((args, kw))
            else:
                slot(s, T, sx + 2, yy + 5, col_w - 6, rh_t - 10)
        # timeline lane
        if not perform:
            s.line(tl_x, yy + rh_t - 0.5, tl_x + tl_w, yy + rh_t - 0.5, T.line1, 1)
            for b in range(0, 21, 4):
                s.line(tl_x + b * px_bar, yy, tl_x + b * px_bar, yy + rh_t, T.line1, 1)
            launched = plays == "timeline" and ti in OVERRIDES
            for c in CLIPS[ti]:
                st, ln, nm, linked, loop = c[:5]
                muted = len(c) > 5
                clip_tile(s, T, tl_x + st * px_bar + 1, yy + 4, ln * px_bar - 2, rh_t - 8, tcol(T, ti), nm,
                          sh(nm) + st, drum=TRACKS[ti][2] == "drum", loop_px=loop * px_bar, linked=linked,
                          selected=(ti, st) == (2, 4), muted=muted, dimmed=launched)
            if launched:
                # the lane says why it is quiet, and how to bring it back
                bx = tl_x + 6
                s.rect(bx, yy + rh_t - 26, 128, 20, T.raised, r=10)
                icon(s, "loop", bx + 13, yy + rh_t - 16, T.accentText, 11)
                s.text(bx + 24, yy + rh_t - 12, "Back to timeline", 10, T.accentText, UI, 600)
        yy += rh_t
        if ti == lane_track and not perform:
            lh = 40
            s.rect(x, yy, head_w, lh, T.surface)
            s.rect(x + 22, yy + 4, 2, lh - 8, tcol(T, ti), op=0.6)
            s.text(x + 34, yy + 16, "Cutoff", 11, T.text0, UI, 600)
            s.text(x + 34, yy + 31, "AUTO 1  ·  ↗ VCF Freq", 9.5, T.text2, UI, 500)
            s.text(x + head_w - 12, yy + 16, "4.2 V", 10, T.text1, MONO, 500, "end")
            # curve
            pts = [(0, 0.2), (4, 0.75), (8, 0.35), (12, 0.85), (16, 0.5), (20, 0.6)]
            d = ""
            for i, (b, v) in enumerate(pts):
                px = tl_x + b * px_bar
                py = yy + lh - 5 - v * (lh - 10)
                d += ("M" if i == 0 else "L") + f"{px:.1f} {py:.1f} "
            col = tcol(T, ti)
            s.path(d + f"L{tl_x + tl_w} {yy + lh} L{tl_x} {yy + lh} Z", fill=col, op=0.12)
            s.path(d, stroke=col, sw=1.6)
            for b, v in pts[1:-1]:
                s.circle(tl_x + b * px_bar, yy + lh - 5 - v * (lh - 10), 3.2, T.canvas, stroke=col, sw=1.5)
            s.line(x, yy + lh - 0.5, x + w, yy + lh - 0.5, T.line1, 1)
            yy += lh
    s.text(x + 16, yy + 18, "+ Add track", 11, T.text2, UI, 500)
    # shelf edge grip: drag to widen into Perform
    gx = cx0 + scenes * col_w + 2
    if not perform:
        s.rect(gx, y + hdr + 34, 3, 26, T.line2, r=1.5)
    else:
        # folded timeline strip
        fx = x + w - 30
        s.rect(fx, y + 34, 24, h - 40, T.raised, r=R_CTRL)
        s.g(f"rotate(-90 {fx + 15} {y + h / 2})")
        s.text(fx + 15, y + h / 2 + 4, "Timeline  ◂  drag or double-click", 11, T.text1, UI, 600, "middle")
        s.g_end()
    if not perform:
        phx = tl_x + 1 + 9.5 * px_bar
        s.line(phx, ry + rh, phx, rows_bottom + 20, T.text0, 1.4, op=0.85)
    # the section that does not play steps back; what still sounds in it stays lit
    dim = 0.55 if T.dark else 0.6
    if plays == "timeline":
        s.rect(x + head_w + 1, ry, clips_x1 - x - head_w - 1, y + h - ry - 2, T.canvas, op=dim)
        for args, kw in live:
            slot(s, T, *args, **kw)
    elif not perform:
        s.rect(tl_x + 1, ry, x + w - tl_x - 3, y + h - ry - 2, T.canvas, op=dim)
    max_btn(s, T, x + w - 34, y + 4, maximized)
    return tl_x, px_bar


# ------------------------------------------------------------------------------------------------ notes pane

def notes_header(s, T, x, y, w, color, title, length="2 bars", drum=False, maximized=False):
    cy = y + 18
    btn(s, T, x + 8, cy - 11, 22, 22, ico="chev-l", ghost=True)
    btn(s, T, x + 30, cy - 11, 22, 22, ico="chev-r", ghost=True)
    # clip chip: name, length, loop, key menu live here
    cw = tw(title, 12, weight=600) + tw(length, 11) + 64
    s.rect(x + 58, cy - 12, cw, 24, T.raised, r=R_CTRL)
    if not T.dark:
        s.rect(x + 58.5, cy - 11.5, cw - 1, 23, "none", r=R_CTRL, stroke="#3A2F1E", sw=1, sop=0.13)
    s.rect(x + 66, cy - 6, 12, 12, color, r=3)
    s.text(x + 86, cy + 4.5, title, 12, T.text0, UI, 600)
    s.text(x + 92 + tw(title, 12, weight=600), cy + 4.5, length, 11, T.text2, MONO, 500)
    icon(s, "chev-d", x + 58 + cw - 12, cy, T.text2, 11)
    dx = x + 66 + cw
    btn(s, T, dx, cy - 12, 92, 24, ico="devices", label="Devices 2", size=11)
    divider(s, T, dx + 102, y + 8, y + 28)
    ox = dx + 112
    if drum:
        btn(s, T, ox, cy - 12, 70, 24, label="Kit", caret=True, size=11)
        sx = ox + 80
        s.rect(sx, cy - 12, 150, 24, T.well, r=R_CTRL)
        s.rect(sx + 2, cy - 10, 64, 20, T.raised, r=R_CTRL - 1)
        s.text(sx + 34, cy + 4, "Steps", 11, T.text0, UI, 600, "middle")
        s.text(sx + 108, cy + 4, "Piano roll", 11, T.text1, UI, 500, "middle")
        btn(s, T, sx + 162, cy - 12, 92, 24, label="Snap: 1/16", caret=True, ghost=True, size=11)
    else:
        btn(s, T, ox, cy - 12, 92, 24, label="Snap: 1/16", caret=True, ghost=True, size=11)
        btn(s, T, ox + 96, cy - 12, 92, 24, label="C minor", caret=True, size=11)
        kx = ox + 194
        kx += toggle_word(s, T, kx, cy - 12, "Snap", True) + 4
        kx += toggle_word(s, T, kx, cy - 12, "Fold", False) + 10
        btn(s, T, kx, cy - 12, 80, 24, ico="chord", label="Chord", caret=True, ghost=True, size=11)
    max_btn(s, T, x + w - 34, cy - 12, maximized)
    rx = x + w - 42
    btn(s, T, rx - 28, cy - 12, 28, 24, ico="speaker", active=True)
    btn(s, T, rx - 112, cy - 12, 80, 24, label="Notes", caret=True, ghost=True, size=11)
    btn(s, T, rx - 186, cy - 12, 70, 24, label="View", caret=True, ghost=True, size=11)


def keyboard_old(s, T, x, y, w, row_h, lo, hi, active=(), roots=()):
    for i, p in enumerate(range(hi, lo - 1, -1)):
        ry = y + i * row_h
        black = (p % 12) in (1, 3, 6, 8, 10)
        s.rect(x, ry, w, row_h, T.keyBlack if black else T.keyWhite)
        if black:
            s.rect(x + w * 0.55, ry, w * 0.45, row_h, T.keyWhite)
        if p in active:
            s.rect(x + 2, ry + 1, w * 0.5, row_h - 2, T.accent, r=2, op=0.9)
        s.line(x, ry + row_h - 0.5, x + w, ry + row_h - 0.5, "#000", 1, op=0.15 if T.dark else 0.12)
        if p % 12 == 0:
            s.text(x + w - 4, ry + row_h - 3, pname(p), 8.5, "#3A3F48", UI, 600, "end")
    s.line(x + w - 0.5, y, x + w - 0.5, y + (hi - lo + 1) * row_h, T.line2, 1)


BLACK = (1, 3, 6, 8, 10)


def keyboard(s, T, x, y, w, row_h, lo, hi, active=(), root=0, color=None, names=None):
    """A real piano, lined up with the note rows. Behind the black keys a white key is one row tall; in front of
    them it widens to meet its neighbours halfway (C, E, F, B are 1.5 rows, D, G, A are 2), like the real thing.
    Keys being played light up in the track colour. Root keys carry a small accent mark at the front."""
    H = (hi - lo + 1) * row_h
    top = lambda p: y + (hi - p) * row_h
    bk = 0.60                       # black key length, as a share of the keyboard width
    if T.dark:
        ivory, ivory2, edge = "#D3D6DB", "#B9BDC4", "#8D929B"
        ebony, ebony2, gloss = "#25272C", "#141519", "#4E535D"
    else:
        ivory, ivory2, edge = "#FFFFFF", "#ECE8E1", "#B8B2A8"
        ebony, ebony2, gloss = "#3A3E46", "#1F2227", "#6A707B"
    lit = color or T.accent
    names = names if names is not None else row_h >= 15
    s.clip_begin(x, y, w, H)
    s.rect(x, y, w, H, edge)
    # white keys, with their wide fronts
    for p in range(lo - 1, hi + 2):
        if p % 12 in BLACK:
            continue
        up = 0.5 if (p + 1) % 12 in BLACK else 0
        dn = 0.5 if (p - 1) % 12 in BLACK else 0
        y0 = top(p) - up * row_h + 0.5
        y1 = top(p) + row_h + dn * row_h - 0.5
        s.rect(x, y0, w - 1, y1 - y0, s.lingrad([(0, ivory2, 1), (0.18, ivory, 1), (1, ivory, 1)], 0, 0, 1, 0), r=1.5)
        s.rect(x + w - 4, y0, 3, y1 - y0, ivory2, r=1.2)          # the key's front lip
        if p in active:
            s.rect(x, y0, w - 1, y1 - y0, lit, r=1.5, op=0.55)
        if p % 12 == root % 12 and lo <= p <= hi:
            s.rect(x + w - 4, top(p) + 2, 3, row_h - 4, T.accent, r=1)
        if lo <= p <= hi and (p % 12 == 0 or (names and row_h >= 15)):
            is_c = p % 12 == 0
            s.text(x + w - 8, top(p) + row_h / 2 + 3.2, pname(p), 8.5 if is_c else 7.5,
                   "#2A2E35" if is_c or p in active else "#7A808A", UI, 600 if is_c else 500, "end")
    # black keys on top, with a soft shadow on the white keys
    for p in range(lo - 1, hi + 2):
        if p % 12 not in BLACK:
            continue
        ky, kh, kw = top(p) + 0.6, row_h - 1.2, w * bk
        s.rect(x, ky + 1.2, kw + 1.8, kh, "#000", r=2, op=0.28)
        s.rect(x, ky, kw, kh, s.lingrad([(0, ebony2, 1), (0.85, ebony, 1), (1, gloss, 1)], 0, 0, 1, 0), r=2)
        s.rect(x, ky, kw * 0.9, max(1.0, kh * 0.18), "#FFFFFF", op=0.07)
        if p in active:
            s.rect(x, ky, kw, kh, lit, r=2, op=0.85)
    s.g_end()
    # the keyboard casts a little shade onto the grid
    s.rect(x + w, y, 5, H, s.lingrad([(0, "#000", 0.22 if T.dark else 0.10), (1, "#000", 0)], 0, 0, 1, 0))
    s.line(x + w - 0.5, y, x + w - 0.5, y + H, T.line2, 1)


def keyboard_fold(s, T, x, y, w, row_h, pitches, active=(), root=0, color=None):
    """With Fold on, only the scale's rows remain, so a piano would lie. Each row becomes a labelled key: ivory for
    naturals, ebony for sharps and flats, the root marked."""
    lit = color or T.accent
    for i, p in enumerate(pitches):
        ry = y + i * row_h
        black = p % 12 in BLACK
        fill = ("#1A1B1F" if T.dark else "#3A3E46") if black else ("#D3D6DB" if T.dark else "#FFFFFF")
        s.rect(x, ry + 0.5, w - 1, row_h - 1, fill, r=2)
        if black:
            s.rect(x + w - 5, ry + 0.5, 4, row_h - 1, "#4E535D" if T.dark else "#6A707B", r=1.5)
        if p in active:
            s.rect(x, ry + 0.5, w - 1, row_h - 1, lit, r=2, op=0.7)
        if p % 12 == root % 12:
            s.rect(x + w - 4, ry + 2, 3, row_h - 4, T.accent, r=1)
        fg = "#E6E8EC" if black else "#2A2E35"
        s.text(x + w - 9, ry + row_h / 2 + 3.2, pname(p), 8.5, fg, UI, 600 if p % 12 == root % 12 else 500, "end")
    s.line(x + w - 0.5, y, x + w - 0.5, y + len(pitches) * row_h, T.line2, 1)


def piano_roll(s, T, x, y, w, h, color, lane_h=86, px_beat=60, lo=50, hi=72):
    """Ruler, keys, grid, notes and the lower lane, laid out from the height budget."""
    rh = 24
    kw = 56
    gx, gw = x + kw, w - kw
    # ruler
    s.rect(gx, y, gw, rh, T.surface)
    bars = 2
    beats = int(gw // px_beat) + 1
    s.rect(gx + 2, y + 3, 8 * px_beat - 4, 6, T.accent, r=3, op=0.30 if T.dark else 0.22)
    for b in range(beats):
        bx = gx + b * px_beat
        major = b % 4 == 0
        s.line(bx, y + (rh - 10 if major else rh - 5), bx, y + rh, T.line2, 1)
        if major:
            s.text(bx + 4, y + rh - 5, f"{b // 4 + 1}", 10, T.text1, MONO, 500)
        else:
            s.text(bx + 3, y + rh - 5, f"{b // 4 + 1}.{b % 4 + 1}", 8.5, T.text2, MONO, 400)
    # grid
    gy = y + rh
    gh = h - rh - lane_h
    rows = hi - lo + 1
    row_h = gh / rows
    s.rect(gx, gy, gw, gh, T.canvas)
    s.clip_begin(gx, gy, gw, gh)
    for i, p in enumerate(range(hi, lo - 1, -1)):
        ry = gy + i * row_h
        inkey = (p % 12) in MINOR
        if not inkey:
            s.rect(gx, ry, gw, row_h, "#000" if T.dark else "#3A2F1E", op=0.16 if T.dark else 0.045)
        if p % 12 == 0:
            s.rect(gx, ry, gw, row_h, T.accent, op=0.06)
            s.line(gx, ry + row_h - 0.5, gx + gw, ry + row_h - 0.5, T.accent, 1, op=0.35)
    for b in range(beats * 4):
        bx = gx + b * px_beat / 4
        c = T.line2 if b % 16 == 0 else (T.line1 if b % 4 == 0 else T.line0)
        s.line(bx, gy, bx, gy + gh, c, 1)
    # beyond loop end dimmed
    s.rect(gx + 8 * px_beat, gy, gw - 8 * px_beat, gh, T.ground, op=0.45 if T.dark else 0.35)
    notes = [(0, 3, 62, 0.9), (0, 3, 58, 0.7), (0, 3, 55, 0.7), (3, 1, 60, 0.6), (4, 2, 63, 0.82, True),
             (4, 2, 58, 0.82, True), (4, 2, 55, 0.82, True), (6, 1, 65, 0.5), (6, 2, 67, 0.75), (8.5, 1, 62, 0.6),
             (9.5, 0.5, 63, 0.9), (10, 2, 60, 0.7), (10, 2, 56, 0.66), (12, 3, 58, 0.8), (12, 3, 53, 0.6),
             (15, 1, 70, 0.7)]
    for n in notes:
        st, ln, p, v = n[:4]
        sel = len(n) > 4
        nx = gx + st * px_beat + 1
        ny = gy + (hi - p) * row_h + 1
        note_tile(s, T, nx, ny, ln * px_beat - 2, row_h - 2, color, vel=v, selected=sel, label=pname(p))
        if st + 8 < beats:  # loop repeat preview beyond the loop end
            note_tile(s, T, gx + (st + 8) * px_beat + 1, ny, ln * px_beat - 2, row_h - 2, color, ghost=True)
    # ghost notes from the bass track
    for st, ln, p in [(0, 4, 50), (4, 4, 51), (8, 2, 53), (10, 2, 51)]:
        s.rect(gx + st * px_beat + 1, gy + (hi - p) * row_h + 1, ln * px_beat - 2, row_h - 2, "none", r=R_NOTE,
               stroke=T.tracks[2], sw=1, sop=0.6)
    # playhead
    phx = gx + 5.5 * px_beat
    s.line(phx, gy, phx, gy + gh, T.text0, 1.4, op=0.85)
    s.g_end()
    s.rect(phx - 4, y + rh - 9, 8, 8, T.accent, r=2)
    # drag readout pill
    s.rect(gx + 4 * px_beat + 20, gy + (hi - 66) * row_h - 2, 108, 20, T.raised, r=10)
    if not T.dark:
        s.rect(gx + 4 * px_beat + 20.5, gy + (hi - 66) * row_h - 1.5, 107, 19, "none", r=10, stroke="#3A2F1E",
               sw=1, sop=0.15)
    s.text(gx + 4 * px_beat + 74, gy + (hi - 66) * row_h + 11.5, "Eb maj · vel 82%", 10, T.text0, MONO, 500,
           "middle")
    keyboard(s, T, x, gy, kw, row_h, lo, hi, active=(55, 58, 62), root=0, color=color)
    # lower lane: tabs while they fit
    ly = gy + gh
    s.rect(x, ly, w, lane_h, T.surface)
    s.line(x, ly + 0.5, x + w, ly + 0.5, T.line2, 1)
    if lane_h < 60:  # short pane: no tabs, the header's lane picker names the lane
        base = ly + lane_h - 4
        top = ly + 6
        for n in notes:
            st, ln, p, v = n[:4]
            sx = gx + st * px_beat + 1 + (p % 3)
            s.line(sx, base, sx, base - v * (base - top), color, 2)
            s.circle(sx, base - v * (base - top), 2.4, color)
        return
    tabs = [("Velocity", True), ("Release", False), ("Chance", False), ("Repeats", False), ("Micro", False), None,
            ("Pitch", False), ("Timbre", False), ("Pressure", False), None, ("Automation ▾", False)]
    tx = gx + 6
    for t in tabs:
        if t is None:
            divider(s, T, tx + 2, ly + 6, ly + 20)
            tx += 10
            continue
        label, on = t
        ww = tw(label, 10.5, weight=500) + 14
        if on:
            s.rect(tx, ly + 4, ww, 18, T.raised, r=R_CTRL - 1)
            s.rect(tx + 4, ly + 20, ww - 8, 2, T.accent, r=1)
        s.text(tx + ww / 2, ly + 16.5, label, 10.5, T.text0 if on else T.text2, UI, 500 if not on else 600, "middle")
        tx += ww + 2
    s.text(x + 8, ly + 32, "100%", 9, T.text2, MONO)
    s.text(x + 8, ly + lane_h - 6, "0", 9, T.text2, MONO)
    base = ly + lane_h - 6
    top = ly + 28
    for n in notes:
        st, ln, p, v = n[:4]
        sel = len(n) > 4
        sx = gx + st * px_beat + 1 + (p % 3)
        s.line(sx, base, sx, base - v * (base - top), color, 2, op=1 if sel or True else 0.5)
        s.circle(sx, base - v * (base - top), 3, color if not sel else T.canvas, stroke=T.accent if sel else None,
                 sw=1.6)


def field(s, T, x, y, w, label, value, frac=None, color=None, open_=False):
    h = 24
    s.rect(x, y, w, h, T.well, r=R_CTRL)
    if frac is not None:
        s.rect(x, y, w * frac, h, color or T.accent, r=R_CTRL, op=0.28 if T.dark else 0.20)
        s.rect(x + w * frac - 1.5, y + 3, 2, h - 6, color or T.accent, r=1)
    s.text(x + 9, y + 16, label, 11, T.text1, UI, 500)
    s.text(x + w - 9, y + 16, value, 11, T.text0, MONO, 500, "end")
    if open_:
        icon(s, "chev-d", x + w - 14 - tw(value, 11, mono=True) - 8, y + 12, T.text2, 10)


def inspector(s, T, x, y, w, h, title="3 notes", chord="Eb maj", drum=False):
    pane(s, T, x, y, w, h)
    ix, iw = x + 14, w - 28
    s.text(ix, y + 27, title, 15, T.text0, UI, 600)
    if chord:
        badge(s, T, ix + tw(title, 15, weight=600) + 10, y + 14, chord, T.accentText, 9.5)
    yy = y + 46
    if drum:
        rows = [("Start", "1.2.1 – 1.4.3", None), ("Lane", "Kick", None)]
    else:
        rows = [("Start", "2.1.1", None), ("Length", "1/4 · 2 beats", None), ("Pitch", "Eb3 – Bb3", None)]
    for lab, val, fr in rows:
        field(s, T, ix, yy, iw, lab, val, fr)
        yy += 30
    # velocity open with its chart
    field(s, T, ix, yy, iw, "Velocity", "82%", 0.82)
    yy += 28
    ch = 62
    s.rect(ix, yy, iw, ch, T.well, r=R_CTRL)
    rnd = random.Random(3)
    nb = 24
    bw = (iw - 16) / nb
    for i in range(nb):
        hgt = [0, 0, 0, 1, 1, 2, 3, 5, 7, 9, 12, 14, 13, 11, 8, 6, 4, 2, 1, 1, 0, 0, 0, 0][i]
        if hgt:
            s.rect(ix + 8 + i * bw, yy + ch - 8 - hgt * 3.2, bw - 2, hgt * 3.2, T.accent, r=1, op=0.75)
    mx = ix + 8 + 11.6 * bw
    s.line(mx, yy + 6, mx, yy + ch - 6, T.text0, 1.2, dash="2 2")
    yy += ch + 6
    pw = (iw - 8) / 3
    for i, lab in enumerate(["Mean 82%", "Spread", "Chaos"]):
        btn(s, T, ix + i * (pw + 4), yy, pw, 24, label=lab, size=10.5)
    yy += 32
    field(s, T, ix, yy, iw, "Release", "50%", 0.5)
    yy += 30
    field(s, T, ix, yy, iw, "Micro-timing", "0%", 0.5, color=T.text2)
    yy += 40
    caption(s, T, ix, yy, "Plays")
    s.text(ix + iw, yy, "differs each pass", 9.5, T.text2, UI, 400, "end")
    yy += 10
    half = (iw - 6) / 2
    btn(s, T, ix, yy, half, 26, label="Chance 75%", active=True, size=11)
    btn(s, T, ix + half + 6, yy, half, 26, label="Repeats", size=11)
    btn(s, T, ix, yy + 32, half, 26, label="Only when…", size=11)
    btn(s, T, ix + half + 6, yy + 32, half, 26, label="Every … passes", size=11)
    s.rect(ix, yy + 22, half * 0.75, 3, T.accent, r=1.5)
    s.text(ix, y + h - 14, "12 notes · C3–G4 · used 2×" if not drum else "71 steps · 12 lanes · used 2×", 10,
           T.text2, UI, 500)


# ------------------------------------------------------------------------------------------------ drum editor

LANES = [("Kick", 1), ("Snare", 2), ("Clap", 3), ("Closed hat", 4), ("Open hat", 5), ("Low tom", 6),
         ("High tom", 7), ("Rim", 8), ("Crash", 9), ("Ride", 10), ("Cowbell", 11), ("Shaker", 12)]


def drum_grid(s, T, x, y, w, h, color, steps=32):
    rh = 24
    lw = 222
    gx, gw = x + lw, w - lw
    lane_h = 96
    s.rect(gx, y, gw, rh, T.surface)
    sw = gw / steps
    for b in range(steps // 4):
        s.text(gx + b * 4 * sw + 4, y + rh - 6, f"{b // 4 + 1}.{b % 4 + 1}", 9.5, T.text1 if b % 4 == 0 else T.text2,
               MONO, 500)
    gy = y + rh + 4
    NL = len(LANES)
    row_h = min(48, (h - rh - lane_h - 34) / NL)
    lane_h = h - rh - 4 - NL * row_h - 34
    pattern = {
        0: {0: 1, 8: 0.9, 16: 1, 22: 0.6, 24: 0.9},
        1: {4: 0.9, 12: 0.95, 20: 0.9, 28: 1},
        2: {12: 0.7, 28: 0.8},
        3: {i: (0.9 if i % 2 == 0 else 0.55) for i in range(12)},
        4: {6: 0.7, 30: 0.8},
        5: {26: 0.6},
        6: {27: 0.7, 29: 0.6},
        7: {7: 0.5, 15: 0.5},
        8: {0: 0.9},
        9: {i: 0.6 for i in range(16, 32, 2)},
        10: {3: 0.5, 11: 0.5, 19: 0.5},
        11: {i: 0.4 + 0.2 * (i % 2) for i in range(0, 32, 1) if i % 4 != 0},
    }
    sel = {(0, 8), (0, 24)}
    for li, (name, out) in enumerate(LANES):
        if li == 8:
            s.line(x + 8, gy + li * row_h - 1, x + w, gy + li * row_h - 1, T.line2, 1, dash="3 3")
            s.text(x + w - 4, gy + li * row_h - 5, "Lanes 9–16 play on a second Drum Out (bank 9–16)", 9, T.text2, UI, 600, "end")
        ry = gy + li * row_h
        if li == 0:
            s.rect(x, ry, lw - 6, row_h, T.raised if T.dark else T.hover, r=R_CTRL)
        s.rect(x + 8, ry + 5, 3, row_h - 10, color, r=1.5)
        s.text(x + 18, ry + row_h / 2 + 4, name, 11.5, T.text0, UI, 600)
        # output chip: which Drum Out jack this lane plays on
        oc = f"→{out}"
        bank2 = out > 8
        s.rect(x + lw - 92, ry + row_h / 2 - 9, 28, 18, T.well, r=R_CTRL - 1)
        if bank2:
            s.rect(x + lw - 92.5, ry + row_h / 2 - 9.5, 29, 19, "none", r=R_CTRL - 1, stroke=T.text2, sw=1, dash="2 2")
        s.text(x + lw - 78, ry + row_h / 2 + 3.5, oc, 9.5, T.text1, MONO, 500, "middle")
        btn(s, T, x + lw - 60, ry + row_h / 2 - 9, 20, 18, label="M", size=9.5, weight=700,
            active=(li == 6), color=T.queue)
        btn(s, T, x + lw - 37, ry + row_h / 2 - 9, 20, 18, label="S", size=9.5, weight=700)
        own = 12 if li == 3 else steps
        for st in range(steps):
            sx = gx + st * sw + 1.5
            beat = (st // 4) % 2 == 0
            v = pattern[li].get(st % own if li == 3 else st)
            faded = li == 3 and st >= own
            s.rect(sx, ry + 2, sw - 3, row_h - 4, T.well, r=R_NOTE)
            if beat and not v:
                s.rect(sx, ry + 2, sw - 3, row_h - 4, T.text0, r=R_NOTE, op=0.03 if T.dark else 0.025)
            if v:
                hh = (row_h - 4) * (0.35 + 0.65 * v)
                s.rect(sx, ry + 2 + (row_h - 4 - hh), sw - 3, hh, color, r=R_NOTE, op=0.35 if faded else 1)
                if (li, st) in sel:
                    s.rect(sx - 1.5, ry + 0.5, sw, row_h - 1, "none", r=R_NOTE + 1, stroke=T.accent, sw=1.8)
                if li == 1 and st == 28:  # repeats drawn as subdivided cells
                    for k in (1, 2):
                        s.line(sx + k * (sw - 3) / 3, ry + 4, sx + k * (sw - 3) / 3, ry + row_h - 4, T.onTile, 1.2,
                               op=0.6)
                if li == 0 and st == 22:  # chance: a corner notch
                    s.path(f"M{sx + sw - 3} {ry + 2} l-6 0 l6 6 z", fill=T.onTile, op=0.7)
        if li == 3:
            s.rect(x + lw - 122, ry + row_h / 2 - 9, 26, 18, T.accent, r=R_CTRL - 1, op=0.18)
            s.text(x + lw - 109, ry + row_h / 2 + 3.5, "12", 9.5, T.accentText, MONO, 600, "middle")
        if li == 6:
            s.rect(gx, ry, gw, row_h, s.hatch(T.text2, 0.25))
    s.text(x + 18, gy + NL * row_h + 20, "+ Lane", 11, T.text2, UI, 500)
    # playhead column
    phx = gx + 13.5 * sw
    s.line(phx, gy - 2, phx, gy + NL * row_h, T.text0, 1.4, op=0.85)
    s.rect(phx - 4, y + rh - 9, 8, 8, T.accent, r=2)
    # velocity strip for the lane last clicked
    ly = y + h - lane_h
    s.rect(x, ly, w, lane_h, T.surface)
    s.line(x, ly + 0.5, x + w, ly + 0.5, T.line2, 1)
    s.text(x + 14, ly + 20, "Velocity", 11, T.text1, UI, 600)
    s.text(x + 14, ly + 35, "Kick", 10.5, T.text2, UI, 500)
    for st, v in pattern[0].items():
        sx = gx + st * sw + sw / 2
        top = ly + 14
        base = ly + lane_h - 10
        s.rect(sx - 3, base - v * (base - top), 6, v * (base - top), color, r=2,
               op=1)
        if (0, st) in sel:
            s.rect(sx - 4.5, base - v * (base - top) - 1.5, 9, v * (base - top) + 3, "none", r=3, stroke=T.accent,
                   sw=1.5)


# ------------------------------------------------------------------------------------------------ artboards

def frame(T):
    s = Svg(W, H, T.ground)
    return s


def mock_editor(T, path, plays="timeline"):
    s = frame(T)
    transport(s, T, GAP, GAP, W - 2 * GAP, plays=plays)
    sy = GAP + 44 + GAP
    song_h = 396
    song_pane(s, T, GAP, sy, W - 2 * GAP, song_h, plays=plays)
    ny = sy + song_h + GAP
    nh = H - ny - GAP
    insp_w = 284
    nw = W - 2 * GAP - insp_w - GAP
    pane(s, T, GAP, ny, nw, nh)
    color = tcol(T, 2)
    notes_header(s, T, GAP, ny, nw, color, "Keys · Verse A")
    s.clip_begin(GAP, ny + 36, nw, nh - 36, R_PANE)
    piano_roll(s, T, GAP, ny + 36, nw, nh - 36, color)
    s.g_end()
    inspector(s, T, GAP + nw + GAP, ny, insp_w, nh)
    s.save(path)


def mock_editor_clips(T, path):
    mock_editor(T, path, plays="clips")


def mock_perform(T, path):
    """Song pane maximised, clips widened by dragging the Clips | Timeline divider."""
    s = frame(T)
    transport(s, T, GAP, GAP, W - 2 * GAP, plays="clips")
    sy = GAP + 44 + GAP
    song_h = H - sy - GAP
    song_pane(s, T, GAP, sy, W - 2 * GAP, song_h, scenes=8, col_w=128, head_w=232, row_h=74, perform=True,
              lane_track=-1, plays="clips", maximized=True)
    s.save(path)


def mock_drums(T, path):
    """Notes pane maximised: the song pane is hidden until Restore."""
    s = frame(T)
    transport(s, T, GAP, GAP, W - 2 * GAP)
    ny = GAP + 44 + GAP
    nh = H - ny - GAP
    insp_w = 284
    nw = W - 2 * GAP - insp_w - GAP
    pane(s, T, GAP, ny, nw, nh)
    color = tcol(T, 0)
    notes_header(s, T, GAP, ny, nw, color, "Drums · Beat A", drum=True, maximized=True)
    drum_grid(s, T, GAP + 8, ny + 40, nw - 16, nh - 44, color)
    inspector(s, T, GAP + nw + GAP, ny, insp_w, nh, title="2 steps", chord="Kick", drum=True)
    s.save(path)


def roll_slice(s, T, x, y, w, row_h, pitches, kb, kw=56, color=None, active=(55, 58, 62)):
    """A narrow piece of the piano roll: keys plus a little grid, for close-ups."""
    color = color or tcol(T, 2)
    hi, lo = pitches[0], pitches[-1]
    H = len(pitches) * row_h
    pane(s, T, x - 8, y - 8, w + 16, H + 16)
    gx, gw = x + kw, w - kw
    s.rect(gx, y, gw, H, T.canvas)
    for i, p in enumerate(pitches):
        ry = y + i * row_h
        if p % 12 not in MINOR:
            s.rect(gx, ry, gw, row_h, "#000" if T.dark else "#3A2F1E", op=0.16 if T.dark else 0.045)
        if p % 12 == 0:
            s.line(gx, ry + row_h - 0.5, gx + gw, ry + row_h - 0.5, T.accent, 1, op=0.35)
    pb = 48
    s.clip_begin(gx, y, gw, H)
    for b in range(int(gw // (pb / 4)) + 1):
        bx = gx + b * pb / 4
        s.line(bx, y, bx, y + H, T.line1 if b % 4 == 0 else T.line0, 1)
    for st, ln, p in [(0, 3, 62), (0, 3, 58), (0, 3, 55), (3, 1, 60), (4, 2, 63), (4, 2, 58), (4, 2, 55),
                      (6, 1, 65), (6, 2, 67), (1.5, 1, 53), (5, 1, 51)]:
        if p in pitches:
            i = pitches.index(p)
            note_tile(s, T, gx + st * pb + 1, y + i * row_h + 1, ln * pb - 2, row_h - 2, color,
                      vel=0.8, label=pname(p) if row_h >= 12 else None)
    s.line(gx + 1.3 * pb, y, gx + 1.3 * pb, y + H, T.text0, 1.4, op=0.85)
    s.g_end()
    if kb == "old":
        keyboard_old(s, T, x, y, kw, row_h, lo, hi, active=active)
    elif kb == "fold":
        keyboard_fold(s, T, x, y, kw, row_h, pitches, active=active, root=0, color=color)
    else:
        keyboard(s, T, x, y, kw, row_h, lo, hi, active=active, root=0, color=color)


def mock_keyboard(T, path):
    s = Svg(1600, 1000, T.ground)
    s.text(44, 52, f"Piano roll keys · {T.name}", 24, T.text0, UI, 600)
    s.text(44, 78, "Shown at 2× so the detail is visible. Same row height as the editor; only the keys change.", 13,
           T.text2, UI, 400)
    k = 2.0
    cols = [
        ("Today", "Every row is a stripe; black keys are bars with a white tail. Reads as a barcode.", "old", 12,
         list(range(72, 47, -1))),
        ("Proposed", "Real piano shapes lined up with the rows: wide white fronts, raised black keys with a soft "
         "shadow, played keys lit in the track colour, root marked.", "new", 12, list(range(72, 47, -1))),
        ("Proposed, zoomed in", "From 15 px rows every white key is named, so you can read pitches without "
         "counting.", "new", 18, list(range(67, 50, -1))),
        ("Proposed, Fold on", "Only scale rows remain, so a piano shape would lie. Each row becomes a named key; "
         "sharps and flats stay dark.", "fold", 18, [p for p in range(72, 47, -1) if p % 12 in MINOR][:17]),
    ]
    for i, (title, sub, kb, rh, pitches) in enumerate(cols):
        X = 52 + i * 386
        s.g(f"translate({X},120) scale({k})")
        roll_slice(s, T, 4, 4, 170, rh, pitches, kb)
        s.g_end()
        ty = 120 + (len(pitches) * rh + 16) * k + 30
        s.text(X, ty, title, 15, T.text0, UI, 600)
        words, line, ly = sub.split(), "", ty + 24
        for wd in words:
            if tw(line + " " + wd, 12) > 330:
                s.text(X, ly, line.strip(), 12, T.text2, UI, 400)
                line, ly = "", ly + 18
            line += " " + wd
        s.text(X, ly, line.strip(), 12, T.text2, UI, 400)
    s.save(path)
