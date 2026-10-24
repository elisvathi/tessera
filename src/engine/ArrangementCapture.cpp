#include "ArrangementCapture.hpp"

#include <cmath>

namespace tessera {

void CaptureBuilder::add(const CaptureEvent& e) {
    close(e.track, e.songTick);
    if (e.clip == 0)
        return;
    CapturedSegment s;
    s.track = TrackId(e.track);
    s.clip = ClipId(e.clip);
    s.start = e.songTick;
    s.end = e.songTick;
    s.clipTick = e.clipTick;
    open[e.track] = s;
}

void CaptureBuilder::close(uint64_t track, double songTick) {
    auto it = open.find(track);
    if (it == open.end())
        return;
    it->second.end = songTick;
    if (it->second.end > it->second.start)
        closed.push_back(it->second);
    open.erase(it);
}

void CaptureBuilder::closeAll(double songTick) {
    while (!open.empty())
        close(open.begin()->first, songTick);
}

std::vector<CapturedSegment> CaptureBuilder::take() {
    std::vector<CapturedSegment> out;
    out.swap(closed);
    return out;
}

CommandPtr recordArrangement(const Song& song, const std::vector<CapturedSegment>& segments,
                             const arrange::IdSource& newId) {
    using namespace arrange;
    Layout before = layoutOf(song);
    Layout after = before;
    IdSet added;
    for (const CapturedSegment& s : segments) {
        const Clip* clip = song.findClip(s.clip);
        Tick start = Tick(std::llround(s.start));
        Tick end = Tick(std::llround(s.end));
        const Track* track = song.findTrack(s.track);
        if (!clip || !track || end <= start)
            continue;
        Placed p{s.track, ArrangerClip{}};
        p.clip.id = newId();
        p.clip.clip = s.clip;
        p.clip.position = start;
        p.clip.length = end - start;
        p.clip.offset = normalizeOffset(*clip, Tick(std::llround(s.clipTick)),
                                        track->type == TrackType::Drum ? &track->drumLanes : nullptr);
        // Segments go in the order they were recorded, so a later pass over the same time (the song loop came
        // round) wins over an earlier one.
        after.push_back(p);
        after = resolveOverlaps(after, {p.clip.id}, song.clips, newId);
        added.insert(p.clip.id);
    }
    if (added.empty())
        return nullptr;
    return replaceArrangement(song, "Record to arrangement", before, after);
}

} // namespace tessera
