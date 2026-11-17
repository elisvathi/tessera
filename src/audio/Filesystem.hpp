#pragma once

// std::filesystem needs macOS 10.15, and Rack builds plugins for 10.9 (the VCV Library's builds too). Rack ships
// ghc::filesystem, the same interface, which works there; it also takes std::string paths as UTF-8 on Windows.
// Open files from a path with fs::ifstream and fs::ofstream, so non-ASCII names open on Windows too.
#include <ghc/filesystem.hpp>

namespace tessera {
namespace fs = ghc::filesystem;
} // namespace tessera
