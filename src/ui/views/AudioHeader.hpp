#pragma once

#include "../EditorContext.hpp"
#include "../../model/Song.hpp"
#include "../widgets/ToolButton.hpp"

#include <rack.hpp>

#include <functional>
#include <string>
#include <vector>

namespace tessera {

class AudioEditor;

/// The audio editor's part of the pane header: the clip's settings as chips, and the Select and Slice tools. In a narrow
/// pane the chips fold into one Clip menu (docs/design/AUDIO_UX.md A2).
class AudioHeader {
  public:
    AudioHeader(EditorContext& ctx, rack::widget::Widget* parent, AudioEditor* editor);

    /// Places everything between `left` and `right`, each frame. Returns the left edge of what it placed.
    float layout(float left, float right, float y, float height);
    /// Hides it all, while the pane shows notes.
    void hide();

  private:
    struct Chip {
        ToolButton* button;
        std::function<std::string()> text;
        std::function<bool()> shown;
        float width = 0;
        bool caret = false;
    };
    /// A chip whose text comes from `text`. A chip with `menu` opens it.
    void addChip(std::function<std::string()> text, const std::string& tooltip, std::function<void(rack::ui::Menu*)> menu,
                 std::function<void()> action, std::function<bool()> shown, std::function<bool()> active = nullptr);
    /// The shown clip, or null.
    const Clip* clip() const;

    EditorContext& ctx;
    AudioEditor* editor;
    rack::widget::Widget* parent;
    std::vector<Chip> chips;
    ToolButton* clipMenu = nullptr;
    std::vector<ToolButton*> tools;
};

} // namespace tessera
