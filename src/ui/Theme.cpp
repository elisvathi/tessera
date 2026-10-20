#include "Theme.hpp"

#include "../model/TrackColors.hpp"
#include "../plugin.hpp"

#include <vector>

namespace tessera::theme {

NVGcolor rgb(uint32_t hex, float alpha) {
    return nvgRGBAf(((hex >> 16) & 0xFF) / 255.f, ((hex >> 8) & 0xFF) / 255.f, (hex & 0xFF) / 255.f, alpha);
}

namespace {

// Values from docs/design/mockups/gen/colors.py (OKLCH sources, contrast checked there).
Palette make(std::initializer_list<uint32_t> hex, bool dark) {
    std::vector<NVGcolor> v;
    for (uint32_t h : hex)
        v.push_back(rgb(h));
    return {v[0], v[1],  v[2],  v[3],  v[4],  v[5],  v[6],  v[7],  v[8],
            v[9], v[10], v[11], v[12], v[13], v[14], v[15], v[16], dark};
}

//                  bg0       bg1       bg2       line0     line1     line2     text0     text1     text2
//                  accent    record    play      queue     select    ground    ink       onTile
const Palette& plaster() {
    static const Palette p =
        make({0xFDFBF9, 0xF5F2ED, 0xE9E5DE, 0xEEEBE6, 0xE1DDD7, 0xC2BDB5, 0x171B21, 0x484D56, 0x646972, 0x5356CD,
              0xC9302D, 0x007D50, 0xA06604, 0x5356CD, 0xE0DCD4, 0xFCFCFC, 0x13151B},
             false);
    return p;
}

const Palette& sand() {
    static const Palette p =
        make({0xF3E6D4, 0xEBDCC9, 0xE1D2BD, 0xE7D8C5, 0xD8C8B3, 0xB9A892, 0x1E2229, 0x484D56, 0x5E636D, 0x0060C0,
              0xBE2323, 0x007448, 0x975D00, 0x0060C0, 0xDAC9B4, 0xFCFCFC, 0x13151B},
             false);
    return p;
}

const Palette& slate() {
    static const Palette p =
        make({0x292B30, 0x2F3238, 0x3B3F45, 0x303238, 0x373B40, 0x4C5057, 0xD8DBDF, 0xAFB4BD, 0x969CA6, 0x909CF5,
              0xF76560, 0x5AD69B, 0xF0C45E, 0x909CF5, 0x212429, 0x131524, 0x13151B},
             true);
    return p;
}

// The schemes named after editor themes take each theme's published palette for canvas, surfaces, text and
// accents, mapped onto the same roles.
const Palette& gruvbox() {
    static const Palette p =
        make({0x282828, 0x32302F, 0x3C3836, 0x2F2D2C, 0x3C3836, 0x504945, 0xEBDBB2, 0xD5C4A1, 0xA89984, 0x83A598,
              0xFB4934, 0xB8BB26, 0xFABD2F, 0x83A598, 0x1D2021, 0x1D2021, 0x13151B},
             true);
    return p;
}

const Palette& nord() {
    static const Palette p =
        make({0x2E3440, 0x3B4252, 0x434C5E, 0x353B49, 0x3B4252, 0x4C566A, 0xECEFF4, 0xD8DEE9, 0x9AA3B4, 0x88C0D0,
              0xBF616A, 0xA3BE8C, 0xEBCB8B, 0x81A1C1, 0x272C36, 0x2E3440, 0x13151B},
             true);
    return p;
}

const Palette& catppuccin() { // the Frappé flavour, the softest of its dark ones
    static const Palette p =
        make({0x303446, 0x292C3C, 0x414559, 0x363A4F, 0x414559, 0x51576D, 0xC6D0F5, 0xB5BFE2, 0x949CBB, 0xCA9EE6,
              0xE78284, 0xA6D189, 0xE5C890, 0xBABBF1, 0x232634, 0x232634, 0x13151B},
             true);
    return p;
}

const Palette& oneDark() {
    static const Palette p =
        make({0x282C34, 0x2C313A, 0x3A3F4B, 0x2E323B, 0x323742, 0x4B5263, 0xABB2BF, 0x9DA5B4, 0x7F848E, 0x61AFEF,
              0xE06C75, 0x98C379, 0xE5C07B, 0x61AFEF, 0x21252B, 0x1E2127, 0x13151B},
             true);
    return p;
}

const Palette& monokai() {
    static const Palette p =
        make({0x272822, 0x2D2E27, 0x3E3D32, 0x2E2F28, 0x34352D, 0x49483E, 0xF8F8F2, 0xCFCFC2, 0x90908A, 0x66D9EF,
              0xF92672, 0xA6E22E, 0xE6DB74, 0x66D9EF, 0x1E1F1A, 0x1E1F1A, 0x13151B},
             true);
    return p;
}

Scheme chosen = Scheme::FollowRack;

Scheme resolved(Scheme s) {
    if (s != Scheme::FollowRack)
        return s;
    return rack::settings::preferDarkPanels ? Scheme::Slate : Scheme::Plaster;
}

const Palette& paletteOf(Scheme s) {
    switch (resolved(s)) {
    case Scheme::Plaster:
        return plaster();
    case Scheme::Sand:
        return sand();
    case Scheme::Gruvbox:
        return gruvbox();
    case Scheme::Nord:
        return nord();
    case Scheme::Catppuccin:
        return catppuccin();
    case Scheme::OneDark:
        return oneDark();
    case Scheme::Monokai:
        return monokai();
    case Scheme::Slate:
    default:
        return slate();
    }
}

// Light schemes use the same hues, darker, so tiles keep their contrast on a pale canvas.
constexpr uint32_t kTrackColorsLight[16] = {
    0xDF6C65, 0xDA743C, 0xCB8100, 0xB39000, 0x929D16, 0x65A84A, 0x1AAE74, 0x00AF9B,
    0x00ABBE, 0x00A3D8, 0x4498E8, 0x768BEC, 0x9B7FE2, 0xB875CD, 0xCD6DAF, 0xDA698C,
};
constexpr float kLightDeepen = 0.86f; // for colours outside the ring

} // namespace

Scheme scheme() { return chosen; }

void setScheme(Scheme s) { chosen = s; }

const char* schemeName(Scheme s) {
    switch (s) {
    case Scheme::Plaster:
        return "Plaster";
    case Scheme::Sand:
        return "Sand";
    case Scheme::Slate:
        return "Slate";
    case Scheme::Gruvbox:
        return "Gruvbox";
    case Scheme::Nord:
        return "Nord";
    case Scheme::Catppuccin:
        return "Catppuccin";
    case Scheme::OneDark:
        return "One Dark";
    case Scheme::Monokai:
        return "Monokai";
    case Scheme::FollowRack:
    default:
        return "Follow Rack (Plaster / Slate)";
    }
}

const char* schemeId(Scheme s) {
    switch (s) {
    case Scheme::Plaster:
        return "plaster";
    case Scheme::Sand:
        return "sand";
    case Scheme::Slate:
        return "slate";
    case Scheme::Gruvbox:
        return "gruvbox";
    case Scheme::Nord:
        return "nord";
    case Scheme::Catppuccin:
        return "catppuccin";
    case Scheme::OneDark:
        return "one-dark";
    case Scheme::Monokai:
        return "monokai";
    case Scheme::FollowRack:
    default:
        return "follow-rack";
    }
}

Scheme schemeFromId(const std::string& id) {
    for (Scheme s : kSchemes)
        if (id == schemeId(s))
            return s;
    return Scheme::FollowRack;
}

const KeyColors& keyColors() {
    static const KeyColors dark = {rgb(0xD3D6DB), rgb(0xB9BDC4), rgb(0x8D929B), rgb(0x25272C),
                                   rgb(0x141519), rgb(0x4E535D), rgb(0x2A2E35), rgb(0x7A808A)};
    static const KeyColors light = {rgb(0xFFFFFF), rgb(0xECE8E1), rgb(0xB8B2A8), rgb(0x3A3E46),
                                    rgb(0x1F2227), rgb(0x6A707B), rgb(0x2A2E35), rgb(0x7A808A)};
    return colors().dark ? dark : light;
}

void appendSchemeMenu(rack::ui::Menu* menu) {
    for (Scheme s : kSchemes)
        menu->addChild(rack::createCheckMenuItem(schemeName(s), "", [s] { return chosen == s; }, [s] { chosen = s; }));
}

const Palette& colors() { return paletteOf(chosen); }

NVGcolor recessed(const Palette& p, NVGcolor base, float strength) {
    return nvgLerpRGBA(base, nvgRGBf(0, 0, 0), p.dark ? strength : strength * 0.25f);
}

const Palette& panelColors() { return paletteOf(chosen); }

const Palette& screenColors() {
    static const Palette p = [] {
        Palette s = slate();
        s.bg0 = rgb(0x191C21);
        s.bg1 = rgb(0x23262B);
        return s;
    }();
    return p;
}

uint64_t generation() { return uint64_t(resolved(chosen)) * 2 + (rack::settings::preferDarkPanels ? 1 : 0); }

NVGcolor track(uint32_t color, float alpha) {
    if (colors().dark)
        return rgb(color, alpha);
    for (int i = 0; i < 16; i++)
        if (kTrackColors[i] == color)
            return rgb(kTrackColorsLight[i], alpha);
    NVGcolor c = rgb(color, alpha);
    return nvgRGBAf(c.r * kLightDeepen, c.g * kLightDeepen, c.b * kLightDeepen, alpha);
}

namespace fonts {

// Rack caches fonts per window, so loading on every draw is cheap and survives window re-creation.
std::shared_ptr<rack::window::Font> ui() {
    return APP->window->loadFont(rack::asset::plugin(pluginInstance, "res/fonts/Inter-Regular.ttf"));
}

std::shared_ptr<rack::window::Font> uiStrong() {
    return APP->window->loadFont(rack::asset::plugin(pluginInstance, "res/fonts/Inter-SemiBold.ttf"));
}

std::shared_ptr<rack::window::Font> mono() {
    return APP->window->loadFont(rack::asset::plugin(pluginInstance, "res/fonts/JetBrainsMono-Regular.ttf"));
}

} // namespace fonts

NVGcolor modulatorColor(uint64_t modulatorId) {
    // Neighbours differ strongly: modulators added one after another usually have neighbouring IDs.
    static const uint32_t kColors[] = {0x4BC7C2, 0xFF7AB6, 0xE8CC5A, 0x8C8CFF, 0x6BD68A, 0xC99BFF, 0x56C2E8, 0xFF9E9E};
    return rgb(kColors[modulatorId % 8]);
}

} // namespace tessera::theme
