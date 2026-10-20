#pragma once

#include <rack.hpp>

#include <functional>
#include <string>

namespace tessera {

/// A text field for context menus. The edit is committed once, on Enter or when the menu closes.
class MenuTextField : public rack::ui::TextField {
  public:
    MenuTextField(const std::string& initial, std::function<void(const std::string&)> onCommit)
        : initialText(initial), commitFn(std::move(onCommit)) {
        box.size.x = 180;
        setText(initial);
        selectAll();
    }

    ~MenuTextField() override { commit(); }

    void onSelectKey(const SelectKeyEvent& e) override {
        if (e.action == GLFW_PRESS && (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER)) {
            commit();
            if (auto* overlay = getAncestorOfType<rack::ui::MenuOverlay>())
                overlay->requestDelete();
            e.consume(this);
            return;
        }
        TextField::onSelectKey(e);
    }

  private:
    void commit() {
        if (committed)
            return;
        committed = true;
        std::string value = getText();
        if (value != initialText && commitFn)
            commitFn(value);
    }

    std::string initialText;
    std::function<void(const std::string&)> commitFn;
    bool committed = false;
};

} // namespace tessera
