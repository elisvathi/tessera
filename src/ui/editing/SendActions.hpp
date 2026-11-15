#pragma once

#include "../../model/Types.hpp"

#include <rack.hpp>

/// The send menus (docs/design/AUDIO_MIXER.md §7.6), shared by the mixer's strips and the inspector's Sends section,
/// so a send is set the same way in every place it shows.
namespace tessera {

class EditorContext;

namespace sendActions {

/// The "+ Send to…" items: every track `from` can send to, in track order with its colour; the ones that would
/// make a loop greyed with the reason. Picking one adds a send at -inf and shows the sends to that track.
void appendAddSendItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId from);

/// One send's items: show the sends to its track, before or after the fader, type a level, -inf, automate, remove.
void appendSendItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId from, TrackId to);
/// Where a track's sound goes after its fader: To the Master (or its group), or Only its sends (DECISIONS §164).
void appendDestinationItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId track);
/// A track's sends, each with its level and its menu, then Send to ▸: the track menu's Sends (DECISIONS §164).
void appendTrackSends(rack::ui::Menu* menu, EditorContext& ctx, TrackId from);
/// Who sends to track `to`, each with its level and the send's menu: a click on a strip's "← 3".
void appendReceivedItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId to);
/// Where a new send starts: loud enough to hear at once.
constexpr float kNewSendDb = -12;

} // namespace sendActions
} // namespace tessera
