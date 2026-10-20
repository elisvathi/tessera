#include "KeyActions.hpp"

#include "../../model/Scale.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../widgets/Draw.hpp"

#include <array>
#include <vector>

namespace tessera::keyActions {

namespace {

using rack::math::Rect;
using rack::math::Vec;

constexpr float kWidth = 316;
constexpr float kPad = 10;
constexpr float kRowH = 22;
constexpr float kScaleRowH = 19;
constexpr float kGap = 8;

/// One popover for the song key or a clip's key (Trello, 13 Oct: "first the root and confirm, then the mode and
/// confirm again"). Every click previews in the piano roll; Enter or closing the menu applies, Esc cancels.
class KeyPicker : public rack::widget::OpaqueWidget {
  public:
    KeyPicker(EditorContext& ctx, std::optional<ClipId> clip) : ctx(ctx) {
        const Song& song = ctx.module()->song();
        songOrig = songKey = song.scale;
        if (clip) {
            if (const Clip* c = song.findClip(*clip)) {
                this->clip = *clip;
                clipOrig = clipKey = c->scaleOverride;
                clipTab = c->scaleOverride.has_value(); // open where the clip's key comes from
            }
        }
        scales.clear();
        for (const ScaleDef& d : builtinScales())
            scales.push_back({d.id, d.id == std::string("chromatic") ? "Chromatic (no key)" : d.name});
        for (const CustomScale& s : song.customScales)
            scales.push_back({s.id, s.name});
        layout();
        preview();
    }

    ~KeyPicker() override {
        if (!cancelled)
            apply();
        ctx.state().keyPreview = KeyPreview{};
    }

    void step() override {
        if (!ctx.module()) {
            cancelled = true;
            close();
        }
        // Take the keyboard so Enter applies and Esc cancels (the editor leaves keys alone while a menu has them).
        if (APP->event->getSelectedWidget() != this)
            APP->event->setSelectedWidget(this);
        OpaqueWidget::step();
    }

    void draw(const DrawArgs& args) override;

    void onHover(const HoverEvent& e) override {
        hovered = hit(e.pos);
        OpaqueWidget::onHover(e);
    }
    void onLeave(const LeaveEvent& e) override { hovered = {}; }

    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        e.consume(this);
        click(hit(e.pos));
    }

    void onSelectKey(const SelectKeyEvent& e) override {
        if (e.action != GLFW_PRESS)
            return;
        if (e.key == GLFW_KEY_ENTER || e.key == GLFW_KEY_KP_ENTER) {
            close();
            e.consume(this);
        } else if (e.key == GLFW_KEY_ESCAPE) {
            cancelled = true;
            close();
            e.consume(this);
        }
    }

  private:
    enum class Part { None, TabSong, TabClip, Follow, Root, Scale, Move, Keep, Custom };
    struct Hit {
        Part part = Part::None;
        int index = 0;
        bool operator==(const Hit& o) const { return part == o.part && index == o.index; }
    };
    struct ScaleItem {
        std::string id, name;
    };

    // ---- What is being picked ----

    bool hasClip() const { return clip.valid(); }
    bool editingClip() const { return hasClip() && clipTab; }
    /// The key the current tab shows: the clip's own, the song's when it follows, or the song key.
    ScaleRef shown() const { return editingClip() && clipKey ? *clipKey : songKey; }
    bool following() const { return editingClip() && !clipKey; }

    void setShown(ScaleRef k) {
        if (editingClip())
            clipKey = k;
        else
            songKey = k;
        preview();
    }

    void preview() {
        KeyPreview p;
        p.active = true;
        p.song = songKey;
        p.clip = clip;
        p.clipKey = clipKey;
        ctx.state().keyPreview = p;
    }

