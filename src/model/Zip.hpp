#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

/// A minimal ZIP archive writer for DAWproject files, with no compression (entries are "stored"), so it needs no
/// library. The reader only reads what the writer writes; it is for tests.
namespace tessera::zip {

uint32_t crc32(const std::string& data);

/// The archive bytes holding `files` (name, contents) in order.
std::string store(const std::vector<std::pair<std::string, std::string>>& files);

/// The files of an archive made by store(); empty when it isn't one.
std::map<std::string, std::string> read(const std::string& archive);

} // namespace tessera::zip
