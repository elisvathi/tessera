#include "OutputMenus.hpp"

#include "../module/Registry.hpp"
#include "../module/TesseraModule.hpp"
#include "ModuleUndo.hpp"
#include "Theme.hpp"

#include <algorithm>

namespace tessera {

namespace {

/// A menu entry for a track: "3  Bass" with a dot in the track's color, checked when bound.
class TrackMenuItem : public rack::ui::MenuItem {
  public:
    TrackMenuItem(int number, const std::string& name, uint32_t color, bool checked, std::function<void()> action)
        : color(color), onPick(std::move(action)) {
        // Leading spaces leave room for the color dot.
        text = "     " + std::to_string(number) + "  " + name;
        rightText = CHECKMARK(checked);
    }

    void draw(const DrawArgs& args) override {
        MenuItem::draw(args);
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, 13, box.size.y / 2, 4);
        nvgFillColor(args.vg, theme::rgb(color));
        nvgFill(args.vg);
    }

    void onAction(const ActionEvent& e) override {
        if (onPick)
            onPick();
    }

  private:
    uint32_t color;
    std::function<void()> onPick;
};

} // namespace

void appendSourceMenu(rack::ui::Menu* menu, OutputModuleBase* m, const std::string& what) {
    std::vector<TesseraModule*> sources = Registry::get().sources();
    if (sources.empty()) {
        menu->addChild(rack::createMenuLabel("No Tessera in this patch"));
        return;
    }
    for (TesseraModule* src : sources) {
        int64_t id = src->id;
        menu->addChild(rack::createCheckMenuItem(
            src->instanceName(), "", [m, id] { return m->binding().sourceId == id; },
            [m, id, what] {
                // Look the source up again: the menu may outlive the module it listed.
                TesseraModule* s = Registry::get().findSource(id);
                if (!s)
                    return;
                std::vector<TrackId> tracks = m->candidateTracks(*s);
                if (tracks.empty())
                    return;
                auto keep = std::find(tracks.begin(), tracks.end(), m->binding().track);
                TrackId track = keep != tracks.end() ? *keep : tracks.front();
                withModuleUndo(m, "change " + what + " source", [&] { m->bind(*s, track); });
            }));
    }
}

void appendTrackMenu(rack::ui::Menu* menu, OutputModuleBase* m, const std::string& what) {
    TesseraModule* src = m->source();
    if (!src)
        return;
    const Song& song = src->song();
    int64_t sourceId = src->id;
    bool following = m->binding().followSelected;
    menu->addChild(rack::createCheckMenuItem(
        "Follow the track selected in the editor", "", [following] { return following; },
        [m, following] {
            withModuleUndo(m, (following ? "stop following" : "follow") + std::string(" the selected track"),
                           [&] { m->setFollowSelected(!following); });
        }));
    menu->addChild(new rack::ui::MenuSeparator);
    for (size_t i = 0; i < song.tracks.size(); i++) {
        const Track& t = song.tracks[i];
        if (!m->accepts(t.type))
            continue;
        TrackId id = t.id;
        bool checked = m->binding().track == id;
        menu->addChild(new TrackMenuItem(int(i) + 1, t.name, t.color, checked, [m, sourceId, id, what] {
            TesseraModule* s = Registry::get().findSource(sourceId);
            if (s)
                withModuleUndo(m, "change " + what + " track", [&] {
                    m->setFollowSelected(false);
                    m->bind(*s, id);
                });
        }));
    }
}

} // namespace tessera
