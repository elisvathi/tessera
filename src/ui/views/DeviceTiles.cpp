#include "DeviceTiles.hpp"

#include "../../model/Devices.hpp"
#include "../../model/Playheads.hpp"
#include "../../model/Scale.hpp"
#include "../Theme.hpp"
#include "../widgets/Draw.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tessera::tiles {

using rack::math::Rect;
using rack::math::Vec;

namespace {

namespace ph = playheads::knob;
namespace dk = chain::knob;

constexpr float kYB = 70; // the time line most pictures stand on
constexpr float kPi = 3.14159265f;
constexpr float kRnd[] = {0.13f, 0.71f, 0.42f, 0.88f, 0.27f, 0.63f, 0.05f, 0.94f, 0.51f, 0.36f,
                          0.79f, 0.18f, 0.67f, 0.31f, 0.84f, 0.46f, 0.22f, 0.58f, 0.09f, 0.75f};

/// What a shape is made of, as the mockup's classes name them.
enum class Style { Note, Ghost, GhostDash, Key, KeyBlack, Band };

/// The pen a picture draws with: the card's colour and the strip's greys.
struct Pen {
    NVGcontext* vg;
    NVGcolor tint;
    const theme::DeviceColors& c;

    /// A rounded box: a coloured note (with its glow), a grey outline, a dashed outline, a key or a faint band.
    void box(float x, float y, float w, float h, Style s, float opacity = 1, float r = 3) const {
        w = std::max(2.f, w);
        h = std::max(1.f, h);
        opacity = std::clamp(opacity, 0.1f, 1.f);
        switch (s) {
        case Style::Note: {
            float g = theme::metrics::kDeviceGlow;
            nvgBeginPath(vg);
            nvgRect(vg, x - 2 * g, y - 2 * g, w + 4 * g, h + 4 * g);
            nvgFillPaint(vg, nvgBoxGradient(vg, x, y, w, h, r, 2 * g, nvgTransRGBAf(tint, c.glow * opacity),
                                            nvgTransRGBAf(tint, 0)));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, w, h, std::min(r, std::min(w, h) / 2));
            nvgFillColor(vg, nvgTransRGBAf(tint, opacity));
            nvgFill(vg);
            break;
        }
        case Style::Ghost:
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, w, h, std::min(r, std::min(w, h) / 2));
            nvgStrokeColor(vg, c.ghost);
            nvgStrokeWidth(vg, 1.3f);
            nvgStroke(vg);
            break;
        case Style::GhostDash:
            dashedBox(x, y, w, h);
            break;
        case Style::Key:
        case Style::KeyBlack:
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, w, h, std::min(r, std::min(w, h) / 2));
            nvgFillColor(vg, s == Style::Key ? c.key : c.keyBlack);
            nvgFill(vg);
            break;
        case Style::Band:
            nvgBeginPath(vg);
            nvgRoundedRect(vg, x, y, w, h, std::min(r, std::min(w, h) / 2));
            nvgFillColor(vg, nvgTransRGBAf(tint, 0.1f * opacity));
            nvgFill(vg);
            break;
        }
    }

    /// A box outlined in short dashes: notes that are left out.
    void dashedBox(float x, float y, float w, float h) const {
        nvgBeginPath(vg);
        auto side = [&](float x0, float y0, float x1, float y1) {
            float len = std::hypot(x1 - x0, y1 - y0);
            int n = int(std::clamp(std::ceil(len / 6), 0.f, 400.f));
            for (int k = 0; k < n; k++) {
                float d = float(k) * 6, e = std::min(len, d + 3);
                nvgMoveTo(vg, x0 + (x1 - x0) * d / len, y0 + (y1 - y0) * d / len);
                nvgLineTo(vg, x0 + (x1 - x0) * e / len, y0 + (y1 - y0) * e / len);
            }
        };
        side(x, y, x + w, y);
        side(x + w, y, x + w, y + h);
        side(x + w, y + h, x, y + h);
        side(x, y + h, x, y);
        nvgStrokeColor(vg, c.ghostDash);
        nvgStrokeWidth(vg, 1.2f);
        nvgStroke(vg);
    }

    void line(float x0, float y0, float x1, float y1, NVGcolor color, float width) const {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x0, y0);
        nvgLineTo(vg, x1, y1);
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, width);
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
    }

    /// The time line along the bottom of a picture.
    void base(float w, float y = kYB) const { line(10, y, w - 10, y, c.base, 2); }
    void tick(float x0, float y0, float x1, float y1) const { line(x0, y0, x1, y1, c.tick, 1.3f); }
    void guide(float x0, float y0, float x1, float y1, float width = 1.3f) const {
        line(x0, y0, x1, y1, c.guide, width);
    }
    void stroke(float x0, float y0, float x1, float y1, float width = 2, float opacity = 1) const {
        line(x0, y0, x1, y1, nvgTransRGBAf(tint, opacity), width);
    }

    /// Small dots along a path, the mockup's dotted coloured line.
    void dots(const std::vector<Vec>& pts, float opacity = 0.85f) const {
        nvgBeginPath(vg);
        float carry = 0;
        for (size_t i = 1; i < pts.size(); i++) {
            Vec a = pts[i - 1], b = pts[i];
            float len = a.minus(b).norm();
            if (!(len > 0.01f) || len > 4000)
                continue;
            int n = int(std::ceil((len - carry) / 5));
            for (int k = 0; k < n; k++) {
                float d = carry + float(k) * 5;
                nvgCircle(vg, a.x + (b.x - a.x) * d / len, a.y + (b.y - a.y) * d / len, 0.8f);
            }
            carry = std::max(0.f, carry + float(n) * 5 - len);
        }
        nvgFillColor(vg, nvgTransRGBAf(tint, opacity));
        nvgFill(vg);
    }

    /// A quadratic curve from a through control c to b, as points.
    static std::vector<Vec> quad(Vec a, Vec ctl, Vec b) {
        std::vector<Vec> out;
        for (int i = 0; i <= 16; i++) {
            float t = float(i) / 16;
            out.push_back(a.mult((1 - t) * (1 - t)).plus(ctl.mult(2 * (1 - t) * t)).plus(b.mult(t * t)));
        }
        return out;
    }

    /// A dotted arrow from one place to another, bent upward by `bend`.
    void arrow(float x0, float y0, float x1, float y1, float bend) const {
        float mx = (x0 + x1) / 2, my = (y0 + y1) / 2 - bend, a = std::atan2(y1 - my, x1 - mx), h = 5;
        dots(quad(Vec(x0, y0), Vec(mx, my), Vec(x1, y1)));
        nvgBeginPath(vg);
        nvgMoveTo(vg, x1 - h * std::cos(a - 0.5f), y1 - h * std::sin(a - 0.5f));
        nvgLineTo(vg, x1, y1);
        nvgLineTo(vg, x1 - h * std::cos(a + 0.5f), y1 - h * std::sin(a + 0.5f));
        nvgStrokeColor(vg, tint);
        nvgStrokeWidth(vg, 1.5f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }

    void cross(float x, float y) const {
        guide(x - 3.5f, y - 3.5f, x + 3.5f, y + 3.5f, 1.5f);
        guide(x + 3.5f, y - 3.5f, x - 3.5f, y + 3.5f, 1.5f);
    }

    void circle(float x, float y, float r, Style s, float opacity = 1) const {
        if (s == Style::Note) {
            float g = theme::metrics::kDeviceGlow;
            nvgBeginPath(vg);
            nvgCircle(vg, x, y, r + 2 * g);
            nvgFillPaint(vg, nvgRadialGradient(vg, x, y, r, r + 2 * g, nvgTransRGBAf(tint, c.glow * opacity),
                                               nvgTransRGBAf(tint, 0)));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgCircle(vg, x, y, r);
            nvgFillColor(vg, nvgTransRGBAf(tint, opacity));
            nvgFill(vg);
        } else if (s == Style::GhostDash) {
            nvgBeginPath(vg);
            for (int k = 0; k < 12; k++) {
                float a0 = float(k) / 12 * 2 * kPi;
                nvgArc(vg, x, y, r, a0, a0 + kPi / 12, NVG_CW);
                nvgMoveTo(vg, x + r * std::cos(a0 + 2 * kPi / 12), y + r * std::sin(a0 + 2 * kPi / 12));
            }
            nvgStrokeColor(vg, c.ghostDash);
            nvgStrokeWidth(vg, 1.2f);
            nvgStroke(vg);
        } else {
            nvgBeginPath(vg);
            nvgCircle(vg, x, y, r);
            nvgStrokeColor(vg, c.ghost);
            nvgStrokeWidth(vg, 1.3f);
            nvgStroke(vg);
        }
    }

    /// The playhead mark: a stem and a small triangle on top; `ghost` draws where it would have been.
    void marker(float x, bool ghost = false) const {
        if (ghost) {
            nvgBeginPath(vg);
            for (int k = 0; k < 5; k++) {
                float y = kYB - 26 + float(k) * 6;
                nvgMoveTo(vg, x, y);
                nvgLineTo(vg, x, std::min(kYB, y + 3));
            }
            nvgStrokeColor(vg, c.guide);
            nvgStrokeWidth(vg, 1.3f);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x - 6, kYB - 29);
            nvgLineTo(vg, x + 6, kYB - 29);
            nvgLineTo(vg, x, kYB - 21);
            nvgClosePath(vg);
            nvgStrokeColor(vg, c.ghost);
            nvgStrokeWidth(vg, 1.3f);
            nvgStroke(vg);
            return;
        }
        stroke(x, kYB - 26, x, kYB + 4, 2.5f);
        nvgBeginPath(vg);
        nvgMoveTo(vg, x - 6, kYB - 29);
        nvgLineTo(vg, x + 6, kYB - 29);
        nvgLineTo(vg, x, kYB - 21);
        nvgClosePath(vg);
        nvgFillColor(vg, tint);
        nvgFill(vg);
    }

    /// A chevron pointing the way a playhead moves.
    void chevron(float cx, float dir, float opacity) const {
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx, kYB - 17);
        nvgLineTo(vg, cx + 6 * dir, kYB - 11);
        nvgLineTo(vg, cx, kYB - 5);
        nvgStrokeColor(vg, nvgTransRGBAf(tint, std::clamp(opacity, 0.2f, 1.f)));
        nvgStrokeWidth(vg, 2);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }
};

