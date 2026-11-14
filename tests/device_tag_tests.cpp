#include "doctest.h"

#include "../src/model/Devices.hpp"
#include "../src/model/Playheads.hpp"
#include "../src/ui/interaction/DeviceTags.hpp"

using namespace tessera;
using namespace tessera::tags;

namespace {

/// The tag spec for `param` on a device of `type`.
TagSpec specOf(DeviceType type, int param) {
    for (const TileSpec& t : tilesOf(type))
        for (const TagSpec& s : t.tags)
            if (s.param == param)
                return s;
    FAIL("no tag for that setting");
    return {};
}

/// The label typing `query` lands on.
std::string found(const ValueList& list, const std::string& query) {
    auto m = match(list, query);
    return m ? list.labels[size_t(m->index)] : "(none)";
}

} // namespace

TEST_SUITE("device tags") {
    TEST_CASE("every device has one to three pictures, each setting on one of them") {
        for (int t = 0; t < kDeviceTypeCount; t++) {
            const auto& tiles = tilesOf(DeviceType(t));
            CHECK(tiles.size() >= 1);
            CHECK(tiles.size() <= 3);
        }
    }

    TEST_CASE("a device's lengths drag from short to long, and read typeset") {
        ValueList rates = valueList(DeviceType::Echo, specOf(DeviceType::Echo, chain::knob::kTime));
        REQUIRE(rates.size() == 17);
        CHECK(rates.labels.front() == "1/64");
        CHECK(rates.labels.back() == "8 bars");
        for (int k = 1; k < rates.size(); k++)
            CHECK(sixteenthsOf(DeviceType::Echo, chain::knob::kTime, rates.values[size_t(k)]) >
                  sixteenthsOf(DeviceType::Echo, chain::knob::kTime, rates.values[size_t(k - 1)]));
        CHECK(rates.labels[size_t(rates.def)] == "1/8");

        ValueText t = splitLabel("1/16 T");
        CHECK(t.num == "1");
        CHECK(t.den == "16");
        CHECK(t.mark == "T");
        ValueText semis = splitLabel("−3 st");
        CHECK(semis.sign == "−");
        CHECK(semis.text == "3");
        CHECK(semis.unit == "st");
        CHECK(splitLabel("2 bars").text == "2 bars");
        CHECK(splitLabel("3/2x").unit == "×");
    }

    TEST_CASE("typing finds the closest choice") {
        ValueList lengths = valueList(DeviceType::Echo, specOf(DeviceType::Echo, chain::knob::kTime));
        CHECK(found(lengths, "1/8t") == "1/8 T");
        CHECK(found(lengths, "3/16") == "1/8 D");
        CHECK(match(lengths, "3/16")->exact);
        CHECK(found(lengths, "1 bar") == "1 bar");
        CHECK(found(lengths, "2bars") == "2 bars");
        CHECK(!match(lengths, "1/7")->exact);

        ValueList rate = valueList(DeviceType::Playheads, specOf(DeviceType::Playheads, playheads::knob::kRate));
        CHECK(found(rate, "1.5") == "3/2x");
        CHECK(found(rate, "2") == "2x");

        CHECK(found(directionList(), "reverse") == "Backward");
        CHECK(found(directionList(), "shuffle") == "Shuffle");

        ValueList curve = valueList(DeviceType::Playheads, specOf(DeviceType::Playheads, playheads::knob::kCurve));
        CHECK(found(curve, "ease out") == "Ease out");
        CHECK(found(curve, "swing") == "Swing light");

        ValueList offset = valueList(DeviceType::Playheads, specOf(DeviceType::Playheads, playheads::knob::kTimeOffset));
        CHECK(found(offset, "1/8") == "+1/8");
        CHECK(found(offset, "-1/16") == "−1/16");
        CHECK(offset.labels[size_t(offset.zero)] == "0");

        ValueList gate = valueList(DeviceType::Echo, specOf(DeviceType::Echo, chain::knob::kEchoGate));
        CHECK(found(gate, "52") == "50%");
        CHECK(found(gate, "zzz") == "(none)");
    }

    TEST_CASE("numbers step by their tag's step, with sign and unit") {
        ValueList pitch = valueList(DeviceType::Echo, specOf(DeviceType::Echo, chain::knob::kEchoPitch));
        CHECK(pitch.labels.front() == "−12 st");
        CHECK(pitch.labels[size_t(pitch.def)] == "0 st");
        CHECK(pitch.labels.back() == "+12 st");
        ValueList gate = valueList(DeviceType::Echo, specOf(DeviceType::Echo, chain::knob::kEchoGate));
        CHECK(gate.values[1] - gate.values[0] == doctest::Approx(5));
        ValueList lowest = valueList(DeviceType::NoteFilter, specOf(DeviceType::NoteFilter, chain::knob::kLowest));
        CHECK(lowest.labels[60] == "C4");
    }

    TEST_CASE("channel sets read as runs and are typed the same way") {
        CHECK(channelsText(0) == "Any");
        CHECK(channelsText(0xFFFF) == "All");
        CHECK(channelsText(0b1000000011) == "1, 2, 10");
        CHECK(channelsText(0b1000001111) == "1–4, 10");
        CHECK(parseChannels("1-4, 10") == uint16_t(0b1000001111));
        CHECK(parseChannels("any") == uint16_t(0));
        CHECK(parseChannels("ALL") == uint16_t(0xFFFF));
        CHECK(parseChannels("3 5") == uint16_t(0b10100));
        CHECK(!parseChannels("17"));
        CHECK(!parseChannels("4-2"));
        CHECK(!parseChannels("x"));
    }

    TEST_CASE("captions say what the picture can't") {
        Values v;
        v.v[size_t(chain::knob::kSemitones)] = 7;
        CHECK(caption(DeviceType::Transpose, 0, v) == "A fifth up");
        v.v[size_t(chain::knob::kOctaves)] = -1;
        CHECK(caption(DeviceType::Transpose, 0, v) == "A fourth down");

        Values chord; // Major, close, in root position
        chord.v[size_t(chain::knob::kChord)] = 3;
        CHECK(caption(DeviceType::Chord, 0, chord) == "On C: C E G");
        chord.v[size_t(chain::knob::kInversion)] = 1;
        CHECK(caption(DeviceType::Chord, 0, chord) == "On C: E G C");

        Values arp;
        arp.v[size_t(chain::knob::kArpOctaves)] = 1;
        CHECK(caption(DeviceType::Arpeggiator, 0, arp) == "On C E G: C E G C E G C E …");

        Values groove;
        groove.v[size_t(chain::knob::kGrooveSwing)] = 62;
        CHECK(caption(DeviceType::Groove, 0, groove) == "Lazy");

        Values ph;
        int plus8 = playheads::choiceIndex(playheads::offsetNames(), "+1/8");
        ph.v[size_t(playheads::knob::kTimeOffset)] = float(plus8);
        CHECK(caption(DeviceType::Playheads, 1, ph) == "A canon, 1/8 behind");
        ph.v[size_t(playheads::knob::kRate)] = float(playheads::choiceIndex(playheads::rateNames(), "2x"));
        CHECK(caption(DeviceType::Playheads, 0, ph) == "2 passes a bar");
        ph.v[size_t(playheads::knob::kRepeatSize)] = float(playheads::choiceIndex(playheads::repeatNames(), "1/8"));
        ph.v[size_t(playheads::knob::kRepeatTimes)] = 4;
        CHECK(caption(DeviceType::Playheads, 2, ph) == "Fills 1/2, then moves on");

        Values filter;
        filter.v[size_t(chain::knob::kEvery)] = 1;
        filter.channels = 0b11;
        CHECK(caption(DeviceType::NoteFilter, 1, filter) == "Only 2 of the 16 channels");
    }

    TEST_CASE("settings that don't apply are dimmed or hidden") {
        Values v;
        CHECK(dimmed(DeviceType::Playheads, playheads::knob::kRepeatTimes, v));
        v.v[size_t(playheads::knob::kRepeatSize)] = 3;
        CHECK(!dimmed(DeviceType::Playheads, playheads::knob::kRepeatTimes, v));
        Values len; // Mode Scale
        CHECK(!hidden(DeviceType::NoteLength, chain::knob::kScaleBy, len));
        CHECK(hidden(DeviceType::NoteLength, chain::knob::kLength, len));
        CHECK(hidden(DeviceType::NoteLength, chain::knob::kOverlap, len));
    }
}
