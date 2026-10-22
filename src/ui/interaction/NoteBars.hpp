#pragma once

#include <array>
#include <vector>

namespace tessera {

/// One bar of a clip's miniature: notes on a pitch, from x0 to x1 in pixels.
struct NoteBar {
    int pitch;
    float x0, x1;
};

/// Turns notes (pitch, x0, x1; sorted by x0) into the bars a miniature draws: notes on one pitch less than a pixel
/// apart become one bar, so zoomed out there are a few bars instead of thousands of rectangles too small to tell
/// apart. Each bar is at least `minWidth` wide. `out` is cleared first.
inline void mergeNoteBars(const std::vector<NoteBar>& notes, float minWidth, std::vector<NoteBar>& out) {
    out.clear();
    std::array<int, 128> open; // index into `out` of the bar still growing on each pitch, -1 for none
    open.fill(-1);
    for (const NoteBar& n : notes) {
        int p = n.pitch & 127;
        float x1 = n.x1 > n.x0 + minWidth ? n.x1 : n.x0 + minWidth;
        int& bar = open[size_t(p)];
        if (bar >= 0 && n.x0 - out[size_t(bar)].x1 < 1.f) {
            if (x1 > out[size_t(bar)].x1)
                out[size_t(bar)].x1 = x1;
            continue;
        }
        bar = int(out.size());
        out.push_back({p, n.x0, x1});
    }
}

} // namespace tessera
