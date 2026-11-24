#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

/// One-file project files (docs/design/AUDIO_EDITING.md §6): a ZIP holding project.json and the song's audio, not
/// compressed. Files are streamed, so a project can be far larger than memory, and the ZIP64 extension takes it past
/// 4 GB. Needs no Rack. Only worker threads should call the functions that copy audio.
namespace tessera::bundle {

/// A file to put in the project: `name` is its path inside, like "audio/3fa1b2c4-vox.wav".
struct AudioEntry {
    std::string name;
    std::string path;
};

/// A file wanted from a project: its path inside, and its content hash (0 when unknown).
struct Wanted {
    std::string file;
    uint64_t contentHash = 0;
};

/// Writes `text` to `path` through a temporary file, so a failure or a crash leaves the old file whole.
bool writeText(const std::string& path, const std::string& text);

/// Writes a project to `path`, the same safe way. `progress` goes from 0 to 1. False when a file couldn't be read
/// or the disk couldn't take it; the old project is then untouched.
bool write(const std::string& path, const std::string& projectJson, const std::vector<AudioEntry>& audio,
           std::atomic<float>* progress = nullptr);

/// Whether `path` is a one-file project. Older projects are plain JSON and say no.
bool isBundle(const std::string& path);

/// The project.json inside the bundle at `path`. False when it isn't there or is damaged.
bool readProject(const std::string& path, std::string& json);

/// Copies the wanted files that `storage` lacks out of the bundle. A file whose content doesn't match its hash, or
/// whose name would land outside `storage`, is left out. Returns how many it copied.
int extract(const std::string& path, const std::vector<Wanted>& wanted, const std::string& storage,
            std::atomic<float>* progress = nullptr);

} // namespace tessera::bundle
