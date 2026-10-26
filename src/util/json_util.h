// ElektroDump — minimal, dependency-free JSON value + parser.
// Covers what the config system and REST layer need (objects, arrays, strings,
// numbers, bools, null) and keeps object insertion order so dumps stay readable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ed::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    using Array = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;

    Value();
    Value(std::nullptr_t);
    Value(bool b);
    Value(int i);
    Value(int64_t i);
    Value(uint64_t i);
    Value(double d);
    Value(const char* s);
    Value(std::string s);
    Value(Array a);
    Value(Object o);

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_integer() const { return type_ == Type::Number && is_int_; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool(bool def = false) const;
    int64_t as_int(int64_t def = 0) const;
    double as_double(double def = 0.0) const;
    const std::string& as_string() const;
    std::string as_string(const std::string& def) const;

    std::size_t size() const;
    bool empty() const;

    bool contains(const std::string& key) const;
    const Value* find(const std::string& key) const;
    Value* find(const std::string& key);
    Value& operator[](const std::string& key);              // creates entry
    const Value& operator[](const std::string& key) const;  // null if missing
    void set(const std::string& key, Value v);
    bool erase(const std::string& key);
    const Object& object() const { return obj_; }
    Object& object() { return obj_; }

    const Value& operator[](std::size_t index) const;
    Value& operator[](std::size_t index);
    void push_back(Value v);
    const Array& array() const { return arr_; }
    Array& array() { return arr_; }

    const Value* find_path(const std::string& path) const;
    Value* find_path(const std::string& path);

    // Pretty-print when indent > 0, compact when indent == 0.
    std::string dump(int indent = 2) const;

    static Value parse(const std::string& text, std::string* error = nullptr);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    bool is_int_ = false;
    int64_t int_ = 0;
    double num_ = 0.0;
    std::string str_;
    Array arr_;
    Object obj_;
};

// Recursively merge `overlay` into `base`; objects merge key-by-key, everything
// else (including arrays) is replaced wholesale.
void deep_merge(Value& base, const Value& overlay);

Value get_or(const Value& root, const std::string& path, const Value& def);

}  // namespace ed::json
