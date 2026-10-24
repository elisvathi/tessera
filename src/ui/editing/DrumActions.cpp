#include "DrumActions.hpp"

#include "../../model/TrackColors.hpp"
#include "../../module/TesseraModule.hpp"
#include "../views/PianoRoll.hpp"
#include "../widgets/ColorMenuItem.hpp"
#include "../widgets/MenuTextField.hpp"
#include "ClipActions.hpp"

#include <cmath>

namespace tessera::drumActions {

namespace {

/// A copied lane: notes relative to the lane region's start.
edits::NoteList laneClipboard;

const char* const kRateNames[] = {"1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32"};
constexpr Tick kRates[] = {kPpq, kPpq / 2, kPpq / 3, kPpq / 4, kPpq / 6, kPpq / 8};
constexpr int kStepCounts[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 24, 32};
constexpr float kGateLengths[] = {0, 10, 25, 50, 100, 250};

const Track* findTrack(EditorContext& ctx, TrackId id) {
    TesseraModule* m = ctx.module();
    return m ? m->song().findTrack(id) : nullptr;
}

const Clip* shownClip(EditorContext& ctx, const Track& t) {
    return ctx.module()->song().findClip(clipActions::shownClip(ctx, t));
}

std::string stepsText(const DrumLane& lane) {
    if (lane.loopLength <= 0)
        return "Clip";
    return std::to_string(std::max<Tick>(1, lane.loopLength / lane.stepLength));
}

} // namespace

void editLane(EditorContext& ctx, TrackId track, int lane, const std::function<void(DrumLane&)>& change,
              const std::string& name) {
    const Track* t = findTrack(ctx, track);
    if (!t || lane < 0 || lane >= int(t->drumLanes.size()))
        return;
    std::vector<DrumLane> lanes = t->drumLanes;
    change(lanes[size_t(lane)]);
    setLanes(ctx, track, std::move(lanes), name);
}

void setLanes(EditorContext& ctx, TrackId track, std::vector<DrumLane> lanes, const std::string& name) {
    TesseraModule* m = ctx.module();
    if (m)
        m->perform(setTrackField(m->song(), track, &Track::drumLanes, std::move(lanes), name));
}

void editPattern(EditorContext& ctx, TrackId track, int lane, const PatternEdit& edit, const std::string& name) {
    TesseraModule* m = ctx.module();
    const Track* t = findTrack(ctx, track);
    if (!t || lane < 0 || lane >= int(t->drumLanes.size()))
        return;
    const Clip* clip = shownClip(ctx, *t);
    if (!clip)
        return;
    const DrumLane& l = t->drumLanes[size_t(lane)];
    edits::NoteList after =
        edit(clip->notes, l.pitch, laneRegion(l, *clip), l.stepLength, [m] { return m->newId<NoteId>(); });
    m->perform(replaceNotes(name, clip->id, clip->notes, after));
}

void audition(EditorContext& ctx, TrackId track, int lane) {
    const Track* t = findTrack(ctx, track);
    if (t && lane >= 0 && lane < int(t->drumLanes.size()))
        ctx.module()->audition(track, t->drumLanes[size_t(lane)].pitch, ctx.state().lastVelocity, true, 0.05f);
}

void addLane(EditorContext& ctx, TrackId track) {
    const Track* t = findTrack(ctx, track);
    if (!t || int(t->drumLanes.size()) >= kMaxDrumLanes)
        return;
    std::vector<DrumLane> lanes = t->drumLanes;
    DrumLane lane;
    lane.name = "Lane " + std::to_string(lanes.size() + 1);
    // The first note from 36 up, and the first output, that no lane uses yet.
    for (int pitch = 36; pitch < 128; pitch++)
        if (laneOfPitch(lanes, pitch) < 0) {
            lane.pitch = int8_t(pitch);
            break;
        }
    for (int out = 0; out < kMaxDrumLanes; out++)
        if (std::none_of(lanes.begin(), lanes.end(), [out](const DrumLane& l) { return l.outputIndex == out; })) {
            lane.outputIndex = out;
            break;
        }
    lanes.push_back(lane);
    setLanes(ctx, track, std::move(lanes), "Add drum lane");
}

void appendLaneMenu(rack::ui::Menu* menu, EditorContext& ctx, TrackId track, int lane,
                    std::function<void()> startRename) {
    const Track* t = findTrack(ctx, track);
    if (!t || lane < 0 || lane >= int(t->drumLanes.size()))
        return;
    const DrumLane& l = t->drumLanes[size_t(lane)];
    const Clip* clip = shownClip(ctx, *t);
    EditorContext* c = &ctx;
    auto set = [c, track, lane](const std::string& name, std::function<void(DrumLane&)> change) {
        return [c, track, lane, name, change] { editLane(*c, track, lane, change, name); };
    };
    auto current = [c, track, lane]() -> const DrumLane* {
        const Track* tr = findTrack(*c, track);
        return tr && lane < int(tr->drumLanes.size()) ? &tr->drumLanes[size_t(lane)] : nullptr;
    };

    menu->addChild(rack::createMenuLabel(l.name + "  (" + pitchName(l.pitch) + ")"));
    menu->addChild(rack::createMenuItem("Rename", "double-click", std::move(startRename)));
    menu->addChild(rack::createSubmenuItem("Colour", "", [c, track, lane, current](rack::ui::Menu* sub) {
        const DrumLane* now = current();
        auto pick = [c, track, lane](uint32_t color) {
            editLane(*c, track, lane, [color](DrumLane& d) { d.color = color; }, "Change lane colour");
        };
        sub->addChild(rack::createCheckMenuItem(
            "Track colour", "", [now] { return now && now->color == 0; }, [pick] { pick(0); }));
        for (int i = 0; i < 16; i++) {
            uint32_t color = kTrackColors[i];
            sub->addChild(new ColorMenuItem("Colour " + std::to_string(i + 1), color, now && now->color == color,
                                            [pick, color] { pick(color); }));
        }
    }));
    menu->addChild(rack::createSubmenuItem(
        "Note", pitchName(l.pitch) + " (" + std::to_string(l.pitch) + ")", [c, track, lane, l](rack::ui::Menu* sub) {
            sub->addChild(rack::createMenuLabel("MIDI note number (0-127)"));
            sub->addChild(new MenuTextField(std::to_string(l.pitch), [c, track, lane](const std::string& text) {
                char* end = nullptr;
                long v = std::strtol(text.c_str(), &end, 10);
                if (end == text.c_str())
                    return;
                int8_t pitch = int8_t(std::clamp<long>(v, 0, 127));
                editLane(*c, track, lane, [pitch](DrumLane& d) { d.pitch = pitch; }, "Change lane note");
            }));
        }));
    menu->addChild(
        rack::createSubmenuItem("Output", std::to_string(l.outputIndex + 1), [set, current](rack::ui::Menu* sub) {
            for (int out = 0; out < kMaxDrumLanes; out++)
                sub->addChild(rack::createCheckMenuItem(
                    std::to_string(out + 1) + (out < 8 ? "" : "  (poly only)"), "",
                    [current, out] { return current() && current()->outputIndex == out; },
                    set("Change lane output", [out](DrumLane& d) { d.outputIndex = out; })));
        }));

    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createSubmenuItem("Steps", stepsText(l), [set, current](rack::ui::Menu* sub) {
        sub->addChild(rack::createCheckMenuItem(
            "Follow the clip", "", [current] { return current() && current()->loopLength == 0; },
            set("Lane follows the clip", [](DrumLane& d) { d.loopLength = 0; })));
        for (int n : kStepCounts)
            sub->addChild(rack::createCheckMenuItem(
                std::to_string(n), "",
                [current, n] { return current() && current()->loopLength == Tick(n) * current()->stepLength; },
                set("Set lane steps", [n](DrumLane& d) { d.loopLength = Tick(n) * d.stepLength; })));
    }));
    std::string rate;
    for (size_t i = 0; i < std::size(kRates); i++)
        if (kRates[i] == l.stepLength)
            rate = kRateNames[i];
    menu->addChild(rack::createSubmenuItem("Rate", rate, [set, current](rack::ui::Menu* sub) {
        for (size_t i = 0; i < std::size(kRates); i++) {
            Tick r = kRates[i];
            sub->addChild(rack::createCheckMenuItem(
                kRateNames[i], "", [current, r] { return current() && current()->stepLength == r; },
                set("Set lane rate", [r](DrumLane& d) {
                    // Keep the step count when the lane has its own loop.
                    if (d.loopLength > 0)
                        d.loopLength = std::max<Tick>(1, d.loopLength / d.stepLength) * r;
                    d.stepLength = r;
                })));
        }
    }));
    menu->addChild(rack::createSubmenuItem(
        "Choke group", l.chokeGroup ? std::to_string(l.chokeGroup) : "None", [set, current](rack::ui::Menu* sub) {
            sub->addChild(rack::createMenuLabel("Lanes in a group cut each other off"));
            for (int g = 0; g <= kMaxChokeGroups; g++)
                sub->addChild(rack::createCheckMenuItem(
                    g ? std::to_string(g) : "None", "",
                    [current, g] { return current() && current()->chokeGroup == g; },
                    set("Set choke group", [g](DrumLane& d) { d.chokeGroup = g; })));
        }));
    char gate[16];
    snprintf(gate, sizeof(gate), "%g ms", double(l.gateLengthMs));
    menu->addChild(rack::createSubmenuItem(
        "Output length", l.gateLengthMs > 0 ? gate : "Trigger", [set, current](rack::ui::Menu* sub) {
            for (float ms : kGateLengths) {
                char text[24];
                snprintf(text, sizeof(text), "Gate %g ms", double(ms));
                sub->addChild(rack::createCheckMenuItem(
                    ms == 0 ? "Trigger (1 ms)" : text, "",
                    [current, ms] { return current() && current()->gateLengthMs == ms; },
                    set("Set lane output length", [ms](DrumLane& d) { d.gateLengthMs = ms; })));
            }
        }));

