#include "EditorContext.hpp"

#include "../module/Registry.hpp"
#include "../module/TesseraModule.hpp"
#include "EditorWindow.hpp"

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

bool EditorContext::drivesView() const { return surface == Surface::Overlay || !EditorWindow::isOpenFor(moduleId); }

EditorState& EditorContext::state() const {
    TesseraModule* m = module();
    // Views also draw in the module browser, with no module: they read defaults there.
    static EditorState noModule;
    return m ? m->editor() : noModule;
}

} // namespace tessera
