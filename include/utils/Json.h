#pragma once

/**
 * Json.h
 * A small JSON reader, enough for the data the ForgeScript library hands back (script graphs).
 * Not a general-purpose library: no comments, no big-number precision, UTF-8 passed through.
 */

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace Forge {
namespace Json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> items;                           // Array
    std::vector<std::pair<std::string, Value>> members; // Object

    bool isNull() const { return type == Type::Null; }
    size_t size() const { return type == Type::Array ? items.size() : members.size(); }

    /// Member of an object, or a null value if there isn't one (so lookups can be chained safely).
    const Value& operator[](const char* key) const {
        static const Value none;
        for (const auto& m : members) {
            if (m.first == key) return m.second;
        }
        return none;
    }

    /// Element of an array, or a null value if out of range.
    const Value& at(size_t i) const {
        static const Value none;
        return (type == Type::Array && i < items.size()) ? items[i] : none;
    }

    std::string str(const std::string& fallback = "") const { return type == Type::String ? string_ : fallback; }
    int integer(int fallback = 0) const { return type == Type::Number ? static_cast<int>(number_) : fallback; }
    bool boolean(bool fallback = false) const { return type == Type::Bool ? boolean_ : fallback; }
};

namespace detail {

struct Reader {
    const std::string& s;
    size_t i = 0;
    std::string error;

    explicit Reader(const std::string& text) : s(text) {}

    void skip() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }

    bool fail(const char* what) {
        if (error.empty()) error = std::string(what) + " at offset " + std::to_string(i);
        return false;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("expected a string");
        ++i;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 > s.size()) return fail("bad \\u escape");
                    unsigned cp = static_cast<unsigned>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                    i += 4;
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }

    bool value(Value& out) {
        skip();
        if (i >= s.size()) return fail("unexpected end");
        char c = s[i];
        if (c == '{') {
            ++i;
            out.type = Value::Type::Object;
            skip();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                skip();
                std::string key;
                if (!string(key)) return false;
                skip();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                ++i;
                Value v;
                if (!value(v)) return false;
                out.members.emplace_back(std::move(key), std::move(v));
                skip();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i;
            out.type = Value::Type::Array;
            skip();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                Value v;
                if (!value(v)) return false;
                out.items.push_back(std::move(v));
                skip();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            out.type = Value::Type::String;
            return string(out.string_);
        }
        if (s.compare(i, 4, "true") == 0) { i += 4; out.type = Value::Type::Bool; out.boolean_ = true; return true; }
        if (s.compare(i, 5, "false") == 0) { i += 5; out.type = Value::Type::Bool; out.boolean_ = false; return true; }
        if (s.compare(i, 4, "null") == 0) { i += 4; out.type = Value::Type::Null; return true; }
        char* end = nullptr;
        double n = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return fail("unexpected character");
        i = static_cast<size_t>(end - s.c_str());
        out.type = Value::Type::Number;
        out.number_ = n;
        return true;
    }
};

} // namespace detail

/// Parse `text`. On failure returns false and sets `error`.
inline bool parse(const std::string& text, Value& out, std::string& error) {
    detail::Reader r(text);
    out = Value();
    if (!r.value(out)) {
        error = r.error;
        return false;
    }
    r.skip();
    if (r.i != text.size()) {
        error = "extra text after the value";
        return false;
    }
    return true;
}

} // namespace Json
} // namespace Forge
