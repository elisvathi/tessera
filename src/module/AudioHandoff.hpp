#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace tessera {

/// Gives one module's process() a raw pointer to a shared object chosen on the UI thread.
///
/// Swapping the object must not free the old one while process() might still be reading it. So a replaced
/// object is kept alive until process() reports a sample that started after the swap.
template <typename T>
class AudioHandoff {
  public:
    // UI thread

    void set(std::shared_ptr<T> next) {
        if (next == current)
            return;
        pointer.store(next.get());
        // A process() call already running may still hold the old pointer; it finishes before
        // reporting the sample after the one we last saw.
        if (current)
            retired.push_back({std::move(current), processed.load() + 1});
        current = std::move(next);
    }

    /// Frees retired objects that process() can no longer reach. Call regularly on the UI thread.
    void collect() {
        int64_t done = processed.load();
        for (size_t i = 0; i < retired.size();) {
            if (done >= retired[i].safeAfter) {
                retired[i] = std::move(retired.back());
                retired.pop_back();
            } else {
                i++;
            }
        }
    }

    const std::shared_ptr<T>& owner() const { return current; }

    // Audio thread

    T* get() const { return pointer.load(); }
    /// Call at the end of process() (and processBypass()): every call, or every Nth, since a report also covers
    /// the samples before it.
    void markProcessed(int64_t frame) { processed.store(frame); }

  private:
    struct Retired {
        std::shared_ptr<T> object;
        int64_t safeAfter;
    };

    std::atomic<T*> pointer{nullptr};
    std::atomic<int64_t> processed{-1};
    std::shared_ptr<T> current;
    std::vector<Retired> retired;
};

} // namespace tessera
