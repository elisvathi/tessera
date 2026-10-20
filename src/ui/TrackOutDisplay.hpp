#pragma once

#include "../module/OutputModuleBase.hpp"

#include <rack.hpp>

#include <string>

namespace tessera {

/// Output module screen: which Tessera and track it follows, in the track's color, or why it is silent.
class TrackOutDisplay : public rack::widget::Widget {
  public:
    explicit TrackOutDisplay(OutputModuleBase* module) : module(module) {}

    void draw(const DrawArgs& args) override;
    void drawLayer(const DrawArgs& args, int layer) override;

  private:
    void gather();
    void drawBound(NVGcontext* vg);
    void drawMessage(NVGcontext* vg, const char* message, NVGcolor color, const char* hint);

    OutputModuleBase* module;

    BindingStatus status = BindingStatus::NoSource;
    std::string sourceName;
    std::string trackName;
    int trackNumber = 0;
    uint32_t trackColor = 0;
};

} // namespace tessera
