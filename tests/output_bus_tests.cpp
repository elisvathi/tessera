#include "doctest.h"

#include "../src/module/AudioHandoff.hpp"
#include "../src/module/OutputBus.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <random>
#include <thread>
#include <vector>

using namespace tessera;

namespace {

constexpr int kSlot = 3;
const TrackId kTrack(7);

void writeSample(OutputBus& bus, int64_t n, TrackId track = kTrack) {
    TrackFrame* frames = bus.beginWrite(n);
    frames[kSlot].trackId = track.value;
    frames[kSlot].gate[0] = float(n); // tag each frame with the sample that wrote it
    bus.publish(n);
}

/// Every thread waits here once per sample, like Rack's engine threads between samples.
class SpinBarrier {
  public:
    explicit SpinBarrier(int count) : count(count) {}
    void wait() {
        int gen = generation.load();
        if (arrived.fetch_add(1) + 1 == count) {
            arrived.store(0);
            generation.fetch_add(1);
        } else {
            while (generation.load() == gen)
                std::this_thread::yield();
        }
    }

  private:
    const int count;
    std::atomic<int> arrived{0};
    std::atomic<int> generation{0};
};

} // namespace

TEST_CASE("readers get exactly the previous sample's frame, in any processing order") {
    OutputBus bus;
    std::mt19937 rng(1);
    std::vector<int> order = {0, 1, 2, 3}; // 0 = Tessera, 1-3 = readers

    for (int64_t n = 0; n < 5000; n++) {
        std::shuffle(order.begin(), order.end(), rng);
        for (int actor : order) {
            if (actor == 0) {
                writeSample(bus, n);
                continue;
            }
            const TrackFrame* f = bus.read(n, kSlot, kTrack);
            if (n == 0) {
                CHECK(f == nullptr);
            } else {
                REQUIRE(f != nullptr);
                REQUIRE(f->gate[0] == float(n - 1));
            }
        }
    }
}

TEST_CASE("readers on other threads stay one sample behind, all aligned") {
    constexpr int kReaders = 3;
    constexpr int64_t kSamples = 20000;
    OutputBus bus;
    SpinBarrier barrier(kReaders + 1);
    std::atomic<int> wrong{0};

    std::vector<std::thread> threads;
    threads.emplace_back([&] {
        for (int64_t n = 0; n < kSamples; n++) {
            writeSample(bus, n);
            barrier.wait();
        }
    });
    for (int r = 0; r < kReaders; r++) {
        threads.emplace_back([&] {
            for (int64_t n = 0; n < kSamples; n++) {
                const TrackFrame* f = bus.read(n, kSlot, kTrack);
                bool ok = n == 0 ? f == nullptr : (f && f->gate[0] == float(n - 1));
                if (!ok)
                    wrong++;
                barrier.wait();
            }
        });
    }
    for (std::thread& t : threads)
        t.join();
    CHECK(wrong.load() == 0);
}

TEST_CASE("a reader gets nothing when Tessera skipped the previous sample") {
    OutputBus bus;
    for (int64_t n = 0; n < 10; n++)
        writeSample(bus, n);
    // Sample 10 is skipped: Tessera bypassed, removed, or stalled.
    CHECK(bus.read(11, kSlot, kTrack) == nullptr);
    CHECK(bus.read(12, kSlot, kTrack) == nullptr);

    writeSample(bus, 12);
    const TrackFrame* f = bus.read(13, kSlot, kTrack);
    REQUIRE(f != nullptr);
    CHECK(f->gate[0] == 12.f);
}

TEST_CASE("a reader never sees another track's frame in its slot") {
    OutputBus bus;
    writeSample(bus, 0, TrackId(99)); // the slot now belongs to a different track
    CHECK(bus.read(1, kSlot, kTrack) == nullptr);
    CHECK(bus.read(1, kSlot, TrackId(99)) != nullptr);
    CHECK(bus.read(1, kSlot, TrackId()) == nullptr); // unbound
    CHECK(bus.read(1, -1, kTrack) == nullptr);
    CHECK(bus.read(1, kMaxTracks, kTrack) == nullptr);
}

TEST_CASE("AudioHandoff keeps a replaced object alive until process() moves past it") {
    struct Probe {
        std::atomic<int>& alive;
        explicit Probe(std::atomic<int>& a) : alive(a) { alive++; }
        ~Probe() { alive--; }
    };
    std::atomic<int> alive{0};
    AudioHandoff<Probe> handoff;

    handoff.set(std::make_shared<Probe>(alive));
    Probe* first = handoff.get();
    handoff.markProcessed(5);

    handoff.set(std::make_shared<Probe>(alive));
    CHECK(handoff.get() != first);
    handoff.collect();
    CHECK(alive == 2); // a process() call for sample 6 may still hold `first`

    handoff.markProcessed(6);
    handoff.collect();
    CHECK(alive == 1);

    handoff.set(nullptr);
    CHECK(handoff.get() == nullptr);
    handoff.markProcessed(7);
    handoff.collect();
    CHECK(alive == 0);
}

TEST_CASE("the return bus gives Tessera what each module wrote one sample before, in any order") {
    OutputBus bus;
    ReturnBus& returns = bus.returns();
    int a = returns.claim(), b = returns.claim();
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    CHECK(a != b);
    std::mt19937 rng(3);
    std::vector<int> order = {0, 1, 2}; // 0 = Tessera, 1 and 2 = modules
    for (int64_t n = 0; n < 3000; n++) {
        std::shuffle(order.begin(), order.end(), rng);
        for (int actor : order) {
            if (actor == 1)
                returns.write(n, a, kTrack, float(n), -float(n));
            else if (actor == 2)
                returns.write(n, b, TrackId(9), 1, 1);
            else if (n > 0) {
                float l, r;
                REQUIRE(returns.read(n, a, kTrack, l, r));
                CHECK(l == float(n - 1));
                CHECK(r == -float(n - 1));
                // A slot read for another track gives nothing.
                CHECK_FALSE(returns.read(n, b, kTrack, l, r));
                CHECK(l == 0);
            }
        }
    }
}

TEST_CASE("a module that stops writing, or writes nonsense, drops out of the return bus") {
    ReturnBus returns;
    int slot = returns.claim();
    returns.write(10, slot, kTrack, 1, 1);
    float l, r;
    CHECK(returns.read(11, slot, kTrack, l, r));
    CHECK_FALSE(returns.read(12, slot, kTrack, l, r)); // nothing at sample 11
    returns.write(12, slot, kTrack, std::nanf(""), 0);
    CHECK_FALSE(returns.read(13, slot, kTrack, l, r));
    CHECK(l == 0);
    returns.release(slot);
    CHECK(returns.claim() == slot);
}

TEST_CASE("audio OUT reads the previous sample, and nothing on samples without audio") {
    OutputBus bus;
    AudioOutFrame* out = bus.beginAudioWrite(4);
    out[kSlot] = {kTrack.value, 0.5f, -0.5f};
    bus.publishAudio(4);
    const AudioOutFrame* f = bus.readAudio(5, kSlot, kTrack);
    REQUIRE(f);
    CHECK(f->l == 0.5f);
    CHECK_FALSE(bus.readAudio(5, kSlot, TrackId(99)));
    CHECK_FALSE(bus.readAudio(6, kSlot, kTrack)); // the player didn't run at sample 5
}
