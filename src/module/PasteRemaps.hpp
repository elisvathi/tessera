#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace tessera {

/// Remembers which Tessera copies were pasted in which UI frame, so output modules pasted together with a
/// Tessera follow the copy instead of the original.
///
/// Rack gives pasted modules new IDs but leaves module data untouched, so a pasted Track Out still names the
/// original Tessera. Everything in one paste is added during the same UI frame, which is what links them.
/// A Track Out pasted on its own finds no record for its frame and keeps following the original.
class PasteRemaps {
  public:
    static constexpr double kKeepSeconds = 10;

    void record(int64_t fromId, int64_t toId, double frameTime) {
        prune(frameTime);
        entries.push_back({fromId, toId, frameTime});
    }

    /// The copy of `fromId` pasted in the frame at `frameTime`, if any. NaN never matches.
    std::optional<int64_t> find(int64_t fromId, double frameTime) const {
        for (const Entry& e : entries)
            if (e.from == fromId && e.frameTime == frameTime)
                return e.to;
        return std::nullopt;
    }

  private:
    struct Entry {
        int64_t from;
        int64_t to;
        double frameTime;
    };

    void prune(double now) {
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const Entry& e) { return !(e.frameTime >= now - kKeepSeconds); }),
                      entries.end());
    }

    std::vector<Entry> entries;
};

} // namespace tessera
