#include "TrackOutDisplay.hpp"

#include "../model/TrackColors.hpp"
#include "../module/TesseraModule.hpp"
#include "OutputMenus.hpp"
#include "Theme.hpp"
#include "widgets/Draw.hpp"

namespace tessera {

namespace {

constexpr float kPad = 1.5f * theme::metrics::kSpacing;
// The display is 24 mm (~71 px) tall; keep the track name ~5 px off the bottom edge.
constexpr float kSourceBaseline = 13.5f;
constexpr float kNumberBaseline = 46;
constexpr float kNameBaseline = 64;
constexpr float kNumberSize = 28;
// A short screen (Drum Out's, 24 mm) with a channel line too: everything moves up a little, so the channel sits
// centred under the name as on Track Out.
constexpr float kShortNumberBaseline = 36, kShortNumberSize = 22, kShortNameBaseline = 51, kShortChannelBaseline = 64;

} // namespace

void TrackOutDisplay::gather() {
    if (!module) {
        status = BindingStatus::Bound;
        sourceName = "Roll";
        trackName = "Track 1";
        trackNumber = 1;
        trackColor = trackColorForIndex(0);
        return;
    }
    status = module->status();
    TesseraModule* src = module->source();
    sourceName = src ? src->instanceName() : module->binding().sourceName;
    if (module->binding().followSelected)
        sourceName += " · follows editor";
    if (status != BindingStatus::Bound)
        return;
    const Song& song = src->song();
    int index = song.trackIndex(module->binding().track);
    const Track& track = song.tracks[index];
    trackName = track.name;
    trackNumber = index + 1;
    trackColor = track.color;
    part = channels ? module->part() : 0;
    partPlayers = part > 0 ? channelPlayers(module, part) : "";
}

void TrackOutDisplay::onButton(const ButtonEvent& e) {
    if (!channels || !module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) {
        Widget::onButton(e);
        return;
    }
    e.consume(this);
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Channel"));
    appendChannelMenu(menu, module);
}

void TrackOutDisplay::draw(const DrawArgs& args) {
    const theme::Palette& c = theme::screenColors();
    rack::math::Rect r(rack::math::Vec(), box.size);
    draw::roundedRect(args.vg, r, theme::metrics::kPanelDisplayRadius, c.bg0);
    draw::roundedRectStroke(args.vg, r, theme::metrics::kPanelDisplayRadius, c.line0, 1.f);
}

void TrackOutDisplay::drawLayer(const DrawArgs& args, int layer) {
    if (layer == 1) {
        gather();
        const theme::Palette& c = theme::screenColors();
        switch (status) {
        case BindingStatus::Bound:
            drawBound(args.vg);
            break;
        case BindingStatus::TrackMissing:
            drawMessage(args.vg, "Track missing", c.record, "Pick a track in the menu");
            break;
        case BindingStatus::NoSource:
            drawMessage(args.vg, "No source", c.text1, "Right-click to choose");
            break;
        }
    }
    Widget::drawLayer(args, layer);
}

void TrackOutDisplay::drawBound(NVGcontext* vg) {
    using namespace theme;
    const Palette& c = screenColors();
    const float cx = box.size.x / 2;
    const float maxWidth = box.size.x - 2 * kPad;
    NVGcolor color = rgb(trackColor);

    // A strip in the track color along the top edge, inside the display's rounded corners.
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, metrics::kStatusBar);
    draw::roundedRect(vg, rack::math::Rect(rack::math::Vec(), box.size), metrics::kPanelDisplayRadius, color);
    nvgRestore(vg);

    draw::TextStyle source{fonts::ui(), metrics::kDisplaySmall, c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
    draw::text(vg, source, cx, kSourceBaseline, draw::ellipsize(vg, source, sourceName, maxWidth));

    bool tall = box.size.y >= kNameBaseline + 16;
    bool compact = channels && !tall;
    draw::TextStyle number{fonts::mono(), compact ? kShortNumberSize : kNumberSize, color,
                           NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
    draw::text(vg, number, cx, compact ? kShortNumberBaseline : kNumberBaseline, std::to_string(trackNumber));

    draw::TextStyle name{fonts::uiStrong(), metrics::kDisplaySmall, c.text0, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
    float nameBaseline = compact ? kShortNameBaseline : kNameBaseline;
    draw::text(vg, name, cx, nameBaseline, draw::ellipsize(vg, name, trackName, maxWidth));

    if (channels) {
        // Always shown, centred under the name, so it reads as something to click.
        std::string label = part == 0 ? "All channels"
                                      : "Ch " + std::to_string(part) + (partPlayers.empty() ? "" : " · " + partPlayers);
        draw::TextStyle ch{fonts::ui(), metrics::kDisplaySmall, part == 0 ? c.text2 : c.text1,
                           NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
        draw::text(vg, ch, cx, tall ? kNameBaseline + 13 : kShortChannelBaseline,
                   draw::ellipsize(vg, ch, label, maxWidth));
    }
}

void TrackOutDisplay::drawMessage(NVGcontext* vg, const char* message, NVGcolor color, const char* hint) {
    using namespace theme;
    const Palette& c = screenColors();
    const float cx = box.size.x / 2;
    const float maxWidth = box.size.x - 2 * kPad;

    if (!sourceName.empty()) {
        draw::TextStyle source{fonts::ui(), metrics::kDisplaySmall, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
        draw::text(vg, source, cx, kSourceBaseline, draw::ellipsize(vg, source, sourceName, maxWidth));
    }

    // Narrow display: break the message at its space so each word gets a line.
    draw::TextStyle msg{fonts::uiStrong(), metrics::kTextUi, color, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
    std::string text = message;
    size_t space = text.find(' ');
    draw::text(vg, msg, cx, kNumberBaseline - 12, text.substr(0, space));
    if (space != std::string::npos)
        draw::text(vg, msg, cx, kNumberBaseline + 1, text.substr(space + 1));

    draw::TextStyle hintStyle{fonts::ui(), metrics::kPanelLabel, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_TOP};
    draw::textBox(vg, hintStyle, kPad, kNameBaseline - 6, maxWidth, hint);
}

} // namespace tessera
