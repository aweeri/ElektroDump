// ElektroDump — RTL-SDR backend (PLAN.md D1).
// Adapted methods (not code) from the reference digest: uint8 -> complex
// conversion, the retry-20 pattern, nearest-step gain selection, the >1 GHz PLL
// re-tune workaround, and the sample-rate whitelist.
#include "sdr/sdr_source.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "core/logging.h"
#include "core/thread_util.h"

#ifdef ED_HAVE_RTLSDR
#include <rtl-sdr.h>
#endif

namespace ed::sdr {

#ifdef ED_HAVE_RTLSDR

namespace {

constexpr uint32_t kSampleRates[] = {250000,  1024000, 1536000, 2048000,
                                     2400000, 2560000, 3200000};

bool is_valid_rate(uint32_t rate) {
    for (uint32_t r : kSampleRates) {
        if (r == rate) {
            return true;
        }
    }
    return false;
}

int open_by_serial(rtlsdr_dev_t** dev, const std::string& serial) {
    if (serial.empty()) {
        return rtlsdr_open(dev, 0);
    }
    const uint32_t count = rtlsdr_get_device_count();
    for (uint32_t i = 0; i < count; ++i) {
        char manufacturer[256] = {0};
        char product[256] = {0};
        char sn[256] = {0};
        if (rtlsdr_get_device_usb_strings(i, manufacturer, product, sn) == 0 &&
            serial == sn) {
            return rtlsdr_open(dev, i);
        }
    }
    return -1;
}

}  // namespace

class RtlsdrSource : public ISdrSource {
public:
    ~RtlsdrSource() override {
        stop();
        if (dev_ != nullptr) {
            rtlsdr_close(dev_);
            dev_ = nullptr;
        }
    }

