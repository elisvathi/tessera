#pragma once

#include "../../model/Types.hpp"

/// Bounce (docs/design/AUDIO_MIXER.md §8.6): records a track's sound, effects included, onto a new audio track.
namespace tessera {

class EditorContext;

namespace bounceActions {

/// The bounce dialog for `track`: Length (the selected clips, the selected area, or until silent: each only when
/// there is something like it), Mute the original afterwards, an estimate, and Bounce.
void openDialog(EditorContext& ctx, TrackId track);

} // namespace bounceActions
} // namespace tessera
