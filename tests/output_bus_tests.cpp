#include "doctest.h"

#include "../src/engine/VoicePicker.hpp"
#include "../src/module/AudioHandoff.hpp"
#include "../src/module/OutputBus.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <random>
#include <set>
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

TEST_CASE("a Track Out on All packs more than 16 voices: sticky, and the first playheads first") {
    // Three channels of 8 voices each: 24 voices, more than an output's 16.
    TrackFrame frame;
    frame.channels = 24;
    frame.byChannel = true;
    for (int v = 0; v < 24; v++) {
        frame.part[v] = uint8_t(1 + v / 8);
        frame.used[v] = 1;
    }
    VoicePicker picker;
    std::array<int8_t, kMaxVoices> out;
    auto gates = [&](std::initializer_list<int> on) {
        for (int v = 0; v < 24; v++)
            frame.gate[v] = 0;
        for (int v : on)
            frame.gate[v] = 10;
    };

    // Channel 2 plays alone: its voices take the first free channels.
    gates({8, 9});
    CHECK(picker.pick(frame, 0, out) == kMaxVoices);
    CHECK(out[0] == 8);
    CHECK(out[1] == 9);
    // Quiet a moment, then the same voice plays again: it goes back where it was.
    gates({});
    picker.pick(frame, 0, out);
    gates({9});
    picker.pick(frame, 0, out);
    CHECK(out[1] == 9);

    // All 24 sound: the 16 first in playhead order win, channel 3's last voices don't fit.
    for (int v = 0; v < 24; v++)
        frame.gate[v] = 10;
    picker.pick(frame, 0, out);
    std::set<int> sent(out.begin(), out.end());
    for (int v = 0; v < 16; v++)
        CHECK(sent.count(v) == 1);
    CHECK(out[1] == 9); // channel 2's voice kept its place
}

TEST_CASE("a Track Out on one channel sends that channel's own voices when the track has more than 16") {
    TrackFrame frame;
    frame.channels = 24;
    frame.byChannel = true;
    for (int v = 0; v < 24; v++) {
        frame.part[v] = uint8_t(1 + v / 8);
        frame.used[v] = 1;
    }
    VoicePicker picker;
    std::array<int8_t, kMaxVoices> out;
    CHECK(picker.pick(frame, 3, out) == 8);
    for (int i = 0; i < 8; i++)
        CHECK(out[size_t(i)] == 16 + i);
}

TEST_CASE("a playhead channel sends only the voices it has needed: a one-note bass sends one") {
    // Channel 1 (a bass) has 3 voices and has only ever needed its first; channel 2 has used all 3.
    TrackFrame frame;
    frame.channels = 6;
    frame.byChannel = true;
    for (int v = 0; v < 6; v++)
        frame.part[v] = uint8_t(1 + v / 3);
    frame.used[0] = 1;
    frame.used[3] = frame.used[4] = frame.used[5] = 1;
    VoicePicker picker;
    std::array<int8_t, kMaxVoices> out;
    CHECK(picker.pick(frame, 1, out) == 1);
    CHECK(out[0] == 0);
    CHECK(picker.pick(frame, 2, out) == 3);
    CHECK(picker.pick(frame, 0, out) == 4); // All: the four voices in use, not six
}

TEST_CASE("a Track Out with 16 voices or fewer sends them as they are") {
    TrackFrame frame;
    frame.channels = 5;
    VoicePicker picker;
    std::array<int8_t, kMaxVoices> out;
    CHECK(picker.pick(frame, 0, out) == 5);
    for (int i = 0; i < 5; i++)
        CHECK(out[size_t(i)] == i);
}
