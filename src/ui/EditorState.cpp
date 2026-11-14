#include "EditorState.hpp"

#include "../model/Json.hpp"

#include <algorithm>

namespace tessera {

using namespace json;

namespace {

json_t* pair(double a, double b) { return json_pack("[ff]", a, b); }

const char* const kPaneNames[] = {"top", "detail", "mixer"};
const char* const kGhostNames[] = {"none", "track", "allTracks", "otherClips"};
const char* const kNoteLaneNames[] = {"velocity", "release", "chance", "velocitySpread", "repeats",
                                      "micro",    "pitch",   "timbre", "pressure"};
const char* const kFollowNames[] = {"off", "page", "continuous"};
const char* const kToolNames[] = {"pointer", "draw", "paint", "erase", "slice", "mute", "velocity"};

template <typename E, size_t N>
E fromName(const json_t* j, const char* const (&names)[N], E fallback) {
    std::string s = asString(j, "");
    for (size_t i = 0; i < N; i++)
        if (s == names[i])
            return E(i);
    return fallback;
}

/// Writes where the views are and the sizes and folds of the rows into `j`.
void viewsToJson(const EditorState& s, json_t* j) {
    setNumber(j, "topFraction", s.topFraction);
    setBool(j, "inspector", s.inspectorOpen);
    setString(j, "focus", kPaneNames[int(s.focus)]);
    if (s.maximized)
        setString(j, "maximized", kPaneNames[int(*s.maximized)]);
    setBool(j, "clipsWide", s.clipsWide);
    set(j, "arranger", pair(s.arranger.left, s.arranger.pxPerTick));
    set(j, "detail", pair(s.detail.left, s.detail.pxPerTick));
    set(j, "pitch", pair(s.pitch.top, s.pitch.rowHeight));
    setNumber(j, "pitchViewHeight", s.pitchViewHeight);
    setNumber(j, "trackScroll", s.trackScroll);
    json_t* heights = json_array();
    for (const auto& [id, h] : s.trackHeights)
        json_array_append_new(heights, json_pack("[If]", static_cast<json_int_t>(id), h));
    set(j, "trackHeights", heights);
    json_t* lanes = json_array();
    for (const auto& [id, h] : s.laneHeights)
        json_array_append_new(lanes, json_pack("[If]", static_cast<json_int_t>(id), h));
    set(j, "laneHeights", lanes);
    json_t* folded = json_array();
    for (uint64_t id : s.foldedGroups)
        json_array_append_new(folded, json_integer(static_cast<json_int_t>(id)));
    set(j, "foldedGroups", folded);
    json_t* open = json_array();
    for (uint64_t id : s.openAutomation)
        json_array_append_new(open, json_integer(static_cast<json_int_t>(id)));
    set(j, "openAutomation", open);
}

/// Reads what viewsToJson wrote; keys that aren't in `j` keep what `s` has. Older patches held these per editor.
void viewsFromJson(const json_t* j, EditorState& s) {
    auto readPair = [&](const char* key, double& a, double& b) {
        const json_t* p = get(j, key);
        a = numberAt(p, 0, a);
        b = numberAt(p, 1, b);
    };
    s.topFraction = std::clamp(getFloat(j, "topFraction", s.topFraction), 0.f, 1.f);
    s.inspectorOpen = getBool(j, "inspector", s.inspectorOpen);
    s.focus = fromName(get(j, "focus"), kPaneNames, s.focus);
    if (json_is_string(get(j, "maximized")))
        s.maximized = fromName(get(j, "maximized"), kPaneNames, Pane::Detail);
    s.clipsWide = getBool(j, "clipsWide", s.clipsWide);
    // The mixer used to be a pane of its own, under the notes; it now shares the notes' pane.
    if (getBool(j, "mixerMax", false) || s.maximized == Pane::Mixer)
        s.maximized = Pane::Detail;
    if (s.focus == Pane::Mixer)
        s.focus = Pane::Detail;
    readPair("arranger", s.arranger.left, s.arranger.pxPerTick);
    readPair("detail", s.detail.left, s.detail.pxPerTick);
    readPair("pitch", s.pitch.top, s.pitch.rowHeight);
    s.pitchViewHeight = getNumber(j, "pitchViewHeight", s.pitchViewHeight);
    s.trackScroll = getNumber(j, "trackScroll", s.trackScroll);
    forEach(get(j, "trackHeights"), [&](const json_t* p) {
        int64_t id = intAt(p, 0, 0);
        if (id > 0)
            s.trackHeights[uint64_t(id)] = numberAt(p, 1, EditorState::kDefaultTrackHeight);
    });
    forEach(get(j, "laneHeights"), [&](const json_t* p) {
        int64_t id = intAt(p, 0, 0);
        if (id > 0)
            s.laneHeights[uint64_t(id)] = numberAt(p, 1, EditorState::kDefaultTrackHeight);
    });
    forEach(get(j, "foldedGroups"), [&](const json_t* g) {
        if (asInt(g, 0) > 0)
            s.foldedGroups.insert(uint64_t(asInt(g, 0)));
    });
    forEach(get(j, "openAutomation"), [&](const json_t* t) {
        if (asInt(t, 0) > 0)
            s.openAutomation.insert(uint64_t(asInt(t, 0)));
    });
}

json_t* viewToJson(const ViewState& v) {
    json_t* j = json_object();
    setNumber(j, "headerWidth", v.headerWidth);
    set(j, "launcherScroll", pair(v.launcherScrollX, v.launcherScrollY));
    setNumber(j, "launcherPanelWidth", v.launcherPanelWidth);
    setNumber(j, "mixerHeight", v.mixerHeight);
    setNumber(j, "curveHeight", v.curveHeight);
    setInt(j, "stripRows", v.stripRows);
    return j;
}

ViewState viewFromJson(const json_t* j, ViewState v = ViewState()) {
    if (!json_is_object(j))
        return v;
    v.headerWidth = std::clamp(getFloat(j, "headerWidth", v.headerWidth), 100.f, 400.f);
    // Patches from before the drum editor became the piano roll saved the same choice as "drumSteps".
    v.mixerHeight = std::clamp(getFloat(j, "mixerHeight", v.mixerHeight), 40.f, 4000.f);
    v.launcherPanelWidth = std::clamp(getFloat(j, "launcherPanelWidth", v.launcherPanelWidth), 40.f, 4000.f);
    v.launcherScrollX = std::max(0.0, numberAt(get(j, "launcherScroll"), 0, 0));
    v.launcherScrollY = std::max(0.0, numberAt(get(j, "launcherScroll"), 1, 0));
    v.curveHeight = std::clamp(getNumber(j, "curveHeight", 0), 0.0, 4000.0);
    v.stripRows = int(std::clamp<int64_t>(getInt(j, "stripRows", v.stripRows), 1, 8));
    return v;
}

} // namespace

json_t* EditorState::toJson() const {
    json_t* j = json_object();
    setInt(j, "selectedTrack", static_cast<json_int_t>(selectedTrack.value));
    setInt(j, "selectedClip", static_cast<json_int_t>(selectedClip.value));
    setString(j, "follow", kFollowNames[int(follow)]);
    setNumber(j, "uiScale", uiScale);
    setInt(j, "panelHp", panelHp);
    setString(j, "tool", kToolNames[int(tool)]);
    json_t* snapJ = json_object();
    setBool(snapJ, "enabled", snap.enabled);
    setBool(snapJ, "adaptive", snap.adaptive);
    setInt(snapJ, "division", int(snap.division));
    setInt(snapJ, "feel", int(snap.feel));
    setBool(snapJ, "events", snap.events);
    set(j, "snap", snapJ);
    setInt(j, "lastLength", lastLength);
    setNumber(j, "lastVelocity", lastVelocity);
    setInt(j, "lastChannel", lastChannel);
    setBool(j, "audition", audition);
    setBool(j, "noteBadges", noteBadges);
    setBool(j, "showSwing", showSwing);
    setBool(j, "stackClips", stackClips);
    set(j, "export", exportOptions.toJson());
    setString(j, "ghosts", kGhostNames[int(ghosts)]);
    setInt(j, "ghostTrack", static_cast<json_int_t>(ghostTrack.value));
    setBool(j, "transformBox", transformBox);
    setBool(j, "scaleHighlight", scaleHighlight);
    setBool(j, "scaleSnap", scaleSnap);
    setBool(j, "scaleFold", scaleFold);
    setBool(j, "keyMovesNotes", keyMovesNotes);
    setBool(j, "ripple", ripple);
    setBool(j, "automationFollowsClips", automationFollowsClips);
    json_t* chordJ = json_object();
    setInt(chordJ, "shape", int(chord.shape));
    setInt(chordJ, "inversion", chord.inversion);
    setInt(chordJ, "voicing", int(chord.voicing));
    set(j, "chord", chordJ);
    setBool(j, "deviceStrip", deviceStrip);
    setBool(j, "drumLanes", drumLanes);
    setBool(j, "mixerOpen", mixerOpen);
    setInt(j, "lowerLane", static_cast<json_int_t>(lowerLane));
    setString(j, "noteLane", kNoteLaneNames[int(noteLane)]);
    viewsToJson(*this, j);
    set(j, "overlay", viewToJson(overlay));
    set(j, "panel", viewToJson(panel));
    return j;
}

EditorState EditorState::fromJson(const json_t* j) {
    EditorState s;
    if (!json_is_object(j))
        return s;
    s.selectedTrack = TrackId(uint64_t(std::max<int64_t>(0, getInt(j, "selectedTrack", 0))));
    s.selectedClip = ClipId(uint64_t(std::max<int64_t>(0, getInt(j, "selectedClip", 0))));
    // Older patches kept what is open per surface. The full editor's choice wins, and devices open in either stay open.
    const json_t* oldOverlay = get(j, "overlay");
    const json_t* oldPanel = get(j, "panel");
    s.deviceStrip = getBool(
        j, "deviceStrip", getBool(oldOverlay, "deviceStrip", s.deviceStrip) || getBool(oldPanel, "deviceStrip", false));
    s.drumLanes = getBool(j, "drumLanes", getBool(oldOverlay, "drumLanes", s.drumLanes));
    bool oldMixer = getBool(oldOverlay, "mixerOpen", s.mixerOpen) || getBool(oldOverlay, "mixerMax", false) ||
                    getString(oldOverlay, "maximized", "") == kPaneNames[int(Pane::Mixer)];
    s.mixerOpen = getBool(j, "mixerOpen", oldMixer);
    s.lowerLane = uint64_t(std::max<int64_t>(0, getInt(j, "lowerLane", getInt(oldOverlay, "lowerLane", 0))));
    s.noteLane =
        fromName(get(j, "noteLane") ? get(j, "noteLane") : get(oldOverlay, "noteLane"), kNoteLaneNames, s.noteLane);
    if (s.noteLane == NoteLane::VelocitySpread)
        s.noteLane = NoteLane::Velocity; // varying each play is no longer edited in the editor
    s.follow = fromName(get(j, "follow"), kFollowNames, s.follow);
    s.uiScale = std::clamp(getFloat(j, "uiScale", s.uiScale), 0.f, 2.f);
    s.panelHp = int(std::clamp<int64_t>(getInt(j, "panelHp", s.panelHp), kPanelMinHp, kPanelMaxHp));
    s.tool = fromName(get(j, "tool"), kToolNames, s.tool);
    if (const json_t* snapJ = get(j, "snap")) {
        s.snap.enabled = getBool(snapJ, "enabled", s.snap.enabled);
        s.snap.adaptive = getBool(snapJ, "adaptive", s.snap.adaptive);
        s.snap.division = GridDivision(
            std::clamp<int64_t>(getInt(snapJ, "division", int(s.snap.division)), 0, int(GridDivision::SixtyFourth)));
        s.snap.feel = GridFeel(std::clamp<int64_t>(getInt(snapJ, "feel", int(s.snap.feel)), 0, int(GridFeel::Dotted)));
        s.snap.events = getBool(snapJ, "events", s.snap.events);
    }
    s.lastLength = std::max<int64_t>(1, getInt(j, "lastLength", s.lastLength));
    s.lastVelocity = std::clamp(getFloat(j, "lastVelocity", s.lastVelocity), 0.f, 1.f);
    s.lastChannel = uint8_t(std::clamp<int64_t>(getInt(j, "lastChannel", s.lastChannel), 1, kMaxNoteChannel));
    s.audition = getBool(j, "audition", s.audition);
    s.noteBadges = getBool(j, "noteBadges", s.noteBadges);
    s.showSwing = getBool(j, "showSwing", s.showSwing);
    s.stackClips = getBool(j, "stackClips", s.stackClips);
    s.exportOptions = render::Options::fromJson(get(j, "export"));
    s.ghosts = fromName(get(j, "ghosts"), kGhostNames, s.ghosts);
    s.ghostTrack = TrackId(uint64_t(std::max<int64_t>(0, getInt(j, "ghostTrack", 0))));
    s.transformBox = getBool(j, "transformBox", s.transformBox);
    s.scaleHighlight = getBool(j, "scaleHighlight", s.scaleHighlight);
    s.scaleSnap = getBool(j, "scaleSnap", s.scaleSnap);
    s.scaleFold = getBool(j, "scaleFold", s.scaleFold);
    s.keyMovesNotes = getBool(j, "keyMovesNotes", s.keyMovesNotes);
    s.ripple = getBool(j, "ripple", s.ripple);
    s.automationFollowsClips = getBool(j, "automationFollowsClips", s.automationFollowsClips);
    if (const json_t* chordJ = get(j, "chord")) {
        s.chord.shape = ChordShape(std::clamp<int64_t>(getInt(chordJ, "shape", 0), 0, int(ChordShape::Octave)));
        s.chord.inversion = int(std::clamp<int64_t>(getInt(chordJ, "inversion", 0), 0, 4));
        s.chord.voicing = ChordVoicing(std::clamp<int64_t>(getInt(chordJ, "voicing", 0), 0, int(ChordVoicing::Drop2)));
    }
    // Milestone 3 saved one flat view; that was the overlay.
    viewsFromJson(get(j, "overlay"), s);
    viewsFromJson(j, s);
    s.overlay = viewFromJson(json_is_object(get(j, "overlay")) ? get(j, "overlay") : j);
    s.panel = viewFromJson(get(j, "panel"), ViewState::compact());
    return s;
}

void ViewState::forgetSong() {
    launcherScrollX = 0;
    launcherScrollY = 0;
}

void EditorState::closeCurveIfTrackChanged() {
    if (lowerLane != kCurveLane || !curveOpenedOn.valid() || curveOpenedOn == selectedTrack)
        return;
    lowerLane = 0;
    curveModulator = 0;
    curveTarget = ParamRef();
    curveOpenedOn = TrackId();
}

void EditorState::forgetSong() {
    arranger = detail = curveTime = TimeAxis::State{0, 0};
    pitch = PitchAxis::State{0, 0};
    trackScroll = pitchViewHeight = 0;
    curveTimeFor = 0;
    trackHeights.clear();
    laneHeights.clear();
    foldedGroups.clear();
    foldedInputs.clear();
    openAutomation.clear();
    lowerLane = 0;
    curveModulator = 0;
    curveTarget = ParamRef();
    curveOpenedOn = TrackId();
    openModulator = selectedDevice = stripOpenedFor = selectedPlayhead = 0;
    selectedTrack = TrackId();
    selectedClip = ClipId();
    selectedTracks.clear();
    layered.clear();
    if (ghosts == GhostSource::Track)
        ghosts = GhostSource::None;
    ghostTrack = TrackId();
    keyPreview = KeyPreview();
    selection = NoteSelection();
    clipSelection.clear();
    autoSelection = AutoSelection();
    stampShape.reset();
    range = ArrangerRange();
    stepInput = false;
    stepCursor = 0;
    overlay.forgetSong();
    panel.forgetSong();
}

} // namespace tessera
