#include "SendActions.hpp"

#include "../../model/AutomationEdits.hpp"
#include "../../model/Sends.hpp"
#include "../../module/OutputModuleBase.hpp"
#include "../../module/Registry.hpp"
#include "../../module/TesseraModule.hpp"
#include "../EditorContext.hpp"
#include "../views/AudioClipDraw.hpp"
#include "../widgets/ColorMenuItem.hpp"
#include "../widgets/MenuTextField.hpp"
#include "Sources.hpp"

namespace tessera::sendActions {

namespace {

/// Adds a lane that moves the send from `from` to `to` (or opens the one there is), at the send's level now.
void automate(EditorContext& ctx, TrackId from, TrackId to) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(from) : nullptr;
    const Track* target = m ? m->song().findTrack(to) : nullptr;
    const Send* send = m ? findSend(m->song(), from, to) : nullptr;
    if (!t || !target || !send)
        return;
    ctx.state().openAutomation.insert(from.value);
    for (const AutomationEnvelope& l : t->automation)
        if (l.drive == LaneDrive::Send && l.sendTo == to)
            return;
    AutomationEnvelope lane = makeLane(m->newId<AutomationId>(), 0, target->color);
    lane.name = "Send to " + target->name;
    lane.drive = LaneDrive::Send;
    lane.sendTo = to;
    lane.rangeMin = 0;
    lane.rangeMax = 1;
    lane.points = {{0, faderPosOfDb(send->db)}};
    m->perform(addLane(m->song(), LaneOwner::ofTrack(from), lane));
}

/// The effect a track's effect loop is patched into ("Plateau"), or empty when it has none: tracks with one are
/// the returns a send usually goes to.
std::string effectName(const TesseraModule& m, TrackId track) {
    for (OutputModuleBase* o : m.sourceModules(track))
        if (o->isLoop())
            return sources::chainLabel(o, "Effect");
    return "";
}

/// The send's menu label: the receiving track's name, after the sending part's when it is one ("Snare → Reverb").
std::string sendLabel(const TesseraModule& m, const Track& from, const Send& s) {
    const Track* to = m.song().findTrack(s.to);
    std::string target = to ? to->name : std::string("?");
    std::string part = partName(m, from, s.from);
    return part.empty() ? target : part + " \xE2\x86\x92 " + target;
}

} // namespace

std::vector<SendFrom> sendingParts(const TesseraModule& m, const Track& t) {
    std::vector<SendFrom> out;
    std::vector<OutputModuleBase*> inputs = m.sourceModules(t.id);
    if (t.type == TrackType::Drum) {
        for (int k = 0; k < kDrumInputs; k++)
            for (OutputModuleBase* o : Registry::get().outputsOf(m.id))
                if (o->boundTrack() == t.id && o->sourcePatched(k)) {
                    out.push_back(SendFrom::ofLane(k));
                    break;
                }
        return out;
    }
    // As the mixer's input strips: an effect loop shows them all, else two inputs or more do.
    bool loop = std::any_of(inputs.begin(), inputs.end(), [](OutputModuleBase* o) { return o->isLoop(); });
    if (!loop && inputs.size() + (t.type == TrackType::Audio ? 1 : 0) < 2)
        return out;
    if (t.type == TrackType::Audio)
        out.push_back(SendFrom::clips());
    for (OutputModuleBase* o : inputs)
        if (!o->isLoop())
            out.push_back(SendFrom::input(o->id));
    return out;
}

std::string partName(const TesseraModule& m, const Track& t, SendFrom part) {
    switch (part.kind) {
    case SendFrom::Kind::Clips:
        return "Clips";
    case SendFrom::Kind::Lane:
        for (const DrumLane& l : t.drumLanes)
            if (l.outputIndex == part.lane && !l.name.empty())
                return l.name;
        return "Lane " + std::to_string(part.lane + 1);
    case SendFrom::Kind::Input: {
        // Numbered as the strips are: "IN" alone, "IN 1", "IN 2" with more.
        int adds = 0, number = 0;
        for (OutputModuleBase* o : m.sourceModules(t.id))
            if (!o->isLoop()) {
                adds++;
                if (o->id == part.module)
                    number = adds;
            }
        if (number == 0)
            return "IN (gone)";
        return adds > 1 ? "IN " + std::to_string(number) : "IN";
    }
    default:
        return "";
    }
}

