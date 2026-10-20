#include "ModulatorPopover.hpp"

#include "../../model/DeviceTypes.hpp"
#include "../../model/Modulators.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/DeviceActions.hpp"
#include "../interaction/ClickOrDrag.hpp"
#include "../widgets/Draw.hpp"

#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;
using Kind = ModulatorPopover::Part::Kind;

namespace {

constexpr float kPad = 8;
constexpr float kHeader = 26;
constexpr float kRow = 22;
constexpr float kSteps = 56;
constexpr float kBarWidth = 50;   // a route's amount bar: -100% at its left, +100% at its right
constexpr float kSettingPx = 200; // dragging a setting this far turns it end to end

/// A setting of the open modulator, dragged sideways or up and down; the engine follows at once.
class SettingDrag : public DragGesture {
  public:
    SettingDrag(ModulatorPopover& pop, EditorContext& ctx, int index, ParamRef ref, float start,
                devices::ParamInfo info, Vec press)
        : pop(pop), ctx(ctx), index(index), ref(ref), start(start), value(start), info(std::move(info)), press(press) {}
    void update(Vec pos, Modifiers mods) override {
        float moved = (pos.x - press.x) + (press.y - pos.y);
        value = std::clamp(start + moved / kSettingPx * (mods.shift ? 0.2f : 1.f), 0.f, 1.f);
        pop.settingPreview = std::make_pair(index, value);
        if (TesseraModule* m = ctx.module())
            m->holdKnob(ref, value);
    }
    void commit() override {
        finish();
        if (value != start)
            deviceActions::setKnob(ctx, ref, value);
    }
    void cancel() override { finish(); }
    std::string readout() const override { return std::string(info.name) + "  " + devices::format(info, value); }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    void finish() {
        pop.settingPreview.reset();
        if (TesseraModule* m = ctx.module())
            m->releaseKnob();
    }
    ModulatorPopover& pop;
    EditorContext& ctx;
    int index;
    ParamRef ref;
    float start, value;
    devices::ParamInfo info;
    Vec press;
};

/// A route's amount bar, dragged sideways: left negative, right positive.
class RouteAmountDrag : public DragGesture {
  public:
    RouteAmountDrag(ModulatorPopover& pop, EditorContext& ctx, ModulatorId id, int index, float start, Vec press)
        : pop(pop), ctx(ctx), id(id), index(index), start(start), amount(start), press(press) {}
    void update(Vec pos, Modifiers mods) override {
        amount = std::clamp(start + (pos.x - press.x) / (kBarWidth / 2) * (mods.shift ? 0.2f : 1.f), -1.f, 1.f);
        pop.amountPreview = std::make_pair(index, amount);
    }
    void commit() override {
        pop.amountPreview.reset();
        TesseraModule* m = ctx.module();
        if (m && amount != start)
            m->perform(setRouteAmount(m->song(), id, index, amount));
    }
    void cancel() override { pop.amountPreview.reset(); }
    std::string readout() const override {
        char text[16];
        snprintf(text, sizeof(text), "%+.0f%%", double(amount) * 100);
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    ModulatorPopover& pop;
    EditorContext& ctx;
    ModulatorId id;
    int index;
    float start, amount;
    Vec press;
};

/// Painting a Steps modulator's bars: each bar under the pointer takes its height.
class StepsDrag : public DragGesture {
  public:
    StepsDrag(ModulatorPopover& pop, EditorContext& ctx, ModulatorId id, std::vector<float> steps, int count, Rect area)
        : pop(pop), ctx(ctx), id(id), steps(std::move(steps)), count(count), area(area) {}
    void update(Vec pos, Modifiers mods) override {
        int k = std::clamp(int((pos.x - area.pos.x) / (area.size.x / float(count))), 0, count - 1);
        float v = std::clamp((area.getBottom() - pos.y) / area.size.y, 0.f, 1.f);
        if (size_t(k) < steps.size())
            steps[size_t(k)] = v;
        pop.stepsPreview = steps;
    }
    void commit() override {
        pop.stepsPreview.reset();
        if (TesseraModule* m = ctx.module())
            m->perform(setSteps(m->song(), id, steps));
    }
    void cancel() override { pop.stepsPreview.reset(); }
    CursorShape cursor() const override { return CursorShape::Crosshair; }

