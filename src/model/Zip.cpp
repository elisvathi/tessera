#include "Zip.hpp"

#include <array>

namespace tessera::zip {

namespace {

constexpr uint32_t kLocalHeader = 0x04034b50;
constexpr uint32_t kCentralHeader = 0x02014b50;
constexpr uint32_t kEndRecord = 0x06054b50;
constexpr uint16_t kVersion = 20;
constexpr uint16_t kUtf8Names = 1 << 11;
// 1980-01-01 00:00, the earliest DOS date: the files carry no meaningful time.
constexpr uint16_t kDosTime = 0;
constexpr uint16_t kDosDate = (0 << 9) | (1 << 5) | 1;

void put16(std::string& out, uint32_t v) {
    out += char(v & 0xFF);
    out += char((v >> 8) & 0xFF);
}

void put32(std::string& out, uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}

uint32_t get16(const std::string& in, size_t at) {
    return uint32_t(uint8_t(in[at])) | uint32_t(uint8_t(in[at + 1])) << 8;
}

uint32_t get32(const std::string& in, size_t at) { return get16(in, at) | get16(in, at + 2) << 16; }

} // namespace

uint32_t crc32(const std::string& data) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (char ch : data)
        c = table[(c ^ uint8_t(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string store(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, central;
    for (const auto& [name, data] : files) {
        uint32_t offset = uint32_t(out.size());
        uint32_t crc = crc32(data);
        auto header = [&](std::string& h, bool inCentral) {
            put32(h, inCentral ? kCentralHeader : kLocalHeader);
            if (inCentral)
                put16(h, kVersion); // made by
            put16(h, kVersion);     // needed to extract
            put16(h, kUtf8Names);
            put16(h, 0); // stored
            put16(h, kDosTime);
            put16(h, kDosDate);
            put32(h, crc);
            put32(h, uint32_t(data.size()));
            put32(h, uint32_t(data.size()));
            put16(h, uint32_t(name.size()));
            put16(h, 0); // extra field
            if (inCentral) {
                put16(h, 0); // comment
                put16(h, 0); // disk
                put16(h, 0); // internal attributes
                put32(h, 0); // external attributes
                put32(h, offset);
            }
            h += name;
        };
        header(out, false);
        out += data;
        header(central, true);
    }
    uint32_t centralAt = uint32_t(out.size());
    out += central;
    put32(out, kEndRecord);
    put16(out, 0); // this disk
    put16(out, 0); // the disk with the central directory
    put16(out, uint32_t(files.size()));
    put16(out, uint32_t(files.size()));
    put32(out, uint32_t(central.size()));
    put32(out, centralAt);
    put16(out, 0); // comment
    return out;
}

std::map<std::string, std::string> read(const std::string& archive) {
    std::map<std::string, std::string> files;
    size_t at = 0;
    while (at + 30 <= archive.size() && get32(archive, at) == kLocalHeader) {
        if (get16(archive, at + 8) != 0)
            return {}; // compressed: not ours
        uint32_t crc = get32(archive, at + 14);
        uint32_t size = get32(archive, at + 18);
        uint32_t nameLength = get16(archive, at + 26);
        uint32_t extra = get16(archive, at + 28);
        size_t dataAt = at + 30 + nameLength + extra;
        if (dataAt + size > archive.size())
            return {};
        std::string name = archive.substr(at + 30, nameLength);
        std::string data = archive.substr(dataAt, size);
        if (crc32(data) != crc)
            return {};
        files[name] = data;
        at = dataAt + size;
    }
    return files;
}

} // namespace tessera::zip
