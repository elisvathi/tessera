#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

/// A lock-free ring of stereo frames for one writer (the audio thread) and one reader (a file writer thread). It
/// is sized on the UI thread before either side uses it; push() never allocates. When the reader falls behind and
/// the ring fills, push() drops the frame and remembers it, so the take can stop and keep what it has (§8.6).
namespace tessera {

class FrameRing {
  public:
    explicit FrameRing(size_t frames) : data(roundUp(frames) * 2), mask(roundUp(frames) - 1) {}

    /// Audio thread.
    bool push(float l, float r) {
        size_t w = writeIndex.load(std::memory_order_relaxed);
        if (w - readIndex.load(std::memory_order_acquire) > mask) {
            overflowed.store(true, std::memory_order_relaxed);
            return false;
        }
        data[(w & mask) * 2] = l;
        data[(w & mask) * 2 + 1] = r;
        writeIndex.store(w + 1, std::memory_order_release);
        return true;
    }
    /// Reader thread: copies up to `max` frames (interleaved L, R) into `out`; returns how many.
    size_t pop(float* out, size_t max) {
        size_t r = readIndex.load(std::memory_order_relaxed);
        size_t w = writeIndex.load(std::memory_order_acquire);
        size_t n = std::min(max, w - r);
        for (size_t i = 0; i < n; i++) {
            out[i * 2] = data[((r + i) & mask) * 2];
            out[i * 2 + 1] = data[((r + i) & mask) * 2 + 1];
        }
        readIndex.store(r + n, std::memory_order_release);
        return n;
    }
    size_t available() const {
        return writeIndex.load(std::memory_order_acquire) - readIndex.load(std::memory_order_relaxed);
    }
    bool overflow() const { return overflowed.load(std::memory_order_relaxed); }

  private:
    static size_t roundUp(size_t n) {
        size_t p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }
    std::vector<float> data;
    size_t mask;
    std::atomic<size_t> writeIndex{0}, readIndex{0};
    std::atomic<bool> overflowed{false};
};

} // namespace tessera
