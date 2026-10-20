#pragma once

#include "../model/ArrangementEdits.hpp"
#include "Engine.hpp"

#include <map>
#include <vector>

namespace tessera {

/// A stretch of time a launcher clip played on a track while "record to arrangement" was on.
struct CapturedSegment {
    TrackId track;
    ClipId clip;
    double start = 0;    // song ticks
    double end = 0;      // song ticks
    double clipTick = 0; // where the clip was at `start`
};

/// Turns the engine's capture events into segments (DESIGN.md §10 "Record arrangement from launcher").
/// UI thread: it is fed from the engine's capture queue every frame.
class CaptureBuilder {
  public:
    void add(const CaptureEvent& event);
    /// Ends every open segment at `songTick` (recording switched off).
    void closeAll(double songTick);
    bool empty() const { return open.empty() && closed.empty(); }
    /// The finished segments, emptying the list.
    std::vector<CapturedSegment> take();
    /// Segments still playing, for drawing the recording live.
    const std::map<uint64_t, CapturedSegment>& active() const { return open; }

  private:
    void close(uint64_t track, double songTick);

    std::map<uint64_t, CapturedSegment> open; // by TrackId value
    std::vector<CapturedSegment> closed;
};

/// One undo step that places the segments on the timeline, over whatever was there.
CommandPtr recordArrangement(const Song& song, const std::vector<CapturedSegment>& segments,
                             const arrange::IdSource& newId);

} // namespace tessera
