#pragma once

#include <rack.hpp>

#include <functional>
#include <string>
#include <vector>

namespace tessera {

/// One row of a SettingsPanel: a number (drag or wheel), a choice (click cycles), an on/off switch, or a section
/// heading.
struct Setting {
    enum class Kind { Number, Choice, Toggle, Heading };
    std::string name;
    Kind kind = Kind::Number;
    double min = 0, max = 1, step = 1;
    std::vector<std::string> choices;
    double value = 0;
    std::function<std::string(double)> format;
    bool remember = true; // false: the default depends on the selection, so it is worked out each time

    std::string text() const;
};

namespace settings {

Setting number(std::string name, double min, double max, double step, double value,
               std::function<std::string(double)> format = nullptr);
Setting choice(std::string name, std::vector<std::string> choices, int value);
Setting toggle(std::string name, bool value);
Setting heading(std::string name);
std::string printf1(const char* format, double v);

} // namespace settings

/// A small settings popover shown in a menu (transforms, MIDI export): rows of settings and Apply / Cancel.
/// `changed` runs at once and after every change (for live previews); Enter or Apply runs `apply`; Esc, Cancel or
/// closing the menu any other way runs `cancel`.
class SettingsPanel : public rack::widget::OpaqueWidget {
  public:
    struct Callbacks {
        std::function<void(const std::vector<double>&)> changed;
        std::function<void(const std::vector<double>&)> apply;
        std::function<void()> cancel;
    };

    SettingsPanel(std::string title, std::vector<Setting> rows, std::string applyLabel, Callbacks callbacks);
    ~SettingsPanel() override;

    /// Opens the panel in a new menu at the mouse.
    static SettingsPanel* open(std::string title, std::vector<Setting> rows, std::string applyLabel,
                               Callbacks callbacks);

    std::vector<double> values() const;

    void step() override;
    void draw(const DrawArgs& args) override;
    void onHover(const HoverEvent& e) override;
    void onLeave(const LeaveEvent& e) override { hovered = -1; }
    void onButton(const ButtonEvent& e) override;
    void onDragStart(const DragStartEvent& e) override { e.consume(this); }
    void onDragMove(const DragMoveEvent& e) override;
    void onDragEnd(const DragEndEvent& e) override { dragging = -1; }
    void onHoverScroll(const HoverScrollEvent& e) override;
    void onSelectKey(const SelectKeyEvent& e) override;

  private:
    static constexpr int kApply = 1000, kCancel = 1001;

    rack::math::Rect rowRect(int i) const;
    rack::math::Rect applyRect() const;
    rack::math::Rect cancelRect() const;
    int hit(rack::math::Vec pos) const;
    void setNumber(Setting& s, double v);
    void changed();
    void apply();
    void close();

    std::string title, applyLabel;
    std::vector<Setting> rows;
    Callbacks callbacks;
    bool done = false, focused = false;
    int hovered = -1, dragging = -1;
    double dragStart = 0;
    float dragPx = 0;
};

} // namespace tessera
