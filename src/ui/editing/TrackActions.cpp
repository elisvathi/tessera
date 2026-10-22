#include "TrackActions.hpp"

#include "../widgets/MenuTextField.hpp"

#include "../../model/TrackColors.hpp"
#include "../../module/TesseraModule.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../interaction/TrackRows.hpp"
#include "../widgets/ColorMenuItem.hpp"

#include <algorithm>

namespace tessera::trackActions {

std::string voicesLabel(const Song& song, const Track& track) {
    std::string count = std::to_string(song.voicesOf(track));
    return track.polyphony == kAutoVoices ? "Auto (" + count + ")" : count;
}

namespace {

bool contains(const std::vector<TrackId>& ids, TrackId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::string countTracks(size_t n) { return std::to_string(n) + (n == 1 ? " track" : " tracks"); }

/// Changes one group (name, colour) as an undoable layout change.
void changeGroup(EditorContext& ctx, GroupId id, const std::string& name, const std::function<void(TrackGroup&)>& f) {
    TesseraModule* m = ctx.module();
    TrackLayout before = TrackLayout::of(m->song());
    TrackLayout after = before;
    for (TrackGroup& g : after.groups)
        if (g.id == id)
            f(g);
    if (!(after == before))
        m->perform(std::make_unique<ChangeTrackLayout>(name, before, after));
}

template <typename T>
void setField(EditorContext& ctx, const std::vector<TrackId>& ids, T Track::* field, T value, const std::string& name) {
    TesseraModule* m = ctx.module();
    auto compound = std::make_unique<CompoundCommand>(name);
    for (TrackId id : ids)
        compound->add(setTrackField(m->song(), id, field, value, name));
    if (!compound->empty())
        m->perform(std::move(compound));
}

bool trackFlag(EditorContext& ctx, TrackId id, bool Track::* flag) {
    const Track* t = ctx.module() ? ctx.module()->song().findTrack(id) : nullptr;
    return t && t->*flag;
}

} // namespace

std::vector<TrackId> targets(EditorContext& ctx, TrackId clicked) {
    const std::vector<TrackId>& sel = ctx.state().selectedTracks;
    return contains(sel, clicked) ? sel : std::vector<TrackId>{clicked};
}

std::vector<TrackId> selection(EditorContext& ctx) {
    const EditorState& s = ctx.state();
    if (!s.selectedTracks.empty())
        return s.selectedTracks;
    return s.selectedTrack.valid() ? std::vector<TrackId>{s.selectedTrack} : std::vector<TrackId>{};
}

void click(EditorContext& ctx, TrackId id, bool range, bool toggle) {
    EditorState& s = ctx.state();
    const Song& song = ctx.module()->song();
    if (range && s.selectedTrack.valid())
        s.selectedTracks = trackRange(song, s.selectedTrack, id);
    else if (toggle)
        s.selectedTracks =
            toggled(s.selectedTracks.empty() ? std::vector<TrackId>{s.selectedTrack} : s.selectedTracks, id);
    else
        s.selectedTracks = {id};
    // The anchor stays put for Shift ranges; otherwise the clicked track is the one the piano roll shows. A track
    // Ctrl/Cmd+clicked out of the selection hands that over to one still selected, so M or S act on the selection.
    if (range)
        return;
    if (!toggle || contains(s.selectedTracks, id))
        s.selectedTrack = id;
    else if (!s.selectedTracks.empty() && !contains(s.selectedTracks, s.selectedTrack))
        s.selectedTrack = s.selectedTracks.back();
}

bool canAdd(const Song& song, size_t count) { return song.tracks.size() + count <= size_t(kMaxTracks); }

void add(EditorContext& ctx, TrackType type) {
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    int at = song.trackIndex(ctx.state().selectedTrack);
    at = at < 0 ? int(song.tracks.size()) : at + 1;
    uint64_t first = 0;
    NextId ids = m->ids();
    CommandPtr command = addTrack(song, type, at, [&] {
        uint64_t v = ids();
        if (!first)
            first = v;
        return v;
    });
    if (!command) {
        ctx.toast(kTracksFull);
        return;
    }
    m->perform(std::move(command));
    ctx.state().selectedTrack = TrackId(first);
    ctx.state().selectedTracks = {TrackId(first)};
}

void duplicateSelected(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    std::vector<TrackId> ids = selection(ctx);
    if (!canAdd(m->song(), ids.size())) {
        ctx.toast(kTracksFull);
        return;
    }
    m->perform(duplicateTracks(m->song(), ids, m->ids()));
}

void deleteSelected(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    std::vector<TrackId> doomed = selection(ctx);
    int index = m->song().trackIndex(ctx.state().selectedTrack);
    CommandPtr command = removeTracks(m->song(), doomed);
    if (!command)
        return;
    m->perform(std::move(command));
    // Select the track that took the deleted one's place.
    const Song& song = m->song();
    int next = std::clamp(index, 0, int(song.tracks.size()) - 1);
    ctx.state().selectedTrack = song.tracks[size_t(next)].id;
    ctx.state().selectedTracks = {ctx.state().selectedTrack};
}

void groupSelected(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    std::vector<TrackId> ids = selection(ctx);
    if (ids.empty())
        return;
    const Song& song = m->song();
    const Track* first = song.findTrack(ids.front());
    std::string name = "Group " + std::to_string(song.groups.size() + 1);
    m->perform(groupTracks(song, ids, m->newId<GroupId>(), name, first ? first->color : kTrackColors[0]));
}

void toggleMute(EditorContext& ctx, TrackId clicked) {
    TesseraModule* m = ctx.module();
    std::vector<TrackId> ids = targets(ctx, clicked);
    // Mute all when any is playing, otherwise unmute all.
    bool mute = false;
    for (TrackId id : ids)
        if (const Track* t = m->song().findTrack(id); t && !t->mute)
            mute = true;
    const Track* t = m->song().findTrack(clicked);
    std::string name = (mute ? "Mute " : "Unmute ") + (ids.size() == 1 && t ? t->name : countTracks(ids.size()));
    setField(ctx, ids, &Track::mute, mute, name);
}

namespace {

/// Turns a flag on for `ids` (off for every other track unless `additive`), or off when all of them have it.
/// With `clearOthersWhenOff`, a non-additive toggle clears the other tracks when turning off too.
CommandPtr exclusiveToggle(const Song& song, const std::vector<TrackId>& ids, bool additive, bool Track::* flag,
                           const std::string& on, const std::string& off, bool clearOthersWhenOff = false) {
    if (ids.empty())
        return nullptr;
    bool turningOn = false;
    for (TrackId id : ids)
        if (const Track* t = song.findTrack(id); t && !(t->*flag))
            turningOn = true;
    const Track* first = song.findTrack(ids.front());
    std::string what = ids.size() == 1 && first ? first->name : countTracks(ids.size());
    auto compound = std::make_unique<CompoundCommand>((turningOn ? on : off) + " " + what);
    for (TrackId id : ids)
        compound->add(setTrackField(song, id, flag, turningOn, on));
    if (!additive && (turningOn || clearOthersWhenOff))
        for (const Track& t : song.tracks)
            if (!contains(ids, t.id))
                compound->add(setTrackField(song, t.id, flag, false, off));
    return compound->empty() ? nullptr : std::move(compound);
}

} // namespace

CommandPtr soloCommand(const Song& song, const std::vector<TrackId>& ids, bool additive) {
    return exclusiveToggle(song, ids, additive, &Track::solo, "Solo", "Unsolo", true);
}

void toggleArm(EditorContext& ctx, TrackId clicked, bool additive) {
    TesseraModule* m = ctx.module();
    m->perform(exclusiveToggle(m->song(), targets(ctx, clicked), additive, &Track::armed, "Arm", "Disarm"));
}

AutomationId addAutomationLane(EditorContext& ctx, TrackId track) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(track) : nullptr;
    if (!t)
        return AutomationId();
    int output = freeOutput(t->automation);
    if (output < 0)
        return AutomationId(); // all 8 AUTO channels have a lane
    AutomationEnvelope lane = makeLane(m->newId<AutomationId>(), output, 0);
    m->perform(addLane(m->song(), LaneOwner::ofTrack(track), lane));
    ctx.view().openAutomation.insert(track.value);
    return lane.id;
}

const std::vector<MidiControl>& commonMidiControls() {
    using K = MidiControl::Kind;
    static const std::vector<MidiControl> controls = {{K::PitchBend, 0}, {K::Pressure, 0}, {K::Cc, 1},
                                                      {K::Cc, 2},        {K::Cc, 4},       {K::Cc, 7},
                                                      {K::Cc, 11},       {K::Cc, 64},      {K::Cc, 74}};
    return controls;
}

std::string midiControlMenuName(MidiControl control) {
    std::string name = midiControlName(control);
    if (control.kind == MidiControl::Kind::PitchBend)
        return name + " (on V/OCT)";
    if (control.kind == MidiControl::Kind::Pressure)
        return "Pressure, channel aftertouch (on PRESS)";
    return name;
}

void addMidiLane(EditorContext& ctx, TrackId track, LaneOwner owner, MidiControl control) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(track) : nullptr;
    const std::vector<AutomationEnvelope>* lanes = m ? lanesOf(m->song(), owner) : nullptr;
    if (!t || !lanes || !control.valid())
        return;
    auto show = [&](AutomationId id) {
        if (owner.clip.valid())
            ctx.view().lowerLane = id.value;
        else
            ctx.view().openAutomation.insert(track.value);
    };
    for (const AutomationEnvelope& l : *lanes)
        if (l.midi == control) {
            show(l.id);
            return;
        }
    int output = 0;
    if (control.kind == MidiControl::Kind::Cc) {
        output = midiLaneOutput(m->song(), *t, control, *lanes);
        if (output < 0) {
            ctx.toast("All 8 AUTO channels are in use on this track");
            return;
        }
    }
    AutomationEnvelope lane = makeMidiLane(m->newId<AutomationId>(), control, output, 0);
    m->perform(addLane(m->song(), owner, lane));
    show(lane.id);
}

void appendAddLaneItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId track, LaneOwner owner) {
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuItem("Empty lane (draw it)", "", [c, track, owner] {
        if (owner.track.valid()) {
            addAutomationLane(*c, track);
            return;
        }
        TesseraModule* m = c->module();
        const Clip* clip = m ? m->song().findClip(owner.clip) : nullptr;
        int output = clip ? freeOutput(clip->automation) : -1;
        if (output < 0)
            return;
        AutomationEnvelope lane = makeLane(m->newId<AutomationId>(), output, 0);
        m->perform(addLane(m->song(), owner, lane));
        c->view().lowerLane = lane.id.value;
    }));
    if (owner.track.valid())
        menu->addChild(
            rack::createMenuItem("Automate a knob: touch it in the rack", "", [c, track] { automateKnob(*c, track); }));
    menu->addChild(rack::createSubmenuItem("MIDI control", "", [c, track, owner](rack::ui::Menu* sub) {
        sub->addChild(rack::createMenuLabel("Records it when the track records"));
        for (MidiControl control : commonMidiControls())
            sub->addChild(rack::createMenuItem(midiControlMenuName(control), "",
                                               [c, track, owner, control] { addMidiLane(*c, track, owner, control); }));
        sub->addChild(new rack::ui::MenuSeparator);
        sub->addChild(rack::createMenuLabel("Other CC: type its number, then Enter"));
        sub->addChild(new MenuTextField("", [c, track, owner](const std::string& text) {
            char* end = nullptr;
            long n = std::strtol(text.c_str(), &end, 10);
            if (end != text.c_str() && n >= 0 && n <= 127)
                addMidiLane(*c, track, owner, {MidiControl::Kind::Cc, uint8_t(n)});
            else
                c->toast("A CC number is 0 to 127");
        }));
    }));
}

