#include "Sources.hpp"

#include "../../model/AudioEdits.hpp"
#include "../../module/OutputModuleBase.hpp"
#include "../../module/TesseraModule.hpp"
#include "../EditorContext.hpp"
#include "../Theme.hpp"
#include "../views/AudioClipDraw.hpp"
#include "AudioActions.hpp"

#include <history.hpp>

#include <algorithm>
#include <map>
#include <tuple>

namespace tessera::sources {

OutputModuleBase* moduleById(int64_t id) {
    return id >= 0 ? dynamic_cast<OutputModuleBase*>(APP->engine->getModule(id)) : nullptr;
}

namespace {

/// A module's Level and Use change as one Rack undo step: they live in its patch data.
struct LevelAction : rack::history::ModuleAction {
    SourceLevel before, after;
    void undo() override { apply(before); }
    void redo() override { apply(after); }
    void apply(SourceLevel level) const {
        if (OutputModuleBase* o = moduleById(moduleId))
            o->setSourceSetting(level);
    }
};

/// Where a Level slider reads and writes: one module's setting, or a track's clips (the master's mix).
struct Target {
    int64_t module = -1;
    int64_t tessera = -1;
    TrackId track;

    TesseraModule* tesseraModule() const {
        return tessera >= 0 ? dynamic_cast<TesseraModule*>(APP->engine->getModule(tessera)) : nullptr;
    }
    SourceLevel level() const {
        if (module >= 0) {
            OutputModuleBase* o = moduleById(module);
            return o ? o->sourceSetting() : SourceLevel{};
        }
        TesseraModule* m = tesseraModule();
        if (!m)
            return {};
        if (track == m->song().master.id)
            return m->song().master.mix;
        const Track* t = m->song().findTrack(track);
        return t ? t->clips : SourceLevel{};
    }
    /// Plays `db` at once, before it is committed.
    void preview(float db) const {
        if (module >= 0) {
            if (OutputModuleBase* o = moduleById(module))
                o->holdSourceLevel(db);
        } else if (TesseraModule* m = tesseraModule()) {
            m->audioPlayer().holdClips(track.value, level().use ? dbToGain(db) : 0.f);
        }
    }
    void commit(SourceLevel next, const std::string& name) const {
        if (module >= 0) {
            OutputModuleBase* o = moduleById(module);
            if (!o)
                return;
            o->holdSourceLevel(std::nullopt);
            SourceLevel before = o->sourceSetting();
            if (before == next)
                return;
            o->setSourceSetting(next);
            auto* undo = new LevelAction;
            undo->moduleId = module;
            undo->name = name;
            undo->before = before;
            undo->after = next;
            APP->history->push(undo);
            return;
        }
        TesseraModule* m = tesseraModule();
        if (!m)
            return;
        if (CommandPtr c = setSongSource(m->song(), track, next, name))
            m->perform(std::move(c));
        m->releaseChannelHolds();
    }
};

/// A source's Level as a fader position (0..1, the mixer's taper), shown in dB.
struct LevelQuantity : rack::Quantity {
    Target target;
    float pos = 0;

    float getValue() override { return pos; }
    void setValue(float value) override {
        pos = rack::math::clamp(value, 0.f, 1.f);
        target.preview(faderDbOfPos(pos));
    }
    float getDefaultValue() override { return faderPosOfDb(0); }
    float getMinValue() override { return 0; }
    float getMaxValue() override { return 1; }
    std::string getLabel() override { return "Level"; }
    std::string getDisplayValueString() override { return audioDraw::dbText(faderDbOfPos(pos)); }
};

/// A Level slider that plays while it moves and is one undo step on release; a double-click resets to 0 dB.
struct LevelSlider : rack::ui::Slider {
    LevelQuantity level;

