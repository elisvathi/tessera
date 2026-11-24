#include "AudioDecode.hpp"

#include <dr_flac.h>
#include <dr_mp3.h>
#include <dr_wav.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <new>

namespace tessera {

namespace {

constexpr int64_t kBlock = 65536;

/// The file's extension in lower case, without the dot.
std::string extensionOf(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "";
    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

/// One open file of any format, read as interleaved floats.
class Reader {
  public:
    virtual ~Reader() = default;
    int channels = 0;
    double sampleRate = 0;
    int64_t frames = 0; // total, as the header says
    double tempo = 0;
    /// Reads up to `n` frames into `out` (interleaved); returns how many it read.
    virtual int64_t read(float* out, int64_t n) = 0;
};

/// WAV and AIFF, through dr_wav.
class WavReader final : public Reader {
  public:
    /// Opens `path`; check `ok`.
    explicit WavReader(const std::string& path) {
        ok = drwav_init_file_with_metadata(&wav, path.c_str(), 0, nullptr);
        if (!ok)
            return;
        channels = int(wav.channels);
        sampleRate = double(wav.sampleRate);
        frames = int64_t(wav.totalPCMFrameCount);
        for (drwav_uint32 i = 0; i < wav.metadataCount; i++)
            if (wav.pMetadata[i].type == drwav_metadata_type_acid && wav.pMetadata[i].data.acid.tempo > 0)
                tempo = double(wav.pMetadata[i].data.acid.tempo);
    }
    ~WavReader() override {
        if (ok)
            drwav_uninit(&wav);
    }
    int64_t read(float* out, int64_t n) override { return int64_t(drwav_read_pcm_frames_f32(&wav, drwav_uint64(n), out)); }
    bool ok = false;

  private:
    drwav wav{};
};

/// FLAC, through dr_flac.
class FlacReader final : public Reader {
  public:
    /// Opens `path`; check `ok()`.
    explicit FlacReader(const std::string& path) : flac(drflac_open_file(path.c_str(), nullptr)) {
        if (!flac)
            return;
        channels = int(flac->channels);
        sampleRate = double(flac->sampleRate);
        frames = int64_t(flac->totalPCMFrameCount);
    }
    ~FlacReader() override {
        if (flac)
            drflac_close(flac);
    }
    int64_t read(float* out, int64_t n) override {
        return int64_t(drflac_read_pcm_frames_f32(flac, drflac_uint64(n), out));
    }
    bool ok() const { return flac != nullptr; }

  private:
    drflac* flac = nullptr;
};

/// MP3, through dr_mp3. Counting its frames reads the whole file once.
class Mp3Reader final : public Reader {
  public:
    /// Opens `path`; check `ok`.
    explicit Mp3Reader(const std::string& path) {
        ok = drmp3_init_file(&mp3, path.c_str(), nullptr);
        if (!ok)
            return;
        channels = int(mp3.channels);
        sampleRate = double(mp3.sampleRate);
        frames = int64_t(drmp3_get_pcm_frame_count(&mp3));
        // An MP3 decoder will find "frames" in almost any noise; a real file has some.
        if (frames <= 0 || channels <= 0) {
            drmp3_uninit(&mp3);
            ok = false;
        }
    }
    ~Mp3Reader() override {
        if (ok)
            drmp3_uninit(&mp3);
    }
    int64_t read(float* out, int64_t n) override {
        return int64_t(drmp3_read_pcm_frames_f32(&mp3, drmp3_uint64(n), out));
    }
    bool ok = false;

  private:
    drmp3 mp3{};
};

/// The first four bytes of a file, to tell formats apart when the extension lies. Empty if unreadable.
std::string magicOf(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return "";
    char buf[4] = {};
    size_t got = std::fread(buf, 1, 4, f);
    std::fclose(f);
    return std::string(buf, got);
}

/// Opens `path` with the reader its content (or else its extension) points to. Null when nothing reads it.
std::unique_ptr<Reader> open(const std::string& path, bool& exists) {
    std::string magic = magicOf(path);
    exists = !magic.empty();
    if (!exists)
        return nullptr;
    auto tryWav = [&]() -> std::unique_ptr<Reader> {
        auto r = std::make_unique<WavReader>(path);
        return r->ok ? std::move(r) : nullptr;
    };
    auto tryFlac = [&]() -> std::unique_ptr<Reader> {
        auto r = std::make_unique<FlacReader>(path);
        return r->ok() ? std::move(r) : nullptr;
    };
    auto tryMp3 = [&]() -> std::unique_ptr<Reader> {
        auto r = std::make_unique<Mp3Reader>(path);
        return r->ok ? std::move(r) : nullptr;
    };
    if (magic == "RIFF" || magic == "RIFX" || magic == "FORM" || magic == "RF64" || magic == "riff")
        return tryWav();
    if (magic == "fLaC")
        return tryFlac();
    // MP3 has no fixed magic: an ID3 tag or a frame sync. Only trust it for files that say they are MP3 or start
    // like one.
    bool sync = magic.size() >= 2 && static_cast<unsigned char>(magic[0]) == 0xFF &&
                (static_cast<unsigned char>(magic[1]) & 0xE0) == 0xE0;
    if (magic.compare(0, 3, "ID3") == 0 || sync || extensionOf(path) == "mp3")
        return tryMp3();
    return nullptr;
}

} // namespace

bool looksLikeAudio(const std::string& path) {
    std::string ext = extensionOf(path);
    return ext == "wav" || ext == "wave" || ext == "aif" || ext == "aiff" || ext == "aifc" || ext == "flac" ||
           ext == "mp3";
}

bool probeAudio(const std::string& path, AudioFileInfo& info) {
    bool exists = false;
    std::unique_ptr<Reader> r = open(path, exists);
    if (!r || r->channels <= 0 || r->sampleRate <= 0)
        return false;
    info.channels = r->channels;
    info.sampleRate = r->sampleRate;
    info.frames = r->frames;
    info.tempo = r->tempo;
    return true;
}

DecodeError decodeAudio(const std::string& path, DecodedAudio& out, const DecodeProgress& progress) {
    bool exists = false;
    std::unique_ptr<Reader> r = open(path, exists);
    if (!r)
        return exists ? DecodeError::NotAudio : DecodeError::CantOpen;
    if (r->channels <= 0 || r->sampleRate <= 0)
        return DecodeError::NotAudio;

    int in = r->channels;
    int keep = std::min(in, 2);
    out = DecodedAudio{};
    out.fileChannels = in;
    out.sampleRate = r->sampleRate;
    out.tempo = r->tempo;
    std::vector<float> block;
    try {
        out.channels.assign(size_t(keep), {});
        for (auto& c : out.channels)
            c.reserve(size_t(std::max<int64_t>(0, r->frames)));
        block.resize(size_t(kBlock * in));
    } catch (const std::bad_alloc&) {
        out = DecodedAudio{};
        return DecodeError::NoMemory;
    }

    // With more than two channels each side gets the average of its channels, so a 5.1 file keeps its level.
    std::vector<int> perSide(2, 0);
    for (int c = 0; c < in; c++)
        perSide[size_t(c % keep)]++;
    int64_t done = 0;
    while (true) {
        if (progress && !progress(r->frames > 0 ? std::min(1.0, double(done) / double(r->frames)) : 0)) {
            out = DecodedAudio{};
            return DecodeError::Cancelled;
        }
        int64_t got = r->read(block.data(), kBlock);
        if (got <= 0)
            break;
        try {
            for (int s = 0; s < keep; s++) {
                std::vector<float>& dst = out.channels[size_t(s)];
                size_t at = dst.size();
                dst.resize(at + size_t(got));
                if (in <= 2) {
                    for (int64_t i = 0; i < got; i++)
                        dst[at + size_t(i)] = block[size_t(i * in + s)];
                } else {
                    float scale = 1.f / float(perSide[size_t(s)]);
                    for (int64_t i = 0; i < got; i++) {
                        float sum = 0;
                        for (int c = s; c < in; c += keep)
                            sum += block[size_t(i * in + c)];
                        dst[at + size_t(i)] = sum * scale;
                    }
                }
            }
        } catch (const std::bad_alloc&) {
            out = DecodedAudio{};
            return DecodeError::NoMemory;
        }
        done += got;
    }
    if (out.frames() == 0) {
        out = DecodedAudio{};
        return DecodeError::NotAudio;
    }
    if (progress)
        progress(1);
    return DecodeError::None;
}

uint64_t hashFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return 0;
    // FNV-1a over the bytes, then mixed with the length: quick, and plenty to tell files apart.
    uint64_t h = 1469598103934665603ull;
    uint64_t length = 0;
    std::vector<unsigned char> buf(1 << 16);
    size_t got;
    while ((got = std::fread(buf.data(), 1, buf.size(), f)) > 0) {
        for (size_t i = 0; i < got; i++) {
            h ^= buf[i];
            h *= 1099511628211ull;
        }
        length += got;
    }
    std::fclose(f);
    h ^= length + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h ? h : 1;
}

} // namespace tessera
