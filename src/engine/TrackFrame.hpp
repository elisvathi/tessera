#pragma once

#include "../model/Types.hpp"

namespace tessera {

/// Everything one track outputs during one sample. The engine fills these; output modules copy them to ports.
struct TrackFrame {
    // Which track wrote this frame (TrackId value, 0 = slot unused). Readers compare it with their binding,
    // so a slot that is freed and handed to another track can never leak into the wrong output module.
    uint64_t trackId = 0;
    uint8_t channels = 0;     // voices in use = the track's polyphony
    uint8_t autoChannels = 0; // automation lanes in use
    float voct[kMaxVoices] = {};
    float gate[kMaxVoices] = {};
    float vel[kMaxVoices] = {};
    float rel[kMaxVoices] = {};
    float timbre[kMaxVoices] = {};
    float press[kMaxVoices] = {};
    float automation[kMaxAutomationLanes] = {}; // already in volts
};

} // namespace tessera
