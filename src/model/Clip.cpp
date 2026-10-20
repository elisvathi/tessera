#include "Clip.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace tessera {

const Note* Clip::findNote(NoteId noteId) const {
    for (const Note& n : notes)
        if (n.id == noteId)
            return &n;
    return nullptr;
}

void Clip::sortNotes() { std::sort(notes.begin(), notes.end(), noteOrder); }

Tick Clip::contentEnd() const {
    Tick end = 0;
    for (const Note& n : notes)
        end = std::max(end, n.end());
    for (const AutomationEnvelope& lane : automation)
        if (!lane.points.empty())
            end = std::max(end, lane.points.back().t);
    return end;
}

int Clip::maxSimultaneousNotes() const {
    Tick from = playStart();
    Tick to = playEnd();
    // +1 at each note start, -1 at each end; ends sort first so back-to-back notes don't count as overlapping.
    std::vector<std::pair<Tick, int>> edges;
    edges.reserve(notes.size() * 2);
    for (const Note& n : notes) {
        if (n.muted || n.start < from || n.start >= to)
            continue;
        edges.push_back({n.start, +1});
        edges.push_back({std::min(n.start + n.length, to), -1});
    }
    std::sort(edges.begin(), edges.end());
    int sounding = 0, most = 0;
    for (const auto& e : edges) {
        sounding += e.second;
        most = std::max(most, sounding);
    }
    return most;
}

} // namespace tessera
