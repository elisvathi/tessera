#pragma once

#include "../../model/Types.hpp"

/// Bounce (docs/design/AUDIO_MIXER.md §8.6): records a track's sound, effects included, onto a new audio track.
namespace tessera {

class EditorContext;

namespace bounceActions {

/// The bounce dialog for `track`: Length (the selected clips, the selected area, or until silent: each only when
/// there is something like it), Mute the original afterwards, an estimate, and Bounce.
void openDialog(EditorContext& ctx, TrackId track);

/// The real-time bounce of a track's sound onto a new audio track.
void openAudioDialog(EditorContext& ctx, TrackId track);

/// The instant bounce of a note or drum track's notes onto a new track.
void openNotes(EditorContext& ctx, TrackId track);

/// A clip's menu in the clips panel: the notes bounce choices for this clip, to a new track in the same scene.
void openClipDialog(EditorContext& ctx, TrackId track, ClipId clip, int slot);

} // namespace bounceActions
} // namespace tessera
