#pragma once

#include "../../model/TrackCommands.hpp"
#include "../EditorContext.hpp"

#include <rack.hpp>

/// Track operations shared by header clicks, context menus and keyboard shortcuts, so each exists once.
/// Operations on "the clicked track" act on the whole selection when the clicked track is part of it
/// (EDITOR_UX §6.2: select several headers to mute, solo or colour them together).
namespace tessera::trackActions {

/// "Auto (3)" or "3": the track's voice setting with the channel count it sends.
std::string voicesLabel(const Song& song, const Track& track);

/// The tracks an operation on `clicked` applies to: the selection if `clicked` is in it, else just `clicked`.
std::vector<TrackId> targets(EditorContext& ctx, TrackId clicked);
/// The selected tracks, or the track the piano roll shows when nothing is multi-selected.
std::vector<TrackId> selection(EditorContext& ctx);

/// Selects one track (header click), a range (Shift) or toggles one (Ctrl/Cmd).
void click(EditorContext& ctx, TrackId id, bool range, bool toggle);

/// Adds a track after the selected one (or at the end) and selects it.
/// A song holds at most kMaxTracks tracks; adding past that does nothing and says so.
constexpr const char* kTracksFull = "A song holds at most 64 tracks";
bool canAdd(const Song& song, size_t count);
void add(EditorContext& ctx, TrackType type);
void duplicateSelected(EditorContext& ctx);
void deleteSelected(EditorContext& ctx);
void groupSelected(EditorContext& ctx);

void toggleMute(EditorContext& ctx, TrackId clicked);
/// Toggles solo, keeping the other soloed tracks with `additive` (a click on S) or un-soloing them (Shift+click).
void toggleSolo(EditorContext& ctx, TrackId clicked, bool additive);
/// Arms for recording the same way as solo: exclusive, or added with `additive`.
void toggleArm(EditorContext& ctx, TrackId clicked, bool additive);
/// A new automation lane on the track's first free AUTO channel; shows the track's lanes. Returns its ID, invalid
/// when every channel has a lane.
AutomationId addAutomationLane(EditorContext& ctx, TrackId track);
/// A new lane that waits for a knob touched in the rack: it maps the knob, takes its name, and records it when the
/// track records.
void automateKnob(EditorContext& ctx, TrackId track);
/// Arm, input, MIDI channel and monitoring items for a track menu.
void appendRecordMenu(rack::ui::Menu* menu, EditorContext& ctx, const std::vector<TrackId>& ids);
/// The voice counts (Auto, 1..16) for the track menu and the inspector.
void appendVoicesItems(rack::ui::Menu* menu, EditorContext& ctx, const std::vector<TrackId>& ids);
/// What a track plays besides its clips: nothing, the external inputs, or another track's notes after or before
/// its devices. Tracks that would make a loop are greyed out.
void appendInputItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId id);
/// "None", "External (MIDI)", "MELODY (after devices)".
std::string inputLabel(const Song& song, const Track& track);
void rename(EditorContext& ctx, TrackId id, const std::string& name);
void renameGroup(EditorContext& ctx, GroupId id, const std::string& name);

/// Moves tracks (drag in the headers). See moveTracks() for `before` and `group`.
void move(EditorContext& ctx, const std::vector<TrackId>& ids, int before, std::optional<GroupId> group);

/// Context menus. `startRename` opens the inline name field in the headers.
void appendTrackMenu(rack::ui::Menu* menu, EditorContext& ctx, TrackId clicked, std::function<void()> startRename);
void appendGroupMenu(rack::ui::Menu* menu, EditorContext& ctx, GroupId group, std::function<void()> startRename);

/// Solo command for a set of tracks: toggles them, and un-soloes every other track unless `additive`.
CommandPtr soloCommand(const Song& song, const std::vector<TrackId>& ids, bool additive);

} // namespace tessera::trackActions
