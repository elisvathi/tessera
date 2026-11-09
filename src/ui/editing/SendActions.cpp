#include "SendActions.hpp"

#include "../../model/AutomationEdits.hpp"
#include "../../model/Sends.hpp"
#include "../../module/TesseraModule.hpp"
#include "../EditorContext.hpp"
#include "../views/AudioClipDraw.hpp"
#include "../widgets/ColorMenuItem.hpp"
#include "../widgets/MenuTextField.hpp"

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

} // namespace

void appendAddSendItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId from) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    const Song& song = m->song();
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuLabel("Send to"));
    menu->addChild(rack::createMenuLabel("Sends made here follow the fader and mute; cables in the rack don't"));
    int offered = 0;
    for (const Track& t : song.tracks) {
        if (t.id == from || findSend(song, from, t.id))
            continue;
        std::string why = whyNoSend(song, from, t.id);
        if (!why.empty()) {
            menu->addChild(rack::createMenuLabel(t.name + ": " + why));
            continue;
        }
        TrackId to = t.id;
        menu->addChild(new ColorMenuItem(t.name, t.color, false, [c, m, from, to] {
            if (CommandPtr add = addSend(m->song(), from, to))
                m->perform(std::move(add));
            // The next move is turning it up: the faders show the sends to that track.
            c->state().setShowSends(to, EditorContext::now());
            if (c->module() && c->module()->makesSound())
                c->state().mixerOpen = true;
        }));
        offered++;
    }
    if (offered == 0)
        menu->addChild(rack::createMenuLabel("No other track to send to"));
}

void appendSendItems(rack::ui::Menu* menu, EditorContext& ctx, TrackId from, TrackId to) {
    TesseraModule* m = ctx.module();
    const Send* send = m ? findSend(m->song(), from, to) : nullptr;
    const Track* target = m ? m->song().findTrack(to) : nullptr;
    if (!send || !target)
        return;
    Send now = *send;
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuLabel("Send to " + target->name));
    menu->addChild(rack::createMenuItem("Show sends to " + target->name, "", [c, to] {
        c->state().setShowSends(to, EditorContext::now());
        c->state().mixerOpen = true;
    }));
    menu->addChild(rack::createMenuItem("Automate in the timeline", "", [c, from, to] { automate(*c, from, to); }));
    menu->addChild(rack::createCheckMenuItem(
        "Before the fader", "", [pre = now.preFader] { return pre; },
        [m, from, now] { m->perform(setSend(m->song(), from, {now.to, now.db, true}, "Send before the fader")); }));
    menu->addChild(rack::createCheckMenuItem(
        "After the fader", "", [pre = now.preFader] { return !pre; },
        [m, from, now] { m->perform(setSend(m->song(), from, {now.to, now.db, false}, "Send after the fader")); }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Send level (dB)"));
    menu->addChild(new MenuTextField(audioDraw::dbText(now.db), [m, from, now](const std::string& text) {
        char* end = nullptr;
        float db = std::strtof(text.c_str(), &end);
        if (end == text.c_str())
            db = kSilentDb;
        m->perform(setSend(m->song(), from, {now.to, db, now.preFader}, "Send " + audioDraw::dbText(db)));
    }));
    menu->addChild(rack::createMenuItem("-inf", "double-click", [m, from, now] {
        m->perform(setSend(m->song(), from, {now.to, kSilentDb, now.preFader}, "Send -inf"));
    }));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem("Remove send", "", [m, from, to] {
        if (CommandPtr r = removeSend(m->song(), from, to))
            m->perform(std::move(r));
    }));
}

} // namespace tessera::sendActions
