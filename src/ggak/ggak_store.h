// ElektroDump — GGAK CADU spool writer (PLAN.md D2/D7).
// Writes validated 224-byte frames to <spool_root>/<YYYYMMDD>/ggak_<HHMMSS>.cadu,
// rotating on a fixed interval (hourly by default). Buffered and flushed in large
// chunks so the DSP thread never pays a syscall per frame.
#pragma once

#include <cstdint>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#include "ggak/ggak_frame.h"

namespace ed::ggak {

class GgakStore {
public:
    void configure(const std::string& spool_root, int rotate_seconds = 3600);

    // Append one frame. Returns false only on a hard I/O error.
    bool write(const GgakFrame& frame);

    void flush();
    void close();

    uint64_t frames_written() const { return frames_written_; }
    const std::string& current_path() const { return current_path_; }
    const std::string& last_error() const { return last_error_; }

private:
    bool ensure_file();

    std::string spool_root_;
    int rotate_seconds_ = 3600;
    std::ofstream file_;
    std::string current_path_;
    std::string last_error_;
    std::time_t current_bucket_ = 0;
    std::vector<uint8_t> buffer_;
    uint64_t frames_written_ = 0;
};

}  // namespace ed::ggak
