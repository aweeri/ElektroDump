// ElektroDump — Airspy backend (PLAN.md D1).
// The device produces interleaved float32 IQ, so the callback is a straight copy
// into a reusable buffer. Manual gain (LNA/Mixer/VGA) and the RF bias tee are
// exposed. Airspy is the only backend that reaches the 6 MSPS needed for HRIT.
#include "sdr/sdr_source.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/logging.h"

#ifdef ED_HAVE_AIRSPY
#include <airspy.h>
#endif

namespace ed::sdr {

#ifdef ED_HAVE_AIRSPY

namespace {

constexpr uint32_t kSampleRates[] = {3000000, 6000000, 10000000};

bool parse_serial(const std::string& text, uint64_t& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 0);
    if (end == text.c_str()) {
        return false;
    }
    out = static_cast<uint64_t>(value);
    return true;
}

}  // namespace

class AirspySource : public ISdrSource {
public:
    ~AirspySource() override {
        stop();
        if (dev_ != nullptr) {
            airspy_close(dev_);
            dev_ = nullptr;
        }
    }

    bool open(const SdrConfig& cfg) override {
        cfg_ = cfg;
        uint64_t serial = 0;
        if (parse_serial(cfg.serial, serial)) {
            if (airspy_open_sn(&dev_, serial) != AIRSPY_SUCCESS) {
                last_error_ = "airspy_open_sn failed for serial " + cfg.serial;
                return false;
            }
        } else if (airspy_open(&dev_) != AIRSPY_SUCCESS) {
            last_error_ = "airspy_open failed (device busy or absent)";
            return false;
        }
        center_.store(cfg.center_freq_hz, std::memory_order_relaxed);
        rate_.store(cfg.sample_rate_hz, std::memory_order_relaxed);
        gain_.store(cfg.gain_tenth_db, std::memory_order_relaxed);
        bias_.store(cfg.bias_tee, std::memory_order_relaxed);
        return true;
    }

    bool start(IqSink sink) override {
        if (dev_ == nullptr) {
            return false;
        }
        sink_ = std::move(sink);

        airspy_set_sample_type(dev_, AIRSPY_SAMPLE_FLOAT32_IQ);
        airspy_set_samplerate(dev_, cfg_.sample_rate_hz);
        airspy_set_freq(dev_, static_cast<uint32_t>(cfg_.center_freq_hz));
        airspy_set_lna_gain(dev_, static_cast<uint8_t>(cfg_.lna_gain));
        airspy_set_mixer_gain(dev_, static_cast<uint8_t>(cfg_.mixer_gain));
        airspy_set_vga_gain(dev_, static_cast<uint8_t>(cfg_.vga_gain));
        airspy_set_rf_bias(dev_, cfg_.bias_tee ? 1 : 0);

        running_.store(true, std::memory_order_relaxed);
        // airspy_start_rx runs the callback on its own internal thread.
        if (airspy_start_rx(dev_, &AirspySource::trampoline, this) !=
            AIRSPY_SUCCESS) {
            running_.store(false, std::memory_order_relaxed);
            last_error_ = "airspy_start_rx failed";
            return false;
        }
        return true;
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_relaxed)) {
            return;
        }
        if (dev_ != nullptr) {
            airspy_stop_rx(dev_);
        }
    }

    bool set_sample_rate(uint32_t rate_hz) override {
        if (dev_ == nullptr) {
            return false;
        }
        // The Airspy only accepts 3/6/10 MSPS: pick the nearest supported rate
        // at or above the request (e.g. LRIT's 1 MSPS runs at 3 MSPS).
        uint32_t chosen = kSampleRates[sizeof(kSampleRates) /
                                        sizeof(kSampleRates[0]) - 1];
        for (uint32_t candidate : kSampleRates) {
            if (rate_hz <= candidate) {
                chosen = candidate;
                break;
            }
        }
        const bool was_running = running_.exchange(false, std::memory_order_relaxed);
        if (was_running) {
            airspy_stop_rx(dev_);
        }
        bool ok = airspy_set_samplerate(dev_, chosen) == AIRSPY_SUCCESS;
        if (ok) {
            rate_.store(chosen, std::memory_order_relaxed);
            cfg_.sample_rate_hz = chosen;
        } else {
            last_error_ = "airspy_set_samplerate failed";
        }
        if (was_running) {
            running_.store(true, std::memory_order_relaxed);
            if (airspy_start_rx(dev_, &AirspySource::trampoline, this) !=
                AIRSPY_SUCCESS) {
                running_.store(false, std::memory_order_relaxed);
                last_error_ = "airspy_start_rx failed after rate change";
                ok = false;
            }
        }
        return ok;
    }

    bool retune(uint64_t freq_hz) override {
        if (dev_ == nullptr) {
            return false;
        }
        if (airspy_set_freq(dev_, static_cast<uint32_t>(freq_hz)) ==
            AIRSPY_SUCCESS) {
            center_.store(freq_hz, std::memory_order_relaxed);
            return true;
        }
        last_error_ = "airspy_set_freq failed";
        return false;
    }

    bool set_gain(int tenth_db) override {
        if (dev_ == nullptr) {
            return false;
        }
        // No single gain register: map the requested 0.1 dB value onto the three
        // manual stages proportionally (0..15 each).
        const int clamped = tenth_db < 0 ? 0 : tenth_db;
        const int step = clamped / 100;  // 0..~30 -> 0..15
        const int value = step > 15 ? 15 : step;
        airspy_set_lna_gain(dev_, static_cast<uint8_t>(value));
        airspy_set_mixer_gain(dev_, static_cast<uint8_t>(value));
        airspy_set_vga_gain(dev_, static_cast<uint8_t>(value));
        gain_.store(tenth_db, std::memory_order_relaxed);
        return true;
    }

    SdrInfo info() const override {
        SdrInfo info;
        info.driver = "airspy";
        info.name = "Airspy";
        info.serial = cfg_.serial;
        info.min_sample_rate = kSampleRates[0];
        info.max_sample_rate = kSampleRates[sizeof(kSampleRates) /
                                             sizeof(kSampleRates[0]) - 1];
        info.has_bias_tee = true;
        info.available = true;
        return info;
    }

    SdrStats stats() const override {
        SdrStats s;
        s.samples = samples_.load(std::memory_order_relaxed);
        s.overflows = overflows_.load(std::memory_order_relaxed);
        s.last_error = last_error_;
        return s;
    }

    uint64_t center_freq_hz() const override {
        return center_.load(std::memory_order_relaxed);
    }
    uint32_t sample_rate_hz() const override {
        return rate_.load(std::memory_order_relaxed);
    }
    int gain_tenth_db() const override {
        return gain_.load(std::memory_order_relaxed);
    }
    bool bias_tee() const override {
        return bias_.load(std::memory_order_relaxed);
    }

