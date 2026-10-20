#include "DeviceStrip.hpp"

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
using Kind = DeviceStrip::Part::Kind;

namespace {

constexpr float kPad = 8;
constexpr float kModWidth = 214;
constexpr float kModHeader = 24;
constexpr float kRow = 22;
constexpr float kCardHeader = 24;
constexpr float kCell = 60;
constexpr float kCardPad = 4;
constexpr float kConnector = 26;
constexpr float kInWidth = 30;
constexpr float kAddTile = 92;
constexpr float kOutWidth = 40;
constexpr float kKnobRadius = 13.5f;
constexpr float kKnobPx = 160;   // dragging this far turns a knob end to end
constexpr float kAmountPx = 100; // dragging this far sets a modulation to its full amount
constexpr float kPi = 3.14159265f;

bool drawMappingBanner(NVGcontext* vg, EditorContext& ctx, float x, float y, float height, float maxWidth);

float angleOf(float t) { return (135.f + 270.f * std::clamp(t, 0.f, 1.f)) * kPi / 180.f; }

void arc(NVGcontext* vg, Vec c, float r, float t0, float t1, NVGcolor color, float width) {
    if (t1 < t0)
        std::swap(t0, t1);
    if (t1 - t0 < 0.002f)
        return;
    nvgBeginPath(vg);
    nvgArc(vg, c.x, c.y, r, angleOf(t0), angleOf(t1), NVG_CW);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, width);
    nvgLineCap(vg, NVG_ROUND);
    nvgStroke(vg);
}

struct Row {
    ModulatorId id;
    ModulatorPlace place;
};

/// Every modulator the selected track can use, as the list shows them: the song's, the track's, then each
/// device's in chain order.
std::vector<Row> modulatorRows(const Song& song, const Track& t) {
    std::vector<Row> rows;
    for (const Modulator& m : song.modulators)
        rows.push_back({m.id, {ModLevel::Song, TrackId(), DeviceId()}});
    for (const Modulator& m : t.modulators)
        rows.push_back({m.id, {ModLevel::Track, t.id, DeviceId()}});
    for (const Device& d : t.devices)
        for (const Modulator& m : d.modulators)
            rows.push_back({m.id, {ModLevel::Device, t.id, d.id}});
    return rows;
}

// ---- Gestures -------------------------------------------------------------------------------------------------

/// Turning a knob: the engine plays it while dragging; one undo step on release.
class KnobDrag : public DragGesture {
  public:
    KnobDrag(DeviceStrip& strip, EditorContext& ctx, ParamRef ref, float start, devices::ParamInfo info, Vec press)
        : strip(strip), ctx(ctx), ref(ref), start(start), value(start), info(std::move(info)), press(press) {}
    void update(Vec pos, Modifiers mods) override {
        value = std::clamp(start + (press.y - pos.y) / kKnobPx * (mods.shift ? 0.2f : 1.f), 0.f, 1.f);
        strip.knobPreview = std::make_pair(ref, value);
        if (TesseraModule* m = ctx.module())
            m->holdKnob(ref, value);
    }
    void commit() override {
        TesseraModule* m = ctx.module();
        if (const AutomationEnvelope* lane = m ? m->recordingKnobLane(ref) : nullptr) {
            // Recording: the lane takes the move. In Latch and Write the knob stays where the hand left it until
            // the pass ends.
            strip.knobPreview.reset();
            if (lane->recordMode == AutoRecordMode::Touch)
                m->releaseKnob();
            return;
        }
        finish();
        if (value != start)
            deviceActions::setKnob(ctx, ref, value);
    }
    void cancel() override { finish(); }
    std::string readout() const override { return std::string(info.name) + "  " + devices::format(info, value); }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    void finish() {
        strip.knobPreview.reset();
        if (TesseraModule* m = ctx.module())
            m->releaseKnob();
    }
    DeviceStrip& strip;
    EditorContext& ctx;
    ParamRef ref;
    float start, value;
    devices::ParamInfo info;
    Vec press;
};

/// Mapping: dragging a lit knob sets how far the modulator moves it (up positive, down negative).
class AmountDrag : public DragGesture {
  public:
    AmountDrag(DeviceStrip& strip, EditorContext& ctx, ModulatorId mod, ParamRef ref, Vec press)
        : strip(strip), ctx(ctx), mod(mod), ref(ref), press(press) {
        if (TesseraModule* m = ctx.module())
            if (const Modulator* mm = findModulator(m->song(), mod))
                for (size_t k = 0; k < mm->routes.size(); k++)
                    if (mm->routes[k].kind == Modulation::Kind::Param && mm->routes[k].target == ref) {
                        index = int(k);
                        start = mm->routes[k].amount;
                    }
        amount = start;
    }
    void update(Vec pos, Modifiers mods) override {
        amount = std::clamp(start + (press.y - pos.y) / kAmountPx * (mods.shift ? 0.2f : 1.f), -1.f, 1.f);
        strip.amountPreview = std::make_pair(ref, amount);
    }
    void commit() override {
        strip.amountPreview.reset();
        TesseraModule* m = ctx.module();
        if (!m || amount == start)
            return;
        if (index >= 0) {
            m->perform(setRouteAmount(m->song(), mod, index, amount));
        } else if (std::fabs(amount) > 0.005f) {
            Modulation route;
            route.target = ref;
            route.amount = amount;
            m->perform(addRoute(m->song(), mod, route));
        }
    }
    void cancel() override { strip.amountPreview.reset(); }
    std::string readout() const override {
        char text[24];
        snprintf(text, sizeof(text), "%+.0f%%", double(amount) * 100);
        return text;
    }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    DeviceStrip& strip;
    EditorContext& ctx;
    ModulatorId mod;
    ParamRef ref;
    Vec press;
    int index = -1;
    float start = 0, amount = 0;
};

