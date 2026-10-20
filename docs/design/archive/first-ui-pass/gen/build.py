"""Builds every mockup SVG and renders it to PNG.  python3 build.py [name-filter]"""
import os, subprocess, sys
from svg import Theme
import editor, panels, system

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)
FONTCONF = os.environ.get("FONTCONFIG_FILE")

JOBS = [
    ("editor-arranger", editor.mock_arranger, 1600),
    ("editor-launcher", editor.mock_launcher, 1600),
    ("editor-drums", editor.mock_drums, 1600),
    ("panels", panels.mock_panels, 1600),
    ("panel-widened", panels.mock_widened, 1600),
    ("system", system.mock_system, 1600),
]

if __name__ == "__main__":
    flt = sys.argv[1] if len(sys.argv) > 1 else ""
    for name, fn, width in JOBS:
        for theme in ("dark", "light"):
            base = f"{name}-{theme}"
            if flt not in base:
                continue
            svg = os.path.join(OUT, "svg", base + ".svg")
            os.makedirs(os.path.dirname(svg), exist_ok=True)
            fn(Theme(theme), svg)
            subprocess.run(["rsvg-convert", "-w", str(width), svg, "-o", os.path.join(OUT, base + ".png")], check=True)
            print("wrote", base)
    if "directions" in flt or not flt:
        system.mock_directions(os.path.join(OUT, "svg", "directions.svg"))
        subprocess.run(["rsvg-convert", "-w", "1600", os.path.join(OUT, "svg", "directions.svg"), "-o",
                        os.path.join(OUT, "directions.png")], check=True)
        print("wrote directions")
