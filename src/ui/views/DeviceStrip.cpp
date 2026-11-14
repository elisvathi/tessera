#include "DeviceStrip.hpp"

#include "../../model/Modulators.hpp"
#include "../../model/Playheads.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../editing/DeviceActions.hpp"
#include "../interaction/ClickOrDrag.hpp"
#include "../widgets/CachedLayer.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/TagPanel.hpp"
#include "DeviceTiles.hpp"

#include <cmath>
#include <cstring>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;
using Kind = DeviceStrip::Part::Kind;
using tags::Family;
namespace mx = theme::metrics;

namespace {

constexpr float kPad = 8;  // around the modulator list
constexpr float kTop = DeviceStrip::kTop; // the cards' top: room above for a playhead frame
constexpr float kEdge = 8;                 // the strip's bottom edge that resizes it
constexpr float kModWidth = 214;
constexpr float kModHeader = 24;
constexpr float kRow = 22;
constexpr float kCardHeight = mx::kDeviceCardTop + mx::kDeviceCardHeader + mx::kDeviceTile + mx::kDeviceCardPad;
static_assert(kCardHeight == DeviceStrip::kCardHeight, "the strip's rows follow the card's height");
constexpr float kHeaderMid = mx::kDeviceCardTop + mx::kDeviceCardHeader / 2; // the header's middle, from the top
constexpr float kAddWidth = 112;
constexpr float kAmountPx = 100; // dragging this far sets a modulation to its full amount
constexpr float kChipHeight = 20;
constexpr float kChipDot = 18;     // the on/off dot at a chip's left end
constexpr float kSwitchW = 30;     // the shown playhead's On switch
constexpr float kMotionJoin = 64;  // a Motion card's "+ Selected"
constexpr float kMotionLeave = 64; // its "− Selected"
constexpr float kMotionCount = 52; // and its "6 notes"
constexpr float kTagPad = 8;

bool drawMappingBanner(NVGcontext* vg, EditorContext& ctx, float x, float y, float height, float maxWidth);

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

/// A device's name as its card shows it: its own, or its type's when it has none.
std::string nameOf(const Device& d) { return d.name.empty() ? devices::deviceInfo(d.type).name : d.name; }

const devices::ParamInfo* infoOf(const ParamRef& ref, DeviceType type) {
    return ref.kind == ParamRef::Kind::Playhead ? devices::playheadParamInfo(ref.param)
                                                : devices::paramInfo(type, ref.param);
}

/// What a tag reads: its value's label, or for a step order its order and step size, or a channel set.
std::string tagLabel(DeviceStrip& strip, const DeviceStrip::TagRef& t, const tags::Values& v) {
    if (t.spec.family == Family::Channels)
        return tags::channelsText(v.channels);
    const tags::ValueList& list = strip.listOf(t);
    int i = strip.indexOf(t);
    if (i < 0 || i >= list.size())
        return "";
    if (t.spec.family == Family::Direction && i >= 4) {
        const std::vector<std::string>& steps = playheads::orderStepNames();
        return list.labels[size_t(i)] + " " +
               steps[size_t(std::clamp(int(std::lround(v[playheads::knob::kOrderStep])), 0, int(steps.size()) - 1))];
    }
    return list.labels[size_t(i)];
}

float tagWidth(NVGcontext* vg, const tags::TagSpec& spec, const std::string& label, const std::string& name) {
    const theme::Palette& c = theme::colors();
    if (spec.family == Family::Toggle) {
        draw::TextStyle st{theme::fonts::ui(), mx::kDeviceTagText, c.text1};
        return kTagPad + draw::textWidth(vg, st, name) + 4 + 22 + 4;
    }
    float w = 2 * kTagPad + tiles::drawValue(vg, 0, 0, label, mx::kDeviceTagText, c.text0, false);
    if (spec.prefix[0]) {
        draw::TextStyle pre{theme::fonts::ui(), mx::kDeviceTagText, c.text2};
        w += draw::textWidth(vg, pre, spec.prefix) + 4;
    }
    return w;
}

// ---- Gestures -------------------------------------------------------------------------------------------------

/// Dragging a tag left, right, up or down steps through its values; the picture follows and the engine plays it.
/// One undo step on release.
class TagDrag : public DragGesture {
  public:
    TagDrag(DeviceStrip& strip, EditorContext& ctx, DeviceStrip::TagRef t, Vec press)
        : strip(strip), ctx(ctx), t(t), list(strip.listOf(t)), press(press) {
        start = index = strip.indexOf(t);
        if (TesseraModule* m = ctx.module())
            if (const devices::ParamInfo* p = deviceActions::infoOf(m->song(), t.ref))
                info = *p;
    }
    void update(Vec pos, Modifiers mods) override {
        int n = std::max(1, list.size());
        float px = std::clamp(190.f / float(n), 2.5f, 14.f) * (mods.shift ? 3.f : 1.f);
        float d = (pos.x - press.x) - (pos.y - press.y);
        index = std::clamp(start + int(std::lround(d / px)), 0, n - 1);
        if (t.spec.family == Family::Direction) {
            strip.directionPreview = std::make_pair(t.ref.owner, index);
            return;
        }
        value = devices::normalize(info, list.values[size_t(index)]);
        strip.knobPreview = std::make_pair(t.ref, value);
        if (TesseraModule* m = ctx.module())
            m->holdKnob(t.ref, value);
    }
    void commit() override {
        if (t.spec.family == Family::Direction) {
            strip.directionPreview.reset();
            if (index != start)
                strip.setIndex(t, index);
            return;
        }
        TesseraModule* m = ctx.module();
        if (const AutomationEnvelope* lane = m ? m->recordingKnobLane(t.ref) : nullptr) {
            // Recording: the lane takes the move. In Latch and Write the setting stays where the hand left it until
            // the pass ends.
            strip.knobPreview.reset();
            if (lane->recordMode == AutoRecordMode::Touch)
                m->releaseKnob();
            return;
        }
        finish();
        if (index != start)
            deviceActions::setKnob(ctx, t.ref, value);
    }
    void cancel() override {
        strip.directionPreview.reset();
        finish();
    }
    std::string readout() const override {
        return (t.spec.family == Family::Direction ? std::string("Direction") : std::string(info.name)) + "  " +
               (index >= 0 && index < list.size() ? list.labels[size_t(index)] : "");
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    void finish() {
        strip.knobPreview.reset();
        if (TesseraModule* m = ctx.module())
            m->releaseKnob();
    }
    DeviceStrip& strip;
    EditorContext& ctx;
    DeviceStrip::TagRef t;
    tags::ValueList list;
    devices::ParamInfo info{""};
    Vec press;
    int start = 0, index = 0;
    float value = 0;
};

/// Mapping: dragging a lit tag sets how far the modulator moves it (up positive, down negative).
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
    CardDrag(DeviceStrip& strip, EditorContext& ctx, DeviceId id, std::vector<Rect> cards)
        : strip(strip), ctx(ctx), id(id), cards(std::move(cards)) {}
    void update(Vec pos, Modifiers mods) override {
        // Cards are in reading order, wrapped onto rows: count those before the pointer.
        int at = 0;
        for (const Rect& r : cards)
            if (pos.y > r.getBottom() + DeviceStrip::kRowGap / 2 || (pos.y >= r.pos.y - DeviceStrip::kRowGap / 2 &&
                                                                      pos.x > r.getCenter().x))
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
    std::vector<Rect> cards;
    bool copy = false;
};

/// Dragging the strip's bottom edge: more or fewer rows of cards, or near the pane's bottom the whole pane. The
/// strip's size is the editor's, not the song's: no undo step.
class StripResize : public DragGesture {
  public:
    StripResize(DeviceStrip& strip, EditorContext& ctx) : strip(strip), ctx(ctx) {}
    void update(Vec pos, Modifiers) override {
        int rows = std::clamp(int(std::lround((pos.y - 2 * DeviceStrip::kTop + DeviceStrip::kRowGap) /
                                              (DeviceStrip::kCardHeight + DeviceStrip::kRowGap))),
                              1, std::max(1, strip.rowsNeeded));
        // Never so deep that the notes lose their room: the strip only takes the whole pane when one row is too much.
        while (rows > 1 && strip.paneRoom - DeviceStrip::heightFor(rows) < DeviceStrip::kMinNotes)
            rows--;
        ctx.view().stripRows = rows;
    }
    std::string readout() const override {
        int rows = ctx.view().stripRows;
        return rows <= 1 ? "1 row" : std::to_string(rows) + " rows";
    }
    CursorShape cursor() const override { return CursorShape::ResizeV; }

  private:
    DeviceStrip& strip;
    EditorContext& ctx;
};

} // namespace

// ---- Cached pictures ------------------------------------------------------------------------------------------

/// The pictures of the cards on show, each drawn once into its own framebuffer and redrawn only when what it shows
/// changes (a value, the tint, the scheme). Clipped to the chain, so pictures scrolled under the modulator list hide.
class TileLayer : public rack::widget::Widget {
  public:
    float clipLeft = 0;

    /// Marks every picture unused; show() marks the ones drawn this frame.
    void beginFrame() {
        for (auto& [key, t] : tiles)
            t->used = false;
    }

    /// Shows a picture at `pos`. `hash` sums up everything it draws; the caption is only worked out when it changed.
    void show(uint64_t key, Vec pos, float width, DeviceType type, int tile, const tags::Values& values, NVGcolor tint,
              float alpha, uint64_t hash, const std::function<std::string()>& caption) {
        std::unique_ptr<Tile>& t = tiles[key];
        if (!t) {
            t = std::make_unique<Tile>();
            Tile* raw = t.get();
            t->layer = new CachedLayer([raw](NVGcontext* vg, Vec) {
                tiles::drawTile(vg, raw->type, raw->tile, raw->values, raw->width, raw->tint, raw->caption);
            });
            addChild(t->layer);
        }
        t->used = true;
        t->alpha = alpha;
        if (t->hash != hash || t->width != width) {
            t->hash = hash;
            t->type = type;
            t->tile = tile;
            t->values = values;
            t->tint = tint;
            t->width = width;
            t->caption = caption();
        }
        t->layer->box.pos = pos;
        t->layer->setSize(Vec(width, mx::kDeviceTile));
        t->layer->setKey(hash);
    }

    /// Frees the pictures not shown this frame.
    void endFrame() {
        for (auto it = tiles.begin(); it != tiles.end();) {
            if (it->second->used) {
                ++it;
                continue;
            }
            removeChild(it->second->layer);
            delete it->second->layer;
            it = tiles.erase(it);
        }
    }

    void draw(const DrawArgs& args) override {
        nvgSave(args.vg);
        nvgIntersectScissor(args.vg, clipLeft, 0, box.size.x - clipLeft, box.size.y);
        for (auto& [key, t] : tiles) {
            nvgSave(args.vg);
            nvgGlobalAlpha(args.vg, t->alpha);
            drawChild(t->layer, args);
            nvgRestore(args.vg);
        }
        nvgRestore(args.vg);
    }

