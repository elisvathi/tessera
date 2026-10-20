#include "KeyActions.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/MenuTextField.hpp"

namespace tessera::keyActions {

namespace {

constexpr float kKeyCell = 20;
constexpr float kKeyRow = 24;
const char* const kIntervalNames[] = {"1", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7"};

/// The 12-key toggle editor for a custom scale, inside a menu. Each click is one undo step, and the menu stays
/// open so several keys can be toggled in a row.
class ScaleKeysEntry : public rack::ui::MenuEntry {
  public:
    ScaleKeysEntry(EditorContext* c, std::string id) : c(c), id(std::move(id)) {
        box.size = rack::math::Vec(12 * kKeyCell + 8, kKeyRow);
    }

    void draw(const DrawArgs& args) override {
        const CustomScale* s = scale();
        if (!s)
            return;
        NVGcontext* vg = args.vg;
        const theme::Palette& p = theme::colors();
        draw::TextStyle label{theme::fonts::ui(), 9, p.text0, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        for (int i = 0; i < 12; i++) {
            rack::math::Rect r(4 + i * kKeyCell, 3, kKeyCell - 2, kKeyRow - 6);
            bool on = i == 0 || (s->mask >> i) & 1;
            NVGcolor fill = on ? (i == 0 ? nvgLerpRGBA(p.accent, p.bg2, 0.4f) : p.accent) : p.bg2;
            if (i == hovered && i != 0)
                fill = nvgLerpRGBA(fill, p.text0, 0.15f);
            draw::roundedRect(vg, r, 3, fill);
            label.color = on ? p.ink : p.text1;
            draw::text(vg, label, r.pos.x + r.size.x / 2, r.pos.y + r.size.y / 2, kIntervalNames[i]);
        }
    }

    void onHover(const HoverEvent& e) override {
        hovered = int((e.pos.x - 4) / kKeyCell);
        MenuEntry::onHover(e);
    }
    void onLeave(const LeaveEvent& e) override {
        hovered = -1;
        MenuEntry::onLeave(e);
    }

    void onButton(const ButtonEvent& e) override {
        if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        e.consume(this);
        int i = int((e.pos.x - 4) / kKeyCell);
        const CustomScale* s = scale();
        TesseraModule* m = c->module();
        // The root is always in the scale.
        if (!s || !m || i <= 0 || i >= 12)
            return;
        CustomScale next = *s;
        next.mask = uint16_t(next.mask ^ (1 << i));
        m->perform(putCustomScale(m->song(), next));
    }

  private:
    const CustomScale* scale() const {
        TesseraModule* m = c->module();
        if (!m)
            return nullptr;
        for (const CustomScale& s : m->song().customScales)
            if (s.id == id)
                return &s;
        return nullptr;
    }

    EditorContext* c;
    std::string id;
    int hovered = -1;
};

const char* const kInversionNames[] = {"Root position", "1st inversion", "2nd inversion", "3rd inversion"};

} // namespace

std::string keyLabel(const Song& song, const ScaleRef& key) {
    std::string name = keyName(key, song.customScales);
    return name == "Chromatic" ? "No key" : name;
}

void openCustomScalesMenu(EditorContext& ctx, std::optional<ClipId> clip) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    EditorContext* c = &ctx;
    // The key the menu starts from, read fresh each time so it follows undo: the clip's own, else the song's.
    auto base = [c, clip]() -> ScaleRef {
        TesseraModule* m = c->module();
        if (!m)
            return ScaleRef{};
        const Clip* cl = clip ? m->song().findClip(*clip) : nullptr;
        return cl && cl->scaleOverride ? *cl->scaleOverride : m->song().scale;
    };
    rack::ui::Menu* sub = rack::createMenu();
    sub->addChild(rack::createMenuLabel("Custom scales"));
    sub->addChild(rack::createMenuItem("New scale from this key", "", [c, clip, base] {
        TesseraModule* m = c->module();
        if (!m)
            return;
        const Song& song = m->song();
        ScaleRef k = base();
        CustomScale s{newCustomScaleId(song.customScales), "", resolveScale(k, song.customScales).mask()};
        s.name = "Custom " + s.id.substr(s.id.find('-') + 1);
        auto both = std::make_unique<CompoundCommand>("Add scale " + s.name);
        both->add(putCustomScale(song, s));
        // The new scale becomes the key, so editing it changes what you see straight away.
        k.scale = s.id;
        if (clip)
            both->add(std::make_unique<SetClipField<std::optional<ScaleRef>>>("Clip key", *clip, &Clip::scaleOverride,
                                                                              song.findClip(*clip)->scaleOverride, k));
        else
            both->add(setSongField(song, &Song::scale, k, "Key"));
        m->perform(std::move(both));
    }));
    if (!m->song().customScales.empty())
        sub->addChild(new rack::ui::MenuSeparator);
    for (const CustomScale& s : m->song().customScales) {
        std::string id = s.id;
        sub->addChild(rack::createSubmenuItem(s.name, "", [c, id](rack::ui::Menu* edit) {
            TesseraModule* m = c->module();
            const CustomScale* found = nullptr;
            for (const CustomScale& cs : m ? m->song().customScales : std::vector<CustomScale>{})
                if (cs.id == id)
                    found = &cs;
            if (!found)
                return;
            edit->addChild(rack::createMenuLabel("Name"));
            edit->addChild(new MenuTextField(found->name, [c, id](const std::string& name) {
                TesseraModule* m = c->module();
                if (!m || name.empty())
                    return;
                for (CustomScale cs : m->song().customScales)
                    if (cs.id == id) {
                        cs.name = name;
                        m->perform(putCustomScale(m->song(), cs));
                    }
            }));
            edit->addChild(rack::createMenuLabel("Notes (click to toggle)"));
            edit->addChild(new ScaleKeysEntry(c, id));
            edit->addChild(new rack::ui::MenuSeparator);
            edit->addChild(rack::createMenuItem("Delete scale", "", [c, id] {
                if (TesseraModule* m = c->module())
                    m->perform(deleteCustomScale(m->song(), id));
            }));
        }));
    }
}

std::string shortKeyLabel(const std::string& label) {
    size_t space = label.find(' ');
    if (space == std::string::npos || label == "No key")
        return label;
    std::string root = label.substr(0, space), scale = label.substr(space + 1);
    if (scale == "Minor")
        return root + "m";
    if (scale == "Major")
        return root;
    return root + " " + scale.substr(0, 3);
}

void openChordMenu(EditorContext& ctx) {
    if (ctx.module())
        appendChordItems(rack::createMenu(), ctx);
}

std::string chordLabel(EditorContext& ctx) {
    if (!ctx.module() || !ctx.state().chord.on())
        return "Off";
    for (const ChordShapeInfo& s : chordShapes())
        if (s.shape == ctx.state().chord.shape)
            return s.name;
    return "On";
}

void appendChordItems(rack::ui::Menu* menu, EditorContext& ctx) {
    if (!ctx.module())
        return;
    EditorContext* c = &ctx;
    auto stamp = [c]() -> ChordStamp& { return c->state().chord; };
    menu->addChild(rack::createMenuLabel("Chord stamp: Draw places the whole chord"));
    auto shapeItem = [c, stamp](ChordShape shape, const std::string& name) {
        return rack::createCheckMenuItem(
            name, "", [c, stamp, shape] { return c->module() && stamp().shape == shape; },
            [c, stamp, shape] {
                if (c->module())
                    stamp().shape = shape;
            });
    };
    menu->addChild(shapeItem(ChordShape::Off, "Off (single notes)"));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("In the key (the chord follows the clicked degree)"));
    for (const ChordShapeInfo& s : chordShapes())
        if (s.diatonic)
            menu->addChild(shapeItem(s.shape, s.name));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Fixed shapes"));
    for (const ChordShapeInfo& s : chordShapes())
        if (!s.diatonic)
            menu->addChild(shapeItem(s.shape, s.name));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createSubmenuItem(
        "Inversion", kInversionNames[std::clamp(stamp().inversion, 0, 3)], [c, stamp](rack::ui::Menu* sub) {
            for (int i = 0; i < 4; i++)
                sub->addChild(rack::createCheckMenuItem(
                    kInversionNames[i], "", [c, stamp, i] { return c->module() && stamp().inversion == i; },
                    [c, stamp, i] {
                        if (c->module())
                            stamp().inversion = i;
                    }));
        }));
    static const char* const voicings[] = {"Close", "Open", "Drop 2"};
    menu->addChild(rack::createSubmenuItem("Voicing", voicings[int(stamp().voicing)], [c, stamp](rack::ui::Menu* sub) {
        for (int v = 0; v < 3; v++)
            sub->addChild(rack::createCheckMenuItem(
                voicings[v], "", [c, stamp, v] { return c->module() && int(stamp().voicing) == v; },
                [c, stamp, v] {
                    if (c->module())
                        stamp().voicing = ChordVoicing(v);
                }));
    }));
}

} // namespace tessera::keyActions
