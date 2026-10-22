#include "doctest.h"

#include "../src/model/DeviceTypes.hpp"
#include "../src/ui/interaction/Search.hpp"

#include "../src/model/Json.hpp"
#include "../src/ui/EditorState.hpp"
#include "../src/ui/interaction/Shortcuts.hpp"

#include <GLFW/glfw3.h>

#include <cmath>
#include <set>

using namespace tessera;

TEST_CASE("TimeAxis: zoom keeps the tick under the cursor in place") {
    TimeAxis axis;
    axis.setViewWidth(1000);
    axis.setZoomLimits(1e-3, 2);
    axis.setContentEnd(1e6);
    axis.restore({50000, 0.1}); // far enough from the start that zooming out never hits it

    double anchorX = 300;
    double before = axis.xToTick(anchorX);
    axis.zoomAt(anchorX, 2.5);
    CHECK(axis.pxPerTick() == doctest::Approx(0.25));
    CHECK(axis.xToTick(anchorX) == doctest::Approx(before));

    axis.zoomAt(anchorX, 0.1);
    CHECK(axis.xToTick(anchorX) == doctest::Approx(before));
    CHECK(axis.tickToX(axis.xToTick(123)) == doctest::Approx(123));
}

TEST_CASE("TimeAxis: zoom and scroll stay within limits") {
    TimeAxis axis;
    axis.setViewWidth(1000);
    axis.setZoomLimits(0.01, 1);
    axis.setContentEnd(50000);

    axis.zoomAt(0, 1000);
    CHECK(axis.pxPerTick() == 1);
    axis.zoomAt(0, 1e-6);
    CHECK(axis.pxPerTick() == 0.01);

    axis.scrollTo(-500);
    CHECK(axis.left() == 0);
    axis.restore({0, 0.1});
    axis.scrollTo(1e9);
    CHECK(axis.right() == doctest::Approx(50000)); // content end stops at the right edge
}

TEST_CASE("TimeAxis: animated zoom ends exactly where an instant zoom would") {
    TimeAxis instant, smooth;
    for (TimeAxis* a : {&instant, &smooth}) {
        a->setViewWidth(800);
        a->setZoomLimits(1e-3, 2);
        a->setContentEnd(1e6);
        a->restore({2000, 0.05});
    }
    instant.zoomAt(200, 2);
    instant.zoomAt(200, 2);
    smooth.animateZoomAt(200, 2, 0.08);
    smooth.step(0.03);
    smooth.animateZoomAt(200, 2, 0.08); // a second wheel step mid-animation compounds
    int frames = 0;
    while (smooth.step(1.0 / 60) && frames < 100)
        frames++;
    CHECK_FALSE(smooth.animating());
    CHECK(smooth.pxPerTick() == doctest::Approx(instant.pxPerTick()));
    CHECK(smooth.left() == doctest::Approx(instant.left()));
}

TEST_CASE("TimeAxis: showRange fits a range with a small margin") {
    TimeAxis axis;
    axis.setViewWidth(1000);
    axis.setZoomLimits(1e-4, 10);
    axis.setContentEnd(1e6);
    axis.showRange(3840, 7680);
    CHECK(axis.left() < 3840);
    CHECK(axis.right() > 7680);
    CHECK(axis.visibleTicks() < 3840 * 1.1);
}

TEST_CASE("grid lines never get closer than the minimum spacing") {
    TimeSig ts{4, 4};
    for (double ppt : {0.001, 0.01, 0.05, 0.2, 1.0, 3.0}) {
        GridSpacing g = gridSpacing(ppt, ts, 6, 40);
        CHECK(g.line * ppt >= 6 - 1e-9);
        CHECK(g.label * ppt >= 40 - 1e-9);
        CHECK(g.label % g.line == 0); // labels sit on grid lines
    }
    CHECK(gridSpacing(10, ts).line == kPpq / 16); // never finer than 1/64
    CHECK(lineStrength(0, ts) == LineStrength::Bar);
    CHECK(lineStrength(kPpq, ts) == LineStrength::Beat);
    CHECK(lineStrength(kPpq / 4, ts) == LineStrength::Sub);
    CHECK(snapTick(130, 240) == 240);
    CHECK(snapTick(110, 240) == 0);
}