std::string senderName(const TesseraModule& m, const Track& t, SendFrom part) {
    std::string name = partName(m, t, part);
    return name.empty() ? t.name : t.name + " \xC2\xB7 " + name;
}

void appendAddSendItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId from, SendFrom part) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    const Song& song = m->song();
    EditorContext* c = &ctx;
    const Track* sender = song.findTrack(from);
    menu->addChild(rack::createMenuLabel(part.whole() || !sender ? std::string("Send to")
                                                                  : "Send " + partName(*m, *sender, part) + " to"));
    // Tracks with an effect patched on them first, named by the effect: those are the returns people look for.
    std::vector<const Track*> returns, others;
    for (const Track& t : song.tracks) {
        if (t.id == from || findSend(song, from, t.id, part))
            continue;
        (effectName(*m, t.id).empty() ? others : returns).push_back(&t);
    }
    int offered = 0;
    auto offer = [&](const Track& t) {
        std::string why = whyNoSend(song, from, t.id, part);
        if (!why.empty()) {
            menu->addChild(rack::createMenuLabel(t.name + ": " + why));
            return;
        }
        TrackId to = t.id;
        std::string toName = t.name;
        auto* item = new ColorMenuItem(t.name, t.color, false, [c, m, from, to, toName, part] {
            // A new send starts where you hear it; its row (mixer, inspector, track menu) sets it from there.
            if (CommandPtr add = addSend(m->song(), from, to, kNewSendDb, part))
                m->perform(std::move(add));
            c->toast("Sending to " + toName + " at " + audioDraw::dbText(kNewSendDb));
        });
        item->rightText = effectName(*m, t.id);
        menu->addChild(item);
        offered++;
    };
    if (!returns.empty()) {
        menu->addChild(rack::createMenuLabel("Tracks with an effect"));
        for (const Track* t : returns)
            offer(*t);
        if (!others.empty()) {
            menu->addChild(new rack::ui::MenuSeparator);
            menu->addChild(rack::createMenuLabel("Other tracks"));
        }
    }
    for (const Track* t : others)
        offer(*t);
    if (offered == 0 && returns.empty() && others.empty())
        menu->addChild(rack::createMenuLabel("No other track to send to"));
}

void appendAddFromItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId to) {
    TesseraModule* m = ctx.module();
    const Track* target = m ? m->song().findTrack(to) : nullptr;
    if (!target)
        return;
    const Song& song = m->song();
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuLabel("Send to " + target->name + " from"));
    int offered = 0;
    for (const Track& t : song.tracks) {
        if (t.id == to)
            continue;
        std::vector<SendFrom> parts{SendFrom{}};
        for (SendFrom p : sendingParts(*m, t))
            parts.push_back(p);
        bool named = false; // the track's own row, or a heading in its place, is in the menu
        for (SendFrom part : parts) {
            if (findSend(song, t.id, to, part))
                continue;
            if (!part.whole() && !named)
                menu->addChild(rack::createMenuLabel(t.name));
            named = true;
            // A part sits under its track, indented, so a kit's lanes read as the kit's.
            std::string label = part.whole() ? t.name : "    " + partName(*m, t, part);
            std::string why = whyNoSend(song, t.id, to, part);
            if (!why.empty()) {
                if (part.whole())
                    menu->addChild(rack::createMenuLabel(t.name + ": " + why));
                continue;
            }
            TrackId from = t.id;
            std::string name = senderName(*m, t, part);
            menu->addChild(new ColorMenuItem(label, t.color, false, [c, m, from, to, part, name] {
                if (CommandPtr add = addSend(m->song(), from, to, kNewSendDb, part))
                    m->perform(std::move(add));
                c->toast("Sending " + name + " here at " + audioDraw::dbText(kNewSendDb));
            }));
            offered++;
        }
    }
    if (offered == 0)
        menu->addChild(rack::createMenuLabel("Nothing else can send here"));
}

void appendDestinationItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId track) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(track) : nullptr;
    if (!t)
        return;
    const TrackGroup* g = groupOf(m->song(), track);
    std::string where = g && m->groupIndex(g->id) >= 0 ? g->name : std::string("the Master");
    auto set = [m, track](bool on) {
        return [m, track, on] {
            if (CommandPtr c = setTrackField(m->song(), track, &Track::toMaster, on,
                                             on ? "Send to the Master again" : "Only its sends"))
                m->perform(std::move(c));
        };
    };
    menu->addChild(rack::createCheckMenuItem("To " + where, "", [t] { return t->toMaster; }, set(true)));
    menu->addChild(rack::createCheckMenuItem(
        "Only its sends", t->sends.empty() ? "no sends yet" : "", [t] { return !t->toMaster; }, set(false)));
}