/// Dragging a card by its header: reorder, or with Ctrl a copy at the drop place.
class CardDrag : public DragGesture {
  public:
    CardDrag(DeviceStrip& strip, EditorContext& ctx, DeviceId id, std::vector<float> centers)
        : strip(strip), ctx(ctx), id(id), centers(std::move(centers)) {}
    void update(Vec pos, Modifiers mods) override {
        int at = 0;
        while (at < int(centers.size()) && pos.x > centers[size_t(at)])
            at++;
        strip.dropIndex = at;
        copy = copy || mods.ctrl; // stays a copy once asked for, like note and clip drags
    }
    void commit() override {
        int at = strip.dropIndex;
        strip.dropIndex = -1;
        TesseraModule* m = ctx.module();
        if (!m || at < 0)
            return;
        TrackId track = ctx.state().selectedTrack;
        if (copy)
            m->perform(copyDevice(m->song(), id, track, at, m->ids()));
        else
            m->perform(moveDevice(m->song(), id, track, at));
    }
    void cancel() override { strip.dropIndex = -1; }
    std::string readout() const override { return copy ? "Copy here" : "Move here"; }
    CursorShape cursor() const override { return CursorShape::Move; }
    bool autoScrollX() const override { return true; }

  private:
    DeviceStrip& strip;
    EditorContext& ctx;
    DeviceId id;
    std::vector<float> centers;
    bool copy = false;
};

} // namespace

// ---- Layout ---------------------------------------------------------------------------------------------------

const Track* DeviceStrip::track() { return deviceActions::track(ctx); }

float DeviceStrip::wantedHeight() {
    const Track* t = ctx.module() ? track() : nullptr;
    if (!t)
        return 0;
    if (deviceActions::stripOpen(ctx))
        return kOpenHeight;
    return t->devices.empty() && t->modulators.empty() ? 0 : kClosedHeight;
}

void DeviceStrip::layoutParts() {
    parts.clear();
    const Track* t = track();
    if (!t)
        return;
    float h = box.size.y;
    if (!deviceActions::stripOpen(ctx) || squeezed()) {
        parts.push_back({Kind::ClosedBar, Rect(0, 0, box.size.x, h)});
        return;
    }
    const Song& song = ctx.module()->song();
    Rect block(kPad, kPad, kModWidth, h - 2 * kPad);
    parts.push_back({Kind::AddModulator, Rect(block.getRight() - 50, block.pos.y + 3, 46, 18)});
    float y = block.pos.y + kModHeader + 3 - modScroll;
    for (const Row& r : modulatorRows(song, *t)) {
        Rect row(block.pos.x + 3, y, block.size.x - 6, kRow);
        if (row.pos.y >= block.pos.y + kModHeader && row.getBottom() <= block.getBottom()) {
            parts.push_back({Kind::ModulatorMap, Rect(row.getRight() - 21, y + 1, 20, 20), -1, -1, r.id.value});
            parts.push_back({Kind::ModulatorRow, row, -1, -1, r.id.value});
        }
        y += kRow + 1;
    }

    chainLeft = block.getRight() + 4;
    float x = chainLeft - scrollX;
    parts.push_back({Kind::Insert, Rect(x, kPad, kInWidth, h - 2 * kPad), 0});
    x += kInWidth;
    float bodyTop = kPad + kCardHeader;
    float y0 = bodyTop + (h - 2 * kPad - kCardHeader - 64) / 2;
    for (size_t i = 0; i < t->devices.size(); i++) {
        const Device& d = t->devices[i];
        int n = int(devices::deviceInfo(d.type).params.size());
        Rect card(x, kPad, 2 * kCardPad + n * kCell, h - 2 * kPad);
        int index = int(i);
        parts.push_back({Kind::Power, Rect(card.pos.x + 4, kPad + 3, 18, 18), index, -1, d.id.value});
        Rect menu(card.getRight() - 22, kPad + 3, 18, 18);
        parts.push_back({Kind::Menu, menu, index, -1, d.id.value});
        for (size_t k = 0; k < d.modulators.size(); k++)
            parts.push_back({Kind::Dot, Rect(menu.pos.x - 12 - float(k) * 11, kPad + 7, 10, 10), index, int(k),
                             d.modulators[k].id.value});
        parts.push_back({Kind::CardHeader, Rect(card.pos.x, kPad, card.size.x, kCardHeader), index, -1, d.id.value});
        for (int k = 0; k < n; k++) {
            float cx = card.pos.x + kCardPad + float(k) * kCell;
            parts.push_back({Kind::KnobValue, Rect(cx, y0 + 52, kCell, 13), index, k, d.id.value});
            parts.push_back({Kind::Knob, Rect(cx, y0 - 2, kCell, 54), index, k, d.id.value});
        }
        parts.push_back({Kind::Card, card, index, -1, d.id.value});
        x = card.getRight();
        parts.push_back({Kind::Insert, Rect(x, kPad, kConnector, h - 2 * kPad), index + 1});
        x += kConnector;
    }
    parts.push_back({Kind::AddTile, Rect(x, kPad, kAddTile, h - 2 * kPad)});
    x += kAddTile + kOutWidth;
    contentWidth = x + scrollX - chainLeft;
}

const DeviceStrip::Part* DeviceStrip::partAt(Vec pos) const {
    for (const Part& p : parts) {
        bool inChain = p.kind != Kind::AddModulator && p.kind != Kind::ModulatorRow && p.kind != Kind::ModulatorMap &&
                       p.kind != Kind::ClosedBar;
        if (inChain && pos.x < chainLeft)
            continue; // the modulator list covers what scrolled under it
        if (p.rect.contains(pos))
            return &p;
    }
    return nullptr;
}

