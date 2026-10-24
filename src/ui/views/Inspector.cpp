#include "Inspector.hpp"

#include "../../model/Arrangement.hpp"
#include "../../model/TrackColors.hpp"
#include "../../module/Registry.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../editing/KeyActions.hpp"
#include "../editing/NoteTarget.hpp"
#include "../editing/TrackActions.hpp"
#include "../interaction/ClickOrDrag.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/MenuTextField.hpp"
#include "PianoRoll.hpp"
#include "TimelineDraw.hpp"

#include <cmath>
#include <functional>
#include <optional>

namespace tessera {

using fields::Field;
using fields::Operator;
using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kPad = 12;
constexpr float kRowHeight = 20;
constexpr float kFieldHeight = 22;
constexpr float kSlider = 18; // a field's bar height
constexpr float kChipHeight = 20;
constexpr float kSectionGap = 14;
constexpr float kPassHeight = 18;
constexpr float kChartHeight = 46;
constexpr double kSpreadPx = 80; // dragging Spread this far doubles (or halves) the distance from the mean
constexpr double kChaosPx = 200; // dragging Chaos this far scatters by the field's whole range

std::string bars(Tick ticks, TimeSig ts) {
    char buf[32];
    double b = double(ticks) / double(ticksPerBar(ts));
    if (std::fabs(b - std::round(b)) < 1e-9)
        snprintf(buf, sizeof(buf), "%d bar%s", int(std::round(b)), std::round(b) == 1 ? "" : "s");
    else
        snprintf(buf, sizeof(buf), "%.2f bars", b);
    return buf;
}

// Occurrence choices as buttons, in the model's order; Always is "off".
const char* const kOccurrences[] = {"Always",       "First pass", "Not first", "After played",
                                    "After silent", "Fill",       "Not fill"};

const Field kNoteFields[] = {Field::Start,    Field::Length,  Field::Pitch, Field::Channel,
                             Field::Velocity, Field::Release, Field::Micro};

std::vector<Field> operatorFields(Operator op) {
    switch (op) {
    case Operator::Chance:
        return {Field::Chance};
    case Operator::Repeats:
        return {Field::Repeats, Field::RepeatCurve, Field::RepeatVelocityEnd, Field::RepeatGate};
    default:
        return {};
    }
}

/// Fields on a fixed scale get a chart with several notes; start and length have no scale to chart against.
bool chartable(Field f) {
    const fields::Info& info = fields::info(f);
    return info.max - info.min <= 1000;
}

/// Dragging a field, sideways or up and down: all values move together.
class FieldDrag : public DragGesture {
  public:
    FieldDrag(NoteTarget& editor, Field field, Vec press, TimeSig ts)
        : editor(editor), field(field), press(press), ts(ts), before(editor.committedNotes()),
          ids(editor.selectedIds()) {}
    void update(Vec pos, Modifiers mods) override {
        double moved = (pos.x - press.x) + (press.y - pos.y);
        const fields::Info& info = fields::info(field);
        double delta = moved * info.step * (mods.shift ? 0.1 : 1);
        if (info.integer)
            delta = std::round(delta);
        result = fields::offset(before, ids, field, delta);
        editor.setPreview(result);
    }
    void commit() override {
        editor.clearPreview();
        if (!result.empty() && !(result == before))
            editor.commit("Set " + fields::lowerName(field) + " of " + countNotes(ids.size()), before, result);
    }
    void cancel() override { editor.clearPreview(); }
    std::string readout() const override {
        auto [lo, hi] = fields::range(result.empty() ? before : result, ids, field);
        std::string text = fields::format(field, lo, ts);
        if (hi != lo)
            text += " – " + fields::format(field, hi, ts);
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    NoteTarget& editor;
    Field field;
    Vec press;
    TimeSig ts;
    edits::NoteList before, result;
    edits::IdSet ids;
};

} // namespace

/// Spread or Chaos in a chart, dragged sideways (a histogram). The control springs back when
/// released: it is a tool applied to the notes, not a setting of theirs.
class ChartDrag : public DragGesture {
  public:
    ChartDrag(Inspector& inspector, NoteTarget& editor, Inspector::Region::Kind kind, Field field, Vec press)
        : inspector(inspector), editor(editor), kind(kind), field(field), press(press), before(editor.committedNotes()),
          ids(editor.selectedIds()), seed(rack::random::u32()) {
        inspector.dragging = kind;
    }
    ~ChartDrag() override {
        inspector.dragging = Inspector::Region::Kind::Field;
        inspector.dragText.clear();
    }
    void update(Vec pos, Modifiers mods) override {
        double moved = ((pos.x - press.x) + (press.y - pos.y)) * (mods.shift ? 0.25 : 1);
        char text[32];
        if (kind == Inspector::Region::Kind::Spread) {
            double factor = std::pow(2.0, moved / kSpreadPx);
            result = fields::spreadAroundMean(before, ids, field, factor);
            snprintf(text, sizeof(text), "%.0f%%", factor * 100);
        } else {
            double amount = std::clamp(moved / kChaosPx, 0.0, 1.0);
            result = fields::chaos(before, ids, field, amount, seed);
            snprintf(text, sizeof(text), "%.0f%%", amount * 100);
        }
        inspector.dragText = text;
        editor.setPreview(result);
    }
    void commit() override {
        editor.clearPreview();
        if (result.empty() || result == before)
            return;
        std::string verb = kind == Inspector::Region::Kind::Spread ? "Spread " : "Scatter ";
        editor.commit(verb + fields::lowerName(field) + " of " + countNotes(ids.size()), before, result);
    }
    void cancel() override { editor.clearPreview(); }
    std::string readout() const override {
        return (kind == Inspector::Region::Kind::Spread ? "Spread " : "Chaos ") + inspector.dragText;
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    Inspector& inspector;
    NoteTarget& editor;
    Inspector::Region::Kind kind;
    Field field;
    Vec press;
    edits::NoteList before, result;
    edits::IdSet ids;
    uint32_t seed; // one scatter per drag: dragging further moves the same notes further
};

// ---- Layout -------------------------------------------------------------------------------------------------

size_t Inspector::countOn(Operator op) {
    size_t n = 0;
    for (const Note& note : editor->notes())
        if (editor->isSelected(note.id) && fields::isOn(note, op))
            n++;
    return n;
}

void Inspector::layoutRegions() {
    regions.clear();
    titles.clear();
    recurrenceHint = -1;
    notesBottom = 36;
    if (!ctx.module() || !editor || editor->selectedIds().empty() || !editor->clip())
        return;
    // A transform preview can replace the selected notes (arpeggiate); then there is nothing to show yet.
    edits::IdSet selected = editor->selectedIds();
    bool present = false;
    for (const Note& n : editor->notes())
        present = present || selected.count(n.id);
    if (!present)
        return;
    bool multi = selected.size() > 1;
    float w = box.size.x;
    float y = notesBottom;
    auto field = [&](Field f) {
        regions.push_back(
            {Region::Kind::Field, f, 0, Rect(kPad, y + (kFieldHeight - kSlider) / 2, w - 2 * kPad, kSlider)});
        y += kFieldHeight;
        if (multi && chartable(f) && chartOpen && chartField == f) {
            regions.push_back({Region::Kind::Chart, f, 0, Rect(kPad, y + 2, w - 2 * kPad, kChartHeight)});
            y += kChartHeight + 6;
            float cw = (w - 2 * kPad - 8) / 3;
            regions.push_back({Region::Kind::Mean, f, 0, Rect(kPad, y, cw, kChipHeight)});
            regions.push_back({Region::Kind::Spread, f, 0, Rect(kPad + cw + 4, y, cw, kChipHeight)});
            regions.push_back({Region::Kind::Chaos, f, 0, Rect(kPad + 2 * (cw + 4), y, cw, kChipHeight)});
            y += kChipHeight + 6;
        }
    };

    titles.push_back({y, (multi ? "NOTES (" + std::to_string(selected.size()) + ")" : std::string("NOTE"))});
    y += kRowHeight;
    for (Field f : kNoteFields)
        field(f);
    y += kSectionGap / 2;

    titles.push_back({y, "OPERATORS"});
    y += kRowHeight;
    // The switches, two by two. Varying each play isn't one: it lives under the values it varies.
    const Operator rows[2][2] = {{Operator::Chance, Operator::Repeats}, {Operator::Occurrence, Operator::Recurrence}};
    for (const auto& row : rows) {
        float cw = (w - 2 * kPad - 4) / 2;
        for (int i = 0; i < 2; i++)
            regions.push_back(
                {Region::Kind::Chip, Field::Count, int(row[i]), Rect(kPad + i * (cw + 4), y, cw, kChipHeight)});
        y += kChipHeight + 4;
    }
    y += 4;

    for (int o = 0; o < int(Operator::Count); o++) {
        Operator op = Operator(o);
        if (op == Operator::Spread || countOn(op) == 0)
            continue;
        titles.push_back({y, fields::operatorName(op)});
        y += kRowHeight - 2;
        for (Field f : operatorFields(op))
            field(f);
        if (op == Operator::Occurrence) {
            float cw = (w - 2 * kPad - 4) / 2;
            for (int i = 1; i < 7; i++) {
                int k = i - 1;
                regions.push_back({Region::Kind::Occurrence, Field::Count, i,
                                   Rect(kPad + (k % 2) * (cw + 4), y + (k / 2) * (kChipHeight + 4), cw, kChipHeight)});
            }
            y += 3 * (kChipHeight + 4);
        }
        if (op == Operator::Recurrence) {
            float cw = (w - 2 * kPad - 15 * 2) / 16;
            for (int p = 0; p < 16; p++)
                regions.push_back({Region::Kind::Pass, Field::Count, p, Rect(kPad + p * (cw + 2), y, cw, kPassHeight)});
            y += kPassHeight + 2;
            recurrenceHint = y;
            y += kRowHeight;
        }
        y += 4;
    }
    notesBottom = y;
}

const Inspector::Region* Inspector::regionAt(Vec pos) const {
    Vec p(pos.x, pos.y + scroll);
    // Buttons first: they sit beside their field.
    for (const Region& r : regions)
        if (r.kind != Region::Kind::Field && r.rect.contains(p))
            return &r;
    for (const Region& r : regions)
        if (r.rect.contains(p))
            return &r;
    return nullptr;
}

void Inspector::step() {
    editor = target ? target() : nullptr;
    if (pendingClick && ctx.now() - pendingClickAt > kDoubleClickWait) {
        std::function<void()> click = std::move(pendingClick);
        pendingClick = nullptr;
        if (editor)
            click();
    }
    layoutRegions();
    InteractiveView::step();
}

// ---- Input --------------------------------------------------------------------------------------------------

std::unique_ptr<DragGesture> Inspector::startGesture(Vec pos, int button, Modifiers mods) {
    const Region* r = regionAt(pos);
    if (button != GLFW_MOUSE_BUTTON_LEFT || !r || !editor)
        return nullptr;
    Field field = r->field;
    bool multi = editor->selectedIds().size() > 1;
    switch (r->kind) {
    case Region::Kind::Field: {
        // With several notes a click opens the field's chart; typing is the Mean's job there.
        bool chart = multi && chartable(field);
        return std::make_unique<ClickOrDrag>(std::make_unique<FieldDrag>(*editor, field, pos, timeSig()), pos,
                                             [this, field, chart] {
                                                 if (ctx.now() - doubleClickedAt < kDoubleClickWait)
                                                     return;
                                                 pendingClickAt = ctx.now();
                                                 pendingClick = [this, field, chart] {
                                                     if (chart)
                                                         toggleChart(field);
                                                     else
                                                         openTyping(field);
                                                 };
                                             });
    }
    case Region::Kind::Mean:
        return std::make_unique<ClickOrDrag>(std::make_unique<FieldDrag>(*editor, field, pos, timeSig()), pos,
                                             [this, field] { openTyping(field); });
    case Region::Kind::Spread:
    case Region::Kind::Chaos:
        return std::make_unique<ChartDrag>(*this, *editor, r->kind, field, pos);
    default:
        return nullptr;
    }
}

void Inspector::toggleChart(Field field) {
    chartOpen = !(chartOpen && chartField == field);
    chartField = field;
}

void Inspector::click(Vec pos, int button, Modifiers mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT)
        for (const SettingRow& s : settingRows)
            if (s.rect.contains(Vec(pos.x, pos.y + scroll))) {
                openSetting(s.which);
                return;
            }
    const Region* hit = regionAt(pos);
    if (!hit || !editor)
        return;
    Region r = *hit;
    edits::NoteList before = editor->committedNotes();
    edits::IdSet ids = editor->selectedIds();
    bool left = button == GLFW_MOUSE_BUTTON_LEFT;
    std::string notes = countNotes(ids.size());
    switch (r.kind) {
    case Region::Kind::Chip: {
        if (!left)
            return;
        Operator op = Operator(r.index);
        // A switch that is on (for every selected note) turns off; otherwise it turns on.
        bool on = countOn(op) < ids.size();
        std::string name = fields::operatorName(op);
        editor->commit((on ? "Turn on " : "Turn off ") + name + " for " + notes, before,
                       fields::setOperator(before, ids, op, on));
        break;
    }
    case Region::Kind::Occurrence: {
        if (!left)
            return;
        edits::NoteList after = before;
        for (Note& n : after)
            if (ids.count(n.id))
                n.ops.occurrence = Occurrence(r.index);
        editor->commit("Set when to play " + notes, before, after);
        break;
    }
    case Region::Kind::Pass: {
        int length = 1;
        bool on = false;
        for (const Note& n : before)
            if (ids.count(n.id)) {
                length = std::clamp<int>(n.ops.recurrenceLength, 1, 16);
                on = (n.ops.recurrenceMask >> r.index) & 1;
                break;
            }
        edits::NoteList after;
        if (!left) // right-click: the cycle ends at this pass
            after = fields::setRecurrenceLength(before, ids, r.index + 1);
        else if (r.index >= length) // a grey pass: the cycle grows to it
            after = fields::setRecurrenceLength(before, ids, r.index + 1);
        else // every selected note follows the first one's toggle
            after = fields::setRecurrencePass(before, ids, r.index, !on);
        editor->commit("Set which passes play " + notes, before, after);
        break;
    }
    default:
        break;
    }
}

void Inspector::doubleClick(Vec pos, Modifiers mods) {
    pendingClick = nullptr;
    doubleClickedAt = ctx.now();
    const Region* r = regionAt(pos);
    if (!r || r->kind != Region::Kind::Field || !editor)
        return;
    Field f = r->field;
    if (f == Field::Start || f == Field::Length || f == Field::Pitch)
        return; // no sensible default
    edits::NoteList before = editor->committedNotes();
    edits::IdSet ids = editor->selectedIds();
    editor->commit("Reset " + fields::lowerName(f) + " of " + countNotes(ids.size()), before,
                   fields::setAll(before, ids, f, fields::defaultValue(f)));
}

void Inspector::openTyping(Field field) {
    if (!editor)
        return;
    edits::NoteList notes = editor->committedNotes();
    edits::IdSet ids = editor->selectedIds();
    auto [lo, hi] = fields::range(notes, ids, field);
    rack::ui::Menu* menu = rack::createMenu();
    // Name and hint on two lines keep the box narrow, so it opens beside the field instead of against the edge.
    menu->addChild(rack::createMenuLabel(field == Field::Length ? "Length in beats" : fields::info(field).name));
    if (field == Field::Start)
        menu->addChild(rack::createMenuLabel("A position like 2.1.1, or +1 beat"));
    else if (ids.size() > 1)
        menu->addChild(rack::createMenuLabel("A value, or +10, -5%, *0.8, =64"));
    TimeSig ts = timeSig();
    char current[32];
    snprintf(current, sizeof(current), "%g",
             std::round(lo * (field == Field::Length ? double(kPpq) / double(ticksPerBeat(ts)) : 1) * 100) / 100);
    std::string shown = field == Field::Start ? fields::format(field, lo, ts) : current;
    NoteTarget* view = editor;
    auto* typing = new MenuTextField(lo == hi ? shown : "", [view, field, ts](const std::string& text) {
        edits::NoteList b = view->committedNotes();
        edits::NoteList a;
        edits::IdSet sel = view->selectedIds();
        if (fields::applyTyped(b, sel, field, text, a, ts))
            view->commit("Set " + fields::lowerName(field) + " of " + countNotes(sel.size()), b, a);
    });
    menu->addChild(typing);
    // The click was a request to type, so take the keyboard right away.
    APP->event->setSelectedWidget(typing);
}

TimeSig Inspector::timeSig() const { return ctx.module() ? ctx.module()->song().timeSig : TimeSig(); }

void Inspector::wheel(Vec pos, Vec delta, Modifiers mods) { scroll = std::max(0.f, scroll - delta.y); }

CursorShape Inspector::hoverCursor(Vec pos, Modifiers mods) {
    hoveredSetting = -1;
    for (size_t i = 0; i < settingRows.size(); i++)
        if (settingRows[i].rect.contains(Vec(pos.x, pos.y + scroll)))
            hoveredSetting = int(i);
    if (hoveredSetting >= 0)
        return CursorShape::Hand;
    const Region* r = regionAt(pos);
    hovered = r ? int(r - regions.data()) : -1;
    if (!r || r->kind == Region::Kind::Chart)
        return CursorShape::Arrow;
    bool drags = r->kind == Region::Kind::Field || r->kind == Region::Kind::Mean || r->kind == Region::Kind::Spread ||
                 r->kind == Region::Kind::Chaos;
    return drags ? CursorShape::ResizeH : CursorShape::Hand;
}

// ---- Drawing ------------------------------------------------------------------------------------------------

float Inspector::section(NVGcontext* vg, float y, const char* title) {
    const theme::Palette& c = theme::colors();
    draw::TextStyle style{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text2,
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme::metrics::kPanelLabelSpacing};
    draw::text(vg, style, kPad, y + kRowHeight / 2, title);
    return y + kRowHeight;
}

float Inspector::row(NVGcontext* vg, float y, const std::string& label, const std::string& value) {
    const theme::Palette& c = theme::colors();
    draw::TextStyle l{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle v{theme::fonts::mono(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    draw::text(vg, l, kPad, y + kRowHeight / 2, label);
    draw::text(vg, v, box.size.x - kPad, y + kRowHeight / 2, draw::ellipsize(vg, v, value, box.size.x * 0.6f - kPad));
    return y + kRowHeight;
}

float Inspector::settingRow(NVGcontext* vg, float y, const std::string& label, const std::string& value,
                            Setting which) {
    const theme::Palette& c = theme::colors();
    draw::TextStyle l{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle v{theme::fonts::ui(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    draw::text(vg, l, kPad, y + kRowHeight / 2, label);
    std::string text = draw::ellipsize(vg, v, value, box.size.x * 0.6f - kPad - 14);
    float width = draw::textWidth(vg, v, text) + 22;
    Rect chip(box.size.x - kPad - width, y + 2, width, kRowHeight - 4);
    bool hot = hoveredSetting == int(settingRows.size());
    draw::roundedRect(vg, chip, theme::metrics::kRadiusControl, hot ? c.line0 : c.bg2);
    draw::text(vg, v, chip.getRight() - 16, y + kRowHeight / 2, text);
    float cx = chip.getRight() - 9, cy = y + kRowHeight / 2;
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - 2.5f, cy - 1.2f);
    nvgLineTo(vg, cx, cy + 1.3f);
    nvgLineTo(vg, cx + 2.5f, cy - 1.2f);
    nvgStrokeColor(vg, c.text1);
    nvgStrokeWidth(vg, 1.2f);
    nvgStroke(vg);
    settingRows.push_back({Rect(kPad, y, box.size.x - 2 * kPad, kRowHeight), which});
    return y + kRowHeight;
}

void Inspector::openSetting(Setting which) {
    TesseraModule* m = ctx.module();
    const Track* t = m ? m->song().findTrack(ctx.state().selectedTrack) : nullptr;
    if (!t)
        return;
    rack::ui::Menu* menu = rack::createMenu();
    if (which == Setting::Voices) {
        menu->addChild(rack::createMenuLabel("Voices"));
        trackActions::appendVoicesItems(menu, ctx, {t->id});
    } else if (which == Setting::Input) {
        trackActions::appendInputItems(menu, ctx, t->id);
    } else {
        menu->addChild(rack::createMenuLabel("Recording"));
        trackActions::appendRecordMenu(menu, ctx, {t->id});
    }
}

void Inspector::drawRegion(NVGcontext* vg, const Region& r, bool hot) {
    const theme::Palette& c = theme::colors();
    const edits::NoteList& notes = editor->notes();
    edits::IdSet ids = editor->selectedIds();
    Rect rect = r.rect;
    draw::TextStyle label{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle value{theme::fonts::mono(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    draw::TextStyle centered{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text1,
                             NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    float radius = theme::metrics::kRadiusItem;
    switch (r.kind) {
    case Region::Kind::Field: {
        // A slider: the bar fills to the value (from zero for values that can go negative); with several notes,
        // a band covers their range and a tick marks each note.
        const fields::Info& info = fields::info(r.field);
        bool multi = ids.size() > 1;
        draw::roundedRect(vg, rect, radius, hot ? c.line0 : c.bg2);
        auto [lo, hi] = fields::range(notes, ids, r.field);
        bool open = info.max - info.min > 1000; // start and length: no fixed scale, show the selection's spread
        double min = open ? lo : info.min, max = open ? hi : info.max;
        auto xOf = [&](double v) {
            double f = max > min ? (v - min) / (max - min) : 0.5;
            return rect.pos.x + float(std::clamp(f, 0.0, 1.0)) * rect.size.x;
        };
        nvgSave(vg);
        nvgIntersectScissor(vg, rect.pos.x, rect.pos.y, rect.size.x, rect.size.y);
        if (!open) {
            double zero = std::clamp(0.0, info.min, info.max);
            float a = xOf(multi && hi > lo ? lo : zero), b = xOf(hi);
            if (a > b)
                std::swap(a, b);
            draw::roundedRect(vg, Rect(a, rect.pos.y, std::max(2.f, b - a), rect.size.y), radius,
                              nvgTransRGBAf(c.accent, multi ? 0.25f : 0.35f));
        }
        nvgRestore(vg);
        if (multi && (hi > lo || open)) {
            nvgBeginPath(vg);
            for (const Note& n : notes)
                if (ids.count(n.id))
                    nvgRect(vg, xOf(fields::get(n, r.field)) - 0.5f, rect.getBottom() - 5, 1, 4);
            nvgFillColor(vg, c.accent);
            nvgFill(vg);
        }
        float mid = rect.getCenter().y;
        float labelX = rect.pos.x + 6;
        if (multi && chartable(r.field)) {
            // A disclosure triangle: this row opens a chart.
            bool shown = chartOpen && chartField == r.field;
            float cx = rect.pos.x + 9;
            nvgBeginPath(vg);
            if (shown) {
                nvgMoveTo(vg, cx - 3.5f, mid - 2);
                nvgLineTo(vg, cx + 3.5f, mid - 2);
                nvgLineTo(vg, cx, mid + 2.5f);
            } else {
                nvgMoveTo(vg, cx - 2, mid - 3.5f);
                nvgLineTo(vg, cx + 2.5f, mid);
                nvgLineTo(vg, cx - 2, mid + 3.5f);
            }
            nvgClosePath(vg);
            nvgFillColor(vg, shown ? c.text0 : c.text2);
            nvgFill(vg);
            labelX += 10;
        }
        draw::text(vg, label, labelX, mid, info.name);
        std::string text = fields::format(r.field, lo, timeSig());
        if (hi != lo)
            text += " – " + fields::format(r.field, hi, timeSig());
        float nameWidth = draw::textWidth(vg, label, info.name) + (labelX - rect.pos.x);
        std::string shown = draw::ellipsize(vg, value, text, rect.size.x - nameWidth - 12);
        draw::text(vg, value, rect.getRight() - 6, mid, shown);
        if (r.field == Field::Channel) {
            // The channel's colour, as its notes show it (the lowest one, when the notes are on several).
            const Track* t = ctx.module()->song().findTrack(ctx.state().selectedTrack);
            uint32_t color = channelColor(t ? t->color : 0x9AA1AB, int(lo));
            float x = rect.getRight() - 6 - draw::textWidth(vg, value, shown) - 12;
            draw::roundedRect(vg, Rect(x, mid - 4, 8, 8), 2, theme::track(color));
        }
        break;
    }
    case Region::Kind::Chart:
        drawChart(vg, r);
        break;
    case Region::Kind::Mean:
    case Region::Kind::Spread:
    case Region::Kind::Chaos: {
        bool active = dragging == r.kind;
        draw::roundedRect(vg, rect, theme::metrics::kRadiusControl,
                          active ? nvgTransRGBAf(c.accent, 0.35f)
                          : hot  ? c.line0
                                 : c.bg2);
        std::string text;
        if (r.kind == Region::Kind::Mean)
            text = "Mean " + fields::format(r.field, fields::mean(notes, ids, r.field));
        else
            text = std::string(r.kind == Region::Kind::Spread ? "Spread" : "Chaos") +
                   (active && !dragText.empty() ? " " + dragText : "");
        centered.color = active || hot ? c.text0 : c.text1;
        draw::text(vg, centered, rect.getCenter().x, rect.getCenter().y,
                   draw::ellipsize(vg, centered, text, rect.size.x - 6));
        break;
    }
    case Region::Kind::Chip: {
        size_t on = countOn(Operator(r.index));
        bool all = on == ids.size(), some = on > 0;
        NVGcolor fill = all ? c.accent : some ? nvgTransRGBAf(c.accent, 0.3f) : (hot ? c.line0 : c.bg2);
        if (all && hot)
            fill = nvgLerpRGBA(fill, c.text0, 0.15f);
        draw::roundedRect(vg, rect, theme::metrics::kRadiusControl, fill);
        centered.color = all ? c.ink : some ? c.text0 : c.text1;
        draw::text(vg, centered, rect.getCenter().x, rect.getCenter().y, fields::operatorName(Operator(r.index)));
        break;
    }
    case Region::Kind::Occurrence: {
        bool current = false, seen = false;
        for (const Note& n : notes)
            if (ids.count(n.id) && !seen) {
                current = int(n.ops.occurrence) == r.index;
                seen = true;
            }
        draw::roundedRect(vg, rect, radius, current ? c.accent : (hot ? c.line0 : c.bg2));
        centered.color = current ? c.ink : c.text1;
        draw::text(vg, centered, rect.getCenter().x, rect.getCenter().y, kOccurrences[r.index]);
        break;
    }
    case Region::Kind::Pass: {
        int length = 1;
        bool on = false;
        for (const Note& n : notes)
            if (ids.count(n.id)) {
                length = std::clamp<int>(n.ops.recurrenceLength, 1, 16);
                on = (n.ops.recurrenceMask >> r.index) & 1;
                break;
            }
        bool inCycle = r.index < length;
        NVGcolor fill = !inCycle ? nvgTransRGBAf(c.bg2, hot ? 1.f : 0.5f) : on ? c.accent : (hot ? c.line0 : c.bg2);
        draw::roundedRect(vg, rect, 2, fill);
        if (inCycle && !on)
            draw::roundedRectStroke(vg, rect, 2, c.line2, 1);
        if (inCycle && rect.size.x >= 9) {
            centered.color = on ? c.ink : c.text2;
            centered.size = 9;
            draw::text(vg, centered, rect.getCenter().x, rect.getCenter().y, std::to_string(r.index + 1));
        }
        break;
    }
    }
}

void Inspector::drawChart(NVGcontext* vg, const Region& r) {
    const theme::Palette& c = theme::colors();
    const edits::NoteList& notes = editor->notes();
    edits::IdSet ids = editor->selectedIds();
    const fields::Info& info = fields::info(r.field);
    Rect rect = r.rect;
    draw::roundedRect(vg, rect, theme::metrics::kRadiusItem, c.bg0);
    draw::roundedRectStroke(vg, rect, theme::metrics::kRadiusItem, c.line0, 1);

    // What the chart spans: the field's scale, except pitch, which zooms to the notes (with room to spread).
    double low = info.min, high = info.max;
    if (r.field == Field::Pitch) {
        auto [lo, hi] = fields::range(notes, ids, r.field);
        double pad = std::max(6.0, (24 - (hi - lo)) / 2);
        low = std::max(info.min, lo - pad);
        high = std::min(info.max, hi + pad);
    }
    // Whole numbers get a bar each; the rest are counted in 4% steps.
    int buckets = info.integer ? int(high - low) + 1 : 25;
    double bucketLow = info.integer ? low - 0.5 : low, bucketHigh = info.integer ? high + 0.5 : high;
    std::vector<int> counts = fields::histogram(notes, ids, r.field, bucketLow, bucketHigh, buckets);
    int most = std::max(1, *std::max_element(counts.begin(), counts.end()));

    Rect plot(rect.pos.x + 4, rect.pos.y + 4, rect.size.x - 8, rect.size.y - 16);
    float step = plot.size.x / float(buckets);
    nvgBeginPath(vg);
    for (int b = 0; b < buckets; b++) {
        if (counts[size_t(b)] == 0)
            continue;
        float h = std::max(2.f, plot.size.y * float(counts[size_t(b)]) / float(most));
        nvgRect(vg, plot.pos.x + b * step + 0.5f, plot.getBottom() - h, std::max(1.f, step - 1), h);
    }
    nvgFillColor(vg, nvgTransRGBAf(c.accent, 0.8f));
    nvgFill(vg);

    // The mean: what Spread gathers toward and Mean moves.
    double m = fields::mean(notes, ids, r.field);
    float mx = plot.pos.x + float(std::clamp((m - bucketLow) / (bucketHigh - bucketLow), 0.0, 1.0)) * plot.size.x;
    nvgBeginPath(vg);
    nvgRect(vg, mx - 0.5f, plot.pos.y, 1, plot.size.y);
    nvgFillColor(vg, nvgTransRGBAf(c.text0, 0.7f));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, mx - 3, plot.getBottom() + 5);
    nvgLineTo(vg, mx + 3, plot.getBottom() + 5);
    nvgLineTo(vg, mx, plot.getBottom() + 1);
    nvgClosePath(vg);
    nvgFill(vg);

    draw::TextStyle ends{theme::fonts::mono(), 9, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM};
    draw::text(vg, ends, rect.pos.x + 4, rect.getBottom() - 1, fields::format(r.field, low));
    ends.align = NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM;
    draw::text(vg, ends, rect.getRight() - 4, rect.getBottom() - 1, fields::format(r.field, high));

    // What the control under the mouse does, written on the chart it changes.
    Region::Kind tool = dragging;
    if (tool == Region::Kind::Field && hovered >= 0 && size_t(hovered) < regions.size() &&
        regions[size_t(hovered)].field == r.field)
        tool = regions[size_t(hovered)].kind;
    const char* hint = tool == Region::Kind::Mean     ? "Drag: move every note.  Click: type a value"
                       : tool == Region::Kind::Spread ? "Drag right: wider apart.  Left: closer to the mean"
                       : tool == Region::Kind::Chaos  ? "Drag right: scatter at random"
                                                      : nullptr;
    if (hint) {
        draw::TextStyle h{theme::fonts::ui(), 9.5f, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
        draw::text(vg, h, rect.pos.x + 5, rect.pos.y + 3, draw::ellipsize(vg, h, hint, rect.size.x - 10));
    }
}

void Inspector::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, 1, box.size.y);
    nvgFillColor(vg, c.line1);
    nvgFill(vg);
    if (!ctx.module())
        return;
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    nvgTranslate(vg, 0, -scroll);
    draw::TextStyle title{theme::fonts::uiStrong(), theme::metrics::kTextHeader, c.text0,
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::text(vg, title, kPad, 18, "Inspector");
    for (const Title& t : titles)
        section(vg, t.y, t.text.c_str());
    for (size_t i = 0; i < regions.size(); i++)
        drawRegion(vg, regions[i], int(i) == hovered);
    if (recurrenceHint >= 0) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextRuler, c.text2,
                             NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, kPad, recurrenceHint + kRowHeight / 2 - 2,
                   draw::ellipsize(vg, hint, "Click: play / skip · grey: lengthen · right-click: end here",
                                   box.size.x - 2 * kPad));
    }
    drawFacts(vg, regions.empty() ? notesBottom : notesBottom + kSectionGap);
    nvgRestore(vg);
}

void Inspector::drawFacts(NVGcontext* vg, float y) {
    settingRows.clear();
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    const Track* t = song.findTrack(ctx.state().selectedTrack);
    if (!t) {
        row(vg, y, "No track selected", "");
        return;
    }
    // The track's settings you can change here, instead of only from its right-click menu.
    y = section(vg, y, "TRACK");
    y = row(vg, y, "Name", t->name);
    if (t->type == TrackType::Instrument)
        y = settingRow(vg, y, "Voices", trackActions::voicesLabel(song, *t), Setting::Voices);
    y = settingRow(vg, y, "Input", trackActions::inputLabel(song, *t), Setting::Input);
    static const char* const inputs[] = {"Any input", "REC CV inputs", "MIDI"};
    std::string recording = inputs[int(t->recordInput)];
    if (t->recordInput != RecordInput::Cv)
        recording += t->midiChannel == 0 ? ", all channels" : ", ch " + std::to_string(t->midiChannel);
    if (t->armed)
        recording = "Armed · " + recording;
    y = settingRow(vg, y, "Recording", recording, Setting::Recording);
    bool connected = Registry::get().hasOutputFor(m->id, t->id);
    y = row(vg, y, "Output", connected ? "Connected" : "None");
    if (!connected) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextRuler, theme::colors().queue,
                             NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, kPad, y + kRowHeight / 2 - 3,
                   draw::ellipsize(vg, hint,
                                   t->type == TrackType::Drum ? "Add a Tessera Drum Out and pick this track"
                                                              : "Add a Tessera Track Out and pick this track",
                                   box.size.x - 2 * kPad));
        y += kRowHeight - 4;
    }
    y += kSectionGap;

    const Clip* clip = song.findClip(clipActions::shownClip(ctx, *t));
    y = section(vg, y, "CLIP");
    if (!clip) {
        row(vg, y, "No clip", "");
        return;
    }
    y = row(vg, y, "Name", clip->name.empty() ? "Untitled" : clip->name);
    y = row(vg, y, "Length", bars(clipActions::shownLength(song, *clip), song.timeSig));
    ClipSpan span = clipSpan(*clip, t->type == TrackType::Drum ? &t->drumLanes : nullptr);
    y = row(vg, y, "Loop",
            !span.looping
                ? "Off"
                : timeline::positionText(double(span.start), song.timeSig) + " – " +
                      timeline::positionText(double(span.end), song.timeSig) + (span.cycles() ? " (lanes)" : ""));
    if (t->type == TrackType::Instrument)
        y = row(vg, y, "Key",
                clip->scaleOverride ? keyActions::keyLabel(song, *clip->scaleOverride)
                                    : "Song (" + keyActions::keyLabel(song, song.scale) + ")");

    // The selected arranger instance of this clip: what it changes on top of the clip.
    const ArrangerClip* instance = nullptr;
    if (ctx.state().clipSelection.size() == 1)
        for (const ArrangerClip& a : t->arrangement)
            if (ctx.state().clipSelection.count(a.id) && a.clip == clip->id)
                instance = &a;
    if (instance) {
        y += kSectionGap;
        y = section(vg, y, "INSTANCE");
        y = row(vg, y, "Start", timeline::positionText(double(instance->position), song.timeSig));
        y = row(vg, y, "Length", bars(instance->length, song.timeSig));
        y = row(vg, y, "Offset", bars(instance->offset, song.timeSig));
        y = row(vg, y, "Transpose", (instance->transpose > 0 ? "+" : "") + std::to_string(instance->transpose));
        y = row(vg, y, "Velocity", std::to_string(int(std::lround(instance->velocityScale * 100))) + "%");
        char stretch[16];
        snprintf(stretch, sizeof(stretch), "×%.3g", double(instance->stretch));
        y = row(vg, y, "Stretch", stretch);
        y = row(vg, y, "Muted", instance->muted ? "Yes" : "No");
    }
}

} // namespace tessera
