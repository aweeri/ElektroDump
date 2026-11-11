// ElektroDump — embedded web server (PLAN.md D8/D9).
// Owns the router and HTTP transport and registers the API endpoint groups;
// adding an endpoint means one register_* call here plus a file under src/web/.
#pragma once

#include <atomic>
#include <chrono>
#include <complex>
#include <functional>
#include <string>

#include "core/config.h"
#include "core/metrics.h"
#include "core/pipeline.h"
#include "dsp/waterfall.h"
#include "ggak/ggak_science.h"
#include "output/product_store.h"
#include "scheduler/cadu_lifecycle.h"
#include "scheduler/scheduler.h"
#include "sdr/sdr_source.h"
#include "web/http_server.h"
#include "web/router.h"
#include "web/webhook.h"

namespace ed::web {

// Read-only view of the daemon handed to the API handlers; pointers are owned
// by App and outlive the server. PipelineSnapshot is thread-safe: the scheduler
// may swap the pipeline at any time, so handlers must not cache a pointer.
struct PipelineSnapshot {
    bool present = false;
    PipelineStats stats;
};

struct WebContext {
    const Config* config = nullptr;
    const Metrics* metrics = nullptr;
    const sdr::ISdrSource* sdr = nullptr;
    const dsp::WaterfallProducer* waterfall = nullptr;
    const IPipeline* pipeline = nullptr;
    const output::ProductStore* products = nullptr;
    // M9: in-memory GGAK science series and the webhook dispatcher.
    const ggak::GgakScienceStore* ggak = nullptr;
    WebhookDispatcher* webhook = nullptr;
    // Live pipeline mode as a PipelineMode code; updated by the scheduler.
    const std::atomic<int>* mode_code = nullptr;
    scheduler::Scheduler* scheduler = nullptr;
    scheduler::CaduLifecycle* cadu = nullptr;
    // Thread-safe accessor for the live pipeline (set by App).
    std::function<PipelineSnapshot()> pipeline_snapshot;
    // Thread-safe copy of the live demodulator's symbol cloud (set by App).
    std::function<void(std::vector<std::complex<float>>&)> constellation;
    std::string satellite = "elektro-l3";
    std::string mode = "ggak";
    std::string sdr_error;
    std::chrono::steady_clock::time_point start_time;
};

class WebServer {
public:
    WebServer() = default;
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    // Register routes and start listening; false when the transport is
    // unavailable or the bind fails (the daemon keeps running).
    bool start(const Config& cfg, const WebContext& ctx);
    void stop();

    bool running() const { return server_.running(); }
    uint16_t port() const { return server_.port(); }
    Router& router() { return router_; }

private:
    Router router_;
    HttpServer server_;
    WebContext ctx_;
};

// Endpoint groups (each in its own translation unit).
void register_status_api(Router& router, const WebContext& ctx);
void register_fft_api(Router& router, const WebContext& ctx);
void register_constellation_api(Router& router, const WebContext& ctx);
void register_products_api(Router& router, const WebContext& ctx);
void register_schedule_api(Router& router, const WebContext& ctx);
void register_ggak_api(Router& router, const WebContext& ctx);
void register_webhook_api(Router& router, const WebContext& ctx);
void register_static_routes(Router& router, const WebContext& ctx);

}  // namespace ed::web
