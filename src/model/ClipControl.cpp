#include "ClipControl.hpp"

#include "Json.hpp"

#include <algorithm>
#include <cmath>

namespace tessera::clipControl {

using namespace json;

namespace {

// The 128 colours of the pad colour list, by index, as the controllers' programming guides give them.
constexpr uint32_t kPalette[128] = {
    0x000000, 0x1E1E1E, 0x7F7F7F, 0xFFFFFF, 0xFF4C4C, 0xFF0000, 0x590000, 0x190000, 0xFFBD6C, 0xFF5400, 0x591D00,
    0x271B00, 0xFFFF4C, 0xFFFF00, 0x595900, 0x191900, 0x88FF4C, 0x54FF00, 0x1D5900, 0x142B00, 0x4CFF4C, 0x00FF00,
    0x005900, 0x001900, 0x4CFF5E, 0x00FF19, 0x00590D, 0x001902, 0x4CFF88, 0x00FF55, 0x00591D, 0x001F12, 0x4CFFB7,
    0x00FF99, 0x005935, 0x001912, 0x4CC3FF, 0x00A9FF, 0x004152, 0x001019, 0x4C88FF, 0x0055FF, 0x001D59, 0x000819,
    0x4C4CFF, 0x0000FF, 0x000059, 0x000019, 0x874CFF, 0x5400FF, 0x190064, 0x0F0030, 0xFF4CFF, 0xFF00FF, 0x590059,
    0x190019, 0xFF4C87, 0xFF0054, 0x59001D, 0x220013, 0xFF1500, 0x993500, 0x795100, 0x436400, 0x033900, 0x005735,
    0x00547F, 0x0000FF, 0x00454F, 0x2500CC, 0x7F7F7F, 0x202020, 0xFF0000, 0xBDFF2D, 0xAFED06, 0x64FF09, 0x108B00,
    0x00FF87, 0x00A9FF, 0x002AFF, 0x3F00FF, 0x7A00FF, 0xB21A7D, 0x402100, 0xFF4A00, 0x88E106, 0x72FF15, 0x00FF00,
    0x3BFF26, 0x59FF71, 0x38FFCC, 0x5B8AFF, 0x3151C6, 0x877FE9, 0xD31DFF, 0xFF005D, 0xFF7F00, 0xB9B000, 0x90FF00,
    0x835D07, 0x392B00, 0x144C10, 0x0D5038, 0x15152A, 0x16205A, 0x693C1C, 0xA8000A, 0xDE513D, 0xD86A1C, 0xFFE126,
    0x9EE12F, 0x67B50F, 0x1E1E30, 0xDCFF6B, 0x80FFBD, 0x9A99FF, 0x8E66FF, 0x404040, 0x757575, 0xE0FFFF, 0xA00000,
    0x350000, 0x1AD000, 0x074200, 0xB9B000, 0x3F3100, 0xB35F00, 0x4B1502};

constexpr int kRed = 5;
constexpr int kDimGrey = 1;
constexpr int kGrey = 2;
constexpr int kDimRed = 7;

const char* const kLayoutIds[] = {"off", "launchpadX", "launchpadMini", "apcMini", "noteGrid"};
const char* const kKindIds[] = {"none", "slot", "scene", "stopTrack", "stopAll", "up", "down", "left", "right"};
const char* const kTypeIds[] = {"note", "cc", "program"};

template <typename E, size_t N>
E fromName(const json_t* j, const char* key, const char* const (&names)[N], E fallback) {
    const char* s = json_string_value(json_object_get(j, key));
    for (size_t i = 0; s && i < N; i++)
        if (std::string(s) == names[i])
            return E(i);
    return fallback;
}

/// A colour scaled toward black, for a clip that waits (a lit but quiet pad).
uint32_t dimmed(uint32_t rgb, float k) {
    auto c = [&](int shift) { return uint32_t(std::lround(float((rgb >> shift) & 0xFF) * k)) << shift; };
    return c(16) | c(8) | c(0);
}

/// The window row a grid pad's row shows: pads count from the bottom, the window's scenes from the top.
Action slotAt(int column, int rowFromBottom, const Window& w) {
    Action a;
    a.kind = Kind::Slot;
    a.column = column;
    a.scene = w.scene + (kSize - 1 - rowFromBottom);
    return a;
}

} // namespace

const char* layoutName(Layout l) {
    switch (l) {
    case Layout::LaunchpadX:
        return "Launchpad X";
    case Layout::LaunchpadMini:
        return "Launchpad Mini MK3";
    case Layout::ApcMini:
        return "APC mini MK2";
    case Layout::NoteGrid:
        return "Any 8x8 pads (notes)";
    case Layout::Off:
    default:
        return "Learned controls only";
    }
}

json_t* Settings::toJson() const {
    json_t* j = json_object();
    setString(j, "layout", kLayoutIds[int(layout)]);
    setInt(j, "noteBase", noteBase);
    setInt(j, "channel", channel);
    setBool(j, "programScenes", programScenes);
    json_t* list = json_array();
    for (const Binding& b : learned) {
        json_t* e = json_object();
        setString(e, "type", kTypeIds[int(b.type)]);
        setInt(e, "channel", b.channel);
        setInt(e, "number", b.number);
        setString(e, "kind", kKindIds[int(b.kind)]);
        if (b.track.valid())
            setInt(e, "track", int64_t(b.track.value));
        if (b.scene >= 0)
            setInt(e, "scene", b.scene);
        json_array_append_new(list, e);
    }
    set(j, "learned", list);
    return j;
}

Settings Settings::fromJson(const json_t* j) {
    Settings s;
    if (!json_is_object(j))
        return s;
    s.layout = fromName(j, "layout", kLayoutIds, s.layout);
    s.noteBase = std::clamp(int(getInt(j, "noteBase", s.noteBase)), 0, 127 - kSize * kSize + 1);
    s.channel = std::clamp(int(getInt(j, "channel", s.channel)), 0, 16);
    s.programScenes = getBool(j, "programScenes", s.programScenes);
    forEach(get(j, "learned"), [&](const json_t* e) {
        Binding b;
        b.type = fromName(e, "type", kTypeIds, b.type);
        b.channel = uint8_t(std::clamp(int(getInt(e, "channel", 0)), 0, 15));
        b.number = uint8_t(std::clamp(int(getInt(e, "number", 0)), 0, 127));
        b.kind = fromName(e, "kind", kKindIds, Kind::None);
        b.track = TrackId(uint64_t(std::max<int64_t>(0, getInt(e, "track", 0))));
        b.scene = int(getInt(e, "scene", -1));
        if (b.kind != Kind::None)
            s.learned.push_back(b);
    });
    return s;
}

bool controlOf(const uint8_t* bytes, int size, Binding& out) {
    if (size < 2)
        return false;
    uint8_t status = bytes[0] & 0xF0;
    out.channel = bytes[0] & 0x0F;
    out.number = bytes[1] & 0x7F;
    if (status == 0x90 && size >= 3 && bytes[2] > 0) {
        out.type = Binding::Type::Note;
        return true;
    }
    if (status == 0xB0 && size >= 3 && bytes[2] > 0) {
        out.type = Binding::Type::Cc;
        return true;
    }
    if (status == 0xC0) {
        out.type = Binding::Type::Program;
        return true;
    }
    return false;
}

Action decode(const Settings& s, const Window& w, InputState& state, const uint8_t* bytes, int size) {
    Action none;
    if (size < 2)
        return none;
    uint8_t status = bytes[0] & 0xF0;
    int channel = bytes[0] & 0x0F;
    int number = bytes[1] & 0x7F;
    int value = size >= 3 ? bytes[2] & 0x7F : 0;
    bool noteOff = status == 0x80 || (status == 0x90 && value == 0);

    // The APC mini's Shift changes what its track buttons do while it is held.
    if (s.layout == Layout::ApcMini && number == 0x7A && (status == 0x90 || status == 0x80)) {
        state.shift = !noteOff;
        return none;
    }
    bool anyChannel = s.channel == 0 || channel == s.channel - 1;

    // Learned controls come first: they can take over a grid button too.
    Binding pressed;
    if (anyChannel && controlOf(bytes, size, pressed))
        for (const Binding& b : s.learned)
            if (b.sameControl(pressed)) {
                Action a;
                a.kind = b.kind;
                a.track = b.track;
                a.scene = b.scene;
                return a;
            }

    if (status == 0xC0 && s.programScenes && anyChannel) {
        Action a;
        a.kind = Kind::Scene;
        a.scene = number;
        return a;
    }
    if (noteOff || !(status == 0x90 || status == 0xB0) || value == 0)
        return none;

    switch (s.layout) {
    case Layout::LaunchpadX:
    case Layout::LaunchpadMini: {
        int row = number / 10, col = number % 10;
        if (status == 0x90 && row >= 1 && row <= kSize && col >= 1 && col <= kSize)
            return slotAt(col - 1, row - 1, w);
        if (status != 0xB0)
            return none;
        Action a;
        if (col == 9 && row >= 1 && row <= kSize) { // the right column launches the window's scenes
            a.kind = Kind::Scene;
            a.scene = w.scene + (kSize - row);
            return a;
        }
        static const Kind top[] = {Kind::Up, Kind::Down, Kind::Left, Kind::Right};
        if (number >= 91 && number <= 94)
            a.kind = top[number - 91];
        else if (number == 98)
            a.kind = Kind::StopAll;
        return a;
    }
    case Layout::ApcMini: {
        if (status != 0x90)
            return none;
        if (number < kSize * kSize)
            return slotAt(number % kSize, number / kSize, w);
        Action a;
        if (number >= 0x64 && number < 0x64 + kSize) { // the track buttons stop; with Shift, the last four move
            int i = number - 0x64;
            static const Kind arrows[] = {Kind::Up, Kind::Down, Kind::Left, Kind::Right};
            if (state.shift && i >= 4) {
                a.kind = arrows[i - 4];
            } else {
                a.kind = Kind::StopTrack;
                a.column = i;
            }
        } else if (number >= 0x70 && number < 0x70 + kSize) {
            a.kind = Kind::Scene;
            a.scene = w.scene + (number - 0x70);
        }
        return a;
    }
    case Layout::NoteGrid: {
        int i = number - s.noteBase;
        if (status != 0x90 || !anyChannel || i < 0 || i >= kSize * kSize)
            return none;
        return slotAt(i % kSize, i / kSize, w);
    }
    case Layout::Off:
    default:
        return none;
    }
}

Window clamped(const Window& w, int tracks, int scenes) {
    Window out;
    out.track = std::clamp(w.track, 0, std::max(0, tracks - kSize));
    out.scene = std::clamp(w.scene, 0, std::max(0, scenes - kSize));
    return out;
}

Window moved(const Window& w, Kind kind, int tracks, int scenes) {
    Window out = w;
    if (kind == Kind::Up)
        out.scene--;
    else if (kind == Kind::Down)
        out.scene++;
    else if (kind == Kind::Left)
        out.track--;
    else if (kind == Kind::Right)
        out.track++;
    return clamped(out, tracks, scenes);
}

int paletteIndex(uint32_t rgb) {
    if ((rgb & 0xFFFFFF) == 0)
        return 0;
    int best = 1;
    long bestDistance = -1;
    for (int i = 1; i < 128; i++) {
        long d = 0;
        for (int shift : {16, 8, 0}) {
            long a = long((rgb >> shift) & 0xFF), b = long((kPalette[i] >> shift) & 0xFF);
            d += (a - b) * (a - b);
        }
        if (bestDistance < 0 || d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

std::vector<Message> enter(Layout layout) {
    // Programmer mode: every pad and button sends and lights by its own number.
    if (layout == Layout::LaunchpadX)
        return {{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7}};
    if (layout == Layout::LaunchpadMini)
        return {{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, 0x01, 0xF7}};
    return {};
}

std::vector<Message> leave(Layout layout) {
    if (layout == Layout::LaunchpadX)
        return {{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x00, 0xF7}};
    if (layout == Layout::LaunchpadMini)
        return {{0xF0, 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, 0x00, 0xF7}};
    return {};
}

std::vector<Message> lights(Layout layout, const Surface& s, std::map<int, Message>& lit) {
    std::vector<Message> out;
    if (!hasLights(layout))
        return out;
    // Sends `want` for light `key` when it differs from what that light last got.
    auto light = [&](int key, Message want) {
        auto it = lit.find(key);
        if (it != lit.end() && it->second == want)
            return;
        for (size_t i = 0; i + 2 < want.size(); i += 3)
            out.push_back({want[i], want[i + 1], want[i + 2]});
        lit[key] = std::move(want);
    };
    bool launchpad = layout != Layout::ApcMini;
    for (int row = 0; row < kSize; row++)
        for (int col = 0; col < kSize; col++) {
            const Cell& c = s.cells[size_t(row)][size_t(col)];
            int rowFromBottom = kSize - 1 - row;
            uint8_t pad = uint8_t(launchpad ? (rowFromBottom + 1) * 10 + col + 1 : rowFromBottom * kSize + col);
            uint8_t colour = uint8_t(paletteIndex(c.color ? c.color : 0x7F7F7F));
            uint8_t quiet = uint8_t(paletteIndex(dimmed(c.color ? c.color : 0x7F7F7F, 0.35f)));
            Message m;
            if (launchpad) {
                // Channel 1 lights steady, 2 flashes (between the steady colour and this one), 3 pulses.
                switch (c.state) {
                case Cell::State::Empty:
                    m = {0x90, pad, 0};
                    break;
                case Cell::State::Clip:
                    m = {0x90, pad, quiet};
                    break;
                case Cell::State::Playing:
                    m = {0x92, pad, colour};
                    break;
                case Cell::State::Queued:
                    m = {0x90, pad, quiet, 0x91, pad, colour};
                    break;
                case Cell::State::Recording:
                    m = {0x92, pad, kRed};
                    break;
                case Cell::State::Stopping:
                    m = {0x90, pad, colour, 0x91, pad, 0};
                    break;
                }
            } else {
                // The channel picks brightness or motion: 2 = 50%, 6 = full, 9 = pulsing 1/4, 13 = blinking 1/8.
                switch (c.state) {
                case Cell::State::Empty:
                    m = {0x90, pad, 0};
                    break;
                case Cell::State::Clip:
                    m = {0x92, pad, colour};
                    break;
                case Cell::State::Playing:
                    m = {0x99, pad, colour};
                    break;
                case Cell::State::Queued:
                case Cell::State::Stopping:
                    m = {0x9D, pad, colour};
                    break;
                case Cell::State::Recording:
                    m = {0x99, pad, kRed};
                    break;
                }
            }
            light(pad, std::move(m));
        }
    if (launchpad) {
        for (int i = 0; i < kSize; i++) { // the right column: lit where a scene holds clips
            uint8_t cc = uint8_t((kSize - i) * 10 + 9);
            light(0x100 | cc, {0xB0, cc, uint8_t(s.sceneHasClips[size_t(i)] ? kGrey : 0)});
        }
        bool ways[] = {s.up, s.down, s.left, s.right};
        for (int i = 0; i < 4; i++)
            light(0x100 | (91 + i), {0xB0, uint8_t(91 + i), uint8_t(ways[i] ? kDimGrey : 0)});
        light(0x100 | 98, {0xB0, 98, uint8_t(s.anyPlays ? kDimRed : 0)});
    } else {
        // Single-colour buttons: 0 off, 1 on, 2 blinking.
        for (int i = 0; i < kSize; i++) {
            light(0x64 + i, {0x90, uint8_t(0x64 + i), uint8_t(s.trackPlays[size_t(i)] ? 1 : 0)});
            light(0x70 + i, {0x90, uint8_t(0x70 + i), uint8_t(s.sceneHasClips[size_t(i)] ? 1 : 0)});
        }
    }
    return out;
}

} // namespace tessera::clipControl