double sixteenths(DeviceType type, const tags::Values& v, int param) {
    return tags::sixteenthsOf(type, param, v[param]);
}

// ---- Playheads ------------------------------------------------------------------------------------------------

void drawMoves(const Pen& p, const tags::Values& v, float w) {
    int d = v.i(ph::kDirection), order = v.i(ph::kOrder);
    int n = std::clamp(int(std::lround(3 + std::log2(tags::rateOf(v[ph::kRate])))), 1, 7);
    if (order > 0)
        d = 0;
    float px = d == 0 ? w * 0.32f : d == 1 ? w * 0.68f : w * 0.5f;
    p.base(w);
    if (order > 0) {
        // Steps in another order: hops from the playhead to places along its way, in the order's sequence.
        px = w * 0.2f;
        float spread = w - 40 - px;
        float last = px;
        for (int k = 0; k < std::min(n + 1, 5); k++) {
            int step = playheads::orderedStep(playheads::Order(order), k, 5, 0, 7);
            float x = px + 14 + float(step) / 4 * spread;
            p.dots(Pen::quad(Vec(last, kYB - 24), Vec((last + x) / 2, kYB - 44), Vec(x, kYB - 24)), 0.85f - k * 0.1f);
            p.box(x - 4, kYB - 14, 8, 14, Style::Note, 0.95f - float(k) * 0.12f, 2);
            last = x;
        }
        p.marker(px);
        return;
    }
    if (d < 2) {
        float dir = d == 0 ? 1 : -1, tl = 26 + float(n) * 6;
        float x0 = dir > 0 ? px - tl : px;
        nvgBeginPath(p.vg);
        nvgRect(p.vg, x0, kYB - 22, tl, 22);
        nvgFillPaint(p.vg, nvgLinearGradient(p.vg, dir > 0 ? x0 : x0 + tl, 0, dir > 0 ? x0 + tl : x0, 0,
                                             nvgTransRGBAf(p.tint, 0), nvgTransRGBAf(p.tint, 0.45f)));
        nvgFill(p.vg);
        for (int k = 0; k < n; k++)
            p.chevron(px + dir * (14 + float(k) * 11), dir, 0.95f - float(k) * 0.11f);
    } else if (d == 2) {
        for (int j = 0; j < std::max(1, (n + 1) / 2); j++) {
            p.chevron(px + 14 + float(j) * 11, 1, 0.95f - float(j) * 0.18f);
            p.chevron(px - 14 - float(j) * 11, -1, 0.95f - float(j) * 0.18f);
        }
    } else {
        p.stroke(px + 13, kYB - 18, px + 13, kYB - 5, 3);
        p.stroke(px + 21, kYB - 18, px + 21, kYB - 5, 3);
    }
    p.marker(px);
}

