#include "Sources.hpp"

#include "../../model/AudioEdits.hpp"
#include "../../module/OutputModuleBase.hpp"
#include "../../module/TesseraModule.hpp"
#include "../EditorContext.hpp"
#include "../Theme.hpp"
#include "../views/AudioClipDraw.hpp"
#include "AudioActions.hpp"

#include <history.hpp>

#include <map>

namespace tessera::sources {

namespace {

/// The output module with Rack ID `id`, if it is still there.
OutputModuleBase* moduleById(int64_t id) {
    return id >= 0 ? dynamic_cast<OutputModuleBase*>(APP->engine->getModule(id)) : nullptr;
}

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
    menu->addChild(rack::createMenuLabel("As a source of its track"));
    auto* use = new UseItem;
    use->target = target;
    use->name = "Use";
    menu->addChild(use);
    menu->addChild(new LevelSlider(target));
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
