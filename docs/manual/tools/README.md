# Keeping the user manual current

The user manual (`docs/manual/`) describes what Tessera does today, for musicians. It is part of every behaviour
change, the same way tests are.

## When a change is done

A change that a user can see or hear is done when the manual matches it:

1. Find every place the behaviour appears: `grep -rn "<the control's label or key>" docs/manual/*.md`. Check the
   shortcut table (`15-shortcuts.md`), the "Where is…" table (`16-troubleshooting.md`) and the chapter list in
   `README.md` too.
2. Rewrite those passages to say what Tessera does now. Remove text about behaviour that is gone.
3. Retake every screenshot that now shows the old UI (see below). A screenshot whose control moved, was renamed or
   looks different is stale.
4. New feature: add it to the chapter where a user would look for it, with a screenshot if it has UI.

Write as the existing chapters do: plain words, short sentences, one idea per sentence, "you", the UI's own labels
in **bold**, keys as **Ctrl+X** (the manual says once that Mac users read Cmd). Describe behaviour, never code.

To read the result as web pages, run `docs/manual/tools/html.sh` (pandoc builds `build/manual-html/`; add `--open`
to open it in the browser).

## Taking screenshots

The scripts here run a private Rack on a hidden X display (:94), so they never touch the user's screen or Rack.
They need Xvfb, xdotool, ImageMagick and Rack Free in `~/Rack2Free`.

```sh
make dist                                   # the plugin package the test Rack loads
docs/manual/tools/rack.sh setup             # build/manual-rack: user folder and demo patches
docs/manual/tools/rack.sh start demo.vcv    # or panel.vcv, modules.vcv, first.vcv, fresh.vcv
docs/manual/tools/rack.sh click 226 198     # EDITOR button: opens the full editor
docs/manual/tools/rack.sh shot timeline "1180x430+422+40"   # writes docs/manual/images/timeline.png
docs/manual/tools/rack.sh stop              # always stop it when done
```

- `demo_patch.py` writes the demo patches: a 112 BPM A-minor song (Bass, Chords, Lead, Drums; scenes Intro, Verse,
  Chorus; a Cutoff lane on Chords). Its docstring lists each patch. Add to it rather than building songs by hand.
- `rack.sh shot <name> <WxH+X+Y>` crops; read the full screenshot first to find the crop. The window is 1600×1000.
- Keys need the mouse over the right pane (`rack.sh move x y` first). Menus open on `click`; submenus on `move`.
- Keep the file name when retaking an image, so the chapters need no edit.

## Numbered pictures

`editor-overview-numbered.png` and `tessera-panel-numbered.png` carry orange numbers that the text refers to
(`03-editor.md`, `02-modules.md`). Retake the plain picture, then redraw the numbers:

```sh
cd docs/manual
tools/annotate.sh images/editor-overview.png images/editor-overview-numbered.png \
  860,19,1 486,19,2 1395,19,3 100,420,4 305,420,5 1000,420,6 300,225,7 600,488,8 1200,640,9 1200,960,10 1480,820,11
tools/annotate.sh images/tessera-panel.png images/tessera-panel-numbered.png \
  15,165,1 18,272,2 18,340,3 420,268,4 530,290,5 292,322,6 532,350,7 18,393,8 532,412,9
```

Each `x,y,n` puts number n at x,y. Move a number when the part it marks moves, and keep the numbered list in the
chapter in step with it.
