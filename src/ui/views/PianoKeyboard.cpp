#include "PianoKeyboard.hpp"

#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../interaction/Navigation.hpp"
#include "../widgets/CachedLayer.hpp"
#include "../widgets/Draw.hpp"
#include "NoteGrid.hpp"
#include "PianoRoll.hpp"

#include <map>

namespace tessera {

namespace {

constexpr float kKeyVelocity = 0.8f;
constexpr float kBlackKeyWidth = 0.6f; // share of the keyboard width
constexpr float kLip = 3;              // the front edge of a key, where the root mark sits
constexpr float kMinLabelRow = 8;      // rows smaller than this have no names
constexpr float kAllNamesRow = 15;     // from this row height every white key is named, not only C

/// Holds the key under the pointer; moving to another key releases the old one first (glissando).
class KeyGesture : public DragGesture {
  public:
    KeyGesture(PianoRollView& view, PianoKeyboard& keys, int pitch) : view(view), keys(keys), current(pitch) {
        view.auditionOn(current, kKeyVelocity);
        keys.playing = current;
    }
    void update(rack::math::Vec pos, Modifiers mods) override {
        int p = std::clamp(view.pitch.yToPitch(pos.y), 0, 127);
        if (p == current)
            return;
        view.auditionOff(current);
        current = p;
        view.auditionOn(current, kKeyVelocity);
        keys.playing = current;
    }
    void commit() override { release(); }
    void cancel() override { release(); }
    std::string readout() const override { return pitchName(current); }

  private:
    void release() {
        view.auditionOff(current);
        keys.playing = -1;
    }

    PianoRollView& view;
    PianoKeyboard& keys;
    int current;
};

} // namespace

PianoKeyboard::PianoKeyboard(EditorContext& ctx, PianoRollView& owner, NoteGrid& grid)
    : InteractiveView(ctx), owner(owner), grid(grid) {
    layer = new CachedLayer([this](NVGcontext* vg, rack::math::Vec) { paintKeys(vg); });
    addChild(layer);
}

int PianoKeyboard::pitchAt(float y) { return std::clamp(owner.pitch.yToPitch(y), 0, 127); }

std::unique_ptr<DragGesture> PianoKeyboard::startGesture(rack::math::Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || mods.ctrl || !owner.track())
        return nullptr;
    return std::make_unique<KeyGesture>(owner, *this, pitchAt(pos.y));
}

void PianoKeyboard::click(rack::math::Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || !mods.ctrl)
        return;
    // Ctrl/Cmd+click a key: select every note of that pitch (EDITOR_UX §4.1).
    int p = pitchAt(pos.y);
    edits::IdSet same;
    for (const Note& n : owner.committedNotes())
        if (n.pitch == p)
            same.insert(n.id);
    owner.select(same);
}

void PianoKeyboard::wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) {
    ctx.touchNavigation();
    if (mods.alt || mods.ctrl)
        grid.zoomVertical(pos.y, wheelZoomFactor(delta.y));
    else
        grid.setVerticalScroll(grid.verticalScroll() - delta.y);
}

CursorShape PianoKeyboard::hoverCursor(rack::math::Vec pos, Modifiers mods) {
    hoverPitch = pitchAt(pos.y);
    return CursorShape::Arrow;
}

void PianoKeyboard::onLeave(const LeaveEvent& e) {
    hoverPitch = -1;
    InteractiveView::onLeave(e);
}

void PianoKeyboard::step() {
    layer->setSize(box.size);
    const theme::Palette& c = theme::colors();
    const Track* track = owner.track();
    look.lit = track ? theme::track(track->color) : c.accent;
    look.laneNames.clear();
    if (track && track->type == TrackType::Drum)
        for (const DrumLane& lane : track->drumLanes)
            look.laneNames.emplace(lane.pitch, lane.name);
    look.named = owner.pitch.folded() || !look.laneNames.empty();
    // The root is marked while the key is highlighted.
    look.rootMark = owner.highlightKey();
    look.key = owner.scale();

    // Keys light up while their notes sound, in the track's colour, and while one is auditioned.
    sounding.reset();
    if (playing >= 0 && playing < 128)
        sounding.set(size_t(playing));
    TesseraModule* m = ctx.module();
    double now = m && m->playing() ? grid.playhead() : NAN;
    if (std::isfinite(now))
        for (const Note& n : owner.notes())
            if (!n.muted && double(n.start) <= now && now < double(n.end()) && n.pitch >= 0)
                sounding.set(size_t(n.pitch));

    const PitchAxis& pitch = owner.pitch;
    uint64_t key = 0;
    key = hashMix(key, pitch.top());
    key = hashMix(key, pitch.rowHeight());
    key = hashMix(key, look.named ? 1 : 0);
    key = hashMix(key, accidentals() == Accidentals::Flats ? 1 : 0); // the names repaint in the new spelling
    key = hashMix(key, double(look.rootMark ? look.key.root() * 4096 + look.key.mask() + 1 : 0));
    if (pitch.folded())
        for (int r = 0; r < pitch.rowCount(); r++)
            key = hashMix(key, double(pitch.pitchAtRow(r)));
    for (const auto& [lanePitch, name] : look.laneNames)
        key = hashMix(key, double(lanePitch * 1000003 + int64_t(std::hash<std::string>()(name) & 0xFFFFFF)));
    layer->setKey(key);
    InteractiveView::step();
}

