#include "InteractiveView.hpp"

#include "Navigation.hpp"

namespace tessera {

namespace {

constexpr double kClickSlopPx = 3;
constexpr double kMaxAutoScrollStep = 0.05; // seconds; avoids a jump after a stalled frame

} // namespace

InteractiveView::~InteractiveView() {
    if (ctx.dragging == this) {
        ctx.dragging = nullptr;
        ctx.readout.clear();
    }
}

rack::math::Vec InteractiveView::localMouse() {
    rack::math::Vec origin = getAbsoluteOffset(rack::math::Vec());
    return APP->scene->getMousePos().minus(origin).div(getAbsoluteZoom());
}

void InteractiveView::cancelGesture() {
    pressButton = -1;
    if (gesture)
        finishGesture(false);
}

void InteractiveView::refreshGesture() {
    if (!gesture)
        return;
    rack::math::Vec p = localMouse();
    gesture->update(p, ctx.mods);
    showReadout(p);
}

void InteractiveView::finishGesture(bool commit) {
    std::unique_ptr<DragGesture> g = std::move(gesture);
    if (ctx.dragging == this)
        ctx.dragging = nullptr;
    ctx.readout.clear();
    ctx.snapMark = NAN;
    ctx.snapAxis = nullptr;
    if (commit)
        g->commit();
    else
        g->cancel();
}

void InteractiveView::showReadout(rack::math::Vec pos) {
    ctx.readout = gesture ? gesture->readout() : std::string();
    if (ctx.root)
        ctx.readoutPos = getRelativeOffset(pos, ctx.root);
}

void InteractiveView::step() {
    double now = EditorContext::now();
    double dt = std::min(now - lastStep, kMaxAutoScrollStep);
    lastStep = now;

    if (gesture) {
        ctx.cursor = gesture->cursor();
        if (gesture->autoScrollX() || gesture->autoScrollY()) {
            rack::math::Vec p = localMouse();
            double vx = gesture->autoScrollX() ? autoScrollSpeed(p.x, box.size.x) : 0;
            double vy = gesture->autoScrollY() ? autoScrollSpeed(p.y, box.size.y) : 0;
            if (vx != 0 || vy != 0) {
                autoScroll(vx * dt, vy * dt);
                gesture->update(p, ctx.mods);
                showReadout(p);
            }
        }
    }
    OpaqueWidget::step();
}

void InteractiveView::draw(const DrawArgs& args) {
    drawView(args);
    OpaqueWidget::draw(args);
    drawForeground(args);
    if (gesture) {
        nvgSave(args.vg);
        gesture->drawOverlay(args.vg);
        nvgRestore(args.vg);
    }
}

void InteractiveView::onButton(const ButtonEvent& e) {
    OpaqueWidget::onButton(e);
    if (e.isConsumed() && e.getTarget() != this)
        return; // a child widget (a button, say) took it
    if (e.action != GLFW_PRESS || gesture)
        return;

    ctx.mods = Modifiers::fromGlfw(e.mods);
    int button = e.button;
#ifdef ARCH_MAC
    // On macOS, Ctrl+click is a right click.
    if (button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & GLFW_MOD_CONTROL))
        button = GLFW_MOUSE_BUTTON_RIGHT;
#endif
    pressButton = button;
    pressPos = e.pos;
    moved = false;
    gesture = startGesture(e.pos, button, ctx.mods);
    if (gesture) {
        ctx.dragging = this;
        gesture->update(e.pos, ctx.mods);
        showReadout(e.pos);
    }
    e.consume(this); // needed to receive drag events
}

void InteractiveView::onDragStart(const DragStartEvent& e) { e.consume(this); }

void InteractiveView::onDragMove(const DragMoveEvent& e) {
    if (pressButton < 0)
        return;
    rack::math::Vec p = localMouse();
    if (p.minus(pressPos).norm() > kClickSlopPx)
        moved = true;
    if (gesture) {
        ctx.mods = Modifiers::fromGlfw(APP->window->getMods());
        gesture->update(p, ctx.mods);
        showReadout(p);
    }
}

void InteractiveView::onDragEnd(const DragEndEvent& e) {
    int button = pressButton;
    pressButton = -1;
    if (button < 0)
        return;
    if (gesture)
        finishGesture(true);
    else if (!moved)
        click(pressPos, button, ctx.mods);
}

void InteractiveView::onDoubleClick(const DoubleClickEvent& e) {
    // The second press already started a gesture or a click; the double-click replaces both.
    cancelGesture();
    doubleClick(localMouse(), Modifiers::fromGlfw(APP->window->getMods()));
    e.consume(this);
}

void InteractiveView::onHover(const HoverEvent& e) {
    OpaqueWidget::onHover(e);
    if (gesture)
        return;
    bool overChild = e.getTarget() != this;
    ctx.cursor = overChild ? CursorShape::Arrow : hoverCursor(e.pos, Modifiers::fromGlfw(APP->window->getMods()));
}

void InteractiveView::onLeave(const LeaveEvent& e) {
    if (!gesture)
        ctx.cursor = CursorShape::Arrow;
}

void InteractiveView::onHoverScroll(const HoverScrollEvent& e) {
    OpaqueWidget::onHoverScroll(e);
    if (e.isConsumed())
        return; // a child scrolled
    wheel(e.pos, e.scrollDelta, Modifiers::fromGlfw(APP->window->getMods()));
    e.consume(this);
}

} // namespace tessera
