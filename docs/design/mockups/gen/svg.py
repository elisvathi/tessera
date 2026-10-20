"""Tiny SVG builder and the theme objects used by the Tessera mockups."""
import colors as C


def esc(s):
    return str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Theme:
    """Resolved tokens for one colour scheme ("dark" = Slate, "light" = Plaster). Screens (module displays) always
    use the Slate screen tokens."""
    ALIAS = {"dark": "slate", "light": "plaster"}

    def __init__(self, name):
        key = self.ALIAS.get(name, name)
        sc = C.SCHEMES[key]
        self.key = key
        self.name = sc["label"]
        self.dark = sc["dark"]
        for k, v in C.resolve(sc["tokens"]).items():
            setattr(self, k, v)
        self.D = C.resolve(C.DARK)          # screen tokens
        self.D["screen"] = self.screen
        self.tracks = C.TRACK_DARK if self.dark else C.TRACK_LIGHT
        self.screenTracks = C.TRACK_DARK
        self.onTile = C.ON_TILE
        self.onState = "#14161B" if self.dark else "#FFFFFF"
        self.keyWhite = "#C4C8CF" if self.dark else "#FFFFFF"
        self.keyBlack = "#30343B" if self.dark else "#2A2E36"
        self.glaze = 0.06 if self.dark else 0.0   # 1 px top highlight on tiles
        self.shade = 0.30 if self.dark else 0.08  # 1 px bottom shade on raised tiles


def track(T, i):
    return T.tracks[(i * 5) % 16]


class Svg:
    def __init__(self, w, h, bg=None):
        self.w, self.h = w, h
        self.parts = []
        self.defs = []
        self.ids = 0
        if bg:
            self.rect(0, 0, w, h, bg)

    def uid(self, p="i"):
        self.ids += 1
        return f"{p}{self.ids}"

    def add(self, s):
        self.parts.append(s)

    def rect(self, x, y, w, h, fill="none", r=0, stroke=None, sw=1, op=None, dash=None, sop=None):
        a = f'<rect x="{x:.2f}" y="{y:.2f}" width="{max(w,0):.2f}" height="{max(h,0):.2f}"'
        if r:
            a += f' rx="{r}"'
        a += f' fill="{fill}"'
        if op is not None:
            a += f' fill-opacity="{op}"'
        if stroke:
            a += f' stroke="{stroke}" stroke-width="{sw}"'
            if sop is not None:
                a += f' stroke-opacity="{sop}"'
            if dash:
                a += f' stroke-dasharray="{dash}"'
        self.add(a + "/>")

    def line(self, x1, y1, x2, y2, stroke, sw=1, op=None, dash=None, cap="butt"):
        a = f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{stroke}" stroke-width="{sw}" stroke-linecap="{cap}"'
        if op is not None:
            a += f' stroke-opacity="{op}"'
        if dash:
            a += f' stroke-dasharray="{dash}"'
        self.add(a + "/>")

    def circle(self, cx, cy, r, fill="none", stroke=None, sw=1, op=None):
        a = f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r}" fill="{fill}"'
        if op is not None:
            a += f' fill-opacity="{op}"'
        if stroke:
            a += f' stroke="{stroke}" stroke-width="{sw}"'
        self.add(a + "/>")

    def path(self, d, fill="none", stroke=None, sw=1.5, op=None, cap="round", join="round", sop=None):
        a = f'<path d="{d}" fill="{fill}"'
        if op is not None:
            a += f' fill-opacity="{op}"'
        if stroke:
            a += f' stroke="{stroke}" stroke-width="{sw}" stroke-linecap="{cap}" stroke-linejoin="{join}"'
            if sop is not None:
                a += f' stroke-opacity="{sop}"'
        self.add(a + "/>")

    def text(self, x, y, s, size=11, fill="#fff", font="Inter", weight=400, anchor="start", ls=0, op=None):
        a = (f'<text x="{x:.2f}" y="{y:.2f}" font-family="{font}" font-size="{size}" font-weight="{weight}" '
             f'fill="{fill}" text-anchor="{anchor}"')
        if ls:
            a += f' letter-spacing="{ls}"'
        if op is not None:
            a += f' fill-opacity="{op}"'
        self.add(a + f">{esc(s)}</text>")

    def clip_begin(self, x, y, w, h, r=0):
        cid = self.uid("c")
        self.defs.append(f'<clipPath id="{cid}"><rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}"/></clipPath>')
        self.add(f'<g clip-path="url(#{cid})">')

    def g_end(self):
        self.add("</g>")

    def g(self, transform):
        self.add(f'<g transform="{transform}">')

    def hatch(self, color, op=0.35):
        pid = self.uid("h")
        self.defs.append(
            f'<pattern id="{pid}" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
            f'<rect width="2" height="6" fill="{color}" fill-opacity="{op}"/></pattern>')
        return f"url(#{pid})"

    def lingrad(self, stops, x1=0, y1=0, x2=0, y2=1):
        gid = self.uid("g")
        st = "".join(f'<stop offset="{o}" stop-color="{c}" stop-opacity="{a}"/>' for o, c, a in stops)
        self.defs.append(f'<linearGradient id="{gid}" x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}">{st}</linearGradient>')
        return f"url(#{gid})"

    def radgrad(self, stops):
        gid = self.uid("r")
        st = "".join(f'<stop offset="{o}" stop-color="{c}" stop-opacity="{a}"/>' for o, c, a in stops)
        self.defs.append(f'<radialGradient id="{gid}">{st}</radialGradient>')
        return f"url(#{gid})"

    def shadow_filter(self, blur=8, dy=4, op=0.45):
        fid = self.uid("f")
        self.defs.append(
            f'<filter id="{fid}" x="-20%" y="-20%" width="140%" height="160%"><feDropShadow dx="0" dy="{dy}" '
            f'stdDeviation="{blur/2}" flood-color="#000" flood-opacity="{op}"/></filter>')
        return fid

    def save(self, path):
        body = "\n".join(self.parts)
        defs = "<defs>" + "".join(self.defs) + "</defs>"
        with open(path, "w") as f:
            f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                    f'viewBox="0 0 {self.w} {self.h}">{defs}\n{body}\n</svg>\n')