void automateKnob(EditorContext& ctx, TrackId track) {
    AutomationId lane = addAutomationLane(ctx, track);
    TesseraModule* m = ctx.module();
    if (!m || !lane.valid()) {
        ctx.toast("All 8 AUTO channels have a lane");
        return;
    }
    m->startLearn(track, lane);
    ctx.toast("Touch a knob in the rack: the new lane turns it and records it");
}

namespace {

/// Sets what a track listens to (and, for the external inputs, which of them) as one undo step.
void setListen(EditorContext& ctx, TrackId id, ListenSource listen, TrackId from, std::optional<RecordInput> input) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    auto compound = std::make_unique<CompoundCommand>("Set track input");
    compound->add(setTrackField(m->song(), id, &Track::listen, listen, "Set track input"));
    compound->add(setTrackField(m->song(), id, &Track::listenTrack, from, "Set track input"));
    if (input)
        compound->add(setTrackField(m->song(), id, &Track::recordInput, *input, "Set track input"));
    if (!compound->empty())
        m->perform(std::move(compound));
}

/// True when `from` already listens to `to`, directly or through other tracks: listening back would loop.
bool listensTo(const Song& song, TrackId from, TrackId to) {
    TrackId at = from;
    for (int step = 0; step < kMaxTracks; step++) {
        const Track* t = song.findTrack(at);
        if (!t || (t->listen != ListenSource::TrackAfter && t->listen != ListenSource::TrackBefore))
            return false;
        if (t->listenTrack == to)
            return true;
        at = t->listenTrack;
    }
    return true;
}

} // namespace

