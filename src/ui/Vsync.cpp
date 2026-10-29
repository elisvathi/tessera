#include "Vsync.hpp"

#include <GLFW/glfw3.h>

namespace tessera::vsync {

namespace {
bool want = false;
bool applied = false; // Rack opens its window with vsync off
} // namespace

bool wanted() { return want; }

void setWanted(bool on) { want = on; }

void apply() {
    if (want == applied)
        return;
    glfwSwapInterval(want ? 1 : 0);
    applied = want;
}

} // namespace tessera::vsync
