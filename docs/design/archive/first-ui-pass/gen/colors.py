"""Tessera "Mosaic" palette: OKLCH sources -> sRGB hex, plus WCAG contrast checks.
Run: python3 colors.py   (prints the token tables used in DESIGN_LANGUAGE.md)"""
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

# Neutrals carry a little lapis (hue 275) in dark, a little warmth (hue 85) in light.
DARK = {
    "grout":   (0.135, 0.008, 275), "canvas":  (0.175, 0.010, 275), "surface": (0.205, 0.012, 275),
    "raised":  (0.255, 0.015, 275), "hover":   (0.295, 0.017, 275), "pressed": (0.335, 0.018, 275),
    "well":    (0.150, 0.010, 275), "overlay": (0.240, 0.014, 275),
    "lineFaint": (0.215, 0.011, 275), "line": (0.255, 0.013, 275), "lineStrong": (0.33, 0.016, 275),
    "text0": (0.95, 0.008, 275), "text1": (0.76, 0.017, 275), "text2": (0.62, 0.018, 275),
    "accent": (0.70, 0.150, 278), "accentText": (0.78, 0.120, 278), "onAccent": (0.16, 0.03, 278),
    "play": (0.78, 0.150, 158), "record": (0.68, 0.190, 18), "queue": (0.84, 0.140, 85),
    "plate": (0.92, 0.010, 85), "plateText": (0.25, 0.012, 275),
}
LIGHT = {
    "grout":   (0.860, 0.010, 85), "canvas":  (0.985, 0.004, 85), "surface": (0.955, 0.006, 85),
    "raised":  (0.915, 0.008, 85), "hover":   (0.885, 0.010, 85), "pressed": (0.850, 0.011, 85),
    "well":    (0.935, 0.007, 85), "overlay": (0.995, 0.003, 85),
    "lineFaint": (0.945, 0.006, 85), "line": (0.910, 0.008, 85), "lineStrong": (0.80, 0.012, 85),
    "text0": (0.22, 0.012, 275), "text1": (0.45, 0.016, 275), "text2": (0.525, 0.014, 275),
    "accent": (0.52, 0.200, 278), "accentText": (0.48, 0.200, 278), "onAccent": (0.99, 0.0, 0),
    "play": (0.50, 0.120, 158), "record": (0.54, 0.190, 22), "queue": (0.525, 0.110, 75),
    "plate": (0.235, 0.014, 275), "plateText": (0.93, 0.008, 85),
}

def track_ring(L, C):
    return [oklch_to_hex(L, C, 30 + 22.5 * i) for i in range(16)]

TRACK_DARK = track_ring(0.74, 0.135)
TRACK_LIGHT = track_ring(0.64, 0.150)

def resolve(theme):
    return {k: oklch_to_hex(*v) for k, v in theme.items()}

if __name__ == "__main__":
    for name, t in (("DARK", DARK), ("LIGHT", LIGHT)):
        h = resolve(t)
        print("==", name)
        for k, v in h.items():
            print(f"  {k:11s} {v}")
        print("  contrast:")
        for fg, bg in [("text0", "surface"), ("text0", "canvas"), ("text1", "surface"), ("text1", "canvas"),
                       ("text2", "surface"), ("accentText", "surface"), ("accentText", "canvas"),
                       ("onAccent", "accent"), ("play", "surface"), ("record", "surface"), ("queue", "surface"),
                       ("accent", "canvas"), ("lineStrong", "canvas"), ("line", "canvas"), ("plateText", "plate"),
                       ("text0", "raised"), ("text1", "raised")]:
            print(f"    {fg:10s} on {bg:8s} {contrast(h[fg], h[bg]):5.2f}")
    print("TRACK_DARK ", TRACK_DARK)
    print("TRACK_LIGHT", TRACK_LIGHT)
    hd, hl = resolve(DARK), resolve(LIGHT)
    print("track vs canvas dark :", [round(contrast(c, hd["canvas"]), 1) for c in TRACK_DARK])
    print("track vs canvas light:", [round(contrast(c, hl["canvas"]), 1) for c in TRACK_LIGHT])
    print("note label on dark track tile (onAccent-ish #15161C):", [round(contrast(c, "#15161C"), 1) for c in TRACK_DARK])
    print("white on light track:", [round(contrast(c, "#FFFFFF"), 1) for c in TRACK_LIGHT])
