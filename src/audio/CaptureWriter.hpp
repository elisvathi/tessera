#pragma once

#include "../engine/FrameRing.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// Writes captured audio to WAV files on its own thread (docs/design/AUDIO_MIXER.md §8.6): never the UI thread,
/// which Rack's file dialogs block, and never the audio thread, which only fills the rings. One writer per Tessera.
namespace tessera {

class CaptureWriter {
  public:
    /// The file format: 32-bit float WAV (recordings, bounces), or for exports 24-bit WAV or FLAC with dither.
    enum class Format { Float32, Pcm24, Flac24 };

    CaptureWriter();
    ~CaptureWriter();
    CaptureWriter(const CaptureWriter&) = delete;
    CaptureWriter& operator=(const CaptureWriter&) = delete;

    /// Starts writing what comes into `ring` to `path` at `sampleRate`. The file is made when the first frames
    /// arrive, so an armed track that never records leaves nothing behind. Returns an ID for finish() and peaks().
    int start(std::shared_ptr<FrameRing> ring, const std::string& path, double sampleRate, Format format);
    /// Writes what is left in the ring and closes the file. True once that is done (call again until it is), with
    /// `frames` set to how many frames the file holds.
    bool finish(int id, int64_t* frames = nullptr);
    /// Drops a capture and deletes its file (a cancelled bounce).
    void cancel(int id);
    /// Frames written so far, and whether the file failed (a full disk, a writer that fell behind).
    int64_t written(int id) const;
    bool failed(int id) const;
    /// A coarse waveform of what was written so far: the peak of each 512 frames, for the live take.
    std::vector<float> peaks(int id) const;

  private:
    struct Job;
    void run();
    bool drain(Job& job);

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::vector<std::unique_ptr<Job>> jobs;
    int nextId = 1;
    bool stopping = false;
    std::thread thread;
};

} // namespace tessera