TEST_CASE("PitchAxis: rows map to pitches and zoom anchors at the cursor") {
    PitchAxis axis;
    axis.setViewHeight(480);
    axis.restore({127 - 72, 12}); // C5 at the top
    CHECK(axis.yToPitch(0) == 72);
    CHECK(axis.yToPitch(11.9) == 72);
    CHECK(axis.yToPitch(12) == 71);
    CHECK(axis.pitchToY(71) == doctest::Approx(12));

    double y = 200;
    double rowBefore = axis.top() + y / axis.rowHeight();
    axis.zoomAt(y, 1.5);
    CHECK(axis.top() + y / axis.rowHeight() == doctest::Approx(rowBefore));

    axis.zoomAt(y, 100);
    CHECK(axis.rowHeight() == PitchAxis::kMaxRow);

    axis.showPitches(48, 67);
    CHECK(axis.pitchToY(67) >= 0);
    CHECK(axis.pitchToY(48) + axis.rowHeight() <= 480);

    // A short pane keeps readable rows and centres on the range instead of squeezing it in.
    axis.setViewHeight(95);
    axis.showPitches(54, 66, 12);
    CHECK(axis.rowHeight() == 12);
    double middle = (axis.pitchToY(60) + axis.rowHeight() / 2);
    CHECK(middle == doctest::Approx(95.0 / 2).epsilon(0.01));
}

TEST_CASE("PitchAxis: a bent pitch sits between rows, and folded rows are crossed smoothly") {
    PitchAxis axis;
    axis.setViewHeight(480);
    axis.restore({127 - 72, 12});                          // C5 at the top
    CHECK(axis.bentPitchToY(72) == doctest::Approx(6));    // the middle of the top row
    CHECK(axis.bentPitchToY(71.5) == doctest::Approx(12)); // half way down to B4
    CHECK(axis.yToBentPitch(12) == doctest::Approx(71.5));
    CHECK(axis.yToBentPitch(axis.bentPitchToY(64.25)) == doctest::Approx(64.25));

    // Folded to C major: E4 (64) and F4 (65) are neighbours, D4 (62) and E4 two semitones on adjacent rows.
    axis.setShownPitches({60, 62, 64, 65, 67, 69, 71, 72});
    double e = axis.bentPitchToY(64), d = axis.bentPitchToY(62);
    CHECK(d - e == doctest::Approx(axis.rowHeight()));
    CHECK(axis.bentPitchToY(63) == doctest::Approx((d + e) / 2)); // a hidden pitch half way between
    CHECK(axis.yToBentPitch((d + e) / 2) == doctest::Approx(63));
    CHECK(axis.yToBentPitch(-100) == 72); // past the top: the top row's pitch
}

TEST_CASE("PitchAxis: folded rows show only some pitches and keep the view centre") {
    PitchAxis axis;
    axis.setViewHeight(120);
    axis.restore({127 - 65, 12}); // rows 62..71 from the top: pitches 65 down to 56
    int centre = axis.yToPitch(60);
    std::vector<int> cMajor;
    for (int p = 0; p < 128; p++)
        if ((0b101010110101 >> (p % 12)) & 1)
            cMajor.push_back(p);
    axis.setShownPitches(cMajor);
    CHECK(axis.folded());
    CHECK(axis.rowCount() == int(cMajor.size()));
    CHECK(axis.pitchAtRow(0) == 127);
    CHECK(axis.pitchAtRow(-5) == 127);
    CHECK(axis.pitchAtRow(1000) == 0);
    CHECK(axis.yToPitch(60) == centre);
    // Every row is a C major pitch, and consecutive rows are consecutive scale notes.
    for (double y = 0; y < 120; y += 7) {
        int p = axis.yToPitch(y);
        CHECK(axis.shown(p));
        CHECK(axis.pitchToY(p) <= y + 1e-9);
        CHECK(y < axis.pitchToY(p) + 12);
    }
    CHECK_FALSE(axis.shown(61));
    CHECK(axis.rowOf(61) == axis.rowOf(60)); // a hidden pitch shares the row below it
    CHECK(axis.stepRows(60, -1) == 62);
    CHECK(axis.stepRows(64, -1) == 65);
    CHECK(axis.stepRows(60, 1) == 59);
    CHECK(axis.stepRows(61, -1) == 62); // from a hidden pitch, the first step lands on its neighbours
    CHECK(axis.stepRows(61, 1) == 60);
    CHECK(axis.stepRows(127, -3) == 127);

    axis.showPitches(60, 72);
    CHECK(axis.pitchToY(72) >= 0);
    CHECK(axis.pitchToY(60) + axis.rowHeight() <= 120 + 1e-9);

    axis.setShownPitches({});
    CHECK_FALSE(axis.folded());
    CHECK(axis.stepRows(60, -1) == 61);
}

