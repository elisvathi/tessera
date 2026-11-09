#include "BounceActions.hpp"

#include "../../model/AudioEdits.hpp"
#include "../../model/BounceNotes.hpp"
#include "../../model/DeviceLookup.hpp"
#include "../../module/TesseraModule.hpp"
#include "../EditorContext.hpp"

#include <memory>

namespace tessera::bounceActions {

namespace {

/// One choice in a bounce dialog, ticked when chosen. Clicking it keeps the menu open, so the choices can be made
/// before pressing Bounce.
struct ChoiceItem : rack::ui::MenuItem {
    std::string note;
    std::function<bool()> chosen;
    std::function<void()> choose;

    void step() override {
        rightText = note + (chosen() ? std::string(note.empty() ? "" : "  ") + CHECKMARK_STRING : "");
        MenuItem::step();
    }
    void onAction(const ActionEvent& e) override {
        choose();
        e.unconsume();
    }
};

/// Makes a ChoiceItem.
ChoiceItem* choiceItem(const std::string& text, const std::string& note, std::function<bool()> chosen,
                       std::function<void()> choose) {
    auto* item = new ChoiceItem;
    item->text = text;
    item->note = note;
    item->chosen = std::move(chosen);
    item->choose = std::move(choose);
    return item;
}

/// What the dialog has chosen.
struct Choice {
    enum class Length { Clips, Area, UntilSilent } length = Length::UntilSilent;
    bool muteOriginal = true;
    bool written = false; // notes: as written rather than as they play
    int channel = 0;      // notes: only this playhead's channel, 0 = all
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

void openAudioDialog(EditorContext& ctx, TrackId track) {
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
        menu->addChild(choiceItem(
            name, where, [choice, length] { return choice->length == length; },
            [choice, length] { choice->length = length; }));
    };
    if (hasClips)
        option(Choice::Length::Clips, "Selected clips", barText(clips.first, clips.second));
    if (hasArea)
        option(Choice::Length::Area, "Selected area", barText(range.from, range.to));
    option(Choice::Length::UntilSilent, "Until silent", "from bar " + barNumber(silentFrom));
    menu->addChild(choiceItem(
        "Mute the original afterwards", "", [choice] { return choice->muteOriginal; },
        [choice] { choice->muteOriginal = !choice->muteOriginal; }));
    double secondsPerTick = 60.0 / ((m->tempo() > 0 ? m->tempo() : 120) * double(kPpq));
    menu->addChild(rack::createMenuLabel("It plays in real time, from a bar before"));
    EditorContext* c = &ctx;
    std::vector<ArrangementId> selected;
    for (const ArrangerClip& a : t->arrangement)
        if (ctx.state().clipSelection.count(a.id))
            selected.push_back(a.id);
    auto* go = rack::createMenuItem("Bounce", "", [c, m, track, choice, span, secondsPerTick, ts, selected] {
        auto [from, to] = span();
        std::vector<ArrangementId> only =
            choice->length == Choice::Length::Clips ? selected : std::vector<ArrangementId>{};
        if (!m->startBounce(track, from, to, choice->length == Choice::Length::UntilSilent, choice->muteOriginal,
                            only)) {
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

/// The instant bounce of notes to a new track: of the timeline when `clip` isn't set, else of that panel clip.
static void openNotesDialog(EditorContext& ctx, TrackId track, ClipId clip, int slot) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(track) : nullptr;
    if (!t)
        return;
    const Clip* panelClip = clip.valid() ? m->song().findClip(clip) : nullptr;
    std::pair<Tick, Tick> clips = selectedClips(ctx, *t);
    const ArrangerRange& range = ctx.state().range;
    bool hasClips = clips.second > clips.first;
    bool hasArea = !range.empty() && range.covers(track);
    Tick first = t->arrangement.empty() ? 0 : lastEnd(*t);
    for (const ArrangerClip& a : t->arrangement)
        first = std::min(first, a.position);
    std::pair<Tick, Tick> whole{first, lastEnd(*t)};
    std::pair<Tick, Tick> area{range.from, range.to};
    auto choice = std::make_shared<Choice>();
    choice->length = hasClips ? Choice::Length::Clips : hasArea ? Choice::Length::Area : Choice::Length::UntilSilent;
    std::vector<ArrangementId> selected;
    for (const ArrangerClip& a : t->arrangement)
        if (ctx.state().clipSelection.count(a.id))
            selected.push_back(a.id);

    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Bounce " + (panelClip ? panelClip->name : t->name) + " to a new track"));
    if (!panelClip) {
        menu->addChild(rack::createMenuLabel("Length"));
        auto length = [&](Choice::Length value, const std::string& name) {
            menu->addChild(choiceItem(
                name, "", [choice, value] { return choice->length == value; },
                [choice, value] { choice->length = value; }));
        };
        if (hasClips)
            length(Choice::Length::Clips, "Selected clips");
        if (hasArea)
            length(Choice::Length::Area, "Selected area");
        length(Choice::Length::UntilSilent, "Whole track");
    }
    menu->addChild(rack::createMenuLabel("Notes"));
    menu->addChild(choiceItem(
        "As it plays", "after devices", [choice] { return !choice->written; }, [choice] { choice->written = false; }));
    menu->addChild(choiceItem(
        "As written", "before devices", [choice] { return choice->written; }, [choice] { choice->written = true; }));
    if (const Device* device = activePlayheads(*t)) {
        menu->addChild(rack::createMenuLabel("Playheads (as it plays)"));
        auto playhead = [&](int channel, const std::string& name) {
            menu->addChild(choiceItem(
                name, "", [choice, channel] { return choice->channel == channel; },
                [choice, channel] { choice->channel = channel; }));
        };
        playhead(0, "All, each on its channel");
        // A playhead without a channel of its own leaves its notes on the channel they were written on, normally 1.
        for (const Playhead& p : device->playheads)
            playhead(p.channel > 0 ? p.channel : 1, p.name);
    }
    EditorContext* c = &ctx;
    std::string name = t->name + " (bounce)";
    menu->addChild(rack::createMenuItem("Bounce", "", [=] {
        TesseraModule* module = c->module();
        if (!module)
            return;
        NoteBounce what;
        what.track = track;
        what.afterDevices = !choice->written;
        what.channel = choice->written ? 0 : choice->channel;
        what.clip = clip;
        what.slot = slot;
        std::pair<Tick, Tick> span = choice->length == Choice::Length::Clips  ? clips
                                     : choice->length == Choice::Length::Area ? area
                                                                              : whole;
        what.from = span.first;
        what.to = span.second;
        if (choice->length == Choice::Length::Clips)
            what.only = selected;
        NoteBounceMade made;
        double tempo = module->tempo() > 0 ? module->tempo() : 120;
        CommandPtr command = bounceNotes(module->song(), what, tempo, module->ids(), &made);
        if (!command) {
            c->toast("Nothing to bounce: there are no notes there");
            return;
        }
        module->perform(std::move(command));
        c->state().selectedTrack = made.track;
        c->state().selectedTracks = {made.track};
        c->toast("Bounced to a new track: " + name);
    }));
}

void openNotes(EditorContext& ctx, TrackId track) { openNotesDialog(ctx, track, ClipId(), 0); }

void openDialog(EditorContext& ctx, TrackId track) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(track) : nullptr;
    if (t && t->type != TrackType::Audio && m->soundIndex(track) < 0)
        openNotes(ctx, track);
    else
        openAudioDialog(ctx, track);
}

void openClipDialog(EditorContext& ctx, TrackId track, ClipId clip, int slot) {
    openNotesDialog(ctx, track, clip, slot);
}

} // namespace tessera::bounceActions
