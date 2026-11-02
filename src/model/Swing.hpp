#pragma once

#include "Types.hpp"

#include <cmath>

namespace tessera {

/// The song's swing (DESIGN.md §7) as a time warp: within each pair of grid steps (1/16s, or 1/8s for `step` = an
/// eighth) the first stretches and the second shrinks, so the off-beat lands up to half a step later (at swing 1)
/// and notes in between move smoothly, never changing order. Swing 0..1 is shown as 50..75%: 50% straight, about 66%
/// the triplet feel, 75% the hardest. The model stays unswung; playback and "as played" export apply this.
inline double swung(double tick, float swing, Tick step = kPpq / 4) {
    if (swing <= 0 || step <= 0)
        return tick;
    const double s = double(step);
    const double d = s * 0.5 * std::fmin(double(swing), 1.0);
    double pair = std::floor(tick / (2 * s)) * 2 * s;
    double pos = tick - pair;
    double warped = pos < s ? pos * (s + d) / s : (s + d) + (pos - s) * (s - d) / s;
    return pair + warped;
}

} // namespace tessera
