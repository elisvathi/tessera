#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

/// A small FLAC encoder for exports (docs/design/AUDIO_MIXER.md §3.5): 24-bit, fixed second-order prediction with
/// Rice-coded residuals, 4096-frame blocks, one or two channels. Lossless and about half a WAV's size. No MD5.
namespace tessera {

class FlacWriter {
  public:
    ~FlacWriter();
    /// Opens `path` and writes the header. False when the file can't be made.
    bool open(const std::string& path, int channels, int sampleRate);
    /// Adds `frames` interleaved frames, each sample -1..1 of full scale (clipped).
    bool write(const float* interleaved, size_t frames);
    /// Writes what is left and the final stream size, and closes the file.
    bool close();

  private:
    void encodeBlock();
    std::FILE* file = nullptr;
    int channels = 2, sampleRate = 48000;
    std::vector<std::vector<int32_t>> block; // per channel
    uint64_t total = 0;
    uint32_t frameNumber = 0;
    uint32_t minFrame = 0xFFFFFF, maxFrame = 0;
    bool ok = true;
};

} // namespace tessera
