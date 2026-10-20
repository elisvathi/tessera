#include "EditorState.hpp"

#include "../model/Json.hpp"

#include <algorithm>

namespace tessera {

using namespace json;

namespace {

json_t* pair(double a, double b) { return json_pack("[ff]", a, b); }

const char* const kPaneNames[] = {"top", "detail"};
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

json_t* viewToJson(const ViewState& v) {
    json_t* j = json_object();
    setNumber(j, "topFraction", v.topFraction);
    setNumber(j, "headerWidth", v.headerWidth);
    setBool(j, "inspector", v.inspectorOpen);
    setString(j, "focus", kPaneNames[int(v.focus)]);
    if (v.maximized)
        setString(j, "maximized", kPaneNames[int(*v.maximized)]);
    setBool(j, "drumSteps", v.drumSteps);
    set(j, "launcherScroll", pair(v.launcherScrollX, v.launcherScrollY));
    set(j, "arranger", pair(v.arranger.left, v.arranger.pxPerTick));
    set(j, "detail", pair(v.detail.left, v.detail.pxPerTick));
    set(j, "pitch", pair(v.pitch.top, v.pitch.rowHeight));
    setNumber(j, "trackScroll", v.trackScroll);
    json_t* heights = json_array();
    for (const auto& [id, h] : v.trackHeights)
        json_array_append_new(heights, json_pack("[If]", json_int_t(id), h));
    set(j, "trackHeights", heights);
    json_t* folded = json_array();
    for (uint64_t id : v.foldedGroups)
        json_array_append_new(folded, json_integer(json_int_t(id)));
    set(j, "foldedGroups", folded);
    json_t* open = json_array();
    for (uint64_t id : v.openAutomation)
        json_array_append_new(open, json_integer(json_int_t(id)));
    set(j, "openAutomation", open);
    setBool(j, "deviceStrip", v.deviceStrip);
    setNumber(j, "launcherPanelWidth", v.launcherPanelWidth);
    setBool(j, "clipsWide", v.clipsWide);
    setInt(j, "lowerLane", json_int_t(v.lowerLane));
    setString(j, "noteLane", kNoteLaneNames[int(v.noteLane)]);
    json_t* lanes = json_array();
    for (const auto& [id, h] : v.laneHeights)
        json_array_append_new(lanes, json_pack("[If]", json_int_t(id), h));
    set(j, "laneHeights", lanes);
    return j;
}

ViewState viewFromJson(const json_t* j, ViewState v = ViewState()) {
    if (!json_is_object(j))
        return v;
    v.topFraction = std::clamp(getFloat(j, "topFraction", v.topFraction), 0.f, 1.f);
    v.headerWidth = std::clamp(getFloat(j, "headerWidth", v.headerWidth), 100.f, 400.f);
    v.inspectorOpen = getBool(j, "inspector", v.inspectorOpen);
    v.focus = fromName(get(j, "focus"), kPaneNames, v.focus);
    if (json_is_string(get(j, "maximized")))
        v.maximized = fromName(get(j, "maximized"), kPaneNames, Pane::Detail);
    v.drumSteps = getBool(j, "drumSteps", v.drumSteps);
    v.clipsWide = getBool(j, "clipsWide", v.clipsWide);
    v.deviceStrip = getBool(j, "deviceStrip", v.deviceStrip);
    v.launcherPanelWidth = std::clamp(getFloat(j, "launcherPanelWidth", v.launcherPanelWidth), 40.f, 4000.f);
    v.launcherScrollX = std::max(0.0, numberAt(get(j, "launcherScroll"), 0, 0));
    v.launcherScrollY = std::max(0.0, numberAt(get(j, "launcherScroll"), 1, 0));
    auto readPair = [&](const char* key, double& a, double& b) {
        const json_t* p = get(j, key);
        a = numberAt(p, 0, a);
        b = numberAt(p, 1, b);
    };
    readPair("arranger", v.arranger.left, v.arranger.pxPerTick);
    readPair("detail", v.detail.left, v.detail.pxPerTick);
    readPair("pitch", v.pitch.top, v.pitch.rowHeight);
    v.trackScroll = getNumber(j, "trackScroll", v.trackScroll);
    forEach(get(j, "trackHeights"), [&](const json_t* p) {
        int64_t id = intAt(p, 0, 0);
        if (id > 0)
            v.trackHeights[uint64_t(id)] = numberAt(p, 1, v.defaultTrackHeight);
    });
    forEach(get(j, "foldedGroups"), [&](const json_t* g) {
        if (asInt(g, 0) > 0)
            v.foldedGroups.insert(uint64_t(asInt(g, 0)));
    });
    v.lowerLane = uint64_t(std::max<int64_t>(0, getInt(j, "lowerLane", 0)));
    v.noteLane = fromName(get(j, "noteLane"), kNoteLaneNames, v.noteLane);
    if (v.noteLane == NoteLane::VelocitySpread)
        v.noteLane = NoteLane::Velocity; // varying each play is no longer edited in the editor
    forEach(get(j, "openAutomation"), [&](const json_t* t) {
        if (asInt(t, 0) > 0)
            v.openAutomation.insert(uint64_t(asInt(t, 0)));
    });
    forEach(get(j, "laneHeights"), [&](const json_t* p) {
        int64_t id = intAt(p, 0, 0);
        if (id > 0)
            v.laneHeights[uint64_t(id)] = numberAt(p, 1, v.defaultTrackHeight);
    });
    return v;
}

} // namespace

json_t* EditorState::toJson() const {
    json_t* j = json_object();
    setInt(j, "selectedTrack", json_int_t(selectedTrack.value));
    setInt(j, "selectedClip", json_int_t(selectedClip.value));
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
    setBool(j, "stackClips", stackClips);
    set(j, "export", exportOptions.toJson());
    setString(j, "ghosts", kGhostNames[int(ghosts)]);
    setInt(j, "ghostTrack", json_int_t(ghostTrack.value));
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
    s.overlay = viewFromJson(json_is_object(get(j, "overlay")) ? get(j, "overlay") : j);
    s.panel = viewFromJson(get(j, "panel"), ViewState::compact());
    return s;
}

void ViewState::forgetSong() {
    arranger = TimeAxis::State{0, 0};
    detail = TimeAxis::State{0, 0};
    pitch = PitchAxis::State{0, 0};
    trackScroll = 0;
    launcherScrollX = 0;
    launcherScrollY = 0;
    trackHeights.clear();
    foldedGroups.clear();
    openAutomation.clear();
    laneHeights.clear();
    lowerLane = 0;
}

void EditorState::forgetSong() {
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
