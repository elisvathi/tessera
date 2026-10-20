"""Module panel mockups: Tessera (default and widened), Track Out, Drum Out, Auto Out. Units are Rack px
(1 HP = 15 px, panel height 380 px); positions follow today's widgets so only the look changes."""
from svg import Svg, Theme, icon
import editor as E

UI, MONO, DISPLAY = E.UI, E.MONO, E.DISPLAY
MM = 380 / 128.5  # px per mm


def mm(v):
    return v * MM


RACK_BG = "#262626"


def rails(s, x, w, y0=0, h=380):
    for ry in (y0 - 14, y0 + h - 1):
        s.rect(x, ry, w, 15, "#8E9196")
        s.rect(x, ry + 1, w, 2, "#C9CCD0")
        for k in range(int(w // 15) + 1):
            s.circle(x + 7.5 + k * 15, ry + 9, 2.6, "#3E4146")


def screw(s, T, cx, cy):
    ring = "#5A5E66" if T.dark else "#B5B8BE"
    s.circle(cx, cy, 5.2, ring)
    s.circle(cx, cy, 4.2, "#2A2C31" if T.dark else "#D9DBDF")
    s.path(f"M{cx-2.6} {cy} H{cx+2.6} M{cx} {cy-2.6} V{cy+2.6}", stroke=ring, sw=1.2)


def jack(s, T, cx, cy, plate=False):
    # Rack's own themed PJ301M: dark nut on dark panels, light nut on light panels.
    nut = "#2B2E35" if T.dark else "#D5D7DB"
    rim = "#8C9099" if T.dark else "#F4F5F7"
    s.circle(cx, cy, 10.8, nut)
    s.circle(cx, cy, 10.8, "none", stroke="#000", sw=0.8)
    s.circle(cx, cy, 8.0, rim)
    s.circle(cx, cy, 6.2, "#111215")
    s.circle(cx, cy, 4.2, "#050506")


def label(s, x, y, text, color, size=7.5, weight=600, anchor="middle", ls=0.6):
    s.text(x, y, text, size, color, UI, weight, anchor, ls)


def logo(s, T, x, y, k=1.0):
    """The mark: a 2x2 of tesserae, one of them lapis."""
    t = 4.2 * k
    g = 1.4 * k
    cols = [T.text1, T.accent, T.text2, T.text1]
    for i, c in enumerate(cols):
        s.rect(x + (i % 2) * (t + g), y + (i // 2) * (t + g), t, t, c, r=1.1 * k)


def title(s, T, cx, text, size=10, ls=2.2, mark=True, y=26):
    w = len(text) * size * 0.86 + ls * (len(text) - 1)
    if mark:
        logo(s, T, cx - w / 2 - 15, y - 9.2, 1.0)
        s.text(cx + 6, y, text, size, T.text0, DISPLAY, 600, "middle", ls)
    else:
        s.text(cx, y, text, size, T.text0, DISPLAY, 600, "middle", ls)


def panel_base(s, T, x, w):
    s.rect(x, 0, w, 380, T.panel)
    s.rect(x + 0.5, 0.5, w - 1, 379, "none", stroke="#000" if T.dark else "#000", sw=1, sop=0.35 if T.dark else 0.12)


def plate(s, T, x, y, w, h):
    """Outputs sit on an inverted plate: limestone on the dark panel, ink on the light one."""
    s.rect(x, y, w, h, T.plate, r=6)


def tile_button(s, T, cx, cy, ico, on=None, size=22, lbl=None, lbl_color=None):
    x, y = cx - size / 2, cy - size / 2
    base = T.raised if T.dark else "#E2DFD8"
    s.rect(x, y + 1.2, size, size, "#000", r=5.5, op=0.35 if T.dark else 0.10)
    s.rect(x, y, size, size, base, r=5.5)
    if on:
        s.rect(x, y, size, size, on, r=5.5, op=0.22)
        s.rect(x + 0.6, y + 0.6, size - 1.2, size - 1.2, "none", r=5, stroke=on, sw=1.2, sop=0.9)
        if T.dark:
            s.circle(cx, cy, size * 0.9, s.radgrad([(0, on, 0.25), (1, on, 0)]))
    elif T.dark:
        s.line(x + 4, y + 0.6, x + size - 4, y + 0.6, "#fff", 1, op=0.08)
    if ico:
        icon(s, ico, cx, cy, on or T.text1, size * 0.62)
    if lbl:
        label(s, cx, cy + size / 2 + 10, lbl, lbl_color or T.text1)


def knob(s, T, cx, cy, r=17, frac=0.42, color=None):
    import math
    color = color or T.accent
    a0, a1 = math.radians(135), math.radians(405)
    av = a0 + (a1 - a0) * frac

    def arc(rad, aa, bb):
        x0, y0 = cx + rad * math.cos(aa), cy + rad * math.sin(aa)
        x1, y1 = cx + rad * math.cos(bb), cy + rad * math.sin(bb)
        large = 1 if (bb - aa) > math.pi else 0
        return f"M{x0:.2f} {y0:.2f} A{rad} {rad} 0 {large} 1 {x1:.2f} {y1:.2f}"
    s.path(arc(r + 4, a0, a1), stroke=T.line if T.dark else T.lineStrong, sw=2.5)
    s.path(arc(r + 4, a0, av), stroke=color, sw=2.5)
    s.circle(cx, cy + 1.5, r, "#000", op=0.35 if T.dark else 0.12)
    s.circle(cx, cy, r, T.raised if T.dark else "#E2DFD8")
    if T.dark:
        s.circle(cx, cy, r, s.lingrad([(0, "#FFFFFF", 0.08), (1, "#000000", 0.0)]))
    s.line(cx + (r * 0.25) * math.cos(av), cy + (r * 0.25) * math.sin(av), cx + (r - 3) * math.cos(av),
           cy + (r - 3) * math.sin(av), T.text0, 2.2, cap="round")


def mosaic_overview(s, T, x, y, w, h, cols=24, now=9, demo=True):
    """Tracks x bars, one tessera per bar: lit where a clip plays. The panel's signature."""
    rows = len(E.TRACKS)
    g = 1.6
    cw = (w - g * (cols - 1)) / cols
    ch = (h - g * (rows - 1)) / rows
    for ti in range(rows):
        color = E.tcol(T, ti)
        bars = set()
        muted = set()
        for c in E.CLIPS[ti]:
            for b in range(c[0], c[0] + c[1]):
                bars.add(b)
                if len(c) > 4:
                    muted.add(b)
        for b in range(cols):
            cx = x + b * (cw + g)
            cy = y + ti * (ch + g)
            if b in bars and b not in muted:
                op = 1.0 if b == now else (0.85 if b < now else 0.45)
                s.rect(cx, cy, cw, ch, color, r=1.2, op=op)
            elif b in muted:
                s.rect(cx, cy, cw, ch, color, r=1.2, op=0.15)
            else:
                s.rect(cx, cy, cw, ch, T.line if T.dark else T.lineStrong, r=1.2, op=0.55 if T.dark else 0.35)
    nx = x + now * (cw + g)
    s.rect(nx - 1.2, y - 2.5, cw + 2.4, h + 5, "none", r=2, stroke=T.text0, sw=1.1)


def tessera_panel(s, T, x, hp=24, wide_editor=False):
    w = hp * 15
    panel_base(s, T, x, 360 if not wide_editor else w)
    title(s, T, x + 180, "TESSERA", 10.5)
    for sx, sy in ((x + 15 + 7.5, 7.5), (x + 360 - 30 + 7.5, 7.5), (x + 15 + 7.5, 372.5), (x + 360 - 30 + 7.5, 372.5)):
        if wide_editor and sx > x + 300:
            sx = x + w - 22.5
        screw(s, T, sx, sy)
    # display well
    dx, dy, dw, dh = x + mm(6), mm(13), mm(109.92), mm(34)
    s.rect(dx, dy, dw, dh, T.well, r=7)
    s.rect(dx + 0.5, dy + 0.5, dw - 1, dh - 1, "none", r=7, stroke="#000", sw=1, sop=0.5 if T.dark else 0.08)
    s.text(dx + 10, dy + 15, "Main", 10, T.text0, UI, 600)
    # state pills: play mode and transport, each with a glyph so state is never colour alone
    px_ = dx + dw - 10
    s.rect(px_ - 62, dy + 5, 62, 14, T.play, r=7, op=0.18 if T.dark else 0.14)
    icon(s, "play", px_ - 53, dy + 12, T.play, 9)
    s.text(px_ - 46, dy + 15.4, "PLAYING", 7.5, T.play, UI, 700, "start", 0.5)
    s.rect(px_ - 132, dy + 5, 66, 14, T.accent, r=7, op=0.20 if T.dark else 0.12)
    s.text(px_ - 99, dy + 15.4, "ARRANGER", 7.5, T.accentText, UI, 700, "middle", 0.5)
    mosaic_overview(s, T, dx + 10, dy + 26, dw - 20, 40)
    # readouts
    ry = dy + dh - 12
    s.text(dx + 10, ry + 2, "9.3.1", 17, T.text0, MONO, 500)
    s.text(dx + 66, ry + 1, "BAR", 6.5, T.text2, UI, 700, ls=0.6)
    s.text(dx + 132, ry + 2, "Verse", 12, T.text0, UI, 600, "middle")
    s.text(dx + 132 - 26, ry - 10, "", 6, T.text2)
    s.text(dx + dw - 36, ry + 2, "120.0", 17, T.text0, MONO, 500, "end")
    s.text(dx + dw - 10, ry + 1, "BPM", 6.5, T.text2, UI, 700, "end", 0.6)
    # transport row
    row = mm(57)
    cols = [mm(c) + x for c in (12, 25, 38, 51, 64)]
    tile_button(s, T, cols[0], row, "play", T.play, lbl="PLAY")
    tile_button(s, T, cols[1], row, "record", None, lbl="REC")
    tile_button(s, T, cols[2], row, "stop", None, lbl="RESET")
    tile_button(s, T, cols[3], row, "loop", T.accent, lbl="LOOP")
    tile_button(s, T, cols[4], row, "metronome", None, lbl="TAP")
    # editor button: the one primary action on the panel
    ex, ey = x + mm(77), row
    s.rect(ex - 24, ey - 11, 48, 22, T.accent, r=6)
    s.text(ex, ey + 4, "EDITOR", 8, T.onAccent, UI, 700, "middle", 0.8)
    knob(s, T, x + mm(100), mm(58), 16, 0.45)
    label(s, x + mm(100), mm(58) + 31, "TEMPO", T.text1)
    # scene row
    srow = mm(72)
    tile_button(s, T, cols[0], srow, "chev-l", None, 19, "PREV")
    tile_button(s, T, cols[1], srow, "chev-r", None, 19, "NEXT")
    tile_button(s, T, cols[2], srow, "scene", T.queue, 19, "LAUNCH")
    tile_button(s, T, x + mm(54), srow, "stop", None, 19, "STOP ALL")
    tile_button(s, T, x + mm(77), srow, "launcher", None, 19, "ARRANGE")
    tile_button(s, T, x + mm(93), srow, None, None, 19, "FILL")
    s.text(x + mm(93), srow + 3.5, "F", 9, T.text1, DISPLAY, 600, "middle")
    jack(s, T, x + mm(106), mm(73))
    # inputs
    jr1, jr2 = mm(95), mm(112)
    for c, l in zip((6.5, 17.5, 28.5, 40, 53), ("CLOCK", "RESET", "RUN", "SCENE", "LAUNCH")):
        jack(s, T, x + mm(c), jr1)
        label(s, x + mm(c), jr1 - mm(6.4) + 2, l, T.text1)
    # record inputs: a quiet well, labelled once
    rx, ry_, rw, rh = x + mm(2), mm(101), mm(55), mm(19)
    s.rect(rx, ry_, rw, rh + 4, T.well, r=6)
    for c, l in zip((8, 21.5, 35, 48.5), ("V/OCT", "GATE", "VEL", "AUTO")):
        jack(s, T, x + mm(c), jr2 + 3)
        label(s, x + mm(c), jr2 + 3 - mm(6.4) + 2, l, T.text1)
    # outputs on the inverted plate
    plate(s, T, x + mm(62), mm(84), mm(54), mm(38) + 2)
    for c, l in zip((71, 88, 105), ("CLOCK", "RESET", "RUN")):
        jack(s, T, x + mm(c), jr1, True)
        label(s, x + mm(c), jr1 - mm(6.4) + 2, l, T.plateText)
    for c, l in zip((71, 88, 105), ("EOC", "BAR", "CLICK")):
        jack(s, T, x + mm(c), jr2, True)
        label(s, x + mm(c), jr2 - mm(6.4) + 2, l, T.plateText)


def out_display(s, T, x, y, w, h, color, num, name, source="Main"):
    s.rect(x, y, w, h, T.well, r=6)
    s.rect(x + 0.5, y + 0.5, w - 1, h - 1, "none", r=6, stroke="#000", sw=1, sop=0.5 if T.dark else 0.08)
    s.text(x + w / 2, y + 13, source, 8, T.text1, UI, 500, "middle")
    ts = min(34, h - 40)
    tx = x + w / 2 - ts / 2
    ty = y + 19
    s.rect(tx, ty, ts, ts, color, r=6)
    if T.dark:
        s.line(tx + 4, ty + 0.75, tx + ts - 4, ty + 0.75, "#fff", 1, op=0.4)
    s.text(x + w / 2, ty + ts / 2 + 6.5, str(num), 17, T.onTrack, DISPLAY, 600, "middle")
    s.text(x + w / 2, y + h - 8, name, 9, T.text0, UI, 600, "middle")


def step_buttons(s, T, cx1, cx2, cy, w=28):
    for cx, ic in ((cx1, "chev-l"), (cx2, "chev-r")):
        s.rect(cx - w / 2, cy - 8.5 + 1, w, 17, "#000", r=5, op=0.3 if T.dark else 0.08)
        s.rect(cx - w / 2, cy - 8.5, w, 17, T.raised if T.dark else "#E2DFD8", r=5)
        icon(s, ic, cx, cy, T.text1, 11)


def small_title(s, T, cx, text):
    s.text(cx, 26, text, 7.6, T.text0, DISPLAY, 600, "middle", 1.2)


def track_out(s, T, x, ti=2):
    w = 90
    panel_base(s, T, x, w)
    small_title(s, T, x + w / 2, "TRACK OUT")
    screw(s, T, x + 22.5, 7.5)
    screw(s, T, x + w - 22.5, 372.5)
    color = E.tcol(T, ti)
    out_display(s, T, x + mm(3), mm(12.5), mm(24.48), mm(30), color, ti + 1, E.TRACKS[ti][0])
    step_buttons(s, T, x + mm(9), x + mm(21.48), mm(48.5))
    plate(s, T, x + mm(2.5), mm(56), mm(25.48), mm(65))
    rows = [mm(r) for r in (66, 82, 98, 114)]
    for (c, r, l) in ((9, 0, "V/OCT"), (21.48, 0, "GATE"), (9, 1, "VEL"), (21.48, 1, "REL"), (9, 2, "TIMBRE"),
                      (21.48, 2, "PRESS"), (15.24, 3, "AUTO")):
        jack(s, T, x + mm(c), rows[r], True)
        label(s, x + mm(c), rows[r] - mm(6.4) + 2, l, T.plateText, 7)
    # gate activity: a tiny tessera beside GATE lights in the track colour
    s.rect(x + mm(21.48) + 13, rows[0] - mm(6.4) - 4, 4, 4, color, r=1)


def drum_out(s, T, x, ti=0):
    w = 150
    panel_base(s, T, x, w)
    small_title(s, T, x + w / 2, "DRUM OUT")
    for sx, sy in ((x + 22.5, 7.5), (x + w - 22.5, 7.5), (x + 22.5, 372.5), (x + w - 22.5, 372.5)):
        screw(s, T, sx, sy)
    color = E.tcol(T, ti)
    # wider display: the number tile on the left, names on the right
    dx, dy, dw, dh = x + mm(3), mm(12.5), mm(44.8), mm(24)
    s.rect(dx, dy, dw, dh, T.well, r=6)
    s.rect(dx + 8, dy + 12, 40, 40, color, r=7)
    s.text(dx + 28, dy + 39, str(ti + 1), 19, T.onTrack, DISPLAY, 600, "middle")
    s.text(dx + 56, dy + 24, "Main", 8, T.text1, UI, 500)
    s.text(dx + 56, dy + 40, "Drums", 11, T.text0, UI, 600)
    s.text(dx + 56, dy + 53, "8 lanes", 8, T.text2, UI, 500)
    step_buttons(s, T, x + mm(12), x + mm(38.8), mm(41.5), 34)
    plate(s, T, x + mm(2.5), mm(47), mm(45.8), mm(75))
    colsx = [mm(c) for c in (8.6, 18.4, 32.4, 42.2)]
    rowsy = [mm(r) for r in (57, 70, 83, 96)]
    hits = {0, 3, 5}
    for i in range(8):
        r = rowsy[i // 2]
        tcx = x + colsx[(i % 2) * 2]
        vcx = x + colsx[(i % 2) * 2 + 1]
        jack(s, T, tcx, r, True)
        jack(s, T, vcx, r, True)
        # one label per lane pair: number tessera (lit on a hit) + lane name, so "which jack is the kick" is obvious
        ly = r - mm(6.4) + 1
        lx = tcx - 9
        s.rect(lx, ly - 7, 9, 9, color if i in hits else ("#C9C4BA" if T.dark else "#3A3D46"), r=2)
        s.text(lx + 4.5, ly, str(i + 1), 6.5, T.onTrack if i in hits else T.plateText if not T.dark else "#3A3D46",
               UI, 700, "middle")
        s.text(lx + 12, ly, E.LANES[i].upper().replace("CLOSED HAT", "C.HAT").replace("OPEN HAT", "O.HAT")
               .replace("LOW TOM", "TOM L").replace("HIGH TOM", "TOM H"), 6.6, T.plateText, UI, 700, "start", 0.3)
    pr = mm(112)
    for c, l in ((colsx[0] + mm(2), "TRIGS"), ((colsx[1] + colsx[2]) / 2, "VELS"), (colsx[3] - mm(2), "AUTO")):
        jack(s, T, x + c, pr, True)
        label(s, x + c, pr - mm(6.4) + 2, l, T.plateText, 7)


def auto_out(s, T, x, ti=2):
    w = 120
    panel_base(s, T, x, w)
    small_title(s, T, x + w / 2, "AUTO OUT")
    for sx, sy in ((x + 22.5, 7.5), (x + w - 22.5, 372.5)):
        screw(s, T, sx, sy)
    color = E.tcol(T, ti)
    dx, dy, dw, dh = x + mm(3), mm(12.5), mm(34.64), mm(26)
    s.rect(dx, dy, dw, dh, T.well, r=6)
    s.rect(dx + 8, dy + 12, 34, 34, color, r=6)
    s.text(dx + 25, dy + 35, str(ti + 1), 17, T.onTrack, DISPLAY, 600, "middle")
    s.text(dx + 50, dy + 24, "Main", 8, T.text1, UI, 500)
    s.text(dx + 50, dy + 39, "Keys", 11, T.text0, UI, 600)
    s.text(dx + 50, dy + 52, "3 lanes", 8, T.text2, UI, 500)
    step_buttons(s, T, x + mm(12), x + mm(28.64), mm(43), 32)
    plate(s, T, x + mm(2.5), mm(48), mm(35.64), mm(74))
    lanes = [("Cutoff", 0.62), ("Reso", 0.3), ("Drive", 0.8), None, None, None, None, None]
    for i in range(8):
        cy = mm(54.5 + i * 8.7)
        jack(s, T, x + mm(32.6), cy, True)
        lx = x + mm(4.5)
        ink = T.plateText
        if lanes[i]:
            n, v = lanes[i]
            s.text(lx, cy - 1, f"{i+1}", 7, ink, MONO, 600)
            s.text(lx + 9, cy - 1, n, 8, ink, UI, 600)
            bw = mm(21)
            s.rect(lx, cy + 3, bw, 3, ink, r=1.5, op=0.18)
            s.rect(lx, cy + 3, bw * v, 3, color, r=1.5)
        else:
            s.text(lx, cy + 2.5, f"{i+1}", 7, ink, MONO, 600, op=0.45)
            s.line(lx + 10, cy, lx + mm(21), cy, ink, 1, op=0.25)


def mock_panels(T, path):
    W, H = 820, 470
    s = Svg(W, H, RACK_BG)
    oy = 52
    x0 = 40
    s.g(f"translate(0,{oy})")
    rails(s, 0, W)
    tessera_panel(s, T, x0)
    track_out(s, T, x0 + 360)
    drum_out(s, T, x0 + 450)
    auto_out(s, T, x0 + 600)
    s.g_end()
    s.text(x0, 30, f"Tessera family  ·  {T.name} panels  ·  24 + 6 + 10 + 8 HP, as the module browser shows them",
           12, "#C8CAD0", UI, 500)
    s.save(path)


# ------------------------------------------------------------------------------------------------ widened

def panel_editor(s, T, x, y, w, h):
    # the strip on top
    s.rect(x, y, w, h, T.grout, r=7)
    sx = x + 3
    E.segmented(s, T, sx + 2, y + 4, ["Launcher", "Arranger", "Both", "Piano roll"], 2, h=22, wpad=10)
    E.btn(s, T, x + w - 108, y + 4, 104, 22, "inspector", "Full editor")
    top = y + 30
    arr_h = 132
    # compact arranger: 4 rows of 25 px, narrow headers
    tw = w - 6
    E.tile(s, T, sx, top, tw, arr_h, r=6)
    s.clip_begin(sx, top, tw, arr_h, 6)
    head_w = 124
    rr = 22
    tx = sx + head_w
    px_bar = (tw - head_w) / 16
    E.ruler(s, T, tx, top, tw - head_w, rr, px_bar, 16, loop=(4, 12), playhead=9.5)
    tools = ["pointer", "pencil", "eraser", "scissors"]
    for i, ic in enumerate(tools):
        E.btn(s, T, sx + 4 + i * 23, top + 2, 21, 18, ic, active=(i == 0))
    s.rect(tx, top + rr, tw - head_w, arr_h - rr, T.canvas)
    for b in range(17):
        s.line(tx + b * px_bar, top + rr, tx + b * px_bar, top + arr_h, T.line, 1)
    row_h = 27
    for ti in range(4):
        ry = top + rr + ti * row_h
        color = E.tcol(T, ti)
        if ti == 2:
            s.rect(sx + 3, ry + 2, head_w - 6, row_h - 4, T.raised, r=4)
        E.chip(s, T, sx + 8, ry + 6, color, ti + 1, 15)
        s.text(sx + 29, ry + 17.5, E.TRACKS[ti][0], 11, T.text0, UI, 600)
        for k, bx in (("M", head_w - 44), ("S", head_w - 22)):
            s.rect(sx + bx, ry + 5, 18, 17, T.raised if ti != 2 else T.hover, r=4)
            s.text(sx + bx + 9, ry + 17, k, 9.5, T.text1, UI, 700, "middle")
        for c in E.CLIPS[ti]:
            st, ln, name, linked = c[:4]
            if st >= 16:
                continue
            ln = min(ln, 16 - st)
            E.arr_clip(s, T, tx + st * px_bar + 1, ry + 3, ln * px_bar - 2, row_h - 6, color, name, linked,
                       (name, st) == ("Verse A", 4) and ti == 2, len(c) > 4, ti == 0, seed=E.sh(name) % 97)
    phx = tx + 9.5 * px_bar
    s.line(phx, top + rr, phx, top + arr_h, T.text0, 1.2)
    s.line(tx, top, tx, top + arr_h, T.line, 1)
    s.g_end()
    # compact piano roll
    py = top + arr_h + 4
    ph = y + h - py
    E.tile(s, T, sx, py, tw, ph, r=6)
    E.piano_roll(s, T, sx, py, tw, ph, E.tcol(T, 2), px_beat=(tw - 56) / 10, lo=50, hi=68, playhead_beat=5.0,
                 tabs=False, header=False, compact=True)
    # splitter grip in the grout gap
    s.rect(x + w / 2 - 14, top + arr_h + 1, 28, 2, T.text2, r=1, op=0.6)


def mock_widened(T, path):
    hp = 72
    W = hp * 15 + 80
    H = 470
    s = Svg(W, H, RACK_BG)
    oy = 52
    x0 = 40
    s.g(f"translate(0,{oy})")
    rails(s, 0, W)
    tessera_panel(s, T, x0, hp, wide_editor=True)
    ex = x0 + 360 + 3
    panel_editor(s, T, ex, 16, hp * 15 - 360 - 3 - 10, 380 - 32)
    # resize handle on the right edge
    s.rect(x0 + hp * 15 - 7, 160, 3, 60, T.text2, r=1.5, op=0.6)
    s.g_end()
    s.text(x0, 30, f"Tessera widened to {hp} HP  ·  in-panel editor, Both view  ·  {T.name}", 12, "#C8CAD0", UI, 500)
    s.save(path)