void PianoKeyboard::visibleRows(int& first, int& last, int& lo, int& hi) const {
    const PitchAxis& pitch = owner.pitch;
    first = std::max(0, pitch.yToRow(0));
    last = std::min(pitch.rowCount() - 1, pitch.yToRow(box.size.y));
    hi = pitch.pitchAtRow(std::max(0, first));
    lo = pitch.pitchAtRow(std::min(pitch.rowCount() - 1, last));
}

void PianoKeyboard::paintKeys(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, c.bg1);
    nvgFill(vg);

    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x, box.size.y);
    int first, last, lo, hi;
    visibleRows(first, last, lo, hi);
    if (look.named) {
        for (int r = first; r <= last; r++)
            paintNamedKey(vg, owner.pitch.pitchAtRow(r), false, false);
    } else {
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, box.size.x - 1, box.size.y);
        nvgFillColor(vg, theme::keyColors().gap);
        nvgFill(vg);
        // White keys first, then the black keys on top of their wider front parts.
        for (int p = std::min(127, hi + 1); p >= std::max(0, lo - 1); p--)
            if (!isBlackKey(p))
                paintWhiteKey(vg, p, false, false);
        for (int p = std::min(127, hi + 1); p >= std::max(0, lo - 1); p--)
            if (isBlackKey(p))
                paintBlackKey(vg, p, false, false, true);
    }
    nvgRestore(vg);

    nvgBeginPath(vg);
    nvgRect(vg, box.size.x - 1, 0, 1, box.size.y);
    nvgFillColor(vg, c.line2);
    nvgFill(vg);
}

void PianoKeyboard::drawForeground(const DrawArgs& args) {
    if (sounding.none() && hoverPitch < 0)
        return;
    NVGcontext* vg = args.vg;
    int first, last, lo, hi;
    visibleRows(first, last, lo, hi);
    auto on = [&](int p) { return p >= 0 && p < 128 && sounding.test(size_t(p)); };
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x - 1, box.size.y);
    if (look.named) {
        for (int r = first; r <= last; r++) {
            int p = owner.pitch.pitchAtRow(r);
            if (on(p) || p == hoverPitch)
                paintNamedKey(vg, p, on(p), p == hoverPitch);
        }
    } else {
        float w = box.size.x - 1;
        for (int p = std::min(127, hi + 1); p >= std::max(0, lo - 1); p--) {
            if (isBlackKey(p) || !(on(p) || p == hoverPitch))
                continue;
            // A lit white key is repainted whole, so its black neighbours go back on top, clipped to it so their
            // shadows aren't laid twice over the keys around it.
            auto [y0, y1] = whiteKeySpan(p);
            nvgSave(vg);
            nvgIntersectScissor(vg, 0, y0, w, y1 - y0);
            paintWhiteKey(vg, p, on(p), p == hoverPitch);
            for (int n : {p + 1, p - 1})
                if (n >= 0 && n <= 127 && isBlackKey(n))
                    paintBlackKey(vg, n, false, false, true);
            nvgRestore(vg);
        }
        for (int p = std::min(127, hi + 1); p >= std::max(0, lo - 1); p--)
            if (isBlackKey(p) && (on(p) || p == hoverPitch))
                paintBlackKey(vg, p, on(p), p == hoverPitch, false);
    }
    nvgRestore(vg);
}

std::pair<float, float> PianoKeyboard::whiteKeySpan(int p) const {
    float row = float(owner.pitch.rowHeight());
    float y = float(owner.pitch.pitchToY(p));
    float up = p < 127 && isBlackKey(p + 1) ? 0.5f : 0.f;
    float down = p > 0 && isBlackKey(p - 1) ? 0.5f : 0.f;
    return {y - up * row + 0.5f, y + row + down * row - 0.5f};
}

