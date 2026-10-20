#pragma once

#include <cstdint>
#include <string>

namespace tessera {

/// A key: root note plus a scale, named by a stable string id ("major", "dorian", "custom-1") so saved
/// patches survive changes to the built-in scale list.
struct ScaleRef {
    int8_t root = 0;                 // 0 = C .. 11 = B
    std::string scale = "chromatic"; // no key: nothing is highlighted or constrained

    bool operator==(const ScaleRef& o) const { return root == o.root && scale == o.scale; }
    bool operator!=(const ScaleRef& o) const { return !(*this == o); }
};

/// A user scale from the 12-key editor. Bit n of `mask` is set when semitone n above the root is in.
struct CustomScale {
    std::string id; // "custom-<n>"
    std::string name;
    uint16_t mask = 0xFFF;

    bool operator==(const CustomScale& o) const { return id == o.id && name == o.name && mask == o.mask; }
};

} // namespace tessera
