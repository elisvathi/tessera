#pragma once

#include "Types.hpp"

#include <cstdint>
#include <vector>

namespace tessera {

/// What a launched clip does by itself once it has played for a while (docs/DECISIONS.md §184). "Next" and
/// "previous" follow the track's slots, which are its scenes, left to right.
enum class NextAction : uint8_t {
    None,       // keep looping: nothing happens
    Next,       // the next clip; after the last one the track stops
    Previous,   // the clip before; before the first one the track stops
    First,      // the first clip
    Last,       // the last clip
    Random,     // any clip, this one too
    Other,      // any clip but this one
    RoundRobin, // the next clip, and after the last one the first again
    Back,       // the clip that played before this one; with none the track stops
    Stop,       // the track stops
};
constexpr int kNextActionCount = 10;

/// What "after" counts: whole loops of the clip, or bars of the song.
enum class NextActionUnit : uint8_t { Loops, Bars };
/// Which clips a next action picks from: every clip on the track, or only the clip's block (the clips next to it,
/// up to an empty slot on each side).
enum class NextActionScope : uint8_t { Track, Block };

/// A clip's next action: after `count` loops or bars it does `action`, or with `chance` below 100 sometimes
/// `otherwise`. A clip that doesn't loop plays once and then does it, whatever the count.
struct NextActionSettings {
    NextAction action = NextAction::None;
    NextAction otherwise = NextAction::None;
    uint8_t chance = 100; // percent for `action`; the rest for `otherwise`
    uint16_t count = 4;
    NextActionUnit unit = NextActionUnit::Loops;
    NextActionScope scope = NextActionScope::Track;

    /// Whether something can happen at all: a clip with nothing to do keeps looping as before.
    bool any() const { return action != NextAction::None || (chance < 100 && otherwise != NextAction::None); }

    bool operator==(const NextActionSettings& o) const {
        return action == o.action && otherwise == o.otherwise && chance == o.chance && count == o.count &&
               unit == o.unit && scope == o.scope;
    }
    bool operator!=(const NextActionSettings& o) const { return !(*this == o); }
};

constexpr uint16_t kMaxNextActionCount = 999;

/// What a next action comes to on a track: keep the clip playing, stop, or launch slot `slot` (which can be the
/// playing one again: it starts over).
struct NextActionTarget {
    enum class Kind : uint8_t { Keep, Stop, Launch } kind = Kind::Keep;
    int slot = -1;
};

/// Picks where `action` goes from the clip in `current`. `filled[i]` says whether slot i holds a clip; `previous` is
/// the slot that played before (-1 for none); `scope` limits the pick to the clip's block. `random` is a number in
/// [0, 1) for the random actions. Allocation free: it runs on the audio thread.
NextActionTarget pickNextAction(NextAction action, NextActionScope scope, const std::vector<bool>& filled, int current,
                                int previous, double random);

/// Picks the action of a roll: `random` in [0, 1) below chance / 100 gives `action`, else `otherwise`.
inline NextAction rollNextAction(const NextActionSettings& s, double random) {
    return random * 100 < double(s.chance) ? s.action : s.otherwise;
}

} // namespace tessera
