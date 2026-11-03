#include "BounceActions.hpp"

#include "../../model/AudioEdits.hpp"
#include "../../module/TesseraModule.hpp"
#include "../EditorContext.hpp"

#include <memory>

namespace tessera::bounceActions {

namespace {

/// What the dialog has chosen.
struct Choice {
    enum class Length { Clips, Area, UntilSilent } length = Length::UntilSilent;
    bool muteOriginal = true;
};

/// The span of the selected clips on `track`, or an empty span.
std::pair<Tick, Tick> selectedClips(EditorContext& ctx, const Track& t) {
    Tick from = 0, to = 0;
    bool any = false;
    const Song& song = ctx.module()->song();
    for (const ArrangerClip& a : t.arrangement) {
        if (!ctx.state().clipSelection.count(a.id))
            continue;
        const Clip* clip = song.findClip(a.clip);
        Tick end = a.position + a.length;
        (void)clip;
        from = any ? std::min(from, a.position) : a.position;
        to = any ? std::max(to, end) : end;
        any = true;
    }
    return any ? std::make_pair(from, to) : std::make_pair(Tick(0), Tick(0));
}

/// Where the track's last clip on the timeline ends.
Tick lastEnd(const Track& t) {
    Tick end = 0;
    for (const ArrangerClip& a : t.arrangement)
        end = std::max(end, a.position + a.length);
    return end;
}

} // namespace

void openDialog(EditorContext& ctx, TrackId track) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(track) : nullptr;
    if (!t)
        return;
    if (m->soundIndex(track) < 0) {
        ctx.toast(t->name + " makes no sound to bounce: patch its voice into a Track Out's IN first");
        return;
    }
    const Song& song = m->song();
    TimeSig ts = song.timeSig;
    std::pair<Tick, Tick> clips = selectedClips(ctx, *t);
    const ArrangerRange& range = ctx.state().range;
    bool hasClips = clips.second > clips.first;
    bool hasArea = !range.empty() && range.covers(track);
    Tick silentFrom = hasClips ? clips.first : hasArea ? range.from : Tick(std::max(0.0, m->playheadTicks()));
    Tick silentTo = std::max(hasClips ? clips.second : hasArea ? range.to : lastEnd(*t), silentFrom + 1);
    auto choice = std::make_shared<Choice>();
    choice->length = hasClips ? Choice::Length::Clips : hasArea ? Choice::Length::Area : Choice::Length::UntilSilent;

    auto span = [=]() -> std::pair<Tick, Tick> {
        switch (choice->length) {
        case Choice::Length::Clips:
            return clips;
        case Choice::Length::Area:
            return {range.from, range.to};
        case Choice::Length::UntilSilent:
        default:
            return {silentFrom, silentTo};
        }
    };
    Tick bar = ticksPerBar(ts);
    auto barNumber = [bar](Tick t) { return std::to_string(t / bar + 1); };
    auto barText = [barNumber](Tick a, Tick b) { return "bar " + barNumber(a) + " – " + barNumber(b); };
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Bounce " + t->name));
    menu->addChild(rack::createMenuLabel("Length"));
    auto option = [&](Choice::Length length, const std::string& name, const std::string& where) {
        menu->addChild(rack::createCheckMenuItem(
            name, where, [choice, length] { return choice->length == length; },
            [choice, length] { choice->length = length; }, false, true));
    };
    if (hasClips)
        option(Choice::Length::Clips, "Selected clips", barText(clips.first, clips.second));
    if (hasArea)
        option(Choice::Length::Area, "Selected area", barText(range.from, range.to));
    option(Choice::Length::UntilSilent, "Until silent", "from bar " + barNumber(silentFrom));
    menu->addChild(rack::createBoolPtrMenuItem("Mute the original afterwards", "", &choice->muteOriginal));
    double secondsPerTick = 60.0 / ((m->tempo() > 0 ? m->tempo() : 120) * double(kPpq));
    menu->addChild(rack::createMenuLabel("It plays in real time, from a bar before"));
    EditorContext* c = &ctx;
    auto* go = rack::createMenuItem("Bounce", "", [c, m, track, choice, span, secondsPerTick, ts] {
        auto [from, to] = span();
        if (!m->startBounce(track, from, to, choice->length == Choice::Length::UntilSilent, choice->muteOriginal)) {
            c->toast("Can't bounce now: a bounce or an export is running");
            return;
        }
        double seconds = double(to - from + ticksPerBar(ts)) * secondsPerTick;
        char text[64];
        snprintf(text, sizeof text, "Bouncing: about %.0f seconds", seconds);
        c->toast(text);
    });
    menu->addChild(go);
    // The estimate follows the choice.
    struct Estimate : rack::ui::MenuLabel {
        std::function<std::string()> text;
        void step() override {
            MenuLabel::text = text();
            MenuLabel::step();
        }
    };
    auto* estimate = new Estimate;
    estimate->text = [span, secondsPerTick, ts, choice] {
        auto [from, to] = span();
        double seconds = double(to - from + ticksPerBar(ts)) * secondsPerTick;
        char text[48];
        snprintf(text, sizeof text, "About %.0f seconds%s", seconds,
                 choice->length == Choice::Length::UntilSilent ? " or more" : "");
        return std::string(text);
    };
    menu->addChild(estimate);
}

} // namespace tessera::bounceActions
