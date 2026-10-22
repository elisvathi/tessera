#include "doctest.h"

#include "../src/engine/MidiReader.hpp"

#include <vector>

using namespace tessera;

namespace {

using Kind = MidiEvent::Kind;

/// Reads one message and returns its events as a list.
std::vector<MidiEvent> read(MidiReader& reader, std::vector<uint8_t> bytes) {
    std::array<MidiEvent, MidiReader::kMaxEvents> out;
    int n = reader.read(bytes.data(), int(bytes.size()), out);
    return {out.begin(), out.begin() + n};
}

/// Selects an RPN on a channel and sends its value, as a controller does.
void sendRpn(MidiReader& reader, int channel, int rpn, int value) {
    uint8_t cc = uint8_t(0xB0 | channel);
    read(reader, {cc, 101, 0});
    read(reader, {cc, 100, uint8_t(rpn)});
    read(reader, {cc, 6, uint8_t(value)});
}

} // namespace

TEST_CASE("midi reader: notes, and a note-on with velocity 0 is a note-off") {
    MidiReader reader;
    auto on = read(reader, {0x92, 60, 127});
    REQUIRE(on.size() == 1);
    CHECK(on[0].kind == Kind::NoteOn);
    CHECK(on[0].channel == 2);
    CHECK(on[0].pitch == 60);
    CHECK(on[0].value == doctest::Approx(1));

    auto off = read(reader, {0x82, 60, 64});
    REQUIRE(off.size() == 1);
    CHECK(off[0].kind == Kind::NoteOff);
    CHECK(off[0].value == doctest::Approx(64 / 127.f));

    auto zero = read(reader, {0x92, 60, 0});
    REQUIRE(zero.size() == 1);
    CHECK(zero[0].kind == Kind::NoteOff);
}

TEST_CASE("midi reader: normal mode keeps poly aftertouch per note and the rest per channel") {
    MidiReader reader;
    auto poly = read(reader, {0xA0, 64, 127});
    REQUIRE(poly.size() == 1);
    CHECK(poly[0].kind == Kind::NoteValue);
    CHECK(poly[0].curve == NoteCurve::Pressure);
    CHECK(poly[0].pitch == 64);
    CHECK(poly[0].value == doctest::Approx(1));

    auto mono = read(reader, {0xD3, 64});
    REQUIRE(mono.size() == 1);
    CHECK(mono[0].kind == Kind::ChannelValue);
    CHECK(mono[0].control == ChannelControl::Pressure);
    CHECK(mono[0].channel == 3);

    auto wheel = read(reader, {0xB0, 1, 127});
    REQUIRE(wheel.size() == 1);
    CHECK(wheel[0].control == ChannelControl::Cc);
    CHECK(wheel[0].cc == 1);

    // CC 74 is only a note's timbre in MPE; here it is a plain controller.
    auto cc74 = read(reader, {0xB0, 74, 10});
    REQUIRE(cc74.size() == 1);
    CHECK(cc74[0].kind == Kind::ChannelValue);
}

TEST_CASE("midi reader: pitch bend spans -1..1 with the centre at 0") {
    MidiReader reader;
    CHECK(read(reader, {0xE0, 0, 64})[0].value == doctest::Approx(0));
    CHECK(read(reader, {0xE0, 0, 0})[0].value == doctest::Approx(-1));
    CHECK(read(reader, {0xE0, 127, 127})[0].value == doctest::Approx(1));
}

TEST_CASE("midi reader: RPN messages are setup, not controllers") {
    MidiReader reader;
    CHECK(read(reader, {0xB0, 101, 0}).empty());
    CHECK(read(reader, {0xB0, 100, 0}).empty());
    CHECK(read(reader, {0xB0, 6, 12}).empty());
    CHECK(read(reader, {0xB0, 38, 0}).empty());
}

