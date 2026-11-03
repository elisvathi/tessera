#pragma once

#include "../engine/AudioSnapshotBuilder.hpp"
#include "../model/AudioAnalysis.hpp"
#include "../model/Song.hpp"
#include "AudioDecode.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// The audio files of one Tessera, ready to play: it copies dropped files into patch storage, decodes them,
/// resamples them to Rack's rate, builds their waveforms and makes the stretched renders clips need
/// (docs/design/AUDIO_MIXER.md §5.2, §8.7). Work happens on two worker threads (loading, rendering), so the UI never
/// waits on a file and the audio thread only ever sees finished buffers. Needs no Rack.
namespace tessera {

/// How far a source is along, for its clips' marks (§6.6).
enum class SourceState : uint8_t {
    Loading,  // decoding and resampling; `progress` says how far
    Ready,    // plays
    Missing,  // its file isn't in patch storage
    Failed,   // the file is there but couldn't be read
    NoMemory, // it didn't fit in memory
};

struct SourceStatus {
    SourceState state = SourceState::Loading;
    float progress = 0;
    std::shared_ptr<const PeakCache> peaks; // the waveform, in the source's own frames; null until decoded
    double sampleRate = 0;                  // of the file
    AudioGuess guess;                       // from the samples, once decoded
};

enum class RenderState : uint8_t { None, Waiting, Rendering, Ready, Failed };

struct RenderStatus {
    RenderState state = RenderState::None;
    float progress = 0;
};

/// A dropped file after it was copied into patch storage and looked at.
struct AudioImport {
    bool ok = false;
    DecodeError error = DecodeError::None;
    std::string displayName; // the file's own name
    AudioSource source;      // without an ID yet
    AudioGuess guess;
};

class AudioStore : public AudioBuffers {
  public:
    AudioStore();
    ~AudioStore() override;
    AudioStore(const AudioStore&) = delete;
    AudioStore& operator=(const AudioStore&) = delete;

    // ---- UI thread ----------------------------------------------------------------------------------------------

    /// The patch storage folder; files live in its "audio" folder. Changing it reloads every source.
    void setFolder(const std::string& folder);
    const std::string& folder() const { return root; }
    /// Rack's sample rate. A change prepares every source again; clips keep playing what they have meanwhile.
    void setEngineRate(double rate);
    double engineRate() const { return rate; }

    /// Starts loading every source the song has that the store doesn't know yet, and forgets the ones it no longer
    /// has. Call after each edit and after loading a patch.
    void sync(const Song& song);

    /// Copies `path` into patch storage and works out what it is: the quick tempo guess at once, and for files up
    /// to a minute long the full guess from the samples, whose decoded samples the store keeps to play at once.
    /// `songTempo` and `ts` pick the nearest loop tempo.
    AudioImport importFile(const std::string& path, double songTempo, TimeSig ts);
    /// Gives the samples decoded by importFile() to the source that now has `id`, so it doesn't decode twice.
    void adopt(AudioSourceId id, const std::string& file);

    /// The renders the clips want at this tempo (from buildAudioSnapshot). Waiting renders no longer wanted are
    /// dropped; finished ones stay a while for tempo changes back and forth.
    void want(const std::vector<RenderKey>& keys);
    /// Tries a failed render again (the clip menu's Try again).
    void retry(const RenderKey& key);
    /// Loads again every source whose file was missing or unreadable, after files were copied in.
    void retryMissing();

    SourceStatus status(AudioSourceId id) const;
    RenderStatus renderStatus(const RenderKey& key) const;
    /// Goes up whenever a buffer, a waveform or a status changes, so the module knows to build a new snapshot.
    uint64_t changes() const { return changeCount.load(std::memory_order_acquire); }
    /// Bytes of samples held in memory (prepared and rendered), for the Master strip's tooltip.
    size_t memoryBytes() const;
    /// Whether every known source is ready or settled (missing or failed): nothing loads.
    bool idle() const;
    /// The full path of a source's file in patch storage.
    std::string pathOf(const std::string& file) const;
    /// Blocks until the workers have nothing to do (tests and saving).
    void waitIdle();

    // ---- AudioBuffers (UI thread) -----------------------------------------------------------------------------
    std::shared_ptr<const AudioBuffer> prepared(AudioSourceId source) override;
    std::shared_ptr<const AudioBuffer> render(const RenderKey& key, RenderKey& found) override;

  private:
    struct Source {
        AudioSource info;
        SourceStatus status;
        std::shared_ptr<const AudioBuffer> buffer; // at `bufferRate`
        double bufferRate = 0;
        std::shared_ptr<DecodedAudio> decoded; // handed over by an import; used once
        bool queued = false;
    };
    struct Render {
        RenderKey key;
        RenderStatus status;
        std::shared_ptr<const AudioBuffer> buffer;
        double bufferRate = 0;
        uint64_t lastWanted = 0;
    };
    struct LoadJob {
        AudioSourceId id;
        double rate;
    };

    void loadLoop();
    void renderLoop();
    void load(const LoadJob& job);
    void renderOne(const RenderKey& key);
    void bump() { changeCount.fetch_add(1, std::memory_order_acq_rel); }
    void queueLoad(Source& s);
    void evictRenders();

    mutable std::mutex mutex;
    std::condition_variable wake, settled;
    std::string root;
    double rate = 48000;
    std::map<uint64_t, Source> sources;
    std::vector<Render> renders;
    std::deque<LoadJob> loads;
    std::deque<RenderKey> renderQueue;
    std::map<std::string, std::shared_ptr<DecodedAudio>> imported; // by file, until adopt()
    uint64_t wantRound = 0;
    int busy = 0;
    bool stopping = false;
    std::atomic<uint64_t> changeCount{1};
    std::thread loader, renderer;
};

/// Copies the song's audio files that `to` lacks from `from` (the patch storage of the module this one was pasted
/// from), when the copy there has the source's content hash. Returns how many it copied (DECISIONS §102).
int copyMissingFiles(const Song& song, const std::string& from, const std::string& to);

/// Copies the song's audio files from patch storage into `folder` (flat), for a saved project file. Returns how many.
int exportFiles(const Song& song, const std::string& storage, const std::string& folder);
/// The other way, for an opened project: copies files from `folder` into patch storage when they are missing there
/// and match their source's hash. Returns how many.
int importFiles(const Song& song, const std::string& folder, const std::string& storage);

/// Removes the files in `folder`/audio that no source of the song uses, as a loaded patch does. Returns how many.
int removeUnusedFiles(const Song& song, const std::string& folder);

/// The file name a dropped file gets in patch storage: "audio/" + 8 hex digits of its hash + "-" + its name.
std::string storageName(const std::string& displayName, uint64_t hash);

} // namespace tessera
