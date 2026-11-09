#include "ProjectBundle.hpp"

#include "../model/Serialize.hpp"
#include "../model/Zip.hpp"
#include "AudioDecode.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>

namespace fs = std::filesystem;

namespace tessera::bundle {

namespace {

constexpr uint32_t kLocalHeader = 0x04034b50;
constexpr uint32_t kCentralHeader = 0x02014b50;
constexpr uint32_t kEndRecord = 0x06054b50;
constexpr uint32_t kEnd64Record = 0x06064b50;
constexpr uint32_t kEnd64Locator = 0x07064b50;
constexpr uint32_t kNeeds64 = 0xFFFFFFFFu;
constexpr uint16_t kManyEntries = 0xFFFF;
constexpr uint16_t kZip64Field = 1;
constexpr uint16_t kVersion = 20;
constexpr uint16_t kVersion64 = 45;
constexpr uint16_t kUtf8Names = 1 << 11;
constexpr uint16_t kDosDate = (0 << 9) | (1 << 5) | 1; // 1980-01-01, the earliest DOS date: entries carry no time
constexpr size_t kBufferSize = size_t(1) << 18;
constexpr uint64_t kMaxDirectoryBytes = uint64_t(1) << 28;
constexpr uint64_t kMaxProjectBytes = uint64_t(1) << 30;
constexpr const char* kProjectName = "project.json";

void put16(std::string& out, uint32_t v) {
    out += char(v & 0xFF);
    out += char((v >> 8) & 0xFF);
}

void put32(std::string& out, uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, v >> 16);
}

void put64(std::string& out, uint64_t v) {
    put32(out, uint32_t(v));
    put32(out, uint32_t(v >> 32));
}

uint16_t get16(const unsigned char* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t get32(const unsigned char* p) { return uint32_t(get16(p)) | uint32_t(get16(p + 2)) << 16; }
uint64_t get64(const unsigned char* p) { return uint64_t(get32(p)) | uint64_t(get32(p + 4)) << 32; }

/// What the central directory needs to know about one written entry.
struct Written {
    std::string name;
    uint32_t crc = 0;
    uint64_t size = 0, offset = 0;
};

/// Hands the writer an entry's bytes: fills up to `n` and says how many, 0 at the end or on an error.
using ReadBytes = std::function<size_t(char* into, size_t n)>;

/// Streams one entry to `out`. Its size is known up front, so only the checksum is filled in afterwards.
bool addEntry(std::ofstream& out, uint64_t& at, std::vector<Written>& list, const std::string& name, uint64_t size,
              const ReadBytes& read, const std::function<void(size_t)>& advanced) {
    Written w;
    w.name = name;
    w.size = size;
    w.offset = at;
    bool big = size >= kNeeds64;
    std::string h;
    put32(h, kLocalHeader);
    put16(h, big ? kVersion64 : kVersion);
    put16(h, kUtf8Names);
    put16(h, 0); // stored, not compressed
    put16(h, 0);
    put16(h, kDosDate);
    uint64_t crcAt = at + h.size();
    put32(h, 0);
    put32(h, big ? kNeeds64 : uint32_t(size));
    put32(h, big ? kNeeds64 : uint32_t(size));
    put16(h, uint32_t(name.size()));
    put16(h, big ? 20 : 0);
    h += name;
    if (big) {
        put16(h, kZip64Field);
        put16(h, 16);
        put64(h, size);
        put64(h, size);
    }
    out.write(h.data(), std::streamsize(h.size()));
    at += h.size();

    std::vector<char> buffer(kBufferSize);
    uint64_t left = size;
    uint32_t crc = 0;
    while (left > 0) {
        size_t got = read(buffer.data(), size_t(std::min<uint64_t>(left, buffer.size())));
        if (got == 0)
            return false;
        crc = zip::crc32Add(crc, buffer.data(), got);
        out.write(buffer.data(), std::streamsize(got));
        if (!out.good())
            return false;
        left -= got;
        if (advanced)
            advanced(got);
    }
    at += size;
    w.crc = crc;

    std::string crcBytes;
    put32(crcBytes, crc);
    out.seekp(std::streamoff(crcAt));
    out.write(crcBytes.data(), 4);
    out.seekp(std::streamoff(at));
    list.push_back(std::move(w));
    return out.good();
}

/// The central directory and end records that close the archive.
void addDirectory(std::ofstream& out, uint64_t at, const std::vector<Written>& list) {
    std::string d;
    for (const Written& w : list) {
        std::string extra;
        if (w.size >= kNeeds64) {
            put64(extra, w.size);
            put64(extra, w.size);
        }
        if (w.offset >= kNeeds64)
            put64(extra, w.offset);
        bool big = !extra.empty();
        put32(d, kCentralHeader);
        put16(d, big ? kVersion64 : kVersion);
        put16(d, big ? kVersion64 : kVersion);
        put16(d, kUtf8Names);
        put16(d, 0);
        put16(d, 0);
        put16(d, kDosDate);
        put32(d, w.crc);
        put32(d, w.size >= kNeeds64 ? kNeeds64 : uint32_t(w.size));
        put32(d, w.size >= kNeeds64 ? kNeeds64 : uint32_t(w.size));
        put16(d, uint32_t(w.name.size()));
        put16(d, big ? uint32_t(extra.size()) + 4 : 0);
        put16(d, 0);
        put16(d, 0);
        put16(d, 0);
        put32(d, 0);
        put32(d, w.offset >= kNeeds64 ? kNeeds64 : uint32_t(w.offset));
        d += w.name;
        if (big) {
            put16(d, kZip64Field);
            put16(d, uint32_t(extra.size()));
            d += extra;
        }
    }
    uint64_t dirSize = d.size();
    bool big = at >= kNeeds64 || dirSize >= kNeeds64 || list.size() >= kManyEntries;
    if (big) {
        uint64_t end64At = at + dirSize;
        put32(d, kEnd64Record);
        put64(d, 44); // the record's size, not counting these first 12 bytes
        put16(d, kVersion64);
        put16(d, kVersion64);
        put32(d, 0);
        put32(d, 0);
        put64(d, list.size());
        put64(d, list.size());
        put64(d, dirSize);
        put64(d, at);
        put32(d, kEnd64Locator);
        put32(d, 0);
        put64(d, end64At);
        put32(d, 1);
    }
    put32(d, kEndRecord);
    put16(d, 0);
    put16(d, 0);
    put16(d, big ? kManyEntries : uint32_t(list.size()));
    put16(d, big ? kManyEntries : uint32_t(list.size()));
    put32(d, big ? kNeeds64 : uint32_t(dirSize));
    put32(d, big ? kNeeds64 : uint32_t(at));
    put16(d, 0);
    out.write(d.data(), std::streamsize(d.size()));
}

/// Puts the finished temporary file in place, or removes it.
bool finishTemporary(const fs::path& temporary, const fs::path& target, bool ok) {
    std::error_code ec;
    if (ok)
        fs::rename(temporary, target, ec);
    if (!ok || ec) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}

/// Where one entry of a read archive is. Only stored entries are listed.
struct Entry {
    uint32_t crc = 0;
    uint64_t size = 0, offset = 0;
};

bool readAt(std::ifstream& in, uint64_t pos, void* into, size_t n) {
    in.clear();
    in.seekg(std::streamoff(pos));
    in.read(static_cast<char*>(into), std::streamsize(n));
    return in.good();
}

/// Reads the archive's table of contents. A project may come from anyone, so every length is checked against the
/// file before it is used.
bool readDirectory(std::ifstream& in, uint64_t total, std::map<std::string, Entry>& out) {
    constexpr size_t kEndSize = 22;
    constexpr size_t kMaxComment = 65535;
    if (total < kEndSize)
        return false;
    size_t tailSize = size_t(std::min<uint64_t>(total, kEndSize + kMaxComment));
    std::vector<unsigned char> tail(tailSize);
    if (!readAt(in, total - tailSize, tail.data(), tailSize))
        return false;
    size_t found = tailSize;
    for (size_t i = tailSize - kEndSize + 1; i-- > 0;)
        if (get32(&tail[i]) == kEndRecord && i + kEndSize + get16(&tail[i + 20]) == tailSize) {
            found = i;
            break;
        }
    if (found == tailSize)
        return false;
    uint64_t count = get16(&tail[found + 10]);
    uint64_t dirSize = get32(&tail[found + 12]);
    uint64_t dirAt = get32(&tail[found + 16]);
    if (count == kManyEntries || dirSize == kNeeds64 || dirAt == kNeeds64) {
        uint64_t endAt = total - tailSize + found;
        unsigned char locator[20], end64[56];
        if (endAt < sizeof locator || !readAt(in, endAt - sizeof locator, locator, sizeof locator) ||
            get32(locator) != kEnd64Locator)
            return false;
        uint64_t end64At = get64(locator + 8);
        if (end64At + sizeof end64 > total || !readAt(in, end64At, end64, sizeof end64) || get32(end64) != kEnd64Record)
            return false;
        count = get64(end64 + 32);
        dirSize = get64(end64 + 40);
        dirAt = get64(end64 + 48);
    }
    if (dirSize > kMaxDirectoryBytes || dirAt > total || dirSize > total - dirAt)
        return false;
    std::vector<unsigned char> dir(static_cast<size_t>(dirSize));
    if (dirSize > 0 && !readAt(in, dirAt, dir.data(), dir.size()))
        return false;
    size_t at = 0;
    for (uint64_t i = 0; i < count; i++) {
        if (at + 46 > dir.size() || get32(&dir[at]) != kCentralHeader)
            return false;
        uint16_t method = get16(&dir[at + 10]);
        uint64_t compressed = get32(&dir[at + 20]), size = get32(&dir[at + 24]), offset = get32(&dir[at + 42]);
        size_t nameLen = get16(&dir[at + 28]), extraLen = get16(&dir[at + 30]), commentLen = get16(&dir[at + 32]);
        size_t next = at + 46 + nameLen + extraLen + commentLen;
        if (next > dir.size())
            return false;
        // Each 64-bit field is present only when its 32-bit field says so, always in this order.
        size_t x = at + 46 + nameLen, xEnd = x + extraLen;
        while (x + 4 <= xEnd) {
            uint16_t id = get16(&dir[x]), len = get16(&dir[x + 2]);
            if (x + 4 + len > xEnd)
                return false;
            if (id == kZip64Field) {
                size_t p = x + 4, pEnd = p + len;
                auto take = [&](uint64_t& field) {
                    if (field == kNeeds64 && p + 8 <= pEnd) {
                        field = get64(&dir[p]);
                        p += 8;
                    }
                };
                take(size);
                take(compressed);
                take(offset);
            }
            x += 4 + size_t(len);
        }
        if (method == 0 && compressed == size) {
            Entry e;
            e.crc = get32(&dir[at + 16]);
            e.size = size;
            e.offset = offset;
            out[std::string(reinterpret_cast<const char*>(&dir[at + 46]), nameLen)] = e;
        }
        at = next;
    }
    return true;
}

/// Where the entry's data starts, read from its local header. False when it runs past the end of the file.
bool dataStart(std::ifstream& in, uint64_t total, const Entry& e, uint64_t& start) {
    unsigned char h[30];
    if (e.offset > total || total - e.offset < sizeof h || !readAt(in, e.offset, h, sizeof h) ||
        get32(h) != kLocalHeader)
        return false;
    start = e.offset + sizeof h + get16(h + 26) + get16(h + 28);
    return start <= total && e.size <= total - start;
}

/// Opens the archive at `path` and reads its table of contents.
bool open(const std::string& path, std::ifstream& in, uint64_t& total, std::map<std::string, Entry>& entries) {
    std::error_code ec;
    fs::path p = fs::u8path(path);
    total = fs::file_size(p, ec);
    if (ec)
        return false;
    in.open(p, std::ios::binary);
    return in.is_open() && readDirectory(in, total, entries);
}

} // namespace

bool writeText(const std::string& path, const std::string& text) {
    fs::path target = fs::u8path(path), temporary = fs::u8path(path + ".part");
    bool ok;
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), std::streamsize(text.size()));
        out.flush();
        ok = out.is_open() && out.good();
    }
    return finishTemporary(temporary, target, ok);
}

