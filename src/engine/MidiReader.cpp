#include "MidiReader.hpp"

#include <algorithm>

namespace tessera {

namespace {

constexpr int kLowerMaster = 0;
constexpr int kUpperMaster = 15;
constexpr uint8_t kCcDataEntry = 6;
constexpr uint8_t kCcTimbre = 74;
constexpr uint8_t kCcRpnLsb = 100;
constexpr uint8_t kCcRpnMsb = 101;

/// A 14-bit pitch bend as -1..1, centre 0, both ends reaching exactly ±1.
float bendValue(uint8_t lsb, uint8_t msb) {
    int raw = ((msb & 127) << 7 | (lsb & 127)) - 8192;
    return raw < 0 ? raw / 8192.f : raw / 8191.f;
}

/// A 7-bit data byte as 0..1.
float unit(uint8_t byte) { return (byte & 127) / 127.f; }

/// An event about one note.
MidiEvent noteEvent(MidiEvent::Kind kind, int channel, int pitch, float value) {
    MidiEvent e;
    e.kind = kind;
    e.channel = uint8_t(channel);
    e.pitch = int8_t(pitch);
    e.value = value;
    return e;
}

/// A value for one of a note's curves; pitch -1 means every note held on the channel.
MidiEvent curveEvent(int channel, int pitch, NoteCurve curve, float value) {
    MidiEvent e = noteEvent(MidiEvent::Kind::NoteValue, channel, pitch, value);
    e.curve = curve;
    return e;
}

/// A value the whole channel shares.
MidiEvent channelEvent(int channel, ChannelControl control, uint8_t cc, float value) {
    MidiEvent e;
    e.kind = MidiEvent::Kind::ChannelValue;
    e.channel = uint8_t(channel);
    e.control = control;
    e.cc = cc;
    e.value = value;
    return e;
}

} // namespace

void MidiReader::setMpe(bool on) {
    mpeOn = on;
    zones = {};
    channels = {};
    if (on)
        configureZone(0, 15);
}

int MidiReader::zoneOf(int channel) const {
    if (!mpeOn)
        return -1;
    if (zones[0].members > 0 && channel >= kLowerMaster && channel <= kLowerMaster + zones[0].members)
        return 0;
    if (zones[1].members > 0 && channel <= kUpperMaster && channel >= kUpperMaster - zones[1].members)
        return 1;
    return -1;
}

bool MidiReader::isMaster(int channel) const {
    int zone = zoneOf(channel);
    return zone >= 0 && channel == (zone == 0 ? kLowerMaster : kUpperMaster);
}

void MidiReader::configureZone(int zone, int members) {
    members = std::clamp(members, 0, 15);
    zones[size_t(zone)] = Zone{members};
    // Both zones together need a master each, so they share the 14 channels left between the two masters.
    Zone& other = zones[size_t(1 - zone)];
    other.members = std::min(other.members, members >= 15 ? 0 : std::max(0, 14 - members));
}

void MidiReader::dataEntry(int channel, int value) {
    ChannelState& c = channels[size_t(channel)];
    if (c.rpnMsb != 0)
        return;
    if (c.rpnLsb == 6 && (channel == kLowerMaster || channel == kUpperMaster)) {
        // The configuration message turns MPE reading on: a controller that sends it is speaking MPE.
        if (!mpeOn) {
            mpeOn = true;
            zones = {};
        }
        configureZone(channel == kLowerMaster ? 0 : 1, value);
        return;
    }
    int zone = zoneOf(channel);
    if (c.rpnLsb == 0 && zone >= 0) {
        Zone& z = zones[size_t(zone)];
        (isMaster(channel) ? z.masterBend : z.noteBend) = float(value);
    }
}

int MidiReader::read(const uint8_t* bytes, int size, std::array<MidiEvent, kMaxEvents>& out) {
    if (size < 2)
        return 0;
    uint8_t status = bytes[0] >> 4;
    int channel = bytes[0] & 15;
    uint8_t d1 = bytes[1] & 127;
    uint8_t d2 = size > 2 ? bytes[2] & 127 : 0;
    int zone = zoneOf(channel);
    bool member = zone >= 0 && !isMaster(channel);
    // In a zone, values for the whole zone come from its master channel; outside one, each channel is its own.
    int sharedChannel = zone < 0 ? channel : (zone == 0 ? kLowerMaster : kUpperMaster);
    ChannelState& state = channels[size_t(channel)];

    switch (status) {
    case 0x9:
        if (d2 > 0) {
            int n = 0;
            out[size_t(n++)] = noteEvent(MidiEvent::Kind::NoteOn, channel, d1, unit(d2));
            if (member) {
                // MPE sends a note's starting bend, timbre and pressure just before its note-on.
                if (state.hasBend)
                    out[size_t(n++)] = curveEvent(channel, d1, NoteCurve::Pitch, state.bend);
                if (state.hasTimbre)
                    out[size_t(n++)] = curveEvent(channel, d1, NoteCurve::Timbre, state.timbre);
                if (state.hasPressure)
                    out[size_t(n++)] = curveEvent(channel, d1, NoteCurve::Pressure, state.pressure);
            }
            return n;
        }
        // A note-on with velocity 0 is a note-off (keyboards using running status send these).
        out[0] = noteEvent(MidiEvent::Kind::NoteOff, channel, d1, 0.5f);
        return 1;
    case 0x8:
        out[0] = noteEvent(MidiEvent::Kind::NoteOff, channel, d1, unit(d2));
        return 1;
    case 0xA:
        out[0] = curveEvent(channel, d1, NoteCurve::Pressure, unit(d2));
        return 1;
    case 0xD:
        if (member) {
            state.pressure = unit(d1);
            state.hasPressure = true;
            out[0] = curveEvent(channel, -1, NoteCurve::Pressure, state.pressure);
        } else {
            out[0] = channelEvent(sharedChannel, ChannelControl::Pressure, 0, unit(d1));
        }
        return 1;
    case 0xE:
        if (member) {
            state.bend = bendValue(d1, d2) * zones[size_t(zone)].noteBend;
            state.hasBend = true;
            out[0] = curveEvent(channel, -1, NoteCurve::Pitch, state.bend);
        } else {
            out[0] = channelEvent(sharedChannel, ChannelControl::PitchBend, 0, bendValue(d1, d2));
        }
        return 1;
    case 0xB:
        switch (d1) {
        case kCcRpnMsb:
            state.rpnMsb = d2;
            return 0;
        case kCcRpnLsb:
            state.rpnLsb = d2;
            return 0;
        case kCcDataEntry:
            dataEntry(channel, d2);
            return 0;
        case 38:  // data entry fine (cents): bend ranges are kept in whole semitones
        case 98:  // NRPN select: nothing in Tessera answers to one
        case 99:
            return 0;
        default:
            break;
        }
        if (member && d1 == kCcTimbre) {
            state.timbre = unit(d2);
            state.hasTimbre = true;
            out[0] = curveEvent(channel, -1, NoteCurve::Timbre, state.timbre);
            return 1;
        }
        out[0] = channelEvent(sharedChannel, ChannelControl::Cc, d1, unit(d2));
        return 1;
    default:
        return 0;
    }
}

} // namespace tessera