private:
    static int trampoline(airspy_transfer* transfer) {
        return static_cast<AirspySource*>(transfer->ctx)->on_samples(transfer);
    }

    int on_samples(airspy_transfer* transfer) {
        const std::size_t n = static_cast<std::size_t>(transfer->sample_count);
        if (conv_.size() < n) {
            conv_.resize(n);
        }
        std::memcpy(conv_.data(), transfer->samples,
                    n * sizeof(std::complex<float>));
        samples_.fetch_add(n, std::memory_order_relaxed);
        if (sink_) {
            sink_(conv_.data(), n);
        }
        return 0;
    }

    airspy_device* dev_ = nullptr;
    SdrConfig cfg_;
    IqSink sink_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> samples_{0};
    std::atomic<uint64_t> overflows_{0};
    std::atomic<uint64_t> center_{0};
    std::atomic<uint32_t> rate_{0};
    std::atomic<int> gain_{0};
    std::atomic<bool> bias_{false};
    std::vector<std::complex<float>> conv_;
    std::string last_error_;
};

#endif  // ED_HAVE_AIRSPY

std::unique_ptr<ISdrSource> make_airspy() {
#ifdef ED_HAVE_AIRSPY
    return std::make_unique<AirspySource>();
#else
    return nullptr;
#endif
}

bool airspy_available() {
#ifdef ED_HAVE_AIRSPY
    return true;
#else
    return false;
#endif
}

std::vector<SdrInfo> probe_airspy() {
    std::vector<SdrInfo> out;
#ifdef ED_HAVE_AIRSPY
    uint64_t serials[16];
    const int count = airspy_list_devices(serials, 16);
    for (int i = 0; i < count; ++i) {
        SdrInfo info;
        info.driver = "airspy";
        info.name = "Airspy";
        info.serial = std::to_string(serials[i]);
        info.min_sample_rate = kSampleRates[0];
        info.max_sample_rate = kSampleRates[sizeof(kSampleRates) /
                                             sizeof(kSampleRates[0]) - 1];
        info.has_bias_tee = true;
        info.available = true;
        out.push_back(std::move(info));
    }
#endif
    return out;
}

}  // namespace ed::sdr
