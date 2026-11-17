#include "Inflate.hpp"

#include <array>
#include <cstring>

namespace tessera::inflate {

namespace {

constexpr int kMaxBits = 15;
constexpr int kFastBits = 9; // codes this short are found with one table look-up

/// The bits of the compressed stream, read from the lowest bit of each byte up.
class Bits {
  public:
    Bits(const uint8_t* data, size_t size) : data(data), size(size) {}

    /// Tops the buffer up to at least `n` bits while bytes are left.
    void fill(int n) {
        while (count < n && at < size) {
            buffer |= uint64_t(data[at++]) << count;
            count += 8;
        }
    }
    /// The next `n` bits as a number; sets `bad` when the data runs out.
    uint32_t take(int n) {
        if (n == 0)
            return 0;
        fill(n);
        if (count < n) {
            bad = true;
            return 0;
        }
        uint32_t v = uint32_t(buffer & ((uint64_t(1) << n) - 1));
        buffer >>= n;
        count -= n;
        return v;
    }
    /// Drops what is left of the current byte (stored blocks start on a byte).
    void align() {
        int drop = count % 8;
        buffer >>= drop;
        count -= drop;
    }
    /// Copies `n` whole bytes straight to `out` (after align()).
    bool copy(size_t n, std::string& out) {
        while (count >= 8 && n > 0) {
            out.push_back(char(buffer & 0xFF));
            buffer >>= 8;
            count -= 8;
            n--;
        }
        if (at + n > size)
            return false;
        out.append(reinterpret_cast<const char*>(data + at), n);
        at += n;
        return true;
    }
    uint64_t peek() const { return buffer; }
    int available() const { return count; }
    void drop(int n) {
        buffer >>= n;
        count -= n;
    }
    bool bad = false;

  private:
    const uint8_t* data;
    size_t size;
    size_t at = 0;
    uint64_t buffer = 0;
    int count = 0;
};

/// A Huffman code from its code lengths (canonical, as DEFLATE builds them), with a table for the short codes.
struct Code {
    std::array<uint16_t, kMaxBits + 1> count{};
    std::array<uint16_t, 320> symbol{};
    // By the next kFastBits bits of the stream: the symbol and its length (0 = a longer code, decoded slowly).
    std::array<uint16_t, 1 << kFastBits> fastSymbol{};
    std::array<uint8_t, 1 << kFastBits> fastLength{};

    /// Builds the code; false when the lengths over-fill it (an incomplete code is allowed, as zlib allows).
    bool build(const uint8_t* lengths, int n) {
        count.fill(0);
        for (int i = 0; i < n; i++)
            count[lengths[i]]++;
        count[0] = 0;
        int left = 1;
        for (int len = 1; len <= kMaxBits; len++) {
            left <<= 1;
            left -= count[size_t(len)];
            if (left < 0)
                return false;
        }
        std::array<uint16_t, kMaxBits + 1> offset{};
        for (int len = 1; len < kMaxBits; len++)
            offset[size_t(len + 1)] = uint16_t(offset[size_t(len)] + count[size_t(len)]);
        for (int i = 0; i < n; i++)
            if (lengths[i])
                symbol[offset[lengths[i]]++] = uint16_t(i);
        // The fast table: each short code, bit-reversed as it arrives, filled for every value of the bits after it.
        fastLength.fill(0);
        uint32_t code = 0;
        int index = 0;
        for (int len = 1; len <= kMaxBits; len++) {
            for (int k = 0; k < count[size_t(len)]; k++, index++, code++) {
                if (len > kFastBits)
                    continue;
                uint32_t reversed = 0;
                for (int b = 0; b < len; b++)
                    reversed |= ((code >> b) & 1) << (len - 1 - b);
                for (uint32_t fill = reversed; fill < (1u << kFastBits); fill += 1u << len) {
                    fastSymbol[fill] = symbol[size_t(index)];
                    fastLength[fill] = uint8_t(len);
                }
            }
            code <<= 1;
        }
        return true;
    }