bool DeviceStrip::reachableNow(const ParamRef& ref) {
    TesseraModule* m = ctx.module();
    if (!m || !ctx.mappingModulator)
        return false;
    Modulation route;
    route.target = ref;
    ModulatorPlace place;
    if (!findModulator(m->song(), ModulatorId(ctx.mappingModulator), &place))
        return false;
    return reachable(m->song(), place, ModulatorId(ctx.mappingModulator), route);
}

float DeviceStrip::modScrollMax() {
    const Track* t = track();
    if (!t)
        return 0;
    float rows = float(modulatorRows(ctx.module()->song(), *t).size());
    float room = box.size.y - 2 * kPad - kModHeader - 6;
    return std::max(0.f, rows * (kRow + 1) - room);
}

void DeviceStrip::sampleScopes() {
    TesseraModule* m = ctx.module();
    scopeHead = (scopeHead + 1) % 40;
    forEachModulator(m->song(), [&](const Modulator& mod, const ModulatorPlace&) {
        float v = m->modulatorOutput(mod.id);
        if (mods::bipolar(mod.type, mod.params.data()))
            v = (v + 1) / 2;
        scopes[mod.id.value][size_t(scopeHead)] = std::clamp(v, 0.f, 1.f);
    });
}

void DeviceStrip::step() {
    if (ctx.module()) {
        const Track* t = track();
        if (t && t->id != shownTrack) {
            shownTrack = t->id;
            scrollX = 0;
            modScroll = 0;
        }
        if (ctx.selectedDevice && !(t && findDevice(ctx.module()->song(), DeviceId(ctx.selectedDevice)) &&
                                    std::any_of(t->devices.begin(), t->devices.end(),
                                                [&](const Device& d) { return d.id.value == ctx.selectedDevice; })))
            ctx.selectedDevice = 0;
        if (ctx.mappingModulator && !findModulator(ctx.module()->song(), ModulatorId(ctx.mappingModulator)))
            ctx.mappingModulator = 0;
        float most = std::max(0.f, contentWidth - (box.size.x - chainLeft));
        scrollX = std::clamp(scrollX, 0.f, most);
        // Never scrolled past the end: removing modulators (or a wheel turn while the list fits) can't empty it.
        modScroll = std::clamp(modScroll, 0.f, modScrollMax());
        layoutParts();
        sampleScopes();
    }
    InteractiveView::step();
}

bool DeviceStrip::handleAction(Action action) {
    TesseraModule* m = ctx.module();
    if (!m || !ctx.selectedDevice || !deviceActions::stripOpen(ctx))
        return false;
    DeviceId id(ctx.selectedDevice);
    switch (action) {
    case Action::Delete:
        m->perform(removeDevice(m->song(), id));
        ctx.selectedDevice = 0;
        return true;
    case Action::Duplicate:
        m->perform(duplicateDevice(m->song(), id, m->ids()));
        return true;
    default:
        return false;
    }
}

// ---- Input ----------------------------------------------------------------------------------------------------

std::unique_ptr<DragGesture> DeviceStrip::startGesture(Vec pos, int button, Modifiers mods) {
    const Part* hit = partAt(pos);
    TesseraModule* m = ctx.module();
    if (button != GLFW_MOUSE_BUTTON_LEFT || !hit || !m)
        return nullptr;
    Part p = *hit;
    EditorContext* c = &ctx;
    switch (p.kind) {
    case Kind::Knob:
    case Kind::KnobValue: {
        ParamRef ref = knobRef(p);
        const devices::ParamInfo* info = deviceActions::infoOf(m->song(), ref);
        const float* value = findParam(m->song(), ref);
        if (!info || !value)
            return nullptr;
        if (ctx.mappingModulator) {
            if (!reachableNow(ref))
                return nullptr;
            ModulatorId mod(ctx.mappingModulator);
            // A click without a drag adds a modulation of +50%.
            return std::make_unique<ClickOrDrag>(std::make_unique<AmountDrag>(*this, ctx, mod, ref, pos), pos,
                                                 [c, mod, ref] {
                                                     TesseraModule* mm = c->module();
                                                     if (!mm || modulationsOn(mm->song(), ref) > 0)
                                                         return;
                                                     Modulation route;
                                                     route.target = ref;
                                                     route.amount = 0.5f;
                                                     mm->perform(addRoute(mm->song(), mod, route));
                                                 });
        }
        if (info->kind == devices::ParamInfo::Kind::Toggle)
            return nullptr; // a click flips it
        DeviceId device(p.id);
        std::function<void()> onClick;
        if (p.kind == Kind::KnobValue)
            onClick = [c, ref] { deviceActions::openTyping(*c, ref); };
        else if (info->kind == devices::ParamInfo::Kind::Choice)
            onClick = [c, ref] { deviceActions::openChoiceMenu(*c, ref); };
        else
            onClick = [c, device] { c->selectedDevice = device.value; };
        // An automated knob starts from where its lane has it, so it doesn't jump.
        float start = m->automated(ref) ? m->knobBase(ref) : *value;
        return std::make_unique<ClickOrDrag>(std::make_unique<KnobDrag>(*this, ctx, ref, start, *info, pos), pos,
                                             onClick);
    }
    case Kind::CardHeader: {
        std::vector<float> centers;
        for (const Part& q : parts)
            if (q.kind == Kind::Card)
                centers.push_back(q.rect.getCenter().x);
        DeviceId device(p.id);
        return std::make_unique<ClickOrDrag>(std::make_unique<CardDrag>(*this, ctx, device, centers), pos,
                                             [c, device] { c->selectedDevice = device.value; });
    }
    default:
        return nullptr;
    }
}

