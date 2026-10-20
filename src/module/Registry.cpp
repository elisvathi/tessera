#include "Registry.hpp"

#include "OutputModuleBase.hpp"
#include "TesseraModule.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

namespace {

template <typename T>
void eraseValue(std::vector<T>& v, const T& value) {
    v.erase(std::remove(v.begin(), v.end(), value), v.end());
}

} // namespace

Registry& Registry::get() {
    static Registry registry;
    return registry;
}

void Registry::addSource(TesseraModule* source) {
    std::lock_guard<std::mutex> lock(mutex);
    if (std::find(addOrder.begin(), addOrder.end(), source) != addOrder.end())
        return;
    addOrder.push_back(source);
    sourceList.push_back(source);
}

void Registry::removeSource(TesseraModule* source) {
    std::lock_guard<std::mutex> lock(mutex);
    eraseValue(addOrder, source);
    eraseValue(sourceList, source);
}

TesseraModule* Registry::findSource(int64_t moduleId) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (TesseraModule* s : addOrder)
        if (s->id == moduleId)
            return s;
    return nullptr;
}

TesseraModule* Registry::findSourceByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (TesseraModule* s : addOrder)
        if (s->instanceName() == name)
            return s;
    return nullptr;
}

std::vector<TesseraModule*> Registry::sources() const {
    std::lock_guard<std::mutex> lock(mutex);
    return addOrder;
}

void Registry::touch(TesseraModule* source) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = std::find(sourceList.begin(), sourceList.end(), source);
    if (it != sourceList.end())
        std::rotate(it, it + 1, sourceList.end());
}

TesseraModule* Registry::mostRecentSource() const {
    std::lock_guard<std::mutex> lock(mutex);
    return sourceList.empty() ? nullptr : sourceList.back();
}

std::string Registry::uniqueSourceName(const std::string& base) const {
    auto taken = [this](const std::string& candidate) {
        std::lock_guard<std::mutex> lock(mutex);
        return std::any_of(addOrder.begin(), addOrder.end(),
                           [&](TesseraModule* s) { return s->instanceName() == candidate; });
    };
    if (!taken(base))
        return base;
    // A copy of "Main 2" is "Main 3", not "Main 2 2".
    std::string stem = base;
    size_t space = base.find_last_of(' ');
    if (space != std::string::npos && space + 1 < base.size() &&
        base.find_first_not_of("0123456789", space + 1) == std::string::npos)
        stem = base.substr(0, space);
    for (int n = 2;; n++) {
        std::string candidate = stem + " " + std::to_string(n);
        if (!taken(candidate))
            return candidate;
    }
}

double Registry::frameTime() { return APP->window ? APP->window->getFrameTime() : NAN; }

void Registry::recordPaste(int64_t fromId, int64_t toId) {
    std::lock_guard<std::mutex> lock(mutex);
    pastes.record(fromId, toId, frameTime());
}

std::optional<int64_t> Registry::findPaste(int64_t fromId, double frameTime) const {
    std::lock_guard<std::mutex> lock(mutex);
    return pastes.find(fromId, frameTime);
}

void Registry::addOutput(OutputModuleBase* output) {
    std::lock_guard<std::mutex> lock(mutex);
    if (std::find(outputList.begin(), outputList.end(), output) == outputList.end())
        outputList.push_back(output);
}

void Registry::removeOutput(OutputModuleBase* output) {
    std::lock_guard<std::mutex> lock(mutex);
    eraseValue(outputList, output);
}

bool Registry::hasOutputFor(int64_t sourceId, TrackId track) const {
    std::lock_guard<std::mutex> lock(mutex);
    return std::any_of(outputList.begin(), outputList.end(), [&](OutputModuleBase* o) {
        return o->binding().sourceId == sourceId && o->binding().track == track;
    });
}

} // namespace tessera
