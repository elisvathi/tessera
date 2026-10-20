# Tessera

A VCV Rack 2 plugin: a sequencer with up to 64 polyphonic tracks, a piano roll, a drum step editor, a clip
launcher, a timeline arranger, automation that outputs CV, recording, and MIDI/DAWproject export. Tracks play
through separate output modules (Track Out, Drum Out, Auto Out) placed anywhere in the patch. It is free, open
source software (GPL-3.0-or-later), given away in the VCV Library.

## Read only what the task needs

Don't read these up front. Open the part that matches the task, and search with grep rather than reading whole files.

| Doc | Read it when |
|---|---|
| [`docs/DESIGN.md`](docs/DESIGN.md) | You need the original design. Sections: §4 data model, §5 commands, undo and threading, §6 modules, output bus, registry and parameter mapping, §7 engine and timing, §9 drums, §10 clip launcher, §12 automation, §13 scales, §14 recording, §15 visual design and palette, §17 saving and MIDI export, §18 performance budgets, §19 testing. |
| [`docs/EDITOR_UX.md`](docs/EDITOR_UX.md) | You change anything under `src/ui/`. It is the authority for editor behaviour, gestures and the keymap. |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | Before you build on something in DESIGN.md or EDITOR_UX.md: grep it for the topic. Where they disagree, DECISIONS.md wins. Add a short entry when you make a choice that changes the design. |
| [`docs/DEVICES.md`](docs/DEVICES.md) | You work on note devices or modulators. |
| [`docs/QA.md`](docs/QA.md) | You add a feature that needs a manual test step. |
| [`docs/manual/`](docs/manual/README.md) | You change anything a user can see or hear (see below). |

## Rules that always apply

- **The audio thread** (`process()` and everything it calls) never allocates, frees, locks a mutex, logs, does file
  I/O, touches widgets, or touches the registry. Output modules read one atomic pointer and copy one frame.
- **Every edit is an undoable Command** (`apply` / `revert`) wrapped in a Rack history action. Widgets never change
  the song directly. A drag is one undo step, committed on mouse-up. Nothing ships without undo.
- **Widgets keep IDs, not pointers** into the song across frames.
- **The UI thread owns the song.** The engine gets an immutable snapshot through an atomic pointer swap.
- **Never leave a gate high** after stop, loop wrap, mute or a snapshot swap.
- **An output module never rebinds silently** to another track or Tessera when its target disappears.
- **`src/model/` and `src/engine/` build without Rack**, so they can be unit tested.
- **Colours and sizes live in `src/ui/Theme.hpp`.** Never hard-code them in a view.
- **Never name other DAWs** (or their makers) in code, docs, the manual or the UI. Describe the behaviour instead.

## Build and test

```sh
RACK_DIR=~/Rack-SDK make -j16           # build the plugin
RACK_DIR=~/Rack-SDK make -j16 test      # unit tests (doctest, no Rack needed)
RACK_DIR=~/Rack-SDK make -j16 install   # install into the local Rack; restart Rack to load it
```

## Conventions

- C++17. `clang-format` (LLVM base, 4-space indent, 120 columns). `PascalCase` types, `camelCase` functions and
  variables, `kConstant` constants, no member prefixes.
- No exceptions or RTTI in engine code. `assert` freely in model code.
- A new feature comes with its command(s), tests, a shortcut if it fits, a tooltip, an entry in the help overlay,
  and the user manual update.
- Shortcuts live in one action table (`src/ui/interaction/Shortcuts.cpp`); the help overlay and tooltips are
  generated from it.

## User manual

`docs/manual/` describes what Tessera does today, for musicians. **Every behaviour change updates it in the same
commit**: a new feature, a changed gesture, key, label, menu or default, a removed feature, a moved control. That
includes retaking screenshots that show the old UI. How: [`docs/manual/tools/README.md`](docs/manual/tools/README.md).