void PianoKeyboard::paintWhiteKey(NVGcontext* vg, int p, bool on, bool hover) {
    const theme::Palette& c = theme::colors();
    const theme::KeyColors& k = theme::keyColors();
    float row = float(owner.pitch.rowHeight());
    float w = box.size.x - 1;
    float y = float(owner.pitch.pitchToY(p));
    // One row tall behind the black keys, wider in front of them where they meet their neighbours halfway, as on
    // a real piano (C, E, F, B are 1.5 rows at the front; D, G, A are 2).
    auto [y0, y1] = whiteKeySpan(p);
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, 0, y0, w, y1 - y0, 0, 1.5f, 1.5f, 0);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, w * 0.18f, 0, k.ivoryShade, k.ivory));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, w - kLip, y0, kLip, y1 - y0, 0, 1.5f, 1.5f, 0);
    nvgFillColor(vg, k.ivoryShade);
    nvgFill(vg);
    if (on || hover) {
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, 0, y0, w, y1 - y0, 0, 1.5f, 1.5f, 0);
        nvgFillColor(vg, nvgTransRGBAf(on ? look.lit : c.accent, on ? 0.55f : 0.15f));
        nvgFill(vg);
    }
    if (look.rootMark && look.key.isRoot(p))
        drawRootMark(vg, y, row);
    bool isC = p % 12 == 0;
    if (row >= kMinLabelRow && (isC || row >= kAllNamesRow)) {
        draw::TextStyle label{isC ? theme::fonts::uiStrong() : theme::fonts::ui(),
                              std::min(theme::metrics::kTextRuler, row - 2), isC || on ? k.label : k.labelSoft,
                              NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
        draw::text(vg, label, w - kLip - 5, y + row / 2, pitchName(p));
    }
}

void PianoKeyboard::paintBlackKey(NVGcontext* vg, int p, bool on, bool hover, bool shadow) {
    const theme::Palette& c = theme::colors();
    const theme::KeyColors& k = theme::keyColors();
    float row = float(owner.pitch.rowHeight());
    float bw = (box.size.x - 1) * kBlackKeyWidth;
    // Raised: a soft shadow on the white keys, a light front edge.
    float y = float(owner.pitch.pitchToY(p)) + 0.6f, h = row - 1.2f;
    if (shadow) {
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, 0, y + 1.2f, bw + 1.8f, h, 0, 2, 2, 0);
        nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.28f));
        nvgFill(vg);
    }
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, 0, y, bw, h, 0, 2, 2, 0);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, bw * 0.85f, 0, k.ebonyDeep, k.ebony));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, bw * 0.85f, y, bw * 0.15f, h, 0, 2, 2, 0);
    nvgFillPaint(vg, nvgLinearGradient(vg, bw * 0.85f, 0, bw, 0, k.ebony, k.gloss));
    nvgFill(vg);
    if (on || hover) {
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, 0, y, bw, h, 0, 2, 2, 0);
        nvgFillColor(vg, nvgTransRGBAf(on ? look.lit : c.accent, on ? 0.85f : 0.3f));
        nvgFill(vg);
    }
    if (look.rootMark && look.key.isRoot(p))
        drawRootMark(vg, y - 0.6f, row);
}

void PianoKeyboard::paintNamedKey(NVGcontext* vg, int p, bool on, bool hover) {
    const theme::Palette& c = theme::colors();
    const theme::KeyColors& k = theme::keyColors();
    float row = float(owner.pitch.rowHeight());
    float w = box.size.x - 1;
    float y = float(owner.pitch.pitchToY(p));
    bool black = isBlackKey(p);
    rack::math::Rect body(0, y + 0.5f, w, row - 1);
    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, body.pos.x, body.pos.y, body.size.x, body.size.y, 0, 2, 2, 0);
    nvgFillColor(vg, black ? k.ebony : k.ivory);
    nvgFill(vg);
    if (black) {
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, w - kLip - 1, body.pos.y, kLip + 1, body.size.y, 0, 2, 2, 0);
        nvgFillColor(vg, k.gloss);
        nvgFill(vg);
    }
    if (on || hover) {
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, body.pos.x, body.pos.y, body.size.x, body.size.y, 0, 2, 2, 0);
        nvgFillColor(vg, nvgTransRGBAf(on ? look.lit : c.accent, on ? 0.7f : 0.2f));
        nvgFill(vg);
    }
    bool root = look.rootMark && look.key.isRoot(p);
    if (root)
        drawRootMark(vg, y, row);
    if (row < kMinLabelRow)
        return;
    draw::TextStyle label{root ? theme::fonts::uiStrong() : theme::fonts::ui(),
                          std::min(theme::metrics::kTextRuler, row - 1), black && !on ? k.ivory : k.label,
                          NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
    auto lane = look.laneNames.find(p);
    if (lane != look.laneNames.end()) {
        label.font = theme::fonts::ui();
        label.align = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE;
        draw::text(vg, label, 5, y + row / 2, draw::ellipsize(vg, label, lane->second, w - 12));
    } else {
        draw::text(vg, label, w - kLip - 5, y + row / 2, pitchName(p));
    }
}

void PianoKeyboard::drawRootMark(NVGcontext* vg, float y, float row) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, box.size.x - 1 - kLip, y + 2, kLip, std::max(1.f, row - 4), 1);
    nvgFillColor(vg, theme::colors().accent);
    nvgFill(vg);
}

} // namespace tessera
