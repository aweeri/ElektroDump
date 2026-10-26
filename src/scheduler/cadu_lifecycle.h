// ElektroDump — CADU spool lifecycle (spec §3.5, milestone M8).
// Owns the per-capture xRIT spool directory and its state machine (Spooling ->
// Decoding -> Verifying -> Deleted|Retained). Success deletes the spool; failure
// retains it for storage.retention.failed_cadu_hours. The janitor also prunes old
// GGAK spool day-directories.
#pragma once

#include <cstdint>
#include <ctime>
#include <map>
#include <mutex>
#include <string>

#include "core/pipeline.h"

namespace ed {
class Config;
}

namespace ed::scheduler {

class CaduLifecycle {
public:
    enum class CaptureState { Spooling, Decoding, Verifying, Deleted, Retained };

    bool configure(const Config& cfg, std::string* error);

    // Create <storage.root>/spool/xrit/<capture_id>/ and return its path.
    std::string begin_capture(PipelineMode mode, const std::string& capture_id);

    // Success deletes the spool; failure keeps it for failed_cadu_hours.
    void end_capture(const std::string& capture_id, bool products_ok);

    // Delete expired failed spools, prune old GGAK spool days.
    void janitor();
    // Deterministic variant for tests.
    void janitor_at(std::time_t now);

    struct Stats {
        uint64_t spools_active = 0;
        uint64_t spools_deleted = 0;
        uint64_t spools_retained = 0;
    };
    Stats stats() const;

    const std::string& spool_root() const { return spool_root_; }

private:
    struct Capture {
        std::string path;
        PipelineMode mode = PipelineMode::GGAK;
        CaptureState state = CaptureState::Spooling;
        std::time_t ended = 0;
    };

    void prune_ggak(std::time_t now);
    // Count captures still in Spooling/Decoding/Verifying. Caller holds mutex_.
    uint64_t count_active_locked() const;

    std::string root_;
    std::string spool_root_;
    std::string ggak_spool_root_;
    int failed_cadu_hours_ = 24;
    int ggak_days_ = 90;

    mutable std::mutex mutex_;
    std::map<std::string, Capture> captures_;
    Stats stats_;
};

}  // namespace ed::scheduler