void DeviceStrip::click(Vec pos, int button, Modifiers mods) {
    TesseraModule* m = ctx.module();
    const Track* t = track();
    if (!m || !t)
        return;
    const Part* hit = partAt(pos);
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (!hit)
            return;
        switch (hit->kind) {
        case Kind::Knob:
        case Kind::KnobValue:
            deviceActions::openKnobMenu(ctx, knobRef(*hit));
            break;
        case Kind::Card:
        case Kind::CardHeader:
        case Kind::Power:
        case Kind::Menu:
            deviceActions::openDeviceMenu(ctx, DeviceId(hit->id));
            break;
        case Kind::ModulatorRow:
        case Kind::ModulatorMap:
        case Kind::Dot:
            deviceActions::openModulatorMenu(ctx, ModulatorId(hit->id));
            break;
        default:
            break;
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    if (!hit) {
        // Empty space ends mapping and drops the card selection.
        ctx.mappingModulator = 0;
        ctx.selectedDevice = 0;
        return;
    }
    switch (hit->kind) {
    case Kind::ClosedBar:
        // Squeezed, the bar can't open further: it says how to make room instead.
        if (!squeezed())
            deviceActions::setStripOpen(ctx, true);
        break;
    case Kind::AddModulator:
        deviceActions::openModulatorBrowser(ctx, t->id);
        break;
    case Kind::ModulatorRow:
    case Kind::Dot:
        ctx.openModulator = ctx.openModulator == hit->id ? 0 : hit->id;
        ctx.mappingModulator = 0;
        break;
    case Kind::ModulatorMap:
        ctx.mappingModulator = ctx.mappingModulator == hit->id ? 0 : hit->id;
        ctx.openModulator = 0;
        break;
    case Kind::Power:
        if (const Device* d = findDevice(m->song(), DeviceId(hit->id)))
            m->perform(setDeviceOn(m->song(), d->id, !d->on));
        break;
    case Kind::Menu:
        deviceActions::openDeviceMenu(ctx, DeviceId(hit->id));
        break;
    case Kind::Knob: {
        // Only toggles get here: they flip on a click.
        ParamRef ref = knobRef(*hit);
        if (const float* v = findParam(m->song(), ref))
            deviceActions::setKnob(ctx, ref, *v >= 0.5f ? 0.f : 1.f);
        break;
    }
    case Kind::Insert:
        deviceActions::openDeviceBrowser(ctx, t->id, hit->device);
        break;
    case Kind::AddTile:
        deviceActions::openDeviceBrowser(ctx, t->id, -1);
        break;
    case Kind::Card:
        ctx.selectedDevice = hit->id;
        ctx.mappingModulator = 0;
        break;
    default:
        break;
    }
}

void DeviceStrip::doubleClick(Vec pos, Modifiers mods) {
    const Part* hit = partAt(pos);
    TesseraModule* m = ctx.module();
    if (!hit || !m || hit->kind != Kind::Knob || ctx.mappingModulator)
        return;
    ParamRef ref = knobRef(*hit);
    if (const devices::ParamInfo* info = deviceActions::infoOf(m->song(), ref))
        if (info->kind != devices::ParamInfo::Kind::Toggle)
            deviceActions::setKnob(ctx, ref, devices::defaultNormalized(*info));
}

void DeviceStrip::wheel(Vec pos, Vec delta, Modifiers mods) {
    if (pos.x < chainLeft) {
        modScroll = std::clamp(modScroll - delta.y * 0.5f, 0.f, modScrollMax());
        return;
    }
    float most = std::max(0.f, contentWidth - (box.size.x - chainLeft));
    scrollX = std::clamp(scrollX - (delta.x != 0 ? delta.x : delta.y), 0.f, most);
}

CursorShape DeviceStrip::hoverCursor(Vec pos, Modifiers mods) {
    const Part* hit = partAt(pos);
    hovered = hit ? int(hit - parts.data()) : -1;
    if (!hit)
        return CursorShape::Arrow;
    switch (hit->kind) {
    case Kind::Knob:
    case Kind::KnobValue:
        if (ctx.mappingModulator && !reachableNow(knobRef(*hit)))
            return CursorShape::NotAllowed;
        return CursorShape::ResizeV;
    case Kind::CardHeader:
        return CursorShape::Move;
    case Kind::Card:
        return CursorShape::Arrow;
    default:
        return CursorShape::Hand;
    }
}

// ---- Drawing --------------------------------------------------------------------------------------------------

void DeviceStrip::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, nvgLerpRGBA(c.bg0, c.bg1, 0.35f));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, box.size.y - 1, box.size.x, 1);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);
    const Track* t = ctx.module() ? track() : nullptr;
    if (!t)
        return;
    if (!deviceActions::stripOpen(ctx) || squeezed()) {
        drawClosed(vg);
        return;
    }
    nvgSave(vg);
    nvgIntersectScissor(vg, 0, 0, box.size.x, box.size.y);
    drawModulators(vg);
    nvgRestore(vg);

    nvgSave(vg);
    nvgIntersectScissor(vg, chainLeft, 0, box.size.x - chainLeft, box.size.y);
    draw::TextStyle small{theme::fonts::ui(), 9.5f, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    for (size_t i = 0; i < parts.size(); i++) {
        const Part& p = parts[i];
        bool hot = int(i) == hovered;
        switch (p.kind) {
        case Kind::Insert: {
            Vec mid = p.rect.getCenter();
            bool first = p.device == 0 && p.rect.size.x == kInWidth;
            if (first) {
                draw::text(vg, small, mid.x, mid.y - 14, "in");
            } else {
                nvgBeginPath(vg);
                nvgRect(vg, p.rect.pos.x, mid.y - 0.5f, p.rect.size.x, 1);
                nvgFillColor(vg, c.line1);
                nvgFill(vg);
            }
            // The insert button is faint until hovered: the row stays calm.
            nvgBeginPath(vg);
            nvgCircle(vg, mid.x, mid.y, 9);
            nvgFillColor(vg, nvgTransRGBAf(c.bg2, hot ? 1.f : 0.6f));
            nvgFill(vg);
            nvgStrokeColor(vg, nvgTransRGBAf(c.line2, hot ? 1.f : 0.4f));
            nvgStrokeWidth(vg, 1);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgRect(vg, mid.x - 4, mid.y - 0.6f, 8, 1.2f);
            nvgRect(vg, mid.x - 0.6f, mid.y - 4, 1.2f, 8);
            nvgFillColor(vg, nvgTransRGBAf(c.text0, hot ? 1.f : 0.35f));
            nvgFill(vg);
            break;
        }
        case Kind::AddTile: {
            draw::roundedRect(vg, p.rect, 6, hot ? c.bg2 : nvgTransRGBAf(c.bg1, 0.6f));
            draw::roundedRectStroke(vg, p.rect, 6, hot ? c.line2 : c.line1, 1);
            draw::TextStyle add{theme::fonts::ui(), theme::metrics::kTextUi, hot ? c.text0 : c.text1,
                                NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            Vec mid = p.rect.getCenter();
            add.size = 16;
            draw::text(vg, add, mid.x, mid.y - 9, "+");
            add.size = theme::metrics::kTextUi;
            draw::text(vg, add, mid.x, mid.y + 9, "Add device");
            draw::text(vg, small, p.rect.getRight() + kOutWidth / 2, mid.y + 12, "out");
            nvgBeginPath(vg);
            float ax = p.rect.getRight() + kOutWidth / 2;
            nvgMoveTo(vg, ax - 7, mid.y);
            nvgLineTo(vg, ax + 6, mid.y);
            nvgMoveTo(vg, ax + 2.5f, mid.y - 3.5f);
            nvgLineTo(vg, ax + 6, mid.y);
            nvgLineTo(vg, ax + 2.5f, mid.y + 3.5f);
            nvgStrokeColor(vg, c.line2);
            nvgStrokeWidth(vg, 1.3f);
            nvgStroke(vg);
            if (t->devices.empty()) {
                draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2,
                                     NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
                draw::text(vg, hint, p.rect.getRight() + kOutWidth + 10, mid.y,
                           "Devices change the notes on their way out: arpeggiator, chord, scale, echo…");
            }
            break;
        }
        case Kind::Card:
            drawCard(vg, t->devices[size_t(p.device)], p.device);
            break;
        default:
            break;
        }
    }
    nvgRestore(vg);
}

void DeviceStrip::drawClosed(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    const Track* t = track();
    bool hot = hovered >= 0;
    if (hot) {
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, box.size.x, box.size.y - 1);
        nvgFillColor(vg, c.bg2);
        nvgFill(vg);
    }
    float mid = box.size.y / 2;
    nvgBeginPath(vg);
    nvgMoveTo(vg, 11, mid - 3.5f);
    nvgLineTo(vg, 14.5f, mid);
    nvgLineTo(vg, 11, mid + 3.5f);
    nvgStrokeColor(vg, c.text1);
    nvgStrokeWidth(vg, 1.3f);
    nvgStroke(vg);
    draw::TextStyle dim{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle name{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    float x = 22;
    draw::text(vg, dim, x, mid, "Devices");
    x += draw::textWidth(vg, dim, "Devices") + 10;
    for (size_t i = 0; i < t->devices.size(); i++) {
        if (i > 0) {
            draw::text(vg, dim, x, mid, "→");
            x += draw::textWidth(vg, dim, "→") + 6;
        }
        name.color = t->devices[i].on ? c.text1 : c.text2;
        draw::text(vg, name, x, mid, t->devices[i].name);
        x += draw::textWidth(vg, name, t->devices[i].name) + 6;
    }
    size_t mods = modulatorRows(ctx.module()->song(), *t).size();
    if (mods > 0)
        draw::text(vg, dim, x + 4, mid, "· " + std::to_string(mods) + (mods == 1 ? " modulator" : " modulators"));
    if (squeezed()) {
        if (mods > 0)
            x += draw::textWidth(vg, dim, "· " + std::to_string(mods) + " modulators") + 14;
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.accent, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, x, mid,
                   draw::ellipsize(vg, hint, "No room to show them here: drag the divider up, or open the full editor",
                                   box.size.x - x - 8));
    }
}

void DeviceStrip::drawModulators(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    const Song& song = ctx.module()->song();
    const Track* t = track();
    Rect block(kPad, kPad, kModWidth, box.size.y - 2 * kPad);
    draw::roundedRect(vg, block, 6, c.bg1);
    draw::roundedRectStroke(vg, block, 6, c.line0, 1);
    draw::TextStyle head{theme::fonts::uiStrong(), 9.5f, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                         theme::metrics::kPanelLabelSpacing};
    draw::text(vg, head, block.pos.x + 9, block.pos.y + kModHeader / 2, "MODULATORS");
    nvgBeginPath(vg);
    nvgRect(vg, block.pos.x, block.pos.y + kModHeader, block.size.x, 1);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);

    draw::TextStyle name{theme::fonts::ui(), theme::metrics::kTextUi, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle where{theme::fonts::ui(), 9.5f, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle button{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    for (size_t i = 0; i < parts.size(); i++) {
        const Part& p = parts[i];
        bool hot = int(i) == hovered;
        if (p.kind == Kind::AddModulator) {
            if (hot)
                draw::roundedRect(vg, p.rect, 3, c.line0);
            draw::text(vg, button, p.rect.getCenter().x, p.rect.getCenter().y, "+ Add");
            continue;
        }
        if (p.kind != Kind::ModulatorRow)
            continue;
        ModulatorPlace place;
        const Modulator* mod = findModulator(song, ModulatorId(p.id), &place);
        if (!mod)
            continue;
        bool open = ctx.openModulator == p.id, mapping = ctx.mappingModulator == p.id;
        bool mapHot =
            hovered >= 0 && parts[size_t(hovered)].kind == Kind::ModulatorMap && parts[size_t(hovered)].id == p.id;
        if (open || mapping || hot || mapHot)
            draw::roundedRect(vg, p.rect, 4, open || mapping ? c.bg2 : nvgTransRGBAf(c.bg2, 0.6f));
        NVGcolor color = deviceActions::colorOf(mod->id);
        float mid = p.rect.getCenter().y;
        nvgBeginPath(vg);
        nvgCircle(vg, p.rect.pos.x + 9, mid, 4);
        nvgFillColor(vg, color);
        nvgFill(vg);
        float x = p.rect.pos.x + 19;
        std::string w = deviceActions::whereName(song, place);
        float right = p.rect.getRight() - 24 - 34;
        float nameW = std::min(draw::textWidth(vg, name, mod->name), right - x - 20);
        draw::text(vg, name, x, mid, draw::ellipsize(vg, name, mod->name, nameW + 1));
        draw::text(vg, where, x + nameW + 6, mid, draw::ellipsize(vg, where, w, right - (x + nameW + 6)));
        // The last moments of its output, as a little graph.
        auto it = scopes.find(mod->id.value);
        if (it != scopes.end()) {
            Rect g(p.rect.getRight() - 24 - 32, mid - 6, 30, 12);
            nvgBeginPath(vg);
            for (int k = 0; k < 40; k++) {
                float v = it->second[size_t((scopeHead + 1 + k) % 40)];
                float px = g.pos.x + g.size.x * float(k) / 39.f, py = g.getBottom() - v * g.size.y;
                k == 0 ? nvgMoveTo(vg, px, py) : nvgLineTo(vg, px, py);
            }
            nvgStrokeColor(vg, color);
            nvgStrokeWidth(vg, 1.3f);
            nvgStroke(vg);
        }
        // The map arrow.
        Rect a(p.rect.getRight() - 21, p.rect.pos.y + 1, 20, 20);
        if (mapping)
            draw::roundedRect(vg, a, 4, nvgTransRGBAf(color, 0.25f));
        else if (mapHot)
            draw::roundedRect(vg, a, 4, c.line0);
        Vec ac = a.getCenter();
        nvgBeginPath(vg);
        nvgMoveTo(vg, ac.x - 3.5f, ac.y + 3.5f);
        nvgLineTo(vg, ac.x + 3.5f, ac.y - 3.5f);
        nvgMoveTo(vg, ac.x - 1, ac.y - 3.5f);
        nvgLineTo(vg, ac.x + 3.5f, ac.y - 3.5f);
        nvgLineTo(vg, ac.x + 3.5f, ac.y + 1);
        nvgStrokeColor(vg, mapping ? color : c.text1);
        nvgStrokeWidth(vg, 1.4f);
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
    }
    if (modulatorRows(song, *t).empty()) {
        draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
        draw::textBox(vg, hint, block.pos.x + 10, block.pos.y + kModHeader + 10, block.size.x - 20,
                      "LFOs, envelopes and ramps that turn knobs for you. + Add to start.");
    }
}

void DeviceStrip::drawCard(NVGcontext* vg, const Device& d, int index) {
    const theme::Palette& c = theme::colors();
    const Part* card = nullptr;
    for (const Part& p : parts)
        if (p.kind == Kind::Card && p.device == index)
            card = &p;
    if (!card)
        return;
    Rect r = card->rect;
    bool selected = ctx.selectedDevice == d.id.value;
    NVGcolor border = selected ? c.select : c.line0;
    // A device modulator being mapped outlines its own device: the only one it may reach.
    if (ctx.mappingModulator) {
        ModulatorPlace place;
        if (findModulator(ctx.module()->song(), ModulatorId(ctx.mappingModulator), &place) &&
            place.level == ModLevel::Device && place.device == d.id)
            border = deviceActions::colorOf(ModulatorId(ctx.mappingModulator));
    }
    draw::roundedRect(vg, r, 6, c.bg1);
    draw::roundedRectStroke(vg, r, 6, border, selected ? 1.5f : 1);
    nvgBeginPath(vg);
    nvgRect(vg, r.pos.x, r.pos.y + kCardHeader, r.size.x, 1);
    nvgFillColor(vg, c.line0);
    nvgFill(vg);

    for (size_t i = 0; i < parts.size(); i++) {
        const Part& p = parts[i];
        if (p.device != index)
            continue;
        bool hot = int(i) == hovered;
        switch (p.kind) {
        case Kind::Power: {
            Vec m = p.rect.getCenter();
            nvgBeginPath(vg);
            nvgCircle(vg, m.x, m.y, 9);
            nvgFillColor(vg, d.on ? nvgTransRGBAf(c.accent, 0.18f) : (hot ? c.line0 : nvgRGBAf(0, 0, 0, 0)));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgArc(vg, m.x, m.y + 0.5f, 4, -kPi / 2 + 0.75f, -kPi / 2 - 0.75f + 2 * kPi, NVG_CW);
            nvgMoveTo(vg, m.x, m.y - 4.5f);
            nvgLineTo(vg, m.x, m.y - 0.5f);
            nvgStrokeColor(vg, d.on ? c.accent : c.text2);
            nvgStrokeWidth(vg, 1.4f);
            nvgLineCap(vg, NVG_ROUND);
            nvgStroke(vg);
            break;
        }
        case Kind::Menu: {
            if (hot)
                draw::roundedRect(vg, p.rect, 4, c.line0);
            Vec m = p.rect.getCenter();
            nvgBeginPath(vg);
            for (int k = -1; k <= 1; k++)
                nvgCircle(vg, m.x + float(k) * 4, m.y, 1.2f);
            nvgFillColor(vg, c.text1);
            nvgFill(vg);
            break;
        }
        case Kind::Dot: {
            Vec m = p.rect.getCenter();
            nvgBeginPath(vg);
            nvgCircle(vg, m.x, m.y, hot ? 4 : 3);
            nvgFillColor(vg, deviceActions::colorOf(ModulatorId(p.id)));
            nvgFill(vg);
            break;
        }
        case Kind::CardHeader: {
            float right = r.getRight() - 26 - float(d.modulators.size()) * 11;
            draw::TextStyle name{theme::fonts::uiStrong(), 11.5f, d.on ? c.text0 : c.text2,
                                 NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
            draw::text(vg, name, r.pos.x + 26, p.rect.getCenter().y,
                       draw::ellipsize(vg, name, d.name, right - r.pos.x - 30));
            break;
        }
        default:
            break;
        }
    }
    // A bypassed device fades: its knobs still turn, but nothing passes through it.
    nvgSave(vg);
    if (!d.on)
        nvgGlobalAlpha(vg, 0.4f);
    for (const Part& p : parts)
        if (p.kind == Kind::Knob && p.device == index)
            drawKnob(vg, p);
    nvgRestore(vg);
}

void DeviceStrip::drawKnob(NVGcontext* vg, const Part& part) {
    const theme::Palette& c = theme::colors();
    TesseraModule* m = ctx.module();
    const Song& song = m->song();
    ParamRef ref = knobRef(part);
    const devices::ParamInfo* info = deviceActions::infoOf(song, ref);
    const float* stored = findParam(song, ref);
    if (!info || !stored)
        return;
    // An automated knob shows what its lane sets while the song plays, in the lane's colour.
    bool automated = m->automated(ref);
    float base = knobPreview && knobPreview->first == ref ? knobPreview->second
                 : automated                              ? m->knobBase(ref)
                                                          : *stored;
    NVGcolor valueColor = c.accent;
    if (automated) {
        std::vector<FoundLane> lanes = lanesTurning(song, ref);
        if (!lanes.empty())
            valueColor = deviceActions::laneColor(song, lanes.front());
    }
    bool mapping = ctx.mappingModulator != 0;
    bool lit = mapping && reachableNow(ref);
    nvgSave(vg);
    if (mapping && !lit)
        nvgGlobalAlpha(vg, 0.3f);

    float cx = part.rect.getCenter().x;
    float top = part.rect.pos.y + 2;
    Vec center(cx, top + 19);
    draw::TextStyle label{theme::fonts::ui(), 10, c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    draw::TextStyle value{theme::fonts::mono(), 9.5f, c.text0, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    std::string shown = devices::format(*info, base);

    // Modulations on this knob, in their modulators' colours (the one being mapped shows its dragged amount).
    struct Ring {
        NVGcolor color;
        float amount;
    };
    std::vector<Ring> rings;
    bool mappedHere = false;
    forEachModulator(song, [&](const Modulator& mod, const ModulatorPlace&) {
        for (const Modulation& r : mod.routes)
            if (r.kind == Modulation::Kind::Param && r.target == ref) {
                float amount = r.amount;
                if (amountPreview && amountPreview->first == ref && mod.id.value == ctx.mappingModulator) {
                    amount = amountPreview->second;
                    mappedHere = true;
                }
                rings.push_back({deviceActions::colorOf(mod.id), amount});
            }
    });
    if (amountPreview && amountPreview->first == ref && !mappedHere)
        rings.push_back({deviceActions::colorOf(ModulatorId(ctx.mappingModulator)), amountPreview->second});

    switch (info->kind) {
    case devices::ParamInfo::Kind::Choice: {
        Rect box(cx - 27, center.y - 10, 54, 20);
        bool hot = hovered >= 0 && &parts[size_t(hovered)] == &part;
        draw::roundedRect(vg, box, 4, hot ? c.line0 : c.bg2);
        draw::roundedRectStroke(vg, box, 4,
                                lit         ? deviceActions::colorOf(ModulatorId(ctx.mappingModulator))
                                : automated ? valueColor
                                            : c.line0,
                                1);
        draw::TextStyle text{theme::fonts::ui(), 10.5f, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        std::string s = draw::ellipsize(vg, text, shown, box.size.x - 16);
        float w = draw::textWidth(vg, text, s);
        draw::text(vg, text, cx - (w + 9) / 2, center.y, s);
        float ax = cx + (w + 9) / 2 - 3;
        nvgBeginPath(vg);
        nvgMoveTo(vg, ax - 3, center.y - 1.5f);
        nvgLineTo(vg, ax, center.y + 1.5f);
        nvgLineTo(vg, ax + 3, center.y - 1.5f);
        nvgStrokeColor(vg, c.text1);
        nvgStrokeWidth(vg, 1.2f);
        nvgStroke(vg);
        shown.clear();
        break;
    }
    case devices::ParamInfo::Kind::Toggle: {
        bool on = base >= 0.5f;
        Rect sw(cx - 15, center.y - 8, 30, 16);
        draw::roundedRect(vg, sw, 8, on ? valueColor : c.line1);
        nvgBeginPath(vg);
        nvgCircle(vg, on ? sw.getRight() - 8 : sw.pos.x + 8, center.y, 5);
        nvgFillColor(vg, c.text0);
        nvgFill(vg);
        shown.clear();
        break;
    }
    default: {
        if (lit) {
            NVGcolor color = deviceActions::colorOf(ModulatorId(ctx.mappingModulator));
            nvgBeginPath(vg);
            nvgCircle(vg, center.x, center.y, kKnobRadius + 6);
            nvgFillColor(vg, nvgTransRGBAf(color, 0.12f));
            nvgFill(vg);
            nvgStrokeColor(vg, color);
            nvgStrokeWidth(vg, 1.4f);
            nvgStroke(vg);
        }
        arc(vg, center, kKnobRadius, 0, 1, c.line1, 2.8f);
        arc(vg, center, kKnobRadius, info->bipolar() ? 0.5f : 0.f, base, valueColor, 2.8f);
        for (size_t k = 0; k < rings.size() && k < 4; k++)
            arc(vg, center, kKnobRadius + 3.4f + float(k) * 2.4f, base, std::clamp(base + rings[k].amount, 0.f, 1.f),
                rings[k].color, 1.8f);
        // Where it is right now, when something moves it.
        bool held = knobPreview && knobPreview->first == ref;
        if (!rings.empty() || held) {
            float live = held ? base : m->liveKnob(ref);
            float a = angleOf(live);
            nvgBeginPath(vg);
            nvgCircle(vg, center.x + kKnobRadius * std::cos(a), center.y + kKnobRadius * std::sin(a), 2.4f);
            nvgFillColor(vg, c.text0);
            nvgFill(vg);
        }
        break;
    }
    }
    if (automated) {
        // The "A" badge: this knob has an automation lane.
        Rect badge(part.rect.getRight() - 13, top, 11, 11);
        draw::roundedRect(vg, badge, 2, valueColor);
        draw::TextStyle a{theme::fonts::uiStrong(), 9, c.onTile, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, a, badge.getCenter().x, badge.getCenter().y + 0.5f, "A");
        value.color = valueColor;
    }
    draw::text(vg, label, cx, top + 46, draw::ellipsize(vg, label, info->name, kCell - 4));
    if (!shown.empty())
        draw::text(vg, value, cx, top + 58, shown);
    nvgRestore(vg);
}

void DeviceStrip::drawForeground(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    if (!ctx.module() || !deviceActions::stripOpen(ctx) || squeezed())
        return;
    nvgSave(vg);
    nvgIntersectScissor(vg, chainLeft, 0, box.size.x - chainLeft, box.size.y);
    if (dropIndex >= 0) {
        // Where the dragged card lands: before card `dropIndex`, or after the last.
        float x = -1;
        int seen = 0;
        for (const Part& p : parts)
            if (p.kind == Kind::Card) {
                if (seen == dropIndex)
                    x = p.rect.pos.x - kConnector / 2;
                seen++;
            } else if (p.kind == Kind::AddTile && x < 0) {
                x = p.rect.pos.x - kConnector / 2;
            }
        nvgBeginPath(vg);
        nvgRect(vg, x - 1, kPad, 2, box.size.y - 2 * kPad);
        nvgFillColor(vg, c.accent);
        nvgFill(vg);
    }
    // A modulated knob names its modulations on hover.
    if (hovered >= 0 && size_t(hovered) < parts.size() && parts[size_t(hovered)].kind == Kind::Knob && !hasGesture()) {
        const Part& p = parts[size_t(hovered)];
        ParamRef ref = knobRef(p);
        std::string text;
        forEachModulator(ctx.module()->song(), [&](const Modulator& mod, const ModulatorPlace&) {
            for (const Modulation& r : mod.routes)
                if (r.kind == Modulation::Kind::Param && r.target == ref) {
                    char amount[16];
                    snprintf(amount, sizeof(amount), " %+.0f%%", double(r.amount) * 100);
                    text += (text.empty() ? "" : "   ") + mod.name + amount;
                }
        });
        if (!text.empty()) {
            draw::TextStyle style{theme::fonts::ui(), 10, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
            float w = draw::textWidth(vg, style, text) + 12;
            float x = std::clamp(p.rect.getCenter().x - w / 2, chainLeft + 2, box.size.x - w - 2);
            Rect pill(x, p.rect.pos.y - 14, w, 16);
            draw::roundedRect(vg, pill, 4, c.bg2);
            draw::roundedRectStroke(vg, pill, 4, c.line1, 1);
            draw::text(vg, style, x + 6, pill.getCenter().y, text);
        }
    }
    nvgRestore(vg);
    // Under the strip when the pane has room there; otherwise over the top of the cards.
    float bannerY = roomBelow >= 28 ? box.size.y + 4 : 2;
    drawMappingBanner(vg, ctx, chainLeft, bannerY, 20, box.size.x - chainLeft - 8);
}

namespace {

/// While mapping, what to do, in the modulator's colour: a pill floating just under the strip, over the notes'
/// ruler, where there is always room. False when not mapping.
bool drawMappingBanner(NVGcontext* vg, EditorContext& ctx, float x, float y, float height, float maxWidth) {
    TesseraModule* m = ctx.module();
    const Modulator* mod =
        m && ctx.mappingModulator ? findModulator(m->song(), ModulatorId(ctx.mappingModulator)) : nullptr;
    if (!mod || maxWidth < 60)
        return false;
    const theme::Palette& c = theme::colors();
    NVGcolor color = deviceActions::colorOf(mod->id);
    draw::TextStyle strong{theme::fonts::uiStrong(), theme::metrics::kTextUi, c.text0,
                           NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle text{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::string title = "Mapping " + mod->name;
    std::string hint = "drag a lit knob up or down to set how far it moves it  ·  Esc to finish";
    float w = std::min(maxWidth, 26 + draw::textWidth(vg, strong, title) + 10 + draw::textWidth(vg, text, hint) + 10);
    Rect pill(x, y, w, height);
    draw::roundedRect(vg, pill, 4, c.bg1);
    draw::roundedRect(vg, pill, 4, nvgTransRGBAf(color, 0.14f));
    draw::roundedRectStroke(vg, pill, 4, color, 1);
    nvgBeginPath(vg);
    nvgCircle(vg, pill.pos.x + 10, pill.getCenter().y, 4);
    nvgFillColor(vg, color);
    nvgFill(vg);
    float tx = pill.pos.x + 20;
    draw::text(vg, strong, tx, pill.getCenter().y, title);
    tx += draw::textWidth(vg, strong, title) + 10;
    draw::text(vg, text, tx, pill.getCenter().y, draw::ellipsize(vg, text, hint, pill.getRight() - tx - 6));
    return true;
}

} // namespace

} // namespace tessera
