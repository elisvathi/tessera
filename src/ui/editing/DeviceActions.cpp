#include "DeviceActions.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../widgets/MenuTextField.hpp"
#include "../widgets/SearchList.hpp"
#include "ClipActions.hpp"

#include <algorithm>
#include <cmath>

namespace tessera::deviceActions {

namespace {

TesseraModule* moduleOf(EditorContext& ctx) { return ctx.module(); }

std::string trackName(const Song& song, TrackId id) {
    const Track* t = song.findTrack(id);
    return t ? t->name : "Track";
}

/// Groups in the order the browser lists them.
template <typename Type>
std::vector<SearchItem> itemsOf(int count, const std::function<void(Type, int)>& pick) {
    std::vector<SearchItem> items;
    // Groups in the order a musician thinks of them; any other group follows.
    std::vector<std::string> order = {"Pitch", "Rhythm", "Feel", "Motion", "Notes", "Hand"};
    for (int t = 0; t < count; t++) {
        const devices::TypeInfo& info = [&]() -> const devices::TypeInfo& {
            if constexpr (std::is_same_v<Type, DeviceType>)
                return devices::deviceInfo(Type(t));
            else
                return devices::modulatorInfo(Type(t));
        }();
        if (std::find(order.begin(), order.end(), info.group) == order.end())
            order.push_back(info.group);
    }
    for (const std::string& group : order)
        for (int t = 0; t < count; t++) {
            const devices::TypeInfo& info = [&]() -> const devices::TypeInfo& {
                if constexpr (std::is_same_v<Type, DeviceType>)
                    return devices::deviceInfo(Type(t));
                else
                    return devices::modulatorInfo(Type(t));
            }();
            if (info.group != group)
                continue;
            Type type = Type(t);
            items.push_back({info.group, info.name, info.description, info.keywords,
                             [pick, type](int scope) { pick(type, scope); }});
        }
    return items;
}

} // namespace

const Track* track(EditorContext& ctx) {
    TesseraModule* m = moduleOf(ctx);
    return m ? m->song().findTrack(ctx.state().selectedTrack) : nullptr;
}

bool stripOpen(EditorContext& ctx) {
    if (!ctx.view().deviceStrip)
        return false;
    const Track* t = track(ctx);
    if (!t)
        return false;
    return !t->devices.empty() || !t->modulators.empty() || ctx.stripOpenedFor == t->id.value;
}

void setStripOpen(EditorContext& ctx, bool open) {
    ctx.view().deviceStrip = open;
    const Track* t = track(ctx);
    ctx.stripOpenedFor = open && t ? t->id.value : 0;
}

const devices::ParamInfo* infoOf(const Song& song, const ParamRef& ref) {
    if (ref.kind == ParamRef::Kind::Device) {
        const Device* d = findDevice(song, DeviceId(ref.owner));
        return d ? devices::paramInfo(d->type, ref.param) : nullptr;
    }
    const Modulator* m = findModulator(song, ModulatorId(ref.owner));
    return m ? devices::paramInfo(m->type, ref.param) : nullptr;
}

std::string paramName(const Song& song, const ParamRef& ref) {
    const devices::ParamInfo* p = infoOf(song, ref);
    std::string owner;
    if (ref.kind == ParamRef::Kind::Device) {
        if (const Device* d = findDevice(song, DeviceId(ref.owner)))
            owner = d->name;
    } else if (const Modulator* m = findModulator(song, ModulatorId(ref.owner))) {
        owner = m->name;
    }
    return p ? owner + " · " + p->name : "(missing)";
}

std::string targetName(const Song& song, const Modulation& route) {
    switch (route.kind) {
    case Modulation::Kind::AutoChannel:
        return trackName(song, route.track) + " · AUTO " + std::to_string(route.channel + 1);
    case Modulation::Kind::RackKnob:
        return TesseraModule::targetName(route.knob);
    case Modulation::Kind::Param:
    default:
        return paramName(song, route.target);
    }
}

std::string whereName(const Song& song, const ModulatorPlace& place) {
    switch (place.level) {
    case ModLevel::Song:
        return "song";
    case ModLevel::Track:
        return "track";
    case ModLevel::Device:
    default: {
        const Device* d = findDevice(song, place.device);
        return d ? d->name : "device";
    }
    }
}

NVGcolor colorOf(ModulatorId id) { return theme::modulatorColor(id.value); }

void setKnob(EditorContext& ctx, const ParamRef& ref, float value) {
    if (TesseraModule* m = moduleOf(ctx))
        m->perform(setParam(m->song(), ref, value));
}

void openDeviceBrowser(EditorContext& ctx, TrackId trackId, int index) {
    TesseraModule* m = moduleOf(ctx);
    const Track* t = m ? m->song().findTrack(trackId) : nullptr;
    if (!t)
        return;
    EditorContext* c = &ctx;
    std::function<void(DeviceType, int)> pick = [c, trackId, index](DeviceType type, int) {
        if (TesseraModule* mod = c->module()) {
            CommandPtr command = addDevice(mod->song(), trackId, type, index, mod->ids());
            mod->perform(std::move(command));
            setStripOpen(*c, true);
        }
    };
    SearchListOptions options;
    options.placeholder = "Search devices: arp, delay, key…";
    if (index < 0 || index >= int(t->devices.size()))
        options.footer = t->devices.empty() ? "adds the first device" : "adds after " + t->devices.back().name;
    else if (index == 0)
        options.footer = "adds before " + t->devices.front().name;
    else
        options.footer = "adds after " + t->devices[size_t(index - 1)].name;
    if (int(t->devices.size()) >= kMaxDevices)
        options.footer = "this track is full (16 devices)";
    openSearchList(itemsOf<DeviceType>(kDeviceTypeCount, pick), options);
}

void openModulatorBrowser(EditorContext& ctx, TrackId trackId, DeviceId device) {
    TesseraModule* m = moduleOf(ctx);
    const Track* t = m ? m->song().findTrack(trackId) : nullptr;
    if (!t)
        return;
    // The chips name where the new modulator lives; the level decides what it can reach.
    std::vector<ModulatorPlace> places = {{ModLevel::Song, TrackId(), DeviceId()},
                                          {ModLevel::Track, t->id, DeviceId()}};
    SearchListOptions options;
    options.placeholder = "Search modulators: lfo, envelope, build…";
    options.scopes = {"Whole song", t->name};
    options.scope = 1;
    for (const Device& d : t->devices) {
        places.push_back({ModLevel::Device, t->id, d.id});
        options.scopes.push_back(d.name);
        if (d.id == device)
            options.scope = int(places.size()) - 1;
    }
    options.footer = "the chip: where it lives and what it reaches";
    EditorContext* c = &ctx;
    std::function<void(ModulatorType, int)> pick = [c, places](ModulatorType type, int scope) {
        TesseraModule* mod = c->module();
        if (!mod || scope < 0 || scope >= int(places.size()))
            return;
        ModulatorId added;
        CommandPtr command = addModulator(mod->song(), places[size_t(scope)], type, mod->ids(), &added);
        if (!command)
            return;
        mod->perform(std::move(command));
        c->openModulator = added.value;
        setStripOpen(*c, true);
    };
    openSearchList(itemsOf<ModulatorType>(kModulatorTypeCount, pick), options);
}

void openDeviceMenu(EditorContext& ctx, DeviceId id) {
    TesseraModule* m = moduleOf(ctx);
    TrackId trackId;
    const Device* d = m ? findDevice(m->song(), id, &trackId) : nullptr;
    if (!d)
        return;
    EditorContext* c = &ctx;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(d->name));
    menu->addChild(new MenuTextField(d->name, [c, id](const std::string& name) {
        if (TesseraModule* mod = c->module())
            mod->perform(renameDevice(mod->song(), id, name));
    }));
    menu->addChild(rack::createMenuItem("Duplicate", "Ctrl+D", [c, id] {
        if (TesseraModule* mod = c->module())
            mod->perform(duplicateDevice(mod->song(), id, mod->ids()));
    }));
    menu->addChild(rack::createMenuItem("Reset knobs", "", [c, id] {
        TesseraModule* mod = c->module();
        const Device* dev = mod ? findDevice(mod->song(), id) : nullptr;
        if (!dev)
            return;
        auto all = std::make_unique<CompoundCommand>("Reset " + dev->name);
        const devices::TypeInfo& info = devices::deviceInfo(dev->type);
        for (size_t k = 0; k < info.params.size(); k++)
            all->add(setParam(mod->song(), {ParamRef::Kind::Device, id.value, uint16_t(k)},
                              devices::defaultNormalized(info.params[k])));
        if (!all->empty())
            mod->perform(std::move(all));
    }));
    bool pitch = d->type == DeviceType::Transpose || d->type == DeviceType::Scale || d->type == DeviceType::Chord ||
                 d->type == DeviceType::Random;
    const Track* t = m->song().findTrack(trackId);
    if (pitch && t && t->type == TrackType::Drum) {
        bool affect = d->affectDrumLanes;
        menu->addChild(rack::createCheckMenuItem(
            "Change drum lanes", "", [affect] { return affect; },
            [c, id, affect] {
                if (TesseraModule* mod = c->module())
                    mod->perform(setDeviceDrumLanes(mod->song(), id, !affect));
            }));
    }
    menu->addChild(rack::createMenuItem("Add a modulator to this device…", "",
                                        [c, trackId, id] { openModulatorBrowser(*c, trackId, id); }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Delete", "Delete", [c, id] {
        if (TesseraModule* mod = c->module())
            mod->perform(removeDevice(mod->song(), id));
    }));
}

