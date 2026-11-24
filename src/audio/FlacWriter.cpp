#include "FlacWriter.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

namespace {

constexpr int kBlock = 4096;
constexpr int kBits = 24;

/// Bits written most significant first.
class BitWriter {
  public:
    void put(uint64_t value, int bits) {
        for (int i = bits - 1; i >= 0; i--) {
            acc = uint8_t((acc << 1) | ((value >> i) & 1));
            if (++filled == 8) {
                bytes.push_back(acc);
                acc = 0;
                filled = 0;
            }
        }
    }
    void putSigned(int64_t value, int bits) { put(uint64_t(value) & ((uint64_t(1) << bits) - 1), bits); }
    /// A run of zeros then a one: `q` in unary.
    void unary(uint32_t q) {
        while (q >= 32) {
            put(0, 32);
            q -= 32;
        }
        put(1, int(q) + 1);
    }
    void align() {
        while (filled != 0)
            put(0, 1);
    }
    std::vector<uint8_t> bytes;

  private:
    uint8_t acc = 0;
    int filled = 0;
};

uint8_t crc8(const uint8_t* data, size_t n) {
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = uint8_t(crc & 0x80 ? (crc << 1) ^ 0x07 : crc << 1);
    }
    return crc;
}

uint16_t crc16(const uint8_t* data, size_t n) {
    uint16_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= uint16_t(data[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = uint16_t(crc & 0x8000 ? (crc << 1) ^ 0x8005 : crc << 1);
    }
    return crc;
}

/// A frame number as FLAC writes it: UTF-8's way of coding a number in one to six bytes.
void putUtf8(BitWriter& w, uint32_t v) {
    if (v < 0x80) {
        w.put(v, 8);
        return;
    }
    int extra = v < 0x800 ? 1 : v < 0x10000 ? 2 : v < 0x200000 ? 3 : v < 0x4000000 ? 4 : 5;
    uint32_t lead = (0xFF00u >> (extra + 1)) & 0xFF;
    w.put(lead | (v >> (6 * extra)), 8);
    for (int i = extra - 1; i >= 0; i--)
        w.put(0x80 | ((v >> (6 * i)) & 0x3F), 8);
}

} // namespace

FlacWriter::~FlacWriter() {
    if (file)
        close();
}

bool FlacWriter::open(const std::string& path, int ch, int rate) {
    channels = std::clamp(ch, 1, 2);
    sampleRate = rate;
    file = std::fopen(path.c_str(), "wb");
    if (!file)
        return false;
    block.assign(size_t(channels), {});
    // "fLaC", then STREAMINFO; its sizes and sample count are filled in by close().
    std::fwrite("fLaC", 1, 4, file);
    std::vector<uint8_t> header(4 + 34, 0);
    std::fwrite(header.data(), 1, header.size(), file);
    return true;
}

bool FlacWriter::write(const float* interleaved, size_t frames) {
    if (!file)
        return false;
    for (size_t i = 0; i < frames; i++) {
        for (int c = 0; c < channels; c++) {
            double x = std::clamp(double(interleaved[i * size_t(channels) + size_t(c)]), -1.0, 1.0);
            block[size_t(c)].push_back(int32_t(std::lround(x * 8388607.0)));
        }
        if (int(block[0].size()) == kBlock)
            encodeBlock();
    }
    return ok;
}

void FlacWriter::encodeBlock() {
    int n = int(block[0].size());
    if (n == 0)
        return;
    BitWriter w;
    w.put(0x3FFE, 14); // sync
    w.put(0, 1);
    w.put(0, 1); // fixed block size
    w.put(n == kBlock ? 0xC : 0x7, 4);
    w.put(0, 4);                  // sample rate: from STREAMINFO
    w.put(uint64_t(channels - 1), 4); // independent channels
    w.put(0x6, 3);                // 24 bits
    w.put(0, 1);
    putUtf8(w, frameNumber++);
    if (n != kBlock)
        w.put(uint64_t(n - 1), 16);
    w.put(crc8(w.bytes.data(), w.bytes.size()), 8);
    for (int c = 0; c < channels; c++) {
        const std::vector<int32_t>& x = block[size_t(c)];
        w.put(0, 1);
        if (n < 3) {
            w.put(0x01, 6); // verbatim
            w.put(0, 1);
            for (int32_t s : x)
                w.putSigned(s, kBits);
            continue;
        }
        w.put(0x0A, 6); // fixed, order 2
        w.put(0, 1);
        w.putSigned(x[0], kBits);
        w.putSigned(x[1], kBits);
        std::vector<uint32_t> folded(size_t(n - 2));
        double sum = 0;
        for (int i = 2; i < n; i++) {
            int64_t r = int64_t(x[size_t(i)]) - 2 * int64_t(x[size_t(i - 1)]) + int64_t(x[size_t(i - 2)]);
            uint32_t u = r >= 0 ? uint32_t(r) << 1 : (uint32_t(-r) << 1) - 1;
            folded[size_t(i - 2)] = u;
            sum += u;
        }
        double mean = sum / double(n - 2);
        int k = mean >= 1 ? std::min(30, int(std::floor(std::log2(mean)))) : 0;
        w.put(1, 2); // Rice with 5-bit parameters
        w.put(0, 4); // one partition
        w.put(uint64_t(k), 5);
        for (uint32_t u : folded) {
            w.unary(u >> k);
            if (k > 0)
                w.put(u & ((1u << k) - 1), k);
        }
    }
    w.align();
    uint16_t crc = crc16(w.bytes.data(), w.bytes.size());
    w.put(crc, 16);
    if (std::fwrite(w.bytes.data(), 1, w.bytes.size(), file) != w.bytes.size())
        ok = false;
    minFrame = std::min<uint32_t>(minFrame, uint32_t(w.bytes.size()));
    maxFrame = std::max<uint32_t>(maxFrame, uint32_t(w.bytes.size()));
    total += uint64_t(n);
    for (auto& ch : block)
        ch.clear();
}

bool FlacWriter::close() {
    if (!file)
        return false;
    encodeBlock();
    BitWriter s;
    s.put(1, 1); // the last metadata block
    s.put(0, 7); // STREAMINFO
    s.put(34, 24);
    s.put(kBlock, 16);
    s.put(kBlock, 16);
    s.put(total ? minFrame : 0, 24);
    s.put(maxFrame, 24);
    s.put(uint64_t(sampleRate), 20);
    s.put(uint64_t(channels - 1), 3);
    s.put(kBits - 1, 5);
    s.put(total, 36);
    for (int i = 0; i < 16; i++)
        s.put(0, 8); // no MD5
    std::fseek(file, 4, SEEK_SET);
    if (std::fwrite(s.bytes.data(), 1, s.bytes.size(), file) != s.bytes.size())
        ok = false;
    std::fclose(file);
    file = nullptr;
    return ok;
}

} // namespace tessera
