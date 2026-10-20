#include "SettingsPanel.hpp"

#include "../Theme.hpp"
#include "Draw.hpp"

#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kWidth = 290;
constexpr float kRow = 22;
constexpr float kPad = 10;
constexpr float kButtonWidth = 76;
constexpr float kPxPerStep = 4; // dragging this far changes a number by one step

} // namespace

std::string Setting::text() const {
    switch (kind) {
    case Kind::Choice:
        return choices.empty() ? "" : choices[size_t(std::clamp<int>(int(value), 0, int(choices.size()) - 1))];
    case Kind::Toggle:
        return value != 0 ? "On" : "Off";
    case Kind::Heading:
        return "";
    default:
        return format ? format(value) : settings::printf1("%.0f", value);
    }
}

namespace settings {

std::string printf1(const char* format, double v) {
    char text[64];
    snprintf(text, sizeof(text), format, v);
    return text;
}

Setting number(std::string name, double min, double max, double step, double value,
               std::function<std::string(double)> format) {
    Setting s;
    s.name = std::move(name);
    s.min = min;
    s.max = max;
    s.step = step;
    s.value = value;
    s.format = std::move(format);
    return s;
}

Setting choice(std::string name, std::vector<std::string> choices, int value) {
    Setting s;
    s.name = std::move(name);
    s.kind = Setting::Kind::Choice;
    s.choices = std::move(choices);
    s.max = double(s.choices.size()) - 1;
    s.value = value;
    return s;
}

Setting toggle(std::string name, bool value) {
    Setting s;
    s.name = std::move(name);
    s.kind = Setting::Kind::Toggle;
    s.value = value;
    return s;
}

Setting heading(std::string name) {
    Setting s;
    s.name = std::move(name);
    s.kind = Setting::Kind::Heading;
    s.remember = false;
    return s;
}

} // namespace settings

SettingsPanel::SettingsPanel(std::string title, std::vector<Setting> rows, std::string applyLabel, Callbacks callbacks)
    : title(std::move(title)), applyLabel(std::move(applyLabel)), rows(std::move(rows)),
      callbacks(std::move(callbacks)) {
    box.size = Vec(kWidth, kRow * float(this->rows.size() + 2) + kPad);
    changed();
}

SettingsPanel::~SettingsPanel() {
    if (!done && callbacks.cancel)
        callbacks.cancel();
}

SettingsPanel* SettingsPanel::open(std::string title, std::vector<Setting> rows, std::string applyLabel,
                                   Callbacks callbacks) {
    rack::ui::Menu* menu = rack::createMenu();
    auto* panel = new SettingsPanel(std::move(title), std::move(rows), std::move(applyLabel), std::move(callbacks));
    menu->addChild(panel);
    return panel;
}

std::vector<double> SettingsPanel::values() const {
    std::vector<double> out;
    for (const Setting& s : rows)
        out.push_back(s.value);
    return out;
}

void SettingsPanel::step() {
    // Take the keyboard so Enter applies and Esc cancels.
    if (!focused) {
        APP->event->setSelectedWidget(this);
        focused = true;
    }
    OpaqueWidget::step();
}

Rect SettingsPanel::rowRect(int i) const { return Rect(0, kRow * float(i + 1) + 2, box.size.x, kRow); }

Rect SettingsPanel::applyRect() const {
    return Rect(box.size.x - kPad - kButtonWidth, box.size.y - kRow - 4, kButtonWidth, kRow - 4);
}

Rect SettingsPanel::cancelRect() const {
    return Rect(box.size.x - 2 * (kPad + kButtonWidth) + kPad / 2, box.size.y - kRow - 4, kButtonWidth, kRow - 4);
}

int SettingsPanel::hit(Vec pos) const {
    if (applyRect().contains(pos))
        return kApply;
    if (cancelRect().contains(pos))
        return kCancel;
    for (size_t i = 0; i < rows.size(); i++)
        if (rows[i].kind != Setting::Kind::Heading && rowRect(int(i)).contains(pos))
            return int(i);
    return -1;
}

