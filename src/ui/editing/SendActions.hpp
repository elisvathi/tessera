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

} // namespace sendActions
} // namespace tessera