    explicit LevelSlider(Target target) {
        level.target = target;
        level.pos = faderPosOfDb(target.level().db);
        quantity = &level;
        box.size.x = 220;
    }
    void commit() {
        SourceLevel next = level.target.level();
        next.db = faderDbOfPos(level.pos);
        level.target.commit(next, "Source level " + audioDraw::dbText(next.db));
    }
    void step() override {
        // As wide as the menu it sits in, like the items around it.
        if (parent)
            box.size.x = std::max(box.size.x, parent->box.size.x);
        Slider::step();
    }
    void onDragEnd(const DragEndEvent& e) override {
        Slider::onDragEnd(e);
        commit();
    }
    void onDoubleClick(const DoubleClickEvent& e) override {
        Slider::onDoubleClick(e);
        commit();
    }
};

/// A source's Use, its name and a small live meter; a click toggles Use and keeps the list open.
struct UseItem : rack::ui::MenuItem {
    Target target;
    std::string name;

    void step() override {
        SourceLevel l = target.level();
        text = name;
        // An effect loop's amount is its Mix, shown on its own slider.
        OutputModuleBase* o = moduleById(target.module);
        if (o && o->isLoop())
            rightText = CHECKMARK(l.use);
        else
            rightText = (l.use ? audioDraw::dbText(l.db) : std::string("off")) + "  " + CHECKMARK(l.use);
        MenuItem::step();
        box.size.x = std::max(box.size.x, 240.f);
    }
    void draw(const DrawArgs& args) override {
        MenuItem::draw(args);
        OutputModuleBase* o = moduleById(target.module);
        if (!o)
            return;
        // What it brings in now, -60..+6 dB, left of the level text.
        float db = gainToDb(o->inputLevel() / kFullScaleVolts);
        float fill = rack::math::clamp((db + 60) / 66, 0.f, 1.f);
        rack::math::Rect r(box.size.x - 120, box.size.y / 2 - 2, 40, 4);
        nvgBeginPath(args.vg);
        nvgRect(args.vg, r.pos.x, r.pos.y, r.size.x, r.size.y);
        const theme::Palette& c = theme::colors();
        nvgFillColor(args.vg, theme::recessed(c, c.bg0, 0.5f));
        nvgFill(args.vg);
        if (fill > 0) {
            nvgBeginPath(args.vg);
            nvgRect(args.vg, r.pos.x, r.pos.y, r.size.x * fill, r.size.y);
            nvgFillColor(args.vg, db > 0 ? c.record : c.play);
            nvgFill(args.vg);
        }
    }
    void onAction(const ActionEvent& e) override {
        SourceLevel next = target.level();
        next.use = !next.use;
        target.commit(next, next.use ? "Use source" : "Don't use source");
        e.consume(this);
    }
};

/// A loop's Mix change as one Rack undo step: it lives in the module's patch data.
struct MixAction : rack::history::ModuleAction {
    float before = 1, after = 1;
    void undo() override {
        if (OutputModuleBase* o = moduleById(moduleId))
            o->setMix(before);
    }
    void redo() override {
        if (OutputModuleBase* o = moduleById(moduleId))
            o->setMix(after);
    }
};

/// A new order of a track's chain as one Rack undo step: each module's place before and after.
struct ChainOrderAction : rack::history::Action {
    std::vector<std::tuple<int64_t, int, int>> places; // module, before, after
    void undo() override { apply(false); }
    void redo() override { apply(true); }
    void apply(bool after) const {
        for (const auto& [id, before, now] : places)
            if (OutputModuleBase* o = moduleById(id))
                o->setChainOrder(after ? now : before);
    }
};

/// A loop's Mix as a slider, 0..100%: it plays while it moves and is one undo step on release.
struct MixQuantity : rack::Quantity {
    int64_t module = -1;
    float value = 1;

    float getValue() override { return value; }
    void setValue(float v) override {
        value = rack::math::clamp(v, 0.f, 1.f);
        previewMix(module, value);
    }
    float getDefaultValue() override { return 1; }
    float getMinValue() override { return 0; }
    float getMaxValue() override { return 1; }
    std::string getLabel() override { return "Mix"; }
    std::string getDisplayValueString() override {
        char text[16];
        snprintf(text, sizeof text, "%.0f%%", double(value * 100));
        return text;
    }
};

/// The slider for MixQuantity; a double-click resets it to 100%.
struct MixSlider : rack::ui::Slider {
    MixQuantity mix;

