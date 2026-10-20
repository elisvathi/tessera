#pragma once

#include "../EditorContext.hpp"

/// The record button's settings menu and the capture command (DESIGN.md §14).
namespace tessera::recordActions {

/// Count-in, new clip length, arranger overdub or replace, input quantize, snap to key, capture, MIDI input.
void openMenu(EditorContext& ctx);

/// Turns the notes last played while not recording into a clip on the selected track, and shows it.
void capture(EditorContext& ctx);

} // namespace tessera::recordActions
