#include "doctest.h"

#include "../src/model/ClipControl.hpp"

#include <algorithm>

using namespace tessera;
using namespace tessera::clipControl;

namespace {

/// Decodes the message `bytes` under `s` with the window at `w`.
Action press(const Settings& s, std::initializer_list<uint8_t> bytes, Window w = {}, InputState* state = nullptr) {
    InputState own;
    std::vector<uint8_t> b(bytes);
    return decode(s, w, state ? *state : own, b.data(), int(b.size()));
}

/// Settings with layout `l`.
Settings with(Layout l) {
    Settings s;
    s.layout = l;
    return s;
}

} // namespace

TEST_CASE("clip control: a Launchpad's pads, right column and arrows") {
    Settings s = with(Layout::LaunchpadX);
    Window w{2, 4};
    // Bottom-left pad (11): the window's first track, its last scene.
    Action a = press(s, {0x90, 11, 127}, w);
    CHECK(a.kind == Kind::Slot);
    CHECK(a.column == 0);
    CHECK(a.scene == 4 + 7);
    // Top-right pad (88): the last track, the first scene.
    a = press(s, {0x90, 88, 100}, w);
    CHECK(a.column == 7);
    CHECK(a.scene == 4);
    CHECK(press(s, {0x90, 88, 0}, w).kind == Kind::None); // a release
    a = press(s, {0xB0, 89, 127}, w);
    CHECK(a.kind == Kind::Scene);
    CHECK(a.scene == 4);
    CHECK(press(s, {0xB0, 19, 127}, w).scene == 11);
    CHECK(press(s, {0xB0, 91, 127}, w).kind == Kind::Up);
    CHECK(press(s, {0xB0, 94, 127}, w).kind == Kind::Right);
    CHECK(press(s, {0xB0, 98, 127}, w).kind == Kind::StopAll);
}

TEST_CASE("clip control: an APC mini's pads, stop buttons, scene buttons, and Shift for the arrows") {
    Settings s = with(Layout::ApcMini);
    Action a = press(s, {0x90, 0x00, 127});
    CHECK(a.kind == Kind::Slot);
    CHECK(a.column == 0);
    CHECK(a.scene == 7);
    a = press(s, {0x90, 0x3F, 127});
    CHECK(a.column == 7);
    CHECK(a.scene == 0);
    a = press(s, {0x90, 0x66, 127});
    CHECK(a.kind == Kind::StopTrack);
    CHECK(a.column == 2);
    CHECK(press(s, {0x90, 0x71, 127}).scene == 1);
    InputState state;
    press(s, {0x90, 0x7A, 127}, {}, &state);
    CHECK(state.shift);
    CHECK(press(s, {0x90, 0x68, 127}, {}, &state).kind == Kind::Up);
    CHECK(press(s, {0x90, 0x6B, 127}, {}, &state).kind == Kind::Right);
    press(s, {0x80, 0x7A, 0}, {}, &state);
    CHECK_FALSE(state.shift);
}

TEST_CASE("clip control: any 8x8 pads from a base note, on a chosen channel") {
    Settings s = with(Layout::NoteGrid);
    s.noteBase = 36;
    s.channel = 10;
    Action a = press(s, {0x99, 36, 100});
    CHECK(a.column == 0);
    CHECK(a.scene == 7);
    CHECK(press(s, {0x99, 36 + 63, 100}).scene == 0);
    CHECK(press(s, {0x90, 36, 100}).kind == Kind::None); // another channel
    CHECK(press(s, {0x99, 35, 100}).kind == Kind::None);
}

TEST_CASE("clip control: learned controls come first, and program changes launch scenes") {
    Settings s = with(Layout::LaunchpadX);
    Binding b;
    b.type = Binding::Type::Note;
    b.channel = 0;
    b.number = 11;
    b.kind = Kind::Scene;
    b.scene = 3;
    s.learned.push_back(b);
    Action a = press(s, {0x90, 11, 127});
    CHECK(a.kind == Kind::Scene);
    CHECK(a.scene == 3);
    CHECK(press(s, {0xC0, 5}).kind == Kind::None);
    s.programScenes = true;
    a = press(s, {0xC0, 5});
    CHECK(a.kind == Kind::Scene);
    CHECK(a.scene == 5);
    Binding seen;
    std::vector<uint8_t> cc{0xB3, 64, 127};
    REQUIRE(controlOf(cc.data(), 3, seen));
    CHECK(seen.type == Binding::Type::Cc);
    CHECK(seen.channel == 3);
    std::vector<uint8_t> off{0xB3, 64, 0};
    CHECK_FALSE(controlOf(off.data(), 3, seen));
}

TEST_CASE("clip control: the window moves a step and stays inside the launcher") {
    Window w;
    w = moved(w, Kind::Right, 12, 10);
    CHECK(w.track == 1);
    w = moved(w, Kind::Up, 12, 10);
    CHECK(w.scene == 0);
    for (int i = 0; i < 10; i++)
        w = moved(w, Kind::Right, 12, 10);
    CHECK(w.track == 4);
    CHECK(clamped({9, 9}, 3, 3) == Window{0, 0});
}

TEST_CASE("clip control: lights go out only for what changed, in the controller's colours") {
    CHECK(paletteIndex(0xFF0000) == 5);
    CHECK(paletteIndex(0x00FF00) == 21);
    CHECK(paletteIndex(0) == 0);
    Surface s;
    s.cells[0][0].state = Cell::State::Playing;
    s.cells[0][0].color = 0xFF0000;
    s.cells[7][7].state = Cell::State::Queued;
    s.cells[7][7].color = 0x00FF00;
    std::map<int, Message> lit;
    std::vector<Message> first = lights(Layout::LaunchpadX, s, lit);
    // Top-left pad (81) pulses red on channel 3; bottom-right (18) flashes green on channel 2.
    CHECK(std::find(first.begin(), first.end(), Message{0x92, 81, 5}) != first.end());
    CHECK(std::find(first.begin(), first.end(), Message{0x91, 18, 21}) != first.end());
    CHECK(lights(Layout::LaunchpadX, s, lit).empty());
    s.cells[0][0].state = Cell::State::Clip;
    CHECK(lights(Layout::LaunchpadX, s, lit).size() == 1);

    std::map<int, Message> apc;
    s.trackPlays[2] = true;
    std::vector<Message> m = lights(Layout::ApcMini, s, apc);
    CHECK(std::find(m.begin(), m.end(), Message{0x9D, 7, 21}) != m.end()); // pad 7: bottom-right, blinking
    CHECK(std::find(m.begin(), m.end(), Message{0x90, 0x66, 1}) != m.end());
    CHECK(lights(Layout::NoteGrid, s, apc).empty());
    CHECK(enter(Layout::LaunchpadMini).front()[5] == 0x0D);
}

TEST_CASE("clip control: settings with learned controls are saved and loaded") {
    Settings s = with(Layout::ApcMini);
    s.channel = 3;
    s.programScenes = true;
    Binding b;
    b.type = Binding::Type::Cc;
    b.channel = 2;
    b.number = 20;
    b.kind = Kind::Slot;
    b.track = TrackId(42);
    b.scene = 1;
    s.learned.push_back(b);
    json_t* j = s.toJson();
    CHECK(Settings::fromJson(j) == s);
    json_decref(j);
    CHECK(Settings::fromJson(nullptr) == Settings());
}