void drawStart(const Pen& p, const tags::Values& v, float w) {
    float o = float(std::clamp(sixteenths(DeviceType::Playheads, v, ph::kTimeOffset), -7.5, 7.5));
    float x0 = w / 2, x1 = x0 + o * (w - 40) / 16;
    p.base(w);
    for (int t = -2; t <= 2; t++)
        p.tick(x0 + float(t) * (w - 40) / 4, kYB - (t == 0 ? 14 : 8), x0 + float(t) * (w - 40) / 4, kYB);
    if (std::fabs(x1 - x0) > 1) {
        p.marker(x0, true);
        std::vector<Vec> arc = Pen::quad(Vec(x0, kYB - 35), Vec((x0 + x1) / 2, kYB - 46), Vec(x1, kYB - 35));
        nvgBeginPath(p.vg);
        for (size_t i = 0; i < arc.size(); i++)
            i == 0 ? nvgMoveTo(p.vg, arc[i].x, arc[i].y) : nvgLineTo(p.vg, arc[i].x, arc[i].y);
        nvgStrokeColor(p.vg, p.tint);
        nvgStrokeWidth(p.vg, 1.5f);
        nvgLineCap(p.vg, NVG_ROUND);
        nvgStroke(p.vg);
    }
    p.marker(x1);
}

