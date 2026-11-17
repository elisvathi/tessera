#include "NextActionMenu.hpp"

#include "../../model/Commands.hpp"
#include "../../module/TesseraModule.hpp"

#include <functional>
#include <vector>

namespace tessera::nextActionMenu {

namespace {

constexpr NextAction kActions[] = {NextAction::None, NextAction::Next,   NextAction::Previous, NextAction::First,
                                   NextAction::Last, NextAction::Random, NextAction::Other,    NextAction::RoundRobin,
                                   NextAction::Back, NextAction::Stop};
constexpr int kCounts[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 64};
constexpr int kChances[] = {100, 90, 75, 50, 25, 10};

/// Sets the clip's next action to what `change` makes of it, as one undo step named `name`.
void change(EditorContext& ctx, ClipId id, const std::string& name,
            const std::function<void(NextActionSettings&)>& edit) {
    TesseraModule* mod = ctx.module();
    const Clip* clip = mod ? mod->song().findClip(id) : nullptr;
    if (!clip)
        return;
    NextActionSettings next = clip->next;
    edit(next);
    mod->perform(setClipField(mod->song(), id, &Clip::next, next, name));
}

/// The clip's next action as it is now, or the default when the clip is gone.
NextActionSettings current(EditorContext& ctx, ClipId id) {
    const Clip* clip = ctx.module() ? ctx.module()->song().findClip(id) : nullptr;
    return clip ? clip->next : NextActionSettings();
}

/// Where `action` leads from the clip, in words: a clip's name, "stops", "a random clip".
std::string targetText(const Song& song, const Track& track, int slot, NextAction action, NextActionScope scope) {
    switch (action) {
    case NextAction::None:
        return "keeps looping";
    case NextAction::Random:
        return "a random clip";
    case NextAction::Other:
        return "another clip at random";
    case NextAction::Back:
        return "the clip that played before";
    case NextAction::Stop:
        return "stops";
    default:
        break;
    }
    std::vector<bool> filled;
    for (const ClipSlot& s : track.slots)
        filled.push_back(s.clip && song.findClip(*s.clip));
    NextActionTarget t = pickNextAction(action, scope, filled, slot, -1, 0);
    if (t.kind == NextActionTarget::Kind::Stop)
        return "stops";
    if (t.kind != NextActionTarget::Kind::Launch)
        return "keeps looping";
    if (t.slot == slot)
        return "starts over";
    const Clip* to = song.findClip(*track.slots[size_t(t.slot)].clip);
    return to && !to->name.empty() ? to->name : "the clip in scene " + std::to_string(t.slot + 1);
}

} // namespace

std::string label(NextAction action) {
    switch (action) {
    case NextAction::None:
        return "Keep looping";
    case NextAction::Next:
        return "Play next";
    case NextAction::Previous:
        return "Play previous";
    case NextAction::First:
        return "Play first";
    case NextAction::Last:
        return "Play last";
    case NextAction::Random:
        return "Play random";
    case NextAction::Other:
        return "Play other";
    case NextAction::RoundRobin:
        return "Round robin";
    case NextAction::Back:
        return "Go back";
    case NextAction::Stop:
        return "Stop";
    }
    return "";
}

std::string hint(NextAction action) {
    switch (action) {
    case NextAction::None:
        return "no next action";
    case NextAction::Next:
        return "the clip to the right";
    case NextAction::Previous:
        return "the clip to the left";
    case NextAction::First:
        return "the first clip";
    case NextAction::Last:
        return "the last clip";
    case NextAction::Random:
        return "any clip, this one too";
    case NextAction::Other:
        return "random, never this one";
    case NextAction::RoundRobin:
        return "next, then back to the first";
    case NextAction::Back:
        return "the clip that played before";
    case NextAction::Stop:
        return "the track goes quiet";
    }
    return "";
}

std::string afterLabel(const NextActionSettings& next) {
    int n = next.count;
    if (next.unit == NextActionUnit::Bars)
        return std::to_string(n) + (n == 1 ? " bar" : " bars");
    return std::to_string(n) + (n == 1 ? " loop" : " loops");
}

std::string howOftenLabel(const NextActionSettings& next) {
    return next.chance >= 100 ? "Always" : std::to_string(next.chance) + "%";
}

std::string pickFromLabel(const NextActionSettings& next) {
    return next.scope == NextActionScope::Block ? "This block" : "Whole track";
}

std::string shortLabel(const NextActionSettings& next) {
    if (!next.any())
        return "Off";
    NextAction shown = next.action != NextAction::None ? next.action : next.otherwise;
    return label(shown) + " · " + std::to_string(next.count) + (next.unit == NextActionUnit::Bars ? " bars" : "");
}

std::string summary(const Song& song, const Track& track, const Clip& clip) {
    const NextActionSettings& n = clip.next;
    if (!n.any())
        return "";
    int slot = -1;
    for (size_t i = 0; i < track.slots.size(); i++)
        if (track.slots[i].clip == clip.id)
            slot = int(i);
    std::string count = clip.loopMode != LoopMode::Loop  ? "Plays once"
                        : n.unit == NextActionUnit::Bars ? "Plays " + afterLabel(n)
                        : n.count == 1                   ? "Plays once"
                        : n.count == 2                   ? "Plays twice"
                                                         : "Plays " + std::to_string(n.count) + " times";
    if (slot < 0)
        return count + ", then does its next action from the launcher.";
    std::string a = targetText(song, track, slot, n.action, n.scope);
    if (n.chance >= 100)
        return count + ", then " + a + ".";
    std::string b = targetText(song, track, slot, n.otherwise, n.scope);
    return count + ". Then " + a + " (" + std::to_string(n.chance) + "%) or " + b + " (" +
           std::to_string(100 - n.chance) + "%).";
}

void appendItems(rack::ui::Menu* menu, EditorContext& ctx, ClipId id, Part part) {
    EditorContext* c = &ctx;
    switch (part) {
    case Part::Do:
    case Part::Otherwise: {
        bool main = part == Part::Do;
        for (NextAction a : kActions)
            menu->addChild(rack::createCheckMenuItem(
                label(a), hint(a),
                [c, id, a, main] {
                    NextActionSettings n = current(*c, id);
                    return (main ? n.action : n.otherwise) == a;
                },
                [c, id, a, main] {
                    change(*c, id, main ? "Set next action: " + label(a) : "Set next action otherwise: " + label(a),
                           [a, main](NextActionSettings& n) { (main ? n.action : n.otherwise) = a; });
                }));
        break;
    }
    case Part::After:
        for (int k : kCounts)
            menu->addChild(rack::createCheckMenuItem(
                std::to_string(k), "", [c, id, k] { return current(*c, id).count == k; },
                [c, id, k] {
                    change(*c, id, "Set next action count to " + std::to_string(k),
                           [k](NextActionSettings& n) { n.count = uint16_t(k); });
                }));
        menu->addChild(new rack::ui::MenuSeparator);
        for (NextActionUnit u : {NextActionUnit::Loops, NextActionUnit::Bars})
            menu->addChild(rack::createCheckMenuItem(
                u == NextActionUnit::Loops ? "Loops of the clip" : "Bars", "",
                [c, id, u] { return current(*c, id).unit == u; },
                [c, id, u] {
                    change(*c, id,
                           u == NextActionUnit::Loops ? "Count next action in loops" : "Count next action in bars",
                           [u](NextActionSettings& n) { n.unit = u; });
                }));
        break;
    case Part::HowOften:
        for (int k : kChances)
            menu->addChild(rack::createCheckMenuItem(
                k == 100 ? "Always" : std::to_string(k) + "%", k == 100 ? "" : "else the Otherwise action",
                [c, id, k] { return current(*c, id).chance == k; },
                [c, id, k] {
                    change(*c, id, "Set next action chance to " + std::to_string(k) + "%",
                           [k](NextActionSettings& n) { n.chance = uint8_t(k); });
                }));
        break;
    case Part::PickFrom:
        for (NextActionScope s : {NextActionScope::Track, NextActionScope::Block})
            menu->addChild(rack::createCheckMenuItem(
                s == NextActionScope::Track ? "Whole track" : "This block",
                s == NextActionScope::Track ? "every clip on the track" : "the clips up to an empty slot",
                [c, id, s] { return current(*c, id).scope == s; },
                [c, id, s] {
                    change(*c, id,
                           s == NextActionScope::Track ? "Next action picks from the whole track"
                                                       : "Next action picks from this block",
                           [s](NextActionSettings& n) { n.scope = s; });
                }));
        break;
    }
}

void appendMenu(rack::ui::Menu* menu, EditorContext& ctx, ClipId id) {
    EditorContext* c = &ctx;
    appendItems(menu, ctx, id, Part::Do);
    menu->addChild(new rack::ui::MenuSeparator);
    NextActionSettings n = current(ctx, id);
    menu->addChild(rack::createSubmenuItem("After", afterLabel(n),
                                           [c, id](rack::ui::Menu* sub) { appendItems(sub, *c, id, Part::After); }));
    menu->addChild(rack::createSubmenuItem("How often", howOftenLabel(n),
                                           [c, id](rack::ui::Menu* sub) { appendItems(sub, *c, id, Part::HowOften); }));
    menu->addChild(rack::createSubmenuItem(
        "Otherwise", label(n.otherwise), [c, id](rack::ui::Menu* sub) { appendItems(sub, *c, id, Part::Otherwise); }));
    menu->addChild(rack::createSubmenuItem("Pick from", pickFromLabel(n),
                                           [c, id](rack::ui::Menu* sub) { appendItems(sub, *c, id, Part::PickFrom); }));
}

} // namespace tessera::nextActionMenu
