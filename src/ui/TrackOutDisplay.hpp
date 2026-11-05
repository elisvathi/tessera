#pragma once

#include "../module/OutputModuleBase.hpp"

#include <rack.hpp>

#include <string>

namespace tessera {

/// Output module screen: which Tessera and track it follows, in the track's color, or why it is silent.
class TrackOutDisplay : public rack::widget::Widget {
  public:
    /// `channels`: the module plays one channel or all, shown on the display and picked by clicking it.
    explicit TrackOutDisplay(OutputModuleBase* module, bool channels = false) : module(module), channels(channels) {}

    void draw(const DrawArgs& args) override;
    void onButton(const ButtonEvent& e) override;
    void drawLayer(const DrawArgs& args, int layer) override;

  private:
    void gather();
    void drawBound(NVGcontext* vg);
    void drawMessage(NVGcontext* vg, const char* message, NVGcolor color, const char* hint);

    OutputModuleBase* module;
    bool channels;
    int part = 0;
    std::string partPlayers;

    BindingStatus status = BindingStatus::NoSource;
    std::string sourceName;
    std::string trackName;
    std::string trackNumber; // "3", or "M" for the master
    uint32_t trackColor = 0;
};

} // namespace tessera
