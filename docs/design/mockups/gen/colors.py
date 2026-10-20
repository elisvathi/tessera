"""Tessera "Grout & Glaze" palette: OKLCH sources -> sRGB hex, plus WCAG contrast checks.
Run: python3 colors.py   (prints the tables used in DESIGN_LANGUAGE.md)"""
import math


def oklch_to_hex(L, C, h):
    a = C * math.cos(math.radians(h)); b = C * math.sin(math.radians(h))
    l_ = L + 0.3963377774 * a + 0.2158037573 * b
    m_ = L - 0.1055613458 * a - 0.0638541728 * b
    s_ = L - 0.0894841775 * a - 1.2914855480 * b
    l, m, s = l_ ** 3, m_ ** 3, s_ ** 3
    r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s
    g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s
    bb = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s

    def enc(x):
        x = max(0.0, min(1.0, x))
        return 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055
    return "#%02X%02X%02X" % tuple(round(enc(v) * 255) for v in (r, g, bb))


def lum(hexs):
    v = [int(hexs[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    v = [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in v]
    return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2]


def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


INK, PLASTER = 262, 78   # neutral hues: blue-grey ink (dark), warm lime plaster (light)


def dark_scheme(canvas, hue, chroma, accent_h, text=(0.89, 0.77, 0.69)):
    """A dark scheme built from one canvas lightness. Steps between surfaces are small and even, so nothing
    reads as a black hole, and text stays near 10:1 instead of 15:1."""
    n = lambda dl, dc=0.0: (canvas + dl, chroma + dc, hue)
    return {
        "ground":  n(-0.030),            # the grout between panes; outermost background
        "canvas":  n(0.0),               # where content sits: grids, timeline, slots area
        "surface": n(0.028, 0.001),      # headers, toolbars, inspector
        "raised":  n(0.075, 0.002),      # buttons, fields, menus
        "hover":   n(0.110, 0.003),
        "pressed": n(0.145, 0.004),
        "well":    n(-0.020),            # recessed: empty slots, steps off, value tracks
        "line0":   n(0.028),             # sub-grid
        "line1":   n(0.060, 0.001),      # beat lines, dividers
        "line2":   n(0.140, 0.003),      # bar lines, borders
        "text0":   (text[0], 0.006, hue),
        "text1":   (text[1], chroma + 0.004, hue),
        "text2":   (text[2], chroma + 0.006, hue),
        "accent":  (0.720, 0.130, accent_h),
        "accentText": (0.800, 0.100, accent_h),
        "onAccent": (0.200, 0.030, accent_h),
        "play":    (0.790, 0.140, 160),  # jade
        "queue":   (0.840, 0.130, 86),   # smalti gold
        "record":  (0.690, 0.180, 25),   # vermilion
        "screen":  (0.225, 0.012, INK),  # module displays: a dark screen in every scheme
        "plate":   (0.900, 0.012, PLASTER),  # output plate, inverted
        "plateText": (0.260, 0.014, INK),
        "panel":   (canvas + 0.010, chroma + 0.002, hue),  # module panel face
        "panelText": (0.890, 0.010, hue),
    }


def light_scheme(canvas, hue, chroma, accent_h, ground_drop=0.095, text=(0.220, 0.420, 0.520), deep=0.0):
    """deep darkens accent and state colours so they keep their contrast on a mid-grey canvas."""
    n = lambda dl, dc=0.0: (canvas + dl, chroma + dc, hue)
    return {
        "ground":  n(-ground_drop, 0.007),
        "canvas":  n(0.0),
        "surface": n(-0.028, 0.003),
        "raised":  (min(1.0, canvas + 0.012), max(0.0, chroma - 0.004), hue),
        "hover":   n(-0.060, 0.005),
        "pressed": n(-0.095, 0.007),
        "well":    n(-0.055, 0.004),
        "line0":   n(-0.040, 0.002),
        "line1":   n(-0.090, 0.005),
        "line2":   n(-0.190, 0.008),
        "text0":   (text[0], 0.014, INK),
        "text1":   (text[1], 0.016, INK),
        "text2":   (text[2], 0.016, INK),
        "accent":  (0.520 - deep, 0.180, accent_h),
        "accentText": (0.480 - deep, 0.180, accent_h),
        "onAccent": (0.990, 0.000, 0),
        "play":    (0.520 - deep, 0.120, 160),
        "queue":   (0.560 - deep, 0.120, 70),
        "record":  (0.550 - deep, 0.190, 27),
        "screen":  (0.225, 0.014, INK),
        "plate":   (0.255, 0.016, INK),
        "plateText": (0.930, 0.008, PLASTER),
        "panel":   (0.935, 0.012, PLASTER),
        "panelText": (0.240, 0.014, INK),
    }


# Colour schemes the user picks from (instance menu > Colour scheme). "Follow Rack" uses Plaster with light
# panels and Slate with dark panels. Accent hues avoid the state hues (play 160, queue 86, record 25).
SCHEMES = {
    "plaster": dict(label="Plaster", dark=False, desc="Light, warm white. Default with light panels",
                    tokens=light_scheme(0.990, PLASTER, 0.004, 277)),
    "sand":    dict(label="Sand", dark=False, desc="Light, softer paper tone, less glare",
                    tokens=light_scheme(0.930, 75, 0.028, 250, ground_drop=0.085, text=(0.250, 0.420, 0.500), deep=0.03)),
    "slate":   dict(label="Slate", dark=True, desc="Soft dark grey. Default with dark panels",
                    tokens=dark_scheme(0.290, INK, 0.010, 277)),
    "walnut":  dict(label="Walnut", dark=True, desc="Warm dark, brown-grey like an old studio desk",
                    tokens=dark_scheme(0.300, 55, 0.020, 250, text=(0.900, 0.790, 0.700))),
}
SCHEME_ORDER = ["plaster", "sand", "slate", "walnut"]
DARK = SCHEMES["slate"]["tokens"]
LIGHT = SCHEMES["plaster"]["tokens"]

TRACK_HUES = [25 + 22.5 * i for i in range(16)]
TRACK_DARK = [oklch_to_hex(0.740, 0.120, h) for h in TRACK_HUES]
TRACK_LIGHT = [oklch_to_hex(0.665, 0.145, h) for h in TRACK_HUES]
ON_TILE = "#13151B"   # text on any solid track tile, both themes


def resolve(theme):
    return {k: oklch_to_hex(*v) for k, v in theme.items()}


PAIRS = [("text0", "surface"), ("text0", "canvas"), ("text1", "surface"), ("text1", "canvas"), ("text2", "surface"),
         ("text2", "canvas"), ("text2", "raised"), ("accentText", "surface"), ("accentText", "canvas"),
         ("onAccent", "accent"), ("accent", "canvas"), ("play", "canvas"), ("record", "canvas"), ("queue", "canvas"),
         ("line2", "canvas"), ("line1", "canvas"), ("plateText", "plate"), ("panelText", "panel"),
         ("text0", "screen")]

if __name__ == "__main__":
    for name, t in ((k, SCHEMES[k]["tokens"]) for k in SCHEME_ORDER):
        h = resolve(t)
        print("==", name)
        for k, v in h.items():
            print(f"  {k:11s} {v}")
        for fg, bg in PAIRS:
            print(f"    {fg:10s} on {bg:8s} {contrast(h[fg], h[bg]):5.2f}")
    hd, hl = resolve(DARK), resolve(LIGHT)
    print("TRACK_DARK ", TRACK_DARK)
    print("TRACK_LIGHT", TRACK_LIGHT)
    print("ink on dark tiles ", [round(contrast(c, ON_TILE), 1) for c in TRACK_DARK])
    print("ink on light tiles", [round(contrast(c, ON_TILE), 1) for c in TRACK_LIGHT])
    print("dark tiles vs canvas ", [round(contrast(c, hd['canvas']), 1) for c in TRACK_DARK])
    print("light tiles vs canvas", [round(contrast(c, hl['canvas']), 1) for c in TRACK_LIGHT])
    old = ("#646B75", "#1A1C20")
    print("old text2 on bg1", round(contrast(*old), 2), " old text1 on bg1", round(contrast("#9AA1AB", "#1A1C20"), 2))
