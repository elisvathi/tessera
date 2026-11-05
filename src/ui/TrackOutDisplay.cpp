#include "TrackOutDisplay.hpp"

#include "../model/TrackColors.hpp"
#include "../module/TesseraModule.hpp"
#include "OutputMenus.hpp"
#include "Theme.hpp"
#include "views/AudioClipDraw.hpp"
#include "widgets/Draw.hpp"

namespace tessera {

namespace {

constexpr float kPad = 1.5f * theme::metrics::kSpacing;
constexpr float kSourceBaseline = 13.5f;

// The number and the name follow the display's height (20 mm on Drum Out, 18 mm on Track Out); the name stays
// ~7 px off the bottom edge.
float numberBaseline(float height) { return height * 0.65f; }
float nameBaseline(float height) { return height - 7; }
float numberSize(float height) { return std::min(28.f, height * 0.4f); }
// With a channel line under the name (Track Out, Drum Out), the name moves up by this much and the number shrinks to
// fit between the source line and the name.
constexpr float kChannelLine = 9;
constexpr float kChannelBottom = 5; // the channel line's baseline off the bottom edge

} // namespace

void TrackOutDisplay::gather() {
    if (!module) {
        status = BindingStatus::Bound;
        sourceName = "Tessera";
        trackName = "Track 1";
        trackNumber = "1";
        trackColor = trackColorForIndex(0);
        return;
    }
    status = module->status();
    part = 0;
    partPlayers.clear();
    TesseraModule* src = module->source();
    sourceName = src ? src->instanceName() : module->binding().sourceName;
    if (module->binding().followSelected)
        sourceName += " · follows editor";
    // Its Level and Use as a source of the track, when they change the sound (§7.8). The screen is narrow, so they
    // take the place of the Tessera's name.
    SourceLevel level = module->sourceSetting();
    if (!level.use)
        sourceName = "IN OFF";
    else if (level.db != 0)
        sourceName = "IN " + audioDraw::dbText(level.db);
    if (status != BindingStatus::Bound)
        return;
    const Song& song = src->song();
    if (module->boundKind() == BoundKind::Master) {
        trackName = "Master";
        trackNumber = "M";
        trackColor = 0xC9CED6;
        return;
    }
    int index = song.trackIndex(module->binding().track);
    const Track& track = song.tracks[index];
    trackName = track.name;
    trackNumber = std::to_string(index + 1);
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
        TesseraModule* src = module ? module->source() : nullptr;
        if (src && src->isBypassed()) {
            drawMessage(args.vg, "Tessera bypassed", c.queue, "Turn it on to play");
        } else {
            switch (status) {
            case BindingStatus::Bound:
                drawBound(args.vg);
                break;
            case BindingStatus::TrackMissing:
                drawMessage(args.vg, "Track deleted", c.queue, "Undo, or pick a track in the menu");
                break;
            case BindingStatus::NoSource:
                drawMessage(args.vg, "No source", c.text1, "Right-click to choose");
                break;
            }
        }
        // Show in the rack: a ring that pulses so the eye finds the module.
        if (float flash = module ? module->flashAmount() : 0.f; flash > 0) {
            NVGcolor ring = c.accent;
            ring.a *= flash;
            draw::roundedRectStroke(args.vg, rack::math::Rect(rack::math::Vec(1, 1), box.size.minus(rack::math::Vec(2, 2))),
                                    theme::metrics::kPanelDisplayRadius, ring, 2.f);
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

    float nameY = nameBaseline(box.size.y) - (channels ? kChannelLine : 0);
    float numberY = channels ? nameY - 10 : numberBaseline(box.size.y);
    // Digits are about 0.72 of their size tall: as big as fits under the source line.
    float size = channels ? std::clamp((numberY - kSourceBaseline - 2) / 0.72f, 10.f, numberSize(box.size.y))
                          : numberSize(box.size.y);
    draw::TextStyle number{fonts::mono(), size, color, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
    draw::text(vg, number, cx, numberY, trackNumber);

    draw::TextStyle name{fonts::uiStrong(), metrics::kDisplaySmall, c.text0, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
    draw::text(vg, name, cx, nameY, draw::ellipsize(vg, name, trackName, maxWidth));

    if (channels) {
        // Always shown, centred under the name, so it reads as something to click.
        std::string label = part == 0 ? "All channels"
                                      : "Ch " + std::to_string(part) + (partPlayers.empty() ? "" : " · " + partPlayers);
        draw::TextStyle ch{fonts::ui(), metrics::kDisplaySmall, part == 0 ? c.text2 : c.text1,
                           NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
        draw::text(vg, ch, cx, box.size.y - kChannelBottom, draw::ellipsize(vg, ch, label, maxWidth));
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
    draw::text(vg, msg, cx, numberBaseline(box.size.y) - 12, text.substr(0, space));
    if (space != std::string::npos)
        draw::text(vg, msg, cx, numberBaseline(box.size.y) + 1, text.substr(space + 1));

    draw::TextStyle hintStyle{fonts::ui(), metrics::kPanelLabel, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_TOP};
    draw::textBox(vg, hintStyle, kPad, nameBaseline(box.size.y) - 6, maxWidth, hint);
}

} // namespace tessera