void drawRepeat(const Pen& p, const tags::Values& v, float w) {
    p.base(w);
    if (v.i(ph::kRepeatSize) == 0) {
        p.box(12, kYB - 18, w - 24, 14, Style::Note, 0.45f);
        return;
    }
    int n = std::max(1, v.i(ph::kRepeatTimes));
    auto [bend, strength] = playheads::curveShape(v.i(ph::kCurve));
    float bw = float(std::clamp(sixteenths(DeviceType::Playheads, v, ph::kRepeatSize) * 4, 4.0, 28.0));
    float span = w - 24 - bw;
    for (int k = 0; k < n; k++) {
        double g = double(k) / n;
        float hx = float(playheads::unwarp(bend, strength, g));
        p.box(12 + std::clamp(hx, 0.f, 1.f) * span, kYB - 18, std::min(bw, span / float(n) + 4), 14, Style::Note,
              1 - float(k) * 0.05f);
        nvgBeginPath(p.vg);
        nvgCircle(p.vg, 12 + float(g) * span + 2, kYB + 8, 1.6f);
        nvgFillColor(p.vg, p.c.guide);
        nvgFill(p.vg);
    }
    std::vector<Vec> curve;
    for (int q = 0; q <= 24; q++) {
        double t = double(q) / 24;
        curve.push_back(Vec(12 + float(t) * span, kYB - 24 - float(playheads::warp(bend, strength, t)) * 12));
    }
    p.dots(curve);
}

// ---- Pitch ----------------------------------------------------------------------------------------------------

void drawTranspose(const Pen& p, const tags::Values& v, float w) {
    int t = v.i(dk::kSemitones) + 12 * v.i(dk::kOctaves) + int(std::lround(v[dk::kKeySteps] * 12 / 7));
    float cy = 56, dy = -float((t > 0) - (t < 0)) * std::min(17.f, std::sqrt(float(std::abs(t))) * 4.4f);
    for (int k = -3; k <= 3; k++)
        p.tick(14, cy + float(k) * 5, 14 + (k == 0 ? 8 : 5), cy + float(k) * 5);
    float x0 = 34, x1 = w - 92;
    p.box(x0, cy - 6, 52, 12, Style::Ghost);
    p.arrow(x0 + 58, cy, x1 - 6, cy + dy, 6);
    p.box(x1, cy + dy - 6, 52, 12, Style::Note);
    std::string label = (t > 0 ? "+" : t < 0 ? "−" : "") + std::to_string(std::abs(t));
    draw::TextStyle big{theme::fonts::uiStrong(), 15, p.tint, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE};
    draw::text(p.vg, big, x1 + 60, cy + dy + 5, label);
}

void drawScale(const Pen& p, const tags::Values& v, float w) {
    bool fixed = v.i(dk::kKeyFrom) == int(chain::KeyFrom::Fixed);
    const std::vector<ScaleDef>& scales = builtinScales();
    int root = fixed ? v.i(dk::kRoot) : v.keyRoot;
    uint16_t mask = fixed ? scales[size_t(std::clamp(v.i(dk::kScale), 0, int(scales.size()) - 1))].mask : v.keyMask;
    float kw = (w - 28) / 12;
    auto pc = [](int x) { return ((x % 12) + 12) % 12; };
    auto inKey = [&](int x) { return (mask >> pc(x - root)) & 1; };
    auto kx = [&](int x) { return 14 + float(pc(x)) * kw; };
    for (int k = 0; k < 12; k++) {
        bool black = k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
        bool lit = inKey(k), isRoot = pc(k - root) == 0;
        p.box(kx(k) + 1, 46, kw - 2, black ? 18 : 24, lit ? Style::Note : black ? Style::KeyBlack : Style::Key,
              lit ? (isRoot ? 1 : black ? 0.55f : 0.75f) : 1, 2.5f);
        if (isRoot)
            p.circle(kx(k) + kw / 2, kYB + 6, 2, Style::Note);
    }
    static const int kOrder[] = {6, 5, 7, 4, 8, 3, 9, 2, 10, 1, 11, 0};
    int off = -1;
    for (int k : kOrder)
        if (!inKey(k)) {
            off = k;
            break;
        }
    if (off < 0)
        return;
    float ox = kx(off) + kw / 2;
    auto mode = chain::OffKey(v.i(dk::kOffKey));
    p.circle(ox, 38, 3.5f, Style::Ghost);
    if (mode == chain::OffKey::Drop) {
        p.cross(ox, 38);
        return;
    }
    int q = off;
    for (int d = 1; d < 7; d++) {
        if (mode != chain::OffKey::Up && inKey(off - d)) {
            q = off - d;
            break;
        }
        if (mode != chain::OffKey::Down && inKey(off + d)) {
            q = off + d;
            break;
        }
    }
    p.arrow(ox + (q > off ? 4.f : -4.f), 37, kx(q) + kw / 2, 43, 7);
}

