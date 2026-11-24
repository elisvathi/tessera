#pragma once

#include "Variation.hpp"

#include "AudioSource.hpp"
#include "Automation.hpp"
#include "Device.hpp"
#include "NextAction.hpp"
#include "Note.hpp"
#include "ScaleRef.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tessera {

enum class LoopMode : uint8_t { Off, Loop };

/// When a launched clip starts: at once, or on the next grid line of this size (DESIGN.md §7).
enum class LaunchQuantize : uint8_t { Off, Sixteenth, Quarter, Bar, TwoBars, FourBars };

/// Ticks of one launch grid step; 0 for Off.
constexpr Tick launchQuantizeTicks(LaunchQuantize q, TimeSig ts) {
    switch (q) {
    case LaunchQuantize::Sixteenth:
        return kPpq / 4;
    case LaunchQuantize::Quarter:
        return kPpq;
    case LaunchQuantize::Bar:
        return ticksPerBar(ts);
    case LaunchQuantize::TwoBars:
        return 2 * ticksPerBar(ts);
    case LaunchQuantize::FourBars:
        return 4 * ticksPerBar(ts);
    case LaunchQuantize::Off:
    default:
        return 0;
    }
}

struct Clip {
    ClipId id;
    std::string name;
    uint32_t color = 0;    // 0xRRGGBB, from before clips showed their track's colour: kept in files, unused
    uint32_t ownColor = 0; // a colour picked for this clip (its slot and timeline copies); 0 = its track's
    Tick length = 0;    // content length
    Tick loopStart = 0;
    Tick loopEnd = 0;
    LoopMode loopMode = LoopMode::Loop;
    std::vector<Note> notes;                    // sorted by noteOrder()
    std::vector<AutomationEnvelope> automation; // clip time, loops with the clip
    std::optional<ScaleRef> scaleOverride;
    std::optional<LaunchQuantize> launchQuantize; // overrides the song's setting for this clip
    NextActionSettings next;                      // what it does by itself once launched
    std::optional<AudioContent> audio;            // set on audio clips, which have no notes
    /// The clip's own device chain (docs/DECISIONS.md §157): while `playsOwnChain` is set it plays only through these
    /// devices and modulators, and its track's chain is skipped for it. `hasOwnChain` says it has one at all (an
    /// empty one plays dry); switching back to the track's keeps it. Audio clips have none.
    bool hasOwnChain = false;
    bool playsOwnChain = false;
    std::vector<Device> devices;
    std::vector<Modulator> modulators;
    int32_t variation = kNewEachPlay; // its notes' chance: the number it plays on, or new every play

    bool isAudio() const { return audio.has_value(); }
    /// Whether its notes go through its own chain now, not its track's.
    bool usesOwnChain() const { return hasOwnChain && playsOwnChain; }

    const Note* findNote(NoteId noteId) const;
    /// The most unmuted notes that sound at the same moment while the clip plays.
    int maxSimultaneousNotes() const;
    /// Where the clip's content ends: its last note (muted ones too) or automation point; 0 when empty. A clip
    /// without a loop plays this far: its length follows its notes, not the loop region. An audio clip's content is
    /// its whole length: the window of the file it plays.
    Tick contentEnd() const;
    /// The part that plays: the loop, or [0, contentEnd()) without one.
    Tick playStart() const { return loopMode == LoopMode::Loop && loopEnd > loopStart ? loopStart : 0; }
    Tick playEnd() const { return loopMode == LoopMode::Loop && loopEnd > loopStart ? loopEnd : contentEnd(); }
    /// Restores the noteOrder() invariant after notes were added or changed.
    void sortNotes();
    /// The colour the clip is drawn in: its own when one was picked, else its track's `trackColor`.
    uint32_t shownColor(uint32_t trackColor) const { return ownColor ? ownColor : trackColor; }

    bool operator==(const Clip& o) const {
        return id == o.id && name == o.name && color == o.color && ownColor == o.ownColor && length == o.length &&
               loopStart == o.loopStart && loopEnd == o.loopEnd && loopMode == o.loopMode && notes == o.notes &&
               automation == o.automation && scaleOverride == o.scaleOverride && launchQuantize == o.launchQuantize &&
               next == o.next && audio == o.audio && hasOwnChain == o.hasOwnChain && playsOwnChain == o.playsOwnChain &&
               devices == o.devices && modulators == o.modulators && variation == o.variation;
    }
};

} // namespace tessera
