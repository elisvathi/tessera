#pragma once

#include "../TimeStretcher.hpp"

namespace tessera {

/// Makes the Signalsmith Stretch adapter (MIT licence).
std::unique_ptr<TimeStretcher> makeSignalsmithStretcher();

} // namespace tessera