void drawChord(const Pen& p, const tags::Values& v, float w) {
    std::vector<int> t = tags::chordOnC(v);
    for (int& x : t)
        x -= 60;
    int lo = 0, hi = 0;
    for (int x : t) {
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    float rh = std::min(2.4f, 30.f / float(std::max(1, hi - lo)));
    auto y = [&](int x) { return 56 - (float(x) - float(lo + hi) / 2) * rh - 3.5f; };
    p.box(16, y(0), 54, 7, Style::Ghost);
    p.arrow(76, y(0) + 3.5f, 104, 56, 0);
    float x0 = 112;
    for (size_t i = 0; i < t.size(); i++) {
        float dx = float(i) * v[dk::kStrum] / 100 * 22;
        p.box(x0 + dx, y(t[i]), w - 16 - x0 - dx, 7, Style::Note, 1 - float(i) * 0.08f);
    }
}

// ---- Rhythm ---------------------------------------------------------------------------------------------------

void drawLength(const Pen& p, const tags::Values& v, float w) {
    const float xs[] = {14, 80, 146}, ys[] = {44, 58, 50}, ow = 42;
    auto mode = chain::LengthMode(v.i(dk::kMode));
    for (int i = 0; i < 3; i++) {
        float x = xs[i], len;
        if (mode == chain::LengthMode::Scale)
            len = ow * v[dk::kScaleBy] / 100;
        else if (mode == chain::LengthMode::Fixed)
            len = float(sixteenths(DeviceType::NoteLength, v, dk::kLength)) * 10;
        else
            len = (i < 2 ? xs[i + 1] - x : w - 14 - x) + v[dk::kOverlap] / 100 * 12;
        p.box(x, ys[i] - 2, ow, 13, Style::Ghost);
        p.box(x + 2, ys[i], std::min(len, w - x) - 4, 9, Style::Note);
    }
}

void drawArpNotes(const Pen& p, const tags::Values& v, float w) {
    int pool = 3 * std::max(1, v.i(dk::kArpOctaves));
    float sw = (w - 70) / 8;
    auto y = [&](int i) { return kYB - 6 - (pool > 1 ? float(i) / float(pool - 1) : 0.5f) * 28; };
    for (int i = 0; i < 3; i++)
        p.box(14, kYB - 6 - float(i) * 9 - 3, 30, 6, Style::Ghost);
    p.arrow(48, 56, 58, 56, 0);
    bool chord = v.i(dk::kPattern) == int(chain::ArpPattern::Chord);
    for (int k = 0; k < 8; k++) {
        float x = 62 + float(k) * sw, op = 1 - float(k) * 0.04f;
        if (chord)
            for (int c = 0; c < pool; c++)
                p.box(x, y(c) - 3, sw - 4, 5, Style::Note, op);
        else
            p.box(x, y(tags::arpStep(v.i(dk::kPattern), pool, k)) - 3.5f, sw - 4, 7, Style::Note, op);
    }
}

void drawArpSteps(const Pen& p, const tags::Values& v, float w) {
    int n = std::clamp(int(std::lround(8 / std::max(0.25, sixteenths(DeviceType::Arpeggiator, v, dk::kRate)))), 1, 16);
    float sw = (w - 24) / float(n);
    p.base(w);
    const float vels[] = {1, 0.75f, 0.9f, 0.65f, 0.95f, 0.7f, 0.85f, 0.6f};
    auto mode = chain::ArpVelocity(v.i(dk::kArpVelocity));
    for (int k = 0; k < n; k++) {
        float x = 12 + float(k) * sw + (k % 2 ? sw * v[dk::kSwing] / 100 * 0.5f : 0);
        float bw = sw * v[dk::kGate] / 100 - 2;
        float vv = mode == chain::ArpVelocity::Fixed    ? 0.8f
                   : mode == chain::ArpVelocity::Accent ? (k % 4 ? 0.5f : 1.f)
                                                        : vels[k % 8];
        float h = 8 + vv * 22;
        p.tick(12 + float(k) * sw, kYB + 3, 12 + float(k) * sw, kYB + 7);
        p.box(x, kYB - h, std::min(bw, w - 10 - x), h, Style::Note, 0.45f + vv * 0.55f);
    }
}

void drawNoteRepeat(const Pen& p, const tags::Values& v, float w) {
    int n = std::clamp(int(std::lround(16 / std::max(0.25, sixteenths(DeviceType::NoteRepeat, v, dk::kRepeatRate)))),
                       1, 32);
    float sw = (w - 28) / float(n);
    p.box(14, 40, w - 28, 10, Style::Ghost);
    for (int k = 0; k < n; k++)
        p.box(14 + float(k) * sw, 55, std::max(2.f, sw * v[dk::kRepeatGate] / 100 - 1), 14, Style::Note,
              std::pow(1 - v[dk::kRepeatDecay] / 100 * 0.3f, float(k)));
}

void drawEcho(const Pen& p, const tags::Values& v, float w) {
    float sp = float(std::clamp(sixteenths(DeviceType::Echo, v, dk::kTime) * 7.5, 9.0, 120.0));
    float bw = std::clamp(sp * v[dk::kEchoGate] / 100, 3.f, 26.f);
    p.base(w);
    p.box(20, kYB - 38, 26, 38, Style::Note);
    std::vector<Vec> tops = {Vec(46, kYB - 38)};
    for (int k = 1; k <= v.i(dk::kRepeats); k++) {
        float x = 20 + float(k) * sp;
        if (x + bw > w - 12)
            break;
        float h = std::max(4.f, 38 * std::pow(1 - v[dk::kDecay] / 100, float(k)));
        float lift = std::clamp(float(k) * v[dk::kEchoPitch] * 1.3f, -12.f, 18.f);
        float y = kYB - h - std::max(0.f, lift);
        p.box(x, y, bw, h, Style::Note, std::max(0.25f, 1 - float(k) * 0.12f));
        tops.push_back(Vec(x + bw / 2, y));
    }
    if (tops.size() > 1)
        p.dots(tops);
    p.guide(20, kYB + 9, 20 + sp, kYB + 9);
    p.guide(20, kYB + 6, 20, kYB + 12);
    p.guide(20 + sp, kYB + 6, 20 + sp, kYB + 12);
}

// ---- Feel -----------------------------------------------------------------------------------------------------

void drawVelocity(const Pen& p, const tags::Values& v, float w) {
    float c = v[dk::kCurve] / 100, e = c >= 0 ? 1 / (1 + 2 * c) : 1 + 2 * -c, top = 40;
    auto yv = [&](float x) { return kYB - x * (kYB - top); };
    p.base(w);
    p.dots({Vec(12, yv(v[dk::kMax] / 100)), Vec(w - 12, yv(v[dk::kMax] / 100))}, 0.45f);
    p.dots({Vec(12, yv(v[dk::kMin] / 100)), Vec(w - 12, yv(v[dk::kMin] / 100))}, 0.45f);
    for (int k = 0; k < 7; k++) {
        float vin = float(k + 1) / 7, x = 26 + float(k) * (w - 52) / 6;
        float vo = v[dk::kMin] / 100 + (v[dk::kMax] - v[dk::kMin]) / 100 * std::pow(vin, e);
        float rb = v[dk::kRandom] / 100 * 12;
        p.guide(x - 5, kYB, x - 5, yv(vin), 1);
        p.circle(x - 5, yv(vin), 2.3f, Style::Ghost);
        if (rb > 1)
            p.box(x - 1, yv(vo) - rb, 10, 2 * rb, Style::Band, 1, 4);
        p.stroke(x + 4, kYB, x + 4, yv(vo));
        p.circle(x + 4, yv(vo), 3.4f, Style::Note);
    }
}

void drawHumanize(const Pen& p, const tags::Values& v, float w) {
    p.base(w);
    for (int k = 0; k < 6; k++) {
        float x = 16 + float(k) * (w - 40) / 5.6f, y = k % 2 ? 54.f : 46.f;
        float d = (kRnd[k] - 0.3f) * v[dk::kHumanizeTiming] / 50 * 16;
        float bw = 28 * (1 + (kRnd[k + 3] - 0.5f) * v[dk::kHumanizeLength] / 100 * 1.4f);
        p.tick(x, kYB - 4, x, kYB + 4);
        p.box(x, y - 2, 28, 13, Style::Ghost);
        p.box(x + d, y, bw, 9, Style::Note, 1 - kRnd[k + 6] * v[dk::kHumanizeVelocity] / 100 * 0.75f);
    }
}

void drawRandom(const Pen& p, const tags::Values& v, float w) {
    float cy = 54, r = v[dk::kRandomPitch] * 1.4f;
    if (r > 0)
        p.box(12, cy - r - 5, w - 24, 2 * r + 10, Style::Band, 1, 5);
    nvgBeginPath(p.vg);
    for (int k = 0; k < int((w - 24) / 6); k++) {
        float x = 12 + float(k) * 6;
        nvgMoveTo(p.vg, x, cy);
        nvgLineTo(p.vg, std::min(w - 12, x + 2), cy);
    }
    nvgStrokeColor(p.vg, p.c.tick);
    nvgStrokeWidth(p.vg, 1.3f);
    nvgStroke(p.vg);
    bool inKey = v.i(dk::kRandomInKey) != 0;
    for (int k = 0; k < 7; k++) {
        float x = 16 + float(k) * (w - 44) / 6.2f, dy = (kRnd[k] * 2 - 1) * r;
        if (inKey)
            dy = std::round(dy / 4.2f) * 4.2f;
        if (kRnd[k + 8] < v[dk::kDrop] / 100) {
            p.box(x, cy - 4, 24, 8, Style::GhostDash);
            p.cross(x + 12, cy);
            continue;
        }
        p.box(x, cy + dy - 4, 24, 8, Style::Note);
    }
}

void drawGroove(const Pen& p, const tags::Values& v, float w) {
    int pairs = v.i(dk::kGrooveGrid) == 0 ? 4 : 2;
    float pw = (w - 24) / float(pairs);
    p.base(w);
    for (int j = 0; j < pairs; j++) {
        float x = 12 + float(j) * pw, xs = x + pw / 2, xw = x + pw * v[dk::kGrooveSwing] / 100;
        float bw = std::min(14.f, pw / 2 - 6);
        p.box(x, kYB - 26, bw, 26, Style::Note);
        if (std::fabs(xw - xs) > 1)
            p.box(xs, kYB - 18, bw, 18, Style::GhostDash);
        p.box(xw, kYB - 18, bw, 18, Style::Note, 0.7f);
    }
}

void drawFilterRange(const Pen& p, const tags::Values& v, float w) {
    auto y = [](float m) { return kYB - 2 - (std::clamp(m, 40.f, 96.f) - 40) / 56 * 32; };
    float lo = v[dk::kLowest], hi = v[dk::kHighest];
    float ya = y(hi) - 3, yb = y(lo) + 5;
    if (yb > ya) {
        p.box(10, ya, w - 20, yb - ya, Style::Band, 1, 4);
        p.dots({Vec(10, ya), Vec(w - 10, ya)}, 0.5f);
        p.dots({Vec(10, yb), Vec(w - 10, yb)}, 0.5f);
    }
    const float pit[] = {50, 62, 57, 69, 74, 55, 81, 64, 60, 86, 67, 72};
    const float vel[] = {0.9f, 0.4f, 0.7f, 1, 0.25f, 0.6f, 0.8f, 0.5f, 0.95f, 0.35f, 0.75f, 0.55f};
    for (int i = 0; i < 12; i++) {
        float x = 16 + float(i) * (w - 40) / 11.4f, m = pit[i];
        bool ok = m >= lo && m <= hi && vel[i] * 100 >= v[dk::kMinVelocity] - 0.5f &&
                  vel[i] * 100 <= v[dk::kMaxVelocity] + 0.5f;
        if (ok)
            p.box(x, y(m) - 2, 16, 6, Style::Note, 0.35f + 0.65f * vel[i]);
        else
            p.box(x, y(m) - 2, 16, 6, Style::GhostDash);
    }
}

void drawFilterSkip(const Pen& p, const tags::Values& v, float w) {
    int every = std::max(1, v.i(dk::kEvery)), offset = v.i(dk::kOffset);
    float gap = (w - 28) / 8;
    for (int i = 0; i < 8; i++) {
        bool ok = (((i - offset) % every) + every) % every == 0;
        float cx = 14 + gap * (float(i) + 0.5f);
        if (ok)
            p.circle(cx, 48, 7.5f, Style::Note);
        else
            p.circle(cx, 48, 7, Style::GhostDash);
    }
    float cw = (w - 28) / 16;
    for (int ch = 0; ch < 16; ch++) {
        bool any = v.channels == 0, in = (v.channels >> ch) & 1;
        if (!any && !in)
            p.box(14 + float(ch) * cw + 1, 66, cw - 2, 6, Style::Key, 1, 2);
        else
            p.box(14 + float(ch) * cw + 1, 66, cw - 2, 6, Style::Note, any ? 0.35f : 1, 2);
    }
}

void drawMotionDrift(const Pen& p, const tags::Values& v, float w) {
    const float xs[] = {16, 96, 176}, ys[] = {54, 60, 48};
    float pr = std::clamp(v[dk::kMotionPitch], -12.f, 12.f) * 1.2f, tr = std::min(v[dk::kMotionTime], 8.f) * 2.4f;
    bool inKey = v.i(dk::kMotionInKey) != 0;
    for (int i = 0; i < 3; i++) {
        float a0 = kRnd[i + 2] * tr, a1 = kRnd[i] * pr, b0 = kRnd[i + 9] * tr, b1 = kRnd[i + 5] * -pr * 0.6f;
        if (inKey) {
            a1 = std::round(a1 / 3.5f) * 3.5f;
            b1 = std::round(b1 / 3.5f) * 3.5f;
        }
        p.box(xs[i], ys[i] - 2, 44, 13, Style::Ghost);
        p.box(xs[i] + b0, ys[i] - b1, 40, 9, Style::Note, 0.3f);
        p.box(xs[i] + a0, ys[i] - a1, 40, 9, Style::Note);
    }
    (void)w;
}

void drawMotionShape(const Pen& p, const tags::Values& v, float w) {
    p.base(w);
    for (int i = 0; i < 3; i++) {
        float x = 14 + float(i) * 72;
        float bw = std::clamp(46 * (1 + kRnd[i + 4] * v[dk::kMotionLength] / 100), 6.f, 70.f);
        float h = std::clamp(22 * (1 + kRnd[i + 1] * v[dk::kMotionVelocity] / 100), 4.f, 30.f);
        p.box(x, kYB - 22, 46, 22, Style::Ghost);
        p.box(x + 2, kYB - h, bw - 4, h - 2, Style::Note, 0.9f);
    }
}

} // namespace