void openKnobMenu(EditorContext& ctx, const ParamRef& ref) {
    TesseraModule* m = moduleOf(ctx);
    if (!m)
        return;
    const Song& song = m->song();
    const devices::ParamInfo* p = infoOf(song, ref);
    const float* value = findParam(song, ref);
    if (!p || !value)
        return;
    EditorContext* c = &ctx;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(std::string(p->name) + " · " + devices::format(*p, *value)));
    float def = devices::defaultNormalized(*p);
    menu->addChild(rack::createMenuItem("Reset to " + devices::format(*p, def), "double-click",
                                        [c, ref, def] { setKnob(*c, ref, def); }));
    menu->addChild(rack::createMenuItem("Type a value…", "", [c, ref] { openTyping(*c, ref); }));

    // Modulators that may reach this knob, and those already on it.
    std::vector<std::pair<ModulatorId, std::string>> can;
    std::vector<std::pair<ModulatorId, int>> on;
    forEachModulator(song, [&](const Modulator& mod, const ModulatorPlace& place) {
        Modulation route;
        route.target = ref;
        for (size_t k = 0; k < mod.routes.size(); k++)
            if (mod.routes[k].kind == Modulation::Kind::Param && mod.routes[k].target == ref)
                on.push_back({mod.id, int(k)});
        if (routeProblem(song, mod.id, route).empty())
            can.push_back({mod.id, mod.name + " (" + whereName(song, place) + ")"});
    });
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createSubmenuItem("Modulate with", "", [c, ref, can](rack::ui::Menu* sub) {
        if (can.empty())
            sub->addChild(rack::createMenuLabel("No modulator can reach this knob"));
        for (const auto& [id, name] : can)
            sub->addChild(rack::createMenuItem(name, "+50%", [c, ref, id = id] {
                TesseraModule* mod = c->module();
                if (!mod)
                    return;
                Modulation route;
                route.target = ref;
                route.amount = 0.5f;
                mod->perform(addRoute(mod->song(), id, route));
            }));
        sub->addChild(new rack::ui::MenuSeparator);
        sub->addChild(rack::createMenuItem("A new modulator…", "", [c] {
            if (const Track* t = track(*c))
                openModulatorBrowser(*c, t->id);
        }));
    }));
    if (!on.empty()) {
        menu->addChild(rack::createSubmenuItem("Remove modulation", "", [c, on](rack::ui::Menu* sub) {
            TesseraModule* mod = c->module();
            if (!mod)
                return;
            for (const auto& [id, index] : on) {
                const Modulator* mm = findModulator(mod->song(), id);
                if (!mm)
                    continue;
                char amount[16];
                snprintf(amount, sizeof(amount), "%+.0f%%", double(mm->routes[size_t(index)].amount) * 100);
                sub->addChild(rack::createMenuItem(mm->name, amount, [c, id = id, index = index] {
                    if (TesseraModule* m2 = c->module())
                        m2->perform(removeRoute(m2->song(), id, index));
                }));
            }
        }));
    }
    // Automation (docs/DEVICES.md §1.3): a lane sets the knob while the song plays.
    menu->addChild(new rack::ui::MenuSeparator);
    std::vector<FoundLane> lanes = lanesTurning(song, ref);
    if (!lanes.empty()) {
        LaneOwner owner = lanes.front().owner;
        AutomationId id = lanes.front().lane->id;
        menu->addChild(rack::createMenuItem("Show its lane", owner.track.valid() ? "arranger" : "clip",
                                            [c, owner, id] { showLane(*c, owner, id); }));
    } else {
        TrackId trackId = trackOf(song, ref);
        if (!trackId.valid())
            if (const Track* t = track(ctx))
                trackId = t->id; // a song modulator's setting: the track on show
        const Track* t = song.findTrack(trackId);
        ClipId clip = t ? clipActions::shownClip(ctx, *t) : ClipId();
        auto* arranger = rack::createMenuItem(
            "Automate in the arranger", "", [c, ref, trackId] { automateKnob(*c, ref, LaneOwner::ofTrack(trackId)); });
        arranger->disabled = !trackId.valid();
        menu->addChild(arranger);
        auto* inClip = rack::createMenuItem("Automate in this clip", "",
                                            [c, ref, clip] { automateKnob(*c, ref, LaneOwner::ofClip(clip)); });
        inClip->disabled = !clip.valid();
        menu->addChild(inClip);
    }
}

