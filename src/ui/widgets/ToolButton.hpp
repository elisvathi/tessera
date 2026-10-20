#pragma once

#include "../Theme.hpp"
#include "HoverTooltip.hpp"

#include <rack.hpp>

#include <functional>
#include <string>

namespace tessera {

enum class Icon {
    None,
    Play,
    Stop,
    Record,
    Loop,
    Follow,
    Inspector,
    Close,
    Undo,
    Redo,
    // Note tools
    Pointer,
    Pencil,
    Brush,
    Eraser,
    Scissors,
    MuteNote,
    Velocity,
    Speaker,
    Badges,
    More,
    Ghost,
    // Scale tools
    Chord,
    ScaleHighlight,
    ScaleSnap,
    Fold,
    // Arranger edit modes
    Range,
    Ripple,
    AutoFollow,
    Stack,
    LauncherPanel,
    // Panes and the launcher
    FullEditor,
    Maximize,
    Restore,
    Capture
};

/// A flat editor button: an icon or a short text, with hover and pressed states, an optional "on" state and
/// a tooltip. Actions run on release, on the UI thread.
class ToolButton : public rack::widget::OpaqueWidget {
  public:
    struct Options {
        Icon icon = Icon::None;
        std::string text;
        std::string tooltip; // include the shortcut, e.g. "Loop (L)"
        std::function<void()> action;
        std::function<void()> rightClick; // e.g. a settings menu
        std::function<bool()> active;     // drawn highlighted while true
        std::function<bool()> enabled;    // greyed out and inert while false
        NVGcolor theme::Palette::* activeColor = &theme::Palette::accent;
        bool filled = false;               // a visible background at rest
        bool onPanel = false;              // drawn on a module panel, in the panel's colours
        bool caret = false;                // a small chevron after the text: the button opens a menu
        std::function<Icon()> dynamicIcon; // when set, the icon drawn now (Icon::None: just the caret)
        std::function<NVGcolor()> swatch;  // a colour square before the text (a clip chip's track colour)
        bool caretBelow = false;           // the chevron sits under the text: for narrow columns
        bool leftAligned = false;          // caret buttons: text from the left edge rather than centred
    };

    explicit ToolButton(Options options);

    void setText(const std::string& text) { opts.text = text; }
    const std::string& getText() const { return opts.text; }
    void setIcon(Icon icon) { opts.icon = icon; }
    /// True when a caret button currently shows an icon (its dynamic icon is set).
    bool isShowingIcon() const { return opts.dynamicIcon ? opts.dynamicIcon() != Icon::None : opts.icon != Icon::None; }

    void draw(const DrawArgs& args) override;
    void step() override;
    void onEnter(const EnterEvent& e) override;
    void onLeave(const LeaveEvent& e) override;
    void onButton(const ButtonEvent& e) override;
    void onDragEnd(const DragEndEvent& e) override;

  private:
    bool isEnabled() const { return !opts.enabled || opts.enabled(); }
    bool isActive() const { return opts.active && opts.active(); }
    /// Draws the icon centred in a square of side `size` at `center`.
    void drawIcon(NVGcontext* vg, NVGcolor color, rack::math::Vec center, float size);

    Options opts;
    HoverTooltip tip;
    bool hovered = false;
    bool pressed = false;
};

} // namespace tessera