void SettingsPanel::draw(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    draw::TextStyle titleStyle{theme::fonts::uiStrong(), theme::metrics::kTextHeader, c.text0,
                               NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle label{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle heading{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text2,
                            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme::metrics::kPanelLabelSpacing};
    draw::TextStyle value{theme::fonts::mono(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    draw::TextStyle button{theme::fonts::uiStrong(), theme::metrics::kTextUi, c.text0,
                           NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::text(vg, titleStyle, kPad, kRow / 2 + 2, draw::ellipsize(vg, titleStyle, title, box.size.x - 2 * kPad));
    for (size_t i = 0; i < rows.size(); i++) {
        const Setting& s = rows[i];
        Rect r = rowRect(int(i));
        if (s.kind == Setting::Kind::Heading) {
            draw::text(vg, heading, kPad, r.getCenter().y + 2, s.name);
            continue;
        }
        if (int(i) == hovered || int(i) == dragging)
            draw::roundedRect(vg, Rect(4, r.pos.y, box.size.x - 8, r.size.y), 3, c.bg2);
        draw::text(vg, label, kPad, r.getCenter().y, s.name);
        draw::text(vg, value, box.size.x - kPad, r.getCenter().y, s.text());
        if (s.kind == Setting::Kind::Number && s.max > s.min) {
            // How far along its range the value is.
            float x0 = box.size.x * 0.5f, x1 = box.size.x - kPad;
            float f = float((s.value - s.min) / (s.max - s.min));
            nvgBeginPath(vg);
            nvgRect(vg, x0, r.getBottom() - 3, x1 - x0, 1);
            nvgFillColor(vg, c.line1);
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRect(vg, x0, r.getBottom() - 3, (x1 - x0) * std::clamp(f, 0.f, 1.f), 1);
            nvgFillColor(vg, c.accent);
            nvgFill(vg);
        }
    }
    Rect a = applyRect(), x = cancelRect();
    draw::roundedRect(vg, a, theme::metrics::kRadiusControl,
                      hovered == kApply ? nvgLerpRGBA(c.accent, c.text0, 0.15f) : c.accent);
    button.color = c.ink;
    draw::text(vg, button, a.getCenter().x, a.getCenter().y, applyLabel);
    draw::roundedRect(vg, x, theme::metrics::kRadiusControl, hovered == kCancel ? c.line0 : c.bg2);
    button.color = c.text0;
    draw::text(vg, button, x.getCenter().x, x.getCenter().y, "Cancel");
}

void SettingsPanel::onHover(const HoverEvent& e) {
    hovered = hit(e.pos);
    OpaqueWidget::onHover(e);
}

void SettingsPanel::onButton(const ButtonEvent& e) {
    if (e.action != GLFW_PRESS)
        return;
    e.consume(this);
    int h = hit(e.pos);
    if (h == kApply && e.button == GLFW_MOUSE_BUTTON_LEFT) {
        apply();
        return;
    }
    if (h == kCancel && e.button == GLFW_MOUSE_BUTTON_LEFT) {
        close();
        return;
    }
    if (h < 0)
        return;
    Setting& s = rows[size_t(h)];
    if (s.kind == Setting::Kind::Number) {
        dragging = h;
        dragStart = s.value;
        dragPx = 0;
        return;
    }
    // Choices step forward on a left click, back on a right click.
    if (s.kind == Setting::Kind::Toggle) {
        s.value = s.value != 0 ? 0 : 1;
    } else {
        double next = s.value + (e.button == GLFW_MOUSE_BUTTON_RIGHT ? -1 : 1);
        s.value = next > s.max ? s.min : next < s.min ? s.max : next;
    }
    changed();
}

void SettingsPanel::onDragMove(const DragMoveEvent& e) {
    if (dragging < 0)
        return;
    Setting& s = rows[size_t(dragging)];
    bool fine = APP->window->getMods() & GLFW_MOD_SHIFT;
    dragPx += (e.mouseDelta.x - e.mouseDelta.y) * (fine ? 0.1f : 1.f);
    setNumber(s, dragStart + std::round(dragPx / kPxPerStep) * s.step);
}

void SettingsPanel::onHoverScroll(const HoverScrollEvent& e) {
    int h = hit(e.pos);
    e.consume(this);
    if (h < 0 || h >= int(rows.size()))
        return;
    Setting& s = rows[size_t(h)];
    int dir = e.scrollDelta.y > 0 ? 1 : -1;
    if (s.kind == Setting::Kind::Number) {
        setNumber(s, s.value + dir * s.step);
    } else if (s.kind == Setting::Kind::Choice) {
        s.value = std::clamp(s.value + dir, s.min, s.max);
        changed();
    }
}

void SettingsPanel::onSelectKey(const SelectKeyEvent& e) {
    if (e.action != GLFW_PRESS)
        return;
    if (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER) {
        apply();
        e.consume(this);
    } else if (e.key == GLFW_KEY_ESCAPE) {
        close();
        e.consume(this);
    }
}

void SettingsPanel::setNumber(Setting& s, double v) {
    v = std::clamp(v, s.min, s.max);
    if (v != s.value) {
        s.value = v;
        changed();
    }
}

void SettingsPanel::changed() {
    if (callbacks.changed)
        callbacks.changed(values());
}

void SettingsPanel::apply() {
    done = true;
    // Close first: apply may open a file dialog, and the menu shouldn't sit over it.
    std::vector<double> v = values();
    auto run = callbacks.apply;
    close();
    if (run)
        run(v);
}

void SettingsPanel::close() {
    if (auto* overlay = getAncestorOfType<rack::ui::MenuOverlay>())
        overlay->requestDelete();
}

} // namespace tessera
