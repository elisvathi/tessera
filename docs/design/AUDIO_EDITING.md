# Audio editing, takes and project files

Status: **agreed 9 Nov 2026.** Built: the split rule (§1) and phase 1, files (§6). Phases 2 to 4 are still to do.
Where this document and DECISIONS.md disagree, DECISIONS.md wins.

Mockups: `docs/design/mockups/audio-editing/index.html`.

It takes over the audio-editor and comping parts of `AUDIO_UX.md` (A2, A4, A5, R1, R2). The separate mixer modules
in that document are not part of this work.

---

## 1. The rule: what is shared and what is not

Audio has three layers. Each edit lands on exactly one of them.

| Layer | What it holds | Linked copies | Independent copy |
|---|---|---|---|
| **File** | The samples, as imported or recorded. Never changes. | Same file | Same file |
| **Clip** | Cuts, moved parts, takes, gain, fades inside the clip, reverse, tempo, warp markers | **Share it: an edit shows in every linked copy** | Has its own: an edit changes only this copy |
| **Tile** | Where it sits on the timeline, its length, its outer fades, mute | Each has its own | Each has its own |

Two kinds of cut follow from this, and they do different things on purpose:

- **Split on the timeline** (Ctrl+E) cuts the tile in two. **Each part becomes its own clip**, so nothing is linked
  between them. If the clip was linked, its parts leave the link and the other copies stay linked to each other.
  Built on 9 Nov for every way of cutting: Split, Split at transients, a clip dropped inside another, Delete range,
  Delete time, Insert silence.
- **Cut in the audio editor** cuts inside the clip. The tile stays one tile, and **every linked copy shows the cut**.

Example. A vocal clip sits at bar 1 and is linked at bar 9. An independent copy sits at bar 17. In the audio editor
you cut out a breath at bar 1. Bar 9 loses the breath too. Bar 17 keeps it. No audio file changed, so a copy costs no
disk space and any edit can be undone or changed later.

The audio editor always says what an edit will reach: **∞ 3 copies** in its header, with **Make unique** beside it.

## 2. Non-destructive edits

Files never change. An edit changes a list inside the clip that says which part of which take plays when.

```cpp
struct AudioRegion {           // one piece of the clip's edit list
    RegionId id;
    Tick start = 0, length = 0; // where it sits, in clip ticks
    int take = 0;               // which take it plays
    int64_t sourceFrame = 0;    // the frame of that take that plays at `start`
    float gainDb = 0;
    bool muted = false, reverse = false;
    Fade fadeIn, fadeOut;
};
```

- `AudioContent` gains `std::vector<AudioRegion> regions`. **Empty means one region over the whole clip**, so every
  existing clip stays as it is. One helper, `regionsOf()`, always returns a list, so there is one code path.
- Parts of a clip may overlap. The overlap is a crossfade, as on the timeline.
- Every gesture is one `SetAudioContent` command, so undo works as it does for gain or tempo today.
- An independent copy (`copyClip`) copies the list and needs no new file.

**Only one action writes audio: Render to new file** (the clip menu). It plays the clip's edits into a new file, which
becomes the clip's take, as Glue does today. The old file stays until **Remove unused audio**. Undo brings the edits
back.

### Playback

- Timeline tiles and launcher clips both read regions from one sorted table per clip, made by the snapshot builder.
  The player finds the region at the start of each 32-sample block. Nothing allocates, locks or frees.
- A clip with no edits takes the path it takes today, plus one check.
- Stretched audio is rendered per file, so moving or cutting a part needs no new render. A reversed region uses the
  reversed render that already exists. Each take in use has its own render.
- **Budget**: clips with no edits cost what they cost today. 16 tracks with 100 regions each add at most 0.2% of a
  core. A benchmark in `audio_engine_tests.cpp` fails above that. At most 1024 regions per clip.
- Waveforms on the timeline, audio export, bounce and glue all read `regionsOf()`. DAWproject export writes one audio
  clip per region.

## 3. The audio editor

**The ruler becomes clip time.** Today the editor's x axis is file time: the whole file runs across and the part that
plays is bright. Moved parts have no place in file time, so the axis becomes the clip's beats. A clip with no edits
looks as it does today: the unused file before and after it stays visible, dim.

| Do | How | Reaches |
|---|---|---|
| Cut | Slice tool (C) click. Shift+click: every hit, 1/16, 1/8, beat | Clip |
| Move a part | Drag it. Snaps; Alt is free | Clip |
| Trim a part | Drag its edge. The file's hidden audio comes back | Clip |
| Slip the audio inside a part | Alt+drag | Clip |
| Delete | Del leaves a gap. Shift+Del closes the gap | Clip |
| Pick a time range | Drag in the ruler strip. Del, Ctrl+C, Ctrl+V and Ctrl+D act on it | Clip |
| Keep only the range | Clip menu ▸ Crop to selection | Clip |
| Gain | Drag a part's top strip | Clip |
| Normalise | Clip menu | Clip |
| Fades | Drag a part's top corners, curve dot as on the timeline | Clip |
| Crossfade a seam | Drag the handle on the seam | Clip |
| Mute, Reverse a part | M, clip menu | Clip |
| Hear it while stopped | Click the ruler | Nothing changes |

The header holds the clip's settings: tempo, Stretch, sound type, pitch, gain, reverse, **Takes**, **∞ 3 copies**, the
Select and Slice tools. In the narrow panel editor they fold into one **Clip ▾**, as `AUDIO_UX.md` A2 describes.

Pitch stays per clip for now. A pitch per part would need a stretched render per part.

## 4. Takes

Takes move to the clip, so that linked copies play the same comp.

- `ArrangerClip::take` goes away. Each region names its take. **Every copy plays this take** goes away with it.
  This replaces DECISIONS §129.
