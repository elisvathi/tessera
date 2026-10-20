#pragma once

#include "Scale.hpp"
#include "Song.hpp"

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
};

/// What `track`'s chain makes of `notes` (sorted by start). Devices that keep playing (echo, arpeggiator) stop at
/// `until`. `tempo` (BPM) times envelopes and hertz LFOs; `keys` are the clips' own keys. Without an active device
/// the notes come back as they are.
std::vector<Note> run(const Song& song, const Track& track, const std::vector<Note>& notes, double until, double tempo,
                      const std::vector<Scale>& keys = {});

} // namespace tessera::chainrender
