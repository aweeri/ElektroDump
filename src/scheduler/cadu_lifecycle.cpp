#include "scheduler/cadu_lifecycle.h"

#include <dirent.h>

#include <cstdio>

#include "core/config.h"
#include "core/logging.h"
#include "util/fs_util.h"

namespace ed::scheduler {

bool CaduLifecycle::configure(const Config& cfg, std::string* error) {
    (void)error;
    root_ = cfg.get_string("storage.root", ".");
    spool_root_ = fs::join(root_, "spool/xrit");
    ggak_spool_root_ = cfg.get_string("ggak.spool_dir", fs::join(root_, "spool/ggak"));

    failed_cadu_hours_ =
        static_cast<int>(cfg.get_int("storage.retention.failed_cadu_hours", 24));
    if (failed_cadu_hours_ < 1) {
        failed_cadu_hours_ = 1;
    }
    ggak_days_ = static_cast<int>(cfg.get_int("storage.retention.ggak_days", 90));
    if (ggak_days_ < 1) {
        ggak_days_ = 1;
    }
    return true;
}

std::string CaduLifecycle::begin_capture(PipelineMode mode,
                                         const std::string& capture_id) {
    const std::string path = fs::join(spool_root_, capture_id);
    fs::mkdir_p(path);

    std::lock_guard<std::mutex> lock(mutex_);
    Capture capture;
    capture.path = path;
    capture.mode = mode;
    capture.state = CaptureState::Spooling;
    captures_[capture_id] = capture;
    stats_.spools_active = count_active_locked();
    return path;
}

uint64_t CaduLifecycle::count_active_locked() const {
    uint64_t active = 0;
    for (const auto& kv : captures_) {
        const CaptureState st = kv.second.state;
        if (st == CaptureState::Spooling || st == CaptureState::Decoding ||
            st == CaptureState::Verifying) {
            ++active;
        }
    }
    return active;
}

void CaduLifecycle::end_capture(const std::string& capture_id, bool products_ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = captures_.find(capture_id);
    if (it == captures_.end()) {
        return;
    }
    it->second.state = CaptureState::Verifying;
    if (products_ok) {
        fs::remove_all(it->second.path);
        ++stats_.spools_deleted;
        captures_.erase(it);
    } else {
        it->second.state = CaptureState::Retained;
        it->second.ended = std::time(nullptr);
        ++stats_.spools_retained;
    }
    stats_.spools_active = count_active_locked();
}

void CaduLifecycle::janitor() { janitor_at(std::time(nullptr)); }

void CaduLifecycle::janitor_at(std::time_t now) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::time_t cutoff =
        now - static_cast<std::time_t>(failed_cadu_hours_) * 3600;
    for (auto it = captures_.begin(); it != captures_.end();) {
        if (it->second.state == CaptureState::Retained &&
            it->second.ended != 0 && it->second.ended <= cutoff) {
            fs::remove_all(it->second.path);
            ++stats_.spools_deleted;
            it = captures_.erase(it);
        } else {
            ++it;
        }
    }
    stats_.spools_active = count_active_locked();
    prune_ggak(now);
}

void CaduLifecycle::prune_ggak(std::time_t now) {
    DIR* dir = ::opendir(ggak_spool_root_.c_str());
    if (dir == nullptr) {
        return;
    }
    const std::time_t cutoff = now - static_cast<std::time_t>(ggak_days_) * 86400;
    while (struct dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.size() != 8) {
            continue;
        }
        int year = 0;
        int month = 0;
        int day = 0;
        if (std::sscanf(name.c_str(), "%4d%2d%2d", &year, &month, &day) != 3) {
            continue;
        }
        std::tm tm {};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        const std::time_t t = ::timegm(&tm);
        if (t != static_cast<std::time_t>(-1) && t < cutoff) {
            fs::remove_all(fs::join(ggak_spool_root_, name));
        }
    }
    ::closedir(dir);
}

CaduLifecycle::Stats CaduLifecycle::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

}  // namespace ed::scheduler
