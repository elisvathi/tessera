#include "AutoOutModule.hpp"

namespace tessera {

AutoOutModule::AutoOutModule() : OutputModuleBase(std::nullopt) {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
    for (int i = 0; i < kLanes; i++)
        configOutput(LANE_OUTPUT + i, "AUTO " + std::to_string(i + 1))->description =
            "The automation lane on AUTO channel " + std::to_string(i + 1) + ", in its own voltage range";
}

void AutoOutModule::writeFrame(const TrackFrame& frame) {
    for (int i = 0; i < kLanes; i++) {
        float v = i < frame.autoChannels ? frame.automation[i] : 0.f;
        outputs[LANE_OUTPUT + i].setVoltage(v);
        volts[size_t(i)].store(v, std::memory_order_relaxed);
    }
}

} // namespace tessera
