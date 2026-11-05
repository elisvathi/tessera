#pragma once

#include <cstdint>

namespace tessera {

/// What kind of sound an audio clip holds. It picks the time stretcher's settings, so the user never sees them.
enum class SoundType : uint8_t {
    Drums,   // keeps attacks sharp
    Melody,  // smooth, for bass, keys, pads and single instruments
    Voice,   // keeps the character of a voice when pitching
    FullMix, // a balance, for finished music
};

} // namespace tessera
