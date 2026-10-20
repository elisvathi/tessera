#include "LaneTabs.hpp"

#include "../Theme.hpp"
#include "../widgets/Draw.hpp"

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kTabPad = 8;

struct Tab {
    const char* label;
    NoteLane lane;
};
const Tab kTabs[] = {
    {"Velocity", NoteLane::Velocity}, {"Release", NoteLane::Release},   {"Chance", NoteLane::Chance},
    {"Repeats", NoteLane::Repeats},   {"Micro", NoteLane::Micro},       {"Pitch", NoteLane::Pitch},
    {"Timbre", NoteLane::Timbre},     {"Pressure", NoteLane::Pressure},
};
constexpr int kTabCount = int(sizeof(kTabs) / sizeof(kTabs[0]));

draw::TextStyle tabStyle(NVGcolor color) {
    return {theme::fonts::ui(), theme::metrics::kTextRuler, color, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
}

constexpr float kArrow = 8; // room for the menu arrow after the automation tab's name

std::string automationLabel(const std::string& name) { return name.empty() ? std::string("Automation") : name; }

} // namespace

std::vector<Rect> LaneTabs::layout(NVGcontext* vg) {
    std::vector<Rect> out;
    float x = 0;
    draw::TextStyle style = tabStyle(theme::colors().text1);
    for (int i = 0; i <= kTabCount; i++) {
        std::string label = i < kTabCount ? kTabs[i].label : automationLabel(automationName);
        float w = draw::textWidth(vg, style, label) + 2 * kTabPad + (i == kTabCount ? kArrow : 0);
        out.push_back(Rect(x, 0, w, box.size.y));
        x += w;
    }
    return out;
}

int LaneTabs::tabAt(Vec pos) {
    for (size_t i = 0; i < rects.size(); i++)
        if (rects[i].contains(pos) && rects[i].getRight() <= box.size.x)
            return int(i);
    return -1;
}

void LaneTabs::draw(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, 1);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);
    if (!ctx.module())
        return;
    rects = layout(vg);
    const ViewState& view = ctx.view();
    for (int i = 0; i <= kTabCount; i++) {
        const Rect& r = rects[size_t(i)];
        if (r.getRight() > box.size.x)
            break; // no room: the lane menu on the left still offers every lane
        bool active = i < kTabCount ? view.lowerLane == 0 && view.noteLane == kTabs[i].lane : view.lowerLane != 0;
        if (i == hovered && !active)
            draw::roundedRect(vg, Rect(r.pos.x + 1, 2, r.size.x - 2, r.size.y - 3), 3, c.bg2);
        if (active) {
            nvgBeginPath(vg);
            nvgRect(vg, r.pos.x + 4, r.getBottom() - 2, r.size.x - 8, 2);
            nvgFillColor(vg, c.accent);
            nvgFill(vg);
        }
        std::string label = i < kTabCount ? kTabs[i].label : automationLabel(automationName);
        float cx = r.getCenter().x - (i == kTabCount ? kArrow / 2 : 0);
        draw::text(vg, tabStyle(active ? c.text0 : c.text1), cx, r.getCenter().y, label);
        if (i == kTabCount) {
            // A small down arrow: this tab opens a menu.
            float ax = r.getRight() - kTabPad - 3, ay = r.getCenter().y;
            nvgBeginPath(vg);
            nvgMoveTo(vg, ax - 3, ay - 1.5f);
            nvgLineTo(vg, ax + 3, ay - 1.5f);
            nvgLineTo(vg, ax, ay + 2);
            nvgClosePath(vg);
            nvgFillColor(vg, active ? c.text0 : c.text1);
            nvgFill(vg);
        }
    }
}

void LaneTabs::onButton(const ButtonEvent& e) {
    if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT || !ctx.module())
        return;
    e.consume(this);
    int i = tabAt(e.pos);
    if (i < 0)
        return;
    if (i == kTabCount) {
        openAutomationMenu();
        return;
    }
    ctx.view().lowerLane = 0;
    ctx.view().noteLane = kTabs[i].lane;
}

void LaneTabs::onHover(const HoverEvent& e) {
    hovered = tabAt(e.pos);
    OpaqueWidget::onHover(e);
}

} // namespace tessera
