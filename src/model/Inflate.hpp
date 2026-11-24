#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/// Unpacking DEFLATE data (RFC 1951), the compression inside zip files, so DAWproject files other programs write
/// can be read with no library.
namespace tessera::inflate {

/// Unpacks raw DEFLATE data (no zlib or gzip header) and appends it to `out`. `expected` (when not 0) is the size the
/// data unpacks to, reserved up front. Returns false for damaged data, or when more than `limit` bytes would come out.
bool raw(const uint8_t* data, size_t size, std::string& out, size_t expected = 0, size_t limit = size_t(1) << 32);

} // namespace tessera::inflate
