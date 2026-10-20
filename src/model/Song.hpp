#pragma once

#include "Clip.hpp"
#include "ScaleRef.hpp"
#include "Track.hpp"

#include <map>
#include <optional>
#include <vector>

namespace tessera {

using ClipPool = std::map<ClipId, Clip>;

constexpr int kDefaultScenes = 8;
constexpr int kMaxScenes = 64;

struct Scene {
    std::string name;
    uint32_t color = 0;
    std::optional<double> tempo; // BPM to switch to when the scene launches

    bool operator==(const Scene& o) const { return name == o.name && color == o.color && tempo == o.tempo; }
};

/// A folder of tracks, for organizing many tracks. Display only: it doesn't change routing.
/// Whether a group is folded is a view setting (ViewState), not song data, so folding never lands in undo.
struct TrackGroup {
    GroupId id;
    std::string name;
    uint32_t color = 0;
    std::vector<TrackId> tracks;

    bool operator==(const TrackGroup& o) const {
        return id == o.id && name == o.name && color == o.color && tracks == o.tracks;
    }
};

struct Marker {
    MarkerId id;
    Tick position = 0;
    std::string name;
    uint32_t color = 0;

    bool operator==(const Marker& o) const {
        return id == o.id && position == o.position && name == o.name && color == o.color;
    }
};

/// The editable musical content of one Tessera. Owned by the UI thread.
/// Live transport state (tempo, loop on/off, play mode) lives in the module's params instead,
/// so it can be mapped and automated from the rack. See docs/DECISIONS.md.
///
/// Invariants (normalize() restores them): 1..kMaxTracks tracks, every track has one slot per scene,
/// notes are sorted, and nextId is above every ID in use.
struct Song {
    static constexpr int kSchemaVersion = 5;

    TimeSig timeSig;
    float swing = 0; // 0..1
    ScaleRef scale;
    std::vector<CustomScale> customScales;
    Tick loopStart = 0;
    Tick loopEnd = 4 * ticksPerBar(TimeSig{});
    uint32_t seed = 1; // randomness for note operators; "Reseed" changes it
    LaunchQuantize launchQuantize = LaunchQuantize::Bar;
    std::vector<Track> tracks; // display order
    std::vector<TrackGroup> groups;
    std::vector<Scene> scenes;
    ClipPool clips;
    std::vector<Marker> markers;       // sorted by position
    std::vector<Modulator> modulators; // song level: reach every track (docs/DEVICES.md)
    uint64_t nextId = 1;               // one counter for every kind of ID, so IDs never repeat

    template <typename IdT>
    IdT newId() {
        return IdT(nextId++);
    }

    Track* findTrack(TrackId id);
    const Track* findTrack(TrackId id) const;
    const TrackGroup* findGroup(GroupId id) const;
    int trackIndex(TrackId id) const; // -1 when not found
    Clip* findClip(ClipId id);
    const Clip* findClip(ClipId id) const;

    /// The clip a track plays. Until the launcher exists this is the clip in the track's first filled slot.
    const Clip* activeClip(const Track& track) const;

    /// How many slots and arranger instances use a clip.
    int clipUses(ClipId clip) const;

    /// The poly channels a track sends: its fixed voice count, or for kAutoVoices the most notes its clips
    /// ever play at once (at least 1).
    int voicesOf(const Track& track) const;

    /// Whether any clip in the track's slots or on its timeline has a note: the track has something to play.
    bool hasNotes(const Track& track) const;

    bool anySolo() const;
    /// A track is heard when it isn't muted and either nothing is soloed or it is.
    bool isAudible(const Track& track) const;

    bool operator==(const Song& o) const {
        return timeSig == o.timeSig && swing == o.swing && scale == o.scale && customScales == o.customScales &&
               loopStart == o.loopStart && loopEnd == o.loopEnd && seed == o.seed &&
               launchQuantize == o.launchQuantize && tracks == o.tracks && groups == o.groups && scenes == o.scenes &&
               clips == o.clips && markers == o.markers && modulators == o.modulators && nextId == o.nextId;
    }
    bool operator!=(const Song& o) const { return !(*this == o); }
};

/// Repairs a song loaded from outside (old or hand-edited patches) so every invariant holds.
/// Values are clamped to their ranges; nothing valid is ever dropped.
void normalize(Song& song);

/// A new track with defaults, one empty slot per scene and a color picked from its position.
Track makeTrack(Song& song, TrackType type, const std::string& name);

/// The song a fresh Tessera starts with: 3 instrument tracks and 1 drum track, no clips.
Song makeDefaultSong();
/// Puts a two-bar demo clip in track 1's first slot. Tests use it as a ready-made clip.
void addDemoClip(Song& song);

} // namespace tessera
