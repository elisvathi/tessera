#include "AudioStore.hpp"

#include "Resample.hpp"
#include "TimeStretcher.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <new>

namespace fs = std::filesystem;

namespace tessera {

namespace {

/// Files up to this long are decoded at drop time, so their full tempo guess is ready before the first sound.
constexpr double kDecodeAtDropSeconds = 60;
/// Finished renders no clip wants right now are dropped, least recently wanted first, above this many bytes.
constexpr size_t kSpareRenderBytes = size_t(512) << 20;

/// Bytes of samples in `b`.
size_t bytesOf(const AudioBuffer* b) {
    return b ? size_t(b->frames) * size_t(b->channels) * sizeof(float) : 0;
}

/// A buffer holding `d`'s samples, resampled to `rate`. Null when memory runs out or `cancel` stops it.
std::shared_ptr<AudioBuffer> prepare(DecodedAudio& d, double rate, const RenderProgress& progress) {
    auto b = std::make_shared<AudioBuffer>();
    b->channels = int(d.channels.size());
    b->rate = rate;
    if (std::abs(d.sampleRate - rate) < 1e-9) {
        b->frames = d.frames();
        for (size_t c = 0; c < d.channels.size(); c++)
            b->data[c] = std::move(d.channels[c]);
        return b;
    }
    int64_t outFrames = std::max<int64_t>(1, std::llround(double(d.frames()) * rate / d.sampleRate));
    try {
        for (size_t c = 0; c < d.channels.size(); c++)
            b->data[c].resize(size_t(outFrames));
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
    std::vector<const float*> in;
    std::vector<float*> out;
    for (size_t c = 0; c < d.channels.size(); c++) {
        in.push_back(d.channels[c].data());
        out.push_back(b->data[c].data());
    }
    if (!resampleBest(in.data(), b->channels, d.frames(), out.data(), outFrames, progress))
        return nullptr;
    b->frames = outFrames;
    return b;
}

} // namespace

std::string storageName(const std::string& displayName, uint64_t hash) {
    char hex[9];
    snprintf(hex, sizeof hex, "%08llx", static_cast<unsigned long long>(hash & 0xffffffffull));
    std::string name = displayName;
    for (char& c : name)
        if (c == '/' || c == '\\' || c == ':')
            c = '_';
    return std::string("audio/") + hex + "-" + name;
}

AudioStore::AudioStore() {
    loader = std::thread([this] { loadLoop(); });
    renderer = std::thread([this] { renderLoop(); });
}

AudioStore::~AudioStore() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    loader.join();
    renderer.join();
}

std::string AudioStore::pathOf(const std::string& file) const {
    return root.empty() ? file : (fs::path(root) / file).string();
}

void AudioStore::setFolder(const std::string& folder) {
    std::lock_guard<std::mutex> lock(mutex);
    if (folder == root)
        return;
    root = folder;
    renders.clear();
    renderQueue.clear();
    for (auto& [id, s] : sources) {
        s.buffer.reset();
        queueLoad(s);
    }
    bump();
}

void AudioStore::setEngineRate(double r) {
    if (!(r > 0))
        return;
    std::lock_guard<std::mutex> lock(mutex);
    if (std::abs(r - rate) < 1e-9)
        return;
    rate = r;
    // Renders are made from the prepared samples at one rate; new ones come with the next snapshot.
    renders.clear();
    renderQueue.clear();
    for (auto& [id, s] : sources)
        queueLoad(s);
    bump();
}

void AudioStore::queueLoad(Source& s) {
    s.status.state = SourceState::Loading;
    s.status.progress = 0;
    if (!s.queued) {
        s.queued = true;
        loads.push_back({s.info.id, rate});
    }
    wake.notify_all();
}

void AudioStore::sync(const Song& song) {
    std::lock_guard<std::mutex> lock(mutex);
    bool changed = false;
    for (const AudioSource& src : song.audioSources) {
        auto it = sources.find(src.id.value);
        if (it != sources.end() && it->second.info.file == src.file) {
            it->second.info = src;
            continue;
        }
        Source s;
        s.info = src;
        auto imp = imported.find(src.file);
        if (imp != imported.end()) {
            s.decoded = imp->second;
            imported.erase(imp);
        }
        Source& placed = sources[src.id.value] = std::move(s);
        queueLoad(placed);
        changed = true;
    }
    for (auto it = sources.begin(); it != sources.end();) {
        if (!song.findSource(AudioSourceId(it->first))) {
            AudioSourceId gone(it->first);
            renders.erase(std::remove_if(renders.begin(), renders.end(),
                                         [&](const Render& r) { return r.key.source == gone; }),
                          renders.end());
            it = sources.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        bump();
}

AudioImport AudioStore::importFile(const std::string& path, double songTempo, TimeSig ts) {
    AudioImport out;
    out.displayName = fs::path(path).filename().string();
    AudioFileInfo info;
    if (!probeAudio(path, info)) {
        out.error = fs::exists(path) ? DecodeError::NotAudio : DecodeError::CantOpen;
        return out;
    }
    uint64_t hash = hashFile(path);
    std::string file = storageName(out.displayName, hash);
    std::string folder;
    {
        std::lock_guard<std::mutex> lock(mutex);
        folder = root;
    }
    fs::path dest = folder.empty() ? fs::path(file) : fs::path(folder) / file;
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (!fs::exists(dest, ec) || hashFile(dest.string()) != hash) {
        fs::copy_file(path, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            out.error = DecodeError::CantOpen;
            return out;
        }
    }

    AudioSource& s = out.source;
    s.name = out.displayName;
    s.file = file;
    s.contentHash = hash;
    s.fileChannels = info.channels;
    s.channels = std::min(info.channels, 2);
    s.sampleRate = info.sampleRate;
    s.frames = info.frames;
    s.volts = kImportVolts;
    double seconds = info.sampleRate > 0 ? double(info.frames) / info.sampleRate : 0;
    out.guess = quickGuess(out.displayName, info.tempo, seconds, songTempo, ts);

    if (seconds <= kDecodeAtDropSeconds) {
        auto decoded = std::make_shared<DecodedAudio>();
        DecodeError e = decodeAudio(dest.string(), *decoded);
        if (e != DecodeError::None) {
            out.error = e;
            return out;
        }
        s.frames = decoded->frames();
        out.guess = fullGuess(out.guess, decoded->channels, decoded->sampleRate, songTempo);
        std::lock_guard<std::mutex> lock(mutex);
        imported[file] = decoded;
    }
    if (out.guess.bpm > 0)
        s.tempo = out.guess.bpm;
    out.ok = true;
    return out;
}

void AudioStore::adopt(AudioSourceId id, const std::string& file) {
    std::lock_guard<std::mutex> lock(mutex);
    auto imp = imported.find(file);
    auto it = sources.find(id.value);
    if (imp == imported.end() || it == sources.end())
        return;
    it->second.decoded = imp->second;
    imported.erase(imp);
}

void AudioStore::want(const std::vector<RenderKey>& keys) {
    std::lock_guard<std::mutex> lock(mutex);
    wantRound++;
    for (const RenderKey& k : keys) {
        auto it = std::find_if(renders.begin(), renders.end(), [&](const Render& r) { return r.key == k; });
        if (it == renders.end()) {
            Render r;
            r.key = k;
            r.status.state = RenderState::Waiting;
            r.lastWanted = wantRound;
            renders.push_back(r);
            renderQueue.push_back(k);
        } else {
            it->lastWanted = wantRound;
        }
    }
    // Waiting renders the clips no longer want go: the tempo moved on before they started.
    auto stale = [&](const RenderKey& k) {
        auto it = std::find_if(renders.begin(), renders.end(), [&](const Render& r) { return r.key == k; });
        return it == renders.end() || it->lastWanted != wantRound;
    };
    renderQueue.erase(std::remove_if(renderQueue.begin(), renderQueue.end(), stale), renderQueue.end());
    renders.erase(std::remove_if(renders.begin(), renders.end(),
                                 [&](const Render& r) {
                                     return r.status.state == RenderState::Waiting && r.lastWanted != wantRound;
                                 }),
                  renders.end());
    evictRenders();
    wake.notify_all();
}

void AudioStore::retry(const RenderKey& key) {
    std::lock_guard<std::mutex> lock(mutex);
    for (Render& r : renders)
        if (r.key == key && r.status.state == RenderState::Failed) {
            r.status = {RenderState::Waiting, 0};
            renderQueue.push_back(key);
            wake.notify_all();
            bump();
        }
}

void AudioStore::evictRenders() {
    size_t spare = 0;
    for (const Render& r : renders)
        if (r.lastWanted != wantRound)
            spare += bytesOf(r.buffer.get());
    while (spare > kSpareRenderBytes) {
        auto oldest = renders.end();
        for (auto it = renders.begin(); it != renders.end(); ++it)
            if (it->lastWanted != wantRound && it->buffer && (oldest == renders.end() || it->lastWanted < oldest->lastWanted))
                oldest = it;
        if (oldest == renders.end())
            break;
        spare -= bytesOf(oldest->buffer.get());
        renders.erase(oldest);
    }
}

SourceStatus AudioStore::status(AudioSourceId id) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = sources.find(id.value);
    return it == sources.end() ? SourceStatus{SourceState::Missing, 0, nullptr, 0, {}} : it->second.status;
}

RenderStatus AudioStore::renderStatus(const RenderKey& key) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const Render& r : renders)
        if (r.key == key)
            return r.status;
    return {};
}