    explicit MixSlider(int64_t module) {
        mix.module = module;
        mix.value = mixOf(module);
        quantity = &mix;
        box.size.x = 220;
    }
    void step() override {
        if (parent)
            box.size.x = std::max(box.size.x, parent->box.size.x);
        Slider::step();
    }
    void onDragEnd(const DragEndEvent& e) override {
        Slider::onDragEnd(e);
        commitMix(mix.module, mix.value);
    }
    void onDoubleClick(const DoubleClickEvent& e) override {
        Slider::onDoubleClick(e);
        commitMix(mix.module, mix.value);
    }
};

} // namespace

std::vector<Row> rowsOf(TesseraModule& m, TrackId track) {
    std::vector<Row> rows;
    const Song& song = m.song();
    std::vector<OutputModuleBase*> modules = m.sourceModules(track);
    if (const Track* t = song.findTrack(track); t && t->type == TrackType::Audio)
        rows.push_back({nullptr, "Clips", t->clips, false});
    else if (track == song.master.id && !modules.empty())
        rows.push_back({nullptr, "Mix", song.master.mix, false});
    std::map<std::string, int> total, seen;
    for (OutputModuleBase* o : modules)
        total[audioActions::outputName(o)]++;
    for (OutputModuleBase* o : modules) {
        std::string name = audioActions::outputName(o);
        if (total[name] > 1)
            name += " " + std::to_string(++seen[name]);
        rows.push_back({o, name, o->sourceSetting(), o->isCountedOnce()});
    }
    return rows;
}

bool needsList(const std::vector<Row>& rows) {
    if (rows.size() >= 2)
        return true;
    for (const Row& r : rows)
        if (!r.level.use || r.level.db != 0)
            return true;
    return false;
}

void set(TesseraModule& m, TrackId track, const Row& row, SourceLevel level, const std::string& name) {
    Target t{row.module ? row.module->id : -1, m.id, track};
    t.commit(level, name);
}

void openList(EditorContext& ctx, TrackId track) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    const Song& song = m->song();
    const Track* t = song.findTrack(track);
    bool master = track == song.master.id;
    if (!t && !master)
        return;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel((t ? t->name : std::string("Master")) + " · sources"));
    for (const Row& row : rowsOf(*m, track)) {
        menu->addChild(new rack::ui::MenuSeparator);
        Target target{row.module ? row.module->id : -1, m->id, track};
        if (row.countedOnce) {
            menu->addChild(rack::createMenuLabel(row.name + ": same cable as another source · counted once"));
        } else {
            auto* use = new UseItem;
            use->target = target;
            use->name = row.name;
            menu->addChild(use);
            if (row.module && row.module->isLoop())
                menu->addChild(new MixSlider(row.module->id));
            else
                menu->addChild(new LevelSlider(target));
        }
        if (row.module) {
            int64_t id = row.module->id;
            menu->addChild(rack::createMenuItem("Show " + row.name + " in the rack", "", [id] {
                audioActions::showInRack(moduleById(id));
            }));
        }
    }
    menu->addChild(new rack::ui::MenuSeparator);
    bool drum = t && t->type == TrackType::Drum;
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuItem(
        std::string(drum ? "Add a Drum Out" : "Add a Track Out") + (t ? " for this track" : " for the master"), "",
        [c, m, track] { c->toast(audioActions::addOutputFor(m, track)); }));
}

void appendModuleItems(rack::ui::Menu* menu, OutputModuleBase* module) {
    if (!module || !module->source())
        return;
    Target target{module->id, module->source()->id, module->boundTrack()};
    menu->addChild(new rack::ui::MenuSeparator);
    bool loop = module->isLoop();
    menu->addChild(rack::createMenuLabel(loop ? "As an effect loop of its track" : "As a source of its track"));
    auto* use = new UseItem;
    use->target = target;
    use->name = loop ? "On" : "Use";
    menu->addChild(use);
    if (loop)
        menu->addChild(new MixSlider(module->id));
    else
        menu->addChild(new LevelSlider(target));
}

