#pragma once

#include "../../model/Device.hpp"
#include "../../model/Scale.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// What a device card says in words (docs/DEVICES.md §1.1): which settings sit on which picture, the values a tag
/// steps through, how a tag reads, what typing in its panel finds, and each picture's caption. No Rack here, so it is
/// unit tested; the strip (DeviceStrip) and the pictures (DeviceTiles) draw from it.
namespace tessera::tags {

/// How a setting's tag behaves and how its panel lays out its choices.
enum class Family : uint8_t {
    Plain,     // a short list of names, chips in a grid
    Number,    // a number in steps; the panel shows a few of them
    Length,    // note lengths: straight, triplet and dotted rows (signed for a playhead's Offset)
    Rate,      // a playhead's speed: faster, normal, slower
    Curve,     // a playhead's Curve: each shape drawn
    Direction, // a playhead's direction, or a step order with its step size
    Toggle,    // a small switch
    Channels   // a set of MIDI channels (the Note Filter)
};

/// One tag on a picture: the setting it shows (a device knob, or for the Playheads card a playhead setting), the word
/// shown before its value where the number alone is unclear, and the step a number moves by.
struct TagSpec {
    int param;
    Family family;
    const char* prefix = "";
    float step = 1;
};

/// One picture on a card: its width and the tags on it, the first one tinted.
struct TileSpec {
    float width;
    std::vector<TagSpec> tags;
};

/// The pictures of a device type, left to right. The Playheads card's tags are playhead settings.
const std::vector<TileSpec>& tilesOf(DeviceType type);

/// A playhead's Direction tag stands for two settings: values 0..3 are the directions, 4..7 the step orders after
/// "In order" (Outside in, Inside out, Odd then even, Shuffle).
constexpr int kDirectionValues = 8;

/// A setting's values as one device shows them now, in display units (a choice's index, a number in its units),
/// indexed by parameter. `channels` is the Note Filter's channel set (bit n = channel n + 1; 0 = any).
struct Values {
    std::array<float, 32> v{};
    uint16_t channels = 0;
    /// The key the device works in now, for captions that name notes (the Scale device on Clip or Song).
    int keyRoot = 0;
    uint16_t keyMask = 0xFFF;
    float operator[](int k) const { return k >= 0 && k < int(v.size()) ? v[size_t(k)] : 0.f; }
    int i(int k) const;
};

/// Every value a tag steps through, in the order dragging goes (short to long, slow to fast, low to high).
struct ValueList {
    Family family = Family::Plain;
    std::vector<float> values;        // display units
    std::vector<std::string> labels;  // as the tag and the panel read: "1/16 T", "+3 st", "50%", "−1/8"
    int def = 0;                      // the default's place in the list
    int zero = -1;                    // a signed length list: where "0" is
    int size() const { return int(values.size()); }
    /// The place of the value nearest to `value` (display units).
    int nearest(float value) const;
};

/// The values of tag `spec` on a device of `type` (a playhead setting on the Playheads card).
ValueList valueList(DeviceType type, const TagSpec& spec);
/// The Direction tag's eight values: the directions, then the step orders.
ValueList directionList();

/// A value as the tag reads it, cut into the parts it is typeset from: "−" or "+", a whole number or a fraction, a
/// small unit ("st", "%", "×") and a small mark (T for triplet, D for dotted). Plain names are all in `text`.
struct ValueText {
    std::string sign;
    std::string text;     // a name, or the whole number
    std::string num, den; // a fraction, stacked when drawn
    std::string unit;
    std::string mark;
};
ValueText splitLabel(const std::string& label);

/// Whether a tag shows faint (it does nothing now: Times and Curve while Piece is Off) or not at all (Note Length
/// shows only the setting its Mode uses).
bool dimmed(DeviceType type, int param, const Values& v);
bool hidden(DeviceType type, int param, const Values& v);

/// What typing in a panel finds: the closest value's place, and whether it is exactly what was typed. "1/8t",
/// "3/16", "1.5", "reverse", "ease out", "-1/16".
struct Match {
    int index = -1;
    bool exact = false;
};
std::optional<Match> match(const ValueList& list, const std::string& query);

/// "Any", "All", "1, 2, 10", "1–4, 10": a channel set (bit n = channel n + 1) as its tag reads.
std::string channelsText(uint16_t mask);
/// "1-4, 10", "any", "all": a typed channel set; nullopt when it can't be read.
std::optional<uint16_t> parseChannels(const std::string& text);

/// Picture `tile`'s caption for a device of `type`: what the picture can't say, following the values.
std::string caption(DeviceType type, int tile, const Values& v);

/// How many sixteenths a length value lasts in 4/4 (a playhead's Piece or Offset, a device's Rate): the pictures
/// and captions draw lengths in these.
double sixteenthsOf(DeviceType type, int param, float value);
/// A playhead's speed for its Rate value (1 = normal).
double rateOf(float value);
/// The pitches a Chord device builds on middle C in C major, low to high, as the engine builds them.
std::vector<int> chordOnC(const Values& v);
/// Which of `n` held notes an Arpeggiator's `pattern` plays at step `k`, as its picture and caption show it (the
/// random patterns use fixed numbers, so the picture stays still).
int arpStep(int pattern, int n, int k);

} // namespace tessera::tags
