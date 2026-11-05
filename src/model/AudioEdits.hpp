#pragma once

#include "ArrangementEdits.hpp"
#include "AudioAnalysis.hpp"
#include "Commands.hpp"
#include "TrackCommands.hpp"

#include <functional>
#include <string>
#include <vector>

/// Commands for audio: importing files, an audio clip's settings, fades, and the mixer (docs/design/AUDIO_MIXER.md).
namespace tessera {

/// Adds audio sources to the song; undo takes them out again. The files stay in patch storage either way: they
/// are only removed when the patch loads without a source that uses them.
class AddAudioSources : public Command {
  public:
    explicit AddAudioSources(std::vector<AudioSource> sources) : sources(std::move(sources)) {}

    void apply(Song& song) override;
    void revert(Song& song) override;
    std::string name() const override { return "Add audio"; }
    ChangeSet changes() const override { return ChangeSet::all(); }

  private:
    std::vector<AudioSource> sources;
};

/// Removes audio sources (Remove unused audio). Undo puts them back.
class RemoveAudioSources : public Command {
  public:
    explicit RemoveAudioSources(std::vector<AudioSource> sources) : sources(std::move(sources)) {}

    void apply(Song& song) override;
    void revert(Song& song) override;
    std::string name() const override { return "Remove unused audio"; }
    ChangeSet changes() const override { return ChangeSet::all(); }

  private:
    std::vector<AudioSource> sources;
};

/// Changes what an audio clip plays: gain, pitch, follow tempo, sound type, start in the file.
class SetAudioContent : public Command {
  public:
    SetAudioContent(std::string name, ClipId clip, AudioContent before, AudioContent after)
        : label(std::move(name)), clip(clip), before(before), after(after) {}

    void apply(Song& song) override { set(song, after); }
    void revert(Song& song) override { set(song, before); }
    std::string name() const override { return label; }
    ChangeSet changes() const override { return ChangeSet::clip(clip); }

  private:
    void set(Song& song, const AudioContent& value) const;

    std::string label;
    ClipId clip;
    AudioContent before, after;
};

/// Changes one or more tracks' mixer strips (a fader drag over several selected strips is one step).
class SetChannels : public Command {
  public:
    struct Change {
        TrackId track;
        Channel before, after;
    };
    SetChannels(std::string name, std::vector<Change> changes) : label(std::move(name)), list(std::move(changes)) {}

    void apply(Song& song) override { set(song, false); }
    void revert(Song& song) override { set(song, true); }
    std::string name() const override { return label; }
    ChangeSet changes() const override;

  private:
    void set(Song& song, bool undo) const;