    void click(Hit h) {
        TesseraModule* m = ctx.module();
        if (!m)
            return;
        ScaleRef k = shown();
        switch (h.part) {
        case Part::TabSong:
            clipTab = false;
            break;
        case Part::TabClip:
            clipTab = true;
            break;
        case Part::Follow:
            clipKey.reset();
            preview();
            break;
        case Part::Root:
            k.root = int8_t(h.index);
            // Picking a root while there is no key means a major key on it.
            if (resolveScale(k, m->song().customScales).chromatic())
                k.scale = "major";
            setShown(k);
            break;
        case Part::Scale:
            k.scale = scales[size_t(h.index)].id;
            setShown(k);
            break;
        case Part::Move:
            ctx.state().keyMovesNotes = true;
            break;
        case Part::Keep:
            ctx.state().keyMovesNotes = false;
            break;
        case Part::Custom: {
            // Custom scales are edited in their own menu, from the key as applied.
            EditorContext* c = &ctx;
            std::optional<ClipId> target = editingClip() ? std::optional<ClipId>(clip) : std::nullopt;
            close();
            apply();
            openCustomScalesMenu(*c, target);
            break;
        }
        case Part::None:
            break;
        }
    }

    void apply() {
        if (applied || cancelled)
            return;
        applied = true;
        ctx.state().keyPreview = KeyPreview{};
        TesseraModule* m = ctx.module();
        if (!m)
            return;
        bool move = ctx.state().keyMovesNotes;
        if (songKey != songOrig)
            m->perform(changeSongKey(m->song(), songKey, move));
        if (hasClip() && clipKey != clipOrig && m->song().findClip(clip))
            m->perform(changeClipKey(m->song(), clip, clipKey, move));
    }

    void close() {
        if (auto* overlay = getAncestorOfType<rack::ui::MenuOverlay>())
            overlay->requestDelete();
    }

    // ---- Layout ----

    void layout() {
        float y = kPad;
        header = Rect(kPad, y, kWidth - 2 * kPad, kRowH);
        float tabW = 74;
        tabSong = Rect(kPad, y, tabW, kRowH);
        tabClip = Rect(kPad + tabW, y, tabW, kRowH);
        follow = Rect(kWidth - kPad - 104, y, 104, kRowH);
        y += kRowH + kGap;
        float cell = (kWidth - 2 * kPad - 11 * 3) / 12;
        for (int r = 0; r < 12; r++)
            roots[size_t(r)] = Rect(kPad + float(r) * (cell + 3), y, cell, kRowH + 2);
        y += kRowH + 2 + kGap;
        float colW = (kWidth - 2 * kPad - 10) / 2;
        int rows = int((scales.size() + 1) / 2);
        scaleRects.clear();
        for (size_t i = 0; i < scales.size(); i++) {
            int col = int(i) / rows, row = int(i) % rows;
            scaleRects.push_back(Rect(kPad + float(col) * (colW + 10), y + float(row) * kScaleRowH, colW, kScaleRowH));
        }
        y += float(rows) * kScaleRowH + kGap;
        keys = Rect(kPad, y, kWidth - 2 * kPad, 26);
        y += 26 + kGap + 2;
        float segW = 210;
        move = Rect(kWidth - kPad - segW, y, segW / 2, kRowH);
        keep = Rect(kWidth - kPad - segW / 2, y, segW / 2, kRowH);
        notesLabelY = y + kRowH / 2;
        y += kRowH + kGap;
        footerY = y + 8;
        custom = Rect(kWidth - kPad - 100, y, 100, 16);
        y += 16 + 4;
        hintY = y + 7;
        y += 14;
        box.size = Vec(kWidth, y + kPad);
    }

