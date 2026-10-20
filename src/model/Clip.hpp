#pragma once

#include "Automation.hpp"
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
    uint32_t color = 0; // 0xRRGGBB
    Tick length = 0;    // content length
    Tick loopStart = 0;
    Tick loopEnd = 0;
    LoopMode loopMode = LoopMode::Loop;
    std::vector<Note> notes;                    // sorted by noteOrder()
    std::vector<AutomationEnvelope> automation; // clip time, loops with the clip
    std::optional<ScaleRef> scaleOverride;
    std::optional<LaunchQuantize> launchQuantize; // overrides the song's setting for this clip

    const Note* findNote(NoteId noteId) const;
    /// The most unmuted notes that sound at the same moment while the clip plays.
    int maxSimultaneousNotes() const;
    /// Where the clip's content ends: its last note (muted ones too) or automation point; 0 when empty. A clip
    /// without a loop plays this far: its length follows its notes, not the loop region.
    Tick contentEnd() const;
    /// The part that plays: the loop, or [0, contentEnd()) without one.
    Tick playStart() const { return loopMode == LoopMode::Loop && loopEnd > loopStart ? loopStart : 0; }
    Tick playEnd() const { return loopMode == LoopMode::Loop && loopEnd > loopStart ? loopEnd : contentEnd(); }
    /// Restores the noteOrder() invariant after notes were added or changed.
    void sortNotes();

    bool operator==(const Clip& o) const {
        return id == o.id && name == o.name && color == o.color && length == o.length && loopStart == o.loopStart &&
               loopEnd == o.loopEnd && loopMode == o.loopMode && notes == o.notes && automation == o.automation &&
               scaleOverride == o.scaleOverride && launchQuantize == o.launchQuantize;
    }
};

} // namespace tessera