size_t AudioStore::memoryBytes() const {
    std::lock_guard<std::mutex> lock(mutex);
    size_t n = 0;
    for (const auto& [id, s] : sources)
        n += bytesOf(s.buffer.get()) + (s.status.peaks ? s.status.peaks->bytes() : 0);
    for (const Render& r : renders)
        n += bytesOf(r.buffer.get());
    return n;
}

bool AudioStore::idle() const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& [id, s] : sources)
        if (s.status.state == SourceState::Loading)
            return false;
    return true;
}

void AudioStore::waitIdle() {
    std::unique_lock<std::mutex> lock(mutex);
    settled.wait(lock, [&] {
        bool rendering = std::any_of(renders.begin(), renders.end(), [](const Render& r) {
            return r.status.state == RenderState::Waiting || r.status.state == RenderState::Rendering;
        });
        return loads.empty() && busy == 0 && !rendering;
    });
}

std::shared_ptr<const AudioBuffer> AudioStore::prepared(AudioSourceId source) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = sources.find(source.value);
    return it == sources.end() ? nullptr : it->second.buffer;
}

std::shared_ptr<const AudioBuffer> AudioStore::render(const RenderKey& key, RenderKey& found) {
    std::lock_guard<std::mutex> lock(mutex);
    const Render* best = nullptr;
    for (const Render& r : renders) {
        if (r.status.state != RenderState::Ready || !r.buffer || r.bufferRate != rate || !(r.key.source == key.source) ||
            r.key.semitones != key.semitones || r.key.sound != key.sound)
            continue;
        if (r.key == key) {
            best = &r;
            break;
        }
        if (!best || std::abs(std::log(r.key.factor / key.factor)) < std::abs(std::log(best->key.factor / key.factor)))
            best = &r;
    }
    if (!best)
        return nullptr;
    found = best->key;
    return best->buffer;
}

