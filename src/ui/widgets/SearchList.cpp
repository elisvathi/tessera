#include "SearchList.hpp"

#include "../Theme.hpp"
#include "../interaction/Search.hpp"
#include "Draw.hpp"

#include <algorithm>
#include <cctype>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kWidth = 340;
constexpr float kPad = 8;
constexpr float kField = 28;
constexpr float kChip = 22;
constexpr float kRow = 40;
constexpr float kGroup = 20;
constexpr float kFooter = 24;
constexpr float kMaxList = 320;


class SearchListWidget;

/// The search box: our look, and the arrow keys, Enter and Esc go to the list.
class SearchField : public rack::ui::TextField {
  public:
    SearchListWidget* list = nullptr;
    void draw(const DrawArgs& args) override;
    void onSelectKey(const SelectKeyEvent& e) override;
    void onChange(const ChangeEvent& e) override;
};

class SearchListWidget : public rack::widget::OpaqueWidget {
  public:
    SearchListWidget(std::vector<SearchItem> items, SearchListOptions options)
        : items(std::move(items)), opts(std::move(options)) {
        field = new SearchField;
        field->list = this;
        field->placeholder = opts.placeholder;
        field->box = Rect(kPad, kPad, kWidth - 2 * kPad, kField);
        addChild(field);
        filter("");
    }

    SearchField* searchField() { return field; }

    void filter(const std::string& text) {
        rows.clear();
        shown.clear();
        float y = 0;
        std::string group;
        for (size_t i = 0; i < items.size(); i++) {
            const SearchItem& it = items[i];
            bool match = searchMatches(it.name, it.description, it.keywords, text);
            if (!match)
                continue;
            if (it.group != group || rows.empty()) {
                group = it.group;
                if (!group.empty()) {
                    rows.push_back({true, group, -1, y});
                    y += kGroup;
                }
            }
            rows.push_back({false, it.name, int(i), y});
            shown.push_back(int(i));
            y += kRow;
        }
        contentHeight = y;
        selected = 0;
        scroll = 0;
        relayout();
    }

    void move(int delta) {
        if (shown.empty())
            return;
        selected = std::clamp(selected + delta, 0, int(shown.size()) - 1);
        // Keep the chosen row in view.
        for (const Row& r : rows)
            if (!r.header && r.item == shown[size_t(selected)]) {
                if (r.y - scroll < 0)
                    scroll = r.y;
                if (r.y + kRow - scroll > listHeight)
                    scroll = r.y + kRow - listHeight;
            }
    }

    void pick() {
        if (shown.empty())
            return;
        std::function<void(int)> chosen = items[size_t(shown[size_t(selected)])].pick;
        int scope = opts.scope;
        close();
        if (chosen)
            chosen(scope);
    }

    void close() {
        if (auto* overlay = getAncestorOfType<rack::ui::MenuOverlay>())
            overlay->requestDelete();
    }