TEST_CASE("TrackAxis: variable heights, hit testing, anchored zoom") {
    TrackAxis axis;
    axis.setViewHeight(100);
    axis.setHeights({40, 100, 60, 10}); // the last clamps up to the minimum
    CHECK(axis.heights()[3] == TrackAxis::kMinHeight);
    CHECK(axis.contentHeight() == 224);
    CHECK(axis.trackAt(39) == 0);
    CHECK(axis.trackAt(40) == 1);
    CHECK(axis.trackAt(500) == -1);

    axis.scrollTo(45);
    CHECK(axis.trackAt(5) == 1);
    double y = 50;
    int track = axis.trackAt(y);
    double fraction = (y - axis.trackTop(track)) / axis.trackHeight(track);
    axis.zoomAt(y, 1.5);
    CHECK(axis.trackAt(y) == track);
    CHECK((y - axis.trackTop(track)) / axis.trackHeight(track) == doctest::Approx(fraction));

    axis.fitAll();
    CHECK(axis.trackHeight(0) == doctest::Approx(25));
}

TEST_CASE("zoom history goes back and forward like a browser") {
    ZoomHistory h;
    TimeAxis::State a{0, 1}, b{10, 2}, c{20, 3};
    CHECK_FALSE(h.back(a).has_value());
    h.push(a); // jumped from a to b
    h.push(b); // jumped from b to c
    CHECK(h.back(c) == b);
    CHECK(h.back(b) == a);
    CHECK_FALSE(h.back(a).has_value());
    CHECK(h.forward() == b);
    CHECK(h.forward() == c);
    CHECK_FALSE(h.forward().has_value());
}

TEST_CASE("follow playhead: page jumps only when the playhead leaves the view") {
    TimeAxis axis;
    axis.setViewWidth(1000);
    axis.setZoomLimits(1e-4, 10);
    axis.setContentEnd(1e6);
    axis.restore({1000, 1}); // shows ticks 1000..2000
    CHECK_FALSE(followScroll(FollowMode::Page, 1500, axis).has_value());
    CHECK(followScroll(FollowMode::Page, 2000, axis) == 2000.0);
    CHECK(followScroll(FollowMode::Continuous, 1500, axis) == doctest::Approx(1500 - 1000.0 / 3));
    CHECK_FALSE(followScroll(FollowMode::Off, 99999, axis).has_value());
}

TEST_CASE("follow playhead: a clip that isn't playing never moves the view") {
    TimeAxis axis;
    axis.setViewWidth(1000);
    axis.setZoomLimits(1e-4, 10);
    axis.setContentEnd(1e6);
    axis.restore({1000, 1});
    CHECK_FALSE(followScroll(FollowMode::Page, NAN, axis).has_value());
    CHECK_FALSE(followScroll(FollowMode::Continuous, NAN, axis).has_value());

    // Even if a NaN slips through, the view stays usable.
    axis.scrollTo(NAN);
    CHECK(axis.left() == 0);
    axis.setZoom(NAN, 0);
    CHECK(std::isfinite(axis.pxPerTick()));
}