void openModulatorMenu(EditorContext& ctx, ModulatorId id) {
    TesseraModule* m = moduleOf(ctx);
    const Modulator* mod = m ? findModulator(m->song(), id) : nullptr;
    if (!mod)
        return;
    EditorContext* c = &ctx;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(mod->name));
    menu->addChild(new MenuTextField(mod->name, [c, id](const std::string& name) {
        if (TesseraModule* m2 = c->module())
            m2->perform(renameModulator(m2->song(), id, name));
    }));
    menu->addChild(rack::createMenuItem("Map to knobs", "", [c, id] {
        c->mappingModulator = id.value;
        c->openModulator = 0;
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Remove", "", [c, id] {
        if (TesseraModule* m2 = c->module()) {
            m2->perform(removeModulator(m2->song(), id));
            if (c->openModulator == id.value)
                c->openModulator = 0;
            if (c->mappingModulator == id.value)
                c->mappingModulator = 0;
        }
    }));
}

void openChoiceMenu(EditorContext& ctx, const ParamRef& ref) {
    TesseraModule* m = moduleOf(ctx);
    if (!m)
        return;
    const devices::ParamInfo* p = infoOf(m->song(), ref);
    const float* value = findParam(m->song(), ref);
    if (!p || !value || p->kind != devices::ParamInfo::Kind::Choice)
        return;
    EditorContext* c = &ctx;
    int current = int(devices::display(*p, *value));
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(p->name));
    for (size_t i = 0; i < p->choices.size(); i++) {
        float n = devices::normalize(*p, float(i));
        menu->addChild(rack::createCheckMenuItem(
            p->choices[i], "", [current, i] { return current == int(i); }, [c, ref, n] { setKnob(*c, ref, n); }));
    }
}