    /// The next symbol, or -1 for a code not in the table.
    int decode(Bits& in) const {
        in.fill(kMaxBits);
        if (in.available() >= kFastBits) {
            uint32_t look = uint32_t(in.peek() & ((1u << kFastBits) - 1));
            if (int len = fastLength[look]) {
                in.drop(len);
                return fastSymbol[look];
            }
        }
        // A long code (or the very end of the data): one bit at a time.
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; len++) {
            code |= int(in.take(1));
            if (in.bad)
                return -1;
            int n = count[size_t(len)];
            if (code - n < first)
                return symbol[size_t(index + (code - first))];
            index += n;
            first += n;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

constexpr uint16_t kLengthBase[] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                  193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/// One block's literals and back-references, until its end code.
bool codes(Bits& in, const Code& lit, const Code& dist, std::string& out, size_t limit) {
    for (;;) {
        int s = lit.decode(in);
        if (s < 0)
            return false;
        if (s < 256) {
            if (out.size() >= limit)
                return false;
            out.push_back(char(s));
            continue;
        }
        if (s == 256)
            return true;
        s -= 257;
        if (s >= 29)
            return false;
        size_t length = kLengthBase[s] + in.take(kLengthExtra[s]);
        int d = dist.decode(in);
        if (d < 0 || d >= 30)
            return false;
        size_t back = kDistBase[d] + in.take(kDistExtra[d]);
        if (in.bad || back > out.size() || out.size() + length > limit)
            return false;
        size_t from = out.size() - back;
        if (back >= length) {
            size_t old = out.size();
            out.resize(old + length);
            std::memcpy(&out[old], &out[from], length); // apart: from ends at or before old
        } else {
            for (size_t i = 0; i < length; i++)
                out.push_back(out[from + i]); // overlapping: a run repeating itself
        }
    }
}

} // namespace

bool raw(const uint8_t* data, size_t size, std::string& out, size_t expected, size_t limit) {
    if (expected)
        out.reserve(out.size() + expected);
    Bits in(data, size);
    static const Code fixedLit = [] {
        uint8_t lengths[288];
        for (int i = 0; i < 288; i++)
            lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        Code c;
        c.build(lengths, 288);
        return c;
    }();
    static const Code fixedDist = [] {
        uint8_t lengths[30];
        for (uint8_t& l : lengths)
            l = 5;
        Code c;
        c.build(lengths, 30);
        return c;
    }();
    Code lit, dist;
    bool last = false;
    while (!last) {
        last = in.take(1) != 0;
        uint32_t type = in.take(2);
        if (in.bad)
            return false;
        if (type == 0) {
            in.align();
            uint32_t len = in.take(16), nlen = in.take(16);
            if (in.bad || (len ^ 0xFFFF) != nlen || out.size() + len > limit || !in.copy(len, out))
                return false;
        } else if (type == 1) {
            if (!codes(in, fixedLit, fixedDist, out, limit))
                return false;
        } else if (type == 2) {
            int nlen = int(in.take(5)) + 257, ndist = int(in.take(5)) + 1, ncode = int(in.take(4)) + 4;
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t lengths[320] = {};
            for (int i = 0; i < ncode; i++)
                lengths[order[i]] = uint8_t(in.take(3));
            Code lencode;
            if (in.bad || nlen > 286 || ndist > 30 || !lencode.build(lengths, 19))
                return false;
            uint8_t all[320] = {};
            for (int i = 0; i < nlen + ndist;) {
                int s = lencode.decode(in);
                if (s < 0)
                    return false;
                if (s < 16) {
                    all[i++] = uint8_t(s);
                    continue;
                }
                int repeat;
                uint8_t value = 0;
                if (s == 16) {
                    if (i == 0)
                        return false;
                    value = all[i - 1];
                    repeat = 3 + int(in.take(2));
                } else if (s == 17) {
                    repeat = 3 + int(in.take(3));
                } else {
                    repeat = 11 + int(in.take(7));
                }
                if (in.bad || i + repeat > nlen + ndist)
                    return false;
                while (repeat--)
                    all[i++] = value;
            }
            if (all[256] == 0 || !lit.build(all, nlen) || !dist.build(all + nlen, ndist) ||
                !codes(in, lit, dist, out, limit))
                return false;
        } else {
            return false;
        }
        if (in.bad)
            return false;
    }
    return true;
}

} // namespace tessera::inflate