// ---- Workers ------------------------------------------------------------------------------------------------

void AudioStore::loadLoop() {
    std::unique_lock<std::mutex> lock(mutex);
    while (true) {
        wake.wait(lock, [&] { return stopping || !loads.empty(); });
        if (stopping)
            return;
        LoadJob job = loads.front();
        loads.pop_front();
        busy++;
        lock.unlock();
        load(job);
        lock.lock();
        busy--;
        settled.notify_all();
        wake.notify_all(); // a render may have waited for this source
    }
}

void AudioStore::load(const LoadJob& job) {
    std::string path;
    std::shared_ptr<DecodedAudio> decoded;
    double songTempo = 120;
    AudioGuess quick;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = sources.find(job.id.value);
        if (it == sources.end())
            return;
        Source& s = it->second;
        s.queued = false;
        path = pathOf(s.info.file);
        decoded = std::move(s.decoded);
        double seconds = s.info.seconds();
        quick = quickGuess(s.info.name, s.info.tempo, seconds, s.info.tempo > 0 ? s.info.tempo : songTempo, TimeSig{});
    }
    auto setProgress = [&](double p) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = sources.find(job.id.value);
        if (it == sources.end() || stopping || job.rate != rate)
            return false;
        it->second.status.progress = float(p);
        return true;
    };
    auto fail = [&](SourceState state) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = sources.find(job.id.value);
        if (it != sources.end() && job.rate == rate) {
            it->second.status.state = state;
            bump();
        }
    };

    if (!decoded) {
        decoded = std::make_shared<DecodedAudio>();
        DecodeError e = decodeAudio(path, *decoded, [&](double p) { return setProgress(p * 0.8); });
        if (e == DecodeError::Cancelled)
            return;
        if (e != DecodeError::None) {
            fail(e == DecodeError::CantOpen ? SourceState::Missing
                 : e == DecodeError::NoMemory ? SourceState::NoMemory
                                              : SourceState::Failed);
            return;
        }
    }
    std::shared_ptr<const PeakCache> peaks;
    AudioGuess guess;
    try {
        peaks = std::make_shared<PeakCache>(decoded->channels);
        guess = fullGuess(quick, decoded->channels, decoded->sampleRate, quick.bpm > 0 ? quick.bpm : songTempo);
    } catch (const std::bad_alloc&) {
        fail(SourceState::NoMemory);
        return;
    }
    double fileRate = decoded->sampleRate;
    std::shared_ptr<AudioBuffer> buffer = prepare(*decoded, job.rate, [&](double p) { return setProgress(0.8 + 0.2 * p); });
    if (!buffer) {
        fail(job.rate == rate ? SourceState::NoMemory : SourceState::Loading);
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    auto it = sources.find(job.id.value);
    if (it == sources.end() || job.rate != rate)
        return;
    Source& s = it->second;
    s.buffer = std::move(buffer);
    s.bufferRate = job.rate;
    s.status.state = SourceState::Ready;
    s.status.progress = 1;
    s.status.peaks = std::move(peaks);
    s.status.sampleRate = fileRate;
    s.status.guess = guess;
    bump();
}