bool isLoop(int64_t module) {
    OutputModuleBase* o = moduleById(module);
    return o && o->isLoop();
}

float mixOf(int64_t module) {
    OutputModuleBase* o = moduleById(module);
    return o ? o->shownMix() : 1.f;
}

void previewMix(int64_t module, std::optional<float> mix) {
    if (OutputModuleBase* o = moduleById(module))
        o->holdMix(mix);
}

void commitMix(int64_t module, float mix) {
    OutputModuleBase* o = moduleById(module);
    if (!o)
        return;
    o->holdMix(std::nullopt);
    mix = rack::math::clamp(mix, 0.f, 1.f);
    float before = o->mixAmount();
    if (before == mix)
        return;
    o->setMix(mix);
    auto* undo = new MixAction;
    undo->moduleId = module;
    undo->name = "Effect " + mixText(mix);
    undo->before = before;
    undo->after = mix;
    APP->history->push(undo);
}

std::string mixText(float mix) {
    char text[24];
    snprintf(text, sizeof text, "Mix %.0f%%", double(mix * 100));
    return text;
}

void moveInChain(TesseraModule& m, TrackId track, int64_t module, int64_t before) {
    std::vector<OutputModuleBase*> chain = m.chainModules(track);
    auto it = std::find_if(chain.begin(), chain.end(), [&](OutputModuleBase* o) { return o->id == module; });
    if (it == chain.end() || module == before)
        return;
    OutputModuleBase* moving = *it;
    chain.erase(it);
    auto at = std::find_if(chain.begin(), chain.end(), [&](OutputModuleBase* o) { return o->id == before; });
    chain.insert(at, moving);
    auto* undo = new ChainOrderAction;
    undo->name = "Reorder the chain";
    bool changed = false;
    for (size_t k = 0; k < chain.size(); k++) {
        int before = chain[k]->chainOrder();
        undo->places.push_back({chain[k]->id, before, int(k)});
        changed |= before != int(k);
    }
    if (!changed) {
        delete undo;
        return;
    }
    undo->redo();
    APP->history->push(undo);
}

std::string chainLabel(OutputModuleBase* module, const std::string& fallback) {
    if (!module || !module->isLoop())
        return fallback;
    for (int64_t cableId : APP->engine->getCableIds())
        if (rack::engine::Cable* c = APP->engine->getCable(cableId))
            if (c->outputModule == module && module->isOutJack(c->outputId) && c->inputModule &&
                c->inputModule != module && c->inputModule->model)
                return c->inputModule->model->name;
    return "FX";
}

SourceLevel inputLevel(EditorContext& ctx, TrackId track, int64_t module) {
    return Target{module, ctx.module()->id, track}.level();
}

void commitInput(EditorContext& ctx, TrackId track, int64_t module, SourceLevel next, const std::string& name) {
    Target{module, ctx.module()->id, track}.commit(next, name);
}

void previewInput(EditorContext& ctx, TrackId track, int64_t module, float db, float pan) {
    Target{module, ctx.module()->id, track}.preview(db);
    if (module < 0)
        ctx.module()->audioPlayer().holdClipsPan(track.value, pan);
    else if (OutputModuleBase* o = moduleById(module))
        o->holdSourcePan(pan);
}

void endInputPreview(EditorContext& ctx, TrackId track, int64_t module) {
    if (OutputModuleBase* o = moduleById(module)) {
        o->holdSourceLevel(std::nullopt);
        o->holdSourcePan(std::nullopt);
    } else if (module < 0) {
        ctx.module()->releaseChannelHolds();
    }
}

bool anyInputSolo(EditorContext& ctx, TrackId track) {
    TesseraModule* m = ctx.module();
    const Track* t = m->song().findTrack(track);
    if (t && t->clips.solo)
        return true;
    for (OutputModuleBase* o : m->sourceModules(track))
        if (o->sourceSetting().solo)
            return true;
    return false;
}

} // namespace tessera::sources
