#include "CaptureWriter.hpp"

#include <dr_wav.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

namespace tessera {

/// One file being written.
struct CaptureWriter::Job {
    int id = 0;
    std::shared_ptr<FrameRing> ring;
    std::string path;
    double sampleRate = 48000;
    Format format = Format::Float32;
    drwav wav{};
    bool open = false;
    bool finishing = false;
    bool cancelled = false;
    bool done = false;
    std::atomic<bool> failed{false};
    std::atomic<int64_t> frames{0};
    std::vector<float> peaks; // guarded by the writer's mutex
    float peak = 0;
    int peakCount = 0;
    std::minstd_rand noise{12345};
};

CaptureWriter::CaptureWriter() : thread([this] { run(); }) {}

CaptureWriter::~CaptureWriter() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        for (auto& job : jobs)
            job->finishing = true;
    }
    wake.notify_all();
    thread.join();
}

int CaptureWriter::start(std::shared_ptr<FrameRing> ring, const std::string& path, double sampleRate, Format format) {
    auto job = std::make_unique<Job>();
    job->ring = std::move(ring);
    job->path = path;
    job->sampleRate = sampleRate;
    job->format = format;
    std::lock_guard<std::mutex> lock(mutex);
    job->id = nextId++;
    int id = job->id;
    jobs.push_back(std::move(job));
    return id;
}

bool CaptureWriter::finish(int id, int64_t* frames) {
    std::unique_lock<std::mutex> lock(mutex);
    for (auto it = jobs.begin(); it != jobs.end(); ++it)
        if ((*it)->id == id) {
            if ((*it)->done) {
                if (frames)
                    *frames = (*it)->frames.load();
                jobs.erase(it);
                return true;
            }
            (*it)->finishing = true;
            lock.unlock();
            wake.notify_all();
            return false;
        }
    return true;
}

void CaptureWriter::cancel(int id) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& job : jobs)
            if (job->id == id)
                job->cancelled = job->finishing = true;
    }
    wake.notify_all();
}

int64_t CaptureWriter::written(int id) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& job : jobs)
        if (job->id == id)
            return job->frames.load();
    return 0;
}

bool CaptureWriter::failed(int id) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& job : jobs)
        if (job->id == id)
            return job->failed.load() || (job->ring && job->ring->overflow());
    return false;
}

std::vector<float> CaptureWriter::peaks(int id) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& job : jobs)
        if (job->id == id)
            return job->peaks;
    return {};
}

bool CaptureWriter::drain(Job& job) {
    constexpr size_t kChunk = 4096;
    float buffer[kChunk * 2];
    bool any = false;
    while (true) {
        size_t n = job.ring->pop(buffer, kChunk);
        if (n == 0)
            break;
        any = true;
        if (!job.open && !job.failed) {
            drwav_data_format f{};
            f.container = drwav_container_riff;
            f.format = job.format == Format::Float32 ? DR_WAVE_FORMAT_IEEE_FLOAT : DR_WAVE_FORMAT_PCM;
            f.channels = 2;
            f.sampleRate = drwav_uint32(std::lround(job.sampleRate));
            f.bitsPerSample = job.format == Format::Float32 ? 32 : 24;
            job.open = drwav_init_file_write(&job.wav, job.path.c_str(), &f, nullptr);
            if (!job.open)
                job.failed = true;
        }
        // Volts to the file's full scale: 10 V is 0 dB (§7.4).
        for (size_t i = 0; i < n * 2; i++)
            buffer[i] /= 10.f;
        for (size_t i = 0; i < n; i++) {
            job.peak = std::max(job.peak, std::max(std::fabs(buffer[i * 2]), std::fabs(buffer[i * 2 + 1])));
            if (++job.peakCount == 512) {
                std::lock_guard<std::mutex> lock(mutex);
                job.peaks.push_back(job.peak);
                job.peak = 0;
                job.peakCount = 0;
            }
        }
        if (!job.open)
            continue;
        drwav_uint64 wrote = 0;
        if (job.format == Format::Float32) {
            wrote = drwav_write_pcm_frames(&job.wav, n, buffer);
        } else {
            // 24 bits with triangular dither, little-endian.
            std::vector<uint8_t> bytes(n * 2 * 3);
            std::uniform_real_distribution<float> u(-0.5f, 0.5f);
            for (size_t i = 0; i < n * 2; i++) {
                float x = std::clamp(buffer[i], -1.f, 1.f) * 8388607.f + u(job.noise) + u(job.noise);
                int32_t v = int32_t(std::clamp(std::lround(x), -8388608L, 8388607L));
                bytes[i * 3] = uint8_t(v & 0xFF);
                bytes[i * 3 + 1] = uint8_t((v >> 8) & 0xFF);
                bytes[i * 3 + 2] = uint8_t((v >> 16) & 0xFF);
            }
            wrote = drwav_write_pcm_frames(&job.wav, n, bytes.data());
        }
        if (wrote != n)
            job.failed = true;
        job.frames += int64_t(wrote);
    }
    return any;
}

void CaptureWriter::run() {
    while (true) {
        std::vector<Job*> work;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(10));
            if (stopping && jobs.empty())
                return;
            for (auto& job : jobs)
                if (!job->done)
                    work.push_back(job.get());
        }
        for (Job* job : work) {
            bool cancelled;
            {
                std::lock_guard<std::mutex> lock(mutex);
                cancelled = job->cancelled;
            }
            if (!cancelled)
                drain(*job);
            std::lock_guard<std::mutex> lock(mutex);
            if (job->cancelled) {
                // Only this thread touches the file, so only it closes and deletes it.
                if (job->open)
                    drwav_uninit(&job->wav);
                std::remove(job->path.c_str());
                jobs.erase(std::find_if(jobs.begin(), jobs.end(), [&](const auto& j) { return j.get() == job; }));
                continue;
            }
            if (job->finishing && job->ring->available() == 0) {
                if (job->open)
                    drwav_uninit(&job->wav);
                job->open = false;
                job->done = true;
            }
        }
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping && std::all_of(jobs.begin(), jobs.end(), [](const auto& j) { return j->done; }))
            return;
    }
}

} // namespace tessera
