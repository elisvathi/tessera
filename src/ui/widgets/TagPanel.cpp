#include "TagPanel.hpp"

#include "../../model/Playheads.hpp"
#include "../Theme.hpp"
#include "../views/DeviceTiles.hpp"
#include "Draw.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;
using tags::Family;

namespace {

constexpr float kPad = 10;
constexpr float kTitle = 16;
constexpr float kField = 30;
constexpr float kHint = 96;
constexpr float kChip = 30;
constexpr float kTileChip = 54;
constexpr float kRowGap = 4;
constexpr float kSection = 12;
constexpr float kHead = 58;
constexpr float kFoot = 28;
constexpr float kLinks = 22;
constexpr float kSign = 200;

const char* const kMinus = "−";

/// How wide each kind of panel is, from the mockup.
float widthOf(const TagPanelSpec& s) {
    if (s.channels)
        return 380;
    switch (s.list.family) {
    case Family::Length:
        return 540;
    case Family::Rate:
        return 520;
    case Family::Curve:
        return 420;
    case Family::Direction:
        return 360;
    default:
        return 400;
    }
}

std::string placeholderOf(const TagPanelSpec& s) {
    if (s.channels)
        return "Type channels: 1-4, 10 or any";
    switch (s.list.family) {
    case Family::Length:
        return "Type a length: 1/8t, 3/16, 1 bar";
    case Family::Rate:
        return "Type a speed: 2, 1.5, 3/4";
    case Family::Curve:
        return "Type a shape: out, swing";
    case Family::Direction:
        return "Type: backward, freeze, shuffle";
    case Family::Number:
        return "Type a number";
    default:
        return "Type to find a choice";
    }
}

/// Removes a leading sign ("+1/8" → "1/8", "−1/8" → "1/8").
std::string unsigned_(const std::string& s) {
    if (!s.empty() && s[0] == '+')
        return s.substr(1);
    if (s.rfind(kMinus, 0) == 0)
        return s.substr(std::string(kMinus).size());
    return s;
}

bool endsWith(const std::string& s, const std::string& end) {
    return s.size() >= end.size() && s.compare(s.size() - end.size(), end.size(), end) == 0;
}

class TagPanelWidget;
TagPanelWidget* openPanel = nullptr;

/// The type box: our look; Enter, Esc and the arrow keys go to the panel.
class PanelField : public rack::ui::TextField {
  public:
    TagPanelWidget* panel = nullptr;
    NVGcolor tint;
    void draw(const DrawArgs& args) override;
    void onSelectKey(const SelectKeyEvent& e) override;
    void onChange(const ChangeEvent& e) override;
};

class TagPanelWidget : public rack::widget::OpaqueWidget {
  public:
    explicit TagPanelWidget(TagPanelSpec s) : spec(std::move(s)) {
        openPanel = this;
        sign = spec.list.zero >= 0 && spec.current < spec.list.zero ? -1 : 1;
        mask = spec.mask;
        field = new PanelField;
        field->panel = this;
        field->tint = spec.tint;
        field->placeholder = placeholderOf(spec);
        addChild(field);
        layout();
    }
    ~TagPanelWidget() override {
        if (openPanel == this)
            openPanel = nullptr;
        if (!picked && !spec.channels && spec.preview)
            spec.preview(-1);
    }

    PanelField* textField() { return field; }

    void close() {
        if (auto* overlay = getAncestorOfType<rack::ui::MenuOverlay>())
            overlay->requestDelete();
    }

    void choose(int index) {
        if (index < 0 || index >= spec.list.size())
            return;
        picked = true;
        if (spec.pick)
            spec.pick(index);
        close();
    }

    void typed(const std::string& text) {
        query = text;
        if (spec.channels) {
            auto parsed = tags::parseChannels(text);
            if (parsed) {
                mask = *parsed;
                if (spec.previewMask)
                    spec.previewMask(mask);
            }
            hint = text.empty() ? "" : parsed ? (mask ? "Channels " + tags::channelsText(mask) : "Any channel") : "Try 1-4, 10";
            hintColor = parsed ? spec.tint : theme::colors().record;
            return;
        }
        auto m = tags::match(spec.list, text);
        if (m && spec.list.zero >= 0 && m->index != spec.list.zero)
            sign = m->index < spec.list.zero ? -1 : 1;
        setPreview(m ? m->index : -1);
        const theme::Palette& c = theme::colors();
        hint = text.empty() ? "" : !m ? "Nothing matches" : m->exact ? "Enter picks it" : "Closest: " + spec.list.labels[size_t(m->index)];
        hintColor = !m ? c.record : m->exact ? spec.tint : c.queue;
        layout();
    }

