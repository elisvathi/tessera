"""Three visual directions side by side, each as the same mini scene: a clip row, a few notes, a transport chip."""
import random
from svg import Svg, Theme, icon
from common import UI, MONO, logo

DIRS = [
    ("A · Grout & Glaze (recommended)", "Panes are glazed tiles on dark grout; colour lives in clips and notes; "
     "one lapis accent.", dict(bg="#0F1217", pane="#1B1E25", canvas="#15181E", line="#25292F", text="#EEF0F4",
                              sub="#8C929E", accent="#8F9BFB", tracks=["#F48A82", "#E09D45", "#18C7B3", "#68B2FB"],
                              font=UI, r=8, gap=6, clipop=0.3, note_r=2.5)),
    ("B · Score", "Paper-first: warm white, ink hairlines, notes as black bars with coloured track ticks; a single "
     "vermilion accent. Calm and print-like, but tracks lose colour and dark mode feels like an afterthought.",
     dict(bg="#F4F0E8", pane="#FBF8F2", canvas="#FFFFFF", line="#D8D2C6", text="#1B1B1B", sub="#6B665E",
          accent="#D2412C", tracks=["#1B1B1B", "#1B1B1B", "#1B1B1B", "#1B1B1B"], font="Instrument Sans", r=2, gap=1,
          clipop=0.06, note_r=0)),
    ("C · Phosphor", "Hardware screen: true black, mono type everywhere, glowing amber and green. Strong identity "
     "and very 'Eurorack', but tiring for long editing, poor in light mode, and close to many tracker plugins.",
     dict(bg="#000000", pane="#07090A", canvas="#000000", line="#1A2A1E", text="#FFB347", sub="#7A6A40",
          accent="#7CFFB2", tracks=["#FFB347", "#7CFFB2", "#FFB347", "#7CFFB2"], font=MONO, r=0, gap=2, clipop=0.18,
          note_r=0)),
]


def scene(s, x, y, w, h, d):
    s.rect(x, y, w, h, d["bg"], r=10)
    g = d["gap"]
    # transport
    s.rect(x + 10, y + 10, w - 20, 34, d["pane"], r=d["r"])
    s.rect(x + 20, y + 17, 28, 20, d["tracks"][2] if d["font"] != MONO else d["accent"], r=min(5, d["r"]))
    icon(s, "play", x + 34, y + 27, d["bg"], 12)
    s.text(x + 60, y + 32, "9.3.1", 15, d["text"], MONO, 500)
    s.text(x + 120, y + 32, "120.00", 15, d["text"], MONO, 500)
    s.text(x + w - 24, y + 32, "C minor", 12, d["sub"], d["font"], 500, "end")
    # song pane
    py = y + 44 + g + 6
    s.rect(x + 10, py, w - 20, 120, d["pane"], r=d["r"])
    for i in range(4):
        ry = py + 10 + i * 26
        c = d["tracks"][i]
        s.rect(x + 20, ry + 4, 14, 14, c if d["font"] != "Instrument Sans" or i == 0 else d["text"], r=min(4, d["r"]))
        s.text(x + 42, ry + 16, ["Drums", "Bass", "Keys", "Lead"][i], 11, d["text"], d["font"], 600)
        for k in range(3):
            cx = x + 110 + k * 95 + (i * 13 % 30)
            s.rect(cx, ry + 2, 86, 20, c, r=min(4, d["r"]), op=d["clipop"] if d["clipop"] > 0.1 else 1)
            if d["clipop"] <= 0.1:
                s.rect(cx, ry + 2, 86, 20, d["canvas"], r=0)
                s.rect(cx + 0.5, ry + 2.5, 85, 19, "none", stroke=d["text"], sw=1)
                s.rect(cx, ry + 2, 3, 20, d["accent"] if i == 2 else d["text"])
            rnd = random.Random(i * 7 + k)
            for n in range(6):
                s.rect(cx + 6 + n * 13, ry + 6 + rnd.random() * 12, 9, 2, c if d["clipop"] > 0.1 else d["text"], op=0.9)
        if i == 2:
            s.rect(x + 109, ry + 1, 88, 22, "none", r=min(5, d["r"] + 1), stroke=d["accent"], sw=1.6)
    # notes pane
    ny = py + 120 + g + 6
    s.rect(x + 10, ny, w - 20, h - (ny - y) - 10, d["pane"], r=d["r"])
    s.rect(x + 20, ny + 10, w - 40, h - (ny - y) - 30, d["canvas"])
    for k in range(12):
        s.line(x + 20 + k * (w - 40) / 12, ny + 10, x + 20 + k * (w - 40) / 12, ny + h - (ny - y) - 20, d["line"], 1)
    rnd = random.Random(5)
    for n in range(12):
        nx = x + 26 + n * 34
        nyy = ny + 18 + rnd.random() * (h - (ny - y) - 60)
        col = d["tracks"][2] if d["font"] != "Instrument Sans" else d["text"]
        s.rect(nx, nyy, 28 + rnd.choice([0, 20]), 9, col, r=d["note_r"])
        if n == 4:
            s.rect(nx - 1.5, nyy - 1.5, 31, 12, "none", r=d["note_r"] + 1, stroke=d["accent"], sw=1.6)


def mock_directions(T, path):
    s = Svg(1600, 640, "#E9E7E2")
    s.text(40, 48, "Three visual directions for Tessera (same scene)", 22, "#1A1A1A", UI, 600)
    for i, (name, desc, d) in enumerate(DIRS):
        x = 40 + i * 515
        scene(s, x, 74, 495, 420, d)
        s.text(x, 524, name, 15, "#1A1A1A", UI, 600)
        words = desc.split()
        line, yy = "", 548
        for wd in words:
            if len(line) + len(wd) > 78:
                s.text(x, yy, line, 12, "#4A4A4A", UI, 400)
                line, yy = "", yy + 18
            line += wd + " "
        s.text(x, yy, line, 12, "#4A4A4A", UI, 400)
    s.save(path)
