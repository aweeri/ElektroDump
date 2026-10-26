// ElektroDump — GGAK pipeline (PLAN.md D2, milestone M3).
// Primary consumer of the IQ ring: PM demod, 224-byte frame validation via the
// sum-CRC, CADU spool writes, and SNR/lock/BER publication. IQ is fanned out to
// the waterfall ring so the FFT producer never competes for the primary ring.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "core/pipeline.h"
#include "dsp/demod_pm.h"
#include "ggak/ggak_frame.h"
#include "ggak/ggak_store.h"

namespace ed {

class GgakPipeline : public IPipeline {
public:
    GgakPipeline();
    ~GgakPipeline() override;

    GgakPipeline(const GgakPipeline&) = delete;
    GgakPipeline& operator=(const GgakPipeline&) = delete;

    PipelineMode mode() const override { return PipelineMode::GGAK; }
    bool configure(const Config& cfg, const PipelineParams& params) override;
    void attach_io(const PipelineIo& io) override;
    void start() override;
    void stop() override;
    PipelineStats stats() const override;

    // Diagnostics for the offline path / tests.
    double snr_db() const { return snr_db_.load(std::memory_order_relaxed); }
    bool locked() const { return locked_.load(std::memory_order_relaxed); }
    const std::string& spool_dir() const { return spool_dir_; }

private:
    void dsp_loop();
    void publish_stats();

    const Config* config_ = nullptr;
    PipelineParams params_;
    PipelineIo io_;

    dsp::PmDemod demod_;
    ggak::GgakFrameParser parser_;
    ggak::GgakStore store_;

    std::string spool_dir_ = "data/spool/ggak";
    int rotate_seconds_ = 3600;
    double time_offset_seconds_ = -10800.0;

    std::thread thread_;
    std::atomic<bool> running_{false};

    std::atomic<double> snr_db_{0.0};
    std::atomic<double> ber_{0.0};
    std::atomic<bool> locked_{false};
    std::atomic<uint64_t> cadus_total_{0};
    std::atomic<uint64_t> cadus_bad_{0};

    std::chrono::steady_clock::time_point last_valid_frame_{};
    std::chrono::steady_clock::time_point last_stats_{};
};

}  // namespace ed