    // Pattern tools
    menu->addChild(new rack::ui::MenuSeparator);
    auto pattern = [c, track, lane](const std::string& name, PatternEdit edit) {
        return [c, track, lane, name, edit] { editPattern(*c, track, lane, edit, name); };
    };
    Tick steps = clip ? laneRegion(l, *clip).steps(l.stepLength) : 16;
    float velocity = ctx.state().lastVelocity;
    menu->addChild(rack::createSubmenuItem("Euclidean", "", [pattern, steps, velocity](rack::ui::Menu* sub) {
        sub->addChild(rack::createMenuLabel("Hits spread evenly over " + std::to_string(steps) + " steps"));
        for (int hits = 1; hits <= int(steps); hits++)
            sub->addChild(rack::createMenuItem(
                std::to_string(hits) + (hits == 1 ? " hit" : " hits"), "",
                pattern("Euclidean " + std::to_string(hits) + "/" + std::to_string(steps),
                        [hits, velocity](const edits::NoteList& n, int pitch, LaneRegion r, Tick s,
                                         const edits::IdSource& id) {
                            return drums::applyPattern(n, pitch, r, s, euclid(hits, int(r.steps(s))), velocity, id);
                        })));
    }));
    menu->addChild(rack::createMenuItem(
        "Rotate left", "",
        pattern("Rotate lane", [](const edits::NoteList& n, int pitch, LaneRegion r, Tick s, const edits::IdSource&) {
            return drums::rotate(n, pitch, r, -s);
        })));
    menu->addChild(rack::createMenuItem(
        "Rotate right", "",
        pattern("Rotate lane", [](const edits::NoteList& n, int pitch, LaneRegion r, Tick s, const edits::IdSource&) {
            return drums::rotate(n, pitch, r, s);
        })));
    menu->addChild(rack::createMenuItem(
        "Reverse", "",
        pattern("Reverse lane", [](const edits::NoteList& n, int pitch, LaneRegion r, Tick s, const edits::IdSource&) {
            return drums::reverse(n, pitch, r, s);
        })));
    menu->addChild(rack::createSubmenuItem("Fill every", "", [pattern, velocity](rack::ui::Menu* sub) {
        for (int every : {2, 3, 4, 8})
            sub->addChild(rack::createMenuItem(
                std::to_string(every) + " steps", "",
                pattern("Fill lane", [every, velocity](const edits::NoteList& n, int pitch, LaneRegion r, Tick s,
                                                       const edits::IdSource& id) {
                    std::vector<bool> p(size_t(r.steps(s)));
                    for (size_t i = 0; i < p.size(); i++)
                        p[i] = i % size_t(every) == 0;
                    return drums::applyPattern(n, pitch, r, s, p, velocity, id);
                })));
    }));
    menu->addChild(rack::createSubmenuItem("Randomize", "", [pattern](rack::ui::Menu* sub) {
        for (int pct : {25, 50, 75})
            sub->addChild(rack::createMenuItem(
                std::to_string(pct) + "% of steps", "",
                pattern("Randomize lane",
                        [pct](const edits::NoteList& n, int pitch, LaneRegion r, Tick s, const edits::IdSource& id) {
                            // A new pattern each time.
                            uint32_t seed = uint32_t(rack::random::u32());
                            return drums::randomize(n, pitch, r, s, float(pct) / 100.f, 0.5f, 1.f, seed, id);
                        })));
    }));
    menu->addChild(
        rack::createMenuItem("Clear", "",
                             pattern("Clear lane", [](const edits::NoteList& n, int pitch, LaneRegion r, Tick,
                                                      const edits::IdSource&) { return drums::clear(n, pitch, r); })));
    menu->addChild(rack::createMenuItem("Copy lane", "", [c, track, lane] {
        const Track* tr = findTrack(*c, track);
        const Clip* cl = tr ? shownClip(*c, *tr) : nullptr;
        if (!cl || lane >= int(tr->drumLanes.size()))
            return;
        const DrumLane& d = tr->drumLanes[size_t(lane)];
        LaneRegion r = laneRegion(d, *cl);
        laneClipboard = drums::laneNotes(cl->notes, d.pitch, r);
        for (Note& n : laneClipboard)
            n.start -= r.start;
    }));
    menu->addChild(rack::createMenuItem(
        "Paste lane", "",
        pattern("Paste lane", [](const edits::NoteList& n, int pitch, LaneRegion r, Tick,
                                 const edits::IdSource& id) { return drums::paste(n, pitch, r, laneClipboard, id); }),
        laneClipboard.empty()));

    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuItem(
        "Add lane", "", [c, track] { addLane(*c, track); }, int(t->drumLanes.size()) >= kMaxDrumLanes));
    menu->addChild(rack::createMenuItem("Delete lane", "", [c, track, lane] {
        const Track* tr = findTrack(*c, track);
        if (!tr || lane >= int(tr->drumLanes.size()))
            return;
        std::vector<DrumLane> lanes = tr->drumLanes;
        lanes.erase(lanes.begin() + lane);
        setLanes(*c, track, std::move(lanes), "Delete drum lane");
    }));
}

