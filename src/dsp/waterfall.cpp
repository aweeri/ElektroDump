#include "dsp/waterfall.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "core/thread_util.h"
#include "core/time_util.h"

namespace ed::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> make_hann(std::size_t n) {
    std::vector<float> w(n);
    for (std::size_t i = 0; i < n; ++i) {
        w[i] = static_cast<float>(
            0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) /
                                  static_cast<double>(n - 1))));
    }
    return w;
}

}  // namespace

WaterfallProducer::WaterfallProducer() = default;

WaterfallProducer::~WaterfallProducer() { stop(); }

void WaterfallProducer::configure(std::size_t fft_size, int rate_hz,
                                  uint32_t sample_rate_hz, uint64_t center_hz,
                                  int average) {
    fft_size_ = fft_size < 16 ? 16 : fft_size;
    rate_hz_ = rate_hz < 1 ? 1 : rate_hz;
    average_ = average < 1 ? 1 : (average > 64 ? 64 : average);
    sample_rate_hz_.store(sample_rate_hz, std::memory_order_relaxed);
    center_hz_.store(center_hz, std::memory_order_relaxed);

    fft_ = std::make_unique<Fft>(fft_size_);
    window_ = make_hann(fft_size_);
    windowed_.assign(fft_size_, std::complex<float>(0.0f, 0.0f));
    block_.assign(fft_size_ * static_cast<std::size_t>(average_),
                  std::complex<float>(0.0f, 0.0f));
}

void WaterfallProducer::start(SpscRing<std::complex<float>>* ring) {
    if (running_.load(std::memory_order_relaxed)) {
        return;
    }
    ring_ = ring;
    running_.store(true, std::memory_order_relaxed);
    thread_ = std::thread([this] { loop(); });
}

void WaterfallProducer::stop() {
    if (!running_.exchange(false, std::memory_order_relaxed)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void WaterfallProducer::loop() {
    thread::set_name("fft");
    thread::set_nice(5);

    const auto period = std::chrono::microseconds(1000000 / rate_hz_);
    const std::size_t span = fft_size_ * static_cast<std::size_t>(average_);
    std::vector<std::complex<float>> tmp(span);
    std::vector<float> bins(fft_size_);
    std::vector<double> power(fft_size_, 0.0);

    while (running_.load(std::memory_order_relaxed)) {
        const auto t0 = std::chrono::steady_clock::now();

        // Drain the ring, keeping only the most recent fft_size samples.
        std::size_t have = 0;
        std::size_t r = 0;
        while (ring_ != nullptr &&
               (r = ring_->read(tmp.data(), tmp.size())) > 0) {
            if (r >= span) {
                std::copy(tmp.begin() + (r - span), tmp.begin() + r,
                          block_.begin());
                have = span;
            } else {
                if (have + r > span) {
                    const std::size_t drop = have + r - span;
                    std::memmove(block_.data(), block_.data() + drop,
                                 (have - drop) * sizeof(std::complex<float>));
                    have -= drop;
                }
                std::copy(tmp.begin(), tmp.begin() + r, block_.begin() + have);
                have += r;
            }
        }

        if (have >= fft_size_ && fft_ != nullptr) {
            // Power-average the newest `average_` blocks: averaging in dB would
            // bias the result low and keep the spikiness.
            const std::size_t blocks = have / fft_size_;
            std::fill(power.begin(), power.end(), 0.0);
            for (std::size_t b = 0; b < blocks; ++b) {
                const std::complex<float>* src =
                    block_.data() + b * fft_size_ +
                    (have - blocks * fft_size_);
                for (std::size_t i = 0; i < fft_size_; ++i) {
                    windowed_[i] = src[i] * window_[i];
                }
                fft_->power_db(windowed_.data(), bins.data());
                for (std::size_t i = 0; i < fft_size_; ++i) {
                    power[i] += std::pow(10.0, static_cast<double>(bins[i]) / 10.0);
                }
            }
            const double inv = 1.0 / static_cast<double>(blocks);
            for (std::size_t i = 0; i < fft_size_; ++i) {
                bins[i] = static_cast<float>(10.0 * std::log10(power[i] * inv));
            }

            SpectrumFrame frame;
            frame.bins_db = bins;
            frame.center_hz = center_hz_.load(std::memory_order_relaxed);
            frame.span_hz = sample_rate_hz_.load(std::memory_order_relaxed);
            frame.timestamp_unix =
                std::chrono::duration<double>(
                    time::now_utc().time_since_epoch())
                    .count();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (history_.size() >= history_capacity_) {
                    history_.erase(history_.begin());
                }
                history_.push_back(std::move(frame));
            }
            frames_.fetch_add(1, std::memory_order_relaxed);
        }

        const auto elapsed = std::chrono::steady_clock::now() - t0;
        if (elapsed < period) {
            std::this_thread::sleep_for(period - elapsed);
        }
    }
}

bool WaterfallProducer::latest(SpectrumFrame& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.empty()) {
        return false;
    }
    out = history_.back();
    return true;
}

std::vector<SpectrumFrame> WaterfallProducer::rows(std::size_t max_rows) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (history_.size() <= max_rows) {
        return history_;
    }
    return std::vector<SpectrumFrame>(history_.end() - max_rows, history_.end());
}

}  // namespace ed::dsp
