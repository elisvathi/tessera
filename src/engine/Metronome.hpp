#pragma once

#include <cmath>

/// The metronome you hear on MAIN (docs/design/AUDIO_MIXER.md §8.5): a short, soft click on each beat, higher on
/// the bar. Plain arithmetic, no allocation: it runs on the audio thread.
namespace tessera {

class Metronome {
  public:
    /// Starts a click now; `accent` for the first beat of a bar.
    void trigger(bool accent) {
        phase = 0;
        envelope = 1;
        frequency = accent ? 1760.f : 1320.f;
    }
    /// The next sample of the click in volts at full level (10 V peak), or 0 once it has died away.
    float next(float sampleRate) {
        if (envelope < 1e-4f)
            return 0.f;
        // About 25 ms to fall 60 dB: short enough not to blur the beat.
        envelope *= std::exp(-6.9f / (0.025f * sampleRate));
        phase += frequency / sampleRate;
        if (phase >= 1)
            phase -= 1;
        return std::sin(phase * 6.2831853f) * envelope * 10.f;
    }

  private:
    float phase = 0, envelope = 0, frequency = 1320;
};

} // namespace tessera
