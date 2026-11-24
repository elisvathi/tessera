#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// Reading audio files (WAV, AIFF, FLAC, MP3) into float samples. Used on worker threads only: it reads files and
/// allocates. Needs no Rack, so it is unit tested.
namespace tessera {

/// What a file holds, read from its header without decoding it, to estimate memory before a long decode.
struct AudioFileInfo {
    int channels = 0; // as in the file
    double sampleRate = 0;
    int64_t frames = 0;
    double tempo = 0; // from the file's tempo chunk (loop libraries write one into WAV files); 0 = none
};

/// A decoded file: at most two channels, each a full list of samples (-1..1 is full scale).
struct DecodedAudio {
    int fileChannels = 0; // what the file had; more than two are mixed down to stereo
    double sampleRate = 0;
    double tempo = 0;
    std::vector<std::vector<float>> channels;

    int64_t frames() const { return channels.empty() ? 0 : int64_t(channels[0].size()); }
};

enum class DecodeError {
    None,
    CantOpen,  // missing, unreadable, or a folder
    NotAudio,  // not a format Tessera reads
    NoMemory,  // the samples don't fit in memory
    Cancelled, // the progress callback said stop
};

/// Reports decode progress from 0 to 1; returning false cancels.
using DecodeProgress = std::function<bool(double)>;

/// Reads a file's header. False when it isn't audio Tessera can read.
bool probeAudio(const std::string& path, AudioFileInfo& info);

/// Decodes a whole file. Files with more than two channels are mixed down to stereo: odd channels to the left,
/// even ones to the right.
DecodeError decodeAudio(const std::string& path, DecodedAudio& out, const DecodeProgress& progress = {});

/// A 64-bit hash of a file's bytes, to find the same file again after a paste or a move. 0 when it can't be read.
uint64_t hashFile(const std::string& path);

/// Whether a file name looks like audio Tessera reads (by its extension), for drops and file dialogs.
bool looksLikeAudio(const std::string& path);

} // namespace tessera