void openTyping(EditorContext& ctx, const ParamRef& ref) {
    TesseraModule* m = moduleOf(ctx);
    if (!m)
        return;
    const devices::ParamInfo* p = infoOf(m->song(), ref);
    const float* value = findParam(m->song(), ref);
    if (!p || !value)
        return;
    EditorContext* c = &ctx;
    devices::ParamInfo info = *p;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(std::string(p->name) + (p->unit[0] ? std::string(" (") + p->unit + ")" : "")));
    char current[32];
    if (p->kind == devices::ParamInfo::Kind::Choice)
        snprintf(current, sizeof(current), "%s", devices::format(*p, *value).c_str());
    else
        snprintf(current, sizeof(current), "%g", std::round(double(devices::display(*p, *value)) * 100) / 100);
    auto* field = new MenuTextField(current, [c, ref, info](const std::string& text) {
        float n = -1;
        if (info.kind == devices::ParamInfo::Kind::Choice) {
            for (size_t i = 0; i < info.choices.size(); i++)
                if (info.choices[i] == text)
                    n = devices::normalize(info, float(i));
        } else {
            char* end = nullptr;
            double v = std::strtod(text.c_str(), &end);
            if (end != text.c_str())
                n = devices::normalize(info, float(v));
        }
        if (n >= 0)
            setKnob(*c, ref, n);
    });
    menu->addChild(field);
    APP->event->setSelectedWidget(field);
}

