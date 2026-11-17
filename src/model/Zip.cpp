#include "Zip.hpp"

#include "Inflate.hpp"

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

uint32_t crc32Add(uint32_t crc, const char* data, size_t size) {
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
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++)
        c = table[(c ^ uint8_t(data[i])) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t crc32(const std::string& data) { return crc32Add(0, data.data(), data.size()); }

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

std::vector<Entry> list(const std::string& archive) {
    std::vector<Entry> out;
    // The end record sits in the last 22 bytes plus a comment of up to 64 KB.
    if (archive.size() < 22)
        return out;
    size_t end = std::string::npos;
    size_t lowest = archive.size() > 22 + 0xFFFF ? archive.size() - 22 - 0xFFFF : 0;
    for (size_t at = archive.size() - 22 + 1; at-- > lowest;)
        if (get32(archive, at) == kEndRecord) {
            end = at;
            break;
        }
    if (end == std::string::npos)
        return out;
    uint32_t count = get16(archive, end + 10);
    size_t at = get32(archive, end + 16);
    for (uint32_t k = 0; k < count; k++) {
        if (at + 46 > archive.size() || get32(archive, at) != kCentralHeader)
            return {};
        Entry e;
        e.method = uint16_t(get16(archive, at + 10));
        e.crc = get32(archive, at + 16);
        e.packedSize = get32(archive, at + 20);
        e.size = get32(archive, at + 24);
        uint32_t nameLength = get16(archive, at + 28), extra = get16(archive, at + 30), comment = get16(archive, at + 32);
        size_t local = get32(archive, at + 42);
        if (at + 46 + nameLength > archive.size())
            return {};
        e.name = archive.substr(at + 46, nameLength);
        // The data follows the local header, whose name and extra field may differ in length from these.
        if (local + 30 > archive.size() || get32(archive, local) != kLocalHeader)
            return {};
        e.dataAt = local + 30 + get16(archive, local + 26) + get16(archive, local + 28);
        if (e.packedSize == 0xFFFFFFFF || e.size == 0xFFFFFFFF || e.dataAt + e.packedSize > archive.size())
            return {}; // ZIP64 (files over 4 GB) isn't read
        if (!e.name.empty() && e.name.back() != '/')
            out.push_back(e);
        at += 46 + nameLength + extra + comment;
    }
    return out;
}

bool extract(const std::string& archive, const Entry& e, std::string& data) {
    data.clear();
    const uint8_t* packed = reinterpret_cast<const uint8_t*>(archive.data()) + e.dataAt;
    if (e.method == 0)
        data.assign(archive, e.dataAt, e.packedSize);
    else if (e.method != 8 || !inflate::raw(packed, e.packedSize, data, e.size, size_t(e.size) + 1))
        return false;
    return data.size() == e.size && crc32(data) == e.crc;
}

std::map<std::string, std::string> read(const std::string& archive) {
    std::map<std::string, std::string> files;
    for (const Entry& e : list(archive)) {
        std::string data;
        if (!extract(archive, e, data))
            return {};
        files[e.name] = std::move(data);
    }
    return files;
}

} // namespace tessera::zip
