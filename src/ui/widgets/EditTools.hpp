#pragma once

#include "../EditorContext.hpp"
#include "ToolButton.hpp"

#include <functional>
#include <vector>

namespace tessera {

/// One button per editing tool (EDITOR_UX §4.1), for the piano roll and arranger toolbars. Each is
/// `size` square; the caller adds and places them.
///
/// Clicking Erase calls `deleteSelection` first: when it deleted something (it returns true), the tool stays
/// as it was, so selecting and clicking Erase removes the selection. An editor that offers only some tools passes
/// `acting`: the tool it uses for the chosen one, whose button lights.
std::vector<ToolButton*> makeToolButtons(EditorContext& ctx, const std::vector<Tool>& tools, float size,
                                         std::function<bool()> deleteSelection,
                                         std::function<Tool(Tool)> acting = nullptr);

/// A pane's tool switch: Select and Draw as buttons, then a caret whose menu holds the pane's other tools
/// (`menuTools`) followed by whatever `extras` adds (the pane's own settings). While a menu tool is on, the caret
/// shows its icon, lit. `extraIcon`, when set and not Icon::None, wins: a pane's own on/off tool (the timeline's
/// Range) shows there the same way.
struct ToolSwitch {
    ToolButton* select;
    ToolButton* draw;
    ToolButton* more;
};
/// `acting`, as for makeToolButtons, maps the chosen tool to the one the pane really uses (the drum grid).
ToolSwitch makeToolSwitch(EditorContext& ctx, const std::vector<Tool>& menuTools,
                          std::function<void(rack::ui::Menu*)> extras, std::function<Icon()> extraIcon = nullptr,
                          std::function<Tool(Tool)> acting = nullptr, std::function<bool()> deleteSelection = nullptr);
/// The caret's width now: wider while it shows a tool's icon.
float toolSwitchCaretWidth(const ToolSwitch& s, float size);

/// One toolbar button and how long it stays when the header is too narrow for all (higher stays longer).
struct ToolbarItem {
    rack::widget::Widget* widget;
    float width, gap; // gap: the room on its left
    int keep;
};
/// Places `items` right to left from `right`, dropping the lowest `keep` first until the rest fit right of `limit`.
/// Dropped items are hidden. `place` sets a shown item's box (default: the box itself). Returns the left edge of the
/// leftmost shown item (`right` when none is).
float fitToolbar(const std::vector<ToolbarItem>& items, float right, float limit, float y, float height,
                 const std::function<void(rack::widget::Widget*, rack::math::Rect)>& place = nullptr);

/// The drum view switch: "Steps" and "Piano roll" side by side, the one showing lit. Both the step grid and a
/// drum track's piano roll show it in the same place, so the way back is always where you left.
std::pair<ToolButton*, ToolButton*> makeDrumViewSwitch(EditorContext& ctx);
constexpr float kDrumSwitchWidth = 132;
/// Places the pair in `r` (Steps on the left).
void placeDrumViewSwitch(std::pair<ToolButton*, ToolButton*> buttons, rack::math::Rect r);

/// "Devices 3": opens and closes the device strip above the notes (F3). Lit while open. Both note editors show it
/// right after the track arrows. updateDevicesToggle() keeps the count current.
/// Play, stop, record and loop, as the full editor's transport bar and the in-panel editor's corner show them.
/// The caller sizes them.
std::vector<ToolButton*> makeTransportButtons(EditorContext& ctx);
ToolButton* makeDevicesToggle(EditorContext& ctx);
void updateDevicesToggle(EditorContext& ctx, ToolButton* button);
constexpr float kDevicesToggleWidth = 68;

/// The clip chip both note editors start their header with: "Bass  ·  Clip 1  ·  2 bars" in the track's colour.
/// A click opens the clip's menu (rename, loop, length, save, export). `track` and `clip` say what it shows now.
ToolButton* makeClipChip(EditorContext& ctx, std::function<TrackId()> track, std::function<ClipId()> clip);
/// Sets the chip's words (`shortText`: the track's name only, for a crowded header) and places it at the
/// header's left, as wide as its words but at most 280 px or 40% of `paneWidth`.
void placeClipChip(EditorContext& ctx, ToolButton* chip, ClipId clip, bool shortText, float paneWidth);

/// Maximize / restore for one pane: the same button in the top-right corner of the song and the notes pane.
ToolButton* makeMaximizeButton(EditorContext& ctx, Pane pane);
constexpr float kMaximizeButton = 20;

} // namespace tessera
