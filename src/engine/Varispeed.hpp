#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace tessera {

/// Reads a sample stream at any fractional position with a 16-tap windowed-sinc interpolator, for playing audio
/// faster or slower like tape (docs/design/AUDIO_MIXER.md §5.2). The table is filled once; reading never
/// allocates, so it is safe on the audio thread.
class Varispeed {
  public:
    static constexpr int kTaps = 16;
    static constexpr int kPhases = 512;

    /// Fills the polyphase table: a sinc (cut a little below Nyquist so it stays clean) under a Kaiser window, each
    /// phase normalised to sum to 1.
    Varispeed() {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kBeta = 8.0;
        constexpr double kCutoff = 0.92;
        auto besselI0 = [](double x) {
            double sum = 1, term = 1;
            for (int k = 1; k < 30; k++) {
                term *= (x / (2 * k)) * (x / (2 * k));
                sum += term;
            }
            return sum;
        };
        double norm = besselI0(kBeta);
        for (int p = 0; p <= kPhases; p++) {
            double frac = double(p) / kPhases;
            double sum = 0;
            for (int t = 0; t < kTaps; t++) {
                double x = double(t - kTaps / 2 + 1) - frac; // distance from the read position
                double s = x == 0 ? kCutoff : std::sin(kPi * kCutoff * x) / (kPi * x);
                double w = x / (kTaps / 2);
                double win = std::abs(w) >= 1 ? 0 : besselI0(kBeta * std::sqrt(1 - w * w)) / norm;
                table[size_t(p)][size_t(t)] = float(s * win);
                sum += s * win;
            }
            for (int t = 0; t < kTaps; t++)
                table[size_t(p)][size_t(t)] = float(table[size_t(p)][size_t(t)] / sum);
        }
    }

    /// The value of `x` (holding `n` samples; silence outside them) at position `pos`.
    float read(const float* x, int64_t n, double pos) const {
        double base = std::floor(pos);
        double frac = pos - base;
        int64_t i0 = int64_t(base) - (kTaps / 2 - 1);
        double phase = frac * kPhases;
        int p = int(phase);
        float mix = float(phase - p);
        const std::array<float, kTaps>& a = table[size_t(p)];
        const std::array<float, kTaps>& b = table[size_t(p + 1)];
        float out = 0;
        if (i0 >= 0 && i0 + kTaps <= n) {
            for (int t = 0; t < kTaps; t++)
                out += x[i0 + t] * (a[size_t(t)] + mix * (b[size_t(t)] - a[size_t(t)]));
        } else {
            for (int t = 0; t < kTaps; t++) {
                int64_t i = i0 + t;
                if (i >= 0 && i < n)
                    out += x[i] * (a[size_t(t)] + mix * (b[size_t(t)] - a[size_t(t)]));
            }
        }
        return out;
    }

  private:
    std::array<std::array<float, kTaps>, kPhases + 1> table{};
};

} // namespace tessera
