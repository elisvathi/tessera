#include "XmlRead.hpp"

#include <cstdlib>
#include <cstring>
#include <locale>
#include <sstream>

namespace tessera::xml {

namespace {

constexpr int kMaxDepth = 256;

/// Walks the document text once, building nodes.
class Parser {
  public:
    explicit Parser(const std::string& text) : s(text) {}

    bool document(Node& root) {
        if (s.compare(0, 3, "\xEF\xBB\xBF") == 0)
            at = 3; // a UTF-8 byte order mark
        if (!skipMisc())
            return false;
        if (at >= s.size() || s[at] != '<')
            return fail("no element");
        if (!element(root, 0))
            return false;
        if (!skipMisc())
            return false;
        return at >= s.size() || fail("text after the top element");
    }
    std::string error;

  private:
    bool fail(const char* what) {
        size_t line = 1;
        for (size_t i = 0; i < at && i < s.size(); i++)
            line += s[i] == '\n' ? 1 : 0;
        error = std::string(what) + " on line " + std::to_string(line);
        return false;
    }
    bool starts(const char* t) const { return s.compare(at, std::strlen(t), t) == 0; }
    void spaces() {
        while (at < s.size() && (s[at] == ' ' || s[at] == '\t' || s[at] == '\n' || s[at] == '\r'))
            at++;
    }
    /// Skips past `end`; false when it never comes.
    bool skipPast(const char* end) {
        size_t found = s.find(end, at);
        if (found == std::string::npos)
            return fail("unclosed markup");
        at = found + std::strlen(end);
        return true;
    }
    /// Skips spaces, comments, processing instructions and a DOCTYPE between elements.
    bool skipMisc() {
        for (;;) {
            spaces();
            if (starts("<?")) {
                if (!skipPast("?>"))
                    return false;
            } else if (starts("<!--")) {
                if (!skipPast("-->"))
                    return false;
            } else if (starts("<!DOCTYPE")) {
                if (!skipPast(">"))
                    return false;
            } else {
                return true;
            }
        }
    }
    static bool nameChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
               c == '.' || c == ':' || (unsigned char)c >= 0x80;
    }
    std::string name() {
        size_t from = at;
        while (at < s.size() && nameChar(s[at]))
            at++;
        return s.substr(from, at - from);
    }
    /// Text with its entities turned back into characters.
    static void unescape(const std::string& raw, std::string& out) {
        for (size_t i = 0; i < raw.size(); i++) {
            if (raw[i] != '&') {
                out += raw[i];
                continue;
            }
            size_t semi = raw.find(';', i);
            if (semi == std::string::npos) {
                out += raw[i];
                continue;
            }
            std::string e = raw.substr(i + 1, semi - i - 1);
            if (e == "amp")
                out += '&';
            else if (e == "lt")
                out += '<';
            else if (e == "gt")
                out += '>';
            else if (e == "quot")
                out += '"';
            else if (e == "apos")
                out += '\'';
            else if (!e.empty() && e[0] == '#') {
                unsigned long code = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, nullptr, 16)
                                                                                 : std::strtoul(e.c_str() + 1, nullptr, 10);
                // As UTF-8.
                if (code < 0x80) {
                    out += char(code);
                } else if (code < 0x800) {
                    out += char(0xC0 | (code >> 6));
                    out += char(0x80 | (code & 0x3F));
                } else if (code < 0x10000) {
                    out += char(0xE0 | (code >> 12));
                    out += char(0x80 | ((code >> 6) & 0x3F));
                    out += char(0x80 | (code & 0x3F));
                } else {
                    out += char(0xF0 | (code >> 18));
                    out += char(0x80 | ((code >> 12) & 0x3F));
                    out += char(0x80 | ((code >> 6) & 0x3F));
                    out += char(0x80 | (code & 0x3F));
                }
            } else {
                out += raw.substr(i, semi - i + 1); // unknown: kept as written
            }
            i = semi;
        }
    }
    bool element(Node& node, int depth) {
        if (depth > kMaxDepth)
            return fail("elements nested too deep");
        at++; // '<'
        node.name = name();
        if (node.name.empty())
            return fail("an element with no name");
        for (;;) {
            spaces();
            if (at >= s.size())
                return fail("unclosed element");
            if (starts("/>")) {
                at += 2;
                return true;
            }
            if (s[at] == '>') {
                at++;
                break;
            }
            std::string key = name();
            if (key.empty())
                return fail("a bad attribute");
            spaces();
            if (at >= s.size() || s[at] != '=')
                return fail("an attribute with no value");
            at++;
            spaces();
            if (at >= s.size() || (s[at] != '"' && s[at] != '\''))
                return fail("an attribute value with no quotes");
            char quote = s[at++];
            size_t end = s.find(quote, at);
            if (end == std::string::npos)
                return fail("an unclosed attribute value");
            std::string value;
            unescape(s.substr(at, end - at), value);
            node.attrs.emplace_back(std::move(key), std::move(value));
            at = end + 1;
        }
        // Content: text, children, comments and CDATA, up to the closing tag.
        for (;;) {
            size_t lt = s.find('<', at);
            if (lt == std::string::npos)
                return fail("unclosed element");
            unescape(s.substr(at, lt - at), node.text);
            at = lt;
            if (starts("</")) {
                at += 2;
                if (name() != node.name)
                    return fail("a closing tag that doesn't match");
                spaces();
                if (at >= s.size() || s[at] != '>')
                    return fail("a bad closing tag");
                at++;
                return true;
            }
            if (starts("<!--")) {
                if (!skipPast("-->"))
                    return false;
            } else if (starts("<![CDATA[")) {
                at += 9;
                size_t end = s.find("]]>", at);
                if (end == std::string::npos)
                    return fail("unclosed CDATA");
                node.text += s.substr(at, end - at);
                at = end + 3;
            } else if (starts("<?")) {
                if (!skipPast("?>"))
                    return false;
            } else {
                node.children.emplace_back();
                if (!element(node.children.back(), depth + 1))
                    return false;
            }
        }
    }

    const std::string& s;
    size_t at = 0;
};

} // namespace

const std::string* Node::attr(const char* key) const {
    for (const auto& [k, v] : attrs)
        if (k == key)
            return &v;
    return nullptr;
}

double Node::number(const char* key, double fallback) const {
    const std::string* v = attr(key);
    if (!v || v->empty())
        return fallback;
    // In the C locale whatever the user's is: files write "1.5", never "1,5".
    std::istringstream in(*v);
    in.imbue(std::locale::classic());
    double d = 0;
    in >> d;
    return in.fail() ? fallback : d;
}

bool Node::flag(const char* key, bool fallback) const {
    const std::string* v = attr(key);
    if (!v)
        return fallback;
    return *v == "true" || *v == "1";
}

const Node* Node::child(const char* childName) const {
    for (const Node& c : children)
        if (c.name == childName)
            return &c;
    return nullptr;
}

bool parse(const std::string& text, Node& root, std::string* error) {
    root = Node();
    Parser p(text);
    bool ok = p.document(root);
    if (!ok && error)
        *error = p.error;
    return ok;
}

} // namespace tessera::xml
