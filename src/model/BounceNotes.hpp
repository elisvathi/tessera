#pragma once

#include "TrackCommands.hpp"

#include <vector>

namespace tessera {

/// What a notes bounce takes: part of a track's timeline, or one clip.
struct NoteBounce {
    TrackId track;
    bool afterDevices = true; // the notes as they play, with devices and playheads; else as written
    int channel = 0;          // keep only this channel (a playhead's own), 0 = all
    Tick from = 0, to = 0;    // the timeline span, when `clip` isn't set
    std::vector<ArrangementId> only; // when not empty, only these timeline clips sound
    ClipId clip;                     // set: bounce this clip from the clips panel
    int slot = 0;                    // the scene its copy takes on the new track
};

/// What a notes bounce made.
struct NoteBounceMade {
    TrackId track;
    ClipId clip;
};

/// A new track under the source holding one clip of the notes `what` plays, with no devices of its own, as one undo
/// step. Null when there are no notes to bounce or the song is full.
CommandPtr bounceNotes(const Song& song, const NoteBounce& what, double tempo, const NextId& nextId,
                       NoteBounceMade* made = nullptr);

} // namespace tessera
