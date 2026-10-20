#pragma once

#include "Types.hpp"

#include <cmath>

namespace tessera {

/// The song's swing (DESIGN.md §7) as a time warp: within each pair of 1/16s the first stretches and the second
/// shrinks, so the off-beat 1/16 lands up to half a 1/16 later (at swing 1) and notes in between move smoothly,
/// never changing order. The model stays unswung; playback and "as played" export apply this.
inline double swung(double tick, float swing) {
    if (swing <= 0)
        return tick;
    const double s = double(kPpq) / 4;
    const double d = s * 0.5 * std::fmin(double(swing), 1.0);
    double pair = std::floor(tick / (2 * s)) * 2 * s;
    double pos = tick - pair;
    double warped = pos < s ? pos * (s + d) / s : (s + d) + (pos - s) * (s - d) / s;
    return pair + warped;
}

} // namespace tessera
