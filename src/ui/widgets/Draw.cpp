#include "Draw.hpp"

namespace tessera::draw {

namespace {

bool apply(NVGcontext* vg, const TextStyle& style) {
    if (!style.font || style.font->handle < 0)
        return false;
    nvgFontFaceId(vg, style.font->handle);
    nvgFontSize(vg, style.size);
    nvgTextLetterSpacing(vg, style.letterSpacing);
    nvgTextAlign(vg, style.align);
    nvgFillColor(vg, style.color);
    return true;
}

} // namespace

bool text(NVGcontext* vg, const TextStyle& style, float x, float y, const std::string& s) {
    if (!apply(vg, style))
        return false;
    nvgText(vg, x, y, s.c_str(), nullptr);
    return true;
}

bool textBox(NVGcontext* vg, const TextStyle& style, float x, float y, float width, const std::string& s) {
    if (!apply(vg, style))
        return false;
    nvgTextBox(vg, x, y, width, s.c_str(), nullptr);
    return true;
}

float textWidth(NVGcontext* vg, const TextStyle& style, const std::string& s) {
    if (!apply(vg, style))
        return 0;
    return nvgTextBounds(vg, 0, 0, s.c_str(), nullptr, nullptr);
}

std::string ellipsize(NVGcontext* vg, const TextStyle& style, const std::string& s, float maxWidth) {
    if (!apply(vg, style))
        return s;
    auto width = [&](const std::string& t) { return nvgTextBounds(vg, 0, 0, t.c_str(), nullptr, nullptr); };
    if (width(s) <= maxWidth)
        return s;
    std::string cut = s;
    while (!cut.empty()) {
        // Remove one whole UTF-8 character: its continuation bytes, then its lead byte.
        while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80)
            cut.pop_back();
        if (!cut.empty())
            cut.pop_back();
        if (width(cut + "…") <= maxWidth)
            return cut + "…";
    }
    return "";
}

void roundedRect(NVGcontext* vg, rack::math::Rect r, float radius, NVGcolor fill) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, radius);
    nvgFillColor(vg, fill);
    nvgFill(vg);
}

void roundedRectStroke(NVGcontext* vg, rack::math::Rect r, float radius, NVGcolor stroke, float width) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, r.pos.x + width / 2, r.pos.y + width / 2, r.size.x - width, r.size.y - width, radius);
    nvgStrokeColor(vg, stroke);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

} // namespace tessera::draw
