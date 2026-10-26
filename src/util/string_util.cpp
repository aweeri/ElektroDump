#include "util/string_util.h"

#include <algorithm>
#include <cctype>

namespace ed::str {

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> split(const std::string& s, const std::string& delim) {
    std::vector<std::string> out;
    if (delim.empty()) {
        out.push_back(s);
        return out;
    }
    std::size_t pos = 0;
    while (true) {
        const std::size_t hit = s.find(delim, pos);
        if (hit == std::string::npos) {
            out.push_back(s.substr(pos));
            break;
        }
        out.push_back(s.substr(pos, hit - pos));
        pos = hit + delim.size();
    }
    return out;
}

std::string trim(const std::string& s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string replace_all(const std::string& s, const std::string& from,
                        const std::string& to) {
    if (from.empty()) {
        return s;
    }
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    while (true) {
        const std::size_t hit = s.find(from, pos);
        if (hit == std::string::npos) {
            out.append(s, pos, std::string::npos);
            break;
        }
        out.append(s, pos, hit - pos);
        out.append(to);
        pos = hit + from.size();
    }
    return out;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string to_upper(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

std::string join(const std::vector<std::string>& parts,
                 const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out.append(sep);
        }
        out.append(parts[i]);
    }
    return out;
}

}  // namespace ed::str
