#include "EditorContext.hpp"

#include "../module/Registry.hpp"
#include "../module/TesseraModule.hpp"

#include <algorithm>
#include <cassert>

namespace tessera {

TesseraModule* EditorContext::module() const { return Registry::get().findSource(moduleId); }

void EditorContext::selectTrackOffset(int delta) const {
    TesseraModule* m = module();
    if (!m || m->song().tracks.empty())
        return;
    const Song& song = m->song();
    int index = std::max(0, song.trackIndex(state().selectedTrack));
    index = std::clamp(index + delta, 0, int(song.tracks.size()) - 1);
    state().selectedTrack = song.tracks[size_t(index)].id;
}

EditorState& EditorContext::state() const {
    TesseraModule* m = module();
    assert(m && "EditorContext::state() without a module");
    return m->editor();
}

} // namespace tessera
