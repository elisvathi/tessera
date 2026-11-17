#pragma once

#include "../../model/AudioSource.hpp"
#include "../../model/Types.hpp"

#include <string>
#include <vector>

#include <rack.hpp>

/// The send menus (docs/design/AUDIO_MIXER.md §7.6), shared by the mixer's strips and the inspector's Sends section,
/// so a send is set the same way in every place it shows.
namespace tessera {

class EditorContext;
class TesseraModule;
struct Track;

namespace sendActions {

/// The parts of `t` that have a strip of their own in the mixer and so can send alone (DECISIONS §175): its drum
/// lane INs with a cable, or the clips and Track Out INs of a track that shows a strip per input.
std::vector<SendFrom> sendingParts(const TesseraModule& m, const Track& t);
/// A part's name as its mixer strip shows it ("Snare", "Clips", "IN 2"); empty for the whole track.
std::string partName(const TesseraModule& m, const Track& t, SendFrom part);
/// Who sends: the track's name, then the part's ("Drums · Snare").
std::string senderName(const TesseraModule& m, const Track& t, SendFrom part);

/// The "+ Send to…" items: every track `from` (or its part `part`) can send to, in track order with its colour;
/// the ones that would make a loop greyed with the reason. Picking one adds a send at -12 dB.
void appendAddSendItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId from, SendFrom part = {});
/// The "+ From…" items of a receiving track: every track and part that can send to `to` and doesn't yet, each
/// track followed by its parts. Picking one adds a send at -12 dB.
void appendAddFromItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId to);

/// One send's items: before or after the fader and automate (a whole track's send only), type a level, -inf,
/// remove.
void appendSendItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId from, TrackId to, SendFrom part = {});
/// Where a track's sound goes after its fader: To the Master (or its group), or Only its sends (DECISIONS §164).
void appendDestinationItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId track);
/// A track's sends, each with its level and its menu, then Send to ▸: the track menu's Sends (DECISIONS §164).
void appendTrackSends(rack::ui::Menu* menu, EditorContext& ctx, TrackId from);
/// Who sends to track `to`, tracks and their parts, each with its level and the send's menu.
void appendReceivedItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId to);
/// Where a new send starts: loud enough to hear at once.
constexpr float kNewSendDb = -12;

} // namespace sendActions
} // namespace tessera
