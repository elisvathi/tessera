#pragma once

#include "../../model/DrumKit.hpp"
#include "../EditorContext.hpp"
#include "NoteTarget.hpp"

#include <rack.hpp>

#include <functional>
#include <string>

/// Drum lane operations shared by the step grid's clicks, menus and keys (DESIGN.md §9), so each exists once.
/// Lanes are addressed by track and lane index; patterns act on the clip the editor shows for the track.
namespace tessera::drumActions {

/// Changes one lane's settings as one undo step.
void editLane(EditorContext& ctx, TrackId track, int lane, const std::function<void(DrumLane&)>& change,
              const std::string& name);
/// Replaces every lane (kit presets, adding and deleting lanes) as one undo step.
void setLanes(EditorContext& ctx, TrackId track, std::vector<DrumLane> lanes, const std::string& name);

/// A pattern tool on one lane: gets the clip's notes, the lane's pitch, region and step length, and returns
/// the new notes. One undo step.
using PatternEdit = std::function<edits::NoteList(const edits::NoteList& notes, int pitch, LaneRegion region, Tick step,
                                                  const edits::IdSource& newId)>;
void editPattern(EditorContext& ctx, TrackId track, int lane, const PatternEdit& edit, const std::string& name);

/// Plays the lane once on the track's outputs.
void audition(EditorContext& ctx, TrackId track, int lane);
/// A new lane after the others, on a note and an output no other lane uses.
void addLane(EditorContext& ctx, TrackId track);

void appendLaneMenu(rack::ui::Menu* menu, EditorContext& ctx, TrackId track, int lane,
                    std::function<void()> startRename);
/// The selected steps' menu: velocity, the note operators (chance, repeats, occurrence, recurrence), mute and
/// delete. It is the way to the operators where the inspector is hidden (the in-panel editor).
void appendStepMenu(rack::ui::Menu* menu, NoteTarget& steps);
/// Kit presets and adding lanes.
void appendKitMenu(rack::ui::Menu* menu, EditorContext& ctx, TrackId track);

} // namespace tessera::drumActions
