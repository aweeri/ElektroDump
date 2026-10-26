// ElektroDump — product-created webhook dispatcher (spec §4.5, PLAN.md D9).
// POSTs a JSON event to each `api.webhooks[]` endpoint on product creation;
// non-blocking (the image worker enqueues and returns). Retries use exponential
// backoff (1 s, 5 s, 25 s, then the janitor). Needs libcurl (ED_HAVE_CURL);
// without it the dispatcher is disabled but the payload/signature path works.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "output/product_meta.h"
#include "web/router.h"

namespace ed {
class Config;
}

namespace ed::web {

struct WebContext;

struct WebhookConfig {
    std::string url;
    std::string secret;
    std::vector<std::string> events;
};

bool is_known_webhook_event(const std::string& event);

// Lower-case hex HMAC-SHA256 of `body` keyed by `secret` (RFC 2104).
std::string hmac_sha256_hex(const std::string& secret, const std::string& body);

// Backoff schedule (milliseconds) applied between delivery attempts.
const std::vector<int>& webhook_retry_backoff_ms();

class WebhookDispatcher {
public:
    WebhookDispatcher() = default;
    ~WebhookDispatcher();

    WebhookDispatcher(const WebhookDispatcher&) = delete;
    WebhookDispatcher& operator=(const WebhookDispatcher&) = delete;

    bool configure(const Config& cfg, std::string* error);
    void start();
    void stop();

    // Enqueue a product.created event (non-blocking).
    void notify_product(const output::ProductMeta& meta,
                        const std::string& base_url);

    // Fire a test event (POST /api/v1/webhook/test).
    bool test(std::string* error);

    // Re-attempt jobs that exhausted the backoff schedule (janitor hook).
    void retry_pending();

    struct Stats {
        uint64_t ok = 0;
        uint64_t fail = 0;
        uint64_t queued = 0;
    };
    Stats stats() const;

    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    std::size_t hook_count() const { return hooks_.size(); }

    // Build the JSON payload for a product.created event (unit-testable).
    static std::string build_product_payload(const output::ProductMeta& meta,
                                             const std::string& base_url,
                                             const std::string& timestamp);

private:
    struct Job {
        std::string url;
        std::string payload;
        std::string secret;
        std::size_t attempt = 0;
        std::vector<int> backoff_ms;
    };

    bool deliver(const std::string& url, const std::string& payload,
                 const std::string& secret, std::string* error);
    void enqueue(Job job);
    void worker();
    void deliver_with_retry(Job& job);

    std::vector<WebhookConfig> hooks_;
    std::string base_url_;
    std::atomic<bool> enabled_{false};
    bool warned_disabled_ = false;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    std::deque<Job> pending_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::atomic<uint64_t> ok_{0};
    std::atomic<uint64_t> fail_{0};
    std::atomic<uint64_t> queued_{0};
};

// Register POST /api/v1/webhook/test.
void register_webhook_api(Router& router, const WebContext& ctx);

}  // namespace ed::web
