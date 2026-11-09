#pragma once

#include "Arrangement.hpp"
#include "Scale.hpp"
#include "Song.hpp"

#include <optional>
#include <vector>

/// A track's device chain run over a list of notes outside the engine, for MIDI export (docs/DEVICES.md §6): the
/// same device and modulator code, the same seed and knob values, with modulators stepping along the song.
namespace tessera::chainrender {

/// A note in song ticks. `source` rides along untouched (export uses it for expression curves).
struct Note {
    double start = 0, end = 0;
    int pitch = 60;
    float velocity = 0.8f, release = 0.5f;
    const tessera::Note* source = nullptr;
    int64_t pass = 0;  // which time through its clip, for repeatable randomness
    double origin = 0; // where it was written
    uint8_t key = 0;   // 0 = the song's key, k = `keys[k - 1]`
    uint8_t channel = 1;
    uint8_t lane = 0; // the playhead that read it (1..16), for its branch; 0 = none
};

/// Calls `set(lane, value, modulate)` for each knob lane playing at song tick `t`, in the order they apply: the track's
/// lanes, then those of the clip under the song. `modulate`: the value moves the track lane's around 0.5.
template <typename Set>
void forEachKnobLane(const Song& song, const Track& track, double t, Set set) {
    for (const AutomationEnvelope& lane : track.automation)
        if (lane.paramTarget.valid() && !lane.mute)
            set(lane, evaluate(lane, t), false);
    const ArrangerClip* inst = instanceAt(track, Tick(t));
    const Clip* clip = inst && !inst->muted ? song.findClip(inst->clip) : nullptr;
    if (!clip)
        return;
    std::optional<double> at = clipTimeAt(*clip, *inst, t);
    if (!at)
        return;
    for (const AutomationEnvelope& lane : clip->automation)
        if (lane.paramTarget.valid() && !lane.mute)
            set(lane, evaluate(lane, *at), lane.combine == CombineMode::Modulate);
}

/// What `track`'s chain makes of `notes` (sorted by start). Devices that keep playing (echo, arpeggiator) stop at
/// `until`. `tempo` (BPM) times envelopes and hertz LFOs; `keys` are the clips' own keys. Without an active device
/// the notes come back as they are.
std::vector<Note> run(const Song& song, const Track& track, const std::vector<Note>& notes, double until, double tempo,
                      const std::vector<Scale>& keys = {});

} // namespace tessera::chainrender
