#pragma once

#include "../Theme.hpp"

namespace tessera {

template <typename TBase = rack::app::ModuleLightWidget>
struct TPlayLight : rack::componentlibrary::TGrayModuleLightWidget<TBase> {
    TPlayLight() { this->addBaseColor(theme::screenColors().play); }
};
using PlayLight = TPlayLight<>;

template <typename TBase = rack::app::ModuleLightWidget>
struct TAccentLight : rack::componentlibrary::TGrayModuleLightWidget<TBase> {
    TAccentLight() { this->addBaseColor(theme::screenColors().accent); }
};
using AccentLight = TAccentLight<>;

template <typename TBase = rack::app::ModuleLightWidget>
struct TRecordLight : rack::componentlibrary::TGrayModuleLightWidget<TBase> {
    TRecordLight() { this->addBaseColor(theme::screenColors().record); }
};
using RecordLight = TRecordLight<>;

template <typename TBase = rack::app::ModuleLightWidget>
struct TQueueLight : rack::componentlibrary::TGrayModuleLightWidget<TBase> {
    TQueueLight() { this->addBaseColor(theme::screenColors().queue); }
};
using QueueLight = TQueueLight<>;

} // namespace tessera
