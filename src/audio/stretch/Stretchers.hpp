#pragma once

#include "../TimeStretcher.hpp"

namespace tessera {

/// Makes the Signalsmith Stretch adapter (MIT licence). Only built when the Makefile's STRETCHERS includes it.
std::unique_ptr<TimeStretcher> makeSignalsmithStretcher();

/// Makes the Rubber Band adapter (GPL licence). Only built when the Makefile's STRETCHERS includes it.
std::unique_ptr<TimeStretcher> makeRubberBandStretcher();

} // namespace tessera