bool write(const std::string& path, const std::string& projectJson, const std::vector<AudioEntry>& audio,
           std::atomic<float>* progress) {
    fs::path target = fs::u8path(path), temporary = fs::u8path(path + ".part");
    uint64_t total = projectJson.size(), done = 0;
    std::vector<uint64_t> sizes;
    for (const AudioEntry& a : audio) {
        std::error_code ec;
        uint64_t size = fs::file_size(fs::u8path(a.path), ec);
        if (ec)
            return false;
        sizes.push_back(size);
        total += size;
    }
    auto advanced = [&](size_t n) {
        done += n;
        if (progress && total > 0)
            progress->store(float(double(done) / double(total)), std::memory_order_relaxed);
    };

    bool ok = true;
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        ok = out.is_open();
        uint64_t at = 0;
        std::vector<Written> list;
        size_t sent = 0;
        ok = ok && addEntry(
                       out, at, list, kProjectName, projectJson.size(),
                       [&](char* into, size_t n) {
                           size_t count = std::min(n, projectJson.size() - sent);
                           std::memcpy(into, projectJson.data() + sent, count);
                           sent += count;
                           return count;
                       },
                       advanced);
        for (size_t i = 0; ok && i < audio.size(); i++) {
            std::ifstream in(fs::u8path(audio[i].path), std::ios::binary);
            ok = in.is_open() && addEntry(
                                     out, at, list, audio[i].name, sizes[i],
                                     [&](char* into, size_t n) {
                                         in.read(into, std::streamsize(n));
                                         return size_t(in.gcount());
                                     },
                                     advanced);
        }
        if (ok) {
            addDirectory(out, at, list);
            out.flush();
            ok = out.good();
        }
    }
    return finishTemporary(temporary, target, ok);
}

