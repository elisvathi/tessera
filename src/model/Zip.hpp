#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

/// A minimal ZIP archive writer for DAWproject files, with no compression (entries are "stored"), so it needs no
/// library, and a reader for the zips other programs write (stored or DEFLATE-compressed, up to 4 GB).
namespace tessera::zip {

uint32_t crc32(const std::string& data);
/// Continues a CRC-32 with more bytes, for files too large to hold in memory. Start from 0.
uint32_t crc32Add(uint32_t crc, const char* data, size_t size);

/// The archive bytes holding `files` (name, contents) in order.
std::string store(const std::vector<std::pair<std::string, std::string>>& files);

/// One file in an archive, from its central directory.
struct Entry {
    std::string name;
    uint16_t method = 0; // 0 stored, 8 DEFLATE
    uint32_t crc = 0, packedSize = 0, size = 0;
    size_t dataAt = 0; // where its (packed) bytes start in the archive
};
/// The files (not folders) of an archive, in its directory's order; empty when it isn't a zip this can read.
std::vector<Entry> list(const std::string& archive);
/// Unpacks one file and checks it against its CRC; false when it is damaged or packed some other way.
bool extract(const std::string& archive, const Entry& e, std::string& data);
/// Every file of an archive by name; empty when any of them can't be read.
std::map<std::string, std::string> read(const std::string& archive);

} // namespace tessera::zip
