#include "MidiFile.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tessera::smf {

namespace {

uint8_t byte7(int v) { return uint8_t(std::clamp(v, 0, 127)); }

Event channelEvent(Tick t, int order, std::vector<uint8_t> bytes) {
    Event e;
    e.tick = t;
    e.bytes = std::move(bytes);
    e.order = order;
    return e;
}

void put16(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    put16(out, v >> 16);
    put16(out, v & 0xFFFF);
}

/// Variable-length quantity: 7 bits per byte, high bit set on all but the last.
void putVlq(std::vector<uint8_t>& out, uint32_t v) {
    uint8_t buf[5];
    int n = 0;
    buf[n++] = uint8_t(v & 0x7F);
    while (v >>= 7)
        buf[n++] = uint8_t((v & 0x7F) | 0x80);
    while (n > 0)
        out.push_back(buf[--n]);
}

/// The number of data bytes after a channel status byte.
int dataLength(uint8_t status) {
    switch (status & 0xF0) {
    case 0xC0:
    case 0xD0:
        return 1;
    default:
        return 2;
    }
}

} // namespace

Event noteOn(Tick t, int ch, int pitch, int vel) {
    return channelEvent(t, 3, {uint8_t(0x90 | (ch & 15)), byte7(pitch), uint8_t(std::clamp(vel, 1, 127))});
}

Event noteOff(Tick t, int ch, int pitch, int vel) {
    return channelEvent(t, 1, {uint8_t(0x80 | (ch & 15)), byte7(pitch), byte7(vel)});
}

Event controlChange(Tick t, int ch, int controller, int value) {
    return channelEvent(t, 2, {uint8_t(0xB0 | (ch & 15)), byte7(controller), byte7(value)});
}

Event pitchBend(Tick t, int ch, int value) {
    int v = std::clamp(value + 8192, 0, 16383);
    return channelEvent(t, 2, {uint8_t(0xE0 | (ch & 15)), uint8_t(v & 0x7F), uint8_t(v >> 7)});
}

Event channelPressure(Tick t, int ch, int value) {
    return channelEvent(t, 2, {uint8_t(0xD0 | (ch & 15)), byte7(value)});
}

Event polyPressure(Tick t, int ch, int pitch, int value) {
    return channelEvent(t, 2, {uint8_t(0xA0 | (ch & 15)), byte7(pitch), byte7(value)});
}

Event meta(Tick t, uint8_t type, const std::string& data) {
    Event e;
    e.tick = t;
    e.order = 0;
    e.bytes.push_back(0xFF);
    e.bytes.push_back(type);
    e.bytes.insert(e.bytes.end(), data.begin(), data.end());
    return e;
}

Event trackName(Tick t, const std::string& name) { return meta(t, kTrackName, name); }
Event marker(Tick t, const std::string& name) { return meta(t, kMarker, name); }
Event cuePoint(Tick t, const std::string& name) { return meta(t, kCuePoint, name); }

Event tempo(Tick t, double bpm) {
    uint32_t us = uint32_t(std::lround(60000000.0 / std::clamp(bpm, 1.0, 1000.0)));
    std::string data{char(us >> 16), char((us >> 8) & 0xFF), char(us & 0xFF)};
    return meta(t, kTempo, data);
}

Event timeSignature(Tick t, int numerator, int denominator) {
    int power = 0;
    while ((1 << (power + 1)) <= denominator)
        power++;
    // 24 MIDI clocks per metronome click, 8 thirty-seconds per quarter.
    std::string data{char(numerator), char(power), char(24), char(8)};
    return meta(t, kTimeSignature, data);
}

int velocity7(float v) { return std::clamp(int(std::lround(double(v) * 127)), 1, 127); }
int value7(float v) { return std::clamp(int(std::lround(double(v) * 127)), 0, 127); }

