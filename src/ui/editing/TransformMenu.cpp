#include "TransformMenu.hpp"

#include "../../model/Transforms.hpp"
#include "../Theme.hpp"
#include "../views/PianoRoll.hpp"
#include "../widgets/SettingsPanel.hpp"

#include <cmath>
#include <map>
#include <memory>

namespace tessera::transformMenu {

using edits::IdSet;
using edits::NoteList;
using rack::math::Rect;
using rack::math::Vec;
namespace tf = transforms;

namespace {

constexpr Tick kSixteenth = kPpq / 4;

const char* const kGridNames[] = {"1/4", "1/8", "1/16", "1/32", "1/8 triplet", "1/16 triplet"};
const Tick kGridTicks[] = {kPpq, kPpq / 2, kPpq / 4, kPpq / 8, kPpq / 3, kPpq / 6};

using Param = Setting;
using settings::choice;
using settings::number;
using settings::printf1;
using settings::toggle;

Param grid(std::string name, int value) {
    return choice(std::move(name), std::vector<std::string>(std::begin(kGridNames), std::end(kGridNames)), value);
}

auto percent = [](double v) { return printf1("%.0f%%", v); };
auto ofSixteenth = [](double v) { return printf1("%.0f%% of 1/16", v); };
auto plain = [](double v) { return printf1("%.0f", v); };

Tick gridTicks(double v) { return kGridTicks[std::clamp(int(v), 0, 5)]; }
Tick sixteenths(double percentOfSixteenth) { return Tick(std::llround(percentOfSixteenth / 100 * kSixteenth)); }

/// What a transform works on.
struct Context {
    NoteList notes;
    IdSet ids;
    Scale scale;
    std::function<NoteId()> newId;
};

struct Transform {
    std::string name;
    std::vector<Param> params;
    std::function<NoteList(const Context&, const std::vector<double>&)> apply;
    /// Select-by transforms pick notes instead of changing them.
    std::function<IdSet(const Context&, const std::vector<double>&)> pick;
};

/// Settings people chose last time, per transform, for this session.
std::map<std::string, std::vector<double>>& remembered() {
    static std::map<std::string, std::vector<double>> values;
    return values;
}

Context contextOf(PianoRollView& view) {
    Context c;
    c.notes = view.committedNotes();
    c.ids = view.selectedIds();
    if (c.ids.empty())
        c.ids = edits::idsOf(c.notes);
    c.scale = view.scale();
    PianoRollView* v = &view;
    c.newId = [v] { return v->newNoteId(); };
    return c;
}

/// The notes to select after a transform: the chosen ones that remain, and every new note.
IdSet selectionAfter(const NoteList& before, const NoteList& after, const IdSet& ids) {
    IdSet old = edits::idsOf(before), out;
    for (const Note& n : after)
        if (ids.count(n.id) || !old.count(n.id))
            out.insert(n.id);
    return out;
}

void openPanel(PianoRollView& view, Transform t) {
    auto it = remembered().find(t.name);
    if (it != remembered().end() && it->second.size() == t.params.size())
        for (size_t i = 0; i < t.params.size(); i++)
            if (t.params[i].remember)
                t.params[i].value = it->second[i];
    // Shared by the callbacks: what the transform starts from, and its latest result.
    struct State {
        Context context;
        IdSet originalSelection;
        NoteList result;
    };
    auto state = std::make_shared<State>();
    state->context = contextOf(view);
    state->originalSelection = view.selectedIds();
    PianoRollView* v = &view;
    auto transform = std::make_shared<Transform>(t);
    std::string title = t.name + " · " + countNotes(state->context.ids.size());
    SettingsPanel::Callbacks callbacks;
    callbacks.changed = [v, state, transform](const std::vector<double>& values) {
        if (transform->pick) {
            v->select(transform->pick(state->context, values));
            return;
        }
        state->result = transform->apply(state->context, values);
        v->setPreview(state->result);
    };
    callbacks.apply = [v, state, transform](const std::vector<double>& values) {
        remembered()[transform->name] = values;
        if (transform->pick)
            return;
        v->clearPreview();
        const Context& c = state->context;
        if (!(state->result == c.notes))
            v->commit(transform->name + " " + countNotes(c.ids.size()), c.notes, state->result,
                      selectionAfter(c.notes, state->result, c.ids));
    };
    callbacks.cancel = [v, state, transform] {
        if (transform->pick)
            v->select(state->originalSelection);
        else
            v->clearPreview();
    };
    SettingsPanel::open(title, std::move(t.params), "Apply", std::move(callbacks));
}

/// Runs a transform without settings as one undo step.
void applyNow(PianoRollView& view, const std::string& name,
              const std::function<NoteList(const NoteList&, const IdSet&)>& f) {
    Context c = contextOf(view);
    NoteList after = f(c.notes, c.ids);
    if (!(after == c.notes))
        view.commit(name + " " + countNotes(c.ids.size()), c.notes, after, selectionAfter(c.notes, after, c.ids));
}

// ---- The transforms -----------------------------------------------------------------------------------------

Transform quantize() {
    return {"Quantize",
            {grid("Grid", 2), number("Strength", 0, 100, 5, 100, percent), number("Swing", 0, 100, 5, 0, percent),
             choice("Move", {"Starts", "Ends", "Starts and ends"}, 0),
             number("Leave notes closer than", 0, 50, 5, 0, [](double v) { return printf1("%.0f%% of grid", v); })},
            [](const Context& c, const std::vector<double>& v) {
                tf::QuantizeOptions o;
                o.grid = gridTicks(v[0]);
                o.strength = float(v[1] / 100);
                o.swing = float(v[2] / 100);
                o.ends = tf::Ends(int(v[3]));
                o.range = Tick(std::llround(double(o.grid) * v[4] / 100));
                return tf::quantize(c.notes, c.ids, o);
            }};
}

Transform humanize() {
    return {"Humanize",
            {number("Timing", 0, 50, 1, 10, ofSixteenth), number("Velocity", 0, 30, 1, 5, percent),
             number("Length", 0, 50, 1, 0, percent), number("Seed", 1, 999, 1, 1, plain)},
            [](const Context& c, const std::vector<double>& v) {
                tf::HumanizeOptions o;
                o.timing = sixteenths(v[0]);
                o.velocity = float(v[1] / 100);
                o.length = float(v[2] / 100);
                o.seed = uint32_t(v[3]);
                return tf::humanize(c.notes, c.ids, o);
            }};
}

Transform shaper() {
    return {"Shape velocity",
            {choice("Shape", {"Ramp up", "Ramp down", "Sine", "Triangle", "Random"}, 0),
             choice("Mode", {"Multiply", "Add", "Replace"}, 0), number("Depth", 0, 100, 5, 50, percent),
             number("Cycles", 1, 16, 1, 1, plain), number("Seed", 1, 999, 1, 1, plain)},
            [](const Context& c, const std::vector<double>& v) {
                return tf::shapeVelocity(c.notes, c.ids, tf::Shape(int(v[0])), tf::ShapeMode(int(v[1])),
                                         float(v[2] / 100), float(v[3]), uint32_t(v[4]));
            }};
}

Transform legato() {
    return {"Legato",
            {toggle("Same pitch only", false),
             number("Overlap", -100, 100, 5, 0, [](double v) { return printf1("%+.0f%% of 1/16", v); })},
            [](const Context& c, const std::vector<double>& v) {
                return tf::legato(c.notes, c.ids, v[0] != 0, sixteenths(v[1]));
            }};
}

Transform strum() {
    return {"Strum",
            {choice("Direction", {"Up", "Down", "Alternate"}, 0), number("Step", 5, 200, 5, 20, ofSixteenth),
             number("Curve", -100, 100, 10, 0, [](double v) { return printf1("%+.0f%%", v); })},
            [](const Context& c, const std::vector<double>& v) {
                return tf::strum(c.notes, c.ids, tf::StrumDirection(int(v[0])), sixteenths(v[1]), float(v[2] / 100));
            }};
}

Transform flam() {
    return {"Flam",
            {number("Before the note", 5, 100, 5, 25, ofSixteenth), number("Velocity", 10, 100, 5, 60, percent)},
            [](const Context& c, const std::vector<double>& v) {
                return tf::flam(c.notes, c.ids, sixteenths(v[0]), float(v[1] / 100), c.newId);
            }};
}

Transform chop() {
    std::vector<std::string> at{"Equal parts"};
    at.insert(at.end(), std::begin(kGridNames), std::end(kGridNames));
    return {"Chop",
            {number("Parts", 2, 16, 1, 2, plain), choice("Cut at", at, 0),
             number("Last velocity", 0, 150, 5, 100, percent)},
            [](const Context& c, const std::vector<double>& v) {
                Tick at = v[1] >= 1 ? gridTicks(v[1] - 1) : 0;
                return tf::chop(c.notes, c.ids, int(v[0]), at, float(v[2] / 100), c.newId);
            }};
}

Transform arpeggiate() {
    return {"Arpeggiate",
            {choice("Pattern", {"Up", "Down", "Up and down", "Random", "As played"}, 0), grid("Rate", 2),
             number("Gate", 10, 100, 5, 80, percent), number("Octaves", 1, 4, 1, 1, plain),
             number("Seed", 1, 999, 1, 1, plain)},
            [](const Context& c, const std::vector<double>& v) {
                tf::ArpOptions o;
                o.pattern = tf::ArpPattern(int(v[0]));
                o.rate = gridTicks(v[1]);
                o.gate = float(v[2] / 100);
                o.octaves = int(v[3]);
                o.seed = uint32_t(v[4]);
                return tf::arpeggiate(c.notes, c.ids, o, c.newId);
            }};
}

Transform connect() {
    return {"Connect", {grid("Rate", 2)}, [](const Context& c, const std::vector<double>& v) {
                return tf::connect(c.notes, c.ids, c.scale, gridTicks(v[0]), c.newId);
            }};
}

Transform invert(const Context& c) {
    int low = 127, high = 0;
    for (const Note& n : c.notes)
        if (c.ids.count(n.id)) {
            low = std::min(low, int(n.pitch));
            high = std::max(high, int(n.pitch));
        }
    Param pivot =
        number("Around", 0, 127, 1, low <= high ? (low + high) / 2 : 60, [](double v) { return pitchName(int(v)); });
    pivot.remember = false;
    return {"Invert", {pivot}, [](const Context& c, const std::vector<double>& v) {
                return tf::invert(c.notes, c.ids, int(v[0]), c.scale);
            }};
}

Transform transpose() {
    return {"Transpose",
            {number("Amount", -24, 24, 1, 0, [](double v) { return printf1("%+.0f", v); }),
             choice("In", {"Semitones", "Key steps"}, 0)},
            [](const Context& c, const std::vector<double>& v) {
                return tf::transpose(c.notes, c.ids, int(v[0]), v[1] != 0, c.scale);
            }};
}

Transform scaleVelocity() {
    return {"Scale velocity",
            {choice("Mode", {"Percent", "Set to"}, 0), number("Amount", 0, 200, 5, 100, percent)},
            [](const Context& c, const std::vector<double>& v) {
                return tf::scaleVelocity(c.notes, c.ids, tf::ScaleMode(int(v[0])), float(v[1] / 100));
            }};
}

Transform scaleLength() {
    return {"Scale length",
            {choice("Mode", {"Percent", "Set to 1/16 ×"}, 0),
             number("Amount", 5, 400, 5, 100, [](double v) { return printf1("%.0f%%", v); })},
            [](const Context& c, const std::vector<double>& v) {
                bool set = v[0] != 0;
                return tf::scaleLength(c.notes, c.ids, tf::ScaleMode(int(v[0])),
                                       set ? double(kSixteenth) * v[1] / 100 : v[1] / 100);
            }};
}

Transform randomize() {
    return {"Randomize",
            {number("Pitch", 0, 12, 1, 0, [](double v) { return printf1("± %.0f", v); }),
             number("Velocity", 0, 50, 1, 10, [](double v) { return printf1("± %.0f%%", v); }),
             number("Timing", 0, 100, 5, 0, [](double v) { return printf1("± %.0f%% of 1/16", v); }),
             number("Length", 0, 50, 5, 0, [](double v) { return printf1("± %.0f%%", v); }),
             number("Seed", 1, 999, 1, 1, plain)},
            [](const Context& c, const std::vector<double>& v) {
                tf::RandomizeOptions o;
                o.pitch = int(v[0]);
                o.velocity = float(v[1] / 100);
                o.timing = sixteenths(v[2]);
                o.length = float(v[3] / 100);
                o.seed = uint32_t(v[4]);
                return tf::randomize(c.notes, c.ids, o, c.scale);
            }};
}

Transform euclidean(PianoRollView& view, const Context& c) {
    // At the first chosen note's pitch and start, or C3 at the clip start.
    int pitch = 48;
    Tick start = 0;
    bool found = false;
    for (const Note& n : c.notes)
        if (view.selectedIds().count(n.id) && !found) {
            pitch = n.pitch;
            start = n.start;
            found = true;
        }
    Param p = number("Pitch", 0, 127, 1, pitch, [](double v) { return pitchName(int(v)); });
    p.remember = false;
    return {"Euclidean fill",
            {number("Hits", 1, 32, 1, 5, plain), number("Steps", 2, 32, 1, 16, plain),
             number("Rotation", 0, 31, 1, 0, plain), grid("Step", 2), p, number("Velocity", 5, 100, 5, 80, percent)},
            [start](const Context& c, const std::vector<double>& v) {
                return tf::euclidean(c.notes, int(v[0]), int(v[1]), int(v[2]), int(v[4]), start, gridTicks(v[3]),
                                     float(v[5] / 100), c.newId);
            }};
}

Transform selectBy(std::string name, std::vector<Param> params,
                   std::function<tf::SelectOptions(const std::vector<double>&)> options) {
    Transform t;
    t.name = std::move(name);
    t.params = std::move(params);
    t.pick = [options](const Context& c, const std::vector<double>& v) {
        return tf::select(c.notes, IdSet(), options(v));
    };
    return t;
}

void addSelectItems(rack::ui::Menu* menu, PianoRollView& view) {
    PianoRollView* v = &view;
    auto now = [v](tf::SelectOptions o) { return [v, o] { v->select(tf::select(v->committedNotes(), IdSet(), o)); }; };
    tf::SelectOptions muted;
    muted.by = tf::SelectBy::Muted;
    tf::SelectOptions ops;
    ops.by = tf::SelectBy::WithOperators;
    menu->addChild(rack::createMenuItem("Velocity range…", "", [v] {
        openPanel(*v, selectBy("Select by velocity",
                               {number("From", 0, 100, 1, 0, percent), number("To", 0, 100, 1, 100, percent)},
                               [](const std::vector<double>& x) {
                                   tf::SelectOptions o;
                                   o.by = tf::SelectBy::VelocityRange;
                                   o.low = float(x[0] / 100);
                                   o.high = float(x[1] / 100);
                                   return o;
                               }));
    }));
    for (bool off : {true, false})
        menu->addChild(rack::createMenuItem(off ? "Off the grid…" : "On the grid…", "", [v, off] {
            openPanel(*v, selectBy(off ? "Select off the grid" : "Select on the grid", {grid("Grid", 0)},
                                   [off](const std::vector<double>& x) {
                                       tf::SelectOptions o;
                                       o.by = off ? tf::SelectBy::OffBeats : tf::SelectBy::OnBeats;
                                       o.grid = gridTicks(x[0]);
                                       return o;
                                   }));
        }));
    menu->addChild(rack::createMenuItem("Every Nth note…", "", [v] {
        openPanel(*v,
                  selectBy("Select every Nth",
                           {number("Every", 2, 16, 1, 2, plain),
                            number("Starting at", 0, 15, 1, 0, [](double x) { return printf1("note %.0f", x + 1); })},
                           [](const std::vector<double>& x) {
                               tf::SelectOptions o;
                               o.by = tf::SelectBy::EveryNth;
                               o.every = int(x[0]);
                               o.offset = int(x[1]);
                               return o;
                           }));
    }));
    menu->addChild(rack::createMenuItem("Muted notes", "", now(muted)));
    menu->addChild(rack::createMenuItem("Notes with operators", "", now(ops)));
}

} // namespace

void addItems(rack::ui::Menu* menu, PianoRollView& view) {
    PianoRollView* v = &view;
    bool any = !view.committedNotes().empty();
    auto panel = [&](const std::string& label, std::function<Transform()> make) {
        auto* item = rack::createMenuItem(label + "…", "", [v, make] { openPanel(*v, make()); });
        item->disabled = !any;
        menu->addChild(item);
    };
    auto direct = [&](const std::string& label, std::function<NoteList(const NoteList&, const IdSet&)> f) {
        auto* item = rack::createMenuItem(label, "", [v, label, f] { applyNow(*v, label, f); });
        item->disabled = !any;
        menu->addChild(item);
    };
    menu->addChild(rack::createMenuLabel(view.selectedIds().empty()
                                             ? "Transform the whole clip"
                                             : "Transform " + countNotes(view.selectedIds().size())));
    menu->addChild(rack::createMenuLabel("Timing"));
    panel("Quantize", quantize);
    panel("Humanize", humanize);
    panel("Legato", legato);
    panel("Strum", strum);
    panel("Flam", flam);
    panel("Chop", chop);
    direct("Glue", tf::glue);
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Pitch"));
    panel("Transpose", transpose);
    panel("Arpeggiate", arpeggiate);
    panel("Connect", connect);
    panel("Invert", [v] { return invert(contextOf(*v)); });
    direct("Reverse", tf::reverse);
    direct("Retrograde", tf::retrograde);
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createMenuLabel("Values"));
    panel("Scale velocity", scaleVelocity);
    panel("Shape velocity", shaper);
    panel("Scale length", scaleLength);
    panel("Randomize", randomize);
    menu->addChild(new rack::ui::MenuSeparator);
    auto* euclid = rack::createMenuItem("Euclidean fill…", "", [v] { openPanel(*v, euclidean(*v, contextOf(*v))); });
    euclid->disabled = !view.clip();
    menu->addChild(euclid);
    menu->addChild(rack::createSubmenuItem("Select", "", [v](rack::ui::Menu* sub) { addSelectItems(sub, *v); }));
}

void quantizeNow(PianoRollView& view) {
    Transform t = quantize();
    std::vector<double> values;
    for (const Param& p : t.params)
        values.push_back(p.value);
    auto it = remembered().find(t.name);
    if (it != remembered().end() && it->second.size() == values.size())
        values = it->second;
    Context c = contextOf(view);
    tf::QuantizeOptions o;
    o.grid = view.snapStep() > 0 ? view.snapStep() : kSixteenth;
    o.strength = float(values[1] / 100);
    o.swing = float(values[2] / 100);
    o.ends = tf::Ends(int(values[3]));
    o.range = Tick(std::llround(double(o.grid) * values[4] / 100));
    NoteList after = tf::quantize(c.notes, c.ids, o);
    if (!(after == c.notes))
        view.commit("Quantize " + countNotes(c.ids.size()), c.notes, after, view.selectedIds());
}

void openQuantize(PianoRollView& view) {
    if (view.clip())
        openPanel(view, quantize());
}

void open(PianoRollView& view) {
    if (!view.clip())
        return;
    rack::ui::Menu* menu = rack::createMenu();
    addItems(menu, view);
}

} // namespace tessera::transformMenu
