#include "util/json_util.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "util/string_util.h"

namespace ed::json {

Value::Value() = default;
Value::Value(std::nullptr_t) : type_(Type::Null) {}
Value::Value(bool b) : type_(Type::Bool), bool_(b) {}
Value::Value(int i) : type_(Type::Number), is_int_(true), int_(i) {}
Value::Value(int64_t i) : type_(Type::Number), is_int_(true), int_(i) {}
Value::Value(uint64_t i) : type_(Type::Number) {
    if (i <= static_cast<uint64_t>(INT64_MAX)) {
        is_int_ = true;
        int_ = static_cast<int64_t>(i);
    } else {
        num_ = static_cast<double>(i);
    }
}
Value::Value(double d) : type_(Type::Number), num_(d) {}
Value::Value(const char* s) : type_(Type::String), str_(s ? s : "") {}
Value::Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
Value::Value(Array a) : type_(Type::Array), arr_(std::move(a)) {}
Value::Value(Object o) : type_(Type::Object), obj_(std::move(o)) {}

bool Value::as_bool(bool def) const {
    return type_ == Type::Bool ? bool_ : def;
}

int64_t Value::as_int(int64_t def) const {
    if (type_ != Type::Number) {
        return def;
    }
    return is_int_ ? int_ : static_cast<int64_t>(num_);
}

double Value::as_double(double def) const {
    if (type_ != Type::Number) {
        return def;
    }
    return is_int_ ? static_cast<double>(int_) : num_;
}

const std::string& Value::as_string() const {
    static const std::string kEmpty;
    return type_ == Type::String ? str_ : kEmpty;
}

std::string Value::as_string(const std::string& def) const {
    return type_ == Type::String ? str_ : def;
}

std::size_t Value::size() const {
    if (type_ == Type::Array) {
        return arr_.size();
    }
    if (type_ == Type::Object) {
        return obj_.size();
    }
    return 0;
}

bool Value::empty() const {
    return size() == 0;
}

bool Value::contains(const std::string& key) const {
    return find(key) != nullptr;
}

const Value* Value::find(const std::string& key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const auto& kv : obj_) {
        if (kv.first == key) {
            return &kv.second;
        }
    }
    return nullptr;
}

Value* Value::find(const std::string& key) {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (auto& kv : obj_) {
        if (kv.first == key) {
            return &kv.second;
        }
    }
    return nullptr;
}

Value& Value::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        obj_.clear();
    }
    if (Value* existing = find(key)) {
        return *existing;
    }
    obj_.emplace_back(key, Value());
    return obj_.back().second;
}

const Value& Value::operator[](const std::string& key) const {
    static const Value kNull;
    const Value* found = find(key);
    return found ? *found : kNull;
}

void Value::set(const std::string& key, Value v) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        obj_.clear();
    }
    if (Value* existing = find(key)) {
        *existing = std::move(v);
        return;
    }
    obj_.emplace_back(key, std::move(v));
}

bool Value::erase(const std::string& key) {
    if (type_ != Type::Object) {
        return false;
    }
    for (auto it = obj_.begin(); it != obj_.end(); ++it) {
        if (it->first == key) {
            obj_.erase(it);
            return true;
        }
    }
    return false;
}

const Value& Value::operator[](std::size_t index) const {
    static const Value kNull;
    if (type_ != Type::Array || index >= arr_.size()) {
        return kNull;
    }
    return arr_[index];
}

Value& Value::operator[](std::size_t index) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arr_.clear();
    }
    if (index >= arr_.size()) {
        arr_.resize(index + 1);
    }
    return arr_[index];
}

void Value::push_back(Value v) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        arr_.clear();
    }
    arr_.push_back(std::move(v));
}

const Value* Value::find_path(const std::string& path) const {
    const Value* cur = this;
    for (const std::string& part : str::split(path, '.')) {
        if (part.empty()) {
            continue;
        }
        cur = cur->find(part);
        if (cur == nullptr) {
            return nullptr;
        }
    }
    return cur;
}

Value* Value::find_path(const std::string& path) {
    Value* cur = this;
    for (const std::string& part : str::split(path, '.')) {
        if (part.empty()) {
            continue;
        }
        cur = cur->find(part);
        if (cur == nullptr) {
            return nullptr;
        }
    }
    return cur;
}

