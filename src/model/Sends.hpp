#pragma once

#include "Commands.hpp"
#include "Song.hpp"

#include <string>
#include <vector>

/// Sends from one track to another (docs/design/AUDIO_MIXER.md §4.4, §7.6): the graph rules and the commands. A send
/// goes to the receiving track's OUT, never into its sound directly, and sends never make a loop.
namespace tessera {

/// Whether `from` reaches `to` through sends, directly or through other tracks.
bool sendsReach(const Song& song, TrackId from, TrackId to);
/// Why `from` (or its part `part`) can't send to `to`: "" when it can. "Would feed back into Bass" for a loop.
std::string whyNoSend(const Song& song, TrackId from, TrackId to, SendFrom part = {});
/// The tracks that send to `to`, from the whole track or a part of it, each once, in song order.
std::vector<TrackId> sendersTo(const Song& song, TrackId to);
/// One send and the track it belongs to.
struct SendRef {
    TrackId track;
    Send send;
};
/// Every send into `to`, from whole tracks and their parts, in song order: the receiving strip's FROM rows.
std::vector<SendRef> sendsInto(const Song& song, TrackId to);
/// Every track in the order its sound is worked out: each sender before the tracks it sends to, song order
/// otherwise.
std::vector<TrackId> sendOrder(const Song& song);
/// The send from `from` (or its part `part`) to `to`, or null.
const Send* findSend(const Song& song, TrackId from, TrackId to, SendFrom part = {});

/// Adds a send from `from` (or its part `part`) to `to` at `db` (-inf by default: the next move is turning it up).
/// Null when whyNoSend() says no.
CommandPtr addSend(const Song& song, TrackId from, TrackId to, float db = kSilentDb, SendFrom part = {});
/// Changes an existing send, found by its `to` and `from` (its level, before or after the fader). Null when
/// unchanged or not there.
CommandPtr setSend(const Song& song, TrackId from, Send send, const std::string& name);
/// Removes the send from `from` (or its part `part`) to `to`. Null when there is none.
CommandPtr removeSend(const Song& song, TrackId from, TrackId to, SendFrom part = {});
/// Drops sends to missing tracks, to themselves, repeated ones, from parts the track can't have (a lane of a track
/// that isn't a drum track), and any that would close a loop; clamps levels.
void normalizeSends(Song& song);

} // namespace tessera
