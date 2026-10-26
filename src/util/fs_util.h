// ElektroDump — POSIX filesystem helpers (no std::filesystem dependency).
#pragma once

#include <cstdint>
#include <string>

namespace ed::fs {

bool exists(const std::string& path);
bool is_directory(const std::string& path);
bool is_regular_file(const std::string& path);
bool is_writable(const std::string& path);

// Create a directory and all missing parents; true if it exists afterwards.
bool mkdir_p(const std::string& path);

std::string read_file(const std::string& path, bool* ok = nullptr);
bool write_file(const std::string& path, const std::string& data);

// Write to `<path>.tmp`, fsync, then rename over `path`.
bool atomic_write(const std::string& path, const std::string& data);

uint64_t file_size(const std::string& path);
bool remove_all(const std::string& path);
uint64_t free_space_mb(const std::string& path);

std::string join(const std::string& a, const std::string& b);
std::string dirname(const std::string& path);
std::string basename(const std::string& path);

}  // namespace ed::fs