std::string inputLabel(const Song& song, const Track& track) {
    static const char* const external[] = {"External (any)", "External (REC CV)", "External (MIDI)"};
    switch (track.listen) {
    case ListenSource::External:
        return external[int(track.recordInput)];
    case ListenSource::TrackAfter:
    case ListenSource::TrackBefore: {
        const Track* from = song.findTrack(track.listenTrack);
        if (!from)
            return "Missing track";
        return from->name + (track.listen == ListenSource::TrackAfter ? " (after devices)" : " (before devices)");
    }
    case ListenSource::None:
    default:
        return "None";
    }
}

void appendInputItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId id) {
    TesseraModule* m = ctx.module();
    const Track* track = m ? m->song().findTrack(id) : nullptr;
    if (!track)
        return;
    EditorContext* c = &ctx;
    auto is = [c, id](ListenSource listen, TrackId from, std::optional<RecordInput> input) {
        const Track* t = c->module() ? c->module()->song().findTrack(id) : nullptr;
        return t && t->listen == listen && (!from.valid() || t->listenTrack == from) &&
               (!input || t->recordInput == *input);
    };
    menu->addChild(rack::createMenuLabel("Plays besides its clips, through its devices (no arming)"));
    menu->addChild(rack::createCheckMenuItem(
        "Nothing", "", [is] { return is(ListenSource::None, TrackId(), std::nullopt); },
        [c, id] { setListen(*c, id, ListenSource::None, TrackId(), std::nullopt); }));
    static const std::pair<RecordInput, const char*> externals[] = {
        {RecordInput::Any, "External: any input (CV and MIDI)"},
        {RecordInput::Cv, "External: the REC CV inputs"},
        {RecordInput::Midi, "External: MIDI"}};
    for (const auto& [input, name] : externals) {
        RecordInput in = input;
        menu->addChild(rack::createCheckMenuItem(
            name, in == RecordInput::Midi ? "channel: Recording menu" : "",
            [is, in] { return is(ListenSource::External, TrackId(), in); },
            [c, id, in] { setListen(*c, id, ListenSource::External, TrackId(), in); }));
    }
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Another track's notes"));
    for (const Track& other : m->song().tracks) {
        if (other.id == id)
            continue;
        TrackId from = other.id;
        bool loops = listensTo(m->song(), from, id);
        std::string current = track->listenTrack == from && track->listen == ListenSource::TrackAfter ? "after devices"
                              : track->listenTrack == from && track->listen == ListenSource::TrackBefore
                                  ? "before devices"
                                  : "";
        auto* item = rack::createSubmenuItem(
            other.name, loops ? "listens to this one" : current, [c, id, from, is](rack::ui::Menu* sub) {
                sub->addChild(rack::createCheckMenuItem(
                    "After its devices: what it plays", "",
                    [is, from] { return is(ListenSource::TrackAfter, from, std::nullopt); },
                    [c, id, from] { setListen(*c, id, ListenSource::TrackAfter, from, std::nullopt); }));
                sub->addChild(rack::createCheckMenuItem(
                    "Before its devices: its clips as written", "",
                    [is, from] { return is(ListenSource::TrackBefore, from, std::nullopt); },
                    [c, id, from] { setListen(*c, id, ListenSource::TrackBefore, from, std::nullopt); }));
            });
        item->disabled = loops;
        menu->addChild(item);
    }
}

void appendVoicesItems(rack::ui::Menu* menu, EditorContext& ctx, const std::vector<TrackId>& ids) {
    EditorContext* c = &ctx;
    auto has = [c, ids](int voices) {
        const Track* t = c->module() ? c->module()->song().findTrack(ids.front()) : nullptr;
        return t && t->polyphony == voices;
    };
    menu->addChild(rack::createCheckMenuItem(
        "Auto", "", [has] { return has(kAutoVoices); },
        [c, ids] { setField(*c, ids, &Track::polyphony, kAutoVoices, "Set voices to auto"); }));
    menu->addChild(new rack::ui::MenuSeparator);
    for (int v = 1; v <= kMaxVoices; v++)
        menu->addChild(rack::createCheckMenuItem(
            std::to_string(v), "", [has, v] { return has(v); },
            [c, ids, v] { setField(*c, ids, &Track::polyphony, v, "Set voices to " + std::to_string(v)); }));
}

