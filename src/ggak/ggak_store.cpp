#include "ggak/ggak_store.h"

#include <cstring>

#include "util/fs_util.h"

namespace ed::ggak {

namespace {

constexpr std::size_t kFlushThreshold = 64 * 1024;

}  // namespace

void GgakStore::configure(const std::string& spool_root, int rotate_seconds) {
    close();
    spool_root_ = spool_root;
    rotate_seconds_ = rotate_seconds > 0 ? rotate_seconds : 3600;
    current_bucket_ = 0;
    current_path_.clear();
    last_error_.clear();
    buffer_.clear();
    frames_written_ = 0;
}

bool GgakStore::ensure_file() {
    const std::time_t now = std::time(nullptr);
    const std::time_t bucket =
        (now / static_cast<std::time_t>(rotate_seconds_)) *
        static_cast<std::time_t>(rotate_seconds_);
    if (file_.is_open() && bucket == current_bucket_) {
        return true;
    }

    flush();
    if (file_.is_open()) {
        file_.close();
    }

    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char day[16] = {0};
    char stamp[16] = {0};
    std::strftime(day, sizeof(day), "%Y%m%d", &tm);
    std::strftime(stamp, sizeof(stamp), "%H%M%S", &tm);

    const std::string dir = fs::join(spool_root_, day);
    if (!fs::mkdir_p(dir)) {
        last_error_ = "cannot create spool directory: " + dir;
        return false;
    }

    current_path_ = fs::join(dir, std::string("ggak_") + stamp + ".cadu");
    file_.open(current_path_, std::ios::binary | std::ios::app);
    if (!file_.is_open()) {
        last_error_ = "cannot open CADU spool: " + current_path_;
        return false;
    }
    current_bucket_ = bucket;
    return true;
}

bool GgakStore::write(const GgakFrame& frame) {
    if (!ensure_file()) {
        return false;
    }
    buffer_.insert(buffer_.end(), frame.raw, frame.raw + kGgakFrameBytes);
    ++frames_written_;
    if (buffer_.size() >= kFlushThreshold) {
        flush();
    }
    return true;
}

void GgakStore::flush() {
    if (!file_.is_open() || buffer_.empty()) {
        return;
    }
    file_.write(reinterpret_cast<const char*>(buffer_.data()),
                static_cast<std::streamsize>(buffer_.size()));
    file_.flush();
    buffer_.clear();
}

void GgakStore::close() {
    flush();
    if (file_.is_open()) {
        file_.close();
    }
}

}  // namespace ed::ggak
