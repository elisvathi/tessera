#pragma once

#include <string>
#include <utility>
#include <vector>

/// A small XML reader for project files other programs write (DAWproject): elements, attributes and text, with the
/// standard entities. No DTDs or namespaces; comments, processing instructions and CDATA are understood.
namespace tessera::xml {

struct Node {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<Node> children;
    std::string text; // the text directly inside it, joined

    /// The attribute's value, or null when it has none.
    const std::string* attr(const char* key) const;
    /// The attribute as a number, or `fallback` when it is missing or not a number.
    double number(const char* key, double fallback = 0) const;
    /// The attribute as true/false ("true" or "1"), or `fallback` when missing.
    bool flag(const char* key, bool fallback = false) const;
    /// The first child with this name, or null.
    const Node* child(const char* childName) const;
};

/// Reads a whole document into `root` (its top element). On failure returns false with `error` saying where.
bool parse(const std::string& text, Node& root, std::string* error = nullptr);

} // namespace tessera::xml
