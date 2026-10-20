#pragma once

#include <rack.hpp>

#include <functional>
#include <string>

namespace tessera {

/// A text field placed over a label to rename it in place. Enter or clicking away commits, Esc cancels.
/// It removes itself when done.
class InlineTextField : public rack::ui::TextField {
  public:
    InlineTextField(const std::string& initial, std::function<void(const std::string&)> onCommit)
        : initialText(initial), commitFn(std::move(onCommit)) {
        setText(initial);
        selectAll();
    }

    /// Gives the field the keyboard. Call after adding it to its parent.
    void focus() { APP->event->setSelectedWidget(this); }

    void onSelectKey(const SelectKeyEvent& e) override {
        if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
            finish(true);
            e.consume(this);
            return;
        }
        if (e.action == GLFW_PRESS && e.key == GLFW_KEY_ESCAPE) {
            finish(false);
            e.consume(this);
            return;
        }
        TextField::onSelectKey(e);
    }

    void onDeselect(const DeselectEvent& e) override { finish(true); }

  private:
    void finish(bool commit) {
        if (done)
            return;
        done = true;
        std::string value = getText();
        if (commit && value != initialText && commitFn)
            commitFn(value);
        if (APP->event->getSelectedWidget() == this)
            APP->event->setSelectedWidget(nullptr);
        requestDelete();
    }

    std::string initialText;
    std::function<void(const std::string&)> commitFn;
    bool done = false;
};

} // namespace tessera
