#pragma once

#include "../model/Types.hpp"
#include "PasteRemaps.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace tessera {

class TesseraModule;
class OutputModuleBase;

/// Plugin-wide directory of Tessera instances and the output modules that follow them.
///
/// UI thread only (including onAdd/onRemove). Never touch it from process(). Pointers it returns are valid
/// for the current UI frame only: modules are deleted on the UI thread, so look them up again next frame.
class Registry {
  public:
    static Registry& get();

    void addSource(TesseraModule* source);
    void removeSource(TesseraModule* source);
    TesseraModule* findSource(int64_t moduleId) const;
    TesseraModule* findSourceByName(const std::string& name) const;
    /// In the order they were added.
    std::vector<TesseraModule*> sources() const;

    /// Marks a Tessera as the one the user worked with last; new output modules follow it.
    void touch(TesseraModule* source);
    TesseraModule* mostRecentSource() const;

    /// `base` if no instance uses it yet, otherwise "base 2", "base 3", ...
    std::string uniqueSourceName(const std::string& base) const;

    /// The start time of the current UI frame; everything one paste adds shares it. NaN without a window.
    static double frameTime();
    /// Records that the Tessera saved as `fromId` was pasted as `toId` during this frame.
    void recordPaste(int64_t fromId, int64_t toId);
    /// The copy of `fromId` pasted in the frame that started at `frameTime`, if any.
    std::optional<int64_t> findPaste(int64_t fromId, double frameTime) const;

    void addOutput(OutputModuleBase* output);
    void removeOutput(OutputModuleBase* output);
    bool hasOutputFor(int64_t sourceId, TrackId track) const;

  private:
    mutable std::mutex mutex;
    std::vector<TesseraModule*> sourceList; // most recently touched last
    std::vector<TesseraModule*> addOrder;
    std::vector<OutputModuleBase*> outputList;
    PasteRemaps pastes;
};

} // namespace tessera
