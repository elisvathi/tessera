#include "Draw.hpp"

#include "../Theme.hpp"

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

void dice(NVGcontext* vg, rack::math::Rect r, DicePart part, int32_t variation, bool hot, NVGcolor tint) {
    const theme::Palette& c = theme::colors();
    if (part == DicePart::Roll) {
        roundedRect(vg, r, 5, hot ? c.line1 : c.bg2);
        roundedRectStroke(vg, r, 5, c.line1, 1);
        rack::math::Vec m = r.getCenter();
        roundedRectStroke(vg, rack::math::Rect(m.x - 5.5f, m.y - 5.5f, 11, 11), 2.5f, c.text0, 1.2f);
        nvgBeginPath(vg);
        for (float k : {-2.6f, 0.f, 2.6f})
            nvgCircle(vg, m.x + k, m.y + k, 1);
        nvgFillColor(vg, c.text0);
        nvgFill(vg);
        return;
    }
    bool fixedHalf = part == DicePart::Fixed;
    bool on = fixedHalf == (variation >= 1);
    NVGcolor lit = fixedHalf ? tint : c.accent;
    // Each half rounded on its outer side only, so the two read as one switch.
    nvgBeginPath(vg);
    if (fixedHalf)
        nvgRoundedRectVarying(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, 5, 0, 0, 5);
    else
        nvgRoundedRectVarying(vg, r.pos.x, r.pos.y, r.size.x, r.size.y, 0, 5, 5, 0);
    nvgFillColor(vg, on ? nvgTransRGBAf(lit, 0.28f) : hot ? c.bg2 : nvgRGBAf(0, 0, 0, 0));
    nvgFill(vg);
    nvgStrokeColor(vg, c.line1);
    nvgStrokeWidth(vg, 1);
    nvgStroke(vg);
    TextStyle style{fixedHalf ? theme::fonts::mono() : theme::fonts::ui(), fixedHalf ? 10.5f : 11.f,
                    on ? c.text0 : c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    int32_t number = variation < 0 ? -variation : variation;
    text(vg, style, r.getCenter().x, r.getCenter().y, fixedHalf ? std::to_string(number) : std::string("New"));
}

} // namespace tessera::draw