void drawTile(NVGcontext* vg, DeviceType type, int tile, const tags::Values& v, float width, NVGcolor tint,
              const std::string& caption) {
    const theme::DeviceColors& c = theme::deviceColors();
    float h = theme::metrics::kDeviceTile, r = theme::metrics::kDeviceTileRadius;
    nvgSave(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, width, h, r);
    nvgFillColor(vg, c.screen);
    nvgFill(vg);
    // The screen is sunk a little: a soft shadow along its top.
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, width, h, r);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 0, 2.5f, c.shade, nvgTransRGBAf(c.shade, 0)));
    nvgFill(vg);
    nvgIntersectScissor(vg, 0, 0, width, h);

    Pen p{vg, tint, c};
    switch (type) {
    case DeviceType::Playheads:
        tile == 0 ? drawMoves(p, v, width) : tile == 1 ? drawStart(p, v, width) : drawRepeat(p, v, width);
        break;
    case DeviceType::Transpose:
        drawTranspose(p, v, width);
        break;
    case DeviceType::Scale:
        drawScale(p, v, width);
        break;
    case DeviceType::Chord:
        drawChord(p, v, width);
        break;
    case DeviceType::NoteLength:
        drawLength(p, v, width);
        break;
    case DeviceType::Arpeggiator:
        tile == 0 ? drawArpNotes(p, v, width) : drawArpSteps(p, v, width);
        break;
    case DeviceType::NoteRepeat:
        drawNoteRepeat(p, v, width);
        break;
    case DeviceType::Echo:
        drawEcho(p, v, width);
        break;
    case DeviceType::Velocity:
        drawVelocity(p, v, width);
        break;
    case DeviceType::Humanize:
        drawHumanize(p, v, width);
        break;
    case DeviceType::Random:
        drawRandom(p, v, width);
        break;
    case DeviceType::Groove:
        drawGroove(p, v, width);
        break;
    case DeviceType::NoteFilter:
        tile == 0 ? drawFilterRange(p, v, width) : drawFilterSkip(p, v, width);
        break;
    case DeviceType::Motion:
        tile == 0 ? drawMotionDrift(p, v, width) : drawMotionShape(p, v, width);
        break;
    }

    draw::TextStyle cap{theme::fonts::ui(), theme::metrics::kDeviceCaptionText, c.caption,
                        NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE};
    draw::text(vg, cap, 10, h - 8, draw::ellipsize(vg, cap, caption, width - 20));
    nvgRestore(vg);
}