bool isBundle(const std::string& path) {
    std::ifstream in(fs::u8path(path), std::ios::binary);
    unsigned char magic[4];
    in.read(reinterpret_cast<char*>(magic), sizeof magic);
    return in.good() && get32(magic) == kLocalHeader;
}

bool readProject(const std::string& path, std::string& json) {
    std::ifstream in;
    uint64_t total = 0, start = 0;
    std::map<std::string, Entry> entries;
    if (!open(path, in, total, entries))
        return false;
    auto it = entries.find(kProjectName);
    if (it == entries.end() || it->second.size > kMaxProjectBytes || !dataStart(in, total, it->second, start))
        return false;
    json.resize(size_t(it->second.size));
    if (!json.empty() && !readAt(in, start, json.data(), json.size()))
        return false;
    return zip::crc32(json) == it->second.crc;
}

int extract(const std::string& path, const std::vector<Wanted>& wanted, const std::string& storage,
            std::atomic<float>* progress) {
    std::ifstream in;
    uint64_t total = 0, wantedBytes = 0, done = 0;
    std::map<std::string, Entry> entries;
    if (!open(path, in, total, entries))
        return 0;
    for (const Wanted& w : wanted) {
        auto it = entries.find(w.file);
        if (it != entries.end())
            wantedBytes += it->second.size;
    }
    int copied = 0;
    std::vector<char> buffer(kBufferSize);
    for (const Wanted& w : wanted) {
        auto it = entries.find(w.file);
        if (it == entries.end() || !isSafeAudioFile(w.file))
            continue;
        std::error_code ec;
        fs::path dest = fs::u8path(storage) / fs::u8path(w.file);
        uint64_t start = 0;
        if (fs::exists(dest, ec) || !dataStart(in, total, it->second, start)) {
            done += it->second.size;
            continue;
        }
        fs::create_directories(dest.parent_path(), ec);
        fs::path temporary = dest;
        temporary += ".part";
        bool ok;
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            ok = out.is_open();
            in.clear();
            in.seekg(std::streamoff(start));
            uint64_t left = it->second.size;
            while (ok && left > 0) {
                in.read(buffer.data(), std::streamsize(std::min<uint64_t>(left, buffer.size())));
                size_t got = size_t(in.gcount());
                out.write(buffer.data(), std::streamsize(got));
                ok = got > 0 && out.good();
                left -= got;
                done += got;
                if (progress && wantedBytes > 0)
                    progress->store(float(double(done) / double(wantedBytes)), std::memory_order_relaxed);
            }
            out.flush();
            ok = ok && out.good();
        }
        if (ok && w.contentHash != 0 && hashFile(temporary.string()) != w.contentHash)
            ok = false;
        if (finishTemporary(temporary, dest, ok))
            copied++;
    }
    return copied;
}

} // namespace tessera::bundle