  private:
    struct Tile {
        CachedLayer* layer = nullptr;
        DeviceType type = DeviceType::Transpose;
        int tile = 0;
        tags::Values values;
        NVGcolor tint;
        float width = 0, alpha = 1;
        std::string caption;
        uint64_t hash = ~uint64_t(0);
        bool used = false;
    };
    std::map<uint64_t, std::unique_ptr<Tile>> tiles;
};

DeviceStrip::DeviceStrip(EditorContext& ctx) : InteractiveView(ctx) {
    tileLayer = new TileLayer;
    addChild(tileLayer);
    self = std::make_shared<DeviceStrip*>(this);
}

// ---- Values ---------------------------------------------------------------------------------------------------

const Track* DeviceStrip::track() { return deviceActions::track(ctx); }

const tags::ValueList& DeviceStrip::listOf(const TagRef& t) {
    uint64_t key = (uint64_t(t.type) << 32) | (uint64_t(t.ref.kind) << 24) | (uint64_t(t.spec.family) << 16) |
                   uint64_t(t.ref.param);
    auto it = lists.find(key);
    if (it == lists.end())
        it = lists.emplace(key, tags::valueList(t.type, t.spec)).first;
    return it->second;
}

float DeviceStrip::shownValue(const ParamRef& ref) {
    if (knobPreview && knobPreview->first == ref)
        return knobPreview->second;
    TesseraModule* m = ctx.module();
    const float* stored = m ? findParam(m->song(), ref) : nullptr;
    if (!stored)
        return 0;
    if (modulationsOn(m->song(), ref) > 0 || m->automated(ref))
        return m->liveKnob(ref);
    return *stored;
}

int DeviceStrip::indexOf(const TagRef& t) {
    const tags::ValueList& list = listOf(t);
    if (t.spec.family == Family::Direction) {
        if (directionPreview && directionPreview->first == t.ref.owner)
            return directionPreview->second;
        const devices::ParamInfo* dir = devices::playheadParamInfo(playheads::knob::kDirection);
        const devices::ParamInfo* order = devices::playheadParamInfo(playheads::knob::kOrder);
        ParamRef o = t.ref;
        o.param = uint16_t(playheads::knob::kOrder);
        int ord = int(std::lround(devices::display(*order, shownValue(o))));
        return ord > 0 ? std::min(3 + ord, tags::kDirectionValues - 1)
                       : int(std::lround(devices::display(*dir, shownValue(t.ref))));
    }
    TesseraModule* m = ctx.module();
    const devices::ParamInfo* info = m ? deviceActions::infoOf(m->song(), t.ref) : nullptr;
    if (!info || list.values.empty())
        return 0;
    return list.nearest(devices::display(*info, shownValue(t.ref)));
}

void DeviceStrip::setIndex(const TagRef& t, int index) {
    TesseraModule* m = ctx.module();
    const tags::ValueList& list = listOf(t);
    if (!m || list.values.empty())
        return;
    index = std::clamp(index, 0, list.size() - 1);
    if (t.spec.family == Family::Direction) {
        using namespace playheads::knob;
        if (index < 4)
            deviceActions::setPlayheadValues(ctx, PlayheadId(t.ref.owner), {{kDirection, float(index)}, {kOrder, 0}},
                                             "Set direction");
        else
            deviceActions::setPlayheadValues(ctx, PlayheadId(t.ref.owner), {{kOrder, float(index - 3)}}, "Set order");
        return;
    }
    const devices::ParamInfo* info = deviceActions::infoOf(m->song(), t.ref);
    const float* stored = findParam(m->song(), t.ref);
    if (!info || !stored)
        return;
    float n = devices::normalize(*info, list.values[size_t(index)]);
    if (n != *stored)
        deviceActions::setKnob(ctx, t.ref, n);
}

uint16_t DeviceStrip::channelsOf(const Device& d) const {
    if (channelPreview && channelPreview->first == d.id.value && tagPanelOpen())
        return channelPreview->second;
    const devices::ParamInfo* info = devices::paramInfo(d.type, chain::knob::kChannel);
    if (!info || d.params.size() <= size_t(chain::knob::kChannel))
        return 0;
    return uint16_t(std::lround(devices::display(*info, d.params[size_t(chain::knob::kChannel)])));
}

void DeviceStrip::setChannels(DeviceId id, uint16_t mask) {
    TesseraModule* m = ctx.module();
    const Device* d = m ? findDevice(m->song(), id) : nullptr;
    const devices::ParamInfo* info = d ? devices::paramInfo(d->type, chain::knob::kChannel) : nullptr;
    if (!info)
        return;
    deviceActions::setKnob(ctx, {ParamRef::Kind::Device, id.value, uint16_t(chain::knob::kChannel)},
                           devices::normalize(*info, float(mask)));
}

tags::Values DeviceStrip::valuesOf(const Device& d, const Playhead* playhead) {
    tags::Values v;
    const std::vector<float>& params = playhead ? playhead->params : d.params;
    ParamRef::Kind kind = playhead ? ParamRef::Kind::Playhead : ParamRef::Kind::Device;
    uint64_t owner = playhead ? playhead->id.value : d.id.value;
    for (size_t k = 0; k < params.size() && k < v.v.size(); k++) {
        const devices::ParamInfo* info =
            playhead ? devices::playheadParamInfo(int(k)) : devices::paramInfo(d.type, int(k));
        if (info)
            v.v[k] = devices::display(*info, params[k]);
    }
    // Settings on tags read as their tags do: previewed, live, snapped to the tag's steps.
    for (const tags::TileSpec& tile : tags::tilesOf(d.type))
        for (const tags::TagSpec& spec : tile.tags) {
            if (spec.family == Family::Channels || spec.param < 0 || spec.param >= int(v.v.size()))
                continue;
            TagRef t{{kind, owner, uint16_t(spec.param)}, d.type, spec, nvgRGBf(0, 0, 0)};
            const tags::ValueList& list = listOf(t);
            int i = indexOf(t);
            if (spec.family == Family::Direction) {
                v.v[size_t(playheads::knob::kDirection)] = i < 4 ? float(i) : 0.f;
                v.v[size_t(playheads::knob::kOrder)] = i < 4 ? 0.f : float(i - 3);
            } else if (i >= 0 && i < list.size()) {
                v.v[size_t(spec.param)] = list.values[size_t(i)];
            }
        }
    if (d.type == DeviceType::NoteFilter)
        v.channels = channelsOf(d);
    if (d.type == DeviceType::Scale && ctx.module()) {
        // The key it works in when it follows the clip or the song.
        const Song& song = ctx.module()->song();
        ScaleRef key = song.scale;
        if (const Track* t = track())
            if (const Clip* clip = song.findClip(clipActions::shownClip(ctx, *t)))
                if (clip->scaleOverride && v.i(chain::knob::kKeyFrom) == int(chain::KeyFrom::Clip))
                    key = *clip->scaleOverride;
        Scale s = resolveScale(key, song.customScales);
        v.keyRoot = s.root();
        v.keyMask = s.mask();
    }
    return v;
}

std::optional<NVGcolor> DeviceStrip::modulatorColorOn(const ParamRef& ref) {
    TesseraModule* m = ctx.module();
    if (!m)
        return std::nullopt;
    std::optional<NVGcolor> color;
    forEachModulator(m->song(), [&](const Modulator& mod, const ModulatorPlace&) {
        if (color)
            return;
        for (const Modulation& r : mod.routes)
            if (r.kind == Modulation::Kind::Param && r.target == ref)
                color = deviceActions::colorOf(mod.id);
    });
    return color;
}

std::vector<DeviceStrip::Span> DeviceStrip::spansOn(const ParamRef& ref) {
    std::vector<Span> out;
    TesseraModule* m = ctx.module();
    const float* stored = m ? findParam(m->song(), ref) : nullptr;
    if (!stored)
        return out;
    const Song& song = m->song();
    for (const FoundLane& f : lanesTurning(song, ref)) {
        if (f.lane->points.empty())
            continue;
        float lo = 1, hi = 0;
        for (const AutoPoint& p : f.lane->points) {
            lo = std::min(lo, p.value);
            hi = std::max(hi, p.value);
        }
        out.push_back({lo, hi, deviceActions::laneColor(song, f), f.lane->name.empty() ? "Its lane" : f.lane->name});
    }
    // Modulations reach from where the setting is (as set, or as its lane has it).
    float base = m->automated(ref) ? m->knobBase(ref) : *stored;
    bool previewed = false;
    forEachModulator(song, [&](const Modulator& mod, const ModulatorPlace&) {
        for (const Modulation& r : mod.routes) {
            if (r.kind != Modulation::Kind::Param || !(r.target == ref))
                continue;
            float amount = r.amount;
            if (amountPreview && amountPreview->first == ref && mod.id.value == ctx.mappingModulator) {
                amount = amountPreview->second;
                previewed = true;
            }
            float a = base, b = base + amount;
            if (mod.type == ModulatorType::Curve && !mod.points.empty()) {
                // A curve's 0.5 is no change; its points reach both ways.
                float lo = 1, hi = 0;
                for (const AutoPoint& p : mod.points) {
                    lo = std::min(lo, p.value);
                    hi = std::max(hi, p.value);
                }
                a = base + amount * (2 * lo - 1);
                b = base + amount * (2 * hi - 1);
            } else if (mods::bipolar(mod.type, mod.params.data())) {
                a = base - amount;
            }
            out.push_back({std::clamp(std::min(a, b), 0.f, 1.f), std::clamp(std::max(a, b), 0.f, 1.f),
                           deviceActions::colorOf(mod.id), mod.name});
        }
    });
    if (amountPreview && amountPreview->first == ref && !previewed)
        out.push_back({std::clamp(std::min(base, base + amountPreview->second), 0.f, 1.f),
                       std::clamp(std::max(base, base + amountPreview->second), 0.f, 1.f),
                       deviceActions::colorOf(ModulatorId(ctx.mappingModulator)), ""});
    return out;
}

NVGcolor DeviceStrip::tintOf(const Device& d, int index) {
    if (d.type == DeviceType::Playheads)
        if (const Playhead* p = shownPlayhead(d))
            return theme::playheadColor(p->color);
    if (index >= kBranchBase)
        if (const Playhead* p = branchPlayhead())
            return theme::playheadColor(p->color);
    if (d.type == DeviceType::Motion)
        return theme::playheadColor(d.color);
    // The track's colour, so its devices read with its notes.
    if (const Track* t = track())
        return theme::track(t->color);
    return theme::colors().accent;
}

// ---- Layout ---------------------------------------------------------------------------------------------------

float DeviceStrip::wantedHeight() {
    const Track* t = ctx.module() ? track() : nullptr;
    if (!t)
        return 0;
    if (deviceActions::stripOpen(ctx))
    {
        int rows = std::clamp(ctx.view().stripRows, 1, std::max(1, rowsNeeded));
        while (rows > 1 && paneRoom - heightFor(rows) < kMinNotes)
            rows--;
        return heightFor(rows);
    }
    return t->devices.empty() && t->modulators.empty() ? 0 : kClosedHeight;
}

void DeviceStrip::layoutParts() {
    parts.clear();
    tagRefs.clear();
    pending.clear();
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

    // The bottom edge resizes the strip when the cards need more than one row; first, so it wins over what it
    // overlaps.
    if (resizable())
        parts.push_back({Kind::ResizeEdge, Rect(0, h - kEdge, box.size.x, kEdge)});

    chainLeft = block.getRight() + 4;
    float x = chainLeft - scrollX;
    float top = kTop;
    // Each card with what follows it (a playhead's frame, the link) is one unit: rows break between units.
    std::vector<std::pair<size_t, size_t>> units;
    size_t unitStart = parts.size();
    // "in", with the + that adds a device first.
    parts.push_back({Kind::Insert, Rect(x, top, mx::kDeviceIo, kCardHeight), 0});
    x += mx::kDeviceIo;
    for (size_t i = 0; i < t->devices.size(); i++) {
        const Device& d = t->devices[i];
        x = layoutCard(d, int(i), x, top, tintOf(d, int(i)));
        if (d.type == DeviceType::Playheads)
            if (const Playhead* p = shownPlayhead(d)) {
                // The shown playhead's own devices: a frame in its colour, or a slim dashed slot when it has none.
                x += mx::kDeviceLink;
                Rect frame(x, top - mx::kDeviceFrameOut, mx::kDeviceSlot, kCardHeight + 2 * mx::kDeviceFrameOut);
                if (p->branch.empty()) {
                    parts.push_back({Kind::Slot, frame, -1, -1, p->id.value});
                    x += mx::kDeviceSlot;
                } else {
                    size_t at = parts.size();
                    x += mx::kDeviceSpine;
                    for (size_t k = 0; k < p->branch.size(); k++) {
                        if (k > 0) {
                            parts.push_back({Kind::Insert, Rect(x, top, mx::kDeviceLink, kCardHeight),
                                             kBranchBase + int(k)});
                            x += mx::kDeviceLink;
                        }
                        x = layoutCard(p->branch[k], kBranchBase + int(k), x, top, theme::playheadColor(p->color));
                    }
                    x += mx::kDeviceLink;
                    parts.push_back({Kind::FramePlus, Rect(x, top + kHeaderMid - mx::kDevicePlus / 2, mx::kDevicePlus,
                                                           mx::kDevicePlus),
                                     -1, -1, p->id.value});
                    x += mx::kDevicePlus + 9;
                    frame.size.x = x - frame.pos.x;
                    // Drawn first, under its cards; it takes no clicks.
                    parts.insert(parts.begin() + long(at), {Kind::Frame, frame, -1, -1, p->id.value});
                }
            }
        parts.push_back({Kind::Insert, Rect(x, top, mx::kDeviceLink, kCardHeight), int(i) + 1});
        x += mx::kDeviceLink;
        units.push_back({unitStart, parts.size()});
        unitStart = parts.size();
    }
    parts.push_back({Kind::AddTile, Rect(x, top + mx::kDeviceCardTop, kAddWidth, mx::kDeviceCardHeader)});
    x += kAddWidth + mx::kDeviceIo + 8;
    units.push_back({unitStart, parts.size()});
    contentWidth = x + scrollX - chainLeft;
    contentHeight = kOpenHeight;
    flowRows(units);
    showTiles();
}

void DeviceStrip::flowRows(const std::vector<std::pair<size_t, size_t>>& units) {
    float h = box.size.y;
    int fit = int(std::floor((h - 2 * kTop + kRowGap) / (kCardHeight + kRowGap)));
    // A unit's width: from its first part's left to its last part's right ("out" follows the add button).
    auto extent = [&](const std::pair<size_t, size_t>& u) {
        float a = INFINITY, b = -INFINITY;
        for (size_t k = u.first; k < u.second; k++) {
            if (parts[k].kind == Kind::Frame || parts[k].kind == Kind::Slot || parts[k].kind == Kind::Card ||
                parts[k].kind == Kind::Insert || parts[k].kind == Kind::AddTile) {
                a = std::min(a, parts[k].rect.pos.x);
                b = std::max(b, parts[k].rect.getRight() + (parts[k].kind == Kind::AddTile ? mx::kDeviceIo : 0));
            }
        }
        return std::make_pair(a, b);
    };
    float right = box.size.x - 8;
    {
        // The rows the cards need at this width, wrapped or not.
        float cx = chainLeft;
        int need = 1;
        for (const auto& u : units) {
            auto [a, b] = extent(u);
            if (!(b > a))
                continue;
            if (cx > chainLeft && cx + (b - a) > right) {
                need++;
                cx = chainLeft;
            }
            cx += b - a;
        }
        rowsNeeded = need;
    }
    if (fit < 2) {
        scrollY = 0;
        return; // one row: it scrolls sideways
    }
    float left = chainLeft - scrollX;
    float cx = left, widest = 0;
    int row = 0;
    for (const auto& u : units) {
        auto [a, b] = extent(u);
        if (!(b > a))
            continue;
        float w = b - a;
        if (cx > left && cx + w > right) {
            row++;
            cx = left;
        }
        float dx = cx - a, dy = float(row) * (kCardHeight + kRowGap) - scrollY;
        for (size_t k = u.first; k < u.second; k++)
            parts[k].rect.pos = parts[k].rect.pos.plus(Vec(dx, dy));
        cx += w;
        widest = std::max(widest, cx - left);
    }
    contentWidth = widest + 8;
    contentHeight = heightFor(row + 1);
}

void DeviceStrip::showTiles() {
    for (const Part& p : parts) {
        if (p.kind != Kind::Tile)
            continue;
        for (const PendingTile& t : pending)
            if (t.device == p.device && t.tile == p.tile) {
                DeviceType type = t.type;
                int tile = t.tile;
                const tags::Values& values = t.values;
                tileLayer->show(t.key, p.rect.pos, t.width, t.type, t.tile, t.values, t.tint, t.alpha, t.hash,
                                [type, tile, &values] { return tags::caption(type, tile, values); });
            }
    }
    pending.clear();
}

float DeviceStrip::tileWidth(DeviceType type, const tags::TileSpec& spec, ParamRef::Kind kind, uint64_t owner) {
    NVGcontext* vg = APP->window->vg;
    if (type == DeviceType::NoteLength || !vg)
        return spec.width; // only one of its settings shows at a time, and it fits
    float sum = 0;
    for (const tags::TagSpec& s : spec.tags) {
        uint64_t key = (uint64_t(type) << 32) | (uint64_t(kind) << 24) | uint64_t(s.param);
        auto it = widestTag.find(key);
        if (it == widestTag.end()) {
            TagRef t{{kind, owner, uint16_t(s.param)}, type, s, nvgRGBf(0, 0, 0)};
            const devices::ParamInfo* info = infoOf(t.ref, type);
            std::string name = info ? info->name : "";
            float widest = 0;
            if (s.family == Family::Channels)
                widest = tagWidth(vg, s, "1–4, 10", name);
            else if (s.family == Family::Toggle)
                widest = tagWidth(vg, s, "", name);
            else
                for (const std::string& label : listOf(t).labels)
                    widest = std::max(widest, tagWidth(vg, s, label + (s.family == Family::Direction ? " 1/16" : ""), name));
            it = widestTag.emplace(key, widest).first;
        }
        sum += it->second;
    }
    sum += float(spec.tags.size() - 1) * mx::kDeviceTagGap + 2 * mx::kDeviceTagInset;
    return std::max(spec.width, std::ceil(sum));
}

float DeviceStrip::layoutTiles(const Device& d, int index, float x, float top, ParamRef::Kind kind, uint64_t owner,
                               const tags::Values& values, NVGcolor tint, float minWidth) {
    NVGcontext* vg = APP->window->vg;
    const std::vector<tags::TileSpec>& specs = tags::tilesOf(d.type);
    float tx = x, ty = top + mx::kDeviceCardTop + mx::kDeviceCardHeader;
    // Each setting's hash, so a picture only redraws when what it shows changed.
    uint64_t hash = 1469598103934665603ull;
    for (float f : values.v)
        hash = hashMix(hash, double(f));
    hash = hashMix(hash, double(values.channels) + 65536.0 * values.keyRoot + 1e6 * values.keyMask);
    hash = hashMix(hash, double(tint.r) + 3 * double(tint.g) + 9 * double(tint.b));
    for (size_t ti = 0; ti < specs.size(); ti++) {
        const tags::TileSpec& spec = specs[ti];
        float w = tileWidth(d.type, spec, kind, owner);
        if (ti + 1 == specs.size())
            w = std::max(w, minWidth - (tx - x)); // a wide header widens the last picture
        Rect tile(tx, ty, w, mx::kDeviceTile);
        // The tags on it, spread across its top; settings that don't apply now are left out or faint.
        struct Placed {
            TagRef t;
            float width;
            bool first, dim;
        };
        std::vector<Placed> placed;
        for (size_t k = 0; k < spec.tags.size(); k++) {
            const tags::TagSpec& s = spec.tags[k];
            if (tags::hidden(d.type, s.param, values))
                continue;
            TagRef t{{kind, owner, uint16_t(s.param)}, d.type, s, tint};
            const devices::ParamInfo* info = infoOf(t.ref, d.type);
            float width = vg ? tagWidth(vg, s, tagLabel(*this, t, values), info ? info->name : "") : 60;
            placed.push_back({t, width, k == 0, tags::dimmed(d.type, s.param, values)});
        }
        float total = 0;
        for (const Placed& p : placed)
            total += p.width;
        float room = w - 2 * mx::kDeviceTagInset;
        float gap = placed.size() > 1 ? std::max(mx::kDeviceTagGap, (room - total) / float(placed.size() - 1)) : 0;
        float px = tx + mx::kDeviceTagInset;
        for (const Placed& p : placed) {
            Part part{p.t.spec.family == Family::Toggle ? Kind::Switch : Kind::Tag,
                      Rect(px, ty + mx::kDeviceTagInset, p.width, mx::kDeviceTag),
                      index,
                      p.t.ref.param,
                      owner};
            part.tag = int(tagRefs.size());
            part.first = p.first;
            part.dim = p.dim;
            // An automated setting wears an "A" on its corner, which shows its lane; before the tag so it wins.
            if (p.t.spec.family != Family::Channels && ctx.module()->automated(p.t.ref)) {
                Part badge = part;
                badge.kind = Kind::TagBadge;
                badge.rect = Rect(part.rect.getRight() - 8, part.rect.pos.y - 6, 14, 14);
                parts.push_back(badge);
            }
            tagRefs.push_back(p.t);
            parts.push_back(part);
            px += p.width + gap;
        }
        Part tilePart{Kind::Tile, tile, index, -1, d.id.value};
        tilePart.tile = int(ti);
        parts.push_back(tilePart);
        pending.push_back({index, int(ti), owner * 8 + ti + (kind == ParamRef::Kind::Playhead ? 4 : 0),
                           hashMix(hash, double(ti) + w), d.type, values, tint, w, d.on ? 1.f : 0.4f});
        tx += w + mx::kDeviceTileGap;
    }
    return tx - mx::kDeviceTileGap;
}

float DeviceStrip::layoutCard(const Device& d, int index, float x, float top, NVGcolor tint) {
    NVGcontext* vg = APP->window->vg;
    const theme::Palette& c = theme::colors();
    float mid = top + kHeaderMid;
    draw::TextStyle nameStyle{theme::fonts::uiStrong(), 13, c.text0};
    float nameW = vg ? draw::textWidth(vg, nameStyle, nameOf(d)) : 80;
    float left = x + mx::kDeviceCardPad;
    parts.push_back({Kind::Power, Rect(left, mid - 9, 18, 18), index, -1, d.id.value});
    float hx = left + 18 + 8 + nameW + 8;
    const Playhead* shown = d.type == DeviceType::Playheads ? shownPlayhead(d) : nullptr;
    if (d.type == DeviceType::Playheads) {
        // The playheads, as chips after the card's name, then the shown one's On switch.
        draw::TextStyle chipStyle{theme::fonts::uiStrong(), 11.5f, c.text0};
        for (size_t k = 0; k < d.playheads.size(); k++) {
            const Playhead& p = d.playheads[k];
            std::string label = p.solo ? "S · " + p.name : p.name;
            float w = 7 + 7 + 6 + (vg ? draw::textWidth(vg, chipStyle, label) : 60) + 9;
            parts.push_back({Kind::PlayheadChip, Rect(hx, mid - kChipHeight / 2, w, kChipHeight), index, int(k),
                             p.id.value});
            hx += w + 6;
        }
        if (int(d.playheads.size()) < playheads::kMaxPlayheads) {
            parts.push_back({Kind::AddPlayhead, Rect(hx, mid - kChipHeight / 2, kChipHeight + 6, kChipHeight), index, -1,
                             d.id.value});
            hx += kChipHeight + 6 + 8;
        }
        if (shown) {
            parts.push_back({Kind::PlayheadOn, Rect(hx, mid - 8.5f, kSwitchW, 17), index, -1, shown->id.value});
            hx += kSwitchW + 8;
        }
    }
    float rightNeed = 18 + float(d.modulators.size()) * 11 + 8;
    if (d.type == DeviceType::Motion)
        rightNeed += kMotionJoin + kMotionLeave + kMotionCount + 16;
    if (index >= kBranchBase)
        rightNeed += 12;
    float headerWidth = hx - x + rightNeed + mx::kDeviceCardPad;

    ParamRef::Kind kind = shown ? ParamRef::Kind::Playhead : ParamRef::Kind::Device;
    uint64_t owner = shown ? shown->id.value : d.id.value;
    tags::Values values = valuesOf(d, shown);
    float tilesRight = left;
    if (d.type != DeviceType::Playheads || shown)
        tilesRight = layoutTiles(d, index, left, top, kind, owner, values, tint, headerWidth - 2 * mx::kDeviceCardPad);
    float width = std::max(headerWidth, tilesRight + mx::kDeviceCardPad - x);
    Rect card(x, top, width, kCardHeight);

    Rect menu(card.getRight() - mx::kDeviceCardPad - 18, mid - 9, 18, 18);
    parts.push_back({Kind::Menu, menu, index, -1, d.id.value});
    for (size_t k = 0; k < d.modulators.size(); k++)
        parts.push_back({Kind::Dot, Rect(menu.pos.x - 12 - float(k) * 11, mid - 5, 10, 10), index, int(k),
                         d.modulators[k].id.value});
    if (d.type == DeviceType::Motion) {
        // Which notes move: "+ Selected" adds the piano roll's selection, "− Selected" takes it out again, "6 notes"
        // selects them there.
        float right = menu.pos.x - 6 - float(d.modulators.size()) * 11;
        Rect leave(right - kMotionLeave, mid - 9, kMotionLeave, 18);
        Rect join(leave.pos.x - 4 - kMotionJoin, mid - 9, kMotionJoin, 18);
        Rect count(join.pos.x - 4 - kMotionCount, mid - 9, kMotionCount, 18);
        parts.push_back({Kind::MotionLeave, leave, index, -1, d.id.value});
        parts.push_back({Kind::MotionJoin, join, index, -1, d.id.value});
        parts.push_back({Kind::MotionNotes, count, index, -1, d.id.value});
    }
    parts.push_back({Kind::CardHeader, Rect(card.pos.x, top, card.size.x, mx::kDeviceCardTop + mx::kDeviceCardHeader),
                     index, -1, d.id.value});
    parts.push_back({Kind::Card, card, index, -1, d.id.value});
    return card.getRight();
}

const Device* DeviceStrip::cardDevice(int index) {
    const Track* t = track();
    if (!t)
        return nullptr;
    if (index < kBranchBase)
        return index >= 0 && index < int(t->devices.size()) ? &t->devices[size_t(index)] : nullptr;
    const Playhead* p = branchPlayhead();
    int k = index - kBranchBase;
    return p && k < int(p->branch.size()) ? &p->branch[size_t(k)] : nullptr;
}

int DeviceStrip::motionNoteCount(DeviceId id) {
    TesseraModule* m = ctx.module();
    if (!m)
        return 0;
    // Counted once per song revision: every clip of the song is read.
    if (motionCountRevision != m->songRevision()) {
        motionCounts.clear();
        for (const auto& [key, clip] : m->song().clips)
            for (const Note& n : clip.notes)
                if (n.motion.valid())
                    motionCounts[n.motion.value]++;
        motionCountRevision = m->songRevision();
    }
    auto it = motionCounts.find(id.value);
    return it == motionCounts.end() ? 0 : it->second;
}

const Playhead* DeviceStrip::branchPlayhead() {
    const Device* d = deviceActions::playheadsDevice(ctx);
    return d ? shownPlayhead(*d) : nullptr;
}

const Playhead* DeviceStrip::shownPlayhead(const Device& d) const {
    for (const Playhead& p : d.playheads)
        if (p.id.value == ctx.state().selectedPlayhead)
            return &p;
    return d.playheads.empty() ? nullptr : &d.playheads.front();
}

const DeviceStrip::Part* DeviceStrip::partAt(Vec pos) const {
    for (const Part& p : parts) {
        if (p.kind == Kind::Frame)
            continue; // only drawn: the parts on it take the clicks
        bool inChain = p.kind != Kind::AddModulator && p.kind != Kind::ModulatorRow && p.kind != Kind::ModulatorMap &&
                       p.kind != Kind::ClosedBar && p.kind != Kind::ResizeEdge;
        if (inChain && pos.x < chainLeft)
            continue; // the modulator list covers what scrolled under it
        if (p.kind == Kind::Insert) {
            // Only its round + takes clicks: the link around it is just a line.
            Vec c(p.rect.getCenter().x, p.rect.pos.y + kHeaderMid + (p.device == 0 ? 16 : 0));
            if (pos.minus(c).norm() <= 10)
                return &p;
            continue;
        }
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
    // The pointer left the strip: nothing is hovered any more (the hover note under a tag goes too).
    if (!hasGesture() && !Rect(Vec(), box.size).contains(localMouse()))
        hovered = -1;
    tileLayer->box = Rect(Vec(), box.size);
    tileLayer->beginFrame();
    if (ctx.module()) {
        const Track* t = track();
        if (t && t->id != shownTrack) {
            shownTrack = t->id;
            scrollX = 0;
            modScroll = 0;
            focusedTag.reset();
        }
        if (ctx.state().selectedDevice &&
            !(t && findDevice(ctx.module()->song(), DeviceId(ctx.state().selectedDevice)) &&
              std::any_of(t->devices.begin(), t->devices.end(),
                          [&](const Device& d) { return d.id.value == ctx.state().selectedDevice; })))
            ctx.state().selectedDevice = 0;
        if (ctx.mappingModulator && !findModulator(ctx.module()->song(), ModulatorId(ctx.mappingModulator)))
            ctx.mappingModulator = 0;
        float most = std::max(0.f, contentWidth - (box.size.x - chainLeft));
        scrollX = std::clamp(scrollX, 0.f, most);
        scrollY = std::clamp(scrollY, 0.f, std::max(0.f, contentHeight - box.size.y));
        // Never scrolled past the end: removing modulators (or a wheel turn while the list fits) can't empty it.
        modScroll = std::clamp(modScroll, 0.f, modScrollMax());
        layoutParts();
        sampleScopes();
        if (focusedTag && !findParam(ctx.module()->song(), focusedTag->ref))
            focusedTag.reset();
    }
    tileLayer->clipLeft = chainLeft;
    tileLayer->endFrame();
    InteractiveView::step();
}

bool DeviceStrip::handleAction(Action action) {
    TesseraModule* m = ctx.module();
    if (!m || !deviceActions::stripOpen(ctx))
        return false;
    // The arrow keys step the tag last touched.
    if (focusedTag && focusedTag->spec.family != Family::Channels && findParam(m->song(), focusedTag->ref)) {
        int delta = action == Action::TransposeUp || action == Action::NudgeRight  ? 1
                    : action == Action::TransposeDown || action == Action::NudgeLeft ? -1
                                                                                     : 0;
        if (delta != 0) {
            setIndex(*focusedTag, indexOf(*focusedTag) + delta);
            return true;
        }
    }
    if (!ctx.state().selectedDevice)
        return false;
    DeviceId id(ctx.state().selectedDevice);
    switch (action) {
    case Action::Delete:
        m->perform(removeDevice(m->song(), id));
        ctx.state().selectedDevice = 0;
        return true;
    case Action::Duplicate:
        m->perform(duplicateDevice(m->song(), id, m->ids()));
        return true;
    default:
        return false;
    }
}

void DeviceStrip::openPanel(const TagRef& t) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    focusedTag = t;
    panelTag = t;
    std::weak_ptr<DeviceStrip*> weak = self;
    EditorContext* c = &ctx;
    TagPanelSpec spec;
    spec.tint = t.tint;
    // Under the picture the tag sits on, so the picture shows what each choice would do.
    for (size_t i = 0; i < parts.size(); i++)
        if ((parts[i].kind == Kind::Tag || parts[i].kind == Kind::Switch) && parts[i].tag >= 0 &&
            tagRefs[size_t(parts[i].tag)] == t) {
            for (const Part& q : parts)
                if (q.kind == Kind::Tile && q.rect.contains(parts[i].rect.getCenter())) {
                    spec.below = getAbsoluteOffset(Vec(0, q.rect.getBottom())).y;
                    spec.centerX = getAbsoluteOffset(q.rect.getCenter()).x;
                }
            break;
        }
    if (t.spec.family == Family::Channels) {
        const Device* d = findDevice(m->song(), DeviceId(t.ref.owner));
        if (!d)
            return;
        DeviceId id = d->id;
        spec.title = "Channels";
        spec.channels = true;
        spec.mask = channelsOf(*d);
        spec.previewMask = [weak, id](uint16_t mask) {
            if (auto s = weak.lock())
                (*s)->channelPreview = std::make_pair(id.value, mask);
        };
        spec.pickMask = [weak, id](uint16_t mask) {
            if (auto s = weak.lock()) {
                (*s)->channelPreview.reset();
                (*s)->setChannels(id, mask);
            }
        };
        openTagPanel(std::move(spec));
        return;
    }
    const devices::ParamInfo* info = deviceActions::infoOf(m->song(), t.ref);
    spec.title = t.spec.family == Family::Direction ? "Direction" : info ? info->name : "";
    spec.list = listOf(t);
    spec.current = indexOf(t);
    spec.preview = [weak, t](int i) {
        auto s = weak.lock();
        if (!s)
            return;
        DeviceStrip* strip = *s;
        if (t.spec.family == Family::Direction) {
            if (i < 0)
                strip->directionPreview.reset();
            else
                strip->directionPreview = std::make_pair(t.ref.owner, i);
            return;
        }
        TesseraModule* mm = strip->ctx.module();
        const devices::ParamInfo* p = mm ? deviceActions::infoOf(mm->song(), t.ref) : nullptr;
        if (i < 0 || !p)
            strip->knobPreview.reset();
        else
            strip->knobPreview = std::make_pair(t.ref, devices::normalize(*p, strip->listOf(t).values[size_t(i)]));
    };
    spec.pick = [weak, t](int i) {
        if (auto s = weak.lock()) {
            (*s)->knobPreview.reset();
            (*s)->directionPreview.reset();
            (*s)->setIndex(t, i);
        }
    };
    if (t.spec.family == Family::Direction) {
        spec.steps = playheads::orderStepNames();
        ParamRef step = t.ref;
        step.param = uint16_t(playheads::knob::kOrderStep);
        if (const float* v = findParam(m->song(), step))
            spec.step = int(std::lround(devices::display(*devices::playheadParamInfo(playheads::knob::kOrderStep), *v)));
        PlayheadId id(t.ref.owner);
        spec.pickStep = [c, id](int s) {
            deviceActions::setPlayheadValues(*c, id, {{playheads::knob::kOrderStep, float(s)}}, "Set step size");
        };
    }
    ParamRef ref = t.ref;
    spec.links.push_back({"Draw a curve", [c, ref] { deviceActions::showCurveFor(*c, ref); }});
    spec.links.push_back({"Modulate, automate…", [c, ref] { deviceActions::openKnobMenu(*c, ref); }});
    openTagPanel(std::move(spec));
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
    case Kind::Tag: {
        TagRef t = tagRefs[size_t(p.tag)];
        focusedTag = t;
        if (ctx.mappingModulator) {
            if (t.spec.family == Family::Channels || !reachableNow(t.ref))
                return nullptr;
            ModulatorId mod(ctx.mappingModulator);
            ParamRef ref = t.ref;
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
        if (t.spec.family == Family::Channels)
            return nullptr; // a click opens its panel
        return std::make_unique<ClickOrDrag>(std::make_unique<TagDrag>(*this, ctx, t, pos), pos,
                                             [this, t] { openPanel(t); });
    }
    case Kind::ResizeEdge:
        return std::make_unique<StripResize>(*this, ctx);
    case Kind::CardHeader: {
        if (p.device >= kBranchBase)
            return nullptr; // branch devices stay in their branch: a click selects them
        std::vector<Rect> cards;
        for (const Part& q : parts)
            if (q.kind == Kind::Card && q.device < kBranchBase)
                cards.push_back(q.rect);
        DeviceId device(p.id);
        return std::make_unique<ClickOrDrag>(std::make_unique<CardDrag>(*this, ctx, device, cards), pos,
                                             [c, device] { c->state().selectedDevice = device.value; });
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
    if (!hit || (hit->kind != Kind::Tag && hit->kind != Kind::Switch))
        focusedTag.reset();
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (!hit)
            return;
        switch (hit->kind) {
        case Kind::Tag:
        case Kind::Switch:
            openPanel(tagRefs[size_t(hit->tag)]);
            break;
        case Kind::Card:
        case Kind::CardHeader:
        case Kind::Power:
        case Kind::Menu:
        case Kind::Tile:
            deviceActions::openDeviceMenu(ctx, DeviceId(hit->id));
            break;
        case Kind::ModulatorRow:
        case Kind::ModulatorMap:
        case Kind::Dot:
            deviceActions::openModulatorMenu(ctx, ModulatorId(hit->id));
            break;
        case Kind::PlayheadChip:
            ctx.state().selectedPlayhead = hit->id;
            deviceActions::openPlayheadMenu(ctx, PlayheadId(hit->id));
            break;
        case Kind::PlayheadOn:
            deviceActions::openPlayheadMenu(ctx, PlayheadId(hit->id));
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
        ctx.state().selectedDevice = 0;
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
    case Kind::Dot: {
        ctx.mappingModulator = 0;
        // A Curve is edited in the lower pane, under the notes it moves, with what it moves in the pane's header: it
        // opens no popover (Trello: the dialog and the pane opened together). A second click puts the notes back.
        const Modulator* mod = findModulator(m->song(), ModulatorId(hit->id));
        if (mod && mod->type == ModulatorType::Curve) {
            ctx.state().openModulator = 0;
            if (deviceActions::curveShown(ctx, mod->id))
                ctx.state().lowerLane = 0;
            else
                deviceActions::showCurve(ctx, mod->id);
            break;
        }
        ctx.state().openModulator = ctx.state().openModulator == hit->id ? 0 : hit->id;
        break;
    }
    case Kind::ModulatorMap:
        ctx.mappingModulator = ctx.mappingModulator == hit->id ? 0 : hit->id;
        ctx.state().openModulator = 0;
        break;
    case Kind::Power:
        if (const Device* d = findDevice(m->song(), DeviceId(hit->id)))
            m->perform(setDeviceOn(m->song(), d->id, !d->on));
        break;
    case Kind::Menu:
        deviceActions::openDeviceMenu(ctx, DeviceId(hit->id));
        break;
    case Kind::Tag:
        // Only channel sets get here (other tags open their panel through their gesture).
        if (!ctx.mappingModulator)
            openPanel(tagRefs[size_t(hit->tag)]);
        break;
    case Kind::TagBadge: {
        // The automated setting's lane, in the arranger or the clip.
        std::vector<FoundLane> lanes = lanesTurning(m->song(), tagRefs[size_t(hit->tag)].ref);
        if (!lanes.empty())
            deviceActions::showLane(ctx, lanes.front().owner, lanes.front().lane->id);
        break;
    }
    case Kind::Switch: {
        TagRef tag = tagRefs[size_t(hit->tag)];
        focusedTag = tag;
        if (ctx.mappingModulator)
            break;
        if (const float* v = findParam(m->song(), tag.ref))
            deviceActions::setKnob(ctx, tag.ref, *v >= 0.5f ? 0.f : 1.f);
        break;
    }
    case Kind::Insert:
        if (hit->device >= kBranchBase) {
            if (const Playhead* p = branchPlayhead())
                deviceActions::openBranchBrowser(ctx, p->id, hit->device - kBranchBase);
        } else {
            deviceActions::openDeviceBrowser(ctx, t->id, hit->device);
        }
        break;
    case Kind::FramePlus:
    case Kind::Slot:
        deviceActions::openBranchBrowser(ctx, PlayheadId(hit->id), -1);
        break;
    case Kind::CardHeader:
    case Kind::Tile:
    case Kind::Card:
        ctx.state().selectedDevice = hit->id;
        ctx.mappingModulator = 0;
        break;
    case Kind::AddTile:
        deviceActions::openDeviceBrowser(ctx, t->id, -1);
        break;
    case Kind::PlayheadChip:
        ctx.state().selectedPlayhead = hit->id;
        // The dot at the chip's left end turns it off or on; with Alt it solos it.
        if (pos.x < hit->rect.pos.x + kChipDot)
            deviceActions::togglePlayheadOn(ctx, mods.alt);
        break;
    case Kind::PlayheadOn:
        deviceActions::togglePlayheadOn(ctx, mods.alt);
        break;
    case Kind::MotionNotes: {
        // Its notes in the shown clip, selected.
        const Clip* clip = m->song().findClip(ctx.state().selectedClip);
        if (!clip)
            break;
        NoteSelection sel;
        sel.clip = clip->id;
        for (const Note& n : clip->notes)
            if (n.motion.value == hit->id)
                sel.notes.insert(n.id);
        ctx.state().selection = sel;
        break;
    }
    case Kind::MotionJoin: {
        const NoteSelection& sel = ctx.state().selection;
        if (sel.empty() || !sel.clip.valid()) {
            ctx.toast("Select notes in the piano roll, then click + Selected");
            break;
        }
        std::vector<NoteId> notes(sel.notes.begin(), sel.notes.end());
        m->perform(setNotesMotion(m->song(), sel.clip, notes, DeviceId(hit->id)));
        break;
    }
    case Kind::MotionLeave: {
        // Only the selected notes that move with this device leave it; the rest of the selection is untouched.
        const NoteSelection& sel = ctx.state().selection;
        const Clip* clip = sel.clip.valid() ? m->song().findClip(sel.clip) : nullptr;
        std::vector<NoteId> notes;
        if (clip)
            for (const Note& n : clip->notes)
                if (n.motion.value == hit->id && sel.notes.count(n.id))
                    notes.push_back(n.id);
        if (notes.empty()) {
            ctx.toast("Select notes of this Motion in the piano roll, then click − Selected");
            break;
        }
        m->perform(setNotesMotion(m->song(), sel.clip, notes, DeviceId()));
        break;
    }
    case Kind::AddPlayhead: {
        PlayheadId added;
        if (CommandPtr add = addPlayhead(m->song(), DeviceId(hit->id), m->ids(), &added)) {
            m->perform(std::move(add));
            ctx.state().selectedPlayhead = added.value;
        }
        break;
    }
    default:
        break;
    }
}

void DeviceStrip::doubleClick(Vec pos, Modifiers mods) {
    const Part* hit = partAt(pos);
    if (!hit || hit->kind != Kind::Tag || ctx.mappingModulator)
        return;
    TagRef t = tagRefs[size_t(hit->tag)];
    if (t.spec.family == Family::Channels)
        setChannels(DeviceId(t.ref.owner), 0);
    else
        setIndex(t, listOf(t).def);
}

void DeviceStrip::wheel(Vec pos, Vec delta, Modifiers mods) {
    if (pos.x < chainLeft) {
        modScroll = std::clamp(modScroll - delta.y * 0.5f, 0.f, modScrollMax());
        return;
    }
    // A vertical turn over a tag steps it once; anything else scrolls the chain.
    const Part* hit = partAt(pos);
    if (hit && hit->kind == Kind::Tag && !ctx.mappingModulator && std::fabs(delta.y) > std::fabs(delta.x)) {
        TagRef t = tagRefs[size_t(hit->tag)];
        if (t.spec.family != Family::Channels) {
            focusedTag = t;
            setIndex(t, indexOf(t) + (delta.y > 0 ? 1 : -1));
            return;
        }
    }
    // Rows taller than the strip scroll up and down; a row wider than it scrolls sideways.
    if (contentHeight > box.size.y + 1 && delta.x == 0 && !mods.shift) {
        scrollY = std::clamp(scrollY - delta.y, 0.f, contentHeight - box.size.y);
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
    case Kind::Tag: {
        const TagRef& t = tagRefs[size_t(hit->tag)];
        if (ctx.mappingModulator)
            return reachableNow(t.ref) ? CursorShape::ResizeV : CursorShape::NotAllowed;
        return t.spec.family == Family::Channels ? CursorShape::Hand : CursorShape::ResizeH;
    }
    case Kind::CardHeader:
        return CursorShape::Move;
    case Kind::ResizeEdge:
        return CursorShape::ResizeV;
    case Kind::Card:
    case Kind::Tile:
        return CursorShape::Arrow;
    default:
        return CursorShape::Hand;
    }
}

// ---- Drawing --------------------------------------------------------------------------------------------------

void DeviceStrip::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    const theme::DeviceColors& dc = theme::deviceColors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, dc.ground);
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
    draw::TextStyle io{theme::fonts::ui(), 10.5f, c.text2, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
    const Playhead* branch = branchPlayhead();
    NVGcolor branchColor = branch ? theme::playheadColor(branch->color) : c.accent;
    // Frames and slots first: the cards sit on them.
    for (size_t i = 0; i < parts.size(); i++) {
        const Part& p = parts[i];
        bool hot = int(i) == hovered;
        if (p.kind == Kind::Frame || p.kind == Kind::Slot) {
            // The link from the Playheads card, in the playhead's colour.
            nvgBeginPath(vg);
            nvgRect(vg, p.rect.pos.x - mx::kDeviceLink, p.rect.pos.y + mx::kDeviceFrameOut + kHeaderMid - 0.5f,
                    mx::kDeviceLink, 1);
            nvgFillColor(vg, nvgTransRGBAf(branchColor, 0.7f));
            nvgFill(vg);
        }
        if (p.kind == Kind::Frame) {
            draw::roundedRect(vg, p.rect, 17, nvgLerpRGBA(dc.ground, branchColor, 0.07f));
            draw::roundedRectStroke(vg, p.rect, 17, nvgTransRGBAf(branchColor, 0.42f), 1);
            draw::TextStyle spine{theme::fonts::uiStrong(), 11, branchColor, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            nvgSave(vg);
            nvgTranslate(vg, p.rect.pos.x + mx::kDeviceSpine / 2, p.rect.getCenter().y);
            nvgRotate(vg, -3.14159265f / 2);
            draw::text(vg, spine, 0, 0, draw::ellipsize(vg, spine, branch ? branch->name + " only" : "", p.rect.size.y - 16));
            nvgRestore(vg);
        } else if (p.kind == Kind::Slot) {
            NVGcolor fill = nvgLerpRGBA(dc.ground, branchColor, hot ? 0.1f : 0.05f);
            draw::roundedRect(vg, p.rect, 17, fill);
            if (hot) {
                draw::roundedRectStroke(vg, p.rect, 17, nvgTransRGBAf(branchColor, 0.55f), 1);
            } else {
                // A dashed outline: dashes along its straight sides, its round ends drawn whole but faint.
                nvgBeginPath(vg);
                Rect r = p.rect;
                int dashes = int(std::max(0.f, r.size.y - 34) / 6);
                for (int k = 0; k < dashes; k++) {
                    float y = r.pos.y + 17 + float(k) * 6;
                    nvgMoveTo(vg, r.pos.x + 0.5f, y);
                    nvgLineTo(vg, r.pos.x + 0.5f, std::min(y + 3, r.getBottom() - 17));
                    nvgMoveTo(vg, r.getRight() - 0.5f, y);
                    nvgLineTo(vg, r.getRight() - 0.5f, std::min(y + 3, r.getBottom() - 17));
                }
                nvgStrokeColor(vg, nvgTransRGBAf(branchColor, 0.55f));
                nvgStrokeWidth(vg, 1);
                nvgStroke(vg);
                nvgSave(vg);
                nvgIntersectScissor(vg, r.pos.x - 1, r.pos.y - 1, r.size.x + 2, 18);
                draw::roundedRectStroke(vg, r, 17, nvgTransRGBAf(branchColor, 0.4f), 1);
                nvgRestore(vg);
                nvgSave(vg);
                nvgIntersectScissor(vg, r.pos.x - 1, r.getBottom() - 17, r.size.x + 2, 18);
                draw::roundedRectStroke(vg, r, 17, nvgTransRGBAf(branchColor, 0.4f), 1);
                nvgRestore(vg);
            }
            Vec mid(p.rect.getCenter().x, p.rect.pos.y + mx::kDeviceFrameOut + kHeaderMid + 4);
            Rect plus(mid.x - mx::kDevicePlus / 2, mid.y - mx::kDevicePlus / 2, mx::kDevicePlus, mx::kDevicePlus);
            draw::roundedRect(vg, plus, mx::kDevicePlus / 2, nvgTransRGBAf(branchColor, hot ? 0.3f : 0.16f));
            draw::roundedRectStroke(vg, plus, mx::kDevicePlus / 2, nvgTransRGBAf(branchColor, 0.6f), 1);
            draw::TextStyle plusText{theme::fonts::ui(), 15, branchColor, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            draw::text(vg, plusText, mid.x, mid.y, "+");
            draw::TextStyle side{theme::fonts::uiStrong(), 11, branchColor, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            nvgSave(vg);
            nvgTranslate(vg, mid.x, (plus.getBottom() + p.rect.getBottom()) / 2);
            nvgRotate(vg, -3.14159265f / 2);
            draw::text(vg, side, 0, 0, branch ? branch->name + " only" : "");
            nvgRestore(vg);
        }
    }
    for (size_t i = 0; i < parts.size(); i++) {
        const Part& p = parts[i];
        bool hot = int(i) == hovered;
        switch (p.kind) {
        case Kind::Insert: {
            float cy = p.rect.pos.y + kHeaderMid;
            float cx = p.rect.getCenter().x;
            NVGcolor ring = dc.link;
            bool inBranch = p.device >= kBranchBase;
            if (inBranch)
                ring = branchColor;
            if (p.device == 0) {
                draw::text(vg, io, cx, cy, "in");
                cy += 16;
            } else {
                // The link between two cards, in the playhead's colour inside its frame.
                nvgBeginPath(vg);
                nvgRect(vg, p.rect.pos.x, cy - 0.5f, p.rect.size.x, 1);
                nvgFillColor(vg, inBranch ? nvgTransRGBAf(branchColor, 0.7f) : dc.link);
                nvgFill(vg);
            }
            // Its + is faint until hovered, so the row stays calm.
            nvgBeginPath(vg);
            nvgCircle(vg, cx, cy, 7);
            nvgFillColor(vg, hot ? c.bg2 : dc.ground);
            nvgFill(vg);
            nvgStrokeColor(vg, nvgTransRGBAf(ring, hot ? 1.f : 0.6f));
            nvgStrokeWidth(vg, 1);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgRect(vg, cx - 3, cy - 0.5f, 6, 1);
            nvgRect(vg, cx - 0.5f, cy - 3, 1, 6);
            nvgFillColor(vg, nvgTransRGBAf(c.text0, hot ? 1.f : 0.4f));
            nvgFill(vg);
            break;
        }
        case Kind::FramePlus: {
            Vec mid = p.rect.getCenter();
            // The link from the last card to it.
            nvgBeginPath(vg);
            nvgRect(vg, p.rect.pos.x - mx::kDeviceLink, mid.y - 0.5f, mx::kDeviceLink, 1);
            nvgFillColor(vg, nvgTransRGBAf(branchColor, 0.7f));
            nvgFill(vg);
            draw::roundedRect(vg, p.rect, mx::kDevicePlus / 2, nvgTransRGBAf(branchColor, hot ? 0.3f : 0.16f));
            draw::roundedRectStroke(vg, p.rect, mx::kDevicePlus / 2, nvgTransRGBAf(branchColor, 0.6f), 1);
            draw::TextStyle plus{theme::fonts::ui(), 15, branchColor, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            draw::text(vg, plus, mid.x, mid.y, "+");
            break;
        }
        case Kind::AddTile: {
            Rect r = p.rect;
            // A dashed pill.
            draw::roundedRect(vg, r, 10, hot ? c.bg2 : nvgRGBAf(0, 0, 0, 0));
            nvgBeginPath(vg);
            int dashes = int(std::max(0.f, r.size.x - 20) / 6);
            for (int k = 0; k < dashes; k++) {
                float x = r.pos.x + 10 + float(k) * 6;
                nvgMoveTo(vg, x, r.pos.y + 0.5f);
                nvgLineTo(vg, std::min(x + 3, r.getRight() - 10), r.pos.y + 0.5f);
                nvgMoveTo(vg, x, r.getBottom() - 0.5f);
                nvgLineTo(vg, std::min(x + 3, r.getRight() - 10), r.getBottom() - 0.5f);
            }
            nvgStrokeColor(vg, dc.tagEdge);
            nvgStrokeWidth(vg, 1);
            nvgStroke(vg);
            nvgSave(vg);
            nvgIntersectScissor(vg, r.pos.x - 1, r.pos.y - 1, 11, r.size.y + 2);
            draw::roundedRectStroke(vg, r, 10, dc.tagEdge, 1);
            nvgRestore(vg);
            nvgSave(vg);
            nvgIntersectScissor(vg, r.getRight() - 10, r.pos.y - 1, 11, r.size.y + 2);
            draw::roundedRectStroke(vg, r, 10, dc.tagEdge, 1);
            nvgRestore(vg);
            draw::TextStyle add{theme::fonts::ui(), 12, hot ? c.text0 : c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            draw::text(vg, add, r.getCenter().x, r.getCenter().y, "+ Add device");
            draw::text(vg, io, r.getRight() + mx::kDeviceIo / 2, r.getCenter().y, "out");
            if (t->devices.empty()) {
                draw::TextStyle hint{theme::fonts::ui(), theme::metrics::kTextUi, c.text2,
                                     NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
                draw::text(vg, hint, r.getRight() + mx::kDeviceIo + 10, r.getCenter().y,
                           "Devices change the notes on their way out: arpeggiator, chord, scale, echo…");
            }
            break;
        }
        case Kind::Card:
            drawCard(vg, p);
            break;
        default:
            break;
        }
    }
    nvgRestore(vg);
}

void DeviceStrip::drawCard(NVGcontext* vg, const Part& cardPart) {
    const Device* dp = cardDevice(cardPart.device);
    if (!dp)
        return;
    const Device& d = *dp;
    int index = cardPart.device;
    const theme::Palette& c = theme::colors();
    const theme::DeviceColors& dc = theme::deviceColors();
    Rect r = cardPart.rect;
    float radius = mx::kDeviceCardRadius;
    bool selected = ctx.state().selectedDevice == d.id.value;
    NVGcolor tint = tintOf(d, index);
    // A soft shadow that falls below it, barely past its sides, then the card.
    nvgBeginPath(vg);
    nvgRect(vg, r.pos.x - 20, r.pos.y, r.size.x + 40, r.size.y + 34);
    nvgFillPaint(vg, nvgBoxGradient(vg, r.pos.x + 6, r.pos.y + 10, r.size.x - 12, r.size.y, radius, 24,
                                    nvgRGBAf(0, 0, 0, c.dark ? 0.12f : 0.05f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    draw::roundedRect(vg, r, radius, dc.card);
    NVGcolor border = selected ? c.select : dc.cardEdge;
    // A device modulator being mapped outlines its own device: the only one it may reach.
    if (ctx.mappingModulator) {
        ModulatorPlace place;
        if (findModulator(ctx.module()->song(), ModulatorId(ctx.mappingModulator), &place) &&
            place.level == ModLevel::Device && place.device == d.id)
            border = deviceActions::colorOf(ModulatorId(ctx.mappingModulator));
    }
    draw::roundedRectStroke(vg, r, radius, border, selected ? 1.5f : 1);

    float mid = r.pos.y + kHeaderMid;
    for (size_t i = 0; i < parts.size(); i++) {
        const Part& p = parts[i];
        if (p.device != index)
            continue;
        bool hot = int(i) == hovered;
        switch (p.kind) {
        case Kind::Power: {
            // A dot in the card's colour with a soft ring; hollow when bypassed.
            Vec m = p.rect.getCenter();
            if (hot) {
                nvgBeginPath(vg);
                nvgCircle(vg, m.x, m.y, 9);
                nvgFillColor(vg, c.bg2);
                nvgFill(vg);
            }
            nvgBeginPath(vg);
            nvgCircle(vg, m.x, m.y, 4);
            if (d.on) {
                nvgFillColor(vg, tint);
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgCircle(vg, m.x, m.y, 5.5f);
                nvgStrokeColor(vg, nvgTransRGBAf(tint, 0.22f));
                nvgStrokeWidth(vg, 3);
                nvgStroke(vg);
            } else {
                nvgStrokeColor(vg, c.text2);
                nvgStrokeWidth(vg, 1.5f);
                nvgStroke(vg);
            }
            // The device's name after it.
            draw::TextStyle name{theme::fonts::uiStrong(), 13, d.on ? c.text0 : c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
            float nx = p.rect.getRight() + 8;
            if (d.type == DeviceType::Motion) {
                // Its colour, the one its notes' corners wear.
                draw::roundedRect(vg, Rect(nx, mid - 4, 8, 8), 2, theme::playheadColor(d.color));
                nx += 13;
            }
            float room = r.getRight() - mx::kDeviceCardPad - 26 - nx;
            if (d.type == DeviceType::Motion)
                room -= kMotionJoin + kMotionLeave + kMotionCount + 16;
            draw::text(vg, name, nx, mid, draw::ellipsize(vg, name, nameOf(d), std::max(30.f, room)));
            break;
        }
        case Kind::Menu: {
            if (hot)
                draw::roundedRect(vg, p.rect, 4, c.bg2);
            Vec m = p.rect.getCenter();
            nvgBeginPath(vg);
            for (int k = -1; k <= 1; k++)
                nvgCircle(vg, m.x + float(k) * 4.5f, m.y, 1.4f);
            nvgFillColor(vg, c.text2);
            nvgFill(vg);
            // A device in a playhead's own chain shows that playhead's dot left of its menu.
            if (index >= kBranchBase) {
                float left = p.rect.pos.x;
                for (const Part& q : parts)
                    if (q.device == index && q.kind == Kind::Dot)
                        left = std::min(left, q.rect.pos.x);
                nvgBeginPath(vg);
                nvgCircle(vg, left - 7, mid, 3.5f);
                nvgFillColor(vg, tint);
                nvgFill(vg);
            }
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
        case Kind::PlayheadChip:
            drawPlayheadChip(vg, d, p, hot);
            break;
        case Kind::AddPlayhead: {
            draw::roundedRect(vg, p.rect, kChipHeight / 2, hot ? c.bg2 : nvgRGBAf(0, 0, 0, 0));
            draw::roundedRectStroke(vg, p.rect, kChipHeight / 2, hot ? c.line2 : c.line1, 1);
            draw::TextStyle plus{theme::fonts::ui(), 14, hot ? c.text0 : c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            draw::text(vg, plus, p.rect.getCenter().x, p.rect.getCenter().y, "+");
            break;
        }
        case Kind::PlayheadOn: {
            const Playhead* ph = findPlayhead(ctx.module()->song(), PlayheadId(p.id));
            if (!ph)
                break;
            bool on = deviceActions::playheadPlaysNow(ctx, *ph);
            draw::roundedRect(vg, p.rect, p.rect.size.y / 2, on ? tint : c.line2);
            nvgBeginPath(vg);
            nvgCircle(vg, on ? p.rect.getRight() - 8.5f : p.rect.pos.x + 8.5f, p.rect.getCenter().y, 6.5f);
            nvgFillColor(vg, nvgRGBf(0.95f, 0.95f, 0.96f));
            nvgFill(vg);
            break;
        }
        case Kind::MotionNotes:
        case Kind::MotionJoin:
        case Kind::MotionLeave: {
            NVGcolor color = theme::playheadColor(d.color);
            bool count = p.kind == Kind::MotionNotes;
            int notes = count ? motionNoteCount(d.id) : 0;
            draw::roundedRect(vg, p.rect, 9,
                              count && notes > 0 ? nvgTransRGBAf(color, hot ? 0.3f : 0.18f) : (hot ? c.bg2 : dc.card));
            draw::roundedRectStroke(vg, p.rect, 9, count && notes > 0 ? color : c.line1, 1);
            draw::TextStyle text{theme::fonts::ui(), 10.5f, count ? (notes > 0 ? c.text0 : c.text2) : c.accent,
                                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            draw::text(vg, text, p.rect.getCenter().x, p.rect.getCenter().y,
                       count                        ? (notes == 1 ? "1 note" : std::to_string(notes) + " notes")
                       : p.kind == Kind::MotionJoin ? "+ Selected"
                                                    : "− Selected");
            break;
        }
        default:
            break;
        }
    }
    if (d.type == DeviceType::Playheads && d.playheads.empty()) {
        draw::TextStyle hint{theme::fonts::ui(), 11, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, r.pos.x + mx::kDeviceCardPad, r.pos.y + 90, "Add a playhead with + to read the clip");
    }
}

void DeviceStrip::drawPlayheadChip(NVGcontext* vg, const Device& d, const Part& part, bool hot) {
    const theme::Palette& c = theme::colors();
    if (part.param < 0 || part.param >= int(d.playheads.size()))
        return;
    const Playhead& p = d.playheads[size_t(part.param)];
    const Playhead* shown = shownPlayhead(d);
    bool selected = shown && shown->id == p.id;
    NVGcolor color = theme::playheadColor(p.color);
    float radius = kChipHeight / 2;
    if (selected || hot)
        draw::roundedRect(vg, part.rect, radius, selected ? nvgTransRGBAf(color, 0.15f) : c.bg2);
    if (selected)
        draw::roundedRectStroke(vg, part.rect, radius, nvgTransRGBAf(color, 0.6f), 1);
    Vec mid = part.rect.getCenter();
    // Its dot: filled when it plays, hollow when it is off; a curve or a lane on its On switches it as the song plays.
    bool on = deviceActions::playheadPlaysNow(ctx, p);
    nvgBeginPath(vg);
    nvgCircle(vg, part.rect.pos.x + 10.5f, mid.y, 3.5f);
    if (!on) {
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
    } else {
        nvgFillColor(vg, color);
        nvgFill(vg);
    }
    draw::TextStyle name{selected ? theme::fonts::uiStrong() : theme::fonts::ui(), 11.5f,
                         selected ? c.text0 : (on ? c.text1 : c.text2), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    std::string label = p.solo ? "S · " + p.name : p.name;
    draw::text(vg, name, part.rect.pos.x + 20, mid.y, draw::ellipsize(vg, name, label, part.rect.size.x - 24));
}

void DeviceStrip::drawTag(NVGcontext* vg, const Part& part, bool hot) {
    if (part.tag < 0 || part.tag >= int(tagRefs.size()))
        return;
    TesseraModule* m = ctx.module();
    const TagRef& t = tagRefs[size_t(part.tag)];
    const theme::Palette& c = theme::colors();
    const theme::DeviceColors& dc = theme::deviceColors();
    const Device* d = cardDevice(part.device);
    const devices::ParamInfo* info = deviceActions::infoOf(m->song(), t.ref);
    if (!d || !info)
        return;
    Rect r = part.rect;
    float radius = r.size.y / 2;
    bool mapping = ctx.mappingModulator != 0;
    bool lit = mapping && t.spec.family != Family::Channels && reachableNow(t.ref);
    std::optional<NVGcolor> moved =
        t.spec.family == Family::Channels ? std::nullopt : modulatorColorOn(t.ref);
    bool automated = t.spec.family != Family::Channels && m->automated(t.ref);
    NVGcolor laneColor = c.accent;
    if (automated) {
        std::vector<FoundLane> lanes = lanesTurning(m->song(), t.ref);
        if (!lanes.empty())
            laneColor = deviceActions::laneColor(m->song(), lanes.front());
    }
    bool active = focusedTag && *focusedTag == t &&
                  ((hasGesture() && hovered >= 0) || (panelTag && *panelTag == t && tagPanelOpen()));
    nvgSave(vg);
    float alpha = d->on ? 1.f : 0.4f;
    if (part.dim)
        alpha *= 0.38f;
    if (mapping && !lit)
        alpha *= 0.3f;
    nvgGlobalAlpha(vg, alpha);

    // Its pill: a soft drop shadow, then the face, tinted when it is the first on its picture.
    nvgBeginPath(vg);
    nvgRect(vg, r.pos.x - 4, r.pos.y - 2, r.size.x + 8, r.size.y + 8);
    nvgFillPaint(vg, nvgBoxGradient(vg, r.pos.x + 1, r.pos.y + 1.5f, r.size.x - 2, r.size.y, radius, 4,
                                    nvgRGBAf(0, 0, 0, c.dark ? 0.2f : 0.06f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    NVGcolor face = hot ? dc.tagHover : dc.tag;
    NVGcolor edge = dc.tagEdge;
    if (part.first && t.spec.family != Family::Toggle) {
        face = nvgLerpRGBA(dc.card, t.tint, hot ? 0.3f : 0.22f);
        edge = nvgTransRGBAf(t.tint, 0.7f);
    }
    if (lit) {
        NVGcolor mc = deviceActions::colorOf(ModulatorId(ctx.mappingModulator));
        face = nvgLerpRGBA(dc.tag, mc, 0.25f);
        edge = mc;
    } else if (moved) {
        edge = *moved;
    } else if (automated) {
        // A lane sets it: the whole tag takes the lane's colour, not only its outline.
        face = nvgLerpRGBA(dc.tag, laneColor, hot ? 0.34f : 0.26f);
        edge = laneColor;
    }
    draw::roundedRect(vg, r, radius, face);
    draw::roundedRectStroke(vg, r, radius, edge, moved || automated || lit ? 1.5f : 1);
    if (active) {
        draw::roundedRectStroke(vg, r.grow(Vec(1, 1)), radius + 1, t.tint, 1.5f);
        draw::roundedRectStroke(vg, r.grow(Vec(3.5f, 3.5f)), radius + 3.5f, nvgTransRGBAf(t.tint, 0.18f), 3);
    } else if (focusedTag && *focusedTag == t) {
        draw::roundedRectStroke(vg, r.grow(Vec(1.5f, 1.5f)), radius + 1.5f, nvgTransRGBAf(t.tint, 0.6f), 1);
    }

    float cy = r.getCenter().y;
    if (t.spec.family == Family::Toggle) {
        // A small switch after its name; as it plays now when something moves it.
        bool on = shownValue(t.ref) >= 0.5f;
        draw::TextStyle name{theme::fonts::ui(), mx::kDeviceTagText, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, name, r.pos.x + kTagPad, cy, info->name);
        Rect sw(r.getRight() - 4 - 22, cy - 6.5f, 22, 13);
        draw::roundedRect(vg, sw, 6.5f, on ? t.tint : c.line2);
        nvgBeginPath(vg);
        nvgCircle(vg, on ? sw.getRight() - 6.5f : sw.pos.x + 6.5f, cy, 4.5f);
        nvgFillColor(vg, nvgRGBf(0.9f, 0.91f, 0.92f));
        nvgFill(vg);
    } else {
        float x = r.pos.x + kTagPad;
        if (t.spec.prefix[0]) {
            draw::TextStyle pre{theme::fonts::ui(), mx::kDeviceTagText, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
            draw::text(vg, pre, x, cy, t.spec.prefix);
            x += draw::textWidth(vg, pre, t.spec.prefix) + 4;
        }
        tags::Values v;
        if (t.spec.family == Family::Channels)
            v.channels = channelsOf(*d);
        else if (t.spec.family == Family::Direction) {
            ParamRef step = t.ref;
            step.param = uint16_t(playheads::knob::kOrderStep);
            if (const float* s = findParam(m->song(), step))
                v.v[size_t(playheads::knob::kOrderStep)] =
                    devices::display(*devices::playheadParamInfo(playheads::knob::kOrderStep), *s);
        }
        nvgSave(vg);
        nvgIntersectScissor(vg, r.pos.x, r.pos.y, r.size.x - 4, r.size.y);
        tiles::drawValue(vg, x, cy, tagLabel(*this, t, v), mx::kDeviceTagText,
                         automated && !lit ? nvgLerpRGBA(c.text0, laneColor, 0.35f) : c.text0);
        nvgRestore(vg);
    }
    // Under it, the depth bar: how far each lane and modulation moves it across its whole range, and where it is now.
    if (t.spec.family != Family::Channels && t.spec.family != Family::Toggle) {
        std::vector<Span> spans = spansOn(t.ref);
        if (!spans.empty()) {
            // At least 48 px wide under a short tag, so a small reach still shows.
            float bw = std::max(48.f, r.size.x - radius * 1.2f);
            float x0 = r.getCenter().x - bw / 2, x1 = x0 + bw, y = r.getBottom() + 3;
            float w = x1 - x0;
            draw::roundedRect(vg, Rect(x0, y, w, 3), 1.5f, nvgTransRGBAf(dc.tagEdge, 0.7f));
            for (size_t k = 0; k < spans.size() && k < 3; k++) {
                float yy = y + float(k) * 4;
                if (k > 0)
                    draw::roundedRect(vg, Rect(x0, yy, w, 3), 1.5f, nvgTransRGBAf(dc.tagEdge, 0.5f));
                float a = x0 + spans[k].from * w, b = x0 + spans[k].to * w;
                draw::roundedRect(vg, Rect(a, yy, std::max(3.f, b - a), 3), 1.5f, spans[k].color);
            }
            float live = std::clamp(shownValue(t.ref), 0.f, 1.f);
            nvgBeginPath(vg);
            nvgCircle(vg, x0 + live * w, y + 1.5f, 2.6f);
            nvgFillColor(vg, c.text0);
            nvgFill(vg);
            nvgStrokeColor(vg, dc.screen);
            nvgStrokeWidth(vg, 1);
            nvgStroke(vg);
        }
    }
    // On its corner: an "A" in the lane's colour when a lane sets it (a click shows the lane), else a dot in the
    // modulator's colour when a modulator or a curve moves it.
    Vec corner(r.getRight() - 1, r.pos.y + 1);
    if (automated) {
        bool badgeHot = hovered >= 0 && parts[size_t(hovered)].kind == Kind::TagBadge &&
                        parts[size_t(hovered)].tag == part.tag;
        nvgBeginPath(vg);
        nvgCircle(vg, corner.x, corner.y, badgeHot ? 8.5f : 7.5f);
        nvgFillColor(vg, laneColor);
        nvgFill(vg);
        nvgStrokeColor(vg, dc.card);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
        draw::TextStyle a{theme::fonts::uiStrong(), 10, c.onTile, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, a, corner.x, corner.y + 0.5f, "A");
    } else if (moved) {
        nvgBeginPath(vg);
        nvgCircle(vg, corner.x, corner.y, 4);
        nvgFillColor(vg, *moved);
        nvgFill(vg);
        nvgStrokeColor(vg, dc.card);
        nvgStrokeWidth(vg, 1.5f);
        nvgStroke(vg);
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
        draw::text(vg, name, x, mid, nameOf(t->devices[i]));
        x += draw::textWidth(vg, name, nameOf(t->devices[i])) + 6;
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
        bool open = ctx.state().openModulator == p.id || deviceActions::curveShown(ctx, mod->id),
             mapping = ctx.mappingModulator == p.id;
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
                      "LFOs, envelopes and ramps that turn settings for you. + Add to start.");
    }
}

void DeviceStrip::drawForeground(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    if (!ctx.module() || !deviceActions::stripOpen(ctx) || squeezed())
        return;
    nvgSave(vg);
    nvgIntersectScissor(vg, chainLeft, 0, box.size.x - chainLeft, box.size.y);
    // The tags sit on the pictures, which the tile layer drew under this.
    for (size_t i = 0; i < parts.size(); i++)
        if (parts[i].kind == Kind::Tag || parts[i].kind == Kind::Switch)
            drawTag(vg, parts[i], int(i) == hovered);
    if (dropIndex >= 0) {
        // Where the dragged card lands: before card `dropIndex`, or after the last.
        float x = -1, y = kTop;
        int seen = 0;
        for (const Part& p : parts)
            if (p.kind == Kind::Card && p.device < kBranchBase) {
                if (seen == dropIndex) {
                    x = p.rect.pos.x - mx::kDeviceLink / 2;
                    y = p.rect.pos.y;
                }
                seen++;
            } else if (p.kind == Kind::AddTile && x < 0) {
                x = p.rect.pos.x - mx::kDeviceLink / 2;
                y = p.rect.pos.y - mx::kDeviceCardTop;
            }
        nvgBeginPath(vg);
        nvgRect(vg, x - 1, y, 2, kCardHeight);
        nvgFillColor(vg, c.accent);
        nvgFill(vg);
    }
    // A modulated tag names its modulations on hover.
    if (hovered >= 0 && size_t(hovered) < parts.size() &&
        (parts[size_t(hovered)].kind == Kind::Tag || parts[size_t(hovered)].kind == Kind::TagBadge) && !hasGesture()) {
        const Part& p = parts[size_t(hovered)];
        ParamRef ref = tagRefs[size_t(p.tag)].ref;
        const TagRef& t = tagRefs[size_t(p.tag)];
        std::string text;
        // Each lane and modulation, with the values it moves the setting between.
        const devices::ParamInfo* info = deviceActions::infoOf(ctx.module()->song(), ref);
        auto valueText = [&](float n) {
            if (!info || t.spec.family == Family::Channels)
                return std::string();
            if (t.spec.family == Family::Direction)
                return devices::format(*info, n);
            const tags::ValueList& list = listOf(t);
            return list.values.empty() ? devices::format(*info, n)
                                       : list.labels[size_t(list.nearest(devices::display(*info, n)))];
        };
        for (const Span& sp : spansOn(ref)) {
            if (sp.who.empty())
                continue;
            text += (text.empty() ? "" : "   ") + sp.who + ": " + valueText(sp.from) + " to " + valueText(sp.to);
        }
        if (ctx.module()->automated(ref))
            text += std::string(text.empty() ? "" : "   ") + "(click the A to show the lane)";
        // Nothing moves it: say what it is and how to change it.
        if (text.empty())
            text = std::string(t.spec.family == Family::Direction ? "Direction" : info ? info->name : "") +
                   (t.spec.family == Family::Channels ? ": click to pick channels"
                                                      : ": drag or scroll to change, click for every choice");
        if (!text.empty()) {
            draw::TextStyle style{theme::fonts::ui(), 10, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
            float w = draw::textWidth(vg, style, text) + 12;
            float x = std::clamp(p.rect.getCenter().x - w / 2, chainLeft + 2, box.size.x - w - 2);
            Rect pill(x, p.rect.getBottom() + 4, w, 16);
            draw::roundedRect(vg, pill, 4, c.bg2);
            draw::roundedRectStroke(vg, pill, 4, c.line1, 1);
            draw::text(vg, style, x + 6, pill.getCenter().y, text);
        }
    }
    nvgRestore(vg);
    // The bottom edge's grip: it resizes the strip.
    bool edgeHot = (hovered >= 0 && parts[size_t(hovered)].kind == Kind::ResizeEdge) || hasGesture() && dropIndex < 0 &&
                                                                                          !knobPreview && !amountPreview &&
                                                                                          !directionPreview &&
                                                                                          localMouse().y > box.size.y - 12;
    if (resizable())
        draw::roundedRect(vg, Rect(box.size.x / 2 - 18, box.size.y - 4.5f, 36, 3), 1.5f,
                          edgeHot ? c.text1 : theme::deviceColors().tagEdge);
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
    std::string hint = "drag a lit tag up or down to set how far it moves it  ·  Esc to finish";
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