std::vector<uint8_t> write(const File& file) {
    std::vector<uint8_t> out;
    out.insert(out.end(), {'M', 'T', 'h', 'd'});
    put32(out, 6);
    put16(out, uint16_t(file.format));
    put16(out, uint16_t(file.tracks.size()));
    put16(out, uint16_t(file.ppq));
    for (const Track& track : file.tracks) {
        std::vector<Event> events = track.events;
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });
        std::vector<uint8_t> body;
        Tick last = 0;
        for (const Event& e : events) {
            if (e.bytes.empty())
                continue;
            Tick t = std::max<Tick>(0, e.tick);
            putVlq(body, uint32_t(std::max<Tick>(0, t - last)));
            last = std::max(last, t);
            if (e.isMeta()) {
                body.push_back(0xFF);
                body.push_back(e.bytes.size() > 1 ? e.bytes[1] : 0);
                uint32_t length = uint32_t(e.bytes.size() > 2 ? e.bytes.size() - 2 : 0);
                putVlq(body, length);
                body.insert(body.end(), e.bytes.begin() + std::min<ptrdiff_t>(2, ptrdiff_t(e.bytes.size())),
                            e.bytes.end());
            } else {
                body.insert(body.end(), e.bytes.begin(), e.bytes.end());
            }
        }
        putVlq(body, 0);
        body.insert(body.end(), {0xFF, kEndOfTrack, 0x00});
        out.insert(out.end(), {'M', 'T', 'r', 'k'});
        put32(out, uint32_t(body.size()));
        out.insert(out.end(), body.begin(), body.end());
    }
    return out;
}

bool writeFile(const File& file, const std::string& path) {
    std::vector<uint8_t> bytes = write(file);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

bool read(const std::vector<uint8_t>& bytes, File& out) {
    size_t pos = 0;
    auto need = [&](size_t n) { return pos + n <= bytes.size(); };
    auto get16 = [&] {
        uint32_t v = uint32_t(bytes[pos]) << 8 | bytes[pos + 1];
        pos += 2;
        return v;
    };
    auto get32 = [&] {
        uint32_t v = get16() << 16;
        return v | get16();
    };
    auto getVlq = [&](size_t end, uint32_t& v) {
        v = 0;
        for (int i = 0; i < 4; i++) {
            if (pos >= end)
                return false;
            uint8_t b = bytes[pos++];
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80))
                return true;
        }
        return false;
    };
    if (!need(14) || std::string(bytes.begin(), bytes.begin() + 4) != "MThd")
        return false;
    pos = 4;
    if (get32() != 6)
        return false;
    out.format = int(get16());
    uint32_t count = get16();
    out.ppq = int(get16());
    out.tracks.clear();
    for (uint32_t k = 0; k < count; k++) {
        if (!need(8) || std::string(bytes.begin() + long(pos), bytes.begin() + long(pos) + 4) != "MTrk")
            return false;
        pos += 4;
        size_t length = get32();
        if (!need(length))
            return false;
        size_t end = pos + length;
        Track track;
        Tick t = 0;
        uint8_t running = 0;
        while (pos < end) {
            uint32_t delta;
            if (!getVlq(end, delta))
                return false;
            t += delta;
            if (pos >= end)
                return false;
            Event e;
            e.tick = t;
            uint8_t status = bytes[pos];
            if (status == 0xFF) {
                if (pos + 2 > end)
                    return false;
                uint8_t type = bytes[pos + 1];
                pos += 2;
                uint32_t len;
                if (!getVlq(end, len) || pos + len > end)
                    return false;
                e.bytes = {0xFF, type};
                e.bytes.insert(e.bytes.end(), bytes.begin() + long(pos), bytes.begin() + long(pos + len));
                pos += len;
                e.order = 0;
                if (type == kEndOfTrack)
                    break;
            } else if (status == 0xF0 || status == 0xF7) {
                pos++;
                uint32_t len;
                if (!getVlq(end, len) || pos + len > end)
                    return false;
                pos += len;
                continue;
            } else {
                if (status & 0x80) {
                    running = status;
                    pos++;
                } else if (!running) {
                    return false;
                }
                int n = dataLength(running);
                if (pos + size_t(n) > end)
                    return false;
                e.bytes.push_back(running);
                for (int i = 0; i < n; i++)
                    e.bytes.push_back(bytes[pos++]);
            }
            track.events.push_back(e);
        }
        pos = end;
        out.tracks.push_back(std::move(track));
    }
    return true;
}

} // namespace tessera::smf
