"""The design-system sheet (tokens, track colours, type, icons, controls, states) and the three-directions board."""
import colors as C
from svg import Svg, Theme, icon, ICON_NAMES
import editor as E
import panels as P

UI, MONO, DISPLAY = E.UI, E.MONO, E.DISPLAY


def swatch(s, T, x, y, name, hexv, note, w=150):
    s.rect(x, y, w, 46, hexv, r=6)
    if not T.dark or C.lum(hexv) < 0.25:
        s.rect(x + 0.5, y + 0.5, w - 1, 45, "none", r=6, stroke=T.text2, sw=1, sop=0.35)
    s.text(x, y + 62, name, 11, T.text0, UI, 600)
    s.text(x + w, y + 62, hexv, 10.5, T.text1, MONO, 500, "end")
    s.text(x, y + 76, note, 10, T.text2, UI, 450)


def section(s, T, x, y, title, sub=""):
    s.text(x, y, title, 15, T.text0, DISPLAY, 600, ls=0.5)
    if sub:
        s.text(x, y + 18, sub, 11, T.text1, UI, 450)


def mock_system(T, path):
    W, H = 1440, 1180
    s = Svg(W, H, T.grout)
    E.tile(s, T, 16, 16, W - 32, H - 32, r=12)
    x0 = 48
    P.logo(s, T, x0, 50, 2.4)
    s.text(x0 + 34, 70, "TESSERA  ·  MOSAIC", 20, T.text0, DISPLAY, 600, ls=2)
    s.text(x0 + 34, 92, f"Design system sheet  ·  {T.name} theme", 12, T.text1, UI, 500)

    y = 136
    section(s, T, x0, y, "Surfaces", "Tiles sit on grout. Depth comes from tone, not shadows.")
    names = [("grout", "app background, gaps"), ("canvas", "grids, timelines"), ("surface", "pane tiles, panels"),
             ("raised", "controls at rest"), ("hover", "control hover"), ("pressed", "control pressed"),
             ("well", "readouts, inset fields"), ("overlay", "menus, popovers")]
    for i, (n, note) in enumerate(names):
        swatch(s, T, x0 + i * 168, y + 34, n, getattr(T, n), note)
    y += 140
    section(s, T, x0, y, "Lines and text")
    names = [("lineFaint", "sub-grid"), ("line", "beats, dividers"), ("lineStrong", "bars, borders"),
             ("text0", "primary text"), ("text1", "secondary text"), ("text2", "hints (AA)")]
    for i, (n, note) in enumerate(names):
        swatch(s, T, x0 + i * 168, y + 22, n, getattr(T, n), note)
    y += 128
    section(s, T, x0, y, "Accent and states", "One accent: lapis. It means focus and selection, nothing else.")
    names = [("accent", "focus, selection, primary"), ("play", "playing"), ("record", "recording, armed"),
             ("queue", "queued, solo"), ("plate", "output plate"), ("plateText", "labels on plate")]
    for i, (n, note) in enumerate(names):
        swatch(s, T, x0 + i * 168, y + 34, n, getattr(T, n), note)
    y += 140
    section(s, T, x0, y, "Track colours", "16 hues at even lightness (OKLCH). Same hue in both themes, lightness per theme.")
    for i in range(16):
        tx = x0 + i * 84
        s.rect(tx, y + 32, 76, 40, T.tracks[i], r=6)
        if T.dark:
            s.line(tx + 5, y + 32.75, tx + 71, y + 32.75, "#fff", 1, op=0.35)
        s.text(tx + 8, y + 57, str(i + 1), 12, T.onTrack, DISPLAY, 600)
        s.text(tx, y + 86, T.tracks[i], 9.5, T.text1, MONO, 500)
    y += 118
    # type
    section(s, T, x0, y, "Type")
    ty = y + 36
    s.text(x0, ty + 8, "TESSERA", 22, T.text0, DISPLAY, 600, ls=3)
    s.text(x0 + 220, ty + 8, "Display · Unbounded SemiBold · titles, big numbers only", 11, T.text1, UI, 500)
    s.text(x0, ty + 44, "Track headers and pane titles", 13, T.text0, UI, 600)
    s.text(x0 + 220, ty + 44, "Inter SemiBold 13 · titles", 11, T.text1, UI, 500)
    s.text(x0, ty + 70, "Buttons, labels and menus", 11.5, T.text0, UI, 500)
    s.text(x0 + 220, ty + 70, "Inter Medium 11.5 · body", 11, T.text1, UI, 500)
    s.text(x0, ty + 94, "Secondary details, hints", 10.5, T.text1, UI, 450)
    s.text(x0 + 220, ty + 94, "Inter 10.5 · secondary", 11, T.text1, UI, 500)
    s.text(x0, ty + 124, "9.3.1   120.00   82%", 19, T.text0, MONO, 500)
    s.text(x0 + 220, ty + 124, "JetBrains Mono · every number that changes", 11, T.text1, UI, 500)
    s.text(x0, ty + 150, "V/OCT   GATE   CLOCK", 7.5, T.text1, UI, 600, ls=0.6)
    s.text(x0 + 220, ty + 150, "Inter SemiBold 7.5 caps · panel labels", 11, T.text1, UI, 500)
    # icons
    ix0 = x0 + 560
    section(s, T, ix0, y, "Icons", "16 px grid, 1.5 px line, round ends. Solid only for transport.")
    for i, n in enumerate(ICON_NAMES):
        cx = ix0 + 18 + (i % 14) * 56
        cy = y + 56 + (i // 14) * 52
        s.rect(cx - 16, cy - 16, 32, 32, T.raised, r=6)
        icon(s, n, cx, cy, T.text0, 18)
        s.text(cx, cy + 28, n, 8.5, T.text2, UI, 500, "middle")
    y += 214
    section(s, T, x0, y, "Controls and states")
    cy = y + 34
    states = [("Rest", {}), ("Hover", {"hover": True}), ("On", {"active": True}), ("Primary", {"primary": True})]
    cx = x0
    for lbl, kw in states:
        E.btn(s, T, cx, cy, 104, 26, "loop", "Loop", **kw)
        s.text(cx, cy + 42, lbl, 10, T.text2, UI, 500)
        cx += 116
    s.rect(cx, cy, 104, 26, "none", r=E.R_CTRL)
    icon(s, "loop", cx + 14, cy + 13, T.text2, 14)
    s.text(cx + 26, cy + 17, "Loop", 11.5, T.text2, UI, 500, op=0.6)
    s.text(cx, cy + 42, "Disabled", 10, T.text2, UI, 500)
    cx += 116
    s.rect(cx - 3, cy - 3, 110, 32, "none", r=E.R_CTRL + 3, stroke=T.accent, sw=2)
    E.btn(s, T, cx, cy, 104, 26, "loop", "Loop", hover=True)
    s.text(cx, cy + 42, "Keyboard focus", 10, T.text2, UI, 500)
    cx += 128
    E.segmented(s, T, cx, cy, ["Steps", "Grid"], 0, h=26)
    s.text(cx, cy + 42, "Segmented", 10, T.text2, UI, 500)
    cx += 150
    E.field(s, T, cx, cy, 250, "Velocity", "82%", 0.82)
    s.text(cx, cy + 42, "Value field", 10, T.text2, UI, 500)
    # state row
    sy = cy + 66
    col = E.tcol(T, 2)
    items = [("Playing", "playing"), ("Queued", "queued"), ("Recording", "recording"), ("Stopped", None)]
    sx = x0
    for lbl, st in items:
        E.launcher_slot(s, T, sx, sy, 120, 40, col, "Verse A", st, seed=3)
        s.text(sx, sy + 58, lbl, 10, T.text2, UI, 500)
        sx += 136
    E.arr_clip(s, T, sx, sy, 130, 40, col, "Selected", selected=True, seed=4)
    s.text(sx, sy + 58, "Selected clip", 10, T.text2, UI, 500)
    sx += 150
    E.arr_clip(s, T, sx, sy, 130, 40, col, "Muted", muted=True, seed=5)
    s.text(sx, sy + 58, "Muted clip", 10, T.text2, UI, 500)
    sx += 150
    s.rect(sx, sy, 150, 40, T.canvas, r=6)
    E.note_tile(s, T, sx + 10, sy + 8, 60, 11, col, 0.9, label="C4")
    E.note_tile(s, T, sx + 80, sy + 8, 60, 11, col, 0.9, selected=True, label="Eb4")
    E.note_tile(s, T, sx + 10, sy + 24, 60, 11, col, 0.4)
    E.note_tile(s, T, sx + 80, sy + 24, 60, 11, col, 0.9, muted=True)
    s.text(sx, sy + 58, "Notes: loud, selected, soft, muted", 10, T.text2, UI, 500)
    sx += 170
    P.tile_button(s, T, sx + 14, sy + 16, "play", T.play)
    P.tile_button(s, T, sx + 50, sy + 16, "record", None)
    P.knob(s, T, sx + 100, sy + 18, 14, 0.45)
    s.text(sx, sy + 58, "Panel controls", 10, T.text2, UI, 500)
    s.save(path)


# ------------------------------------------------------------------------------------------------ directions

DIRS = [
    dict(key="A", name="Mosaic", tag="Recommended",
         line="Tinted-ink tiles on grout, glazed track-colour tesserae, one lapis accent, Unbounded titles.",
         bg="#07080B", tile="#15171D", canvas="#0F1015", text="#EDEEF4", text1="#AEB0BC", accent="#8A93FA",
         gap=6, r=8, clip="tint", font=DISPLAY, ui=UI, caps=False,
         tracks=["#F2849B", "#ED9154", "#40C595", "#6EADFE"]),
    dict(key="B", name="Console", tag="Alternative",
         line="Hardware instrument: graphite, hairlines instead of gaps, square corners, phosphor-teal accent, mono caps.",
         bg="#1B1C1E", tile="#1B1C1E", canvas="#141516", text="#E4E2DC", text1="#8F8D86", accent="#3EE6C0",
         gap=0, r=1, clip="outline", font=MONO, ui=MONO, caps=True,
         tracks=["#E8E2D0", "#B9C8A6", "#9FC3C9", "#C9B2A4"]),
    dict(key="C", name="Paper", tag="Alternative",
         line="Light-first and editorial: paper white, ink black UI, colour only in the music, vermilion playhead.",
         bg="#E9E6DF", tile="#FAF8F3", canvas="#FFFFFF", text="#16161A", text1="#6B6A66", accent="#16161A",
         gap=10, r=2, clip="pastel", font=UI, ui=UI, caps=False,
         tracks=["#F3B7C3", "#F6C79E", "#A9DCC2", "#B7CDF4"]),
]


def mini_scene(s, d, x, y, w, h):
    s.rect(x, y, w, h, d["bg"], r=10)
    g = d["gap"]
    inner = 10 if g == 0 else g
    tx, ty, tw = x + inner, y + inner, w - 2 * inner
    th = 34
    s.rect(tx, ty, tw, th, d["tile"], r=d["r"])
    if g == 0:
        s.line(x, ty + th, x + w, ty + th, "#3A3B3E", 1)
    icon(s, "play", tx + 18, ty + th / 2, d["accent"] if d["key"] != "C" else d["text"], 15)
    icon(s, "stop", tx + 40, ty + th / 2, d["text1"], 15)
    icon(s, "record", tx + 62, ty + th / 2, "#E5482D" if d["key"] == "C" else "#F75B6B", 15)
    num = "009.3.1" if d["caps"] else "9.3.1"
    s.text(tx + 90, ty + 23, num, 16, d["text"], MONO, 500)
    s.text(tx + tw - 12, ty + 22, "120.00 BPM", 11.5, d["text1"], d["ui"], 600, "end")
    # tracks pane
    py = ty + th + (g if g else 0)
    ph = 168
    s.rect(tx, py, tw, ph, d["tile"], r=d["r"])
    hw = 92
    s.rect(tx + hw, py, tw - hw, ph, d["canvas"])
    if g == 0:
        s.line(tx + hw, py, tx + hw, py + ph, "#3A3B3E", 1)
    names = ["DRUMS", "BASS", "KEYS", "LEAD"] if d["caps"] else ["Drums", "Bass", "Keys", "Lead"]
    rh = ph / 4
    for i in range(4):
        ry = py + i * rh
        c = d["tracks"][i]
        if d["key"] == "A":
            s.rect(tx + 8, ry + rh / 2 - 7, 14, 14, c, r=3.5)
            s.text(tx + 15, ry + rh / 2 + 3.5, str(i + 1), 9, "#101116", UI, 700, "middle")
        elif d["key"] == "B":
            s.circle(tx + 14, ry + rh / 2, 3, c)
        else:
            s.rect(tx + 6, ry + 8, 3, rh - 16, c)
        s.text(tx + 30, ry + rh / 2 + 4, names[i], 11 if not d["caps"] else 9.5, d["text"], d["ui"], 600)
        if g == 0:
            s.line(tx, ry + rh, tx + tw, ry + rh, "#2C2D30", 1)
        for (st, ln) in [(0, 2), (2, 3)] if i % 2 == 0 else [(1, 3), (4, 1)]:
            cx = tx + hw + 6 + st * (tw - hw - 12) / 5
            cw = ln * (tw - hw - 12) / 5 - 4
            cy, ch = ry + 5, rh - 10
            sel = i == 2 and st == 0
            if d["clip"] == "tint":
                s.rect(cx, cy, cw, ch, c, r=4, op=0.2)
                s.rect(cx + .5, cy + .5, cw - 1, ch - 1, "none", r=4, stroke=c, sop=0.6)
                s.text(cx + 5, cy + 11, "Clip", 9, c, UI, 600)
                for k in range(int(cw // 14)):
                    s.rect(cx + 5 + k * 14, cy + 15 + (k * 7 % 13), 9, 3, c, r=1)
                if sel:
                    s.rect(cx - 2.5, cy - 2.5, cw + 5, ch + 5, "none", r=6, stroke=d["accent"], sw=1.5)
            elif d["clip"] == "outline":
                s.rect(cx, cy, cw, ch, "none", r=1, stroke=c, sw=1)
                s.text(cx + 4, cy + 10, "CLIP", 8, c, MONO, 600)
                for k in range(int(cw // 9)):
                    s.circle(cx + 6 + k * 9, cy + 18 + (k * 5 % 9), 1.6, c)
                if sel:
                    s.rect(cx, cy, cw, ch, d["accent"], r=1, op=0.15)
                    s.rect(cx, cy, cw, ch, "none", r=1, stroke=d["accent"], sw=1.5)
            else:
                s.rect(cx, cy, cw, ch, c, r=2)
                s.text(cx + 5, cy + 11, "Clip", 9, "#16161A", UI, 600)
                for k in range(int(cw // 14)):
                    s.rect(cx + 5 + k * 14, cy + 15 + (k * 7 % 13), 9, 3, "#16161A", r=0.5, op=0.6)
                if sel:
                    s.rect(cx - 2, cy - 2, cw + 4, ch + 4, "none", r=3, stroke="#16161A", sw=2)
    phx = tx + hw + (tw - hw) * 0.55
    s.line(phx, py, phx, py + ph, "#E5482D" if d["key"] == "C" else (d["accent"] if d["key"] == "B" else d["text"]),
           1.3)


def mock_directions(path):
    W, H = 1440, 560
    s = Svg(W, H, "#101114")
    s.text(40, 52, "Three directions", 22, "#EDEEF4", DISPLAY, 600, ls=1)
    s.text(40, 76, "Same content, three visual languages. A is recommended; B and C are shown so the choice is real.",
           12, "#AEB0BC", UI, 500)
    cw = (W - 80 - 2 * 24) / 3
    for i, d in enumerate(DIRS):
        x = 40 + i * (cw + 24)
        y = 104
        mini_scene(s, d, x, y, cw, 270)
        s.text(x, y + 304, f"{d['key']}  ·  {d['name']}", 17, "#EDEEF4", DISPLAY, 600, ls=0.5)
        tagc = "#8A93FA" if d["tag"] == "Recommended" else "#AEB0BC"
        s.text(x + cw, y + 302, d["tag"], 11, tagc, UI, 600, "end")
        # wrap the line by hand
        words, lines, cur = d["line"].split(), [], ""
        for wd in words:
            if len(cur) + len(wd) > 62:
                lines.append(cur)
                cur = wd
            else:
                cur = (cur + " " + wd).strip()
        lines.append(cur)
        for k, ln in enumerate(lines):
            s.text(x, y + 328 + k * 17, ln, 12, "#AEB0BC", UI, 450)
        for k, c in enumerate([d["bg"], d["tile"], d["canvas"], d["text"], d["accent"]] + d["tracks"]):
            s.rect(x + k * 30, y + 380, 26, 26, c, r=5)
            s.rect(x + k * 30 + .5, y + 380.5, 25, 25, "none", r=5, stroke="#fff", sop=0.12)
    s.save(path)