TEST_CASE("midi reader: MPE member channels carry one note's pitch, timbre and pressure") {
    MidiReader reader;
    reader.setMpe(true);

    auto bend = read(reader, {0xE1, 0, 96}); // half way up
    REQUIRE(bend.size() == 1);
    CHECK(bend[0].kind == Kind::NoteValue);
    CHECK(bend[0].curve == NoteCurve::Pitch);
    CHECK(bend[0].pitch == -1);
    CHECK(bend[0].value == doctest::Approx(24).epsilon(0.01));

    auto timbre = read(reader, {0xB1, 74, 127});
    REQUIRE(timbre.size() == 1);
    CHECK(timbre[0].curve == NoteCurve::Timbre);

    auto press = read(reader, {0xD1, 127});
    REQUIRE(press.size() == 1);
    CHECK(press[0].curve == NoteCurve::Pressure);
    CHECK(press[0].value == doctest::Approx(1));
}

TEST_CASE("midi reader: an MPE note starts with the values sent just before it") {
    MidiReader reader;
    reader.setMpe(true);
    read(reader, {0xE4, 0, 64});
    read(reader, {0xB4, 74, 64});
    read(reader, {0xD4, 0});

    auto on = read(reader, {0x94, 60, 100});
    REQUIRE(on.size() == 4);
    CHECK(on[0].kind == Kind::NoteOn);
    for (size_t i = 1; i < on.size(); i++) {
        CHECK(on[i].kind == Kind::NoteValue);
        CHECK(on[i].pitch == 60);
    }
    CHECK(on[1].curve == NoteCurve::Pitch);
    CHECK(on[2].curve == NoteCurve::Timbre);
    CHECK(on[3].curve == NoteCurve::Pressure);

    // A channel that sent nothing yet starts its note bare.
    CHECK(read(reader, {0x95, 62, 100}).size() == 1);
}

TEST_CASE("midi reader: the MPE master channel speaks for the whole zone") {
    MidiReader reader;
    reader.setMpe(true);
    auto bend = read(reader, {0xE0, 0, 0});
    REQUIRE(bend.size() == 1);
    CHECK(bend[0].kind == Kind::ChannelValue);
    CHECK(bend[0].control == ChannelControl::PitchBend);
    CHECK(bend[0].value == doctest::Approx(-1));

    // A member channel's other controllers count for the zone too.
    auto wheel = read(reader, {0xB3, 1, 127});
    REQUIRE(wheel.size() == 1);
    CHECK(wheel[0].kind == Kind::ChannelValue);
    CHECK(wheel[0].channel == 0);
}

TEST_CASE("midi reader: RPN 0 sets the MPE bend ranges") {
    MidiReader reader;
    reader.setMpe(true);
    CHECK(reader.noteBendRange(0) == doctest::Approx(48));
    sendRpn(reader, 1, 0, 24);
    CHECK(reader.noteBendRange(0) == doctest::Approx(24));
    CHECK(read(reader, {0xE1, 127, 127})[0].value == doctest::Approx(24));
}

TEST_CASE("midi reader: the MPE configuration message sets the zones and turns MPE on") {
    MidiReader reader;
    sendRpn(reader, 0, 6, 7); // lower zone: master 1, members 2..8
    CHECK(reader.mpe());
    CHECK(read(reader, {0xD7, 64})[0].kind == Kind::NoteValue);
    // Channel 10 is past the zone: an ordinary channel.
    CHECK(read(reader, {0xD9, 64})[0].kind == Kind::ChannelValue);

    sendRpn(reader, 15, 6, 4); // upper zone: master 16, members 12..15
    CHECK(read(reader, {0xDB, 64})[0].kind == Kind::NoteValue);
    CHECK(read(reader, {0xDF, 64})[0].kind == Kind::ChannelValue);

    // A full lower zone leaves no room for the upper one: channel 16 is a member now, not a master.
    sendRpn(reader, 0, 6, 15);
    CHECK(read(reader, {0xDF, 64})[0].kind == Kind::NoteValue);
    CHECK(read(reader, {0xEF, 0, 64})[0].kind == Kind::NoteValue);
}

TEST_CASE("midi reader: short or unknown messages mean nothing") {
    MidiReader reader;
    CHECK(read(reader, {0x90}).empty());
    CHECK(read(reader, {0xF8, 0}).empty());
    CHECK(read(reader, {0xC0, 5}).empty());
}