void AudioStore::renderLoop() {
    std::unique_lock<std::mutex> lock(mutex);
    while (true) {
        // The first waiting render whose source is ready; one whose source failed fails too.
        auto ready = [&]() -> std::deque<RenderKey>::iterator {
            for (auto it = renderQueue.begin(); it != renderQueue.end(); ++it) {
                auto s = sources.find(it->source.value);
                if (s == sources.end() || s->second.status.state != SourceState::Loading)
                    return it;
            }
            return renderQueue.end();
        };
        wake.wait(lock, [&] { return stopping || ready() != renderQueue.end(); });
        if (stopping)
            return;
        auto pick = ready();
        RenderKey key = *pick;
        renderQueue.erase(pick);
        busy++;
        lock.unlock();
        renderOne(key);
        lock.lock();
        busy--;
        settled.notify_all();
    }
}

void AudioStore::renderOne(const RenderKey& key) {
    std::shared_ptr<const AudioBuffer> in;
    double inRate = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = sources.find(key.source.value);
        auto r = std::find_if(renders.begin(), renders.end(), [&](const Render& x) { return x.key == key; });
        if (r == renders.end())
            return;
        if (it == sources.end() || !it->second.buffer || it->second.status.state != SourceState::Ready) {
            r->status = {RenderState::Failed, 0};
            bump();
            return;
        }
        in = it->second.buffer;
        inRate = it->second.bufferRate;
        r->status = {RenderState::Rendering, 0};
        bump();
    }
    auto out = std::make_shared<AudioBuffer>();
    out->channels = in->channels;
    out->rate = inRate;
    out->frames = std::max<int64_t>(1, stretchedFrames(in->frames, key.factor));
    bool ok = true;
    try {
        for (int c = 0; c < in->channels; c++)
            out->data[size_t(c)].resize(size_t(out->frames));
    } catch (const std::bad_alloc&) {
        ok = false;
    }
    if (ok) {
        std::vector<const float*> src;
        std::vector<float*> dst;
        for (int c = 0; c < in->channels; c++) {
            src.push_back(in->data[size_t(c)].data());
            dst.push_back(out->data[size_t(c)].data());
        }
        StretchJob job;
        job.in = src.data();
        job.channels = in->channels;
        job.inFrames = in->frames;
        job.sampleRate = inRate;
        job.ratio = key.factor;
        job.semitones = key.semitones;
        job.sound = key.sound;
        std::unique_ptr<TimeStretcher> stretcher = makeStretcher("signalsmith");
        ok = stretcher->render(job, dst.data(), out->frames, [&](double p) {
            std::lock_guard<std::mutex> lock(mutex);
            auto r = std::find_if(renders.begin(), renders.end(), [&](const Render& x) { return x.key == key; });
            if (stopping || r == renders.end() || inRate != rate)
                return false;
            r->status.progress = float(p);
            return true;
        });
    }
    std::lock_guard<std::mutex> lock(mutex);
    auto r = std::find_if(renders.begin(), renders.end(), [&](const Render& x) { return x.key == key; });
    if (r == renders.end() || inRate != rate)
        return;
    if (ok) {
        r->buffer = std::move(out);
        r->bufferRate = inRate;
        r->status = {RenderState::Ready, 1};
    } else {
        r->status = {RenderState::Failed, 0};
    }
    bump();
}

} // namespace tessera
