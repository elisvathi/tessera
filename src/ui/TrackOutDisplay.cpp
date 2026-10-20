#include "TrackOutDisplay.hpp"

#include "../model/TrackColors.hpp"
#include "../module/TesseraModule.hpp"
#include "Theme.hpp"
#include "widgets/Draw.hpp"

namespace tessera {

namespace {

constexpr float kPad = 1.5f * theme::metrics::kSpacing;
constexpr float kSourceBaseline = 15;
constexpr float kNumberBaseline = 50;
constexpr float kNameBaseline = 68;
constexpr float kNumberSize = 28;

} // namespace

void TrackOutDisplay::gather() {
    if (!module) {
        status = BindingStatus::Bound;
        sourceName = "Tessera";
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

    draw::TextStyle number{fonts::mono(), kNumberSize, color, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
    draw::text(vg, number, cx, kNumberBaseline, std::to_string(trackNumber));

    draw::TextStyle name{fonts::uiStrong(), metrics::kDisplaySmall, c.text0, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE};
    draw::text(vg, name, cx, kNameBaseline, draw::ellipsize(vg, name, trackName, maxWidth));
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
