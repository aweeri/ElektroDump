// ElektroDump — M9 self-tests (webhook signing / payload / config).
//
// The HTTP transport needs libcurl, which may be absent; the signing and
// payload paths are pure and are exercised here so the webhook remains testable
// on any build.
#include <cstdio>
#include <string>
#include <vector>

#include "core/config.h"
#include "output/product_meta.h"
#include "web/webhook.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
                         #cond);                                         \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

void test_hmac_vectors() {
    // RFC 4231 test case 1: key = 0x0b * 20, data = "Hi There".
    const std::string key1(20, '\x0b');
    CHECK(ed::web::hmac_sha256_hex(key1, "Hi There") ==
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    // RFC 4231 test case 2: key = "Jefe".
    CHECK(ed::web::hmac_sha256_hex("Jefe", "what do ya want for nothing?") ==
          "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    CHECK(ed::web::hmac_sha256_hex("secret", "body").size() == 64);
    std::printf("  hmac: RFC 4231 vectors match\n");
}

void test_retry_schedule() {
    const std::vector<int>& backoff = ed::web::webhook_retry_backoff_ms();
    CHECK(backoff.size() == 3);
    CHECK(backoff.size() >= 1 && backoff[0] == 1000);
    CHECK(backoff.size() >= 2 && backoff[1] == 5000);
    CHECK(backoff.size() >= 3 && backoff[2] == 25000);
    std::printf("  retry: backoff 1s/5s/25s\n");
}

void test_events() {
    CHECK(ed::web::is_known_webhook_event("product.created"));
    CHECK(ed::web::is_known_webhook_event("test"));
    CHECK(!ed::web::is_known_webhook_event("nope"));
}

void test_payload() {
    ed::output::ProductMeta meta;
    meta.id = "20261026T120000Z_visual";
    meta.satellite = "elektro-l3";
    meta.product = "visual";
    meta.projection = "mercator";
    meta.width = 2048;
    meta.height = 2048;
    meta.capture_start = "2026-10-26T12:00:00Z";
    meta.capture_end = "2026-10-26T12:02:30Z";

    const std::string payload = ed::web::WebhookDispatcher::build_product_payload(
        meta, "http://host:8080/", "2026-10-26T12:03:11Z");

    CHECK(payload.find("\"event\":\"product.created\"") != std::string::npos);
    CHECK(payload.find("\"id\":\"20261026T120000Z_visual\"") !=
          std::string::npos);
    CHECK(payload.find(
              "\"image_url\":\"http://host:8080/api/v1/products/"
              "20261026T120000Z_visual/image\"") != std::string::npos);
    CHECK(payload.find(
              "\"latest_url\":\"http://host:8080/api/v1/products/latest"
              "?product=visual\"") != std::string::npos);
    CHECK(payload.find("\"width\":2048") != std::string::npos);
    std::printf("  payload: %zu bytes, absolute urls built\n", payload.size());
}

void test_configure() {
    ed::json::Value root = ed::json::Value::parse(
        R"({"version":1,"api":{"base_url":"http://host","webhooks":[
             {"url":"http://hook/1","secret":"s","events":["product.created"]},
             {"url":"","secret":"x"}]}})",
        nullptr);
    ed::Config cfg;  // embedded defaults
    cfg.root() = root;
    ed::web::WebhookDispatcher dispatcher;
    std::string error;
    CHECK(dispatcher.configure(cfg, &error));
    CHECK(error.empty());
    // The empty-url entry is skipped.
    CHECK(dispatcher.hook_count() == 1u);

#ifndef ED_HAVE_CURL
    // Without libcurl the dispatcher must be disabled and refuse to test.
    CHECK(!dispatcher.enabled());
    CHECK(!dispatcher.test(&error));
    CHECK(!error.empty());
    std::printf("  configure: hooks=%zu disabled (no libcurl)\n",
                dispatcher.hook_count());
#else
    std::printf("  configure: hooks=%zu enabled (libcurl)\n",
                dispatcher.hook_count());
#endif
}

}  // namespace

int main() {
    std::printf("test_webhook\n");
    test_hmac_vectors();
    test_retry_schedule();
    test_events();
    test_payload();
    test_configure();
    if (g_failures != 0) {
        std::fprintf(stderr, "test_webhook: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("test_webhook: PASS\n");
    return 0;
}
