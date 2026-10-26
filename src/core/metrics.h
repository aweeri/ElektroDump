// ElektroDump — lock-free counters and gauges for the hot path.
#pragma once

#include <atomic>
#include <cstdint>

namespace ed {

struct MetricsSnapshot {
    uint64_t iq_samples = 0;
    uint64_t soft_symbols = 0;
    uint64_t cadus_total = 0;
    uint64_t cadus_bad = 0;
    uint64_t rs_corrected = 0;
    uint64_t rs_uncorrectable = 0;
    uint64_t files_total = 0;
    uint64_t files_recovered = 0;
    uint64_t products_total = 0;
    uint64_t webhook_ok = 0;
    uint64_t webhook_fail = 0;
    uint64_t sdr_overflows = 0;
    double snr_db = 0.0;
    double ber = 0.0;
    bool locked = false;
};

class Metrics {
public:
    void add_iq(uint64_t n) { iq_samples_.fetch_add(n, std::memory_order_relaxed); }
    void add_soft(uint64_t n) { soft_symbols_.fetch_add(n, std::memory_order_relaxed); }
    void add_cadu(bool bad) {
        cadus_total_.fetch_add(1, std::memory_order_relaxed);
        if (bad) {
            cadus_bad_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    // Absolute counters, used by pipelines that already track totals.
    void set_cadus(uint64_t total, uint64_t bad) {
        cadus_total_.store(total, std::memory_order_relaxed);
        cadus_bad_.store(bad, std::memory_order_relaxed);
    }
    void add_rs_corrected(uint64_t n) { rs_corrected_.fetch_add(n, std::memory_order_relaxed); }
    void add_rs_uncorrectable(uint64_t n) { rs_uncorrectable_.fetch_add(n, std::memory_order_relaxed); }
    void add_file(bool recovered) {
        files_total_.fetch_add(1, std::memory_order_relaxed);
        if (recovered) {
            files_recovered_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    void add_product() { products_total_.fetch_add(1, std::memory_order_relaxed); }
    void add_webhook(bool ok) {
        if (ok) {
            webhook_ok_.fetch_add(1, std::memory_order_relaxed);
        } else {
            webhook_fail_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    void add_overflow() { sdr_overflows_.fetch_add(1, std::memory_order_relaxed); }

    void set_snr(double db) { snr_db_.store(db, std::memory_order_relaxed); }
    void set_ber(double ber) { ber_.store(ber, std::memory_order_relaxed); }
    void set_locked(bool locked) { locked_.store(locked, std::memory_order_relaxed); }

    MetricsSnapshot snapshot() const {
        MetricsSnapshot s;
        s.iq_samples = iq_samples_.load(std::memory_order_relaxed);
        s.soft_symbols = soft_symbols_.load(std::memory_order_relaxed);
        s.cadus_total = cadus_total_.load(std::memory_order_relaxed);
        s.cadus_bad = cadus_bad_.load(std::memory_order_relaxed);
        s.rs_corrected = rs_corrected_.load(std::memory_order_relaxed);
        s.rs_uncorrectable = rs_uncorrectable_.load(std::memory_order_relaxed);
        s.files_total = files_total_.load(std::memory_order_relaxed);
        s.files_recovered = files_recovered_.load(std::memory_order_relaxed);
        s.products_total = products_total_.load(std::memory_order_relaxed);
        s.webhook_ok = webhook_ok_.load(std::memory_order_relaxed);
        s.webhook_fail = webhook_fail_.load(std::memory_order_relaxed);
        s.sdr_overflows = sdr_overflows_.load(std::memory_order_relaxed);
        s.snr_db = snr_db_.load(std::memory_order_relaxed);
        s.ber = ber_.load(std::memory_order_relaxed);
        s.locked = locked_.load(std::memory_order_relaxed);
        return s;
    }

    void reset() {
        iq_samples_ = 0;
        soft_symbols_ = 0;
        cadus_total_ = 0;
        cadus_bad_ = 0;
        rs_corrected_ = 0;
        rs_uncorrectable_ = 0;
        files_total_ = 0;
        files_recovered_ = 0;
        products_total_ = 0;
        webhook_ok_ = 0;
        webhook_fail_ = 0;
        sdr_overflows_ = 0;
        snr_db_ = 0.0;
        ber_ = 0.0;
        locked_ = false;
    }

private:
    std::atomic<uint64_t> iq_samples_{0};
    std::atomic<uint64_t> soft_symbols_{0};
    std::atomic<uint64_t> cadus_total_{0};
    std::atomic<uint64_t> cadus_bad_{0};
    std::atomic<uint64_t> rs_corrected_{0};
    std::atomic<uint64_t> rs_uncorrectable_{0};
    std::atomic<uint64_t> files_total_{0};
    std::atomic<uint64_t> files_recovered_{0};
    std::atomic<uint64_t> products_total_{0};
    std::atomic<uint64_t> webhook_ok_{0};
    std::atomic<uint64_t> webhook_fail_{0};
    std::atomic<uint64_t> sdr_overflows_{0};
    std::atomic<double> snr_db_{0.0};
    std::atomic<double> ber_{0.0};
    std::atomic<bool> locked_{false};
};

}  // namespace ed
