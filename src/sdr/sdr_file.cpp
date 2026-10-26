// ElektroDump — file SDR backend (hardware-free testing).
// Streams raw IQ from a file, looping at EOF, throttled to real time. Formats:
// "int8" (RTL-SDR uint8 pairs) and "float32" (interleaved complex<float>).
// Always compiled in; useful for replaying fixtures through the waterfall.
#include "sdr/sdr_source.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "core/logging.h"
#include "core/thread_util.h"

namespace ed::sdr {

namespace {

constexpr std::size_t kBlock = 16384;

}  // namespace

class FileSource : public ISdrSource {
public:
    ~FileSource() override {
        stop();
        if (file_ != nullptr) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

    bool open(const SdrConfig& cfg) override {
        cfg_ = cfg;
        if (cfg.file_path.empty()) {
            last_error_ = "file backend requires sdr.file_path";
            return false;
        }
        file_ = std::fopen(cfg.file_path.c_str(), "rb");
        if (file_ == nullptr) {
            last_error_ = "cannot open IQ file: " + cfg.file_path;
            return false;
        }
        center_.store(cfg.center_freq_hz, std::memory_order_relaxed);
        rate_.store(cfg.sample_rate_hz, std::memory_order_relaxed);
        gain_.store(cfg.gain_tenth_db, std::memory_order_relaxed);
        bias_.store(cfg.bias_tee, std::memory_order_relaxed);
        return true;
    }

    bool start(IqSink sink) override {
        if (file_ == nullptr) {
            return false;
        }
        sink_ = std::move(sink);
        running_.store(true, std::memory_order_relaxed);
        thread_ = std::thread([this] { loop(); });
        return true;
    }

    void stop() override {
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

    bool retune(uint64_t freq_hz) override {
        center_.store(freq_hz, std::memory_order_relaxed);
        return true;
    }

    bool set_gain(int tenth_db) override {
        gain_.store(tenth_db, std::memory_order_relaxed);
        return true;
    }

    SdrInfo info() const override {
        SdrInfo info;
        info.driver = "file";
        info.name = "IQ file (" + cfg_.file_path + ")";
        info.serial = "file-0";
        info.min_sample_rate = 250000;
        info.max_sample_rate = 10000000;
        info.has_bias_tee = false;
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
    void loop() {
        thread::set_name("sdr-file");
        const bool float32 = cfg_.file_format == "float32";
        const std::size_t bytes_per_sample = float32 ? 8 : 2;
        const double fs = static_cast<double>(cfg_.sample_rate_hz);
        const auto period = std::chrono::nanoseconds(
            static_cast<long long>(1e9 * static_cast<double>(kBlock) / fs));

        std::vector<unsigned char> raw(kBlock * bytes_per_sample);
        std::vector<std::complex<float>> buf(kBlock);

        while (running_.load(std::memory_order_relaxed)) {
            const auto t0 = std::chrono::steady_clock::now();
            const std::size_t got =
                std::fread(raw.data(), 1, raw.size(), file_);
            if (got < bytes_per_sample) {
                std::fseek(file_, 0, SEEK_SET);
                continue;
            }
            const std::size_t n = got / bytes_per_sample;
            if (float32) {
                std::memcpy(buf.data(), raw.data(),
                            n * sizeof(std::complex<float>));
            } else {
                for (std::size_t i = 0; i < n; ++i) {
                    buf[i] = std::complex<float>(
                        (static_cast<float>(raw[2 * i]) - 127.4f) / 128.0f,
                        (static_cast<float>(raw[2 * i + 1]) - 127.4f) / 128.0f);
                }
            }
            samples_.fetch_add(n, std::memory_order_relaxed);
            if (sink_) {
                sink_(buf.data(), n);
            }
            const auto elapsed = std::chrono::steady_clock::now() - t0;
            if (elapsed < period) {
                std::this_thread::sleep_for(period - elapsed);
            }
        }
    }

    SdrConfig cfg_;
    IqSink sink_;
    std::FILE* file_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> samples_{0};
    std::atomic<uint64_t> overflows_{0};
    std::atomic<uint64_t> center_{0};
    std::atomic<uint32_t> rate_{0};
    std::atomic<int> gain_{0};
    std::atomic<bool> bias_{false};
    std::string last_error_;
};

std::unique_ptr<ISdrSource> make_file() {
    return std::make_unique<FileSource>();
}

std::vector<SdrInfo> probe_file() {
    FileSource source;
    return {source.info()};
}

}  // namespace ed::sdr
