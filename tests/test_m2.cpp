// ElektroDump — M2 self-tests (ring buffer, FFT, waterfall, SDR, router).
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "core/ring_buffer.h"
#include "dsp/fft.h"
#include "dsp/waterfall.h"
#include "sdr/sdr_source.h"
#include "web/router.h"

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

constexpr double kPi = 3.14159265358979323846;

void test_ring() {
    ed::SpscRing<int> ring(8);
    CHECK(ring.capacity() == 8);
    CHECK(ring.empty());

    const int data[4] = {1, 2, 3, 4};
    CHECK(ring.write(data, 4) == 4);
    CHECK(ring.size() == 4);

    int out[4] = {0, 0, 0, 0};
    CHECK(ring.read(out, 4) == 4);
    CHECK(out[0] == 1 && out[3] == 4);
    CHECK(ring.empty());

    // Overflow drops the oldest items.
    int big[16];
    for (int i = 0; i < 16; ++i) {
        big[i] = i;
    }
    ring.write(big, 16);
    CHECK(ring.size() == 8);
    int out2[8] = {0};
    CHECK(ring.read(out2, 8) == 8);
    CHECK(out2[0] == 8 && out2[7] == 15);
}

void test_fft() {
    const std::size_t n = 64;
    ed::dsp::Fft fft(n);
    std::vector<std::complex<float>> in(n);
    const int tone_bin = 8;
    for (std::size_t i = 0; i < n; ++i) {
        const double phase = 2.0 * kPi * tone_bin * static_cast<double>(i) /
                             static_cast<double>(n);
        in[i] = std::polar(1.0f, static_cast<float>(phase));
    }
    std::vector<float> db(n);
    fft.power_db(in.data(), db.data());

    std::size_t max_bin = 0;
    for (std::size_t i = 1; i < n; ++i) {
        if (db[i] > db[max_bin]) {
            max_bin = i;
        }
    }
    // power_db fft-shifts, so bin 8 lands at index (8 + n/2) % n.
    CHECK(max_bin == (tone_bin + static_cast<int>(n / 2)) % static_cast<int>(n));
}

void test_router() {
    ed::web::Router router;
    router.get("/api/v1/status", [](const ed::web::HttpRequest&) {
        return ed::web::HttpResponse::json("{\"ok\":true}");
    });

    ed::web::HttpRequest req;
    req.method = "GET";
    req.path = "/api/v1/status";
    const ed::web::HttpResponse ok = router.handle(req);
    CHECK(ok.status == 200);
    CHECK(ok.body == "{\"ok\":true}");
    CHECK(ok.content_type == "application/json");

    req.path = "/missing";
    CHECK(router.handle(req).status == 404);

    req.method = "POST";
    req.path = "/api/v1/status";
    CHECK(router.handle(req).status == 405);

    const auto params = ed::web::parse_query("a=1&b=hello%20world&c");
    CHECK(params.at("a") == "1");
    CHECK(params.at("b") == "hello world");
    CHECK(params.at("c").empty());
}

void test_sdr_factory() {
    const std::vector<std::string> drivers = ed::sdr::known_drivers();
    CHECK(drivers.size() == 4);
    CHECK(ed::sdr::backend_available("mock"));
    CHECK(ed::sdr::backend_available("file"));
    CHECK(ed::sdr::make_sdr("nope") == nullptr);

    const std::vector<ed::sdr::SdrInfo> devices = ed::sdr::probe_devices();
    CHECK(!devices.empty());
}

void test_mock_sdr() {
    std::unique_ptr<ed::sdr::ISdrSource> src = ed::sdr::make_sdr("mock");
    CHECK(src != nullptr);

    ed::sdr::SdrConfig cfg;
    cfg.driver = "mock";
    cfg.sample_rate_hz = 1000000;
    cfg.center_freq_hz = 1693000000;
    CHECK(src->open(cfg));

    std::atomic<std::size_t> total{0};
    CHECK(src->start([&total](const std::complex<float>*, std::size_t n) {
        total.fetch_add(n, std::memory_order_relaxed);
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    src->stop();

    CHECK(total.load() > 0);
    CHECK(src->center_freq_hz() == 1693000000);
    CHECK(src->sample_rate_hz() == 1000000);
    CHECK(src->retune(1691000000));
    CHECK(src->center_freq_hz() == 1691000000);
}

void test_waterfall() {
    ed::SpscRing<std::complex<float>> ring(65536);
    ed::dsp::WaterfallProducer wf;
    wf.configure(256, 20, 1000000, 1693000000);
    wf.start(&ring);

    std::vector<std::complex<float>> tone(256);
    for (std::size_t i = 0; i < tone.size(); ++i) {
        const double phase = 2.0 * kPi * 32.0 * static_cast<double>(i) / 256.0;
        tone[i] = std::polar(1.0f, static_cast<float>(phase));
    }
    for (int k = 0; k < 30; ++k) {
        ring.write(tone.data(), tone.size());
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ed::dsp::SpectrumFrame frame;
    bool got = false;
    for (int i = 0; i < 100 && !got; ++i) {
        got = wf.latest(frame);
        if (!got) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    wf.stop();

    CHECK(got);
    CHECK(frame.bins_db.size() == 256);
    CHECK(frame.center_hz == 1693000000);
    CHECK(frame.span_hz == 1000000);
    CHECK(wf.frames() > 0);
}

}  // namespace

int main() {
    test_ring();
    test_fft();
    test_router();
    test_sdr_factory();
    test_mock_sdr();
    test_waterfall();

    if (g_failures == 0) {
        std::printf("all M2 tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
