#pragma once

#include <cstdint>

namespace tessera {

/// A setting inside Tessera that modulators and automation can turn (docs/DEVICES.md §4): a knob of a device,
/// a setting of a modulator, or a setting of a playhead (docs/design/PLAYHEADS.md). `param` indexes the owner
/// type's parameter list.
struct ParamRef {
    enum class Kind : uint8_t { Device, Modulator, Playhead, Motion };
    Kind kind = Kind::Device;
    uint64_t owner = 0; // DeviceId, ModulatorId, PlayheadId or MotionId value
    uint16_t param = 0;

    bool valid() const { return owner != 0; }
    bool operator==(const ParamRef& o) const { return kind == o.kind && owner == o.owner && param == o.param; }
    bool operator!=(const ParamRef& o) const { return !(*this == o); }
};

} // namespace tessera
