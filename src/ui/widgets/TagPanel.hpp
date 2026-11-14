#pragma once

#include "../interaction/DeviceTags.hpp"

#include <rack.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace tessera {

/// What a tag's panel shows and does (docs/DEVICES.md §1.1): the setting's name, a box to type in, every choice laid
/// out by its kind, "Back to <default>", and links to the setting's other actions. Hovering a choice only previews it;
/// picking one changes the song as one undo step; Esc or a click outside leaves it as it was.
struct TagPanelSpec {
    std::string title;
    NVGcolor tint;
    tags::ValueList list; // empty for channels
    int current = 0;      // the value's place in `list`
    /// The Direction panel's step sizes (for the step orders), and which one is set.
    std::vector<std::string> steps;
    int step = 0;
    /// A channel set (the Note Filter) in place of a list.
    bool channels = false;
    uint16_t mask = 0;

    std::function<void(int)> preview;            // a place in `list` to show, -1 for the value as set
    std::function<void(int)> pick;               // a place in `list`, picked
    std::function<void(int)> pickStep;           // a step size, picked
    std::function<void(uint16_t)> previewMask;   // channels
    std::function<void(uint16_t)> pickMask;
    /// Where the panel opens, in scene coordinates: under `below` (the bottom of the picture it changes, so the
    /// picture stays in view while choices are previewed), centred on `centerX`. Negative: under the mouse.
    float below = -1, centerX = -1;
    /// Links along the bottom: "Draw a curve", "Modulate, automate…". Each closes the panel first.
    std::vector<std::pair<std::string, std::function<void()>>> links;
};

/// Opens the panel under the mouse. A panel already open closes first.
void openTagPanel(TagPanelSpec spec);
/// Whether a tag's panel is open now.
bool tagPanelOpen();

} // namespace tessera
