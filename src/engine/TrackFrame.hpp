#pragma once

#include "../model/Types.hpp"

namespace tessera {

/// Everything one track outputs during one sample. The engine fills these; output modules copy them to ports.
struct TrackFrame {
    // Which track wrote this frame (TrackId value, 0 = slot unused). Readers compare it with their binding,
    // so a slot that is freed and handed to another track can never leak into the wrong output module.
    uint64_t trackId = 0;
    uint8_t channels = 0;     // voices in use = the track's polyphony (with playheads, every channel's, in blocks)
    uint8_t autoChannels = 0; // automation lanes in use
    float voct[kMaxTrackVoices] = {};
    float gate[kMaxTrackVoices] = {};
    float vel[kMaxTrackVoices] = {};
    float rel[kMaxTrackVoices] = {};
    float timbre[kMaxTrackVoices] = {};
    float press[kMaxTrackVoices] = {};
    float tune[kMaxTrackVoices] = {}; // drum tracks: the hit's tuning away from normal, 1 V per octave (Drum Out PITCH)
    float automation[kMaxAutomationLanes] = {}; // already in volts
    // The channel (1..16) of the note each voice plays or last played, so an output can play one channel only.
    uint8_t part[kMaxTrackVoices] = {};
    // With playheads: the voices come in blocks, one per playhead channel (`part` says whose), and a channel sends
    // only the voices it has needed so far (`used`); without, every voice is sent as it is.
    bool byChannel = false;
    uint8_t used[kMaxTrackVoices] = {};
};

} // namespace tessera
