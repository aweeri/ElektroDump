// ElektroDump — small, dependency-free string helpers.
#pragma once

#include <string>
#include <vector>

namespace ed::str {

// Split on a single delimiter. Empty fields are preserved.
std::vector<std::string> split(const std::string& s, char delim);

std::vector<std::string> split(const std::string& s, const std::string& delim);

std::string trim(const std::string& s);

std::string replace_all(const std::string& s, const std::string& from,
                        const std::string& to);

bool starts_with(const std::string& s, const std::string& prefix);
bool ends_with(const std::string& s, const std::string& suffix);

std::string to_lower(const std::string& s);
std::string to_upper(const std::string& s);

std::string join(const std::vector<std::string>& parts, const std::string& sep);

}  // namespace ed::str
