#pragma once

#include "../../model/Song.hpp"
#include "../EditorContext.hpp"
#include "../widgets/EditTools.hpp"
#include "../widgets/ToolButton.hpp"

#include <rack.hpp>

#include <functional>
#include <string>
#include <vector>

namespace tessera {

class AudioEditor;

/// The audio editor's part of the pane header: the clip's settings as chips, then the same tool switch and Snap as
/// the notes header. As the pane narrows, the chips first drop their leading words ("Tempo 112 BPM" becomes
/// "112 BPM"), then fold into one Clip settings menu, then Snap hides (docs/DECISIONS.md §162).
class AudioHeader {
  public:
    AudioHeader(EditorContext& ctx, rack::widget::Widget* parent, AudioEditor* editor);

    /// Places everything between `left` and `right`, each frame. Returns the left edge of what it placed.
    float layout(float left, float right, float y, float height);
    /// Hides it all, while the pane shows notes.
    void hide();
    /// True when the last layout had to shorten or fold the chips: the clip chip on the left then shortens too.
    bool narrow() const { return density > 0; }

  private:
    struct Chip {
        ToolButton* button;
        /// Its words at a density: 0 with the setting's name, 1 the value alone.
        std::function<std::string(int)> text;
        std::function<bool()> shown;
        float width = 0;
        bool caret = false;
    };
    /// A chip whose text comes from `text`. A chip with `menu` opens it.
    void addChip(std::function<std::string(int)> text, const std::string& tooltip,
                 std::function<void(rack::ui::Menu*)> menu, std::function<void()> action, std::function<bool()> shown,
                 std::function<bool()> active = nullptr);
    /// The room the shown chips need at `level` (0 or 1), measuring each one's width on the way.
    float chipsWidth(int level);
    /// The shown clip, or null.
    const Clip* clip() const;

    EditorContext& ctx;
    AudioEditor* editor;
    rack::widget::Widget* parent;
    std::vector<Chip> chips;
    ToolButton* clipMenu = nullptr;
    ToolButton* snapButton = nullptr;
    ToolSwitch tools{};
    int density = 0; // 0 words, 1 values alone, 2 folded into the Clip settings menu
};

} // namespace tessera
