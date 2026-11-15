#include "AudioHeader.hpp"

#include "../../model/AudioRegions.hpp"
#include "../../module/TesseraModule.hpp"
#include "../editing/AudioActions.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../interaction/Snap.hpp"
#include "AudioClipDraw.hpp"
#include "AudioEditor.hpp"

namespace tessera {

namespace {

constexpr float kChipGap = 4;
constexpr float kToolSize = 22;
constexpr float kCharWidth = 6.6f; // of the header's font, near enough to size a chip
constexpr float kChipPadding = 16;
constexpr float kCaretWidth = 18; // room for a chip's chevron
constexpr float kSnapWide = 108;
constexpr float kSnapShort = 74;
constexpr float kClipMenuWidth = 104;
constexpr float kClipMenuShort = 56;

/// The clip gain as the header writes it: "0 dB" at unity, otherwise signed with one decimal.
std::string gainText(float db) {
    if (std::fabs(db) < 0.05f)
        return "0 dB";
    return audioDraw::dbText(db);
}

/// A chip's width for its words: UTF-8 symbols count once.
float textWidth(const std::string& text) {
    int glyphs = 0;
    for (unsigned char ch : text)
        glyphs += (ch & 0xC0) != 0x80;
    return float(glyphs) * kCharWidth;
}

} // namespace

const Clip* AudioHeader::clip() const {
    TesseraModule* m = ctx.module();
    const Clip* c = m ? m->song().findClip(editor->clip) : nullptr;
    return c && c->audio ? c : nullptr;
}

void AudioHeader::addChip(std::function<std::string(int)> text, const std::string& tooltip,
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
    auto named = [](const char* name, const std::string& value, int level) {
        return level == 0 ? std::string(name) + " " + value : value;
    };
    addChip(
        [this, named](int level) {
            char t[24];
            snprintf(t, sizeof t, "%.4g BPM", clip() ? clip()->audio->sourceTempo : 120);
            return named("Tempo", t, level);
        },
        "Tempo: the speed the file was played at",
        [c, id](rack::ui::Menu* m) { audioActions::appendTempoItems(m, *c, id()); }, nullptr, always);
    addChip(
        [this, named](int level) { return named("Follow", kFollow[clip() ? int(clip()->audio->follow) : 0], level); },
        "Follow: how the clip keeps up when the song's tempo changes",
        [c, id](rack::ui::Menu* m) { audioActions::appendFollowItems(m, *c, id()); }, nullptr, always);
    addChip([this, named](int level) { return named("Sound", kSound[clip() ? int(clip()->audio->sound) : 0], level); },
            "Sound: what the file holds, so stretching keeps it clean",
            [c, id](rack::ui::Menu* m) { audioActions::appendSoundItems(m, *c, id()); }, nullptr, always);
    addChip(
        [this, named](int level) { return clip() ? named("Pitch", audioDraw::pitchText(*clip()->audio), level) : ""; },
        "Pitch", [c, id](rack::ui::Menu* m) { audioActions::appendPitchItems(m, *c, id()); }, nullptr,
        [this] { return clip() && clip()->audio->follow == TempoFollow::Stretch; });
    addChip([this, named](int level) { return clip() ? named("Gain", gainText(clip()->audio->gainDb), level) : ""; },
            "Gain: the clip's own level, before the track's fader",
            [c, id](rack::ui::Menu* m) { audioActions::appendGainItems(m, *c, id()); }, nullptr, always);
    addChip([](int) { return std::string("Reverse"); }, "Reverse: play the clip backwards. Lit while on", nullptr,
            [c, id] {
                TesseraModule* m = c->module();
                if (m)
                    m->perform(reverseAudio(m->song(), id()));
            },
            always, [this] { return clip() && regions::playsBackwards(*clip()->audio); });
    addChip([](int) { return std::string("Quantize"); },
            withShortcut("Quantize: move the hits toward the grid with warp markers", Action::QuantizeDialog),
            [c, id](rack::ui::Menu* m) { audioActions::appendWarpItems(m, *c, id()); }, nullptr, always);
    addChip([this](int) { return clip() ? "Takes " + std::to_string(clip()->audio->takes.size() + 1) : std::string(); },
            withShortcut("Show the takes", Action::ToggleTakes), nullptr, [e] { e->toggleTakes(); },
            [this] { return clip() && !clip()->audio->takes.empty(); }, [e] { return e->takesShown(); });
    auto uses = [c, id] { return c->module() ? c->module()->song().clipUses(id()) : 0; };
    addChip(
        [uses](int level) {
            std::string n = "∞ " + std::to_string(uses());
            return level == 0 ? n + " linked copies" : n;
        },
        "Linked copies: an edit here changes every copy", nullptr, nullptr, [uses] { return uses() > 1; });
    addChip([](int) { return std::string("Make unique"); },
            withShortcut("Give this copy its own clip", Action::MakeUnique), nullptr, [e] { e->makeClipUnique(); },
            [uses, e] { return uses() > 1 && e->onTimeline(); });

    ToolButton::Options menu;
    menu.text = "Clip settings";
    menu.caret = true;
    menu.tooltip = "This clip's tempo, follow, sound, pitch, gain and more";
    menu.action = [this, c, id] {
        rack::ui::Menu* m = rack::createMenu();
        audioActions::appendAudioClipMenu(m, *c, id());
        if (!snapButton->visible) {
            m->addChild(new rack::ui::MenuSeparator);
            m->addChild(rack::createSubmenuItem("Snap", snapLabel(c->state().snap),
                                                [c](rack::ui::Menu* sub) { appendSnapItems(sub, c); }));
        }
    };
    clipMenu = new ToolButton(menu);
    parent->addChild(clipMenu);

    ToolButton::Options snap;
    snap.caret = true;
    snap.tooltip = "Snap: the grid that splits, part edges and warp markers stick to. Alt bypasses it while dragging";
    snap.action = [c] { openSnapMenu(c); };
    snapButton = new ToolButton(snap);
    parent->addChild(snapButton);

    tools = makeToolSwitchOf(ctx, {Tool::Pointer, Tool::Slice}, {Tool::Mute, Tool::Erase}, nullptr);
    for (ToolButton* b : {tools.select, tools.draw, tools.more})
        parent->addChild(b);
}

void AudioHeader::hide() {
    for (Chip& chip : chips)
        chip.button->visible = false;
    for (ToolButton* b : {clipMenu, snapButton, tools.select, tools.draw, tools.more})
        b->visible = false;
}

float AudioHeader::chipsWidth(int level) {
    float needed = 0;
    for (Chip& chip : chips) {
        if (!clip() || !chip.shown())
            continue;
        chip.width = textWidth(chip.text(level)) + kChipPadding + (chip.caret ? kCaretWidth : 0);
        needed += chip.width + kChipGap;
    }
    return needed;
}

float AudioHeader::layout(float left, float right, float y, float height) {
    // From the right, as in the notes header: Snap, then the tool caret, Slice and Select.
    float caret = toolSwitchCaretWidth(tools, kToolSize);
    float toolsWidth = kToolSize * 2 + caret + 2;
    std::string grid = snapLabel(ctx.state().snap);
    auto room = [&](float snapWidth) { return right - left - toolsWidth - (snapWidth > 0 ? snapWidth + 8 : 0) - 8; };
    // Pick the widest layout that fits: words, values alone, then one menu, giving up Snap's word and then Snap.
    float snapWidth = kSnapWide;
    density = 0;
    if (chipsWidth(0) > room(kSnapWide)) {
        density = 1;
        if (chipsWidth(1) > room(kSnapWide)) {
            snapWidth = kSnapShort;
            if (chipsWidth(1) > room(kSnapShort)) {
                density = 2;
                if (kClipMenuWidth > room(kSnapShort) && kClipMenuShort + kSnapShort + 8 > room(0))
                    snapWidth = 0;
            }
        }
    }
    float x = right;
    snapButton->visible = snapWidth > 0;
    if (snapWidth > 0) {
        snapButton->setText(snapWidth == kSnapWide ? "Snap " + grid : grid);
        x -= snapWidth;
        snapButton->box = rack::math::Rect(x, y, snapWidth, height);
        x -= 8;
    }
    for (auto [b, w] :
         {std::pair<ToolButton*, float>{tools.more, caret}, {tools.draw, kToolSize}, {tools.select, kToolSize}}) {
        x -= w;
        b->visible = true;
        b->box = rack::math::Rect(x, y, w, height);
        x -= 1;
    }
    x -= 8;
    if (density == 2) {
        for (Chip& chip : chips)
            chip.button->visible = false;
        float w = std::max(0.f, std::min(kClipMenuWidth, x - left));
        clipMenu->setText(w < kClipMenuWidth ? "Clip" : "Clip settings");
        clipMenu->visible = clip() && w >= kClipMenuShort;
        clipMenu->box = rack::math::Rect(x - w, y, w, height);
        return clipMenu->visible ? x - w : x;
    }
    clipMenu->visible = false;
    float needed = chipsWidth(density);
    float cursor = x - needed + kChipGap;
    float first = cursor;
    for (Chip& chip : chips) {
        bool shown = clip() && chip.shown();
        chip.button->visible = shown;
        if (!shown)
            continue;
        chip.button->setText(chip.text(density));
        chip.button->box = rack::math::Rect(cursor, y, chip.width, height);
        cursor += chip.width + kChipGap;
    }
    return first;
}

} // namespace tessera