void openCvOutMenu(EditorContext& ctx, ModulatorId id) {
    TesseraModule* m = moduleOf(ctx);
    ModulatorPlace place;
    const Modulator* mod = m ? findModulator(m->song(), id, &place) : nullptr;
    if (!mod)
        return;
    EditorContext* c = &ctx;
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Send " + mod->name + " out of the rack"));
    auto channels = [c, id](rack::ui::Menu* sub, TrackId trackId) {
        TesseraModule* m2 = c->module();
        const Track* t = m2 ? m2->song().findTrack(trackId) : nullptr;
        if (!t)
            return;
        for (int ch = 0; ch < kMaxAutomationLanes; ch++) {
            std::string label = "AUTO " + std::to_string(ch + 1);
            for (const AutomationEnvelope& lane : t->automation)
                if (lane.outputIndex == ch && !lane.paramTarget.valid())
                    label += "  (" + lane.name + ")";
            sub->addChild(rack::createMenuItem(label, "", [c, id, trackId, ch] {
                TesseraModule* m3 = c->module();
                if (!m3)
                    return;
                Modulation route;
                route.kind = Modulation::Kind::AutoChannel;
                route.track = trackId;
                route.channel = int8_t(ch);
                route.amount = 1;
                m3->perform(addRoute(m3->song(), id, route));
            }));
        }
    };
    if (place.level == ModLevel::Song) {
        menu->addChild(rack::createSubmenuItem("AUTO output of", "", [c, channels](rack::ui::Menu* sub) {
            TesseraModule* m2 = c->module();
            if (!m2)
                return;
            for (const Track& t : m2->song().tracks) {
                TrackId tid = t.id;
                sub->addChild(
                    rack::createSubmenuItem(t.name, "", [channels, tid](rack::ui::Menu* s) { channels(s, tid); }));
            }
        }));
    } else {
        TrackId tid = place.track;
        menu->addChild(
            rack::createSubmenuItem("AUTO output", "", [channels, tid](rack::ui::Menu* sub) { channels(sub, tid); }));
    }
    menu->addChild(rack::createMenuItem("A knob in the rack (touch it next)", "", [c, id] {
        if (TesseraModule* m2 = c->module())
            m2->startLearn(id);
    }));
}

NVGcolor laneColor(const Song& song, const FoundLane& found) {
    if (found.lane && found.lane->color)
        return theme::track(found.lane->color);
    TrackId track = found.owner.track.valid() ? found.owner.track : clipActions::trackOf(song, found.owner.clip);
    const Track* t = song.findTrack(track);
    return t ? theme::track(t->color) : theme::colors().accent;
}

void automateKnob(EditorContext& ctx, const ParamRef& ref, LaneOwner owner) {
    TesseraModule* m = moduleOf(ctx);
    if (!m || !owner.valid())
        return;
    const Song& song = m->song();
    const devices::ParamInfo* info = infoOf(song, ref);
    const float* value = findParam(song, ref);
    if (!info || !value)
        return;
    using Kind = devices::ParamInfo::Kind;
    bool steps = info->kind == Kind::Choice || info->kind == Kind::Toggle || info->kind == Kind::Integer;
    AutomationEnvelope lane = makeKnobLane(m->newId<AutomationId>(), ref, paramName(song, ref), *value, steps, 0);
    CommandPtr command = addLane(song, owner, lane);
    if (!command)
        return;
    m->perform(std::move(command));
    showLane(ctx, owner, lane.id);
}

void showLane(EditorContext& ctx, LaneOwner owner, AutomationId lane) {
    TesseraModule* m = moduleOf(ctx);
    if (!m)
        return;
    if (owner.track.valid()) {
        if (ctx.view().maximized == Pane::Detail)
            ctx.view().maximized.reset();
        ctx.view().openAutomation.insert(owner.track.value);
        return;
    }
    TrackId track = clipActions::trackOf(m->song(), owner.clip);
    if (track.valid())
        ctx.state().selectedTrack = track;
    ctx.state().selectedClip = owner.clip;
    ctx.view().lowerLane = lane.value;
}

} // namespace tessera::deviceActions