    void enter() {
        if (spec.channels) {
            picked = true;
            if (spec.pickMask && mask != spec.mask)
                spec.pickMask(mask);
            close();
            return;
        }
        if (preview >= 0)
            choose(preview);
    }

    void stepBy(int delta) {
        if (spec.channels || spec.list.values.empty())
            return;
        int from = preview >= 0 ? preview : spec.current;
        setPreview(std::clamp(from + delta, 0, spec.list.size() - 1));
        if (spec.list.zero >= 0 && preview != spec.list.zero)
            sign = preview < spec.list.zero ? -1 : 1;
        layout();
    }

    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        const theme::Palette& c = theme::colors();
        Rect all(Vec(), box.size);
        nvgBeginPath(vg);
        nvgRect(vg, -14, -8, box.size.x + 28, box.size.y + 30);
        nvgFillPaint(vg, nvgBoxGradient(vg, 0, 8, box.size.x, box.size.y, 12, 22, nvgRGBAf(0, 0, 0, c.dark ? 0.55f : 0.25f),
                                        nvgRGBAf(0, 0, 0, 0)));
        nvgFill(vg);
        NVGcolor face = nvgLerpRGBA(c.bg1, c.bg2, 0.4f);
        draw::roundedRect(vg, all, 12, face);
        draw::roundedRectStroke(vg, all, 12, c.line2, 1);