    std::string label;
    std::vector<Change> list;
};

/// Edits an audio clip's settings through `change`, which gets a copy to modify. Null when nothing changed or the
/// clip isn't an audio clip.
CommandPtr editAudio(const Song& song, ClipId clip, const std::function<void(AudioContent&)>& change,
                     const std::string& name);

/// Reverse (or play forwards again) an audio clip, keeping its cuts: a reversed clip plays its window from the end,
/// so each timeline copy's offset is mirrored too and every slice of a split clip plays its own audio backwards,
/// where it was. Copies that wrap round a loop keep their offsets. One undo step; null when not an audio clip.
CommandPtr reverseAudio(const Song& song, ClipId clip);

/// Sets the gain of every audio clip in `clips` to its current gain plus `deltaDb` (the title-bar drag on a
/// selection), clamped. Clips at -inf stay there. One undo step; null when nothing changed.
CommandPtr nudgeClipGain(const Song& song, const std::vector<ClipId>& clips, float deltaDb);

/// The loudest sample of the part of its file `clip` plays, as a share of the file's full scale (0..1), read from
/// the source's waveform. 0 when the clip isn't audio or plays silence.
float clipPeak(const Clip& clip, const AudioSource& source, const PeakCache& peaks);
/// Normalise: sets the clip gain so its loudest sample reaches the file's full scale (`peak` from clipPeak()), for
/// every copy of the clip. Null when the clip is silent or already there.
CommandPtr normaliseClip(const Song& song, ClipId clip, float peak);

/// Which part of its file an audio clip plays: source frames [start, end) (the audio editor's window edges, §6.4).
/// The clip's length follows at its tempo. Its loop keeps its place in the file: a loop over the whole window still
/// covers the whole new window. One undo step; null when nothing changes.
CommandPtr setAudioWindow(const Song& song, ClipId clip, int64_t start, int64_t end);

/// Sets an audio clip's tempo, the one place its speed is set (§5.1). A Stretch clip keeps the same audio, so its
/// length in ticks, its loop and every timeline copy scale with the tempo. One undo step; null when unchanged.
CommandPtr setClipTempo(const Song& song, ClipId clip, double bpm);

/// Sets the fades of one timeline instance of an audio clip. Lengths are clamped to the instance.
CommandPtr setFades(const Song& song, ArrangementId instance, Fade in, Fade out);

/// Mixer strip edits. Null when nothing changed.
CommandPtr setChannel(const Song& song, TrackId track, Channel channel, const std::string& name);
/// Moves the volume of every track in `tracks` by `deltaDb` from `start` (their values when the drag began).
/// Strips at -inf stay at -inf.
CommandPtr nudgeVolumes(const Song& song, const std::vector<SetChannels::Change>& start, float deltaDb);
CommandPtr setMasterVolume(const Song& song, float db);
/// A group strip's level and pan, or its mute (§3.5). Null when nothing changed or the group isn't there.
CommandPtr setGroupChannel(const Song& song, GroupId group, Channel channel, const std::string& name);
CommandPtr setGroupMute(const Song& song, GroupId group, bool mute);
/// The Level and Use of an audio track's clips, or of the master's mix when `track` is the master (§7.8). Null when
/// nothing changed or the track isn't there.
CommandPtr setSongSource(const Song& song, TrackId track, SourceLevel level, const std::string& name);
/// A drum track's lane IN level and pan (`lane` 0..7, the Drum Out output). Null when nothing changed.
CommandPtr setDrumInput(const Song& song, TrackId track, int lane, Channel channel, const std::string& name);

/// The warp markers of audio clip `clip` (§3.5), cleaned up as normalize does (rising ticks and frames). One undo
/// step; null when nothing changed.
CommandPtr setWarps(const Song& song, ClipId clip, std::vector<WarpMarker> warps, const std::string& name);
/// Pins every hit (transients, in the file's frames) of `clip` to the nearest `grid` ticks: a marker per hit, so
/// the audio plays in time with the grid. Hits closer than half a grid step to the one before are left alone.
CommandPtr quantizeAudio(const Song& song, ClipId clip, const std::vector<int64_t>& transients, double fileRate,
                         Tick grid);

/// Takes (§3.5): which take a timeline copy of an audio clip plays (-1 the clip's own). One undo step; null when
/// unchanged.
CommandPtr setInstanceTake(const Song& song, TrackId track, ArrangementId instance, int take);
/// Every timeline copy of `clip` plays take `take`. One undo step; null when none changes.
CommandPtr useTakeEverywhere(const Song& song, ClipId clip, int take);

/// Where a timeline audio clip's hits fall, in song ticks, from its file's transients (frames at `fileRate`): each
/// pass of a looping clip has its own. Reversed clips have none for now.
std::vector<Tick> hitTicks(const Clip& clip, const ArrangerClip& at, const std::vector<int64_t>& transients,
                           double fileRate, double songTempo);

/// The sources no clip uses, and a command that removes them. Null when there are none.
std::vector<AudioSourceId> unusedSources(const Song& song);
CommandPtr removeUnusedAudio(const Song& song);

// ---- Import -------------------------------------------------------------------------------------------------

/// One file ready to go into the song: its source (decoded, copied into patch storage) and how its clip plays.
struct ImportedAudio {
    AudioSource source; // gets its ID from importAudio
    double tempo = 120; // the clip tempo
    TempoFollow follow = TempoFollow::Stretch;
    SoundType sound = SoundType::FullMix;
    bool loop = true; // a loop repeats; a one-shot plays once
};

/// Where dropped files go (§6.5).
struct AudioDropTarget {
    enum class Kind {
        NewTracks, // one new audio track per file, at `index`; the clip goes on the timeline at `at` or in slot 0
        Track,     // on an audio track's timeline at `at`, end to end
        Slot,      // into an audio track's launcher slots from `slot` down
    };
    Kind kind = Kind::NewTracks;
    int index = 0;
    TrackId track;
    Tick at = 0;
    int slot = 0;
    bool timeline = true; // NewTracks: on the timeline (Plays = Timeline) rather than in the first slot
};

/// What an import made, to select and scroll to.
struct ImportResult {
    std::vector<TrackId> tracks; // new tracks
    std::vector<ClipId> clips;
    std::vector<ArrangementId> instances;
    int skipped = 0; // files that didn't fit (track or slot limit)
};

/// The audio clip for `file`: its whole length, looping or not, named after the file.
Clip makeAudioClip(const ImportedAudio& file, ClipId id);

/// Imports files as one undo step: their sources, the tracks they need and their clips, placed as `target` says.
/// Null when nothing could be placed.
CommandPtr importAudio(const Song& song, std::vector<ImportedAudio> files, const AudioDropTarget& target,
                       const NextId& nextId, ImportResult* result = nullptr);

/// A track name for an audio file: its name without the extension.
std::string audioTrackName(const std::string& fileName);

/// Replace file (Ctrl/Cmd+drop on a clip, or its menu): the clip plays `file` from its start at the file's tempo,
/// keeping its length, gain, follow tempo, sound type and every timeline copy with its place and fades (§6.5).
/// The old source stays until Remove unused audio. One undo step; null when the clip isn't audio.
CommandPtr replaceAudioFile(const Song& song, ClipId clip, ImportedAudio file, const NextId& nextId);

} // namespace tessera
