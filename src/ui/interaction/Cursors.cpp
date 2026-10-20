#include "Cursors.hpp"

#include <array>
#include <cmath>
#include <map>

namespace tessera {

namespace {

int glfwShape(CursorShape shape) {
    switch (shape) {
    case CursorShape::IBeam:
        return GLFW_IBEAM_CURSOR;
    case CursorShape::Crosshair:
        return GLFW_CROSSHAIR_CURSOR;
    case CursorShape::Hand:
        return GLFW_POINTING_HAND_CURSOR;
    case CursorShape::ResizeH:
        return GLFW_RESIZE_EW_CURSOR;
    case CursorShape::ResizeV:
        return GLFW_RESIZE_NS_CURSOR;
    case CursorShape::Move:
        return GLFW_RESIZE_ALL_CURSOR;
    case CursorShape::NotAllowed:
        return GLFW_NOT_ALLOWED_CURSOR;
    case CursorShape::Arrow:
    case CursorShape::AddPoint:
    default:
        return GLFW_ARROW_CURSOR;
    }
}

// Standard cursors live for the whole session; GLFW frees them when it terminates.
std::map<int, GLFWcursor*>& cache() {
    static std::map<int, GLFWcursor*> cursors;
    return cursors;
}

/// Draws the add-point cursor: a ring like a curve point, its middle on the hot spot, and a plus at the lower
/// right. White with a dark outline, so it reads on any background.
GLFWcursor* makeAddPointCursor() {
    constexpr int kSize = 24;
    constexpr int kHot = 7;
    static std::array<unsigned char, kSize * kSize * 4> pixels;
    pixels.fill(0);
    auto put = [](int x, int y, unsigned char v) {
        if (x < 0 || y < 0 || x >= kSize || y >= kSize)
            return;
        unsigned char* p = &pixels[size_t((y * kSize + x) * 4)];
        p[0] = p[1] = p[2] = v;
        p[3] = 255;
    };
    auto rect = [&](int x0, int y0, int x1, int y1, unsigned char v) {
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                put(x, y, v);
    };
    // Outline first, then the white shape over it.
    for (int pass = 0; pass < 2; pass++) {
        unsigned char v = pass == 0 ? 20 : 255;
        float grow = pass == 0 ? 1.f : 0.f;
        for (int y = 0; y < kSize; y++)
            for (int x = 0; x < kSize; x++) {
                float d = std::hypot(float(x - kHot), float(y - kHot));
                if (std::fabs(d - 4.f) <= 0.8f + grow)
                    put(x, y, v);
            }
        int g = pass == 0 ? 1 : 0;
        rect(14 - g, 17 - g, 22 + g, 18 + g, v); // the plus: across
        rect(17 - g, 14 - g, 18 + g, 22 + g, v); // and down
    }
    GLFWimage image;
    image.width = kSize;
    image.height = kSize;
    image.pixels = pixels.data();
    return glfwCreateCursor(&image, kHot, kHot);
}

CursorShape current = CursorShape::Arrow;
bool applied = false;

} // namespace

void applyCursor(CursorShape shape) {
    if (!APP->window || !APP->window->win || (applied && shape == current))
        return;
    // The custom cursor is cached under a key no standard shape uses.
    int key = shape == CursorShape::AddPoint ? -1 : glfwShape(shape);
    GLFWcursor*& cursor = cache()[key];
    if (!cursor)
        cursor = shape == CursorShape::AddPoint ? makeAddPointCursor() : glfwCreateStandardCursor(key);
    glfwSetCursor(APP->window->win, cursor);
    current = shape;
    applied = true;
}

void resetCursor() {
    if (APP->window && APP->window->win)
        glfwSetCursor(APP->window->win, nullptr);
    applied = false;
    current = CursorShape::Arrow;
}

} // namespace tessera
