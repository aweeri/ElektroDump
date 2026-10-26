#include "web/webhook.h"

#include <chrono>
#include <cstring>

#include "core/config.h"
#include "core/logging.h"
#include "core/time_util.h"
#include "util/json_util.h"
#include "web/web_server.h"

#ifdef ED_HAVE_CURL
#include <curl/curl.h>
#endif

namespace ed::web {

namespace {

// SHA-256 / HMAC-SHA256 (self-contained; no OpenSSL dependency).

constexpr uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

struct Sha256 {
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint8_t block[64];
    std::size_t block_len = 0;
    uint64_t total_len = 0;

    void transform(const uint8_t* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
                   (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(p[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 =
                rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 =
                rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = hh + s1 + ch + kSha256K[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    void update(const uint8_t* data, std::size_t len) {
        total_len += len;
        for (std::size_t i = 0; i < len; ++i) {
            block[block_len++] = data[i];
            if (block_len == 64) {
                transform(block);
                block_len = 0;
            }
        }
    }

    void final(uint8_t out[32]) {
        const uint64_t bit_len = total_len * 8;
        const uint8_t pad = 0x80;
        update(&pad, 1);
        const uint8_t zero = 0x00;
        while (block_len != 56) {
            update(&zero, 1);
        }
        for (int i = 0; i < 8; ++i) {
            block[block_len++] =
                static_cast<uint8_t>((bit_len >> (56 - i * 8)) & 0xFF);
            if (block_len == 64) {
                transform(block);
                block_len = 0;
            }
        }
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
            out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
            out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
            out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
        }
    }
};

std::vector<uint8_t> sha256_bytes(const uint8_t* data, std::size_t len) {
    Sha256 sha;
    sha.update(data, len);
    uint8_t out[32];
    sha.final(out);
    return std::vector<uint8_t>(out, out + 32);
}

std::string trim_trailing_slashes(std::string value) {
    while (value.size() > 1 && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

}  // namespace

bool is_known_webhook_event(const std::string& event) {
    return event == "product.created" || event == "test";
}

const std::vector<int>& webhook_retry_backoff_ms() {
    static const std::vector<int> kBackoff = {1000, 5000, 25000};
    return kBackoff;
}

std::string hmac_sha256_hex(const std::string& secret, const std::string& body) {
    uint8_t key[64];
    std::memset(key, 0, sizeof(key));
    if (secret.size() > sizeof(key)) {
        const std::vector<uint8_t> hashed = sha256_bytes(
            reinterpret_cast<const uint8_t*>(secret.data()), secret.size());
        std::memcpy(key, hashed.data(), hashed.size());
    } else {
        std::memcpy(key, secret.data(), secret.size());
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<uint8_t>(key[i] ^ 0x36);
        opad[i] = static_cast<uint8_t>(key[i] ^ 0x5c);
    }

    Sha256 inner;
    inner.update(ipad, sizeof(ipad));
    inner.update(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    uint8_t inner_hash[32];
    inner.final(inner_hash);

    Sha256 outer;
    outer.update(opad, sizeof(opad));
    outer.update(inner_hash, sizeof(inner_hash));
    uint8_t digest[32];
    outer.final(digest);

    static const char* kHex = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (int i = 0; i < 32; ++i) {
        result.push_back(kHex[digest[i] >> 4]);
        result.push_back(kHex[digest[i] & 0x0F]);
    }
    return result;
}

WebhookDispatcher::~WebhookDispatcher() { stop(); }

bool WebhookDispatcher::configure(const Config& cfg, std::string* error) {
    hooks_.clear();
    base_url_ = trim_trailing_slashes(cfg.get_string("api.base_url", ""));
    warned_disabled_ = false;

    const json::Value* hooks = cfg.root().find_path("api.webhooks");
    if (hooks != nullptr && hooks->is_array()) {
        for (std::size_t i = 0; i < hooks->size(); ++i) {
            const json::Value& entry = (*hooks)[i];
            if (!entry.is_object()) {
                continue;
            }
            WebhookConfig hook;
            hook.url = trim_trailing_slashes(entry.find("url") != nullptr
                                                 ? entry.find("url")->as_string("")
                                                 : std::string());
            if (hook.url.empty()) {
                ED_LOG(Warn, "webhook")
                    << "api.webhooks[" << i << "] has no url; skipped";
                continue;
            }
            const json::Value* secret = entry.find("secret");
            if (secret != nullptr) {
                hook.secret = secret->as_string("");
            }
            const json::Value* events = entry.find("events");
            if (events != nullptr && events->is_array()) {
                for (std::size_t j = 0; j < events->size(); ++j) {
                    hook.events.push_back((*events)[j].as_string(""));
                }
            }
            if (hook.events.empty()) {
                hook.events.push_back("product.created");
            }
            hooks_.push_back(std::move(hook));
        }
    }

    if (hooks_.empty()) {
        enabled_.store(false, std::memory_order_relaxed);
        if (error != nullptr) {
            error->clear();
        }
        return true;
    }

#ifdef ED_HAVE_CURL
    enabled_.store(true, std::memory_order_relaxed);
    if (error != nullptr) {
        error->clear();
    }
#else
    enabled_.store(false, std::memory_order_relaxed);
    ED_LOG(Warn, "webhook")
        << hooks_.size()
        << " webhook(s) configured but libcurl is not compiled in; disabled";
    if (error != nullptr) {
        error->clear();
    }
#endif
    return true;
}

void WebhookDispatcher::start() {
    if (!enabled_.load(std::memory_order_relaxed)) {
        return;
    }
    if (running_.exchange(true, std::memory_order_relaxed)) {
        return;
    }
#ifdef ED_HAVE_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    thread_ = std::thread([this] { worker(); });
}

void WebhookDispatcher::stop() {
    if (!running_.exchange(false, std::memory_order_relaxed)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
#ifdef ED_HAVE_CURL
    curl_global_cleanup();
#endif
}

void WebhookDispatcher::enqueue(Job job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(job));
        queued_.fetch_add(1, std::memory_order_relaxed);
    }
    cv_.notify_one();
}

void WebhookDispatcher::notify_product(const output::ProductMeta& meta,
                                       const std::string& base_url) {
    if (!enabled_.load(std::memory_order_relaxed)) {
        if (!warned_disabled_) {
            warned_disabled_ = true;
            ED_LOG(Debug, "webhook") << "product notification dropped (disabled)";
        }
        return;
    }
    const std::string url_base = base_url.empty() ? base_url_ : base_url;
    const std::string payload =
        build_product_payload(meta, url_base, time::iso8601_now(false));
    for (const WebhookConfig& hook : hooks_) {
        bool wanted = false;
        for (const std::string& event : hook.events) {
            if (event == "product.created") {
                wanted = true;
                break;
            }
        }
        if (!wanted) {
            continue;
        }
        Job job;
        job.url = hook.url;
        job.payload = payload;
        job.secret = hook.secret;
        job.backoff_ms = webhook_retry_backoff_ms();
        enqueue(std::move(job));
    }
}

bool WebhookDispatcher::test(std::string* error) {
    if (!enabled_.load(std::memory_order_relaxed)) {
        if (error != nullptr) {
            *error = hooks_.empty() ? "no webhooks configured"
                                    : "webhook disabled (libcurl not compiled in)";
        }
        return false;
    }
    json::Value root;
    root.set("event", json::Value("test"));
    root.set("timestamp", json::Value(time::iso8601_now(false)));
    const std::string payload = root.dump(0);
    for (const WebhookConfig& hook : hooks_) {
        Job job;
        job.url = hook.url;
        job.payload = payload;
        job.secret = hook.secret;
        job.backoff_ms = webhook_retry_backoff_ms();
        enqueue(std::move(job));
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void WebhookDispatcher::retry_pending() {
    std::deque<Job> retry;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        retry.swap(pending_);
    }
    for (Job& job : retry) {
        job.attempt = 0;
        enqueue(std::move(job));
    }
}

void WebhookDispatcher::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return !running_.load(std::memory_order_relaxed) ||
                       !queue_.empty();
            });
            if (!running_.load(std::memory_order_relaxed)) {
                return;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        deliver_with_retry(job);
    }
}

void WebhookDispatcher::deliver_with_retry(Job& job) {
    for (;;) {
        std::string error;
        if (deliver(job.url, job.payload, job.secret, &error)) {
            ok_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (job.attempt >= job.backoff_ms.size()) {
            fail_.fetch_add(1, std::memory_order_relaxed);
            ED_LOG(Warn, "webhook") << "delivery failed for " << job.url << ": "
                                    << error << " (queued for janitor retry)";
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_.size() < 256) {
                job.attempt = 0;
                pending_.push_back(job);
            }
            return;
        }

        const int delay_ms = job.backoff_ms[job.attempt];
        ++job.attempt;
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(delay_ms), [this] {
            return !running_.load(std::memory_order_relaxed);
        });
        if (!running_.load(std::memory_order_relaxed)) {
            return;
        }
    }
}

bool WebhookDispatcher::deliver(const std::string& url,
                                const std::string& payload,
                                const std::string& secret,
                                std::string* error) {
#ifdef ED_HAVE_CURL
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        if (error != nullptr) {
            *error = "curl_easy_init failed";
        }
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string signature_header;
    if (!secret.empty()) {
        signature_header = "X-ElektroDump-Signature: sha256=" +
                           hmac_sha256_hex(secret, payload);
        headers = curl_slist_append(headers, signature_header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                     static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        if (error != nullptr) {
            *error = curl_easy_strerror(rc);
        }
        return false;
    }
    if (status < 200 || status >= 300) {
        if (error != nullptr) {
            *error = "HTTP " + std::to_string(status);
        }
        return false;
    }
    return true;
#else
    (void)url;
    (void)payload;
    (void)secret;
    if (error != nullptr) {
        *error = "libcurl not compiled in";
    }
    return false;
#endif
}

WebhookDispatcher::Stats WebhookDispatcher::stats() const {
    Stats s;
    s.ok = ok_.load(std::memory_order_relaxed);
    s.fail = fail_.load(std::memory_order_relaxed);
    s.queued = queued_.load(std::memory_order_relaxed);
    return s;
}

std::string WebhookDispatcher::build_product_payload(
    const output::ProductMeta& meta, const std::string& base_url,
    const std::string& timestamp) {
    const std::string base = trim_trailing_slashes(base_url);
    json::Value root;
    root.set("event", json::Value("product.created"));
    root.set("timestamp", json::Value(timestamp));
    root.set("satellite", json::Value(meta.satellite));
    root.set("product", json::Value(meta.product));
    root.set("id", json::Value(meta.id));
    root.set("image_url",
             json::Value(base + "/api/v1/products/" + meta.id + "/image"));
    root.set("latest_url", json::Value(base +
                                       "/api/v1/products/latest?product=" +
                                       meta.product));
    root.set("width", json::Value(static_cast<int64_t>(meta.width)));
    root.set("height", json::Value(static_cast<int64_t>(meta.height)));
    root.set("projection", json::Value(meta.projection));
    root.set("capture_start", json::Value(meta.capture_start));
    root.set("capture_end", json::Value(meta.capture_end));
    return root.dump(0);
}

void register_webhook_api(Router& router, const WebContext& ctx) {
    router.post("/api/v1/webhook/test", [&ctx](const HttpRequest&) {
        json::Value out;
        if (ctx.webhook == nullptr) {
            out.set("enabled", json::Value(false));
            out.set("queued", json::Value(false));
            out.set("error", json::Value("no_webhook"));
            return HttpResponse::json(out.dump(0), 503);
        }
        std::string error;
        const bool queued = ctx.webhook->test(&error);
        out.set("enabled", json::Value(ctx.webhook->enabled()));
        out.set("queued", json::Value(queued));
        const WebhookDispatcher::Stats stats = ctx.webhook->stats();
        out.set("ok", json::Value(static_cast<int64_t>(stats.ok)));
        out.set("fail", json::Value(static_cast<int64_t>(stats.fail)));
        if (!error.empty()) {
            out.set("error", json::Value(error));
        }
        return HttpResponse::json(out.dump(0), queued ? 200 : 503);
    });
}

}  // namespace ed::web
