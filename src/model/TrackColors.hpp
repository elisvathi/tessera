#pragma once

#include <cstdint>

namespace tessera {

/// 16 track colors with even perceived lightness: an OKLCH hue ring at L=0.72, C=0.14,
/// starting at 30° in 22.5° steps. Stored in the song as 0xRRGGBB.
constexpr uint32_t kTrackColors[16] = {
    0xEF806F, 0xE88949, 0xD79628, 0xBDA422, 0x9AB140, 0x6DBA67, 0x2DC08E, 0x00C0B4,
    0x00BBD5, 0x24B2ED, 0x65A7FA, 0x909BFB, 0xB28FEF, 0xCE85D8, 0xE17EB9, 0xED7C95,
};

/// Spreads consecutive tracks around the ring so neighbours never look alike.
constexpr uint32_t trackColorForIndex(int index) { return kTrackColors[(index * 5) % 16]; }

/// The colour of a note on `channel` (1..16) in a track coloured `base`. Channel 1 is the track's own colour, so
/// songs that don't use channels look as before. The others step around the ring from it, halving the gap each
/// time (opposite, then the quarters, then the eighths...), so the first channels used are far apart.
inline uint32_t channelColor(uint32_t base, int channel) {
    if (channel <= 1)
        return base;
    // The ring position nearest the base colour (a custom colour falls back to its closest ring colour).
    auto distance = [](uint32_t a, uint32_t b) {
        int dr = int(a >> 16 & 0xFF) - int(b >> 16 & 0xFF), dg = int(a >> 8 & 0xFF) - int(b >> 8 & 0xFF),
            db = int(a & 0xFF) - int(b & 0xFF);
        return dr * dr + dg * dg + db * db;
    };
    int home = 0;
    for (int i = 1; i < 16; i++)
        if (distance(kTrackColors[i], base) < distance(kTrackColors[home], base))
            home = i;
    static constexpr int kSteps[15] = {8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
    return kTrackColors[(home + kSteps[(channel - 2) % 15]) % 16];
}

} // namespace tessera
