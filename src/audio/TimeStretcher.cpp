#include "TimeStretcher.hpp"

#include "stretch/Stretchers.hpp"

#include <cmath>

#if !defined(TESSERA_STRETCH_SIGNALSMITH) && !defined(TESSERA_STRETCH_RUBBERBAND)
#error "Build at least one time stretcher: set STRETCHERS in the Makefile"
#endif

namespace tessera {

namespace {

// Until the Phase 0 listening test picks one, the MIT library is the default: it can ship in any product.
constexpr const char* kDefaultStretcher = "signalsmith";

/// Makes a stretcher by id, or returns null when this build doesn't include it.
std::unique_ptr<TimeStretcher> makeBuilt(const std::string& id) {
#ifdef TESSERA_STRETCH_SIGNALSMITH
    if (id == "signalsmith")
        return makeSignalsmithStretcher();
#endif
#ifdef TESSERA_STRETCH_RUBBERBAND
    if (id == "rubberband")
        return makeRubberBandStretcher();
#endif
    return nullptr;
}

} // namespace

int64_t stretchedFrames(int64_t inFrames, double ratio) { return int64_t(std::llround(double(inFrames) * ratio)); }

std::vector<std::string> availableStretchers() {
    std::vector<std::string> ids;
    if (makeBuilt(kDefaultStretcher))
        ids.push_back(kDefaultStretcher);
    for (const char* id : {"signalsmith", "rubberband"})
        if (id != std::string(kDefaultStretcher) && makeBuilt(id))
            ids.push_back(id);
    return ids;
}

std::unique_ptr<TimeStretcher> makeStretcher(const std::string& id) {
    if (auto stretcher = makeBuilt(id))
        return stretcher;
    return makeBuilt(availableStretchers().front());
}

} // namespace tessera
