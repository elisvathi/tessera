#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tessera {

/// Which of a note's own curves a per-note value belongs to (the note's pressure, timbre or pitch curve).
enum class NoteCurve : uint8_t { Pressure, Timbre, Pitch };

/// A value one MIDI channel shares among all its notes.
enum class ChannelControl : uint8_t { PitchBend, Pressure, Cc };

/// One thing a MIDI message means, after MPE has been worked out.
struct MidiEvent {
    enum class Kind : uint8_t {
        NoteOn,       // pitch, value = velocity 0..1
        NoteOff,      // pitch, value = release velocity 0..1
        NoteValue,    // curve, pitch (-1 = every note held on the channel), value: 0..1, or semitones for Pitch
        ChannelValue, // control (+ cc), value: -1..1 for PitchBend, else 0..1
    };
    Kind kind = Kind::NoteOn;
    uint8_t channel = 0; // 0..15; for a ChannelValue in MPE mode, the zone's master channel
    int8_t pitch = 0;
    NoteCurve curve = NoteCurve::Pressure;
    ChannelControl control = ChannelControl::Cc;
    uint8_t cc = 0;
    float value = 0;
};

/// Reads raw MIDI 1 messages and says what they mean for notes and lanes (docs/design/EXPRESSION_INPUT.md).
///
/// Normal mode: poly aftertouch is per-note pressure; pitch bend, channel pressure and CCs are per-channel values.
/// MPE mode: each member channel carries one note, so its pitch bend, CC 74 and channel pressure become that note's
/// pitch, timbre and pressure; the master channel's messages are per-channel values for the whole zone. Zones and
/// bend ranges follow the MPE Configuration Message (RPN 6) and RPN 0. Audio-thread safe: fixed-size state only.
class MidiReader {
  public:
    static constexpr int kMaxEvents = 4; // a note-on in MPE mode: the note and its three starting values
    static constexpr float kDefaultNoteBend = 48;
    static constexpr float kDefaultMasterBend = 2;

    /// Switches between normal and MPE reading; MPE starts as one lower zone of 15 member channels.
    void setMpe(bool on);
    bool mpe() const { return mpeOn; }

    /// The per-note bend range in semitones for a zone's member channels (0 = lower zone, 1 = upper).
    float noteBendRange(int zone) const { return zones[size_t(zone)].noteBend; }

    /// Reads one message of `size` bytes and writes what it means into `out`. Returns the number of events.
    int read(const uint8_t* bytes, int size, std::array<MidiEvent, kMaxEvents>& out);

  private:
    struct Zone {
        int members = 0; // 0 = no zone
        float noteBend = kDefaultNoteBend;
        float masterBend = kDefaultMasterBend;
    };
    /// What a member channel last sent, so a note started on it begins with those values.
    struct ChannelState {
        uint8_t rpnMsb = 127, rpnLsb = 127; // 127/127 = no RPN selected
        float pressure = 0, timbre = 0, bend = 0;
        bool hasPressure = false, hasTimbre = false, hasBend = false;
    };

    /// The zone a channel belongs to (0 lower, 1 upper), or -1 when MPE is off or the channel is in no zone.
    int zoneOf(int channel) const;
    /// Whether a channel is a zone's master channel.
    bool isMaster(int channel) const;
    /// Handles CC 6 (data entry) for the RPN selected on a channel: bend range or MPE configuration.
    void dataEntry(int channel, int value);
    /// Sets a zone's member count from an MPE Configuration Message, shrinking the other zone to fit.
    void configureZone(int zone, int members);

    bool mpeOn = false;
    std::array<Zone, 2> zones{};
    std::array<ChannelState, 16> channels{};
};

} // namespace tessera
