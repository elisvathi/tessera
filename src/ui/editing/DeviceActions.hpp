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
void openModulatorBrowser(EditorContext& ctx, TrackId track, DeviceId device = DeviceId());

void openDeviceMenu(EditorContext& ctx, DeviceId device);
void openKnobMenu(EditorContext& ctx, const ParamRef& ref);
void openModulatorMenu(EditorContext& ctx, ModulatorId id);
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
