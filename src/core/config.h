// ElektroDump — versioned config: embedded defaults + user override.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "util/json_util.h"

namespace ed {

class Config {
public:
    static constexpr int kVersion = 1;

    Config();

    // Embedded defaults only.
    static Config defaults();

    // Embedded defaults deep-merged with `path` when it exists; on parse or
    // migration failure returns defaults and sets `error`.
    static Config load(const std::string& path, std::string* error);

    // Validate types, ranges and cross-field rules; true when there are no errors.
    bool validate(std::vector<std::string>* errors,
                  std::vector<std::string>* warnings) const;

    bool save(const std::string& path, std::string* error) const;

    std::string dump(int indent = 2) const;

    const json::Value& root() const { return root_; }
    json::Value& root() { return root_; }

    bool loaded_from_file() const { return loaded_from_file_; }
    const std::string& source_path() const { return source_path_; }

    std::string get_string(const std::string& path,
                           const std::string& def = "") const;
    int64_t get_int(const std::string& path, int64_t def = 0) const;
    double get_double(const std::string& path, double def = 0.0) const;
    bool get_bool(const std::string& path, bool def = false) const;

private:
    json::Value root_;
    bool loaded_from_file_ = false;
    std::string source_path_;

    static bool migrate(json::Value& root, std::string* error);
};

}  // namespace ed
