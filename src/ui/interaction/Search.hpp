#pragma once

#include <string>

namespace tessera {

/// Whether a searchable entry matches what was typed: any of its name, description or keywords contains the
/// query, ignoring case ("delay" finds Echo through its keywords). An empty query matches everything.
bool searchMatches(const std::string& name, const std::string& description, const std::string& keywords,
                   const std::string& query);

} // namespace tessera
