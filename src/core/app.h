// ElektroDump — top-level orchestrator.
#pragma once

#include <atomic>
#include <chrono>
#include <complex>
#include <memory>
#include <mutex>
#include <string>

#include "core/config.h"
#include "core/metrics.h"
#include "core/pipeline.h"
#include "core/ring_buffer.h"
#include "dsp/waterfall.h"
#include "ggak/ggak_science.h"
#include "output/product_store.h"
#include "scheduler/cadu_lifecycle.h"
#include "scheduler/scheduler.h"
#include "sdr/sdr_source.h"
#include "web/snr_history.h"
#include "web/web_server.h"
#include "web/webhook.h"

namespace ed {

namespace image {
class ProductBuilder;
}

inline constexpr const char* kAppName = "elektrodump";
inline constexpr const char* kVersion = "0.1.0";

class App {
public:
    App();
    ~App();

    // Load + validate config, configure logging; false on fatal error (in *error).
    bool initialize(const std::string& config_path, std::string* error);

    // Daemon: open the SDR, start the FFT/waterfall producer, serve the status API, block until signalled.
    int run();

    // Web server only (offline browsing); no SDR is opened.
    int serve();

    void request_stop();
    void shutdown();

    const Config& config() const { return config_; }
    Metrics& metrics() { return metrics_; }
    const Metrics& metrics() const { return metrics_; }
    bool running() const { return running_.load(std::memory_order_relaxed); }

private:
    sdr::SdrConfig sdr_config() const;

    // Serialised by pipeline_mutex_.
    void start_pipeline(PipelineMode mode, const scheduler::ScheduleEntry* entry);

    // Scheduler transition: retune the SDR, swap the pipeline, update the mode.
    void switch_mode(PipelineMode mode, const scheduler::ScheduleEntry* entry);

    PipelineParams ggak_params() const;

    PipelineParams xrit_params() const;

    // Shared startup for run/serve: start the web server, block until stopped.
    int run_loop(bool with_sdr);

    Config config_;
    Metrics metrics_;
    PipelineMode mode_ = PipelineMode::GGAK;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    bool initialized_ = false;

    std::unique_ptr<sdr::ISdrSource> sdr_;
    // Sample rate actually in effect on the device (a backend may quantise the
    // requested rate to one it supports), so pipelines are configured with what
    // the radio is really producing.
    uint32_t active_sample_rate_hz_ = 0;
    SpscRing<std::complex<float>> iq_ring_{262144};
    // Demod fans IQ out to this ring for the FFT/waterfall producer.
    SpscRing<std::complex<float>> waterfall_ring_{262144};
    dsp::WaterfallProducer waterfall_;
    web::WebServer web_;
    std::chrono::steady_clock::time_point start_time_;
    std::string sdr_error_;

    std::unique_ptr<IPipeline> pipeline_;

    std::unique_ptr<image::ProductBuilder> products_;

    // Product store for the web API when no builder is running (serve/GGAK).
    std::unique_ptr<output::ProductStore> store_;

    // In-memory GGAK science series + the product-created webhook.
    std::unique_ptr<ggak::GgakScienceStore> ggak_science_;
    std::unique_ptr<web::WebhookDispatcher> webhook_;
    // One-second SNR ring behind /api/v1/history/snr.
    std::unique_ptr<web::SnrHistory> snr_history_;

    // pipeline_mutex_ serialises mode switches against run_loop startup/shutdown.
    std::mutex pipeline_mutex_;
    std::unique_ptr<scheduler::Scheduler> scheduler_;
    std::unique_ptr<scheduler::CaduLifecycle> cadu_;
    std::atomic<int> mode_code_{static_cast<int>(PipelineMode::GGAK)};
};

}  // namespace ed
