"""Builds every mockup SVG and renders it to PNG.
Run from this folder:  FONTCONFIG_FILE=<fonts.conf> PYTHONDONTWRITEBYTECODE=1 python3 build.py [filter]
fonts.conf must list res/fonts (Inter, JetBrains Mono) and Instrument Sans (OFL, Google Fonts)."""
import os, subprocess, sys
from svg import Theme
import editor, panels, wireframes, directions

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)

JOBS = [
    ("E1-editor", editor.mock_editor, 1600, True),
    ("E2-perform", editor.mock_perform, 1600, True),
    ("E4-editor-plays-clips", editor.mock_editor_clips, 1600, True),
    ("E3-drums", editor.mock_drums, 1600, True),
    ("E5-keyboard", editor.mock_keyboard, 1600, True),
    ("P1-panels", panels.mock_panels, 1600, True),
    ("P2-panel-widened", panels.mock_widened, 1600, True),
    ("P3-drum-out-variants", panels.mock_variants, 1600, True),
    ("P4-library", panels.mock_library, 1600, False),
    ("P5-screen-options", panels.mock_screens, 1600, True),
    ("D2-colour-schemes", panels.mock_schemes, 1600, False),
    ("D1-design-system", panels.mock_system, 1600, True),
    ("D0-directions", directions.mock_directions, 1600, False),
]

if __name__ == "__main__":
    flt = sys.argv[1] if len(sys.argv) > 1 else ""
    for name, fn, width, themed in JOBS:
        for theme in (("dark", "light") if themed else ("both",)):
            base = f"{name}-{theme}" if themed else name
            if flt not in base:
                continue
            svg = os.path.join(OUT, "svg", base + ".svg")
            fn(Theme(theme if themed else "light"), svg)
            subprocess.run(["rsvg-convert", "-w", str(width), svg, "-o", os.path.join(OUT, base + ".png")], check=True)
            print("wrote", base)
    if "W" in flt or not flt:
        wireframes.build_all(os.path.join(os.path.dirname(OUT), "wireframes"))