TEST_CASE("edge auto-scroll speeds up toward the edge") {
    CHECK(autoScrollSpeed(500, 1000) == 0);
    CHECK(autoScrollSpeed(12, 1000) < 0);
    CHECK(autoScrollSpeed(0, 1000) < autoScrollSpeed(12, 1000));
    CHECK(autoScrollSpeed(-50, 1000) == autoScrollSpeed(0, 1000));
    CHECK(autoScrollSpeed(995, 1000) > 0);

    // A 75 px tall view (the panel editor) keeps most of its height free of auto-scroll.
    CHECK(autoScrollSpeed(15, 75) == 0);
    CHECK(autoScrollSpeed(60, 75) == 0);
    CHECK(autoScrollSpeed(-5, 75) < 0);
    CHECK(autoScrollSpeed(80, 75) > 0);
}

TEST_CASE("Shift locks a drag to the axis of its first movement") {
    AxisLock lock;
    double dx = 2, dy = 1;
    lock.apply(dx, dy, true);
    CHECK(dx == 0); // undecided below 4 px: hold still
    CHECK(dy == 0);
    dx = 10;
    dy = 3;
    lock.apply(dx, dy, true);
    CHECK(lock.current() == AxisLock::Axis::Horizontal);
    CHECK(dy == 0);
    dx = 1;
    dy = 50; // stays horizontal even when the pointer later moves vertically
    lock.apply(dx, dy, true);
    CHECK(dx == 1);
    CHECK(dy == 0);
    lock.apply(dx, dy, false);
    CHECK(lock.current() == AxisLock::Axis::Undecided);
}

TEST_CASE("keymap: every action is in the table and no chord does two things in one pane") {
    std::set<Action> seen;
    for (const ActionInfo& info : actionTable())
        seen.insert(info.action);
    CHECK(seen.size() == size_t(Action::Count));

    for (Scope pane : {Scope::TopPane, Scope::DetailPane}) {
        for (const ActionInfo& a : actionTable()) {
            for (const ActionInfo& b : actionTable()) {
                if (a.action >= b.action)
                    continue;
                auto inPane = [&](const ActionInfo& i) { return i.scope == Scope::Everywhere || i.scope == pane; };
                if (!inPane(a) || !inPane(b))
                    continue;
                for (const KeyChord& ca : a.chords)
                    for (const KeyChord& cb : b.chords) {
                        CAPTURE(a.label);
                        CAPTURE(b.label);
                        CHECK_FALSE((ca == cb && ca.hold == cb.hold));
                    }
            }
        }
    }
}

TEST_CASE("keymap: lookups use the key name for printable keys and the key code otherwise") {
    CHECK(findAction({GLFW_KEY_SPACE, "", 0}, Scope::DetailPane) == Action::PlayStop);
    CHECK(findAction({GLFW_KEY_Z, "z", kModCtrl}, Scope::DetailPane) == Action::Undo);
    CHECK(findAction({GLFW_KEY_W, "z", kModCtrl}, Scope::DetailPane) == Action::Undo); // AZERTY: the key labeled Z
    CHECK(findAction({GLFW_KEY_Z, "z", 0}, Scope::DetailPane) == Action::ZoomToSelection);
    CHECK(findAction({GLFW_KEY_T, "t", kModCtrl | kModShift}, Scope::DetailPane) == Action::TransformBox);
    CHECK(findAction({GLFW_KEY_T, "t", kModCtrl | kModShift}, Scope::TopPane) == Action::AddDrumTrack);
    CHECK_FALSE(findAction({GLFW_KEY_G, "g", kModCtrl | kModAlt}, Scope::TopPane).has_value());
    CHECK(shortcutLabel(Action::Redo) == "Ctrl+Shift+Z");
    CHECK(shortcutLabel(Action::GoToStart) == "Home");
}