        // Title: a dot in the card's colour and the setting's name.
        nvgBeginPath(vg);
        nvgCircle(vg, kPad + 6, kPad + kTitle / 2, 4);
        nvgFillColor(vg, spec.tint);
        nvgFill(vg);
        draw::TextStyle title{theme::fonts::uiStrong(), 12, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, title, kPad + 15, kPad + kTitle / 2, spec.title);
        draw::TextStyle hintStyle{theme::fonts::ui(), 11, hintColor, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
        draw::text(vg, hintStyle, box.size.x - kPad, field->box.getCenter().y,
                   draw::ellipsize(vg, hintStyle, hint, kHint - 6));

        draw::TextStyle head{theme::fonts::ui(), 11, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        for (const Head& h : heads)
            draw::text(vg, head, h.pos.x, h.pos.y, h.text);
        for (size_t i = 0; i < chips.size(); i++)
            drawChip(vg, chips[i], int(i) == hovered);

        // Footer: back to the default, then the setting's other actions.
        float fy = box.size.y - kFoot - (spec.links.empty() ? 0 : kLinks);
        nvgBeginPath(vg);
        nvgRect(vg, kPad, fy, box.size.x - 2 * kPad, 1);
        nvgFillColor(vg, c.line1);
        nvgFill(vg);
        draw::TextStyle link{theme::fonts::ui(), 12, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        float lx = kPad; // the links after "Back to", measured as they are drawn
        for (size_t k = 0; k < footLinks.size(); k++) {
            link.color = int(k) == hoveredLink ? c.text0 : c.text1;
            if (footLinks[k].action >= 0) {
                footLinks[k].rect.pos.x = lx;
                lx += draw::textWidth(vg, link, footLinks[k].text) + 18;
            }
            draw::text(vg, link, footLinks[k].rect.pos.x, footLinks[k].rect.getCenter().y, footLinks[k].text);
            float w = draw::textWidth(vg, link, footLinks[k].text);
            footLinks[k].rect.size.x = w;
            nvgBeginPath(vg);
            nvgRect(vg, footLinks[k].rect.pos.x, footLinks[k].rect.getCenter().y + 7, w, 1);
            nvgFillColor(vg, int(k) == hoveredLink ? c.text1 : c.line2);
            nvgFill(vg);
        }
        draw::TextStyle keys{theme::fonts::ui(), 11, c.text2, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
        draw::text(vg, keys, box.size.x - kPad, fy + kFoot / 2 + 1, spec.channels ? "Enter or a click outside keeps it" : "Enter picks, Esc closes");
        OpaqueWidget::draw(args);
    }

    void onButton(const ButtonEvent& e) override {
        OpaqueWidget::onButton(e);
        if (e.isConsumed() && e.getTarget() != this)
            return;
        e.consume(this);
        if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        for (size_t k = 0; k < footLinks.size(); k++)
            if (footLinks[k].rect.grow(Vec(2, 3)).contains(e.pos)) {
                activate(footLinks[k]);
                return;
            }
        int at = chipAt(e.pos);
        if (at < 0)
            return;
        const Chip& ch = chips[size_t(at)];
        switch (ch.kind) {
        case Chip::Value:
            choose(ch.index);
            break;
        case Chip::Step:
            picked = true;
            if (spec.pickStep)
                spec.pickStep(ch.index);
            if (spec.preview)
                spec.preview(-1);
            close();
            break;
        case Chip::Sign:
            sign = ch.index;
            layout();
            break;
        case Chip::Any:
            mask = 0;
            if (spec.pickMask)
                spec.pickMask(mask);
            spec.mask = mask;
            layout();
            break;
        case Chip::Channel:
            mask = uint16_t(mask ^ (1u << ch.index));
            if (spec.pickMask)
                spec.pickMask(mask);
            spec.mask = mask;
            layout();
            break;
        }
    }

    void onHover(const HoverEvent& e) override {
        OpaqueWidget::onHover(e);
        hovered = chipAt(e.pos);
        hoveredLink = -1;
        for (size_t k = 0; k < footLinks.size(); k++)
            if (footLinks[k].rect.grow(Vec(2, 3)).contains(e.pos))
                hoveredLink = int(k);
        if (!query.empty() || spec.channels)
            return;
        int value = hovered >= 0 && chips[size_t(hovered)].kind == Chip::Value ? chips[size_t(hovered)].index : -1;
        setPreview(value);
    }
    void onLeave(const LeaveEvent& e) override {
        OpaqueWidget::onLeave(e);
        hovered = -1;
        hoveredLink = -1;
        if (query.empty() && !spec.channels)
            setPreview(-1);
    }

  private:
    struct Chip {
        enum Kind { Value, Step, Sign, Any, Channel } kind;
        int index;
        Rect rect;
        bool tile = false; // tall, with a drawing: curves and directions
        std::string label;
        bool enabled = true;
    };
    struct Head {
        Vec pos;
        std::string text;
    };
    struct FootLink {
        std::string text;
        Rect rect;
        int action; // -1: back to the default; else a link in spec.links
    };

    void setPreview(int index) {
        if (index == preview)
            return;
        preview = index;
        if (spec.preview)
            spec.preview(index);
    }

    void activate(const FootLink& l) {
        if (l.action < 0) {
            if (spec.channels) {
                picked = true;
                if (spec.pickMask)
                    spec.pickMask(0);
                close();
            } else {
                choose(spec.list.def);
            }
            return;
        }
        std::function<void()> run = spec.links[size_t(l.action)].second;
        close();
        if (run)
            run();
    }

    int chipAt(Vec pos) const {
        for (size_t i = 0; i < chips.size(); i++)
            if (chips[i].enabled && chips[i].rect.contains(pos))
                return int(i);
        return -1;
    }

    /// One row of chips in `cols` equal columns, after an optional head word; empty labels leave a gap.
    void row(float& y, const char* headText, const std::vector<Chip>& rowChips, int cols, bool section, float h = kChip) {
        y += section ? kSection : kRowGap;
        float x0 = kPad, w = box.size.x - 2 * kPad;
        if (headText) {
            heads.push_back({Vec(x0, y + h / 2), headText});
            x0 += kHead;
            w -= kHead;
        }
        float cw = (w - float(cols - 1) * kRowGap) / float(std::max(1, cols));
        for (size_t k = 0; k < rowChips.size(); k++) {
            if (rowChips[k].index < -1000)
                continue; // a gap
            Chip ch = rowChips[k];
            ch.rect = Rect(x0 + float(k) * (cw + kRowGap), y, cw, h);
            chips.push_back(ch);
        }
        y += h;
    }

    Chip value(int index) const {
        if (index < 0)
            return gap();
        return {Chip::Value, index, Rect(), false, spec.list.labels[size_t(index)]};
    }
    static Chip gap() { return {Chip::Value, -2000, Rect(), false, ""}; }

    void layout() {
        chips.clear();
        heads.clear();
        float w = widthOf(spec);
        box.size.x = w;
        float y = kPad + kTitle + 8;
        field->box = Rect(kPad, y, w - 2 * kPad - kHint, kField);
        y += kField;
        const tags::ValueList& L = spec.list;
        auto idx = [&](const std::string& label) {
            auto it = std::find(L.labels.begin(), L.labels.end(), label);
            return it == L.labels.end() ? -1 : int(it - L.labels.begin());
        };
        if (spec.channels) {
            row(y, nullptr, {{Chip::Any, 0, Rect(), false, "Any"}}, 4, true);
            std::vector<Chip> a, b;
            for (int k = 0; k < 16; k++)
                (k < 8 ? a : b).push_back({Chip::Channel, k, Rect(), false, std::to_string(k + 1)});
            row(y, nullptr, a, 8, true);
            row(y, nullptr, b, 8, false);
        } else {
            switch (L.family) {
            case Family::Length: {
                bool signedList = L.zero >= 0;
                std::string pre = signedList ? (sign < 0 ? kMinus : "+") : "";
                std::vector<std::string> cols, other;
                bool hasOff = !L.labels.empty() && L.labels[0] == "Off";
                for (const std::string& l : L.labels) {
                    std::string b = unsigned_(l);
                    if (signedList && l.rfind(pre, 0) != 0)
                        continue;
                    if (std::find(cols.begin(), cols.end(), b) != cols.end() ||
                        std::find(other.begin(), other.end(), b) != other.end())
                        continue;
                    bool whole = b.rfind("1/", 0) == 0 && b.find(' ') == std::string::npos;
                    if (whole || endsWith(b, "bar") || endsWith(b, "bars"))
                        cols.push_back(b);
                    else if (!endsWith(b, " T") && !endsWith(b, " D") && b != "Off" && b != "0")
                        other.push_back(b);
                }
                if (signedList) {
                    y += kSection;
                    float sw = kSign / 2;
                    chips.push_back({Chip::Sign, -1, Rect(kPad, y, sw, 26), false, "Earlier"});
                    chips.push_back({Chip::Sign, 1, Rect(kPad + sw, y, sw, 26), false, "Later"});
                    y += 26;
                }
                int n = std::max(1, int(cols.size()));
                if (hasOff)
                    row(y, "None", {value(0)}, n, true);
                const char* names[] = {"Straight", "Triplet", "Dotted"};
                const char* marks[] = {"", " T", " D"};
                for (int r = 0; r < 3; r++) {
                    std::vector<Chip> rc;
                    bool any = false;
                    for (const std::string& col : cols) {
                        bool fraction = col.find('/') != std::string::npos;
                        int at = r == 0 || fraction ? idx(pre + col + marks[r]) : -1;
                        any = any || at >= 0;
                        rc.push_back(value(at));
                    }
                    if (any)
                        row(y, names[r], rc, n, r == 0 && !hasOff);
                }
                if (!other.empty()) {
                    std::vector<Chip> rc;
                    for (const std::string& o : other)
                        rc.push_back(value(idx(pre + o)));
                    row(y, signedList ? "Between" : "Other", rc, std::max(int(other.size()), n), true);
                }
                break;
            }
            case Family::Rate: {
                int z = std::max(0, idx("1x"));
                std::vector<Chip> faster, slower;
                for (int k = z + 1; k < L.size(); k++)
                    faster.push_back(value(k));
                for (int k = z - 1; k >= 0; k--)
                    slower.push_back(value(k));
                int n = std::max({int(faster.size()), int(slower.size()), 1});
                row(y, "Faster", faster, n, true);
                row(y, "Normal", {value(z)}, n, false);
                row(y, "Slower", slower, n, false);
                break;
            }
            case Family::Curve: {
                auto tile = [&](int k, const char* name) {
                    Chip ch = value(k < L.size() ? k : -1);
                    ch.tile = true;
                    ch.label = name;
                    return ch;
                };
                row(y, "Ease in", {tile(1, "Soft"), tile(2, "Medium"), tile(3, "Hard"), gap(), gap()}, 5, true, kTileChip);
                row(y, "Ease out", {tile(4, "Soft"), tile(5, "Medium"), tile(6, "Hard"), gap(), gap()}, 5, false,
                    kTileChip);
                row(y, "Others",
                    {tile(0, "Straight"), tile(7, "In-out"), tile(8, "Swing light"), tile(9, "Swing heavy"),
                     tile(10, "Stairs")},
                    5, false, kTileChip);
                break;
            }
            case Family::Direction: {
                std::vector<Chip> dirs, orders, steps;
                for (int k = 0; k < 4; k++) {
                    Chip a = value(k), b = value(4 + k);
                    a.tile = b.tile = true;
                    dirs.push_back(a);
                    orders.push_back(b);
                }
                row(y, nullptr, dirs, 4, true, kTileChip);
                y += kSection;
                heads.push_back({Vec(kPad, y + 6), "Steps in another order"});
                y += 8;
                row(y, nullptr, orders, 4, false, kTileChip);
                bool ordered = (preview >= 0 ? preview : spec.current) >= 4;
                for (int k = 0; k < int(spec.steps.size()); k++) {
                    Chip s{Chip::Step, k, Rect(), false, spec.steps[size_t(k)]};
                    s.enabled = ordered;
                    steps.push_back(s);
                }
                // Two rows of five, so "1/16 T" stays readable.
                for (size_t a = 0; a < steps.size(); a += 5) {
                    std::vector<Chip> part(steps.begin() + long(a), steps.begin() + long(std::min(steps.size(), a + 5)));
                    row(y, a == 0 ? "Step size" : "", part, 5, a == 0);
                }
                break;
            }
            case Family::Number: {
                int n = L.size();
                std::vector<Chip> rc;
                if (n <= 17) {
                    for (int k = 0; k < n; k++)
                        rc.push_back(value(k));
                    // Long lists wrap onto rows of nine.
                    for (size_t a = 0; a < rc.size(); a += 9) {
                        std::vector<Chip> part(rc.begin() + long(a), rc.begin() + long(std::min(rc.size(), a + 9)));
                        row(y, nullptr, part, std::min(9, n), a == 0);
                    }
                } else {
                    for (int j = 0; j <= 8; j++) {
                        int at = int(std::lround(double(j) * (n - 1) / 8));
                        if (rc.empty() || rc.back().index != at)
                            rc.push_back(value(at));
                    }
                    row(y, nullptr, rc, int(rc.size()), true);
                }
                break;
            }
            default: {
                int n = L.size(), cols = n > 12 ? 5 : n > 6 ? 4 : 3;
                std::vector<Chip> rc;
                for (int k = 0; k < n; k++)
                    rc.push_back(value(k));
                for (size_t a = 0; a < rc.size(); a += size_t(cols)) {
                    std::vector<Chip> part(rc.begin() + long(a), rc.begin() + long(std::min(rc.size(), a + size_t(cols))));
                    row(y, nullptr, part, cols, a == 0);
                }
                break;
            }
            }
        }
        y += 10;
        footLinks.clear();
        std::string back = spec.channels ? "any channel" : spec.list.labels.empty() ? "" : spec.list.labels[size_t(spec.list.def)];
        footLinks.push_back({"Back to " + back, Rect(kPad, y + 6, 120, 16), -1});
        y += kFoot;
        for (size_t k = 0; k < spec.links.size(); k++)
            footLinks.push_back({spec.links[k].first, Rect(kPad, y + 2, 0, 16), int(k)});
        if (!spec.links.empty())
            y += kLinks;
        box.size.y = y + 2;
    }

    void drawChip(NVGcontext* vg, const Chip& ch, bool hot) const {
        const theme::Palette& c = theme::colors();
        bool current = false, lit = false;
        switch (ch.kind) {
        case Chip::Value:
            current = ch.index == spec.current;
            lit = ch.index == preview;
            break;
        case Chip::Step:
            current = ch.index == spec.step && spec.current >= 4;
            break;
        case Chip::Sign:
            current = ch.index == sign;
            break;
        case Chip::Any:
            current = mask == 0;
            break;
        case Chip::Channel:
            current = (mask >> ch.index) & 1;
            break;
        }
        NVGcolor fill = current ? nvgTransRGBAf(spec.tint, 0.2f) : hot || lit ? nvgLerpRGBA(c.bg2, c.line2, 0.4f) : c.bg2;
        float radius = ch.kind == Chip::Sign ? 6 : 7;
        draw::roundedRect(vg, ch.rect, radius, fill);
        if (current || hot || lit)
            draw::roundedRectStroke(vg, ch.rect, radius, current ? spec.tint : c.line2, 1);
        nvgSave(vg);
        if (!ch.enabled)
            nvgGlobalAlpha(vg, 0.35f);
        Vec mid = ch.rect.getCenter();
        if (ch.tile) {
            NVGcolor ink = spec.tint;
            Rect icon(mid.x - 18, ch.rect.pos.y + 9, 36, 20);
            if (spec.list.family == Family::Curve) {
                auto [bend, strength] = playheads::curveShape(ch.index);
                nvgBeginPath(vg);
                for (int k = 0; k <= 40; k++) {
                    double t = double(k) / 40;
                    float x = icon.pos.x + 1 + float(t) * (icon.size.x - 2);
                    float yy = icon.getBottom() - float(playheads::warp(bend, strength, t)) * icon.size.y;
                    k == 0 ? nvgMoveTo(vg, x, yy) : nvgLineTo(vg, x, yy);
                }
                nvgStrokeColor(vg, ink);
                nvgStrokeWidth(vg, 1.6f);
                nvgLineCap(vg, NVG_ROUND);
                nvgLineJoin(vg, NVG_ROUND);
                nvgStroke(vg);
            } else {
                tiles::drawDirectionIcon(vg, Rect(mid.x - 12, ch.rect.pos.y + 10, 24, 18), ch.index, ink);
            }
            draw::TextStyle name{theme::fonts::ui(), 10.5f, c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            draw::text(vg, name, mid.x, ch.rect.getBottom() - 11, draw::ellipsize(vg, name, ch.label, ch.rect.size.x - 4));
        } else if (ch.kind == Chip::Sign || ch.kind == Chip::Any) {
            draw::TextStyle t{theme::fonts::ui(), 12, current ? c.text0 : c.text1, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
            draw::text(vg, t, mid.x, mid.y, ch.label);
        } else {
            float wv = tiles::drawValue(vg, 0, 0, ch.label, 12, c.text0, false);
            nvgSave(vg);
            nvgIntersectScissor(vg, ch.rect.pos.x + 2, ch.rect.pos.y, ch.rect.size.x - 4, ch.rect.size.y);
            tiles::drawValue(vg, mid.x - std::min(wv, ch.rect.size.x - 6) / 2, mid.y, ch.label, 12, c.text0);
            nvgRestore(vg);
        }
        nvgRestore(vg);
    }

    TagPanelSpec spec;
    PanelField* field;
    std::vector<Chip> chips;
    std::vector<Head> heads;
    std::vector<FootLink> footLinks;
    std::string query, hint;
    NVGcolor hintColor = nvgRGBAf(0, 0, 0, 0);
    int preview = -1;
    int sign = 1;
    uint16_t mask = 0;
    int hovered = -1, hoveredLink = -1;
    bool picked = false;
};

void PanelField::draw(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    Rect r(Vec(), box.size);
    draw::roundedRect(vg, r, 8, c.bg0);
    draw::roundedRectStroke(vg, r, 8, tint, 1);
    draw::TextStyle style{theme::fonts::ui(), 13, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    float x = 10;
    if (text.empty()) {
        style.color = c.text2;
        draw::text(vg, style, x, box.size.y / 2, placeholder);
    } else {
        draw::text(vg, style, x, box.size.y / 2, text);
    }
    if (std::fmod(rack::system::getTime(), 1.0) < 0.6) {
        float cx = x + draw::textWidth(vg, style, text.substr(0, size_t(std::clamp(cursor, 0, int(text.size())))));
        nvgBeginPath(vg);
        nvgRect(vg, cx, 7, 1, box.size.y - 14);
        nvgFillColor(vg, c.text0);
        nvgFill(vg);
    }
}

void PanelField::onSelectKey(const SelectKeyEvent& e) {
    if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
        switch (e.key) {
        case GLFW_KEY_UP:
            panel->stepBy(+1);
            e.consume(this);
            return;
        case GLFW_KEY_DOWN:
            panel->stepBy(-1);
            e.consume(this);
            return;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            panel->enter();
            e.consume(this);
            return;
        case GLFW_KEY_ESCAPE:
            panel->close();
            e.consume(this);
            return;
        default:
            break;
        }
    }
    TextField::onSelectKey(e);
}

void PanelField::onChange(const ChangeEvent& e) { panel->typed(getText()); }

} // namespace

bool tagPanelOpen() { return openPanel != nullptr; }

void openTagPanel(TagPanelSpec spec) {
    if (openPanel)
        openPanel->close();
    auto* overlay = new rack::ui::MenuOverlay;
    APP->scene->addChild(overlay);
    float below = spec.below, centerX = spec.centerX;
    auto* panel = new TagPanelWidget(std::move(spec));
    Vec mouse = APP->scene->mousePos;
    Vec size = APP->scene->box.size;
    float cx = centerX >= 0 ? centerX : mouse.x;
    float x = std::clamp(cx - panel->box.size.x / 2, 8.f, std::max(8.f, size.x - panel->box.size.x - 8));
    float y = below >= 0 ? below + 8 : mouse.y + 18;
    if (y + panel->box.size.y > size.y - 8)
        y = std::max(8.f, mouse.y - 18 - panel->box.size.y);
    panel->box.pos = Vec(x, y);
    overlay->addChild(panel);
    APP->event->setSelectedWidget(panel->textField());
}

} // namespace tessera
