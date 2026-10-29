#pragma once

namespace tessera::vsync {

/// Whether the user asked for vsync (a Tessera setting kept in the plugin's settings, off by default).
bool wanted();
/// Turns the wish on or off; `apply()` makes it so on the next frame.
void setWanted(bool on);
/// Called each frame from the UI thread with Rack's window current: switches the window's buffer swaps to wait for
/// the screen (no tearing) or back to Rack's own setting. Rack sets it once when it opens its window and never
/// again, so ours stays until changed.
void apply();

} // namespace tessera::vsync
