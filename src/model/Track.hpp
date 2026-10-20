#pragma once

#include "Automation.hpp"
#include "Device.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tessera {

enum class TrackType : uint8_t { Instrument, Drum };

/// Where an armed track takes its notes from (DESIGN.md §14).
enum class RecordInput : uint8_t { Any, Cv, Midi };

/// What a track plays besides its clips, all the time (no arming needed): nothing, the external inputs (recordInput
/// and midiChannel say which), or another track's notes after or before that track's devices. The notes go through
/// this track's own devices.
enum class ListenSource : uint8_t { None, External, TrackAfter, TrackBefore };

constexpr int kMaxDrumLanes = 16;

struct ClipSlot {
    std::optional<ClipId> clip;

    bool operator==(const ClipSlot& o) const { return clip == o.clip; }
};

/// An instance of a clip on the timeline. It references the clip, so editing the clip changes every instance.
struct ArrangerClip {
    ArrangementId id;
    ClipId clip;
    Tick position = 0;
    Tick length = 0; // may differ from the clip's length (loops or truncates)
    Tick offset = 0; // start offset into the clip
    bool muted = false;
    int8_t transpose = 0;    // non-destructive, per instance
    float velocityScale = 1; // non-destructive, per instance
    float stretch = 1;       // time-stretch factor

    bool operator==(const ArrangerClip& o) const {
        return id == o.id && clip == o.clip && position == o.position && length == o.length && offset == o.offset &&
               muted == o.muted && transpose == o.transpose && velocityScale == o.velocityScale && stretch == o.stretch;
    }
};

struct DrumLane {
    std::string name;
    int8_t pitch = 36;   // the note number this lane uses inside clips
    int outputIndex = 0; // trigger output channel
    bool mute = false;
    bool solo = false;
    float gateLengthMs = 0;     // 0 = trigger, > 0 = gate
    int chokeGroup = 0;         // 0 = none
    Tick loopLength = 0;        // polymeter: 0 = follow the clip loop
    Tick stepLength = kPpq / 4; // the step grid's rate for this lane
    uint32_t color = 0;         // 0 = the track's colour

    bool operator==(const DrumLane& o) const {
        return name == o.name && pitch == o.pitch && outputIndex == o.outputIndex && mute == o.mute && solo == o.solo &&
               gateLengthMs == o.gateLengthMs && chokeGroup == o.chokeGroup && loopLength == o.loopLength &&
               stepLength == o.stepLength && color == o.color;
    }
};

/// Track::polyphony value meaning "as many voices as the track's clips ever play at once".
constexpr int kAutoVoices = 0;

struct Track {
    TrackId id; // stable for the track's lifetime; output modules bind to this, not to the index
    std::string name;
    uint32_t color = 0; // 0xRRGGBB
    TrackType type = TrackType::Instrument;
    int polyphony = kAutoVoices; // 1..16, or kAutoVoices: as many as the track's clips need
    bool mute = false;
    bool solo = false;
    bool armed = false;
    bool monitor = true; // play incoming notes through while armed
    RecordInput recordInput = RecordInput::Any;
    int8_t midiChannel = 0;    // 1..16, or 0 for every channel
    ListenSource listen = ListenSource::None;
    TrackId listenTrack; // the track listened to, for TrackAfter / TrackBefore
    float gateLengthScale = 1; // 0..1, legato vs staccato at the output
    int8_t transpose = 0;
    std::vector<DrumLane> drumLanes;            // drum tracks only, up to kMaxDrumLanes
    std::vector<ClipSlot> slots;                // launcher slots, one per scene
    std::vector<ArrangerClip> arrangement;      // sorted by position
    std::vector<AutomationEnvelope> automation; // arranger time
    std::vector<Device> devices;                // the MIDI effect chain, in the order notes flow
    std::vector<Modulator> modulators;          // track level

    bool operator==(const Track& o) const {
        return id == o.id && name == o.name && color == o.color && type == o.type && polyphony == o.polyphony &&
               mute == o.mute && solo == o.solo && armed == o.armed && monitor == o.monitor &&
               recordInput == o.recordInput && midiChannel == o.midiChannel && listen == o.listen &&
               listenTrack == o.listenTrack && gateLengthScale == o.gateLengthScale &&
               transpose == o.transpose && drumLanes == o.drumLanes && slots == o.slots &&
               arrangement == o.arrangement && automation == o.automation && devices == o.devices &&
               modulators == o.modulators;
    }
};

} // namespace tessera