    Hit hit(Vec p) const {
        if (hasClip()) {
            if (tabSong.contains(p))
                return {Part::TabSong, 0};
            if (tabClip.contains(p))
                return {Part::TabClip, 0};
            if (editingClip() && follow.contains(p))
                return {Part::Follow, 0};
        }
        for (int r = 0; r < 12; r++)
            if (roots[size_t(r)].contains(p))
                return {Part::Root, r};
        for (size_t i = 0; i < scaleRects.size(); i++)
            if (scaleRects[i].contains(p))
                return {Part::Scale, int(i)};
        if (move.contains(p))
            return {Part::Move, 0};
        if (keep.contains(p))
            return {Part::Keep, 0};
        if (custom.contains(p))
            return {Part::Custom, 0};
        return {};
    }

    EditorContext& ctx;
    ClipId clip;
    bool clipTab = false;
    ScaleRef songKey, songOrig;
    std::optional<ScaleRef> clipKey, clipOrig;
    bool cancelled = false, applied = false;
    std::vector<ScaleItem> scales;

    Rect header, tabSong, tabClip, follow, keys, move, keep, custom;
    std::array<Rect, 12> roots;
    std::vector<Rect> scaleRects;
    float notesLabelY = 0, footerY = 0, hintY = 0;
    Hit hovered;
};

bool isBlack(int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

void KeyPicker::draw(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    const theme::Palette& c = theme::colors();
    const theme::KeyColors& k = theme::keyColors();
    const Song& song = m->song();
    ScaleRef key = shown();
    Scale scale = resolveScale(key, song.customScales);
    bool noKey = scale.chromatic();

    draw::roundedRect(vg, Rect(Vec(), box.size), theme::metrics::kRadiusControl, c.bg1);
    draw::TextStyle text{theme::fonts::ui(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::TextStyle left{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    auto button = [&](Rect r, const std::string& label, bool on, bool hover, bool strong = false) {
        draw::roundedRect(vg, r, theme::metrics::kRadiusControl, on ? c.accent : (hover ? c.bg2 : c.bg0));
        draw::TextStyle t = text;
        t.color = on ? c.ink : c.text0;
        if (on || strong)
            t.font = theme::fonts::uiStrong();
        draw::text(vg, t, r.getCenter().x, r.getCenter().y, label);
    };

    // Song | This clip, or a title.
    if (hasClip()) {
        draw::roundedRect(vg, Rect(tabSong.pos, Vec(tabSong.size.x * 2, kRowH)), theme::metrics::kRadiusControl, c.bg0);
        button(tabSong, "Song", !clipTab, hovered.part == Part::TabSong);
        button(tabClip, "This clip", clipTab, hovered.part == Part::TabClip);
        if (editingClip())
            button(follow, "Same as song", following(), hovered.part == Part::Follow);
        else {
            draw::TextStyle hint = left;
            hint.color = c.text2;
            hint.align = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;
            draw::text(vg, hint, kWidth - kPad, header.getCenter().y, "Every clip that follows it");
        }
    } else {
        draw::TextStyle title = left;
        title.font = theme::fonts::uiStrong();
        title.color = c.text0;
        title.size = theme::metrics::kTextHeader;
        draw::text(vg, title, kPad, header.getCenter().y, "Song key");
    }

    // Roots, black keys a shade darker. A clip following the song shows the song's key, dimmed.
    float dim = following() ? 0.5f : 1.f;
    nvgGlobalAlpha(vg, dim);
    for (int r = 0; r < 12; r++) {
        Rect rr = roots[size_t(r)];
        bool on = !noKey && key.root == r;
        bool hover = hovered.part == Part::Root && hovered.index == r;
        draw::roundedRect(vg, rr, 3,
                          on ? c.accent : (hover ? c.bg2 : (isBlack(r) ? theme::recessed(c, c.bg0, 0.25f) : c.bg0)));
        draw::TextStyle t = text;
        t.color = on ? c.ink : (isBlack(r) ? c.text1 : c.text0);
        t.font = on ? theme::fonts::uiStrong() : theme::fonts::ui();
        draw::text(vg, t, rr.getCenter().x, rr.getCenter().y, rootName(r));
    }

    // Scales.
    for (size_t i = 0; i < scales.size(); i++) {
        Rect r = scaleRects[i];
        bool on = scales[i].id == key.scale || (noKey && scales[i].id == "chromatic");
        bool hover = hovered.part == Part::Scale && hovered.index == int(i);
        if (on || hover)
            draw::roundedRect(vg, r, 3, on ? nvgTransRGBAf(c.accent, 0.22f) : c.bg2);
        draw::TextStyle t = left;
        t.color = on ? c.text0 : c.text1;
        t.font = on ? theme::fonts::uiStrong() : theme::fonts::ui();
        draw::text(vg, t, r.pos.x + 6, r.getCenter().y, draw::ellipsize(vg, t, scales[i].name, r.size.x - 10));
    }
    nvgGlobalAlpha(vg, 1);

    // One octave, the key's notes marked, the root in its own colour.
    float w = keys.size.x / 12;
    for (int pc = 0; pc < 12; pc++) {
        Rect kr(keys.pos.x + float(pc) * w + 0.5f, keys.pos.y, w - 1, keys.size.y);
        draw::roundedRect(vg, kr, 2, isBlack(pc) ? k.ebony : k.ivory);
        int pitch = 60 + pc;
        if (!noKey && scale.inScale(pitch)) {
            Rect mark(kr.pos.x + 2, kr.pos.y + kr.size.y - 6, kr.size.x - 4, 4);
            draw::roundedRect(vg, mark, 1.5f, scale.isRoot(pitch) ? c.queue : c.accent);
        }
    }

    // What happens to the notes already there.
    draw::text(vg, left, kPad, notesLabelY, "Notes");
    bool moves = ctx.state().keyMovesNotes;
    draw::roundedRect(vg, Rect(move.pos, Vec(move.size.x * 2, kRowH)), theme::metrics::kRadiusControl, c.bg0);
    button(move, "Move with the key", moves, hovered.part == Part::Move);
    button(keep, "Keep pitches", !moves, hovered.part == Part::Keep);

    // The key in words, the custom scales, and how to finish.
    draw::TextStyle name = left;
    name.font = theme::fonts::uiStrong();
    name.color = c.text0;
    std::string label = keyLabel(song, key);
    if (editingClip())
        label += following() ? "  ·  same as the song" : "  ·  this clip";
    draw::text(vg, name, kPad, footerY, label);
    draw::TextStyle link = left;
    link.align = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;
    link.color = hovered.part == Part::Custom ? c.text0 : c.accent;
    draw::text(vg, link, custom.pos.x + custom.size.x, custom.getCenter().y, "Custom scales…");
    draw::TextStyle hint = left;
    hint.color = c.text2;
    draw::text(vg, hint, kPad, hintY, "Enter or click away applies  ·  Esc cancels");
}

} // namespace

void openKeyPicker(EditorContext& ctx, std::optional<ClipId> clip, bool withSettings) {
    if (!ctx.module())
        return;
    rack::ui::Menu* menu = rack::createMenu();
    auto* picker = new KeyPicker(ctx, clip);
    menu->addChild(picker);
    if (withSettings) {
        EditorContext* c = &ctx;
        menu->addChild(new rack::ui::MenuSeparator);
        auto flag = [c, menu](std::string text, std::string right, bool EditorState::* field) {
            // The menu stays open, so the switches can be tried while the key picker is still there.
            menu->addChild(rack::createCheckMenuItem(
                text, right, [c, field] { return c->module() && c->state().*field; },
                [c, field] { c->state().*field = !(c->state().*field); }, false, true));
        };
        flag("Highlight the key's rows", "", &EditorState::scaleHighlight);
        flag("Snap notes to the key", "Alt: any note", &EditorState::scaleSnap);
        flag("Fold: only the scale's rows", shortcutLabel(Action::ScaleFold), &EditorState::scaleFold);
    }
    APP->event->setSelectedWidget(picker);
}

} // namespace tessera::keyActions