void appendTrackSends(rack::ui::Menu* menu, EditorContext& ctx, TrackId from) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(from) : nullptr;
    if (!t)
        return;
    EditorContext* c = &ctx;
    // Where its sound goes after the fader: on to the Master, or only to what it sends to.
    appendDestinationItems(menu, ctx, from);
    menu->addChild(new rack::ui::MenuSeparator);
    for (const Send& s : t->sends) {
        if (!m->song().findTrack(s.to))
            continue;
        TrackId target = s.to;
        SendFrom part = s.from;
        menu->addChild(rack::createSubmenuItem(
            sendLabel(*m, *t, s), audioDraw::dbText(s.db) + (s.preFader && part.whole() ? " · pre" : ""),
            [c, from, target, part](rack::ui::Menu* sub) { appendSendItems(sub, *c, from, target, part); }));
    }
    if (!t->sends.empty())
        menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(
        rack::createSubmenuItem("Send to", "", [c, from](rack::ui::Menu* sub) { appendAddSendItems(sub, *c, from); }));
}

void appendReceivedItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId to) {
    TesseraModule* m = ctx.module();
    const Track* target = m ? m->song().findTrack(to) : nullptr;
    if (!target)
        return;
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuLabel("Sends to " + target->name));
    for (const SendRef& ref : sendsInto(m->song(), to)) {
        const Track* t = m->song().findTrack(ref.track);
        if (!t)
            continue;
        TrackId from = ref.track;
        SendFrom part = ref.send.from;
        menu->addChild(rack::createSubmenuItem(
            senderName(*m, *t, part), audioDraw::dbText(ref.send.db) + (ref.send.preFader && part.whole() ? " · pre" : ""),
            [c, from, to, part](rack::ui::Menu* sub) { appendSendItems(sub, *c, from, to, part); }));
    }
}

void appendSendItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId from, TrackId to, SendFrom part) {
    TesseraModule* m = ctx.module();
    const Send* send = m ? findSend(m->song(), from, to, part) : nullptr;
    const Track* target = m ? m->song().findTrack(to) : nullptr;
    const Track* sender = m ? m->song().findTrack(from) : nullptr;
    if (!send || !target || !sender)
        return;
    Send now = *send;
    EditorContext* c = &ctx;
    auto with = [now](float db, bool pre) {
        Send s = now;
        s.db = db;
        s.preFader = pre;
        return s;
    };
    menu->addChild(rack::createMenuLabel(part.whole() ? "Send to " + target->name
                                                      : senderName(*m, *sender, part) + " to " + target->name));
    if (part.whole()) {
        menu->addChild(rack::createMenuItem("Automate in the timeline", "", [c, from, to] { automate(*c, from, to); }));
        menu->addChild(rack::createCheckMenuItem(
            "Before the fader", "", [pre = now.preFader] { return pre; },
            [m, from, now, with] { m->perform(setSend(m->song(), from, with(now.db, true), "Send before the fader")); }));
        menu->addChild(rack::createCheckMenuItem(
            "After the fader", "", [pre = now.preFader] { return !pre; },
            [m, from, now, with] { m->perform(setSend(m->song(), from, with(now.db, false), "Send after the fader")); }));
    } else {
        // A part sends what it adds to its track: after its own level and pan, whatever the track's fader does.
        menu->addChild(rack::createMenuLabel("After its own level and pan"));
    }
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Send level (dB)"));
    menu->addChild(new MenuTextField(audioDraw::dbText(now.db), [m, from, now, with](const std::string& text) {
        char* end = nullptr;
        float db = std::strtof(text.c_str(), &end);
        if (end == text.c_str())
            db = kSilentDb;
        m->perform(setSend(m->song(), from, with(db, now.preFader), "Send " + audioDraw::dbText(db)));
    }));
    menu->addChild(rack::createMenuItem("-inf", "double-click", [m, from, now, with] {
        m->perform(setSend(m->song(), from, with(kSilentDb, now.preFader), "Send -inf"));
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Remove send", "", [m, from, to, part] {
        if (CommandPtr r = removeSend(m->song(), from, to, part))
            m->perform(std::move(r));
    }));
}

} // namespace tessera::sendActions
