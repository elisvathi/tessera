#pragma once

#include "Snapshot.hpp"

#include <atomic>
#include <deque>
#include <memory>

namespace tessera {

/// Hands snapshots from the UI thread to the engine without locks or audio-thread frees.
///
/// The UI publishes a new snapshot by swapping one atomic pointer. The engine picks up the newest one and
/// acknowledges its generation. Only snapshots older than the acknowledged one can no longer be reached by
/// the engine, so the UI frees exactly those, on the UI thread.
class SnapshotExchange {
  public:
    // UI thread

    void publish(std::unique_ptr<Snapshot> snapshot) {
        snapshot->generation = nextGeneration++;
        latestPublished = snapshot.get();
        pending.store(snapshot.get(), std::memory_order_release);
        live.push_back(std::move(snapshot));
        collect();
    }

    /// Frees snapshots the engine has moved past. publish() calls this too.
    void collect() {
        uint64_t acked = acknowledged.load(std::memory_order_acquire);
        while (live.size() > 1 && live.front()->generation < acked)
            live.pop_front();
    }

    /// The newest snapshot published, as the UI last built it.
    const Snapshot* latest() const { return latestPublished; }
    size_t liveCount() const { return live.size(); }

    // Audio thread

    /// The newest snapshot if it changed since the last call, otherwise nullptr.
    const Snapshot* poll() {
        const Snapshot* s = pending.load(std::memory_order_acquire);
        if (s == current)
            return nullptr;
        current = s;
        acknowledged.store(s->generation, std::memory_order_release);
        return s;
    }

  private:
    std::atomic<const Snapshot*> pending{nullptr};
    std::atomic<uint64_t> acknowledged{0};

    // UI thread
    std::deque<std::unique_ptr<Snapshot>> live; // in publish order, oldest first
    uint64_t nextGeneration = 1;
    const Snapshot* latestPublished = nullptr;

    // Audio thread
    const Snapshot* current = nullptr;
};

} // namespace tessera
