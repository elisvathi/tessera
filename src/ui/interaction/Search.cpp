#include "Search.hpp"

#include <algorithm>
#include <cctype>

namespace tessera {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

} // namespace

bool searchMatches(const std::string& name, const std::string& description, const std::string& keywords,
                   const std::string& query) {
    std::string q = lower(query);
    // Leading and trailing spaces are typing, not meaning.
    q.erase(0, q.find_first_not_of(' '));
    q.erase(q.find_last_not_of(' ') + 1);
    if (q.empty())
        return true;
    return lower(name).find(q) != std::string::npos || lower(description).find(q) != std::string::npos ||
           lower(keywords).find(q) != std::string::npos;
}

} // namespace tessera