    bool open(const SdrConfig& cfg) override {
        cfg_ = cfg;
        if (!is_valid_rate(cfg.sample_rate_hz)) {
            last_error_ = "unsupported RTL-SDR sample rate: " +
                          std::to_string(cfg.sample_rate_hz);
            return false;
        }
        if (open_by_serial(&dev_, cfg.serial) != 0 || dev_ == nullptr) {
            last_error_ = "rtlsdr_open failed (device busy or absent)";
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

        rtlsdr_set_sample_rate(dev_, cfg_.sample_rate_hz);
        rtlsdr_set_center_freq(dev_, static_cast<uint32_t>(cfg_.center_freq_hz));
        rtlsdr_set_tuner_gain_mode(dev_, 1);
        set_gain(cfg_.gain_tenth_db);
        rtlsdr_set_bias_tee(dev_, cfg_.bias_tee ? 1 : 0);
        rtlsdr_set_freq_correction(dev_, cfg_.ppm);
        rtlsdr_reset_buffer(dev_);

        running_.store(true, std::memory_order_relaxed);
        thread_ = std::thread([this] {
            thread::set_name("sdr");
            rtlsdr_read_async(dev_, &RtlsdrSource::trampoline, this, 0, 0);
        });
        return true;
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_relaxed)) {
            if (thread_.joinable()) {
                thread_.join();
            }
            return;
        }
        if (dev_ != nullptr) {
            rtlsdr_cancel_async(dev_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool retune(uint64_t freq_hz) override {
        if (dev_ == nullptr) {
            return false;
        }
        // >1 GHz PLL workaround: land below 1 GHz first, then on target.
        if (freq_hz > 1000000000ULL) {
            rtlsdr_set_center_freq(dev_,
                                   static_cast<uint32_t>(freq_hz - 1000000000ULL));
        }
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (rtlsdr_set_center_freq(dev_, static_cast<uint32_t>(freq_hz)) ==
                0) {
                center_.store(freq_hz, std::memory_order_relaxed);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        last_error_ = "rtlsdr_set_center_freq failed";
        return false;
    }

    bool set_sample_rate(uint32_t rate_hz) override {
        if (dev_ == nullptr) {
            return false;
        }
        // The RTL-SDR whitelist is coarse (250k..3.2M); pick the nearest rate
        // the tuner supports instead of failing the whole mode switch.
        uint32_t chosen = kSampleRates[0];
        for (uint32_t candidate : kSampleRates) {
            if (std::abs(static_cast<long long>(candidate) -
                         static_cast<long long>(rate_hz)) <
                std::abs(static_cast<long long>(chosen) -
                         static_cast<long long>(rate_hz))) {
                chosen = candidate;
            }
        }
        if (rtlsdr_set_sample_rate(dev_, chosen) != 0) {
            last_error_ = "rtlsdr_set_sample_rate failed";
            return false;
        }
        const uint32_t actual = static_cast<uint32_t>(rtlsdr_get_sample_rate(dev_));
        rate_.store(actual != 0 ? actual : chosen, std::memory_order_relaxed);
        cfg_.sample_rate_hz = rate_.load(std::memory_order_relaxed);
        return true;
    }

    bool set_gain(int tenth_db) override {
        if (dev_ == nullptr) {
            return false;
        }
        int gains[256];
        const int n = rtlsdr_get_tuner_gains(dev_, gains);
        if (n <= 0) {
            return false;
        }
        int best = gains[0];
        for (int i = 0; i < n; ++i) {
            if (std::abs(gains[i] - tenth_db) < std::abs(best - tenth_db)) {
                best = gains[i];
            }
        }
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (rtlsdr_set_tuner_gain(dev_, best) == 0) {
                gain_.store(best, std::memory_order_relaxed);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    SdrInfo info() const override {
        SdrInfo info;
        info.driver = "rtlsdr";
        info.name = "RTL-SDR";
        info.serial = cfg_.serial;
        info.min_sample_rate = kSampleRates[0];
        info.max_sample_rate = kSampleRates[sizeof(kSampleRates) /
                                             sizeof(kSampleRates[0]) - 1];
        info.has_bias_tee = true;
        info.available = true;
        if (dev_ != nullptr) {
            int gains[256];
            const int n = rtlsdr_get_tuner_gains(dev_, gains);
            for (int i = 0; i < n; ++i) {
                info.gains_tenth_db.push_back(gains[i]);
            }
        }
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
    static void trampoline(unsigned char* buf, uint32_t len, void* ctx) {
        static_cast<RtlsdrSource*>(ctx)->on_samples(buf, len);
    }

    void on_samples(unsigned char* buf, uint32_t len) {
        const std::size_t n = len / 2;
        if (conv_.size() < n) {
            conv_.resize(n);
        }
        for (std::size_t i = 0; i < n; ++i) {
            conv_[i] = std::complex<float>(
                (static_cast<float>(buf[2 * i]) - 127.4f) / 128.0f,
                (static_cast<float>(buf[2 * i + 1]) - 127.4f) / 128.0f);
        }
        samples_.fetch_add(n, std::memory_order_relaxed);
        if (sink_) {
            sink_(conv_.data(), n);
        }
    }

    rtlsdr_dev_t* dev_ = nullptr;
    SdrConfig cfg_;
    IqSink sink_;
    std::thread thread_;
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

#endif  // ED_HAVE_RTLSDR

std::unique_ptr<ISdrSource> make_rtlsdr() {
#ifdef ED_HAVE_RTLSDR
    return std::make_unique<RtlsdrSource>();
#else
    return nullptr;
#endif
}

bool rtlsdr_available() {
#ifdef ED_HAVE_RTLSDR
    return true;
#else
    return false;
#endif
}

std::vector<SdrInfo> probe_rtlsdr() {
    std::vector<SdrInfo> out;
#ifdef ED_HAVE_RTLSDR
    const uint32_t count = rtlsdr_get_device_count();
    for (uint32_t i = 0; i < count; ++i) {
        SdrInfo info;
        info.driver = "rtlsdr";
        info.name = rtlsdr_get_device_name(i);
        char manufacturer[256] = {0};
        char product[256] = {0};
        char serial[256] = {0};
        rtlsdr_get_device_usb_strings(i, manufacturer, product, serial);
        info.serial = serial;
        info.min_sample_rate = kSampleRates[0];
        info.max_sample_rate = kSampleRates[sizeof(kSampleRates) /
                                             sizeof(kSampleRates[0]) - 1];
        info.has_bias_tee = true;
        info.available = true;

        rtlsdr_dev_t* dev = nullptr;
        if (rtlsdr_open(&dev, i) == 0 && dev != nullptr) {
            int gains[256];
            const int n = rtlsdr_get_tuner_gains(dev, gains);
            for (int g = 0; g < n; ++g) {
                info.gains_tenth_db.push_back(gains[g]);
            }
            rtlsdr_close(dev);
        }
        out.push_back(std::move(info));
    }
#endif
    return out;
}

}  // namespace ed::sdr