namespace {

void append_escaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

std::string format_double(double d) {
    if (std::isnan(d) || std::isinf(d)) {
        return "null";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.15g", d);
    if (std::strtod(buf, nullptr) != d) {
        std::snprintf(buf, sizeof(buf), "%.17g", d);
    }
    return buf;
}

void dump_value(const Value& v, std::string& out, int indent, int level);

void dump_object(const Value& v, std::string& out, int indent, int level) {
    const auto& obj = v.object();
    if (obj.empty()) {
        out += "{}";
        return;
    }
    out += "{";
    const bool pretty = indent > 0;
    for (std::size_t i = 0; i < obj.size(); ++i) {
        if (pretty) {
            out += "\n";
            out.append(static_cast<std::size_t>(indent) * (level + 1), ' ');
        }
        append_escaped(out, obj[i].first);
        out += pretty ? ": " : ":";
        dump_value(obj[i].second, out, indent, level + 1);
        if (i + 1 < obj.size()) {
            out += ",";
        }
    }
    if (pretty) {
        out += "\n";
        out.append(static_cast<std::size_t>(indent) * level, ' ');
    }
    out += "}";
}

void dump_array(const Value& v, std::string& out, int indent, int level) {
    const auto& arr = v.array();
    if (arr.empty()) {
        out += "[]";
        return;
    }
    out += "[";
    const bool pretty = indent > 0;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (pretty) {
            out += "\n";
            out.append(static_cast<std::size_t>(indent) * (level + 1), ' ');
        }
        dump_value(arr[i], out, indent, level + 1);
        if (i + 1 < arr.size()) {
            out += ",";
        }
    }
    if (pretty) {
        out += "\n";
        out.append(static_cast<std::size_t>(indent) * level, ' ');
    }
    out += "]";
}

void dump_value(const Value& v, std::string& out, int indent, int level) {
    switch (v.type()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.as_bool() ? "true" : "false"; break;
        case Value::Type::Number:
            if (v.is_integer()) {
                out += std::to_string(v.as_int());
            } else {
                out += format_double(v.as_double());
            }
            break;
        case Value::Type::String: append_escaped(out, v.as_string()); break;
        case Value::Type::Array: dump_array(v, out, indent, level); break;
        case Value::Type::Object: dump_object(v, out, indent, level); break;
    }
}

}  // namespace