- Each take keeps its own warp markers, so a comp can be quantized.
- Loading an older song: a tile's take becomes the take of its clip's region. Tiles of one clip that picked different
  takes get their own clips.
- Comping needs no timeline split any more. The tile stays one tile.

**Takes rows.** **Takes 3** in the header opens rows under the waveform, newest first. The top row, **Comp**, shows
what plays.

- **Drag across a take**: that stretch now plays that take. One undo step. Neighbours on the same take join.
- **Hear** on a row plays that take in place of the comp, in time. Nothing in the song changes.
- Up and Down with a part selected: use the take above or below.
- A take that ends early is hatched where it has no audio.
- A take can be renamed or deleted. Its file stays until **Remove unused audio**.

**Recording.** Each loop pass on an audio track becomes a take of one clip, in every record mode. Open question §8.3.

## 5. How audio is packed today

Checked in the code on 9 Nov.

| | How it works | Safe when moved? |
|---|---|---|
| **Original file** | Copied in on import, so the original can move or vanish | Yes |
| **Rack patch (.vcv)** | Files live in the module's patch storage (`audio/<hash>-<name>`). Rack packs that folder into the .vcv | Yes: one file |
| **Tessera project (.tessera)** | Plain JSON, plus a folder beside it named after it: `Song audio` | **No: two things to keep together, joined only by a name** |
| **Rack preset, saved selection, copy and paste to another patch** | Carry the song, not the files (Rack's source) | **No: clips come back Missing** |

Problems found:

1. **Renaming `Song.tessera` loses its audio**: the folder is found by the project's name.
2. **Moving the project without its folder loses its audio**, and nothing warns you at save time.
3. **Every later save hashes every file twice, on the UI thread** (`exportFiles`). With 1 GB of audio the editor freezes
   while it reads about 2 GB.
4. **The folder only grows**: files no longer used stay in it.
5. **A crash during save destroys the project**: the file is written in place.
6. **Find… accepts any file** and stores it under the old file's name. Picking the wrong file silently plays wrong audio.
7. **Find… fixes one file at a time.** Twenty missing files means twenty dialogs.
8. **A file name in a project is trusted.** `AudioSource::file` is joined to a folder path as it stands, so a crafted
   project could write outside the patch folder.
9. **The manual says presets save the whole song**, which is true of everything except the audio.

## 6. Proposed packing

**A `.tessera` becomes one file**: a ZIP with `project.json` and `audio/<hash>-<name>`, not compressed, since audio
barely shrinks and copying is fast.

- The working copy stays in patch storage, so playback and the .vcv don't change.
- **Save** runs on a worker thread and shows progress. It writes to a temporary file and renames it at the end, so a
  crash leaves the old project whole.
- **Open** treats anything that isn't a ZIP as an old project and still looks for its `<name> audio` folder. The next
  save makes one file and leaves the folder alone. Nothing is converted without a save. A song with no audio stays
  plain JSON.
- Extracting takes only names of the form `audio/<no slashes>` and checks each file's hash.
- The ZIP code needs a streaming reader and writer: `Zip.hpp` holds whole files in memory, which is too much for
  audio. Projects over 4 GB need the ZIP64 extension.

| When the user… | Today | Proposed |
|---|---|---|
| Moves or renames the project | Audio lost unless the folder follows and keeps its name | Works |
| Sends it to another computer | Must send both | Send one file |
| Saves again with no audio change | Reads every file, freezes | Rewrites in the background |
| Deletes clips, then saves | Old audio stays forever | Only audio the song has goes in |
| Loses power during save | Project damaged | Old project is safe |
| Moves or deletes the original WAV | Fine | Fine |
| Moves, renames or mails the .vcv | Fine | Fine |
| Loads a preset in another patch | Missing, one dialog per file | Missing, one Find for all (below) |
| Pastes into another Rack session | Missing, one dialog per file | Missing, one Find for all |

**Finding missing files.** Find… asks for a folder.

- It then searches that folder and its subfolders for **every** missing file: by hash first, then by name.
- A file with the wrong hash asks first: "This isn't the same recording. Use it anyway?" Cuts still point at the same
  frames, so a shorter file leaves silence.
- It searches other Tesseras in the patch first, by hash. That costs nothing and needs no shared cache, which the
  user turned down (DECISIONS §102).
- No original paths are saved, so a shared patch doesn't reveal your folders.

**Automatic clean-up** (`removeUnusedFiles` when a patch loads) doesn't run on a patch saved by a newer Tessera. Such a
patch "loads what it can", so audio this version can't read about would count as unused and be deleted.

The manual and the Audio files list say plainly that presets don't carry audio.

**Patch size.** Rack repacks all patch storage on every patch save, so big patches save slowly. Non-destructive edits
add no audio, which helps. To measure in phase 1: how long a patch with 1 GB of audio takes. Recording to FLAC instead
of WAV could halve it if the number is bad.

## 7. Phases

Each phase ends installed, tested and with the manual updated.

1. **Files.** One-file projects, Find for all, hash checks, name checks, the clean-up guard, background save.
2. **Regions.** The model, playback, benchmark, waveform on the timeline, clip-time editor, Slice, move, trim, delete,
   gain, fades, crossfades, ∞ copies in the header.
3. **Takes.** Takes on the clip, loading older songs, rows, drag to comp, Hear, passes as takes.
4. **Utilities.** Header settings, range copy and paste, Crop, preview while stopped, Render to new file, Quantize.

## 8. Answered (9 Nov)

1. `.tessera` is one file when the song has audio. Existing projects and folders keep working (DECISIONS §147).
2. Del leaves a gap, Shift+Del closes it.
3. Every loop pass on an audio track is kept as a take.
4. The clip holds the take choice.
