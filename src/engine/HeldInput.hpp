#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tessera {

/// The keys held now on the live inputs (MIDI, REC CV, the computer keyboard), so the editor can light them.
/// The UI thread feeds it the engine's capture copies of live notes. A key held on two inputs at once stays lit
/// until both let go; a release with no press before it (one lost when a queue was full) is ignored.
class HeldInput {
  public:
    /// Counts one live note-on or note-off: `pitch` as it plays, `inPitch` as it came in.
    void note(int pitch, int inPitch, bool on) {
        count(played, pitch, on);
        count(incoming, inPitch, on);
    }
    /// Whether a held key plays `pitch` now, after the song's key snapped it when live input follows the key.
    bool plays(int pitch) const { return valid(pitch) && played[std::size_t(pitch)] > 0; }
    /// Whether a held key came in as `pitch`, before any snapping: what a drum lane answers to.
    bool cameIn(int pitch) const { return valid(pitch) && incoming[std::size_t(pitch)] > 0; }
    /// Whether any key is held.
    bool any() const {
        for (uint8_t n : played)
            if (n > 0)
                return true;
        return false;
    }
    /// Forgets every held key.
    void clear() { *this = HeldInput(); }

  private:
    /// Whether `pitch` is a MIDI note number.
    static bool valid(int pitch) { return pitch >= 0 && pitch < 128; }
    /// Adds or takes away one hold of `pitch` in `keys`.
    static void count(std::array<uint8_t, 128>& keys, int pitch, bool on) {
        if (!valid(pitch))
            return;
        uint8_t& n = keys[std::size_t(pitch)];
        if (on && n < 255)
            n++;
        else if (!on && n > 0)
            n--;
    }

    std::array<uint8_t, 128> played{}, incoming{};
};

} // namespace tessera