void appendHitOperators(rack::ui::Menu* menu, NoteTarget& hits) {
    edits::IdSet ids = hits.selectedIds();
    std::vector<Note> chosen;
    for (const Note& n : hits.committedNotes())
        if (ids.count(n.id))
            chosen.push_back(n);
    if (chosen.empty())
        return;
    NoteTarget* target = &hits;
    std::string what = chosen.size() == 1 ? "hit" : std::to_string(chosen.size()) + " hits";

    // One undo step that changes every selected hit.
    auto edit = [target, what](const std::string& verb, std::function<void(Note&)> change) {
        return [target, what, verb, change] {
            edits::IdSet sel = target->selectedIds();
            edits::NoteList before = target->committedNotes(), after = before;
            for (Note& n : after)
                if (sel.count(n.id))
                    change(n);
            target->commit(verb + " of " + what, before, after);
        };
    };
    // Checked when every selected hit has it.
    auto all = [chosen](std::function<bool(const Note&)> has) {
        return [chosen, has] { return std::all_of(chosen.begin(), chosen.end(), has); };
    };

    menu->addChild(rack::createSubmenuItem("Velocity", "", [=](rack::ui::Menu* sub) {
        for (int pct : {25, 50, 75, 100}) {
            float v = float(pct) / 100.f;
            sub->addChild(rack::createCheckMenuItem(
                std::to_string(pct) + "%", "", all([v](const Note& n) { return std::fabs(n.velocity - v) < 0.005f; }),
                edit("Set velocity", [v](Note& n) { n.velocity = v; })));
        }
    }));
    menu->addChild(rack::createSubmenuItem("Chance", "", [=](rack::ui::Menu* sub) {
        for (int pct : {100, 75, 50, 25, 10}) {
            float v = float(pct) / 100.f;
            sub->addChild(
                rack::createCheckMenuItem(pct == 100 ? "Always plays" : std::to_string(pct) + "%", "",
                                          all([v](const Note& n) { return std::fabs(n.ops.chance - v) < 0.005f; }),
                                          edit("Set chance", [v](Note& n) { n.ops.chance = v; })));
        }
    }));
    menu->addChild(rack::createSubmenuItem("Repeats", "", [=](rack::ui::Menu* sub) {
        for (int count : {0, 2, 3, 4, 6, 8}) {
            sub->addChild(
                rack::createCheckMenuItem(count == 0 ? "Off" : std::to_string(count) + " hits", "",
                                          all([count](const Note& n) { return n.ops.repeats == count; }),
                                          edit("Set repeats", [count](Note& n) { n.ops.repeats = int8_t(count); })));
        }
    }));
    menu->addChild(rack::createSubmenuItem("Only when…", "", [=](rack::ui::Menu* sub) {
        static const char* const kNames[] = {"Always",       "First pass", "Not first pass", "After played",
                                             "After silent", "Fill",       "Not fill"};
        for (int i = 0; i < 7; i++) {
            auto o = Occurrence(i);
            sub->addChild(
                rack::createCheckMenuItem(kNames[i], "", all([o](const Note& n) { return n.ops.occurrence == o; }),
                                          edit("Set when the hit plays", [o](Note& n) { n.ops.occurrence = o; })));
        }
    }));
    menu->addChild(rack::createSubmenuItem("Every … passes", "", [=](rack::ui::Menu* sub) {
        sub->addChild(rack::createCheckMenuItem("Every pass", "",
                                                all([](const Note& n) { return n.ops.recurrenceLength <= 1; }),
                                                edit("Set the passes the hit plays on", [](Note& n) {
                                                    n.ops.recurrenceLength = 1;
                                                    n.ops.recurrenceMask = NoteOperators().recurrenceMask;
                                                })));
        for (int length : {2, 3, 4}) {
            for (int pass = 0; pass < length; pass++) {
                auto mask = uint16_t(1u << pass);
                sub->addChild(
                    rack::createCheckMenuItem("Pass " + std::to_string(pass + 1) + " of " + std::to_string(length), "",
                                              all([length, mask](const Note& n) {
                                                  return n.ops.recurrenceLength == length &&
                                                         (n.ops.recurrenceMask & ((1u << length) - 1)) == mask;
                                              }),
                                              edit("Set the passes the hit plays on", [length, mask](Note& n) {
                                                  n.ops.recurrenceLength = uint8_t(length);
                                                  n.ops.recurrenceMask = mask;
                                              })));
            }
        }
    }));
}

void appendKitMenu(rack::ui::Menu* menu, EditorContext& ctx, TrackId track) {
    const Track* t = findTrack(ctx, track);
    if (!t)
        return;
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuLabel("Drum kit"));
    for (DrumKitPreset preset : {DrumKitPreset::Standard8, DrumKitPreset::Gm16})
        menu->addChild(rack::createMenuItem(std::string("Load ") + drumKitName(preset), "", [c, track, preset] {
            setLanes(*c, track, drumKit(preset), std::string("Load ") + drumKitName(preset));
        }));
    menu->addChild(rack::createMenuItem(
        "Add lane", "", [c, track] { addLane(*c, track); }, int(t->drumLanes.size()) >= kMaxDrumLanes));
}

} // namespace tessera::drumActions