float drawValue(NVGcontext* vg, float x, float cy, const std::string& label, float size, NVGcolor color, bool draw) {
    tags::ValueText t = tags::splitLabel(label);
    draw::TextStyle main{theme::fonts::uiStrong(), size, color, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle small{theme::fonts::uiStrong(), size * 0.76f, color, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle unit{theme::fonts::ui(), size * 0.78f, nvgTransRGBAf(color, color.a * 0.65f),
                         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    draw::TextStyle slash{theme::fonts::ui(), size * 1.05f, nvgTransRGBAf(color, color.a * 0.5f),
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    float at = x;
    auto put = [&](const draw::TextStyle& st, const std::string& s, float dy) {
        if (s.empty())
            return;
        if (draw)
            draw::text(vg, st, at, cy + dy, s);
        at += draw::textWidth(vg, st, s);
    };
    put(main, t.sign, 0);
    if (!t.num.empty()) {
        // A stacked fraction: the top a little up, the bottom a little down.
        put(small, t.num, -0.3f * size * 0.76f);
        at += 0.5f;
        put(slash, "/", 0);
        at += 0.5f;
        put(small, t.den, 0.22f * size * 0.76f);
    } else {
        put(main, t.text, 0);
    }
    if (!t.unit.empty()) {
        at += 2;
        put(unit, t.unit, 0.5f);
    }
    if (!t.mark.empty()) {
        at += 2;
        put(unit, t.mark, 0.5f);
    }
    return at - x;
}

void drawDirectionIcon(NVGcontext* vg, Rect r, int value, NVGcolor color) {
    // Drawn on a 16 × 12 grid, as the mockup's icons.
    float sx = r.size.x / 16, sy = r.size.y / 12;
    auto P = [&](float x, float y) { return Vec(r.pos.x + x * sx, r.pos.y + y * sy); };
    nvgBeginPath(vg);
    auto move = [&](float x, float y) { Vec q = P(x, y); nvgMoveTo(vg, q.x, q.y); };
    auto to = [&](float x, float y) { Vec q = P(x, y); nvgLineTo(vg, q.x, q.y); };
    switch (value) {
    case 0:
        move(2, 6), to(13, 6), move(9, 2.5f), to(13, 6), to(9, 9.5f);
        break;
    case 1:
        move(14, 6), to(3, 6), move(7, 2.5f), to(3, 6), to(7, 9.5f);
        break;
    case 2:
        move(3, 6), to(13, 6), move(6, 3), to(3, 6), to(6, 9), move(10, 3), to(13, 6), to(10, 9);
        break;
    case 3:
        move(5.5f, 2.5f), to(5.5f, 9.5f), move(10.5f, 2.5f), to(10.5f, 9.5f);
        break;
    default: {
        // A step order: three hops of different sizes.
        int o = value - 4;
        const float xs[4][4] = {{2, 14, 4, 12}, {8, 5, 11, 2}, {2, 8, 5, 11}, {6, 13, 2, 9}};
        move(xs[o][0], 9);
        for (int k = 1; k < 4; k++)
            to(xs[o][k], k % 2 ? 3.f : 9.f);
        break;
    }
    }
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 1.3f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
}

} // namespace tessera::tiles
