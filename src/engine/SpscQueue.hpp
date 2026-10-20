#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace tessera {

/// A fixed-size, lock-free queue for exactly one producer thread and one consumer thread.
/// No allocation after construction, so the audio thread can use it. When full, push() drops the item.
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

  public:
    /// Producer side. Returns false when the queue is full.
    bool push(const T& item) {
        size_t head = writeIndex.load(std::memory_order_relaxed);
        if (head - readIndex.load(std::memory_order_acquire) >= Capacity)
            return false;
        items[head & (Capacity - 1)] = item;
        writeIndex.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Consumer side. Returns false when the queue is empty.
    bool pop(T& item) {
        size_t tail = readIndex.load(std::memory_order_relaxed);
        if (tail == writeIndex.load(std::memory_order_acquire))
            return false;
        item = items[tail & (Capacity - 1)];
        readIndex.store(tail + 1, std::memory_order_release);
        return true;
    }

  private:
    std::array<T, Capacity> items{};
    std::atomic<size_t> writeIndex{0};
    std::atomic<size_t> readIndex{0};
};

} // namespace tessera
