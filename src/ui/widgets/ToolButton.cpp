#include "ToolButton.hpp"

#include "../Theme.hpp"
#include "Draw.hpp"

namespace tessera {

ToolButton::ToolButton(Options options) : opts(std::move(options)), tip(opts.tooltip) {}

void ToolButton::step() {
    tip.step();
    OpaqueWidget::step();
}

void ToolButton::onEnter(const EnterEvent& e) {
    hovered = true;
    tip.enter();
}

void ToolButton::onLeave(const LeaveEvent& e) {
    hovered = false;
    pressed = false;
    tip.leave();
}

void ToolButton::onButton(const ButtonEvent& e) {
    if (e.button == GLFW_MOUSE_BUTTON_RIGHT && opts.rightClick) {
        e.consume(this);
        if (e.action == GLFW_PRESS)
            opts.rightClick();
        return;
    }
    if (e.button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    e.consume(this);
    if (e.action == GLFW_PRESS && isEnabled())
        pressed = true;
}

void ToolButton::onDragEnd(const DragEndEvent& e) {
    bool fire = pressed && hovered && isEnabled();
    pressed = false;
    if (fire && opts.action)
        opts.action();
}

void ToolButton::draw(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    // Module panels follow Rack's panel setting rather than the editor's scheme.
    const theme::Palette& c = opts.onPanel ? theme::panelColors() : theme::colors();
    bool enabled = isEnabled();
    bool active = isActive();
    NVGcolor activeColor = c.*opts.activeColor;

    NVGcolor rest = opts.filled ? c.bg2 : nvgRGBAf(0, 0, 0, 0);
    NVGcolor fill = active ? nvgTransRGBAf(activeColor, 0.18f) : rest;
    if (enabled && pressed)
        fill = c.line1;
    else if (enabled && hovered)
        fill = active ? nvgTransRGBAf(activeColor, 0.28f) : opts.filled ? c.line0 : c.bg2;
    draw::roundedRect(vg, rack::math::Rect(rack::math::Vec(), box.size), theme::metrics::kRadiusControl, fill);

    NVGcolor fg = !enabled ? c.text2 : active ? activeColor : hovered ? c.text0 : c.text1;
    auto chevron = [&](float cx, float cy) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 3, cy - 1.5f);
        nvgLineTo(vg, cx, cy + 1.5f);
        nvgLineTo(vg, cx + 3, cy - 1.5f);
        nvgStrokeColor(vg, fg);
        nvgStrokeWidth(vg, 1.3f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    };
    if (opts.dynamicIcon)
        opts.icon = opts.dynamicIcon();
    if (opts.caret && opts.text.empty()) {
        // A menu button with no words: the chosen item's icon, if any, then the chevron.
        if (opts.icon != Icon::None) {
            drawIcon(vg, fg, rack::math::Vec(box.size.y / 2, box.size.y / 2), box.size.y);
            chevron(box.size.x - 5, box.size.y / 2);
        } else {
            chevron(box.size.x / 2, box.size.y / 2);
        }
    } else if (opts.icon != Icon::None && !opts.text.empty()) {
        // Icon on the left, text after it.
        float side = box.size.y;
        drawIcon(vg, fg, rack::math::Vec(side / 2 + 1, box.size.y / 2), side);
        draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kTextUi, fg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, style, side, box.size.y / 2, draw::ellipsize(vg, style, opts.text, box.size.x - side - 3));
    } else if (opts.icon != Icon::None) {
        drawIcon(vg, fg, box.size.div(2), std::min(box.size.x, box.size.y));
    } else if (opts.caret && opts.caretBelow) {
        draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kTextUi, fg,
                              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, style, box.size.x / 2, box.size.y / 2 - 4,
                   draw::ellipsize(vg, style, opts.text, box.size.x - 4));
        chevron(box.size.x / 2, box.size.y / 2 + 7);
    } else if (opts.caret) {
        draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kTextUi, fg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        float left = 6;
        if (opts.swatch) {
            draw::roundedRect(vg, rack::math::Rect(6, box.size.y / 2 - 5, 10, 10), 2, opts.swatch());
            left = 21;
        }
        std::string text = draw::ellipsize(vg, style, opts.text, box.size.x - left - 18);
        float x = opts.swatch || opts.leftAligned
                      ? left
                      : std::max(6.f, (box.size.x - draw::textWidth(vg, style, text) - 14) / 2);
        draw::text(vg, style, x, box.size.y / 2, text);
        chevron(x + draw::textWidth(vg, style, text) + 8, box.size.y / 2);
    } else {
        draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kTextUi, fg,
                              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, style, box.size.x / 2, box.size.y / 2, draw::ellipsize(vg, style, opts.text, box.size.x - 6));
    }
}