# ---------------------------------------------------------------- icons (16 px grid, 1.5 px stroke, round joins)

def icon(s, name, cx, cy, color, size=16, sw=1.5):
    k = size / 16.0
    s.g(f"translate({cx - 8*k:.2f},{cy - 8*k:.2f}) scale({k:.3f})")
    st = dict(stroke=color, sw=sw / k if k else sw)
    P = lambda d, **kw: s.path(d, **{**st, **kw})
    F = lambda d: s.path(d, fill=color)
    if name == "play":
        F("M5 3.5 L12.5 8 L5 12.5 Z")
    elif name == "stop":
        s.rect(4, 4, 8, 8, color, r=1.5)
    elif name == "record":
        s.circle(8, 8, 4.5, color)
    elif name == "loop":
        P("M3.5 9.5 V7 a2.5 2.5 0 0 1 2.5 -2.5 H12 M10 2.5 L12 4.5 L10 6.5 M12.5 6.5 V9 a2.5 2.5 0 0 1 -2.5 2.5 H4 M6 13.5 L4 11.5 L6 9.5")
    elif name == "pointer":
        P("M4.5 2.5 L4.5 12.5 L7.2 10 L9.2 14 L10.8 13.2 L8.9 9.4 L12.5 9.2 Z")
    elif name == "pencil":
        P("M3 13 L3.6 10.4 L10.6 3.4 L12.6 5.4 L5.6 12.4 Z M9.4 4.6 L11.4 6.6")
    elif name == "brush":
        P("M3 13 C3 11 4 10 5.5 10 L12.5 3 L13 3.5 L6 10.5 C6 12 5 13 3 13 Z")
    elif name == "eraser":
        P("M6 13.5 H13 M2.8 9.8 L8.8 3.8 L12.2 7.2 L7.9 11.5 L5.5 11.5 Z M5.8 6.8 L9.2 10.2")
    elif name == "scissors":
        s.circle(4.5, 11.5, 2, stroke=color, sw=1.5/k)
        s.circle(11.5, 11.5, 2, stroke=color, sw=1.5/k)
        P("M5.8 10 L11 2.5 M10.2 10 L5 2.5")
    elif name == "mute":
        s.rect(2.5, 6, 11, 4, "none", r=1, stroke=color, sw=1.5/k)
        P("M3 13 L13 3")
    elif name == "velocity":
        P("M3 13.5 V9 M6.3 13.5 V5 M9.6 13.5 V7 M12.9 13.5 V3")
    elif name == "range":
        P("M3 3 V13 M13 3 V13 M5.5 8 H10.5 M7 6.5 L5.5 8 L7 9.5 M9 6.5 L10.5 8 L9 9.5")
    elif name == "ripple":
        P("M2.5 8 H8 M6 6 L8 8 L6 10 M10 4 V12 M13.5 4 V12")
    elif name == "follow":
        P("M4.5 3 V13 M7.5 8 H13 M11 6 L13 8 L11 10")
    elif name == "inspector":
        s.rect(2.5, 3, 11, 10, "none", r=2, stroke=color, sw=1.5/k)
        P("M9.5 3 V13")
    elif name == "close":
        P("M4 4 L12 12 M12 4 L4 12")
    elif name == "undo":
        P("M5.5 4 L3 6.5 L5.5 9 M3 6.5 H9.5 a3.5 3.5 0 0 1 0 7 H6")
    elif name == "redo":
        P("M10.5 4 L13 6.5 L10.5 9 M13 6.5 H6.5 a3.5 3.5 0 0 0 0 7 H10")
    elif name == "chev-l":
        P("M10 3.5 L5.5 8 L10 12.5")
    elif name == "chev-r":
        P("M6 3.5 L10.5 8 L6 12.5")
    elif name == "chev-d":
        P("M4.5 6.5 L8 10 L11.5 6.5")
    elif name == "plus":
        P("M8 3.5 V12.5 M3.5 8 H12.5")
    elif name == "more":
        for x in (4, 8, 12):
            s.circle(x, 8, 1.2, color)
    elif name == "link":
        P("M7 9 L9 7 M6.2 6.6 L5 7.8 a2.3 2.3 0 0 0 3.2 3.2 L9.4 9.8 M9.8 9.4 L11 8.2 a2.3 2.3 0 0 0 -3.2 -3.2 L6.6 6.2")
    elif name == "curve":
        P("M2.5 12.5 C6 12.5 6 3.5 9 3.5 C11 3.5 11.5 8 13.5 8")
        s.circle(2.5, 12.5, 1.3, color)
        s.circle(13.5, 8, 1.3, color)
    elif name == "arm":
        s.circle(8, 8, 4.5, "none", stroke=color, sw=1.5/k)
        s.circle(8, 8, 2, color)
    elif name == "speaker":
        P("M3 6.2 H5.5 L8.8 3.5 V12.5 L5.5 9.8 H3 Z M11 5.5 a3.5 3.5 0 0 1 0 5")
    elif name == "chord":
        for y in (4, 8, 12):
            s.rect(3, y - 1.25, 10, 2.5, color, r=1)
    elif name == "snap":
        P("M3 4 V9 a5 5 0 0 0 10 0 V4 M3 6.5 H6 M10 6.5 H13")
    elif name == "fold":
        P("M3 4 H13 M3 8 H13 M3 12 H13")
        s.circle(13, 4, 0.01, color)
    elif name == "ghost":
        P("M4 13.5 V7 a4 4 0 0 1 8 0 V13.5 L10.5 12 L9 13.5 L8 12 L6.5 13.5 L5.5 12 Z")
    elif name == "devices":
        s.rect(2.5, 4, 4.5, 8, "none", r=1.2, stroke=color, sw=1.5/k)
        s.rect(9, 4, 4.5, 8, "none", r=1.2, stroke=color, sw=1.5/k)
    elif name == "launcher":
        for x in (3, 9):
            for y in (3, 9):
                s.rect(x, y, 4.5, 4.5, "none", r=1, stroke=color, sw=1.3/k)
    elif name == "file":
        P("M4 2.5 H9.5 L12.5 5.5 V13.5 H4 Z M9.5 2.5 V5.5 H12.5")
    elif name == "metronome":
        P("M5.5 13.5 L7 2.5 H9 L10.5 13.5 Z M8 10 L12 4")
    elif name == "dice":
        s.rect(3, 3, 10, 10, "none", r=2.2, stroke=color, sw=1.5/k)
        for x, y in ((5.8, 5.8), (10.2, 10.2), (8, 8)):
            s.circle(x, y, 1, color)
    elif name == "drum":
        P("M3 6 a5 2 0 0 0 10 0 a5 2 0 0 0 -10 0 V10.5 a5 2 0 0 0 10 0 V6")
    elif name == "keys":
        s.rect(2.5, 3, 11, 10, "none", r=1.5, stroke=color, sw=1.5/k)
        P("M6.2 3 V9 M9.8 3 V9")
    elif name == "scene":
        F("M4 3.5 L11 8 L4 12.5 Z")
        P("M13 3.5 V12.5")
    elif name == "capture":
        P("M8 3 V10 M5 7 L8 10 L11 7 M3.5 13 H12.5")
    elif name == "tempo":
        P("M3 13 L8 3 L13 13 Z M8 9 L11 5")
    elif name == "sun":
        s.circle(8, 8, 2.8, "none", stroke=color, sw=1.5/k)
        P("M8 1.5 V3 M8 13 V14.5 M1.5 8 H3 M13 8 H14.5 M3.4 3.4 L4.4 4.4 M11.6 11.6 L12.6 12.6 M3.4 12.6 L4.4 11.6 M11.6 4.4 L12.6 3.4")
    elif name == "max":
        P("M9.5 2.5 H13.5 V6.5 M13.5 2.5 L9 7 M6.5 13.5 H2.5 V9.5 M2.5 13.5 L7 9")
    elif name == "restore":
        P("M13.5 2.5 L9.5 6.5 M9.5 3 V6.5 H13 M2.5 13.5 L6.5 9.5 M3 9.5 H6.5 V13")
    elif name == "timeline":
        s.rect(2, 3.5, 7, 3, "none", r=1, stroke=color, sw=1.3/k)
        s.rect(6, 9.5, 8, 3, "none", r=1, stroke=color, sw=1.3/k)
    elif name == "moon":
        P("M12.5 10 A5.5 5.5 0 1 1 6 3.5 A4.5 4.5 0 0 0 12.5 10 Z")
    s.g_end()


ICON_NAMES = ["play", "stop", "record", "loop", "follow", "pointer", "pencil", "brush", "eraser", "scissors", "mute",
              "velocity", "range", "ripple", "chord", "snap", "fold", "ghost", "speaker", "link", "curve", "arm",
              "devices", "launcher", "inspector", "file", "undo", "redo", "metronome", "dice", "capture", "scene",
              "drum", "keys", "more", "plus", "chev-l", "chev-r", "close", "sun", "moon", "max", "restore", "timeline"]