    void draw(const DrawArgs& args) override {
        NVGcontext* vg = args.vg;
        const theme::Palette& c = theme::colors();
        // A soft shadow, then the panel.
        nvgBeginPath(vg);
        nvgRect(vg, -10, -6, box.size.x + 20, box.size.y + 20);
        nvgFillPaint(
            vg, nvgBoxGradient(vg, 0, 4, box.size.x, box.size.y, 8, 14, nvgRGBAf(0, 0, 0, 0.5f), nvgRGBAf(0, 0, 0, 0)));
        nvgFill(vg);
        draw::roundedRect(vg, Rect(Vec(), box.size), 8, c.bg1);
        draw::roundedRectStroke(vg, Rect(Vec(), box.size), 8, c.line1, 1);

        draw::TextStyle chipText{theme::fonts::ui(), theme::metrics::kTextUi, c.text1,
                                 NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        for (size_t i = 0; i < chips.size(); i++) {
            bool on = int(i) == opts.scope;
            draw::roundedRect(vg, chips[i], 4, on ? nvgTransRGBAf(c.accent, 0.25f) : c.bg2);
            chipText.color = on ? c.accent : c.text1;
            draw::text(vg, chipText, chips[i].getCenter().x, chips[i].getCenter().y, opts.scopes[i]);
        }

        nvgSave(vg);
        nvgScissor(vg, 0, listTop, box.size.x, listHeight);
        draw::TextStyle head{theme::fonts::uiStrong(), 9.5f, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                             theme::metrics::kPanelLabelSpacing};
        draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextHeader, c.text0,
                             NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE};
        draw::TextStyle what{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE};
        draw::TextStyle badge{theme::fonts::uiStrong(), theme::metrics::kTextUi, c.text1,
                              NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        for (const Row& r : rows) {
            float y = listTop + r.y - scroll;
            if (y > listTop + listHeight || y + kRow < listTop)
                continue;
            if (r.header) {
                std::string upper = r.text;
                for (char& ch : upper)
                    ch = char(std::toupper(static_cast<unsigned char>(ch)));
                draw::text(vg, head, kPad + 4, y + kGroup / 2 + 2, upper);
                continue;
            }
            bool on = !shown.empty() && shown[size_t(selected)] == r.item;
            Rect row(kPad / 2, y, box.size.x - kPad, kRow);
            if (on)
                draw::roundedRect(vg, row, 5, c.line0);
            Rect icon(row.pos.x + 6, y + 7, 26, 26);
            draw::roundedRect(vg, icon, 5, c.bg2);
            badge.color = on ? c.accent : c.text1;
            draw::text(vg, badge, icon.getCenter().x, icon.getCenter().y, r.text.substr(0, 2));
            const SearchItem& it = items[size_t(r.item)];
            draw::text(vg, name, icon.getRight() + 9, y + 17, it.name);
            draw::text(vg, what, icon.getRight() + 9, y + 32,
                       draw::ellipsize(vg, what, it.description, row.getRight() - icon.getRight() - 60));
            if (on) {
                draw::TextStyle enter{theme::fonts::ui(), 10, c.text2, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE};
                draw::text(vg, enter, row.getRight() - 8, y + kRow / 2, "Enter");
            }
        }
        if (shown.empty()) {
            draw::TextStyle none{theme::fonts::ui(), theme::metrics::kTextUi, c.text1, NVG_ALIGN_LEFT | NVG_ALIGN_TOP};
            draw::text(vg, none, kPad + 4, listTop + 10, "Nothing matches “" + field->getText() + "”");
        }
        nvgRestore(vg);

        float fy = box.size.y - kFooter;
        nvgBeginPath(vg);
        nvgRect(vg, 0, fy, box.size.x, 1);
        nvgFillColor(vg, c.line0);
        nvgFill(vg);
        draw::TextStyle hint{theme::fonts::ui(), 10, c.text2, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
        draw::text(vg, hint, kPad + 2, fy + kFooter / 2, "↑ ↓ choose   Enter add   Esc close");
        hint.align = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;
        draw::text(vg, hint, box.size.x - kPad - 2, fy + kFooter / 2, opts.footer);

        OpaqueWidget::draw(args);
    }

    void onButton(const ButtonEvent& e) override {
        OpaqueWidget::onButton(e);
        if (e.isConsumed() && e.getTarget() != this)
            return;
        e.consume(this);
        if (e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        for (size_t i = 0; i < chips.size(); i++)
            if (chips[i].contains(e.pos)) {
                opts.scope = int(i);
                return;
            }
        if (itemAt(e.pos) >= 0)
            pick();
    }
    void onHover(const HoverEvent& e) override {
        OpaqueWidget::onHover(e);
        int at = itemAt(e.pos);
        if (at >= 0)
            selected = at;
    }
    void onHoverScroll(const HoverScrollEvent& e) override {
        scroll = std::clamp(scroll - e.scrollDelta.y, 0.f, std::max(0.f, contentHeight - listHeight));
        e.consume(this);
    }

  private:
    struct Row {
        bool header;
        std::string text;
        int item;
        float y;
    };

    void relayout() {
        chips.clear();
        float x = kPad, y = kPad + kField + 6;
        for (const std::string& s : opts.scopes) {
            float w = std::min(kWidth - 2 * kPad, 16 + 6.5f * float(s.size()));
            if (x + w > kWidth - kPad && x > kPad) { // wrap onto the next line
                x = kPad;
                y += kChip + 4;
            }
            chips.push_back(Rect(x, y, w, kChip));
            x += w + 4;
        }
        listTop = chips.empty() ? kPad + kField + 6 : chips.back().getBottom() + 6;
        listHeight = std::max(kRow, std::min(kMaxList, contentHeight));
        box.size = Vec(kWidth, listTop + listHeight + 4 + kFooter);
    }

    /// The position in `shown` of the item under `pos`, or -1.
    int itemAt(Vec pos) const {
        if (pos.y < listTop || pos.y > listTop + listHeight)
            return -1;
        float y = pos.y - listTop + scroll;
        for (const Row& r : rows)
            if (!r.header && y >= r.y && y < r.y + kRow)
                for (size_t k = 0; k < shown.size(); k++)
                    if (shown[k] == r.item)
                        return int(k);
        return -1;
    }

    std::vector<SearchItem> items;
    SearchListOptions opts;
    SearchField* field;
    std::vector<Row> rows;
    std::vector<int> shown; // item indices that match, in order
    std::vector<Rect> chips;
    int selected = 0;
    float scroll = 0, contentHeight = 0, listTop = 0, listHeight = 0;
};

void SearchField::draw(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    Rect r(Vec(), box.size);
    draw::roundedRect(vg, r, 5, c.bg0);
    draw::roundedRectStroke(vg, r, 5, c.accent, 1);
    // A magnifier.
    nvgBeginPath(vg);
    nvgCircle(vg, 13, box.size.y / 2 - 1, 4.5f);
    nvgMoveTo(vg, 16.5f, box.size.y / 2 + 2.5f);
    nvgLineTo(vg, 20, box.size.y / 2 + 6);
    nvgStrokeColor(vg, c.text1);
    nvgStrokeWidth(vg, 1.4f);
    nvgStroke(vg);
    draw::TextStyle style{theme::fonts::ui(), theme::metrics::kTextHeader, c.text0, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    float x = 27;
    if (text.empty()) {
        style.color = c.text2;
        draw::text(vg, style, x, box.size.y / 2, placeholder);
    } else {
        draw::text(vg, style, x, box.size.y / 2, text);
    }
    // The caret, blinking.
    if (std::fmod(rack::system::getTime(), 1.0) < 0.6) {
        float cx = x + draw::textWidth(vg, style, text.substr(0, size_t(std::clamp(cursor, 0, int(text.size())))));
        nvgBeginPath(vg);
        nvgRect(vg, cx, 7, 1, box.size.y - 14);
        nvgFillColor(vg, c.text0);
        nvgFill(vg);
    }
}

void SearchField::onSelectKey(const SelectKeyEvent& e) {
    if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
        switch (e.key) {
        case GLFW_KEY_UP:
            list->move(-1);
            e.consume(this);
            return;
        case GLFW_KEY_DOWN:
            list->move(+1);
            e.consume(this);
            return;
        case GLFW_KEY_ENTER:
        case GLFW_KEY_KP_ENTER:
            list->pick();
            e.consume(this);
            return;
        case GLFW_KEY_ESCAPE:
            list->close();
            e.consume(this);
            return;
        default:
            break;
        }
    }
    TextField::onSelectKey(e);
}

void SearchField::onChange(const ChangeEvent& e) { list->filter(getText()); }

} // namespace

void openSearchList(std::vector<SearchItem> items, SearchListOptions options) {
    auto* overlay = new rack::ui::MenuOverlay;
    APP->scene->addChild(overlay);
    auto* list = new SearchListWidget(std::move(items), std::move(options));
    Vec pos = APP->scene->mousePos;
    Vec limit = APP->scene->box.size.minus(list->box.size).minus(Vec(8, 8));
    list->box.pos = Vec(std::clamp(pos.x, 8.f, std::max(8.f, limit.x)), std::clamp(pos.y, 8.f, std::max(8.f, limit.y)));
    overlay->addChild(list);
    APP->event->setSelectedWidget(list->searchField());
}

} // namespace tessera