TEST_CASE("editor state survives a save and load, with separate overlay and panel views") {
    EditorState s;
    s.selectedTrack = TrackId(7);
    s.follow = FollowMode::Continuous;
    s.uiScale = 1.25f;
    s.panelHp = 80;
    ViewState& v = s.overlay;
    v.topFraction = 0.3f;
    v.headerWidth = 220;
    v.inspectorOpen = false;
    v.focus = Pane::Top;
    v.maximized = Pane::Detail;
    v.arranger = {3840, 0.02};
    v.detail = {960, 0.3};
    v.pitch = {40, 14};
    v.trackScroll = 12;
    v.trackHeights[7] = 96;
    v.clipsWide = true;
    v.launcherScrollX = 30;
    v.launcherScrollY = 44;
    s.selectedClip = ClipId(12);
    s.panel.detail = {0, 0.5};
    s.tool = Tool::Slice;
    s.snap.adaptive = false;
    s.snap.division = GridDivision::Eighth;
    s.snap.feel = GridFeel::Triplet;
    s.lastLength = 720;
    s.lastVelocity = 0.42f;
    s.audition = false;
    s.panel.pitch = {50, 10};
    s.scaleHighlight = false;
    s.scaleSnap = true;
    s.scaleFold = true;
    s.chord = ChordStamp{ChordShape::DiatonicSeventh, 2, ChordVoicing::Drop2};

    json::Ref j(s.toJson());
    EditorState t = EditorState::fromJson(j.get());
    CHECK(t.selectedTrack == s.selectedTrack);
    CHECK(t.selectedClip == s.selectedClip);
    CHECK(t.follow == s.follow);
    CHECK(t.uiScale == s.uiScale);
    CHECK(t.panelHp == 80);
    CHECK(t.tool == Tool::Slice);
    CHECK(t.snap == s.snap);
    CHECK(t.lastLength == 720);
    CHECK(t.lastVelocity == doctest::Approx(0.42f));
    CHECK_FALSE(t.audition);
    CHECK_FALSE(t.scaleHighlight);
    CHECK(t.scaleSnap);
    CHECK(t.scaleFold);
    CHECK(t.chord == s.chord);
    const ViewState& o = t.overlay;
    CHECK(o.topFraction == v.topFraction);
    CHECK(o.headerWidth == v.headerWidth);
    CHECK(o.inspectorOpen == v.inspectorOpen);
    CHECK(o.focus == v.focus);
    CHECK(o.maximized == v.maximized);
    CHECK(o.arranger == v.arranger);
    CHECK(o.detail == v.detail);
    CHECK(o.pitch == v.pitch);
    CHECK(o.trackScroll == v.trackScroll);
    CHECK(o.clipsWide);
    CHECK(o.launcherScrollX == 30);
    CHECK(o.launcherScrollY == 44);
    CHECK_FALSE(t.panel.clipsWide);
    CHECK(o.trackHeight(TrackId(7)) == 96);
    CHECK(o.trackHeight(TrackId(8)) == ViewState().defaultTrackHeight);
    CHECK(t.panel.headerWidth == ViewState::compact().headerWidth); // the panel keeps its compact defaults
    CHECK(t.panel.detail == s.panel.detail);
    CHECK(t.panel.pitch == s.panel.pitch);
    CHECK(t.panel.arranger.pxPerTick == 0); // never set: fits on first open

    EditorState fallback = EditorState::fromJson(nullptr);
    CHECK(fallback.overlay.topFraction == ViewState().topFraction);
    CHECK(fallback.panelHp == EditorState::kPanelMinHp);
}

TEST_CASE("an editor layout saved by milestone 3 loads as the overlay view") {
    json::Ref old(json_loads(R"({"topFraction": 0.3, "focus": "top", "detail": [960, 0.3],
                                 "follow": "continuous", "selectedTrack": 4, "panelHp": 9999})",
                             0, nullptr));
    EditorState s = EditorState::fromJson(old.get());
    CHECK(s.overlay.topFraction == doctest::Approx(0.3));
    CHECK(s.overlay.focus == Pane::Top);
    CHECK(s.overlay.detail.pxPerTick == doctest::Approx(0.3));
    CHECK(s.follow == FollowMode::Continuous);
    CHECK(s.selectedTrack == TrackId(4));
    CHECK(s.panelHp == EditorState::kPanelMaxHp);
}

