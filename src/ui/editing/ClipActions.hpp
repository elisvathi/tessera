#pragma once

#include "../../model/Song.hpp"
#include "../EditorContext.hpp"

#include <rack.hpp>

#include <functional>
#include <string>

/// Clip-launcher operations shared by clicks, context menus and keyboard shortcuts (DESIGN.md §10), so each
/// exists once. Slots are addressed by track and scene index.
namespace tessera::clipActions {

/// The clip the detail editor shows for a track: the selected clip when it sits on that track (in a slot or
/// on the timeline), else the clip the launcher plays on it, else its first clip. Invalid when the track has none.
ClipId shownClip(EditorContext& ctx, const Track& track);
/// The scene index holding `clip` on `track`, or -1.
int slotOf(const Track& track, ClipId clip);

/// Selects a clip (and its track) for the detail editor.
void select(EditorContext& ctx, TrackId track, ClipId clip);
/// Creates a one-bar clip in an empty slot and selects it.
void create(EditorContext& ctx, TrackId track, int slot);
/// Creates a clip in the track's first empty slot (adding a scene when all are full).
void createInFirstEmpty(EditorContext& ctx, TrackId track);
void duplicate(EditorContext& ctx, TrackId track, int slot);
void remove(EditorContext& ctx, TrackId track, int slot);
void move(EditorContext& ctx, TrackId fromTrack, int fromSlot, TrackId toTrack, int toSlot, bool copy);
void rename(EditorContext& ctx, ClipId clip, const std::string& name);
/// "Length ▸": makes the clip twice as long, half as long, or a typed percentage (notes, loop, automation and
/// every timeline use), as one undo step.
void appendScaleMenu(rack::ui::Menu* menu, EditorContext& ctx, ClipId clip);
/// Opens a name field for the clip under the mouse; Enter or closing it renames the clip (one undo step).
void startRename(EditorContext& ctx, ClipId clip);

void exportClip(EditorContext& ctx, ClipId clip);
void importClip(EditorContext& ctx, TrackId track, int slot);

void insertScene(EditorContext& ctx, int index);

/// "Length: 2 bars" for a clip's loop (or the clip, when it doesn't loop).
std::string lengthLabel(const Song& song, const Clip& clip);
/// Loop length choices for a clip: whole bars, double (copying the notes) and halve.
void openLengthMenu(EditorContext& ctx, ClipId clip);
/// The length menu's items (loop lengths, Double, Halve), for a menu of its own or the notes header's clip chip.
void appendLengthItems(rack::ui::Menu* menu, EditorContext& ctx, ClipId clip);

/// The clip chip that starts the piano roll's and the drum editor's header: "Bass  ·  Clip 1  ·  2 bars", or just
/// the track's name (`shortText`, for a narrow header).
std::string chipText(EditorContext& ctx, ClipId clip, bool shortText);
/// The clip chip's menu: which track shows, then rename, loop, length, save and export for `clip` (on
/// `track`), or "Create a clip" when there is none.
void openChipMenu(EditorContext& ctx, TrackId track, ClipId clip);

/// "1/16", "1 bar", ... for menus and buttons.
std::string quantizeLabel(LaunchQuantize q);

/// Context menus. The scene menu's `startRename` opens the scene's name field.
void appendClipMenu(rack::ui::Menu* menu, EditorContext& ctx, TrackId track, int slot);
void appendEmptySlotMenu(rack::ui::Menu* menu, EditorContext& ctx, TrackId track, int slot);
void appendSceneMenu(rack::ui::Menu* menu, EditorContext& ctx, int scene, std::function<void()> startRename);
/// The song's launch quantize choices.
void appendQuantizeMenu(rack::ui::Menu* menu, EditorContext& ctx);

/// Opens several clips together in the piano roll (EDITOR_UX §4.9, layered editing): `open` is
/// edited, the others show behind it in their colours, one click away.
void editTogether(EditorContext& ctx, const std::vector<ClipId>& clips, TrackId track, ClipId open);
/// The track a clip is on (in a slot or on the timeline), or an invalid ID.
TrackId trackOf(const Song& song, ClipId clip);

} // namespace tessera::clipActions
