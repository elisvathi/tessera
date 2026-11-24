#include "Lengths.hpp"

#include <algorithm>
#include <cassert>

namespace tessera::lengths {

namespace {

/// The whole scale, short to long.
const std::vector<Length>& scale() {
    static const std::vector<Length> all = {
        {"1/64", 1.0 / 64},    {"1/32 T", 1.0 / 48},  {"1/32", 1.0 / 32},   {"1/16 T", 1.0 / 24}, {"1/32 D", 3.0 / 64},
        {"1/16", 1.0 / 16},    {"1/8 T", 1.0 / 12},   {"1/16 D", 3.0 / 32}, {"1/8", 1.0 / 8},     {"1/4 T", 1.0 / 6},
        {"1/8 D", 3.0 / 16},   {"1/4", 1.0 / 4},      {"1/2 T", 1.0 / 3},   {"1/4 D", 3.0 / 8},   {"1/2", 1.0 / 2},
        {"1/2 D", 3.0 / 4},    {"1 bar", 1, true},    {"2 bars", 2, true},  {"4 bars", 4, true},  {"8 bars", 8, true},
        {"16 bars", 16, true}, {"32 bars", 32, true}, {"64 bars", 64, true}};
    return all;
}

/// The place of `name` on the scale; it must be on it.
int find(const std::string& name) {
    const std::vector<Length>& all = scale();
    auto it = std::find_if(all.begin(), all.end(), [&](const Length& l) { return l.name == name; });
    assert(it != all.end());
    return int(it - all.begin());
}

} // namespace

std::vector<Length> range(const std::string& shortest, const std::string& longest) {
    const std::vector<Length>& all = scale();
    return std::vector<Length>(all.begin() + find(shortest), all.begin() + find(longest) + 1);
}

std::vector<std::string> names(const std::vector<Length>& list) {
    std::vector<std::string> out;
    for (const Length& l : list)
        out.push_back(l.name);
    return out;
}

double ticks(const Length& l, TimeSig ts) {
    return l.bars ? l.wholes * double(ticksPerBar(ts)) : l.wholes * 4 * double(kPpq);
}

double ticksAt(const std::vector<Length>& list, int index, TimeSig ts) {
    if (list.empty())
        return 0;
    return ticks(list[size_t(std::clamp(index, 0, int(list.size()) - 1))], ts);
}

const std::vector<Length>& noteValues() {
    static const std::vector<Length> list = range("1/64", "8 bars");
    return list;
}

const std::vector<Length>& slowValues() {
    static const std::vector<Length> list = range("1/64", "32 bars");
    return list;
}

} // namespace tessera::lengths