TEST_CASE("the device browser finds devices by name and by what they do") {
    auto found = [](const std::string& query) {
        std::vector<std::string> names;
        for (int t = 0; t < kDeviceTypeCount; t++) {
            const devices::TypeInfo& info = devices::deviceInfo(DeviceType(t));
            if (searchMatches(info.name, info.description, info.keywords, query))
                names.push_back(info.name);
        }
        return names;
    };
    CHECK(found("").size() == size_t(kDeviceTypeCount));
    CHECK(found("delay") == std::vector<std::string>{"Echo"});
    CHECK(found("ARP") == std::vector<std::string>{"Arpeggiator"});
    CHECK(found("humanize") == std::vector<std::string>{"Humanize"});
    CHECK(found("  key ").size() >= 2); // Transpose and Scale both work in a key
    CHECK(found("zzz").empty());
    for (int t = 0; t < kDeviceTypeCount; t++) {
        const devices::TypeInfo& info = devices::deviceInfo(DeviceType(t));
        CAPTURE(info.name);
        CHECK(std::string(info.group).size() > 0);
        CHECK(std::string(info.description).size() > 0);
    }
    for (int t = 0; t < kModulatorTypeCount; t++)
        CHECK(std::string(devices::modulatorInfo(ModulatorType(t)).description).size() > 0);
}

TEST_CASE("a new song forgets the old one's selections and per-track views, and keeps the preferences") {
    EditorState s;
    s.selectedTrack = TrackId(7);
    s.selectedClip = ClipId(9);
    s.selectedTracks = {TrackId(7)};
    s.clipSelection = {ArrangementId(3)};
    s.layered = {ClipId(9)};
    s.ghosts = GhostSource::Track;
    s.ghostTrack = TrackId(7);
    s.range.from = 0;
    s.range.to = kPpq;
    s.range.tracks = {TrackId(7)};
    s.overlay.trackHeights[7] = 90;
    s.overlay.openAutomation.insert(7);
    s.panel.laneHeights[4] = 50;
    s.overlay.trackScroll = 120;
    // Preferences
    s.tool = Tool::Draw;
    s.scaleSnap = true;
    s.panelHp = 80;
    s.overlay.headerWidth = 230;

    s.forgetSong();
    CHECK_FALSE(s.selectedTrack.valid());
    CHECK_FALSE(s.selectedClip.valid());
    CHECK(s.selectedTracks.empty());
    CHECK(s.clipSelection.empty());
    CHECK(s.layered.empty());
    CHECK(s.ghosts == GhostSource::None);
    CHECK(s.range.empty());
    CHECK(s.overlay.trackHeights.empty());
    CHECK(s.overlay.openAutomation.empty());
    CHECK(s.panel.laneHeights.empty());
    CHECK(s.overlay.trackScroll == 0);
    CHECK(s.tool == Tool::Draw);
    CHECK(s.scaleSnap);
    CHECK(s.panelHp == 80);
    CHECK(s.overlay.headerWidth == 230);
}

TEST_CASE("TrackAxis: a zoom may leave room past the last track, which scrolling back gives up") {
    TrackAxis axis;
    axis.setViewHeight(400);
    axis.setHeights({100, 100}); // fits: no scrolling
    axis.scrollByPx(50);
    CHECK(axis.scroll() == 0);
    axis.anchorScroll(80);
    CHECK(axis.scroll() == 80);
    axis.setHeights({100, 100}); // a rebuild keeps it
    CHECK(axis.scroll() == 80);
    axis.scrollByPx(20); // no further down than the zoom left it
    CHECK(axis.scroll() == 80);
    axis.scrollByPx(-50);
    CHECK(axis.scroll() == 30);
    axis.scrollByPx(50); // given up: back to the normal end
    CHECK(axis.scroll() == 30);
    axis.scrollTo(0);
    axis.scrollByPx(10);
    CHECK(axis.scroll() == 0);
}
