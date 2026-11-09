#include "AudioHeader.hpp"

#include "../../model/AudioRegions.hpp"
#include "../../module/TesseraModule.hpp"
#include "../editing/AudioActions.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../widgets/EditTools.hpp"
#include "AudioClipDraw.hpp"
#include "AudioEditor.hpp"

namespace tessera {

namespace {

constexpr float kChipGap = 4;
constexpr float kToolSize = 24;
constexpr float kCharWidth = 6.6f; // of the header's font, near enough to size a chip
constexpr float kChipPadding = 16;
constexpr float kCaretWidth = 18; // room for a chip's chevron

} // namespace

const Clip* AudioHeader::clip() const {
    TesseraModule* m = ctx.module();
    const Clip* c = m ? m->song().findClip(editor->clip) : nullptr;
    return c && c->audio ? c : nullptr;
}

void AudioHeader::addChip(std::function<std::string()> text, const std::string& tooltip,
                          std::function<void(rack::ui::Menu*)> menu, std::function<void()> action,
                          std::function<bool()> shown, std::function<bool()> active) {
    ToolButton::Options o;
    o.tooltip = tooltip;
    o.caret = menu != nullptr;
    o.active = std::move(active);
    if (menu)
        o.action = [menu] { menu(rack::createMenu()); };
    else
        o.action = action ? std::move(action) : [] {};
    Chip chip;
    chip.button = new ToolButton(o);
    chip.text = std::move(text);
    chip.shown = std::move(shown);
    chip.caret = menu != nullptr;
    parent->addChild(chip.button);
    chips.push_back(std::move(chip));
}

AudioHeader::AudioHeader(EditorContext& context, rack::widget::Widget* parentWidget, AudioEditor* audioEditor)
    : ctx(context), editor(audioEditor), parent(parentWidget) {
    EditorContext* c = &ctx;
    AudioEditor* e = editor;
    auto always = [] { return true; };
    auto id = [e] { return e->clip; };
    static const char* const kFollow[] = {"Stretch", "Repitch", "Off"};
    static const char* const kSound[] = {"Drums", "Melody", "Voice", "Full mix"};
    addChip(
        [this] {
            char t[16];
            snprintf(t, sizeof t, "%.4g", clip() ? clip()->audio->sourceTempo : 120);
            return std::string(t);
        },
        "Clip tempo", [c, id](rack::ui::Menu* m) { audioActions::appendTempoItems(m, *c, id()); }, nullptr, always);
    addChip([this] { return std::string(kFollow[clip() ? int(clip()->audio->follow) : 0]); }, "Follow tempo",
            [c, id](rack::ui::Menu* m) { audioActions::appendFollowItems(m, *c, id()); }, nullptr, always);
    addChip([this] { return std::string(kSound[clip() ? int(clip()->audio->sound) : 0]); }, "Sound type",
            [c, id](rack::ui::Menu* m) { audioActions::appendSoundItems(m, *c, id()); }, nullptr, always);
    addChip([this] { return clip() ? "Pitch " + audioDraw::pitchText(*clip()->audio) : std::string(); }, "Pitch",
            [c, id](rack::ui::Menu* m) { audioActions::appendPitchItems(m, *c, id()); }, nullptr,
            [this] { return clip() && clip()->audio->follow == TempoFollow::Stretch; });
    addChip([this] { return clip() ? "Gain " + audioDraw::dbText(clip()->audio->gainDb) : std::string(); }, "Clip gain",
            [c, id](rack::ui::Menu* m) { audioActions::appendGainItems(m, *c, id()); }, nullptr, always);
    addChip(
        [] { return std::string("\u25c0"); }, "Reverse", nullptr,
        [c, id] {
            TesseraModule* m = c->module();
            if (m)
                m->perform(reverseAudio(m->song(), id()));
        },
        always, [this] { return clip() && regions::playsBackwards(*clip()->audio); });
    addChip([] { return std::string("Warp"); }, withShortcut("Quantize the hits with warp markers", Action::QuantizeDialog),
            [c, id](rack::ui::Menu* m) { audioActions::appendWarpItems(m, *c, id()); }, nullptr, always);
    addChip([this] { return clip() ? "Takes " + std::to_string(clip()->audio->takes.size() + 1) : std::string(); },
            withShortcut("Show the takes", Action::ToggleTakes), nullptr, [e] { e->toggleTakes(); },
            [this] { return clip() && !clip()->audio->takes.empty(); }, [e] { return e->takesShown(); });
    auto uses = [c, id] { return c->module() ? c->module()->song().clipUses(id()) : 0; };
    addChip([uses] { return "\u221e " + std::to_string(uses()) + " copies"; }, "Edits change every linked copy", nullptr,
            nullptr, [uses] { return uses() > 1; });
    addChip([] { return std::string("Make unique"); }, withShortcut("Give this copy its own clip", Action::MakeUnique), nullptr,
            [e] { e->makeClipUnique(); }, [uses, e] { return uses() > 1 && e->onTimeline(); });

    ToolButton::Options menu;
    menu.text = "Clip";
    menu.caret = true;
    menu.tooltip = "This clip's settings";
    menu.action = [c, id] {
        rack::ui::Menu* m = rack::createMenu();
        audioActions::appendAudioClipMenu(m, *c, id());
    };
    clipMenu = new ToolButton(menu);
    parent->addChild(clipMenu);

    tools = makeToolButtons(ctx, {Tool::Pointer, Tool::Slice}, 18, nullptr);
    for (ToolButton* b : tools)
        parent->addChild(b);
}

void AudioHeader::hide() {
    for (Chip& chip : chips)
        chip.button->visible = false;
    clipMenu->visible = false;
    for (ToolButton* b : tools)
        b->visible = false;
}

float AudioHeader::layout(float left, float right, float y, float height) {
    float x = right;
    for (auto it = tools.rbegin(); it != tools.rend(); ++it) {
        x -= kToolSize;
        (*it)->visible = true;
        (*it)->box = rack::math::Rect(x, y, kToolSize, height);
        x -= kChipGap;
    }
    x -= kChipGap;
    float needed = 0;
    for (Chip& chip : chips) {
        bool shown = clip() && chip.shown();
        chip.button->visible = shown;
        if (!shown)
            continue;
        std::string text = chip.text();
        chip.button->setText(text);
        chip.width = float(text.size()) * kCharWidth + kChipPadding + (chip.caret ? kCaretWidth : 0);
        needed += chip.width + kChipGap;
    }
    if (needed > x - left) {
        for (Chip& chip : chips)
            chip.button->visible = false;
        clipMenu->visible = true;
        clipMenu->box = rack::math::Rect(x - 56, y, 56, height);
        return x - 56;
    }
    clipMenu->visible = false;
    float cursor = x - needed + kChipGap;
    float first = cursor;
    for (Chip& chip : chips) {
        if (!chip.button->visible)
            continue;
        chip.button->box = rack::math::Rect(cursor, y, chip.width, height);
        cursor += chip.width + kChipGap;
    }
    return first;
}

} // namespace tessera
