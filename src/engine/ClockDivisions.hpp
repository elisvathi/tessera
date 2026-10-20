#pragma once

#include "../model/Types.hpp"

namespace tessera {

struct ClockDivision {
    const char* label;
    Tick ticks;
};

/// Choices for the CLOCK output. Saved by index, so only append.
constexpr ClockDivision kClockDivisions[] = {
    {"1/4", kPpq},          {"1/8", kPpq / 2},      {"1/16", kPpq / 4},     {"1/32", kPpq / 8},
    {"24 PPQN", kPpq / 24}, {"48 PPQN", kPpq / 48}, {"96 PPQN", kPpq / 96},
};
constexpr int kClockDivisionCount = sizeof(kClockDivisions) / sizeof(kClockDivisions[0]);
constexpr int kDefaultClockDivision = 2;

} // namespace tessera