  private:
    ModulatorPopover& pop;
    EditorContext& ctx;
    ModulatorId id;
    std::vector<float> steps;
    int count;
    Rect area;
};

/// An LFO shows the one rate that applies: the synced length while Sync is on, else hertz.
bool settingShown(const Modulator& m, int index) {
    if (m.type != ModulatorType::Lfo || m.params.size() <= size_t(mods::knob::kSyncedRate))
        return true;
    bool synced = m.params[size_t(mods::knob::kSync)] >= 0.5f;
    if (index == mods::knob::kRate)
        return !synced;
    if (index == mods::knob::kSyncedRate)
        return synced;
    return true;
}

std::string settingLabel(const Modulator& m, int index, const char* name) {
    if (m.type == ModulatorType::Lfo && (index == mods::knob::kRate || index == mods::knob::kSyncedRate))
        return "Rate";
    return name;
}

/// How many steps a Steps modulator plays, and its values padded to that many.
std::vector<float> stepsOf(const Modulator& m, int* count) {
    const devices::ParamInfo* p = devices::paramInfo(m.type, 0);
    int n = p && !m.params.empty() ? int(devices::display(*p, m.params[0])) : int(m.steps.size());
    n = std::clamp(n, 1, kMaxSteps);
    std::vector<float> steps = m.steps;
    steps.resize(size_t(kMaxSteps), 0.5f);
    *count = n;
    return steps;
}

} // namespace

const Modulator* ModulatorPopover::modulator(ModulatorPlace* place) {
    TesseraModule* m = ctx.module();
    return m && ctx.openModulator ? findModulator(m->song(), ModulatorId(ctx.openModulator), place) : nullptr;
}

float ModulatorPopover::wantedHeight() {
    if (!modulator() || !deviceActions::stripOpen(ctx))
        return 0;
    layoutParts();
    return height;
}

void ModulatorPopover::layoutParts() {
    parts.clear();
    const Modulator* mod = modulator();
    if (!mod)
        return;
    float w = kWidth;
    parts.push_back({Kind::Close, Rect(w - 26, 5, 18, 18)});
    float y = kHeader + 2;
    int n = int(devices::modulatorInfo(mod->type).params.size());
    // Two columns, so the popover stays short enough to sit over the notes.
    float cw = (w - 2 * kPad - 4) / 2;
    int column = 0;
    for (int k = 0; k < n; k++) {
        if (!settingShown(*mod, k))
            continue;
        parts.push_back({Kind::Setting, Rect(kPad + float(column) * (cw + 4), y, cw, kRow - 2), k});
        if (++column == 2) {
            column = 0;
            y += kRow;
        }
    }
    if (column > 0)
        y += kRow;
    if (mod->type == ModulatorType::Steps) {
        parts.push_back({Kind::StepBars, Rect(kPad, y + 4, w - 2 * kPad, kSteps)});
        y += kSteps + 8;
    }
    y += 6;
    movesTitleY = y;
    y += 18;
    for (size_t r = 0; r < mod->routes.size(); r++) {
        float right = w - kPad;
        parts.push_back({Kind::RouteRemove, Rect(right - 16, y + 3, 16, 16), int(r)});
        parts.push_back({Kind::RouteAmount, Rect(right - 16 - 44 - kBarWidth, y + 4, kBarWidth, 14), int(r)});
        y += kRow;
    }
    if (mod->routes.empty())
        y += kRow;
    parts.push_back({Kind::Map, Rect(kPad, y + 4, 118, 22)});
    parts.push_back({Kind::CvOut, Rect(kPad + 122, y + 4, 86, 22)});
    height = y + 4 + 22 + kPad;
    // Below the header everything scrolls together.
    scroll = std::clamp(scroll, 0.f, std::max(0.f, height - box.size.y));
    for (Part& p : parts)
        if (p.kind != Kind::Close)
            p.rect.pos.y -= scroll;
    movesTitleY -= scroll;
}

void ModulatorPopover::wheel(Vec pos, Vec delta, Modifiers mods) {
    scroll = std::clamp(scroll - delta.y, 0.f, std::max(0.f, height - box.size.y));
}

const ModulatorPopover::Part* ModulatorPopover::partAt(Vec pos) const {
    for (const Part& p : parts) {
        // Content scrolled under the header can't be clicked.
        if (p.kind != Kind::Close && pos.y < kHeader)
            continue;
        if (p.rect.contains(pos))
            return &p;
    }
    return nullptr;
}

void ModulatorPopover::step() {
    if (ctx.openModulator && !modulator())
        ctx.openModulator = 0;
    layoutParts();
    InteractiveView::step();
}

std::unique_ptr<DragGesture> ModulatorPopover::startGesture(Vec pos, int button, Modifiers mods) {
    const Part* hit = partAt(pos);
    const Modulator* mod = modulator();
    if (button != GLFW_MOUSE_BUTTON_LEFT || !hit || !mod)
        return nullptr;
    Part p = *hit;
    EditorContext* c = &ctx;
    switch (p.kind) {
    case Kind::Setting: {
        const devices::ParamInfo* info = devices::paramInfo(mod->type, p.index);
        if (!info || size_t(p.index) >= mod->params.size() || info->kind == devices::ParamInfo::Kind::Toggle)
            return nullptr;
        ParamRef ref = settingRef(p.index);
        std::function<void()> onClick =
            info->kind == devices::ParamInfo::Kind::Choice
                ? std::function<void()>([c, ref] { deviceActions::openChoiceMenu(*c, ref); })
                : std::function<void()>([c, ref] { deviceActions::openTyping(*c, ref); });
        return std::make_unique<ClickOrDrag>(
            std::make_unique<SettingDrag>(*this, ctx, p.index, ref, mod->params[size_t(p.index)], *info, pos), pos,
            onClick);
    }
    case Kind::RouteAmount:
        return std::make_unique<RouteAmountDrag>(*this, ctx, mod->id, p.index, mod->routes[size_t(p.index)].amount,
                                                 pos);
    case Kind::StepBars: {
        int count = 0;
        std::vector<float> steps = stepsOf(*mod, &count);
        auto drag = std::make_unique<StepsDrag>(*this, ctx, mod->id, steps, count, p.rect);
        drag->update(pos, mods); // a click sets the bar under it
        return drag;
    }
    default:
        return nullptr;
    }
}

void ModulatorPopover::click(Vec pos, int button, Modifiers mods) {
    const Part* hit = partAt(pos);
    TesseraModule* m = ctx.module();
    const Modulator* mod = modulator();
    if (!hit || !m || !mod || button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    switch (hit->kind) {
    case Kind::Close:
        ctx.openModulator = 0;
        break;
    case Kind::Setting: {
        // Only toggles get here: they flip on a click.
        ParamRef ref = settingRef(hit->index);
        if (size_t(hit->index) < mod->params.size())
            deviceActions::setKnob(ctx, ref, mod->params[size_t(hit->index)] >= 0.5f ? 0.f : 1.f);
        break;
    }
    case Kind::RouteRemove:
        m->perform(removeRoute(m->song(), mod->id, hit->index));
        break;
    case Kind::Map:
        ctx.mappingModulator = mod->id.value;
        ctx.openModulator = 0;
        break;
    case Kind::CvOut:
        deviceActions::openCvOutMenu(ctx, mod->id);
        break;
    default:
        break;
    }
}

void ModulatorPopover::doubleClick(Vec pos, Modifiers mods) {
    const Part* hit = partAt(pos);
    const Modulator* mod = modulator();
    if (!hit || !mod || hit->kind != Kind::Setting)
        return;
    if (const devices::ParamInfo* info = devices::paramInfo(mod->type, hit->index))
        if (info->kind != devices::ParamInfo::Kind::Toggle)
            deviceActions::setKnob(ctx, settingRef(hit->index), devices::defaultNormalized(*info));
}

CursorShape ModulatorPopover::hoverCursor(Vec pos, Modifiers mods) {
    const Part* hit = partAt(pos);
    hovered = hit ? int(hit - parts.data()) : -1;
    if (!hit)
        return CursorShape::Arrow;
    switch (hit->kind) {
    case Kind::Setting:
    case Kind::RouteAmount:
        return CursorShape::ResizeH;
    case Kind::StepBars:
        return CursorShape::Crosshair;
    default:
        return CursorShape::Hand;
    }
}

void ModulatorPopover::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    ModulatorPlace place;
    const Modulator* mod = modulator(&place);
    if (!mod)
        return;
    const Song& song = ctx.module()->song();
    NVGcolor color = deviceActions::colorOf(mod->id);
    Rect panel(Vec(), box.size);
    nvgBeginPath(vg);
    nvgRect(vg, -10, -6, box.size.x + 20, box.size.y + 22);
    nvgFillPaint(
        vg, nvgBoxGradient(vg, 0, 4, box.size.x, box.size.y, 7, 14, nvgRGBAf(0, 0, 0, 0.55f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    draw::roundedRect(vg, panel, 7, c.bg1);
    draw::roundedRectStroke(vg, panel, 7, c.line1, 1);

    // Header: colour, name, what it is and what it reaches.
    float hy = kHeader / 2 + 2;
    nvgBeginPath(vg);
    nvgCircle(vg, kPad + 5, hy, 4.5f);
    nvgFillColor(vg, color);
    nvgFill(vg);
    draw::TextStyle title{theme::fonts::uiStrong(), theme::metrics::kTextHeader, c.text0,
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle sub{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::text(vg, title, kPad + 15, hy, mod->name);
    float x = kPad + 15 + draw::textWidth(vg, title, mod->name) + 8;
    std::string reach = place.level == ModLevel::Song    ? "reaches every track"
                        : place.level == ModLevel::Track ? "reaches this track"
                                                         : "on " + deviceActions::whereName(song, place);
    draw::text(
        vg, sub, x, hy,
        draw::ellipsize(vg, sub, std::string(devices::modulatorInfo(mod->type).name) + " · " + reach, kWidth - x - 30));

    draw::TextStyle label{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle value{theme::fonts::mono(), 10.5f, c.text0, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    draw::TextStyle head{theme::fonts::uiStrong(), 9.5f, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                         theme::metrics::kPanelLabelSpacing};
    nvgSave(vg);
    nvgIntersectScissor(vg, 0, kHeader, box.size.x, box.size.y - kHeader);
    for (size_t i = 0; i < parts.size(); i++) {
        const Part& p = parts[i];
        bool hot = int(i) == hovered;
        Vec mid = p.rect.getCenter();
        if (p.kind == Kind::Close)
            continue; // in the header, which never scrolls
        switch (p.kind) {
        case Kind::Close:
            if (hot)
                draw::roundedRect(vg, p.rect, 3, c.line0);
            nvgBeginPath(vg);
            nvgMoveTo(vg, mid.x - 4, mid.y - 4);
            nvgLineTo(vg, mid.x + 4, mid.y + 4);
            nvgMoveTo(vg, mid.x + 4, mid.y - 4);
            nvgLineTo(vg, mid.x - 4, mid.y + 4);
            nvgStrokeColor(vg, c.text1);
            nvgStrokeWidth(vg, 1.3f);
            nvgStroke(vg);
            break;
        case Kind::Setting: {
            const devices::ParamInfo* info = devices::paramInfo(mod->type, p.index);
            if (!info || size_t(p.index) >= mod->params.size())
                break;
            float v = settingPreview && settingPreview->first == p.index ? settingPreview->second
                                                                         : mod->params[size_t(p.index)];
            draw::roundedRect(vg, p.rect, 3, hot ? c.line0 : c.bg2);
            std::string name = settingLabel(*mod, p.index, info->name);
            std::string text = devices::format(*info, v);
            if (info->kind == devices::ParamInfo::Kind::Choice)
                text += " ▾";
            float textW = draw::textWidth(vg, value, text);
            draw::text(vg, label, p.rect.pos.x + 7, mid.y,
                       draw::ellipsize(vg, label, name, p.rect.size.x - textW - 20));
            draw::text(vg, value, p.rect.getRight() - 7, mid.y, text);
            break;
        }
        case Kind::StepBars: {
            int count = 0;
            std::vector<float> steps = stepsOf(*mod, &count);
            if (stepsPreview)
                steps = *stepsPreview;
            draw::roundedRect(vg, p.rect, 3, c.bg0);
            float bw = p.rect.size.x / float(count);
            nvgBeginPath(vg);
            for (int k = 0; k < count; k++) {
                float h = std::max(1.f, steps[size_t(k)] * (p.rect.size.y - 4));
                nvgRect(vg, p.rect.pos.x + float(k) * bw + 1, p.rect.getBottom() - 2 - h, std::max(1.f, bw - 2), h);
            }
            nvgFillColor(vg, color);
            nvgFill(vg);
            break;
        }
        case Kind::RouteAmount: {
            const Modulation& route = mod->routes[size_t(p.index)];
            float amount = amountPreview && amountPreview->first == p.index ? amountPreview->second : route.amount;
            float rowY = mid.y;
            draw::text(vg, label, kPad + 2, rowY,
                       draw::ellipsize(vg, label, deviceActions::targetName(song, route), p.rect.pos.x - kPad - 8));
            Rect track(p.rect.pos.x, rowY - 3, p.rect.size.x, 6);
            draw::roundedRect(vg, track, 3, hot ? c.line1 : c.line0);
            float zero = track.getCenter().x;
            float end = zero + amount * kBarWidth / 2;
            draw::roundedRect(vg, Rect(std::min(zero, end), track.pos.y, std::max(2.f, std::fabs(end - zero)), 6), 3,
                              color);
            char text[16];
            snprintf(text, sizeof(text), "%+.0f%%", double(amount) * 100);
            draw::text(vg, value, p.rect.getRight() + 40, rowY, text);
            break;
        }
        case Kind::RouteRemove:
            if (hot)
                draw::roundedRect(vg, p.rect, 3, c.line0);
            nvgBeginPath(vg);
            nvgMoveTo(vg, mid.x - 3.5f, mid.y - 3.5f);
            nvgLineTo(vg, mid.x + 3.5f, mid.y + 3.5f);
            nvgMoveTo(vg, mid.x + 3.5f, mid.y - 3.5f);
            nvgLineTo(vg, mid.x - 3.5f, mid.y + 3.5f);
            nvgStrokeColor(vg, c.text2);
            nvgStrokeWidth(vg, 1.2f);
            nvgStroke(vg);
            break;
        case Kind::Map:
        case Kind::CvOut: {
            draw::roundedRect(vg, p.rect, 4, hot ? c.line0 : c.bg2);
            draw::TextStyle button{theme::fonts::ui(), theme::metrics::kTextUi, c.text0,
                                   NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
            float tx = p.rect.pos.x + 9;
            if (p.kind == Kind::Map) {
                nvgBeginPath(vg);
                nvgMoveTo(vg, tx, mid.y + 3.5f);
                nvgLineTo(vg, tx + 7, mid.y - 3.5f);
                nvgMoveTo(vg, tx + 2.5f, mid.y - 3.5f);
                nvgLineTo(vg, tx + 7, mid.y - 3.5f);
                nvgLineTo(vg, tx + 7, mid.y + 1);
                nvgStrokeColor(vg, color);
                nvgStrokeWidth(vg, 1.4f);
                nvgLineCap(vg, NVG_ROUND);
                nvgStroke(vg);
                draw::text(vg, button, tx + 13, mid.y, "Map to knobs");
            } else {
                draw::text(vg, button, tx, mid.y, "CV out");
                float ax = tx + draw::textWidth(vg, button, "CV out") + 8;
                nvgBeginPath(vg);
                nvgMoveTo(vg, ax - 3, mid.y - 1.5f);
                nvgLineTo(vg, ax, mid.y + 1.5f);
                nvgLineTo(vg, ax + 3, mid.y - 1.5f);
                nvgStrokeColor(vg, c.text1);
                nvgStrokeWidth(vg, 1.2f);
                nvgStroke(vg);
            }
            break;
        }
        }
    }
    draw::text(vg, head, kPad + 2, movesTitleY + 8, "MOVES");
    if (mod->routes.empty()) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, kPad + 2, movesTitleY + 18 + kRow / 2, "Nothing yet: map it to knobs, or send it out");
    }
    nvgRestore(vg);
    // More below: a fade at the cut edge says the wheel scrolls.
    if (scroll < height - box.size.y - 1) {
        nvgBeginPath(vg);
        nvgRect(vg, 1, box.size.y - 18, box.size.x - 2, 17);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, box.size.y - 18, 0, box.size.y - 1, nvgTransRGBAf(c.bg1, 0), c.bg1));
        nvgFill(vg);
    }
    for (size_t i = 0; i < parts.size(); i++) {
        const Part& p = parts[i];
        if (p.kind != Kind::Close)
            continue;
        Vec mid = p.rect.getCenter();
        if (int(i) == hovered)
            draw::roundedRect(vg, p.rect, 3, c.line0);
        nvgBeginPath(vg);
        nvgMoveTo(vg, mid.x - 4, mid.y - 4);
        nvgLineTo(vg, mid.x + 4, mid.y + 4);
        nvgMoveTo(vg, mid.x + 4, mid.y - 4);
        nvgLineTo(vg, mid.x - 4, mid.y + 4);
        nvgStrokeColor(vg, c.text1);
        nvgStrokeWidth(vg, 1.3f);
        nvgStroke(vg);
    }
}

} // namespace tessera
