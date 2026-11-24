#pragma once

#include <cstdint>

namespace tessera {

/// What a dice switch holds (docs/DECISIONS.md §163): a number from 1 to kMaxVariation that plays the same every time,
/// or a negative number for New, which rolls again on every play and every loop pass and remembers the number to go
/// back to (-17: New, back to 17). kNewEachPlay is New with nothing in particular to go back to.
constexpr int32_t kNewEachPlay = -1;
constexpr int32_t kMaxVariation = 999;

/// The number a "New" device, modulator or clip plays on in one run. `roll` changes on every play and loop pass. It
/// is a number a switch can hold (1 to kMaxVariation), so Keep it stores exactly what was heard.
inline uint32_t rolledVariation(uint32_t roll, uint64_t id) {
    uint64_t z = (uint64_t(roll) * 0x9E3779B97F4A7C15ull) ^ (id + 0xBF58476D1CE4E5B9ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return uint32_t(z % uint64_t(kMaxVariation)) + 1;
}

/// The number a switch shows, and goes back to from New.
inline int32_t variationNumber(int32_t variation) { return variation < 0 ? -variation : variation; }
/// A switch kept in range: 0 reads as New.
inline int32_t clampVariation(int32_t v) {
    return v == 0 ? kNewEachPlay : v > kMaxVariation ? kMaxVariation : v < -kMaxVariation ? -kMaxVariation : v;
}

/// The seed randomness uses: the switch's fixed number, or this run's roll.
inline uint32_t variationSeed(int32_t variation, uint32_t roll, uint64_t id) {
    return variation >= 1 ? uint32_t(variation) : rolledVariation(roll, id);
}

} // namespace tessera