void appendRecordMenu(rack::ui::Menu* menu, EditorContext& ctx, const std::vector<TrackId>& ids) {
    EditorContext* c = &ctx;
    const Track* track = c->module()->song().findTrack(ids.front());
    if (!track)
        return;
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createCheckMenuItem(
        "Armed for recording", "", [c, ids] { return trackFlag(*c, ids.front(), &Track::armed); },
        [c, ids] { toggleArm(*c, ids.front(), true); }));
    static const char* const inputs[] = {"Any (CV and MIDI)", "REC CV inputs", "MIDI"};
    std::string channel = track->midiChannel == 0 ? "all channels" : "channel " + std::to_string(track->midiChannel);
    menu->addChild(
        rack::createSubmenuItem("Record from", inputs[int(track->recordInput)], [c, ids](rack::ui::Menu* sub) {
            for (int i = 0; i < 3; i++) {
                RecordInput input = RecordInput(i);
                sub->addChild(rack::createCheckMenuItem(
                    inputs[i], "",
                    [c, ids, input] {
                        const Track* t = c->module() ? c->module()->song().findTrack(ids.front()) : nullptr;
                        return t && t->recordInput == input;
                    },
                    [c, ids, input] { setField(*c, ids, &Track::recordInput, input, "Set record input"); }));
            }
        }));
    menu->addChild(rack::createSubmenuItem("MIDI channel", channel, [c, ids](rack::ui::Menu* sub) {
        for (int ch = 0; ch <= 16; ch++) {
            sub->addChild(rack::createCheckMenuItem(
                ch == 0 ? "All channels" : "Channel " + std::to_string(ch), "",
                [c, ids, ch] {
                    const Track* t = c->module() ? c->module()->song().findTrack(ids.front()) : nullptr;
                    return t && t->midiChannel == ch;
                },
                [c, ids, ch] { setField(*c, ids, &Track::midiChannel, int8_t(ch), "Set MIDI channel"); }));
        }
    }));
    menu->addChild(rack::createCheckMenuItem(
        "Monitor while armed", "", [c, ids] { return trackFlag(*c, ids.front(), &Track::monitor); },
        [c, ids] {
            bool on = !trackFlag(*c, ids.front(), &Track::monitor);
            setField(*c, ids, &Track::monitor, on, on ? "Monitor on" : "Monitor off");
        }));
}

void toggleSolo(EditorContext& ctx, TrackId clicked, bool additive) {
    TesseraModule* m = ctx.module();
    m->perform(soloCommand(m->song(), targets(ctx, clicked), additive));
}

void rename(EditorContext& ctx, TrackId id, const std::string& name) {
    TesseraModule* m = ctx.module();
    if (name.empty())
        return;
    m->perform(setTrackField<std::string>(m->song(), id, &Track::name, name, "Rename track to " + name));
}

void renameGroup(EditorContext& ctx, GroupId id, const std::string& name) {
    if (!name.empty())
        changeGroup(ctx, id, "Rename group to " + name, [&](TrackGroup& g) { g.name = name; });
}

void move(EditorContext& ctx, const std::vector<TrackId>& ids, int before, std::optional<GroupId> group) {
    TesseraModule* m = ctx.module();
    m->perform(moveTracks(m->song(), ids, before, group));
}

