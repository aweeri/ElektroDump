// ElektroDump — FFT waterfall / spectrum producer (PLAN.md D2, D8).
// Consumes IQ from the shared SPSC ring, computes a windowed power spectrum at a
// fixed rate, and keeps a bounded row history for the web UI. Runs on its own
// low-priority thread and never blocks the SDR.
#pragma once

#include <atomic>
#include <complex>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "core/ring_buffer.h"
#include "dsp/fft.h"

namespace ed::dsp {

struct SpectrumFrame {
    std::vector<float> bins_db;  // fft-shifted power in dB
    uint64_t center_hz = 0;
    uint32_t span_hz = 0;
    double timestamp_unix = 0.0;
};

class WaterfallProducer {
public:
    WaterfallProducer();
    ~WaterfallProducer();

    WaterfallProducer(const WaterfallProducer&) = delete;
    WaterfallProducer& operator=(const WaterfallProducer&) = delete;

    // `average` FFT blocks are power-averaged into each emitted row; one raw
    // block per row is too noisy to read (each bin varies by several dB).
    void configure(std::size_t fft_size, int rate_hz, uint32_t sample_rate_hz,
                   uint64_t center_hz, int average = 8);

    // Start consuming from `ring` (owned by the caller, must outlive this).
    void start(SpscRing<std::complex<float>>* ring);
    void stop();

    void set_center(uint64_t hz) { center_hz_.store(hz, std::memory_order_relaxed); }
    void set_sample_rate(uint32_t hz) {
        sample_rate_hz_.store(hz, std::memory_order_relaxed);
    }

    // Copy the most recent frame. Returns false when none is available yet.
    bool latest(SpectrumFrame& out) const;

    // Copy up to `max_rows` most recent frames, oldest first.
    std::vector<SpectrumFrame> rows(std::size_t max_rows) const;

    std::size_t fft_size() const { return fft_size_; }
    int average() const { return average_; }
    int rate_hz() const { return rate_hz_; }
    uint64_t frames() const { return frames_.load(std::memory_order_relaxed); }
    bool running() const { return running_.load(std::memory_order_relaxed); }

private:
    void loop();

    SpscRing<std::complex<float>>* ring_ = nullptr;
    std::unique_ptr<Fft> fft_;
    std::vector<std::complex<float>> windowed_;
    std::vector<float> window_;
    std::vector<std::complex<float>> block_;

    std::size_t fft_size_ = 1024;
    int rate_hz_ = 10;
    int average_ = 8;
    std::atomic<uint32_t> sample_rate_hz_{1000000};
    std::atomic<uint64_t> center_hz_{0};
    std::atomic<uint64_t> frames_{0};
    std::atomic<bool> running_{false};

    std::thread thread_;

    mutable std::mutex mutex_;
    std::vector<SpectrumFrame> history_;
    std::size_t history_capacity_ = 64;
};

}  // namespace ed::dsp
