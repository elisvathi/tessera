#pragma once

#include "../../model/AutomationEdits.hpp"
#include "../../model/DeviceCommands.hpp"
#include "../../model/DeviceTypes.hpp"
#include "../EditorContext.hpp"

#include <string>

/// The device strip's menus and edits (docs/DEVICES.md §1.1), shared by the strip and the modulator popover.
namespace tessera::deviceActions {

/// The track whose devices show (the selected one), or nullptr.
const Track* track(EditorContext& ctx);

/// Whether the device strip shows open. A track without devices keeps it closed, so a new track's notes
/// get the room, unless the strip was opened by hand while that track was selected.
bool stripOpen(EditorContext& ctx);
/// Opens or closes the device strip for the selected track (the Devices button, F3, the closed bar).
void setStripOpen(EditorContext& ctx, bool open);

/// What a knob or setting is, or nullptr when it no longer exists.
const devices::ParamInfo* infoOf(const Song& song, const ParamRef& ref);
/// "Arpeggiator · Gate", "LFO 1 · Rate".
std::string paramName(const Song& song, const ParamRef& ref);
/// "Arpeggiator · Gate", "Track 2 · AUTO 3", "VCO · Frequency".
std::string targetName(const Song& song, const Modulation& route);
/// Where a modulator lives, as its row says: "song", "track", or its device's name.
std::string whereName(const Song& song, const ModulatorPlace& place);
/// The modulator's colour, as the theme gives it.
NVGcolor colorOf(ModulatorId id);
/// An automation lane's colour: its own, or its track's.
NVGcolor laneColor(const Song& song, const FoundLane& lane);

/// The searchable list of devices; the pick goes in at `index` of the track's chain (-1 = the end).
void openDeviceBrowser(EditorContext& ctx, TrackId track, int index);
/// The searchable list of modulators, with chips for where the new one lives (song, this track, a device).
/// `device` lights that device's chip first.
/// The device list, adding the picked device to a playhead's branch at `index` (-1 = the end).
void openBranchBrowser(EditorContext& ctx, PlayheadId playhead, int index);
void openModulatorBrowser(EditorContext& ctx, TrackId track, DeviceId device = DeviceId());

void openDeviceMenu(EditorContext& ctx, DeviceId device);
void openKnobMenu(EditorContext& ctx, const ParamRef& ref);
/// "Drift per pass" on a motion knob: a Steps modulator on its track, set to Per pass and mapped four steps up the
/// knob, either rising then falling back (`bounce`) or rising then starting again. One undo step.
void addDrift(EditorContext& ctx, const ParamRef& ref, bool bounce);
void openModulatorMenu(EditorContext& ctx, ModulatorId id);
/// A new Motion device on the selected track (before its Playheads) with `notes` of `clip` joining it, as one undo
/// step; the strip opens on it.
void newMotion(EditorContext& ctx, ClipId clip, const std::vector<NoteId>& notes);
/// Which Motion device `notes` of `clip` move with: the track's Motion devices (`current` ticked), a new one, or
/// none (the inspector's MOTION line, a note's right-click menu).
void openMotionMenu(EditorContext& ctx, ClipId clip, const std::vector<NoteId>& notes, DeviceId current);
/// The same choices, added to `menu` (a note's Motion › submenu).
void appendMotionItems(rack::ui::Menu* menu, EditorContext& ctx, ClipId clip, const std::vector<NoteId>& notes,
                       DeviceId current);
/// A playhead's menu: rename, channel, mute, solo, its rarer settings (Notes, On a jump, Length, Path...), duplicate,
/// delete.
void openPlayheadMenu(EditorContext& ctx, PlayheadId id);
/// The merged Direction dropdown (direction, or a step order with its step size), as one undo step.
void openDirectionMenu(EditorContext& ctx, PlayheadId id);
/// What the Direction dropdown shows for a playhead ("Forward", "Outside in").
std::string directionText(const Playhead& p);
/// Shows a Curve modulator's shape in the piano roll's lower pane.
void showCurve(EditorContext& ctx, ModulatorId id);
/// Shows the Curve modulator moving `target` in the lower pane; with none yet, an empty curve for it: the first point
/// drawn adds the modulator.
void showCurveFor(EditorContext& ctx, const ParamRef& target);
/// The first Curve modulator turning `target`, or an invalid ID.
ModulatorId curveOn(const Song& song, const ParamRef& target);

/// The selected track's Playheads device when it has one (on or off), else nullptr.
const Device* playheadsDevice(EditorContext& ctx);
/// The playhead the editor shows (the Playheads card, the ruler's segment, the lower pane's curves): the one last
/// picked on the selected track, else its first. nullptr when the track has no playheads.
const Playhead* shownPlayhead(EditorContext& ctx);
/// Turns the shown playhead off or on (Shift+M, the dot on its chip); `solo` solos it instead (Alt-click).
void togglePlayheadMute(EditorContext& ctx, bool solo = false);
/// A choice knob's choices, the current one ticked.
void openChoiceMenu(EditorContext& ctx, const ParamRef& ref);
/// Type a value in the knob's units ("75", "1/16" for choices by name).
void openTyping(EditorContext& ctx, const ParamRef& ref);
/// "CV out": an AUTO output channel, or a Rack knob by learn.
void openCvOutMenu(EditorContext& ctx, ModulatorId id);

/// One undo step setting a knob (normalized).
void setKnob(EditorContext& ctx, const ParamRef& ref, float value);

/// Adds a lane that turns the knob, on a track (arranger time) or in a clip, and shows it.
void automateKnob(EditorContext& ctx, const ParamRef& ref, LaneOwner owner);
/// Brings a lane into view: the track's lanes in the arranger, or the clip's lower lane in the piano roll.
void showLane(EditorContext& ctx, LaneOwner owner, AutomationId lane);

} // namespace tessera::deviceActions