void appendTrackMenu(rack::ui::Menu* menu, EditorContext& ctx, TrackId clicked, std::function<void()> startRename) {
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    const Track* track = song.findTrack(clicked);
    if (!track)
        return;
    std::vector<TrackId> ids = targets(ctx, clicked);
    EditorContext* c = &ctx;
    std::string what = ids.size() == 1 ? track->name : countTracks(ids.size());
    menu->addChild(rack::createMenuLabel(what));

    bool full = !canAdd(song, 1);
    menu->addChild(rack::createMenuItem(
        "Add instrument track", shortcutLabel(Action::AddInstrumentTrack), [c] { add(*c, TrackType::Instrument); },
        full));
    menu->addChild(rack::createMenuItem(
        "Add drum track", shortcutLabel(Action::AddDrumTrack), [c] { add(*c, TrackType::Drum); }, full));
    menu->addChild(rack::createMenuItem(
        "Duplicate", shortcutLabel(Action::Duplicate), [c] { duplicateSelected(*c); }, !canAdd(song, ids.size())));
    if (full)
        menu->addChild(rack::createMenuLabel(kTracksFull));
    menu->addChild(rack::createMenuItem(
        "Delete", shortcutLabel(Action::Delete), [c] { deleteSelected(*c); }, ids.size() >= song.tracks.size()));
    if (ids.size() == 1)
        menu->addChild(rack::createMenuItem("Rename", "double-click", startRename));

    menu->addChild(rack::createSubmenuItem("Colour", "", [c, ids](rack::ui::Menu* sub) {
        const Track* t = c->module()->song().findTrack(ids.front());
        for (int i = 0; i < 16; i++) {
            uint32_t color = kTrackColors[i];
            sub->addChild(
                new ColorMenuItem("Colour " + std::to_string(i + 1), color, t && t->color == color,
                                  [c, ids, color] { setField(*c, ids, &Track::color, color, "Change colour"); }));
        }
    }));
    if (track->type == TrackType::Instrument) {
        menu->addChild(rack::createSubmenuItem("Voices", voicesLabel(song, *track),
                                               [c, ids](rack::ui::Menu* sub) { appendVoicesItems(sub, *c, ids); }));
    }

    TrackId first = ids.front();
    menu->addChild(rack::createCheckMenuItem(
        "Show automation lanes", "", [c, first] { return c->view().openAutomation.count(first.value) > 0; },
        [c, first] {
            std::set<uint64_t>& open = c->view().openAutomation;
            if (!open.erase(first.value))
                open.insert(first.value);
        }));
    menu->addChild(rack::createSubmenuItem("Add automation lane", "", [c, first](rack::ui::Menu* sub) {
        appendAddLaneItems(sub, *c, first, LaneOwner::ofTrack(first));
    }));
    menu->addChild(
        rack::createSubmenuItem("Input", inputLabel(song, *track),
                                [c, first = ids.front()](rack::ui::Menu* sub) { appendInputItems(sub, *c, first); }));
    appendRecordMenu(menu, ctx, ids);

    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Group", shortcutLabel(Action::GroupTracks), [c] { groupSelected(*c); }));
    if (const TrackGroup* g = groupOf(song, clicked)) {
        GroupId gid = g->id;
        menu->addChild(rack::createMenuItem("Remove from group", "", [c, ids, gid] {
            const Song& s = c->module()->song();
            const TrackGroup* group = groupOf(s, ids.front());
            if (!group)
                return;
            // Out of the group, right after it.
            int after = 0;
            for (TrackId member : group->tracks)
                after = std::max(after, s.trackIndex(member) + 1);
            move(*c, ids, after, std::nullopt);
        }));
        menu->addChild(rack::createMenuItem("Ungroup", "", [c, gid] {
            TesseraModule* mod = c->module();
            mod->perform(ungroup(mod->song(), gid));
        }));
    }
}

void appendGroupMenu(rack::ui::Menu* menu, EditorContext& ctx, GroupId group, std::function<void()> startRename) {
    const Song& song = ctx.module()->song();
    const TrackGroup* g = nullptr;
    for (const TrackGroup& candidate : song.groups)
        if (candidate.id == group)
            g = &candidate;
    if (!g)
        return;
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuLabel(g->name));
    menu->addChild(rack::createMenuItem("Rename", "double-click", startRename));
    bool folded = ctx.view().foldedGroups.count(group.value) > 0;
    menu->addChild(rack::createMenuItem(folded ? "Unfold" : "Fold", "", [c, group] {
        std::set<uint64_t>& f = c->view().foldedGroups;
        if (!f.erase(group.value))
            f.insert(group.value);
    }));
    menu->addChild(rack::createSubmenuItem("Colour", "", [c, group](rack::ui::Menu* sub) {
        for (int i = 0; i < 16; i++) {
            uint32_t color = kTrackColors[i];
            sub->addChild(new ColorMenuItem("Colour " + std::to_string(i + 1), color, false, [c, group, color] {
                changeGroup(*c, group, "Change group colour", [&](TrackGroup& g) { g.color = color; });
            }));
        }
    }));
    menu->addChild(rack::createMenuItem("Ungroup", "", [c, group] {
        TesseraModule* mod = c->module();
        mod->perform(ungroup(mod->song(), group));
    }));
}

} // namespace tessera::trackActions