std::string Value::dump(int indent) const {
    std::string out;
    dump_value(*this, out, indent, 0);
    return out;
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool parse(Value& out, std::string& err) {
        skip_ws();
        if (!parse_value(out, err)) {
            return false;
        }
        skip_ws();
        if (pos_ != text_.size()) {
            err = "trailing characters at offset " + std::to_string(pos_);
            return false;
        }
        return true;
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;

    bool eof() const { return pos_ >= text_.size(); }
    char peek() const { return eof() ? '\0' : text_[pos_]; }

    void skip_ws() {
        while (!eof()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool fail(std::string& err, const std::string& msg) const {
        err = msg + " at offset " + std::to_string(pos_);
        return false;
    }

    bool parse_value(Value& out, std::string& err) {
        if (eof()) {
            return fail(err, "unexpected end of input");
        }
        switch (peek()) {
            case '{': return parse_object(out, err);
            case '[': return parse_array(out, err);
            case '"': {
                std::string s;
                if (!parse_string(s, err)) {
                    return false;
                }
                out = Value(std::move(s));
                return true;
            }
            case 't':
                if (text_.compare(pos_, 4, "true") == 0) {
                    pos_ += 4;
                    out = Value(true);
                    return true;
                }
                return fail(err, "invalid literal");
            case 'f':
                if (text_.compare(pos_, 5, "false") == 0) {
                    pos_ += 5;
                    out = Value(false);
                    return true;
                }
                return fail(err, "invalid literal");
            case 'n':
                if (text_.compare(pos_, 4, "null") == 0) {
                    pos_ += 4;
                    out = Value();
                    return true;
                }
                return fail(err, "invalid literal");
            default:
                return parse_number(out, err);
        }
    }

    bool parse_object(Value& out, std::string& err) {
        ++pos_;
        Value::Object obj;
        skip_ws();
        if (peek() == '}') {
            ++pos_;
            out = Value(std::move(obj));
            return true;
        }
        while (true) {
            skip_ws();
            if (peek() != '"') {
                return fail(err, "expected object key string");
            }
            std::string key;
            if (!parse_string(key, err)) {
                return false;
            }
            skip_ws();
            if (peek() != ':') {
                return fail(err, "expected ':' after object key");
            }
            ++pos_;
            skip_ws();
            Value val;
            if (!parse_value(val, err)) {
                return false;
            }
            obj.emplace_back(std::move(key), std::move(val));
            skip_ws();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == '}') {
                ++pos_;
                break;
            }
            return fail(err, "expected ',' or '}' in object");
        }
        out = Value(std::move(obj));
        return true;
    }

    bool parse_array(Value& out, std::string& err) {
        ++pos_;
        Value::Array arr;
        skip_ws();
        if (peek() == ']') {
            ++pos_;
            out = Value(std::move(arr));
            return true;
        }
        while (true) {
            skip_ws();
            Value val;
            if (!parse_value(val, err)) {
                return false;
            }
            arr.push_back(std::move(val));
            skip_ws();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            if (peek() == ']') {
                ++pos_;
                break;
            }
            return fail(err, "expected ',' or ']' in array");
        }
        out = Value(std::move(arr));
        return true;
    }

    static void append_utf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parse_hex4(uint32_t& out, std::string& err) {
        if (pos_ + 4 > text_.size()) {
            return fail(err, "truncated \\u escape");
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return fail(err, "invalid hex digit in \\u escape");
            }
        }
        out = value;
        return true;
    }

    bool parse_string(std::string& out, std::string& err) {
        ++pos_;
        out.clear();
        while (!eof()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (eof()) {
                return fail(err, "truncated escape sequence");
            }
            const char esc = text_[pos_++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!parse_hex4(cp, err)) {
                        return false;
                    }
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // High surrogate: expect a following low surrogate.
                        if (pos_ + 2 <= text_.size() && text_[pos_] == '\\' &&
                            text_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            uint32_t low = 0;
                            if (!parse_hex4(low, err)) {
                                return false;
                            }
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) +
                                     (low - 0xDC00);
                            }
                        }
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    return fail(err, "invalid escape sequence");
            }
        }
        return fail(err, "unterminated string");
    }

    bool parse_number(Value& out, std::string& err) {
        const std::size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        bool saw_digit = false;
        while (!eof() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
            saw_digit = true;
        }
        bool is_int = true;
        if (peek() == '.') {
            is_int = false;
            ++pos_;
            while (!eof() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
                saw_digit = true;
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            is_int = false;
            ++pos_;
            if (peek() == '+' || peek() == '-') {
                ++pos_;
            }
            while (!eof() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
        }
        if (!saw_digit) {
            return fail(err, "invalid number");
        }
        const std::string token = text_.substr(start, pos_ - start);
        if (is_int) {
            errno = 0;
            char* end = nullptr;
            const long long v = std::strtoll(token.c_str(), &end, 10);
            if (errno == 0 && end != nullptr && *end == '\0') {
                out = Value(static_cast<int64_t>(v));
                return true;
            }
        }
        char* end = nullptr;
        const double d = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0') {
            return fail(err, "invalid number");
        }
        out = Value(d);
        return true;
    }
};

}  // namespace

Value Value::parse(const std::string& text, std::string* error) {
    Parser parser(text);
    Value out;
    std::string err;
    if (!parser.parse(out, err)) {
        if (error != nullptr) {
            *error = err;
        }
        return Value();
    }
    if (error != nullptr) {
        error->clear();
    }
    return out;
}

void deep_merge(Value& base, const Value& overlay) {
    if (base.is_object() && overlay.is_object()) {
        for (const auto& kv : overlay.object()) {
            if (Value* existing = base.find(kv.first)) {
                deep_merge(*existing, kv.second);
            } else {
                base.set(kv.first, kv.second);
            }
        }
        return;
    }
    base = overlay;
}

Value get_or(const Value& root, const std::string& path, const Value& def) {
    const Value* found = root.find_path(path);
    return found ? *found : def;
}

}  // namespace ed::json
