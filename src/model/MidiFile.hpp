#pragma once

#include "Types.hpp"

#include <cstdint>
#include <string>
#include <vector>

/// Standard MIDI Files (DESIGN.md §17): a writer for export, and a small reader the tests use to check what was
/// written. No dependencies.
namespace tessera::smf {

/// One event: a channel message (status byte first) or a meta event (0xFF, type, then the data, no length).
struct Event {
    Tick tick = 0;
    std::vector<uint8_t> bytes;
    /// Ties at the same tick: lower first. Meta events, then note-offs, then everything else, then note-ons, so a
    /// note ending where the next one starts never cuts it off.
    int order = 2;

    bool isMeta() const { return !bytes.empty() && bytes[0] == 0xFF; }
    uint8_t status() const { return bytes.empty() ? 0 : bytes[0]; }
};

struct Track {
    std::vector<Event> events;
};

struct File {
    int format = 1;
    int ppq = int(kPpq);
    std::vector<Track> tracks;
};

// Building events. Channels are 0..15.
Event noteOn(Tick t, int channel, int pitch, int velocity);
Event noteOff(Tick t, int channel, int pitch, int velocity);
Event controlChange(Tick t, int channel, int controller, int value);
/// `value` -8192..8191, 0 = centre.
Event pitchBend(Tick t, int channel, int value);
Event channelPressure(Tick t, int channel, int value);
Event polyPressure(Tick t, int channel, int pitch, int value);
Event meta(Tick t, uint8_t type, const std::string& data);
Event trackName(Tick t, const std::string& name);
Event marker(Tick t, const std::string& name);
Event cuePoint(Tick t, const std::string& name);
Event tempo(Tick t, double bpm);
Event timeSignature(Tick t, int numerator, int denominator);

/// MIDI velocity 1..127 from 0..1 (a note-on must not be 0, which means note-off).
int velocity7(float v);
/// 0..127 from 0..1.
int value7(float v);

/// The file's bytes. Each track's events are sorted (stably, by tick then order) and get an end-of-track.
std::vector<uint8_t> write(const File& file);
bool writeFile(const File& file, const std::string& path);

/// Reads a file written by write() (or any plain SMF). Running status is understood. Returns false on bad data.
bool read(const std::vector<uint8_t>& bytes, File& out);

/// Meta event type numbers.
enum MetaType : uint8_t {
    kText = 0x01,
    kTrackName = 0x03,
    kMarker = 0x06,
    kCuePoint = 0x07,
    kEndOfTrack = 0x2F,
    kTempo = 0x51,
    kTimeSignature = 0x58,
};

} // namespace tessera::smf
