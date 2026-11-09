#pragma once

#include "AudioSource.hpp"
#include "Automation.hpp"
#include "Device.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace tessera {

enum class TrackType : uint8_t { Instrument, Drum, Audio };

/// Where an armed track records from (DESIGN.md §14). Note tracks use Any, Cv or Midi. An audio track records its
/// Track Out IN (Any), Roll's own IN jacks (RollIn) or another track's sound (OtherTrack, see Track::recordFrom).
enum class RecordInput : uint8_t { Any, Cv, Midi, RollIn, OtherTrack };

/// What a track plays besides its clips, all the time (no arming needed): nothing, the external inputs (recordInput
/// and midiChannel say which), or another track's notes after or before that track's devices. The notes go through
/// this track's own devices.
enum class ListenSource : uint8_t { None, External, TrackAfter, TrackBefore };

constexpr int kMaxDrumLanes = 16;

struct ClipSlot {
    std::optional<ClipId> clip;
    // An empty slot's stop button: its scene's launch stops the track. Without it the scene leaves the track
    // playing (docs/DECISIONS.md §90).
    bool stop = true;

    bool operator==(const ClipSlot& o) const { return clip == o.clip && stop == o.stop; }
};

constexpr int kMaxStackLayer = 63;

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
    int8_t layer = 0;        // stacking order among overlapping clips: a lower layer takes a higher sub-row first
    // Audio clips only. Where two instances overlap, the overlap is their crossfade and these curves shape it.
    Fade fadeIn, fadeOut;
    int8_t take = -1; // audio: the take this copy plays, -1 the clip's own, k the clip's takes[k] (comping)

    bool operator==(const ArrangerClip& o) const {
        return id == o.id && clip == o.clip && position == o.position && length == o.length && offset == o.offset &&
               muted == o.muted && transpose == o.transpose && velocityScale == o.velocityScale &&
               stretch == o.stretch && layer == o.layer && fadeIn == o.fadeIn && fadeOut == o.fadeOut && take == o.take;
    }
};

struct DrumLane {
    std::string name;
    int8_t pitch = 36;   // the note number this lane uses inside clips
    int outputIndex = 0; // trigger output channel
    bool mute = false;
    bool solo = false;
    float gateLengthMs = 0;     // 0 = trigger, > 0 = gate
    bool gateFollowsNote = false; // gate high for the note's own length (clip notes)
    int chokeGroup = 0;         // 0 = none
    Tick loopLength = 0;        // polymeter: 0 = follow the clip loop
    Tick stepLength = kPpq / 4; // how long a step of this lane is
    uint32_t color = 0;         // 0 = the track's colour

    bool operator==(const DrumLane& o) const {
        return name == o.name && pitch == o.pitch && outputIndex == o.outputIndex && mute == o.mute && solo == o.solo &&
               gateLengthMs == o.gateLengthMs && gateFollowsNote == o.gateFollowsNote && chokeGroup == o.chokeGroup &&
               loopLength == o.loopLength && stepLength == o.stepLength && color == o.color;
    }
};

/// Track::polyphony value meaning "as many voices as the track's clips ever play at once".
constexpr int kAutoVoices = 0;

/// Drum Out lane outputs with their own jacks, and so their own audio IN.
constexpr int kDrumInputs = 8;

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
    TrackId recordFrom;  // the track (or the master) an audio track records, for RecordInput::OtherTrack
    float gateLengthScale = 1; // 0..1, legato vs staccato at the output
    int8_t transpose = 0;
    std::vector<DrumLane> drumLanes;            // drum tracks only, up to kMaxDrumLanes
    std::vector<ClipSlot> slots;                // launcher slots, one per scene
    std::vector<ArrangerClip> arrangement;      // sorted by position
    std::vector<AutomationEnvelope> automation; // arranger time
    std::vector<Device> devices;                // the MIDI effect chain, in the order notes flow
    std::vector<Modulator> modulators;          // track level
    Channel channel;                            // its mixer strip
    // Drum tracks: the level and pan of what comes in on each Drum Out lane IN (outputs 1-8), before the track's own
    // fader (docs/design/AUDIO_MIXER.md §8.1, DECISIONS §112).
    std::array<Channel, kDrumInputs> drumInputs{};
    // Audio tracks: the clips as a source of the track, beside what comes back on its Track Out's IN (§7.8).
    SourceLevel clips;
    std::vector<Send> sends; // only the sends the user added, in the order added (§7.6)
    // Extra delay this track's sound is known to have (a slow effect, a long cable path), in samples: recordings
    // and bounces of it are moved earlier by this much (§8.6).
    int latencySamples = 0;

    bool operator==(const Track& o) const {
        return id == o.id && name == o.name && color == o.color && type == o.type && polyphony == o.polyphony &&
               mute == o.mute && solo == o.solo && armed == o.armed && monitor == o.monitor &&
               recordInput == o.recordInput && midiChannel == o.midiChannel && listen == o.listen &&
               listenTrack == o.listenTrack && recordFrom == o.recordFrom && gateLengthScale == o.gateLengthScale &&
               transpose == o.transpose && drumLanes == o.drumLanes && slots == o.slots &&
               arrangement == o.arrangement && automation == o.automation && devices == o.devices &&
               modulators == o.modulators && channel == o.channel && drumInputs == o.drumInputs && clips == o.clips &&
               sends == o.sends && latencySamples == o.latencySamples;
    }
};

} // namespace tessera