void ToolButton::drawIcon(NVGcontext* vg, NVGcolor color, rack::math::Vec center, float size) {
    float cx = center.x, cy = center.y;
    float s = size * 0.22f; // half-size of the glyph
    nvgStrokeColor(vg, color);
    nvgFillColor(vg, color);
    nvgStrokeWidth(vg, 1.5f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgBeginPath(vg);
    switch (opts.icon) {
    case Icon::Play:
        nvgMoveTo(vg, cx - s * 0.8f, cy - s);
        nvgLineTo(vg, cx + s, cy);
        nvgLineTo(vg, cx - s * 0.8f, cy + s);
        nvgClosePath(vg);
        nvgFill(vg);
        break;
    case Icon::Stop:
        nvgRect(vg, cx - s * 0.85f, cy - s * 0.85f, s * 1.7f, s * 1.7f);
        nvgFill(vg);
        break;
    case Icon::Record:
        nvgCircle(vg, cx, cy, s * 0.9f);
        nvgFill(vg);
        break;
    case Icon::Loop:
        // Two arrows chasing each other around a rounded rectangle.
        nvgRoundedRect(vg, cx - s * 1.1f, cy - s * 0.7f, s * 2.2f, s * 1.4f, s * 0.5f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx + s * 0.1f, cy - s * 1.05f);
        nvgLineTo(vg, cx + s * 0.45f, cy - s * 0.7f);
        nvgLineTo(vg, cx + s * 0.1f, cy - s * 0.35f);
        nvgStroke(vg);
        break;
    case Icon::Follow:
        // A playhead line with an arrow pushing right.
        nvgMoveTo(vg, cx - s * 0.6f, cy - s);
        nvgLineTo(vg, cx - s * 0.6f, cy + s);
        nvgMoveTo(vg, cx - s * 0.2f, cy);
        nvgLineTo(vg, cx + s, cy);
        nvgMoveTo(vg, cx + s * 0.5f, cy - s * 0.5f);
        nvgLineTo(vg, cx + s, cy);
        nvgLineTo(vg, cx + s * 0.5f, cy + s * 0.5f);
        nvgStroke(vg);
        break;
    case Icon::Inspector:
        nvgRect(vg, cx - s, cy - s * 0.8f, s * 2, s * 1.6f);
        nvgMoveTo(vg, cx + s * 0.3f, cy - s * 0.8f);
        nvgLineTo(vg, cx + s * 0.3f, cy + s * 0.8f);
        nvgStroke(vg);
        break;
    case Icon::Close:
        nvgMoveTo(vg, cx - s * 0.8f, cy - s * 0.8f);
        nvgLineTo(vg, cx + s * 0.8f, cy + s * 0.8f);
        nvgMoveTo(vg, cx + s * 0.8f, cy - s * 0.8f);
        nvgLineTo(vg, cx - s * 0.8f, cy + s * 0.8f);
        nvgStroke(vg);
        break;
    case Icon::Undo:
    case Icon::Redo: {
        float d = opts.icon == Icon::Undo ? -1.f : 1.f;
        nvgArc(vg, cx, cy + s * 0.3f, s * 0.8f, NVG_PI, 2 * NVG_PI, NVG_CW);
        nvgStroke(vg);
        nvgBeginPath(vg);
        float ax = cx + d * s * 0.8f;
        nvgMoveTo(vg, ax - s * 0.4f, cy);
        nvgLineTo(vg, ax, cy + s * 0.4f);
        nvgLineTo(vg, ax + s * 0.4f, cy);
        nvgStroke(vg);
        break;
    }
    case Icon::Pointer:
        nvgMoveTo(vg, cx - s * 0.6f, cy - s);
        nvgLineTo(vg, cx - s * 0.6f, cy + s * 0.7f);
        nvgLineTo(vg, cx - s * 0.15f, cy + s * 0.25f);
        nvgLineTo(vg, cx + s * 0.3f, cy + s);
        nvgLineTo(vg, cx + s * 0.5f, cy + s * 0.85f);
        nvgLineTo(vg, cx + s * 0.05f, cy + s * 0.1f);
        nvgLineTo(vg, cx + s * 0.6f, cy + s * 0.05f);
        nvgClosePath(vg);
        nvgFill(vg);
        break;
    case Icon::Pencil:
        nvgMoveTo(vg, cx - s, cy + s);
        nvgLineTo(vg, cx - s * 0.75f, cy + s * 0.25f);
        nvgLineTo(vg, cx + s * 0.55f, cy - s * 1.05f);
        nvgLineTo(vg, cx + s * 1.05f, cy - s * 0.55f);
        nvgLineTo(vg, cx - s * 0.25f, cy + s * 0.75f);
        nvgClosePath(vg);
        nvgStroke(vg);
        break;
    case Icon::Brush:
        // Three painted notes stepping up.
        for (int i = 0; i < 3; i++)
            nvgRect(vg, cx - s + i * s * 0.75f, cy + s * 0.6f - i * s * 0.6f, s * 0.6f, s * 0.4f);
        nvgFill(vg);
        break;
    case Icon::Eraser:
        nvgMoveTo(vg, cx - s * 0.2f, cy + s * 0.9f);
        nvgLineTo(vg, cx - s, cy + s * 0.1f);
        nvgLineTo(vg, cx + s * 0.1f, cy - s);
        nvgLineTo(vg, cx + s, cy - s * 0.1f);
        nvgClosePath(vg);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - s * 0.6f, cy - s * 0.3f);
        nvgLineTo(vg, cx + s * 0.2f, cy + s * 0.5f);
        nvgStroke(vg);
        break;
    case Icon::Scissors:
        nvgCircle(vg, cx - s * 0.5f, cy + s * 0.6f, s * 0.35f);
        nvgCircle(vg, cx + s * 0.5f, cy + s * 0.6f, s * 0.35f);
        nvgMoveTo(vg, cx - s * 0.3f, cy + s * 0.3f);
        nvgLineTo(vg, cx + s * 0.6f, cy - s);
        nvgMoveTo(vg, cx + s * 0.3f, cy + s * 0.3f);
        nvgLineTo(vg, cx - s * 0.6f, cy - s);
        nvgStroke(vg);
        break;
    case Icon::MuteNote:
        nvgRect(vg, cx - s, cy - s * 0.35f, s * 2, s * 0.7f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - s * 0.8f, cy + s);
        nvgLineTo(vg, cx + s * 0.8f, cy - s);
        nvgStroke(vg);
        break;
    case Icon::Velocity:
        for (int i = 0; i < 3; i++) {
            float x = cx - s * 0.7f + i * s * 0.7f;
            float top = cy + s - (0.8f + i * 0.5f) * s;
            nvgMoveTo(vg, x, cy + s);
            nvgLineTo(vg, x, top);
        }
        nvgStroke(vg);
        break;
    case Icon::Speaker:
        nvgMoveTo(vg, cx - s, cy - s * 0.35f);
        nvgLineTo(vg, cx - s * 0.45f, cy - s * 0.35f);
        nvgLineTo(vg, cx + s * 0.15f, cy - s);
        nvgLineTo(vg, cx + s * 0.15f, cy + s);
        nvgLineTo(vg, cx - s * 0.45f, cy + s * 0.35f);
        nvgLineTo(vg, cx - s, cy + s * 0.35f);
        nvgClosePath(vg);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgArc(vg, cx + s * 0.2f, cy, s * 0.7f, -0.8f, 0.8f, NVG_CW);
        nvgStroke(vg);
        break;
    case Icon::Ghost:
        // Two notes: one solid, one an outline behind it.
        nvgRect(vg, cx - s, cy + s * 0.15f, s * 1.3f, s * 0.6f);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, cx - s * 0.3f, cy - s * 0.75f, s * 1.3f, s * 0.6f);
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
        break;
    case Icon::More:
        for (int i = -1; i <= 1; i++)
            nvgCircle(vg, cx + i * s * 0.75f, cy, s * 0.22f);
        nvgFill(vg);
        break;
    case Icon::LauncherPanel:
        // A column of slots beside a timeline bar.
        for (int i = 0; i < 3; i++)
            nvgRect(vg, cx - s, cy - s + i * s * 0.75f, s * 0.6f, s * 0.5f);
        nvgRect(vg, cx - s * 0.1f, cy - s * 0.25f, s * 1.1f, s * 0.5f);
        nvgFill(vg);
        break;
    case Icon::FullEditor:
        // A window with an arrow leaving it: opens elsewhere, unlike maximize's corners.
        nvgRoundedRect(vg, cx - s, cy - s * 0.35f, s * 1.35f, s * 1.35f, 1.5f);
        nvgMoveTo(vg, cx - s * 0.1f, cy + s * 0.1f);
        nvgLineTo(vg, cx + s, cy - s);
        nvgMoveTo(vg, cx + s * 0.25f, cy - s);
        nvgLineTo(vg, cx + s, cy - s);
        nvgLineTo(vg, cx + s, cy - s * 0.25f);
        nvgStroke(vg);
        break;
    case Icon::Maximize:
        // Two corners pushed out.
        nvgMoveTo(vg, cx + s * 0.2f, cy - s);
        nvgLineTo(vg, cx + s, cy - s);
        nvgLineTo(vg, cx + s, cy - s * 0.2f);
        nvgMoveTo(vg, cx + s, cy - s);
        nvgLineTo(vg, cx + s * 0.25f, cy - s * 0.25f);
        nvgMoveTo(vg, cx - s * 0.2f, cy + s);
        nvgLineTo(vg, cx - s, cy + s);
        nvgLineTo(vg, cx - s, cy + s * 0.2f);
        nvgMoveTo(vg, cx - s, cy + s);
        nvgLineTo(vg, cx - s * 0.25f, cy + s * 0.25f);
        nvgStroke(vg);
        break;
    case Icon::Restore:
        // Two corners pulled in.
        nvgMoveTo(vg, cx + s * 0.25f, cy - s);
        nvgLineTo(vg, cx + s * 0.25f, cy - s * 0.25f);
        nvgLineTo(vg, cx + s, cy - s * 0.25f);
        nvgMoveTo(vg, cx + s * 0.25f, cy - s * 0.25f);
        nvgLineTo(vg, cx + s, cy - s);
        nvgMoveTo(vg, cx - s * 0.25f, cy + s);
        nvgLineTo(vg, cx - s * 0.25f, cy + s * 0.25f);
        nvgLineTo(vg, cx - s, cy + s * 0.25f);
        nvgMoveTo(vg, cx - s * 0.25f, cy + s * 0.25f);
        nvgLineTo(vg, cx - s, cy + s);
        nvgStroke(vg);
        break;
    case Icon::Capture:
        // An arrow down onto a line: keep what was just played.
        nvgMoveTo(vg, cx, cy - s);
        nvgLineTo(vg, cx, cy + s * 0.35f);
        nvgMoveTo(vg, cx - s * 0.55f, cy - s * 0.15f);
        nvgLineTo(vg, cx, cy + s * 0.4f);
        nvgLineTo(vg, cx + s * 0.55f, cy - s * 0.15f);
        nvgMoveTo(vg, cx - s, cy + s);
        nvgLineTo(vg, cx + s, cy + s);
        nvgStroke(vg);
        break;
    case Icon::Stack:
        // Two clips, one above the other, overlapping in time.
        nvgRect(vg, cx - s, cy - s * 0.75f, s * 1.3f, s * 0.6f);
        nvgRect(vg, cx - s * 0.3f, cy + s * 0.15f, s * 1.3f, s * 0.6f);
        nvgFill(vg);
        break;
    case Icon::Badges:
        // A note cut into repeats, with a dot above it: the marks drawn on notes with operators.
        for (int i = 0; i < 3; i++)
            nvgRect(vg, cx - s + i * s * 0.7f, cy - s * 0.1f, s * 0.55f, s * 0.8f);
        nvgCircle(vg, cx + s * 0.75f, cy - s * 0.7f, s * 0.25f);
        nvgFill(vg);
        break;
    case Icon::Chord:
        // Three notes stacked at the same time.
        for (int i = 0; i < 3; i++)
            nvgRect(vg, cx - s * 0.8f, cy - s * 0.95f + i * s * 0.7f, s * 1.6f, s * 0.45f);
        nvgFill(vg);
        break;
    case Icon::ScaleHighlight:
        // Two lit rows around a dim one.
        nvgRect(vg, cx - s, cy - s, s * 2, s * 0.5f);
        nvgRect(vg, cx - s, cy + s * 0.5f, s * 2, s * 0.5f);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - s * 0.5f, cy);
        nvgLineTo(vg, cx + s * 0.5f, cy);
        nvgStroke(vg);
        break;
    case Icon::ScaleSnap:
        // A magnet.
        nvgArc(vg, cx, cy, s * 0.8f, 0, NVG_PI, NVG_CW);
        nvgMoveTo(vg, cx - s * 0.8f, cy);
        nvgLineTo(vg, cx - s * 0.8f, cy - s);
        nvgMoveTo(vg, cx + s * 0.8f, cy);
        nvgLineTo(vg, cx + s * 0.8f, cy - s);
        nvgStroke(vg);
        break;
    case Icon::Fold:
        // Rows closing in on the middle.
        nvgMoveTo(vg, cx - s, cy - s);
        nvgLineTo(vg, cx + s, cy - s);
        nvgMoveTo(vg, cx - s, cy + s);
        nvgLineTo(vg, cx + s, cy + s);
        nvgMoveTo(vg, cx - s * 0.45f, cy - s * 0.6f);
        nvgLineTo(vg, cx, cy - s * 0.15f);
        nvgLineTo(vg, cx + s * 0.45f, cy - s * 0.6f);
        nvgMoveTo(vg, cx - s * 0.45f, cy + s * 0.6f);
        nvgLineTo(vg, cx, cy + s * 0.15f);
        nvgLineTo(vg, cx + s * 0.45f, cy + s * 0.6f);
        nvgStroke(vg);
        break;
    case Icon::Range:
        // A band between two edges.
        nvgMoveTo(vg, cx - s, cy - s);
        nvgLineTo(vg, cx - s, cy + s);
        nvgMoveTo(vg, cx + s, cy - s);
        nvgLineTo(vg, cx + s, cy + s);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgRect(vg, cx - s * 0.6f, cy - s * 0.45f, s * 1.2f, s * 0.9f);
        nvgFill(vg);
        break;
    case Icon::Ripple:
        // Blocks pushed along by an arrow.
        nvgRect(vg, cx - s, cy - s * 0.4f, s * 0.6f, s * 0.8f);
        nvgRect(vg, cx + s * 0.4f, cy - s * 0.4f, s * 0.6f, s * 0.8f);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - s * 0.3f, cy);
        nvgLineTo(vg, cx + s * 0.25f, cy);
        nvgMoveTo(vg, cx + s * 0.05f, cy - s * 0.25f);
        nvgLineTo(vg, cx + s * 0.25f, cy);
        nvgLineTo(vg, cx + s * 0.05f, cy + s * 0.25f);
        nvgStroke(vg);
        break;
    case Icon::AutoFollow:
        // A clip with a curve riding along under it.
        nvgRect(vg, cx - s, cy - s, s * 2, s * 0.7f);
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - s, cy + s);
        nvgBezierTo(vg, cx - s * 0.3f, cy + s * 0.9f, cx, cy + s * 0.1f, cx + s, cy + s * 0.2f);
        nvgStroke(vg);
        break;
    case Icon::None:
        break;
    }
}

} // namespace tessera
