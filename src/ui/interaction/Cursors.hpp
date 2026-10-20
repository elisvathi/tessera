#pragma once

#include "Gesture.hpp"

namespace tessera {

/// Sets the OS mouse cursor. Cheap to call every frame: it only changes the cursor when the shape changes.
void applyCursor(CursorShape shape);
/// Back to the system default, e.g. when the editor closes.
void resetCursor();

} // namespace tessera
