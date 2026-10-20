#pragma once

#include <rack.hpp>

#include <functional>
#include <string>
#include <vector>

namespace tessera {

/// One entry of a searchable list: its group heading, name, a few words on what it does, and other words people
/// search for. `pick` gets the chosen scope (0 when the list has none).
struct SearchItem {
    std::string group;
    std::string name;
    std::string description;
    std::string keywords;
    std::function<void(int scope)> pick;
};

struct SearchListOptions {
    std::string placeholder = "Search…";
    std::string footer;              // e.g. "adds after Arpeggiator"
    std::vector<std::string> scopes; // optional chips above the list ("Song", "Track 1", "Arpeggiator")
    int scope = 0;                   // the chip lit at first
};

/// Opens a searchable list at the mouse (docs/DEVICES.md §1.1). Typing filters by name,
/// description and keywords; ↑ ↓ choose, Enter picks, Esc or a click outside closes.
void openSearchList(std::vector<SearchItem> items, SearchListOptions options);

} // namespace tessera
